//! The account rules (ADR-055), independent of HTTP. Every change is written to the store before it is
//! answered; a failed write changes nothing. Public answers do not say whether an email has an account.

use crate::policy;
use crate::secrets;
use crate::servers::GameServer;
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
/// Outstanding admission tickets per account; a newer one replaces the oldest.
pub const ADMISSIONS_PER_ACCOUNT: usize = 3;
/// How often its server may redeem one ticket within its minute (ADR-086). The ticket travels in the clear in the
/// engine's hello, so anyone on the path can redeem it first; without the connection key that admits nobody, and the
/// player's own hello must still work. Admission needs the key, so the ticket alone is no longer single-use.
pub const REDEEMS_PER_TICKET: u32 = 3;
/// Live sessions per account; a new sign-in ends the oldest. Unbounded, one account signing in again and again grew
/// the store, which every commit rewrites under the lock, until redeems timed out (QA 2026-11-19, AR3).
pub const SESSIONS_PER_ACCOUNT: usize = 5;
/// How often expired sign-in failure records are dropped.
const FAILURE_SWEEP_S: i64 = 60;

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

/// A world-admission ticket and its connection key.
#[derive(Serialize, Debug, PartialEq)]
pub struct AdmissionTicket {
    pub ticket: String,
    /// The connection's AES-256-GCM key, 32 random bytes in URL-safe base64. The client keeps it; the ticket travels
    /// in the clear in the engine's hello, and the key never does (ADR-083).
    pub key: String,
    pub expires_in: i64,
}

/// Who an admission ticket admits, for the game server only.
#[derive(Serialize, Debug, PartialEq)]
pub struct Admitted {
    pub account: String,
    pub character: CharacterView,
    /// The ticket's connection key, for the game server to encrypt that connection with (ADR-083).
    pub key: String,
}

struct Admission {
    account: String,
    /// The game server the ticket was asked for; only its key spends it (AR1).
    server: String,
    /// Held in memory only, like the ticket; handed to the client, and to its server on each redeem.
    key: String,
    redeems: u32,
    issued_at: i64,
    expires_at: i64,
}

/// A sign-in between its two locked steps. The password is checked in between, without the service's lock, so a
/// burst of sign-ins cannot hold up every other request, the game server's redeem included.
pub struct LoginCheck {
    username: Option<String>,
    account: Option<String>,
    phc: Option<String>,
}

/// A reset whose code was right, waiting for its new password's hash (see `reset_begin`).
pub struct ResetCheck {
    account: String,
    username: String,
    verifier: String,
}

impl std::fmt::Debug for LoginCheck {
    /// Without the stored hash.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LoginCheck").field("username", &self.username).field("account", &self.account).finish_non_exhaustive()
    }
}

impl LoginCheck {
    /// The stored hash to check the password against; none means burn the same work and refuse.
    pub fn phc(&self) -> Option<&str> {
        self.phc.as_deref()
    }
}

/// The password check between `login_begin` and `login_finish`: the real one, or the same work for nobody.
pub fn check_password(password: &str, phc: Option<&str>) -> bool {
    match phc {
        Some(phc) => secrets::verify_password(password, phc),
        None => {
            secrets::burn_password_check(password);
            false
        }
    }
}

