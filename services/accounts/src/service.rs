//! The account rules (ADR-055), independent of HTTP. Every change is written to the store before it is
//! answered; a failed write changes nothing. Public answers do not say whether an email has an account.

use crate::policy;
use crate::secrets;
use crate::store::{self, Account, Challenge, Character, Session, Setup, Store};
use serde::Serialize;
use std::collections::HashMap;
use std::path::PathBuf;

pub const CODE_TTL_S: i64 = 30 * 60;
pub const CODE_ATTEMPTS: u32 = 5;
pub const RESEND_GAP_S: i64 = 60;
pub const RESENDS_PER_HOUR: usize = 5;
pub const SETUP_TTL_S: i64 = 30 * 60;
pub const SESSION_TTL_S: i64 = 12 * 3600;
pub const ADMISSION_TTL_S: i64 = 60;
pub const LOGIN_FAILURES: usize = 5;
pub const LOGIN_WINDOW_S: i64 = 15 * 60;

/// A refused request: the HTTP status and a stable code the client turns into a message.
#[derive(Debug, PartialEq, Eq)]
pub struct Refusal {
    pub status: u16,
    pub code: &'static str,
}

fn refuse<T>(status: u16, code: &'static str) -> Result<T, Refusal> {
    Err(Refusal { status, code })
}

pub type Outcome<T> = Result<T, Refusal>;

/// Where emails go. The development sender writes them to a folder (ADR-055); a real sender is a later choice.
pub trait Mailer: Send {
    fn send(&self, to: &str, subject: &str, body: &str) -> std::io::Result<()>;
}

#[derive(Serialize, Debug, PartialEq)]
pub struct CharacterView {
    pub id: String,
    pub first_name: String,
    pub last_name: String,
    pub model: String,
}

#[derive(Serialize, Debug, PartialEq)]
pub struct AccountView {
    pub username: String,
    pub character: Option<CharacterView>,
}

#[derive(Serialize, Debug, PartialEq)]
pub struct SignedIn {
    pub session: String,
    pub expires_in: i64,
    pub account: AccountView,
}

#[derive(Serialize, Debug, PartialEq)]
pub struct Ticket {
    pub ticket: String,
    pub expires_in: i64,
}

/// Who an admission ticket admits, for the game server only.
#[derive(Serialize, Debug, PartialEq)]
pub struct Admitted {
    pub account: String,
    pub character: CharacterView,
}

struct Admission {
    account: String,
    expires_at: i64,
}

pub struct Service {
    store: Store,
    path: PathBuf,
    mailer: Box<dyn Mailer>,
    models: Vec<String>,
    server_key: String,
    admissions: HashMap<String, Admission>,
    failures: HashMap<String, Vec<i64>>,
}

fn view(account: &Account) -> AccountView {
    AccountView {
        username: account.username.clone(),
        character: account.character.as_ref().map(|c| CharacterView {
            id: c.id.clone(),
            first_name: c.first_name.clone(),
            last_name: c.last_name.clone(),
            model: c.model.clone(),
        }),
    }
}

/// The resend limit: one code a minute and five an hour per address.
fn may_send(sent: &[i64], now: i64) -> bool {
    sent.last().is_none_or(|last| now - last >= RESEND_GAP_S) && sent.iter().filter(|t| now - **t < 3600).count() < RESENDS_PER_HOUR
}

impl Service {
    pub fn open(path: PathBuf, mailer: Box<dyn Mailer>, models: Vec<String>, server_key: String) -> Result<Self, String> {
        if server_key.len() < 32 {
            return Err("the server key must be at least 32 characters".into());
        }
        let store = Store::load(&path)?;
        Ok(Service { store, path, mailer, models, server_key, admissions: HashMap::new(), failures: HashMap::new() })
    }

    pub fn store(&self) -> &Store {
        &self.store
    }

