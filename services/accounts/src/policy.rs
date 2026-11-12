//! What the service accepts as an email, a username, a password, a character name and a model (ADR-055).
//! Every rule is checked here, on the server; the client's checks are only a convenience.

use unicode_normalization::UnicodeNormalization;

pub const NAME_MIN: usize = 2;
pub const NAME_MAX: usize = 24;
pub const USERNAME_MIN: usize = 3;
pub const USERNAME_MAX: usize = 20;
pub const PASSWORD_MIN: usize = 10;
pub const PASSWORD_MAX: usize = 128;

/// Words nobody may take as a username or as a whole character name: they read as staff or as the game itself.
const RESERVED: &[&str] = &[
    "admin", "administrator", "moderator", "mod", "gm", "gamemaster", "staff", "support", "system", "server",
    "marburgum", "marburgumii", "official", "null", "undefined", "root",
];

/// Lower-cases and trims an email, and rejects anything that cannot be a mailbox. No lookup, no MX check.
pub fn email(raw: &str) -> Result<String, &'static str> {
    let email = raw.trim().to_lowercase();
    if email.len() > 254 || email.chars().any(|c| c.is_whitespace() || c.is_control()) {
        return Err("email_invalid");
    }
    let (local, domain) = email.split_once('@').ok_or("email_invalid")?;
    let labels: Vec<&str> = domain.split('.').collect();
    if local.is_empty() || local.len() > 64 || domain.contains('@') || labels.len() < 2
        || labels.iter().any(|label| label.is_empty() || label.starts_with('-') || label.ends_with('-'))
    {
        return Err("email_invalid");
    }
    Ok(email)
}

/// A login name: 3–20 of a-z, 0-9, '.', '_' and '-', starting with a letter or digit; case does not matter.
pub fn username(raw: &str) -> Result<String, &'static str> {
    let name = raw.trim().to_lowercase();
    let length = name.chars().count();
    if !(USERNAME_MIN..=USERNAME_MAX).contains(&length) {
        return Err("username_length");
    }
    if !name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || matches!(c, '.' | '_' | '-'))
        || !name.starts_with(|c: char| c.is_ascii_alphanumeric())
    {
        return Err("username_characters");
    }
    if RESERVED.contains(&name.as_str()) {
        return Err("username_reserved");
    }
    Ok(name)
}

/// 10–128 characters, and not the username or the email itself.
pub fn password(password: &str, username: &str, email: &str) -> Result<(), &'static str> {
    let length = password.chars().count();
    if !(PASSWORD_MIN..=PASSWORD_MAX).contains(&length) {
        return Err("password_length");
    }
    let lower = password.to_lowercase();
    if lower.contains(username) || lower == email {
        return Err("password_contains_username");
    }
    Ok(())
}

/// A letter a character name may use: Latin letters, including Slovene and the neighbouring languages'
/// diacritics (č, š, ž, ć, đ, ä, ö, ü, ß ...). Other scripts wait for a moderation policy that can read them.
fn latin_letter(c: char) -> bool {
    c.is_ascii_alphabetic()
        || (('\u{00C0}'..='\u{024F}').contains(&c) && c != '\u{00D7}' && c != '\u{00F7}' && c.is_alphabetic())
}

/// A first or last name for the world, not the player's legal name. NFC-normalised; 2–24 characters; letters
/// with single spaces, hyphens or apostrophes between them (Ana-Marija, D'Angelo, Van Dijk).
pub fn character_name(raw: &str) -> Result<String, &'static str> {
    let name: String = raw.trim().replace('\u{2019}', "'").nfc().collect();
    let length = name.chars().count();
    if !(NAME_MIN..=NAME_MAX).contains(&length) {
        return Err("name_length");
    }
    let mut previous_separator = true; // a name starts with a letter
    for c in name.chars() {
        if latin_letter(c) {
            previous_separator = false;
        } else if matches!(c, ' ' | '-' | '\'') && !previous_separator {
            previous_separator = true;
        } else {
            return Err("name_characters");
        }
    }
    if previous_separator {
        return Err("name_characters");
    }
    if RESERVED.contains(&name.to_lowercase().as_str()) {
        return Err("name_reserved");
    }
    Ok(name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn emails() {
        assert_eq!(email("  Ana.Kos@Example.SI ").unwrap(), "ana.kos@example.si");
        for bad in ["", "ana", "ana@", "@example.si", "ana@example", "ana@@example.si", "a na@example.si", "ana@-x.si"] {
            assert!(email(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn usernames() {
        assert_eq!(username(" Ana_K ").unwrap(), "ana_k");
        assert_eq!(username("ab").unwrap_err(), "username_length");
        assert_eq!(username("_ana").unwrap_err(), "username_characters");
        assert_eq!(username("ana kos").unwrap_err(), "username_characters");
        assert_eq!(username("čevapčič").unwrap_err(), "username_characters");
        assert_eq!(username("Admin").unwrap_err(), "username_reserved");
    }

    #[test]
    fn passwords() {
        assert!(password("drava-bridge-1990", "ana_k", "ana@example.si").is_ok());
        assert_eq!(password("short", "ana_k", "a@b.si").unwrap_err(), "password_length");
        assert_eq!(password("my-ANA_K-password", "ana_k", "a@b.si").unwrap_err(), "password_contains_username");
        assert_eq!(password("ana@example.si", "zz9", "ana@example.si").unwrap_err(), "password_contains_username");
    }

    #[test]
    fn names_with_slovene_letters() {
        for good in ["Ana", "Žiga", "Čedomir", "Špela", "Ana-Marija", "D'Angelo", "Van Dijk", "Đurđa", "Müller"] {
            assert_eq!(character_name(good).unwrap(), good);
        }
        assert_eq!(character_name("D\u{2019}Angelo").unwrap(), "D'Angelo");
        // A decomposed Č (C + combining caron) is stored as the single letter; a stray mark is refused.
        assert_eq!(character_name("C\u{030C}rt").unwrap(), "\u{010C}rt");
        assert_eq!(character_name("Ana\u{0301}\u{0301}").unwrap_err(), "name_characters");
    }

    #[test]
    fn names_refused() {
        assert_eq!(character_name("A").unwrap_err(), "name_length");
        assert_eq!(character_name("Abcdefghijklmnopqrstuvwxy").unwrap_err(), "name_length");
        for bad in ["Ana  Kos", "-Ana", "Ana-", "Ana--Kos", "R2D2", "Ana_Kos", "Анна", "Ana\tKos"] {
            assert_eq!(character_name(bad).unwrap_err(), "name_characters", "{bad}");
        }
        assert_eq!(character_name("Moderator").unwrap_err(), "name_reserved");
    }
}