pub struct Service {
    store: Store,
    path: PathBuf,
    mailer: Box<dyn Mailer>,
    models: Vec<String>,
    servers: Vec<GameServer>,
    admissions: HashMap<String, Admission>,
    failures: HashMap<String, Vec<i64>>,
    failures_swept_at: i64,
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
    pub fn open(path: PathBuf, mailer: Box<dyn Mailer>, models: Vec<String>, servers: Vec<GameServer>) -> Result<Self, String> {
        crate::servers::check(&servers)?;
        let store = Store::load(&path)?;
        Ok(Service {
            store, path, mailer, models, servers, admissions: HashMap::new(), failures: HashMap::new(), failures_swept_at: 0,
        })
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
    #[cfg(test)]
    pub fn setup(&mut self, ticket: &str, raw_username: &str, password: &str, now: i64) -> Outcome<AccountView> {
        self.setup_check(ticket, raw_username, password, now)?;
        self.setup_commit(ticket, raw_username, password, secrets::hash_password(password), now)
    }

    /// Step 3, first part: everything `setup_commit` would refuse, without hashing. The HTTP route hashes between
    /// the two, outside the lock (QA 2026-11-19, AR8).
    pub fn setup_check(&self, ticket: &str, raw_username: &str, password: &str, now: i64) -> Outcome<(String, Setup, String)> {
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
        Ok((setup_verifier, setup, username))
    }

    /// Step 3, last part: the checks again, since the lock was let go while hashing, then the account.
    pub fn setup_commit(&mut self, ticket: &str, raw_username: &str, password: &str, phc: String, now: i64) -> Outcome<AccountView> {
        let (setup_verifier, setup, username) = self.setup_check(ticket, raw_username, password, now)?;
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

    /// Sign-in, in one call: `login_begin`, the password check and `login_finish`. The HTTP route runs the three
    /// separately so the check happens outside the lock.
    #[cfg(test)]
    pub fn login(&mut self, raw_username: &str, password: &str, now: i64) -> Outcome<SignedIn> {
        let check = self.login_begin(raw_username, now)?;
        let accepted = check_password(password, check.phc());
        self.login_finish(check, accepted, now)
    }

    /// Drops sign-in failure records older than the window, at most once a minute.
    fn sweep_failures(&mut self, now: i64) {
        if now - self.failures_swept_at < FAILURE_SWEEP_S {
            return;
        }
        self.failures_swept_at = now;
        self.failures.retain(|_, times| {
            times.retain(|t| now - t < LOGIN_WINDOW_S);
            !times.is_empty()
        });
    }

    /// Sign-in, first step. Five failed sign-ins in fifteen minutes lock the username for the rest of the window.
    /// A name no account could have is not recorded at all, so made-up names cannot grow the service's memory.
    /// The attempt counts as a failure until `login_finish` proves the password, so concurrent guesses cannot
    /// slip past the limit while their checks run.
    pub fn login_begin(&mut self, raw_username: &str, now: i64) -> Outcome<LoginCheck> {
        let Ok(username) = policy::username(raw_username) else {
            return Ok(LoginCheck { username: None, account: None, phc: None });
        };
        self.sweep_failures(now);
        let failures = self.failures.entry(username.clone()).or_default();
        failures.retain(|t| now - t < LOGIN_WINDOW_S);
        if failures.len() >= LOGIN_FAILURES {
            return refuse(429, "too_many_attempts");
        }
        failures.push(now);
        let account = self.store.usernames.get(&username).cloned();
        let phc = account.as_ref().and_then(|id| self.store.accounts.get(id)).map(|a| a.password.clone());
        Ok(LoginCheck { username: Some(username), account, phc })
    }

    /// Sign-in, last step: a proven password opens a session and clears the username's failures.
    pub fn login_finish(&mut self, check: LoginCheck, accepted: bool, now: i64) -> Outcome<SignedIn> {
        let Some(username) = check.username else {
            return refuse(401, "credentials_invalid");
        };
        // A reset between the steps changes the hash; only the hash that was checked counts.
        let current = check.account.as_ref().and_then(|id| self.store.accounts.get(id)).map(|a| &a.password);
        let Some(account_id) = check.account.filter(|_| accepted && current.is_some() && current == check.phc.as_ref()) else {
            return refuse(401, "credentials_invalid");
        };
        self.failures.remove(&username);
        let token = secrets::token();
        let session_verifier = secrets::verifier("session", &token);
        self.commit(now, |store| {
            // At most SESSIONS_PER_ACCOUNT: the oldest go, in the same commit as the new one.
            let mut own: Vec<(String, i64)> =
                store.sessions.iter().filter(|(_, s)| s.account == account_id).map(|(v, s)| (v.clone(), s.expires_at)).collect();
            if own.len() >= SESSIONS_PER_ACCOUNT {
                own.sort_by_key(|(_, expires)| *expires);
                for (verifier, _) in &own[..=own.len() - SESSIONS_PER_ACCOUNT] {
                    store.sessions.remove(verifier);
                }
            }
            store.sessions.insert(session_verifier, Session { account: account_id.clone(), expires_at: now + SESSION_TTL_S });
            Ok(())
        })?;
        let account = view(&self.store.accounts[&account_id]);
        Ok(SignedIn { session: token, expires_in: SESSION_TTL_S, account })
    }

    pub fn me(&self, token: &str, now: i64) -> Outcome<AccountView> {
        self.session_account(token, now).map(|(_, account)| view(account))
    }

    /// Signing out also spends the account's outstanding admission tickets (QA 2026-11-19, AR10).
    pub fn logout(&mut self, token: &str, now: i64) -> Outcome<()> {
        let session_verifier = secrets::verifier("session", token);
        let Some(account_id) = self.store.sessions.get(&session_verifier).map(|s| s.account.clone()) else {
            return Ok(());
        };
        self.commit(now, |store| {
            store.sessions.remove(&session_verifier);
            Ok(())
        })?;
        self.admissions.retain(|_, a| a.account != account_id);
        Ok(())
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

    /// A single-use, one-minute ticket that lets this account's character into the game server at `address`, the
    /// address the client is about to join. Only listed servers get tickets, so a player lured to another address
    /// has nothing to hand over, and a ticket is spendable only by the server it names (QA 2026-11-19, AR1).
    pub fn admission(&mut self, token: &str, address: &str, now: i64) -> Outcome<AdmissionTicket> {
        let (account_id, account) = self.session_account(token, now)?;
        if account.character.is_none() {
            return refuse(409, "no_character");
        }
        let address = policy::server_address(address).or_else(|code| refuse(403, code))?;
        let Some(server) = self.servers.iter().find(|s| s.addresses.contains(&address)).map(|s| s.id.clone()) else {
            return refuse(403, "server_unknown");
        };
        self.admissions.retain(|_, a| a.expires_at > now);
        // A few outstanding tickets per account at most: a newer one replaces the oldest.
        let mut outstanding: Vec<(String, i64)> =
            self.admissions.iter().filter(|(_, a)| a.account == account_id).map(|(v, a)| (v.clone(), a.issued_at)).collect();
        if outstanding.len() >= ADMISSIONS_PER_ACCOUNT {
            outstanding.sort_by_key(|(_, issued)| *issued);
            for (verifier, _) in &outstanding[..=outstanding.len() - ADMISSIONS_PER_ACCOUNT] {
                self.admissions.remove(verifier);
            }
        }
        let (ticket, key) = (secrets::token(), secrets::token());
        self.admissions.insert(secrets::verifier("admission", &ticket),
            Admission { account: account_id, server, key: key.clone(), redeems: 0, issued_at: now, expires_at: now + ADMISSION_TTL_S });
        Ok(AdmissionTicket { ticket, key, expires_in: ADMISSION_TTL_S })
    }

    /// A game server redeems a ticket for its connection key; the server's own key says which server it is. A ticket
    /// asked for another server is refused and left as it was (AR1). Its own server may redeem it up to
    /// REDEEMS_PER_TICKET times within its minute: a stolen ticket redeemed first, without its key, admits nobody and
    /// does not lock the player out (ADR-086). Signing out or a password reset still ends it at once (AR10).
    pub fn redeem(&mut self, server_key: &str, ticket: &str, now: i64) -> Outcome<Admitted> {
        // Every key is compared, to the end, so the time taken does not say which one came close.
        let caller = self.servers.iter().fold(None, |found, s| if secrets::same(server_key, &s.key) { Some(s.id.clone()) } else { found });
        let Some(caller) = caller else {
            return refuse(401, "server_key_invalid");
        };
        let verifier = secrets::verifier("admission", ticket);
        match self.admissions.get(&verifier) {
            Some(a) if a.expires_at > now && a.server == caller => {}
            Some(a) if a.expires_at > now => {
                eprintln!("mb2-accounts event=redeem_refused reason=other_server caller={caller} ticket_server={}", a.server);
                return refuse(403, "ticket_invalid");
            }
            _ => return refuse(403, "ticket_invalid"),
        }
        let admission = self.admissions.get_mut(&verifier).expect("checked above");
        admission.redeems += 1;
        let (account_id, key) = (admission.account.clone(), admission.key.clone());
        if admission.redeems >= REDEEMS_PER_TICKET {
            self.admissions.remove(&verifier);
        }
        let Some(account) = self.store.accounts.get(&account_id) else {
            return refuse(403, "ticket_invalid");
        };
        let Some(character) = view(account).character else {
            return refuse(403, "ticket_invalid");
        };
        Ok(Admitted { account: account_id, character, key })
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
    #[cfg(test)]
    pub fn reset(&mut self, raw_email: &str, raw_code: &str, password: &str, now: i64) -> Outcome<()> {
        let check = self.reset_begin(raw_email, raw_code, password, now)?;
        self.reset_finish(check, secrets::hash_password(password), now)
    }

    /// Reset, first part: the code and the new password's rules, a wrong code counted. The HTTP route hashes
    /// between this and `reset_finish`, outside the lock (QA 2026-11-19, AR8).
    pub fn reset_begin(&mut self, raw_email: &str, raw_code: &str, password: &str, now: i64) -> Outcome<ResetCheck> {
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
        let verifier = challenge.verifier.clone();
        let username = self.store.accounts[&account_id].username.clone();
        policy::password(password, &username, &email).or_else(|code| refuse(400, code))?;
        Ok(ResetCheck { account: account_id, username, verifier })
    }

    /// Reset, last part: the same code must still be outstanding (another reset may have spent it while the lock
    /// was let go). Sessions and outstanding admission tickets end with the old password (AR10).
    pub fn reset_finish(&mut self, check: ResetCheck, phc: String, now: i64) -> Outcome<()> {
        let ResetCheck { account: account_id, username, verifier } = check;
        let live = self.store.resets.get(&account_id)
            .is_some_and(|c| c.expires_at > now && c.attempts < CODE_ATTEMPTS && secrets::same(&c.verifier, &verifier));
        if !live || !self.store.accounts.contains_key(&account_id) {
            return refuse(400, "code_invalid");
        }
        self.commit(now, |store| {
            store.resets.remove(&account_id);
            store.sessions.retain(|_, s| s.account != account_id);
            store.accounts.get_mut(&account_id).expect("account").password = phc;
            Ok(())
        })?;
        self.admissions.retain(|_, a| a.account != account_id);
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
    /// The address of the server KEY belongs to.
    pub const SERVER: &str = "127.0.0.1:7777";
    /// A second listed server, with its own key.
    pub const OTHER_KEY: &str = "fedcba9876543210fedcba9876543210";
    pub const OTHER: &str = "melje.example.si:7777";

    pub fn servers() -> Vec<GameServer> {
        vec![
            GameServer { id: "lent".into(), addresses: vec![SERVER.into(), "localhost:7777".into()], key: KEY.into() },
            GameServer { id: "melje".into(), addresses: vec![OTHER.into()], key: OTHER_KEY.into() },
        ]
    }

    pub fn service(dir: &std::path::Path) -> (Service, Inbox) {
        let inbox = Inbox::default();
        let models = vec!["protagonist".to_string(), "manny".into(), "quinn".into()];
        (Service::open(dir.join("accounts.json"), Box::new(inbox.clone()), models, servers()).unwrap(), inbox)
    }

    /// Registers, verifies and sets up "ana_k", then signs in.
    pub fn signed_in(service: &mut Service, inbox: &Inbox) -> String {
        service.register("Ana@Example.si", 0).unwrap();
        let ticket = service.verify("ana@example.si", &inbox.last_code().to_lowercase(), 10).unwrap().ticket;
        service.setup(&ticket, "Ana_K", "drava-bridge-1990", 20).unwrap();
        service.login("ana_k", "drava-bridge-1990", 30).unwrap().session
    }

    use base64::engine::general_purpose::URL_SAFE_NO_PAD;
    use base64::Engine;

    /// ADR-083: every ticket has a key of its own.
    #[test]
    fn each_ticket_has_its_own_key() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        service.create_character(&session, "req-1", "Ana", "Kos", "manny", 40).unwrap();
        let a = service.admission(&session, SERVER, 41).unwrap();
        let b = service.admission(&session, SERVER, 42).unwrap();
        assert_ne!(a.key, b.key);
        assert_eq!(service.redeem(KEY, &b.ticket, 43).unwrap().key, b.key);
        assert_eq!(service.redeem(KEY, &a.ticket, 43).unwrap().key, a.key);
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
        assert_eq!(code_of(service.admission(&session, SERVER, 40)), "no_character");
        let made = service.create_character(&session, "req-1", " Žiga ", "Kos", "quinn", 50).unwrap();
        let character = made.character.expect("character");
        assert_eq!((character.first_name.as_str(), character.last_name.as_str(), character.model.as_str()), ("Žiga", "Kos", "quinn"));
        let issued = service.admission(&session, SERVER, 60).unwrap();
        let ticket = issued.ticket.clone();
        assert_eq!(code_of(service.redeem("wrong-key-wrong-key-wrong-key-xx", &ticket, 61)), "server_key_invalid");
        let admitted = service.redeem(KEY, &ticket, 61).unwrap();
        assert_eq!(admitted.character.id, character.id);
        // ADR-083: the server gets the key the client got, 32 bytes, and it is not the ticket.
        assert_eq!(admitted.key, issued.key);
        assert_eq!(URL_SAFE_NO_PAD.decode(&issued.key).unwrap().len(), 32);
        assert_ne!(issued.key, ticket);
        // ADR-086: its server may redeem it again (a thief's redeem must not lock the player out), up to the limit.
        for _ in 1..REDEEMS_PER_TICKET {
            assert_eq!(service.redeem(KEY, &ticket, 62).unwrap().key, issued.key);
        }
        assert_eq!(code_of(service.redeem(KEY, &ticket, 62)), "ticket_invalid", "limited redeems");
        let late = service.admission(&session, SERVER, 70).unwrap().ticket;
        assert_eq!(code_of(service.redeem(KEY, &late, 70 + ADMISSION_TTL_S)), "ticket_invalid", "expired");
        // Nothing secret is in the store: no password, code, session or ticket in the clear.
        let text = std::fs::read_to_string(dir.path().join("accounts.json")).unwrap();
        for secret in ["drava-bridge-1990", session.as_str(), ticket.as_str(), issued.key.as_str()] {
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
    fn made_up_names_cannot_grow_the_failure_records() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, _) = service(dir.path());
        // Names no account could have are refused without a record, however long or many.
        let long = "x".repeat(1 << 20);
        for name in [long.as_str(), "a b", "ab", "_x", "admin"] {
            assert!(service.login_begin(name, 100).unwrap().phc().is_none());
        }
        for i in 0..10_000 {
            service.login_begin(&format!("no such name {i}"), 100).unwrap();
        }
        assert!(service.failures.is_empty());
        // Names an account could have are recorded, and the records go once the window is over.
        for i in 0..100 {
            service.login_begin(&format!("nobody{i}"), 100).unwrap();
        }
        assert_eq!(service.failures.len(), 100);
        service.login_begin("nobody0", 100 + LOGIN_WINDOW_S).unwrap();
        assert_eq!(service.failures.len(), 1, "only the new attempt is left");
    }

    #[test]
    fn concurrent_guesses_count_before_their_check_finishes() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        signed_in(&mut service, &inbox);
        // Five sign-ins begun at once, none finished yet: the sixth is refused.
        let checks: Vec<LoginCheck> = (0..LOGIN_FAILURES).map(|_| service.login_begin("ana_k", 100).unwrap()).collect();
        assert_eq!(code_of(service.login_begin("ana_k", 100)), "too_many_attempts");
        for check in checks {
            assert_eq!(code_of(service.login_finish(check, false, 101)), "credentials_invalid");
        }
        assert_eq!(code_of(service.login("ana_k", "drava-bridge-1990", 102)), "too_many_attempts");
    }

    #[test]
    fn a_reset_between_the_steps_refuses_the_old_password() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        signed_in(&mut service, &inbox);
        let check = service.login_begin("ana_k", 100).unwrap();
        let accepted = check_password("drava-bridge-1990", check.phc());
        assert!(accepted);
        service.forgot("ana@example.si", 101).unwrap();
        service.reset("ana@example.si", &inbox.last_code(), "pohorje-snow-1991", 102).unwrap();
        assert_eq!(code_of(service.login_finish(check, accepted, 103)), "credentials_invalid");
        assert!(service.login("ana_k", "pohorje-snow-1991", 104).is_ok());
    }

    #[test]
    fn outstanding_admissions_per_account_are_capped() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        service.create_character(&session, "r1", "Ana", "Kos", "quinn", 40).unwrap();
        let tickets: Vec<String> = (0..5).map(|i| service.admission(&session, SERVER, 50 + i).unwrap().ticket).collect();
        assert_eq!(service.admissions.len(), ADMISSIONS_PER_ACCOUNT);
        for (i, ticket) in tickets.iter().enumerate() {
            let redeemed = service.redeem(KEY, ticket, 56).is_ok();
            assert_eq!(redeemed, i >= tickets.len() - ADMISSIONS_PER_ACCOUNT, "ticket {i}");
        }
    }

    /// QA 2026-11-19, AR3: one account signing in again and again keeps at most SESSIONS_PER_ACCOUNT, newest first.
    #[test]
    fn sessions_per_account_are_capped() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let first = signed_in(&mut service, &inbox);
        let later: Vec<String> = (0..SESSIONS_PER_ACCOUNT + 1)
            .map(|i| service.login("ana_k", "drava-bridge-1990", 40 + i as i64).unwrap().session)
            .collect();
        assert_eq!(service.store().sessions.len(), SESSIONS_PER_ACCOUNT);
        assert_eq!(code_of(service.me(&first, 60)), "session_invalid", "the oldest went first");
        assert_eq!(code_of(service.me(&later[0], 60)), "session_invalid");
        for session in &later[1..] {
            assert!(service.me(session, 60).is_ok());
        }
    }