    /// Applies a change to a copy, writes the copy, and only then makes it current.
    fn commit<T>(&mut self, now: i64, change: impl FnOnce(&mut Store) -> Outcome<T>) -> Outcome<T> {
        let mut next = self.store.clone();
        next.sweep(now);
        let result = change(&mut next)?;
        next.revision += 1;
        if store::write_durably(&self.path, &next).is_err() {
            return refuse(503, "store_unavailable");
        }
        self.store = next;
        Ok(result)
    }

    fn session_account(&self, token: &str, now: i64) -> Outcome<(String, &Account)> {
        let session = self.store.sessions.get(&secrets::verifier("session", token));
        match session.filter(|s| s.expires_at > now) {
            Some(session) => match self.store.accounts.get(&session.account) {
                Some(account) => Ok((session.account.clone(), account)),
                None => refuse(401, "session_invalid"),
            },
            None => refuse(401, "session_invalid"),
        }
    }

    /// Step 1: an email address. A new address gets a code; an address that already has an account gets a note
    /// saying so instead. Both answer the same.
    pub fn register(&mut self, raw_email: &str, now: i64) -> Outcome<()> {
        let email = policy::email(raw_email).or_else(|code| refuse(400, code))?;
        if let Some(existing) = self.store.registrations.get(&email) {
            if !may_send(&existing.sent, now) {
                return refuse(429, "resend_too_soon");
            }
        }
        let has_account = self.store.emails.contains_key(&email);
        let code = secrets::code();
        let verifier = if has_account { secrets::verifier(&email, &secrets::token()) } else { secrets::verifier(&email, &code) };
        self.commit(now, |store| {
            let entry = store.registrations.entry(email.clone()).or_insert(Challenge {
                verifier: String::new(), expires_at: 0, attempts: 0, sent: Vec::new(),
            });
            entry.verifier = verifier;
            entry.expires_at = now + CODE_TTL_S;
            entry.attempts = 0;
            entry.sent.retain(|t| now - t < 3600);
            entry.sent.push(now);
            Ok(())
        })?;
        let sent = if has_account {
            self.mailer.send(&email, "Your Marburgum II account",
                "Someone asked to create a Marburgum II account with this address, which already has one.\n\
                 If that was you, sign in with your username, or use \"Forgot password\" on the sign-in screen.\n\
                 If it was not you, you can ignore this message.\n")
        } else {
            self.mailer.send(&email, "Confirm your email for Marburgum II", &format!(
                "Your Marburgum II confirmation code is:\n\n    {code}\n\n\
                 Type it on the confirmation screen within 30 minutes. It works once.\n\
                 If you did not ask for a Marburgum II account, you can ignore this message.\n"))
        };
        sent.or_else(|_| refuse(503, "mail_unavailable"))
    }

    /// Step 2: the emailed code. A right code becomes a single-use setup ticket; five wrong ones spend the code.
    pub fn verify(&mut self, raw_email: &str, raw_code: &str, now: i64) -> Outcome<Ticket> {
        let email = policy::email(raw_email).or_else(|_| refuse(400, "code_invalid"))?;
        let code = secrets::normalise_code(raw_code);
        let Some(challenge) = self.store.registrations.get(&email).filter(|c| c.expires_at > now) else {
            return refuse(400, "code_invalid");
        };
        if challenge.attempts >= CODE_ATTEMPTS {
            return refuse(400, "code_invalid");
        }
        if !secrets::same(&challenge.verifier, &secrets::verifier(&email, &code)) {
            self.commit(now, |store| {
                if let Some(c) = store.registrations.get_mut(&email) {
                    c.attempts += 1;
                }
                Ok(())
            })?;
            return refuse(400, "code_invalid");
        }
        let ticket = secrets::token();
        let setup_verifier = secrets::verifier("setup", &ticket);
        self.commit(now, |store| {
            // Spent, but its resend history stays so the limit holds.
            if let Some(c) = store.registrations.get_mut(&email) {
                c.verifier = String::new();
                c.expires_at = now;
            }
            store.setups.insert(setup_verifier, Setup { email: email.clone(), expires_at: now + SETUP_TTL_S });
            Ok(())
        })?;
        Ok(Ticket { ticket, expires_in: SETUP_TTL_S })
    }

