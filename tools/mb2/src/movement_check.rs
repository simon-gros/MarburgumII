//! ADR-019 movement bound for one Lent smoke walker, from the dedicated server's own positions.
//!
//! A walker starts on Stari most and walks north until the server places it in the Lent area. Between
//! the client's `walk_start` and the server's `area=Lent` sample the server's pawn may have covered at
//! most `MaxWalkSpeed * (elapsed + MovementTimeDiscrepancyMaxTimeMargin) + slack` metres: real time,
//! plus the time a client may bank before the server detects its clock, plus slack for the 4 Hz area
//! sampling and the capsule settling. A client whose move timestamps run ahead of real time breaks this
//! unless the server limits it. Both logs come from processes on one host, so their wall-clock stamps
//! are comparable. Prints one result line; 0 within the bound, 1 otherwise.

use std::collections::HashMap;

use regex::Regex;

const SLACK_M: f64 = 0.5;

/// Seconds since the epoch (UTC calendar arithmetic) of a `[YYYY.MM.DD-HH.MM.SS:mmm]` log stamp.
fn when(line: &str) -> Option<f64> {
    let stamp = Regex::new(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]").unwrap();
    let c = stamp.captures(line)?;
    let n = |i: usize| c[i].parse::<i64>().unwrap();
    let (y, m, d) = (n(1), n(2), n(3));
    // Days from 1970-01-01 to y-m-d (Howard Hinnant's days_from_civil).
    let y = if m <= 2 { y - 1 } else { y };
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let doy = (153 * (if m > 2 { m - 3 } else { m + 9 }) + 2) / 5 + d - 1;
    let days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    Some((days * 86400 + n(4) * 3600 + n(5) * 60 + n(6)) as f64 + n(7) as f64 / 1000.0)
}

fn fields(line: &str) -> HashMap<String, String> {
    let pairs = Regex::new(r"(\w+)=(\S+)").unwrap();
    let tail = line.find("MB2").map(|at| &line[at..]).unwrap_or(line);
    pairs.captures_iter(tail).map(|c| (c[1].to_string(), c[2].to_string())).collect()
}

/// The last `MovementTimeDiscrepancyMaxTimeMargin=` in DefaultGame.ini.
pub fn time_margin(ini: &str) -> Option<f64> {
    let key = Regex::new(r"^\s*MovementTimeDiscrepancyMaxTimeMargin\s*=\s*([-0-9.]+)").unwrap();
    ini.lines().filter_map(|line| key.captures(line)).last().and_then(|c| c[1].parse().ok())
}

pub fn check(server: &str, client: &str, role: &str, key: &str, kind: &str, margin: Option<f64>) -> (bool, String) {
    let start = client.lines().find(|l| l.contains(&format!("tag={role} event=walk_start")));
    let character = format!(" character={key} ");
    let areas: Vec<&str> = server.lines().filter(|l| l.contains("role=server event=area ") && l.contains(&character)).collect();
    let spawn = areas.first().copied();
    let entry = areas.iter().copied().find(|l| fields(l).get("area").is_some_and(|a| a.eq_ignore_ascii_case("lent")));
    let (Some(margin), Some(start), Some(spawn), Some(entry)) = (margin, start, spawn, entry) else {
        return (false, format!("{role} {kind} movement check FAILED: margin={margin:?} walk_start={} spawn={} entry={}",
            start.is_some(), spawn.is_some(), entry.is_some()));
    };
    let (s, a, e) = (fields(start), fields(spawn), fields(entry));
    let number = |map: &HashMap<String, String>, k: &str| map.get(k).and_then(|v| v.parse::<f64>().ok()).unwrap_or(f64::NAN);
    let speed = number(&s, "max_walk_mps");
    let elapsed = match (when(entry), when(start)) {
        (Some(entered), Some(started)) => entered - started,
        _ => f64::NAN,
    };
    let covered = (number(&e, "x_m") - number(&a, "x_m")).hypot(number(&e, "y_m") - number(&a, "y_m"));
    let allowed = speed * (elapsed.max(0.0) + margin) + SLACK_M;
    let ok = elapsed > 0.0 && speed > 0.0 && covered <= allowed;
    let clock = s.get("clock_rate").map(String::as_str).unwrap_or("?");
    (ok, format!("{role} {kind} clock_rate={clock} server_covered_m={covered:.2} in_s={elapsed:.2} speed_mps={:.2} \
allowed_m={allowed:.2} max_walk_mps={speed:.2} margin_s={margin:.2} {}",
        covered / elapsed.max(1e-3), if ok { "ok" } else { "TOO FAST" }))
}