    /// QA 2026-11-19, AR1: a ticket names its server; an unlisted address gets none, another server cannot spend it
    /// (nor burn it), and the spelling of the address does not matter.
    #[test]
    fn tickets_are_bound_to_their_server() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        service.create_character(&session, "req-1", "Ana", "Kos", "manny", 40).unwrap();
        assert_eq!(code_of(service.admission(&session, "evil.example.si:7777", 41)), "server_unknown", "lured elsewhere");
        assert_eq!(code_of(service.admission(&session, "127.0.0.1:7778", 41)), "server_unknown", "another port");
        assert_eq!(code_of(service.admission(&session, "not an address", 41)), "server_unknown");
        let ticket = service.admission(&session, " LOCALHOST ", 42).unwrap().ticket;
        assert_eq!(code_of(service.redeem(OTHER_KEY, &ticket, 43)), "ticket_invalid", "another listed server");
        assert_eq!(service.redeem(KEY, &ticket, 43).unwrap().character.first_name, "Ana", "not burned by the other server");
        let melje = service.admission(&session, OTHER, 44).unwrap().ticket;
        assert_eq!(code_of(service.redeem(KEY, &melje, 45)), "ticket_invalid");
        assert!(service.redeem(OTHER_KEY, &melje, 45).is_ok());
        assert_eq!(code_of(service.redeem("wrong-key-wrong-key-wrong-key-xx", &ticket, 45)), "server_key_invalid");
    }

    /// QA 2026-11-19, AR10: signing out or resetting the password spends the outstanding admission tickets.
    #[test]
    fn logout_and_reset_spend_outstanding_tickets() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        let session = signed_in(&mut service, &inbox);
        service.create_character(&session, "r1", "Ana", "Kos", "quinn", 40).unwrap();
        let ticket = service.admission(&session, SERVER, 41).unwrap().ticket;
        service.logout(&session, 42).unwrap();
        assert_eq!(code_of(service.redeem(KEY, &ticket, 43)), "ticket_invalid", "after logout");
        let session = service.login("ana_k", "drava-bridge-1990", 44).unwrap().session;
        let ticket = service.admission(&session, SERVER, 45).unwrap().ticket;
        service.forgot("ana@example.si", 46).unwrap();
        service.reset("ana@example.si", &inbox.last_code(), "pohorje-snow-1991", 47).unwrap();
        assert_eq!(code_of(service.redeem(KEY, &ticket, 48)), "ticket_invalid", "after a reset");
    }

    /// QA 2026-11-19, AR8: setup and reset hash outside the lock, so their last step checks again what may have
    /// changed in between.
    #[test]
    fn the_hash_outside_the_lock_is_checked_again() {
        let dir = tempfile::tempdir().unwrap();
        let (mut service, inbox) = service(dir.path());
        // Two setups race for one username: the second commit finds it taken.
        service.register("ana@example.si", 0).unwrap();
        let ana = service.verify("ana@example.si", &inbox.last_code(), 1).unwrap().ticket;
        service.register("bor@example.si", 2).unwrap();
        let bor = service.verify("bor@example.si", &inbox.last_code(), 3).unwrap().ticket;
        service.setup_check(&ana, "kos", "drava-bridge-1990", 4).unwrap();
        service.setup_check(&bor, "kos", "pohorje-snow-1991", 4).unwrap();
        service.setup_commit(&ana, "kos", "drava-bridge-1990", secrets::hash_password("drava-bridge-1990"), 5).unwrap();
        let late = service.setup_commit(&bor, "kos", "pohorje-snow-1991", secrets::hash_password("pohorje-snow-1991"), 6);
        assert_eq!(code_of(late), "username_taken");
        // Two resets with one code: the second finds it spent.
        service.forgot("ana@example.si", 10).unwrap();
        let code = inbox.last_code();
        let first = service.reset_begin("ana@example.si", &code, "pohorje-snow-1991", 11).unwrap();
        let second = service.reset_begin("ana@example.si", &code, "pohorje-snow-1992", 11).unwrap();
        service.reset_finish(first, secrets::hash_password("pohorje-snow-1991"), 12).unwrap();
        assert_eq!(code_of(service.reset_finish(second, secrets::hash_password("pohorje-snow-1992"), 13)), "code_invalid");
        assert!(service.login("kos", "pohorje-snow-1991", 14).is_ok(), "the first reset's password");
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
