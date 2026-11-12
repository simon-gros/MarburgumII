//! The account store: one JSON document, rewritten durably on every change (ADR-055). Accounts and their one
//! character, pending email codes, password resets and sign-in sessions. Codes and tokens are stored as
//! verifiers only; passwords as Argon2id PHC strings. Admission tickets live in memory (service.rs).

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

pub const FORMAT: &str = "marburgumii.account_store";
pub const SCHEMA: u32 = 1;

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Store {
    pub format: String,
    pub schema_version: u32,
    pub revision: u64,
    /// Account id → account.
    pub accounts: BTreeMap<String, Account>,
    /// Normalised username → account id.
    pub usernames: BTreeMap<String, String>,
    /// Normalised email → account id.
    pub emails: BTreeMap<String, String>,
    /// Normalised email → the code that proves it, before an account exists.
    pub registrations: BTreeMap<String, Challenge>,
    /// Verifier of a setup ticket → the email it was issued for, once the code has been accepted.
    pub setups: BTreeMap<String, Setup>,
    /// Account id → an outstanding password-reset code.
    pub resets: BTreeMap<String, Challenge>,
    /// Verifier of a session token → the session.
    pub sessions: BTreeMap<String, Session>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Account {
    pub email: String,
    pub username: String,
    pub password: String,
    pub created_at: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub character: Option<Character>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Character {
    pub id: String,
    pub first_name: String,
    pub last_name: String,
    pub model: String,
    pub created_at: i64,
    /// The client's idempotency key for the create request that made it.
    pub request: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Challenge {
    pub verifier: String,
    pub expires_at: i64,
    pub attempts: u32,
    /// When a code was sent, newest last, for the resend limit.
    pub sent: Vec<i64>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Setup {
    pub email: String,
    pub expires_at: i64,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Session {
    pub account: String,
    pub expires_at: i64,
}

impl Store {
    pub fn new() -> Self {
        Store { format: FORMAT.into(), schema_version: SCHEMA, ..Default::default() }
    }

    /// Reads the store, or starts an empty one when the file does not exist yet.
    pub fn load(path: &Path) -> Result<Self, String> {
        match fs::read(path) {
            Ok(bytes) => {
                let store: Store = serde_json::from_slice(&bytes).map_err(|e| format!("{}: {e}", path.display()))?;
                if store.format != FORMAT || store.schema_version != SCHEMA {
                    return Err(format!("{}: not a schema {SCHEMA} account store", path.display()));
                }
                Ok(store)
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(Store::new()),
            Err(e) => Err(format!("{}: {e}", path.display())),
        }
    }

    /// Drops what has expired. Returns whether anything went.
    pub fn sweep(&mut self, now: i64) -> bool {
        let before = self.registrations.len() + self.setups.len() + self.resets.len() + self.sessions.len();
        // A registration stays while its resend history matters, so waiting out the expiry does not reset the limit.
        self.registrations.retain(|_, c| c.expires_at > now || c.sent.last().is_some_and(|t| now - t < 3600));
        self.setups.retain(|_, s| s.expires_at > now);
        self.resets.retain(|_, c| c.expires_at > now || c.sent.last().is_some_and(|t| now - t < 3600));
        self.sessions.retain(|_, s| s.expires_at > now);
        before != self.registrations.len() + self.setups.len() + self.resets.len() + self.sessions.len()
    }
}

/// Writes the whole store or nothing: owner-only temporary file, every byte synced, renamed over the old one and
/// the directory synced, so the new name cannot outlive the new bytes (the original game's durable save).
pub fn write_durably(path: &Path, store: &Store) -> io::Result<()> {
    let text = serde_json::to_vec_pretty(store).map_err(io::Error::other)?;
    let directory = path.parent().filter(|d| !d.as_os_str().is_empty()).map(Path::to_path_buf).unwrap_or(PathBuf::from("."));
    fs::create_dir_all(&directory)?;
    let temporary = path.with_extension("json.tmp");
    {
        let mut options = OpenOptions::new();
        options.write(true).create(true).truncate(true);
        #[cfg(unix)]
        std::os::unix::fs::OpenOptionsExt::mode(&mut options, 0o600);
        let mut file = options.open(&temporary)?;
        file.write_all(&text)?;
        file.write_all(b"\n")?;
        file.sync_all()?;
    }
    fs::rename(&temporary, path)?;
    if let Ok(dir) = File::open(&directory) {
        let _ = dir.sync_all();
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_owner_only() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("nested").join("accounts.json");
        assert_eq!(Store::load(&path).unwrap(), Store::new());
        let mut store = Store::new();
        store.revision = 3;
        store.usernames.insert("ana_k".into(), "a_1".into());
        store.accounts.insert("a_1".into(), Account {
            email: "ana@example.si".into(), username: "ana_k".into(), password: "$argon2id$x".into(), created_at: 5,
            character: Some(Character { id: "c_1".into(), first_name: "Žiga".into(), last_name: "Kos".into(),
                model: "quinn".into(), created_at: 6, request: "r".into() }),
        });
        write_durably(&path, &store).unwrap();
        assert_eq!(Store::load(&path).unwrap(), store);
        assert!(!path.with_extension("json.tmp").exists());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(fs::metadata(&path).unwrap().permissions().mode() & 0o777, 0o600);
        }
    }

    #[test]
    fn refuses_other_documents() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("world-store.json");
        fs::write(&path, r#"{"format":"marburgumii.world_store","schema_version":4,"revision":1}"#).unwrap();
        assert!(Store::load(&path).is_err());
    }

    #[test]
    fn sweep_keeps_recent_resend_history() {
        let mut store = Store::new();
        let challenge = |expires_at, sent| Challenge { verifier: "v".into(), expires_at, attempts: 0, sent: vec![sent] };
        store.registrations.insert("old@x.si".into(), challenge(100, 0));
        store.registrations.insert("recent@x.si".into(), challenge(100, 4000));
        store.sessions.insert("s".into(), Session { account: "a".into(), expires_at: 100 });
        assert!(store.sweep(5000));
        assert!(store.registrations.contains_key("recent@x.si") && !store.registrations.contains_key("old@x.si"));
        assert!(store.sessions.is_empty());
        assert!(!store.sweep(5000));
    }
}