    /// Step 3: the verified address chooses a username and a password. Only now does the account exist.
    pub fn setup(&mut self, ticket: &str, raw_username: &str, password: &str, now: i64) -> Outcome<AccountView> {
        let setup_verifier = secrets::verifier("setup", ticket);
        let Some(setup) = self.store.setups.get(&setup_verifier).filter(|s| s.expires_at > now).cloned() else {
            return refuse(400, "setup_expired");
        };
        let username = policy::username(raw_username).or_else(|code| refuse(400, code))?;
        policy::password(password, &username, &setup.email).or_else(|code| refuse(400, code))?;
        if self.store.usernames.contains_key(&username) {
            return refuse(409, "username_taken");
        }
        if self.store.emails.contains_key(&setup.email) {
            return refuse(400, "setup_expired");
        }
        let phc = secrets::hash_password(password);
        let id = secrets::id("a_");
        let account = Account { email: setup.email.clone(), username: username.clone(), password: phc, created_at: now, character: None };
        let answer = view(&account);
        self.commit(now, |store| {
            store.setups.remove(&setup_verifier);
            store.registrations.remove(&setup.email);
            store.usernames.insert(username, id.clone());
            store.emails.insert(setup.email.clone(), id.clone());
            store.accounts.insert(id, account);
            Ok(())
        })?;
        Ok(answer)
    }

    /// Sign-in. Five wrong passwords in fifteen minutes lock the username for the rest of the window.
    pub fn login(&mut self, raw_username: &str, password: &str, now: i64) -> Outcome<SignedIn> {
        let username = raw_username.trim().to_lowercase();
        let failures = self.failures.entry(username.clone()).or_default();
        failures.retain(|t| now - t < LOGIN_WINDOW_S);
        if failures.len() >= LOGIN_FAILURES {
            return refuse(429, "too_many_attempts");
        }
        let account_id = self.store.usernames.get(&username).cloned();
        let accepted = match account_id.as_ref().and_then(|id| self.store.accounts.get(id)) {
            Some(account) => secrets::verify_password(password, &account.password),
            None => {
                secrets::burn_password_check(password);
                false
            }
        };
        let Some(account_id) = account_id.filter(|_| accepted) else {
            self.failures.entry(username).or_default().push(now);
            return refuse(401, "credentials_invalid");
        };
        self.failures.remove(&username);
        let token = secrets::token();
        let session_verifier = secrets::verifier("session", &token);
        self.commit(now, |store| {
            store.sessions.insert(session_verifier, Session { account: account_id.clone(), expires_at: now + SESSION_TTL_S });
            Ok(())
        })?;
        let account = view(&self.store.accounts[&account_id]);
        Ok(SignedIn { session: token, expires_in: SESSION_TTL_S, account })
    }

    pub fn me(&self, token: &str, now: i64) -> Outcome<AccountView> {
        self.session_account(token, now).map(|(_, account)| view(account))
    }

    pub fn logout(&mut self, token: &str, now: i64) -> Outcome<()> {
        let session_verifier = secrets::verifier("session", token);
        if !self.store.sessions.contains_key(&session_verifier) {
            return Ok(());
        }
        self.commit(now, |store| {
            store.sessions.remove(&session_verifier);
            Ok(())
        })
    }

