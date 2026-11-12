//! Passwords, codes and tokens. Passwords are kept only as Argon2id PHC strings; codes and tokens only as
//! SHA-256 verifiers, so the store never holds anything a reader could replay (ADR-055).

use argon2::password_hash::{PasswordHash, PasswordHasher, PasswordVerifier, SaltString};
use argon2::{Algorithm, Argon2, Params, Version};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use rand::rngs::OsRng;
use rand::{Rng, RngCore};
use sha2::{Digest, Sha256};
use subtle::ConstantTimeEq;

/// OWASP's Argon2id minimum: 19 MiB, two passes, one lane.
fn argon2() -> Argon2<'static> {
    Argon2::new(Algorithm::Argon2id, Version::V0x13, Params::new(19 * 1024, 2, 1, None).expect("argon2 params"))
}

pub fn hash_password(password: &str) -> String {
    let salt = SaltString::generate(&mut OsRng);
    argon2().hash_password(password.as_bytes(), &salt).expect("argon2 hash").to_string()
}

pub fn verify_password(password: &str, phc: &str) -> bool {
    PasswordHash::new(phc).map(|hash| argon2().verify_password(password.as_bytes(), &hash).is_ok()).unwrap_or(false)
}

/// Spends the same work as a real check, so an unknown username answers no faster than a wrong password.
pub fn burn_password_check(password: &str) {
    static DUMMY: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    let phc = DUMMY.get_or_init(|| hash_password("marburgum-timing-equaliser"));
    let _ = verify_password(password, phc);
}

/// A bearer secret: 32 random bytes, URL-safe.
pub fn token() -> String {
    let mut bytes = [0u8; 32];
    OsRng.fill_bytes(&mut bytes);
    URL_SAFE_NO_PAD.encode(bytes)
}

/// An opaque id with a readable prefix ("a_", "c_").
pub fn id(prefix: &str) -> String {
    let mut bytes = [0u8; 12];
    OsRng.fill_bytes(&mut bytes);
    format!("{prefix}{}", bytes.iter().map(|b| format!("{b:02x}")).collect::<String>())
}

/// Letters and digits that cannot be misread for each other.
const CODE_ALPHABET: &[u8] = b"ABCDEFGHJKMNPQRSTUVWXYZ23456789";
pub const CODE_LENGTH: usize = 8;

/// A code a person can type from an email: eight characters, about 40 bits.
pub fn code() -> String {
    (0..CODE_LENGTH).map(|_| CODE_ALPHABET[OsRng.gen_range(0..CODE_ALPHABET.len())] as char).collect()
}

/// What a typed code is compared on: upper-cased, without spaces or dashes.
pub fn normalise_code(raw: &str) -> String {
    raw.chars().filter(|c| !c.is_whitespace() && *c != '-').collect::<String>().to_uppercase()
}

/// The stored verifier of a secret, bound to what it is for (an email, "session", "admission").
pub fn verifier(scope: &str, secret: &str) -> String {
    let mut hash = Sha256::new();
    hash.update(scope.as_bytes());
    hash.update([0u8]);
    hash.update(secret.as_bytes());
    hash.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

pub fn same(a: &str, b: &str) -> bool {
    a.len() == b.len() && bool::from(a.as_bytes().ct_eq(b.as_bytes()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn passwords_round_trip() {
        let phc = hash_password("drava-bridge-1990");
        assert!(phc.starts_with("$argon2id$v=19$m=19456,t=2,p=1$"));
        assert!(verify_password("drava-bridge-1990", &phc));
        assert!(!verify_password("drava-bridge-1991", &phc));
        assert!(!verify_password("x", "not a hash"));
        assert_ne!(phc, hash_password("drava-bridge-1990"), "salted");
    }

    #[test]
    fn codes_and_tokens() {
        let code = code();
        assert_eq!(code.len(), CODE_LENGTH);
        assert!(code.bytes().all(|b| CODE_ALPHABET.contains(&b)));
        assert_eq!(normalise_code(" abcd-2345 "), "ABCD2345");
        assert_eq!(token().len(), 43);
        assert_ne!(token(), token());
        assert!(id("c_").starts_with("c_") && id("c_").len() == 26);
        assert_ne!(verifier("a@b.si", "X"), verifier("c@d.si", "X"));
        assert!(same(&verifier("s", "t"), &verifier("s", "t")));
        assert!(!same("ab", "abc"));
    }
}