/// `mb2 movement-check <server log> <client log> <role> <character key> <honest|adversary> <DefaultGame.ini>`
pub fn run(server_log: &str, client_log: &str, role: &str, key: &str, kind: &str, ini: &str) -> u8 {
    // Unreal logs are mostly UTF-8 but may carry stray bytes; read them lossily.
    let text = |path: &str| std::fs::read(path).map(|b| String::from_utf8_lossy(&b).into_owned());
    let (server, client, config) = match (text(server_log), text(client_log), text(ini)) {
        (Ok(s), Ok(c), Ok(i)) => (s, c, i),
        _ => {
            println!("{role} {kind} movement check FAILED: cannot read {server_log}, {client_log} or {ini}");
            return 1;
        }
    };
    let (ok, message) = check(&server, &client, role, key, kind, time_margin(&config));
    println!("{message}");
    if ok { 0 } else { 1 }
}

#[cfg(test)]
mod tests {
    use super::*;

    const KEY: &str = "mb2f-t1";

    fn client() -> String {
        "[2026.11.10-13.30.15:952][411]LogMarburgumNet: MB2LENT role=client tag=F event=walk_start area=None \
x_m=2.00 y_m=-40.00 max_walk_mps=4.50 clock_rate=2.50\n".to_string()
    }

    fn server(entry_stamp: &str, entry_y: f64) -> String {
        format!("[2026.11.10-13.30.12:335][ 23]LogMarburgumNet: MB2INTERACT role=server event=area player=256 character={KEY} area=- x_m=2.00 y_m=-40.00\n\
[2026.11.10-13.30.13:000][ 24]LogMarburgumNet: MB2INTERACT role=server event=area player=257 character=other area=Lent x_m=0.00 y_m=-60.00\n\
[2026.11.10-{entry_stamp}][165]LogMarburgumNet: MB2INTERACT role=server event=area player=256 character={KEY} area=Lent x_m=2.00 y_m={entry_y:.2}\n")
    }

    #[test]
    fn unchecked_fast_clock_is_too_fast() {
        // The 2026-11-10 finding: 12.7 m in 1.1 s of wall time.
        let (ok, message) = check(&server("13.30.17:085", -52.7), &client(), "F", KEY, "adversary", Some(0.25));
        assert!(!ok);
        assert!(message.contains("TOO FAST"), "{message}");
    }

    #[test]
    fn walking_speed_plus_margin_passes() {
        // 2.6 s at 4.5 m/s is 11.7 m; one banked margin (1.1 m) and the slack still pass.
        let (ok, message) = check(&server("13.30.18:552", -53.2), &client(), "F", KEY, "adversary", Some(0.25));
        assert!(ok, "{message}");
    }

    #[test]
    fn beyond_the_margin_fails() {
        let y = -40.0 - 4.5 * (2.6 + 0.25) - 0.6;
        assert!(!check(&server("13.30.18:552", y), &client(), "F", KEY, "adversary", Some(0.25)).0);
    }

    #[test]
    fn missing_entry_or_margin_fails() {
        let without_entry: String = server("13.30.18:552", -53.2).lines().take(2).map(|l| format!("{l}\n")).collect();
        assert!(!check(&without_entry, &client(), "F", KEY, "adversary", Some(0.25)).0);
        assert!(!check(&server("13.30.18:552", -53.2), &client(), "F", KEY, "adversary", None).0);
    }

    #[test]
    fn elapsed_crosses_midnight() {
        let late = client().replace("2026.11.10-13.30.15:952", "2026.11.10-23.59.59:000");
        let (ok, message) = check(&server("00.00.02:000", -50.0).replace("2026.11.10-00", "2026.11.11-00"),
            &late, "F", KEY, "adversary", Some(0.25));
        assert!(ok, "{message}");
        assert!(message.contains("in_s=3.00"), "{message}");
    }

    #[test]
    fn margin_read_from_config() {
        let ini = "[/Script/Engine.GameNetworkManager]\nMovementTimeDiscrepancyMaxTimeMargin=0.25\n";
        assert_eq!(time_margin(ini), Some(0.25));
    }
}