    /// The first and only character (one per account for now). The same request id answers the same character
    /// again, so a double click or a retried request cannot make a second one.
    pub fn create_character(&mut self, token: &str, request: &str, first: &str, last: &str, model: &str, now: i64)
        -> Outcome<AccountView>
    {
        let (account_id, account) = self.session_account(token, now)?;
        if request.is_empty() || request.len() > 64 {
            return refuse(400, "request_invalid");
        }
        if let Some(existing) = &account.character {
            return if existing.request == request { Ok(view(account)) } else { refuse(409, "character_exists") };
        }
        let first_name = policy::character_name(first).map_err(|code| Refusal { status: 400, code: field("first", code) })?;
        let last_name = policy::character_name(last).map_err(|code| Refusal { status: 400, code: field("last", code) })?;
        if !self.models.iter().any(|m| m == model) {
            return refuse(400, "model_unknown");
        }
        let character = Character {
            id: secrets::id("c_"), first_name, last_name, model: model.to_string(), created_at: now, request: request.to_string(),
        };
        self.commit(now, |store| {
            store.accounts.get_mut(&account_id).expect("account").character = Some(character);
            Ok(())
        })?;
        Ok(view(&self.store.accounts[&account_id]))
    }

    /// A single-use, one-minute ticket that lets this account's character into a game server.
    pub fn admission(&mut self, token: &str, now: i64) -> Outcome<Ticket> {
        let (account_id, account) = self.session_account(token, now)?;
        if account.character.is_none() {
            return refuse(409, "no_character");
        }
        self.admissions.retain(|_, a| a.expires_at > now);
        let ticket = secrets::token();
        self.admissions.insert(secrets::verifier("admission", &ticket), Admission { account: account_id, expires_at: now + ADMISSION_TTL_S });
        Ok(Ticket { ticket, expires_in: ADMISSION_TTL_S })
    }

    /// The game server spends a ticket. Only a caller holding the server key may.
    pub fn redeem(&mut self, server_key: &str, ticket: &str, now: i64) -> Outcome<Admitted> {
        if !secrets::same(server_key, &self.server_key) {
            return refuse(401, "server_key_invalid");
        }
        let Some(admission) = self.admissions.remove(&secrets::verifier("admission", ticket)).filter(|a| a.expires_at > now) else {
            return refuse(403, "ticket_invalid");
        };
        let Some(account) = self.store.accounts.get(&admission.account) else {
            return refuse(403, "ticket_invalid");
        };
        let Some(character) = view(account).character else {
            return refuse(403, "ticket_invalid");
        };
        Ok(Admitted { account: admission.account, character })
    }

    /// "Forgot password": a code to the address if it has an account; the same answer either way.
    pub fn forgot(&mut self, raw_email: &str, now: i64) -> Outcome<()> {
        let Ok(email) = policy::email(raw_email) else {
            return Ok(());
        };
        let Some(account_id) = self.store.emails.get(&email).cloned() else {
            return Ok(());
        };
        if self.store.resets.get(&account_id).is_some_and(|c| !may_send(&c.sent, now)) {
            return Ok(());
        }
        let code = secrets::code();
        let verifier = secrets::verifier(&email, &code);
        self.commit(now, |store| {
            let entry = store.resets.entry(account_id.clone()).or_insert(Challenge {
                verifier: String::new(), expires_at: 0, attempts: 0, sent: Vec::new(),
            });
            entry.verifier = verifier;
            entry.expires_at = now + CODE_TTL_S;
            entry.attempts = 0;
            entry.sent.retain(|t| now - t < 3600);
            entry.sent.push(now);
            Ok(())
        })?;
        let username = self.store.accounts[&account_id].username.clone();
        self.mailer.send(&email, "Reset your Marburgum II password", &format!(
            "Your Marburgum II username is {username}.\nYour password reset code is:\n\n    {code}\n\n\
             Type it on the reset screen within 30 minutes, with your new password. It works once.\n\
             If you did not ask for this, you can ignore this message; your password has not changed.\n"))
            .or_else(|_| refuse(503, "mail_unavailable"))
    }

