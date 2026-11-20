//! The game servers the service issues admission tickets for (QA 2026-11-19, AR1). Each has an id, the addresses
//! players type to reach it and its own key; a ticket is bound to one server, and only that server's key spends it.
//!
//! The file (`--servers <servers.json>`) holds no key itself, only where each key is, beside it:
//!
//! { "servers": [ { "id": "lent-1", "addresses": ["lent.example.si:7777"], "key_file": "lent-1.key" } ] }

use crate::policy;
use serde::Deserialize;
use std::collections::HashSet;
use std::fs;
use std::path::Path;

/// A server key is a shared secret of at least this many characters.
pub const KEY_MIN: usize = 32;

#[derive(Clone)]
pub struct GameServer {
    pub id: String,
    /// Normalised with `policy::server_address`.
    pub addresses: Vec<String>,
    pub key: String,
}

impl std::fmt::Debug for GameServer {
    /// Without the key.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("GameServer").field("id", &self.id).field("addresses", &self.addresses).finish_non_exhaustive()
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Entry {
    id: String,
    addresses: Vec<String>,
    key_file: String,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct File {
    servers: Vec<Entry>,
}

/// Ids, keys and addresses must each be unique: two servers sharing a key could spend each other's tickets, and an
/// address on two servers would make the ticket's server a guess.
pub fn check(servers: &[GameServer]) -> Result<(), String> {
    if servers.is_empty() {
        return Err("no game servers".into());
    }
    let (mut ids, mut keys, mut addresses) = (HashSet::new(), HashSet::new(), HashSet::new());
    for server in servers {
        if server.id.is_empty() || server.id.len() > 32
            || !server.id.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
        {
            return Err(format!("server id {:?}: use 1-32 of a-z, 0-9 and '-'", server.id));
        }
        if !ids.insert(server.id.as_str()) {
            return Err(format!("server {}: the id is listed twice", server.id));
        }
        if server.key.len() < KEY_MIN {
            return Err(format!("server {}: the key needs at least {KEY_MIN} characters", server.id));
        }
        if !keys.insert(server.key.as_str()) {
            return Err(format!("server {}: another server has the same key", server.id));
        }
        if server.addresses.is_empty() {
            return Err(format!("server {}: no addresses", server.id));
        }
        for address in &server.addresses {
            if !addresses.insert(address.as_str()) {
                return Err(format!("server {}: address {address} is listed twice", server.id));
            }
        }
    }
    Ok(())
}

/// Reads the servers file and each key file it names (relative paths are beside the servers file).
pub fn load(path: &Path) -> Result<Vec<GameServer>, String> {
    let text = fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let file: File = serde_json::from_slice(&text).map_err(|e| format!("{}: {e}", path.display()))?;
    let dir = path.parent().unwrap_or(Path::new("."));
    let mut servers = Vec::new();
    for entry in file.servers {
        let key_path = dir.join(&entry.key_file);
        let key = fs::read_to_string(&key_path).map_err(|e| format!("{}: {e}", key_path.display()))?.trim().to_string();
        let addresses = entry.addresses.iter()
            .map(|a| policy::server_address(a).map_err(|_| format!("server {}: address {a:?} is not host[:port]", entry.id)))
            .collect::<Result<Vec<_>, _>>()?;
        servers.push(GameServer { id: entry.id, addresses, key });
    }
    check(&servers)?;
    Ok(servers)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(dir: &Path, name: &str, text: &str) {
        fs::write(dir.join(name), text).unwrap();
    }

    #[test]
    fn a_servers_file_with_its_keys() {
        let dir = tempfile::tempdir().unwrap();
        write(dir.path(), "a.key", "0123456789abcdef0123456789abcdef\n");
        write(dir.path(), "b.key", "fedcba9876543210fedcba9876543210");
        write(dir.path(), "servers.json", r#"{ "servers": [
            { "id": "lent-1", "addresses": ["Lent.Example.SI", "127.0.0.1:27714"], "key_file": "a.key" },
            { "id": "melje", "addresses": ["[::1]:27800"], "key_file": "b.key" } ] }"#);
        let servers = load(&dir.path().join("servers.json")).unwrap();
        assert_eq!(servers[0].addresses, ["lent.example.si:7777", "127.0.0.1:27714"]);
        assert_eq!(servers[0].key, "0123456789abcdef0123456789abcdef", "trimmed");
        assert!(!format!("{servers:?}").contains("0123456789abcdef"), "Debug leaves the key out");
    }

    #[test]
    fn shared_keys_short_keys_and_shared_addresses_are_refused() {
        let server = |id: &str, address: &str, key: &str| GameServer { id: id.into(), addresses: vec![address.into()], key: key.into() };
        let (a, b) = ("0123456789abcdef0123456789abcdef", "fedcba9876543210fedcba9876543210");
        assert!(check(&[server("x", "h:1", a), server("y", "h:2", b)]).is_ok());
        assert!(check(&[]).is_err());
        assert!(check(&[server("x", "h:1", a), server("y", "h:2", a)]).unwrap_err().contains("same key"));
        assert!(check(&[server("x", "h:1", a), server("y", "h:1", b)]).unwrap_err().contains("listed twice"));
        assert!(check(&[server("x", "h:1", a), server("x", "h:2", b)]).unwrap_err().contains("id is listed twice"));
        assert!(check(&[server("x", "h:1", "short")]).unwrap_err().contains("at least"));
        assert!(check(&[server("Lent 1", "h:1", a)]).unwrap_err().contains("server id"));
    }
}