    /// A right reset code sets the new password and signs every session out.
    pub fn reset(&mut self, raw_email: &str, raw_code: &str, password: &str, now: i64) -> Outcome<()> {
        let email = policy::email(raw_email).or_else(|_| refuse(400, "code_invalid"))?;
        let code = secrets::normalise_code(raw_code);
        let Some(account_id) = self.store.emails.get(&email).cloned() else {
            return refuse(400, "code_invalid");
        };
        let Some(challenge) = self.store.resets.get(&account_id).filter(|c| c.expires_at > now && c.attempts < CODE_ATTEMPTS) else {
            return refuse(400, "code_invalid");
        };
        if !secrets::same(&challenge.verifier, &secrets::verifier(&email, &code)) {
            self.commit(now, |store| {
                if let Some(c) = store.resets.get_mut(&account_id) {
                    c.attempts += 1;
                }
                Ok(())
            })?;
            return refuse(400, "code_invalid");
        }
        let username = self.store.accounts[&account_id].username.clone();
        policy::password(password, &username, &email).or_else(|code| refuse(400, code))?;
        let phc = secrets::hash_password(password);
        self.commit(now, |store| {
            store.resets.remove(&account_id);
            store.sessions.retain(|_, s| s.account != account_id);
            store.accounts.get_mut(&account_id).expect("account").password = phc;
            Ok(())
        })?;
        self.failures.remove(&username);
        Ok(())
    }
}

/// "first_name_length", "last_name_characters" ...: which field a name refusal is about.
fn field(which: &str, code: &'static str) -> &'static str {
    match (which, code) {
        ("first", "name_length") => "first_name_length",
        ("first", "name_characters") => "first_name_characters",
        ("first", "name_reserved") => "first_name_reserved",
        ("last", "name_length") => "last_name_length",
        ("last", "name_characters") => "last_name_characters",
        ("last", "name_reserved") => "last_name_reserved",
        _ => code,
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// Keeps every email, so a test can read the code a player would.
    #[derive(Clone, Default)]
    pub struct Inbox(pub Arc<Mutex<Vec<(String, String, String)>>>);

    impl Mailer for Inbox {
        fn send(&self, to: &str, subject: &str, body: &str) -> std::io::Result<()> {
            self.0.lock().unwrap().push((to.into(), subject.into(), body.into()));
            Ok(())
        }
    }

    impl Inbox {
        pub fn last_code(&self) -> String {
            let mails = self.0.lock().unwrap();
            let body = &mails.last().expect("a mail").2;
            body.lines().map(str::trim).find(|l| l.len() == secrets::CODE_LENGTH && l.chars().all(|c| c.is_ascii_alphanumeric()))
                .expect("a code").to_string()
        }
        pub fn count(&self) -> usize {
            self.0.lock().unwrap().len()
        }
    }

    pub const KEY: &str = "0123456789abcdef0123456789abcdef";

    pub fn service(dir: &std::path::Path) -> (Service, Inbox) {
        let inbox = Inbox::default();
        let models = vec!["protagonist".to_string(), "manny".into(), "quinn".into()];
        (Service::open(dir.join("accounts.json"), Box::new(inbox.clone()), models, KEY.into()).unwrap(), inbox)
    }

    /// Registers, verifies and sets up "ana_k", then signs in.
    pub fn signed_in(service: &mut Service, inbox: &Inbox) -> String {
        service.register("Ana@Example.si", 0).unwrap();
        let ticket = service.verify("ana@example.si", &inbox.last_code().to_lowercase(), 10).unwrap().ticket;
        service.setup(&ticket, "Ana_K", "drava-bridge-1990", 20).unwrap();
        service.login("ana_k", "drava-bridge-1990", 30).unwrap().session
    }

    fn code_of(result: Outcome<impl std::fmt::Debug>) -> &'static str {
        result.unwrap_err().code
    }

    #[test]
    fn a_new_player_from_email_to_admission() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        let me = service.me(&session, 40).unwrap();
        assert_eq!(me, AccountView { username: "ana_k".into(), character: None });
        assert_eq!(code_of(service.admission(&session, 40)), "no_character");
        let made = service.create_character(&session, "req-1", " Žiga ", "Kos", "quinn", 50).unwrap();
        let character = made.character.expect("character");
        assert_eq!((character.first_name.as_str(), character.last_name.as_str(), character.model.as_str()), ("Žiga", "Kos", "quinn"));
        let ticket = service.admission(&session, 60).unwrap().ticket;
        assert_eq!(code_of(service.redeem("wrong-key-wrong-key-wrong-key-xx", &ticket, 61)), "server_key_invalid");
        let admitted = service.redeem(KEY, &ticket, 61).unwrap();
        assert_eq!(admitted.character.id, character.id);
        assert_eq!(code_of(service.redeem(KEY, &ticket, 62)), "ticket_invalid", "single use");
        let late = service.admission(&session, 70).unwrap().ticket;
        assert_eq!(code_of(service.redeem(KEY, &late, 70 + ADMISSION_TTL_S)), "ticket_invalid", "expired");
        // Nothing secret is in the store: no password, code, session or ticket in the clear.
        let text = std::fs::read_to_string(dir.path().join("accounts.json")).unwrap();
        for secret in ["drava-bridge-1990", session.as_str(), ticket.as_str()] {
            assert!(!text.contains(secret));
        }
    }

    #[test]
    fn a_returning_player_survives_a_restart() {
        let dir = tempfile::tempdir().unwrap();
        let (mut first, inbox) = service(dir.path());
        let session = signed_in(&mut first, &inbox);
        first.create_character(&session, "req-1", "Ana", "Kos", "protagonist", 50).unwrap();
        drop(first);
        let (mut again, _) = service(dir.path());
        assert_eq!(again.me(&session, 60).unwrap().character.unwrap().first_name, "Ana");
        let fresh = again.login("ANA_K", "drava-bridge-1990", 70).unwrap();
        assert_eq!(fresh.account.character.unwrap().last_name, "Kos");
    }

    #[test]
    fn codes_are_single_use_limited_and_neutral() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        assert_eq!(code_of(service.register("not-an-email", 0)), "email_invalid");
        service.register("ana@example.si", 0).unwrap();
        assert_eq!(code_of(service.register("ana@example.si", 30)), "resend_too_soon");
        let code = inbox.last_code();
        for attempt in 0..CODE_ATTEMPTS {
            assert_eq!(code_of(service.verify("ana@example.si", "WRONG234", 40 + attempt as i64)), "code_invalid");
        }
        assert_eq!(code_of(service.verify("ana@example.si", &code, 50)), "code_invalid", "spent by guessing");
        service.register("ana@example.si", 100).unwrap();
        let code = inbox.last_code();
        assert_eq!(code_of(service.verify("ana@example.si", &code, 100 + CODE_TTL_S)), "code_invalid", "expired");
        service.register("ana@example.si", 200).unwrap();
        let code = inbox.last_code();
        let ticket = service.verify("ana@example.si", &code, 210).unwrap().ticket;
        assert_eq!(code_of(service.verify("ana@example.si", &code, 211)), "code_invalid", "used");
        service.setup(&ticket, "ana_k", "drava-bridge-1990", 220).unwrap();
        assert_eq!(code_of(service.setup(&ticket, "ana_k2", "drava-bridge-1990", 221)), "setup_expired", "used");
        // The same address again: same answer, a note instead of a code, and no way in with any code.
        let mails = inbox.count();
        service.register("ana@example.si", 400).unwrap();
        assert_eq!(inbox.count(), mails + 1);
        assert!(inbox.0.lock().unwrap().last().unwrap().2.contains("already has one"));
        // Five codes an hour at most.
        for minute in 1..RESENDS_PER_HOUR as i64 {
            service.register("bor@example.si", minute * 60).unwrap();
        }
        service.register("bor@example.si", 5 * 60).unwrap();
        assert_eq!(code_of(service.register("bor@example.si", 6 * 60)), "resend_too_soon");
        service.register("bor@example.si", 3600 + 60).unwrap();
    }

    #[test]
    fn setup_refuses_taken_and_bad_credentials() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        signed_in(&mut service, &inbox);
        service.register("bor@example.si", 0).unwrap();
        let ticket = service.verify("bor@example.si", &inbox.last_code(), 1).unwrap().ticket;
        assert_eq!(code_of(service.setup(&ticket, "ANA_K", "drava-bridge-1990", 2)), "username_taken");
        assert_eq!(code_of(service.setup(&ticket, "bo", "drava-bridge-1990", 2)), "username_length");
        assert_eq!(code_of(service.setup(&ticket, "bor", "short", 2)), "password_length");
        assert_eq!(code_of(service.setup("forged", "bor", "drava-bridge-1990", 2)), "setup_expired");
        assert_eq!(code_of(service.setup(&ticket, "bor", "drava-bridge-1990", 2 + SETUP_TTL_S)), "setup_expired");
    }

    #[test]
    fn sign_in_failures_lock_and_unknown_names_answer_alike() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        signed_in(&mut service, &inbox);
        assert_eq!(code_of(service.login("nobody", "drava-bridge-1990", 100)), "credentials_invalid");
        for i in 0..LOGIN_FAILURES as i64 {
            assert_eq!(code_of(service.login("ana_k", "wrong-password", 100 + i)), "credentials_invalid");
        }
        assert_eq!(code_of(service.login("ana_k", "drava-bridge-1990", 200)), "too_many_attempts");
        assert!(service.login("ana_k", "drava-bridge-1990", 100 + LOGIN_WINDOW_S).is_ok());
    }

    #[test]
    fn one_character_and_an_idempotent_create() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        assert_eq!(code_of(service.create_character("forged", "r", "Ana", "Kos", "quinn", 40)), "session_invalid");
        assert_eq!(code_of(service.create_character(&session, "r", "A", "Kos", "quinn", 40)), "first_name_length");
        assert_eq!(code_of(service.create_character(&session, "r", "Ana", "K0s", "quinn", 40)), "last_name_characters");
        assert_eq!(code_of(service.create_character(&session, "r", "Ana", "Kos", "/Game/Evil.Evil", 40)), "model_unknown");
        assert_eq!(code_of(service.create_character(&session, "", "Ana", "Kos", "quinn", 40)), "request_invalid");
        let revision = service.store().revision;
        let made = service.create_character(&session, "r1", "Ana", "Kos", "quinn", 41).unwrap();
        let again = service.create_character(&session, "r1", "Ana", "Kos", "quinn", 42).unwrap();
        assert_eq!(made, again);
        assert_eq!(service.store().revision, revision + 1, "the retry wrote nothing");
        assert_eq!(code_of(service.create_character(&session, "r2", "Eva", "Kos", "manny", 43)), "character_exists");
    }

    #[test]
    fn reset_changes_the_password_and_signs_everyone_out() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        let mails = inbox.count();
        service.forgot("nobody@example.si", 100).unwrap();
        assert_eq!(inbox.count(), mails, "no account, no mail, same answer");
        service.forgot("ANA@example.si", 100).unwrap();
        let code = inbox.last_code();
        assert!(inbox.0.lock().unwrap().last().unwrap().2.contains("username is ana_k"));
        assert_eq!(code_of(service.reset("ana@example.si", &code, "short", 110)), "password_length");
        service.reset("ana@example.si", &code, "pohorje-snow-1991", 120).unwrap();
        assert_eq!(code_of(service.me(&session, 130)), "session_invalid");
        assert_eq!(code_of(service.reset("ana@example.si", &code, "pohorje-snow-1992", 130)), "code_invalid", "used");
        assert_eq!(code_of(service.login("ana_k", "drava-bridge-1990", 140)), "credentials_invalid");
        assert!(service.login("ana_k", "pohorje-snow-1991", 150).is_ok());
    }

    #[test]
    fn a_failed_write_changes_nothing() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        let before = service.store().clone();
        // A directory where the temporary file must go makes every write fail.
        std::fs::create_dir(dir.path().join("accounts.json.tmp")).unwrap();
        assert_eq!(code_of(service.create_character(&session, "r", "Ana", "Kos", "quinn", 50)), "store_unavailable");
        assert_eq!(service.store(), &before);
        assert!(service.me(&session, 60).unwrap().character.is_none());
    }
}
