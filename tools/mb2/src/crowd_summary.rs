//! The verdict of scripts/street-multiplayer-smoke-linux.sh: every expected crowd scenario's last result line in
//! the server log, the clients' watch lines and the server's net-rate samples, written to `<out>/summary.json`.
//! Exit 0 when every scenario was accepted and the world clock was the expected one.

use std::path::Path;

use regex::Regex;
use serde_json::{json, Value};

const DAY: [&str; 10] = ["solo", "A", "B", "C_apart", "C_back", "D", "E_dropped", "E", "cap", "cap_drop"];
const NIGHT: [&str; 1] = ["night"];
const CLIENT_LOGS: [&str; 4] = ["client-a.log", "client-b.log", "client-b2.log", "client-c.log"];

fn read_lossy(path: &Path) -> Option<String> {
    std::fs::read(path).ok().map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
}

/// The value of ` key=value` on a log line, or "".
fn field<'a>(line: &'a str, key: &str) -> &'a str {
    let pattern = Regex::new(&format!(r"(?:^|\s){}=(\S+)", regex::escape(key))).unwrap();
    pattern.captures(line).and_then(|c| c.get(1)).map_or("", |m| m.as_str())
}

/// The summary document for one run; `ok` in it is the verdict.
pub fn summarize(server_log: &str, clients: &[(&str, Option<String>)], sha: &str, case_name: &str, expect_clock: &str) -> Value {
    let lines: Vec<&str> = server_log.lines().collect();
    let wanted: &[&str] = if case_name == "night" { &NIGHT } else { &DAY };
    let mut ok = true;
    let mut rows = Vec::new();
    for name in wanted {
        let tag = format!("name={name} ");
        let line = lines.iter().rev()
            .find(|ln| ln.contains(&tag) && ln.contains("event=scenario") && !ln.contains("phase="))
            .copied()
            .unwrap_or("");
        let state = field(line, "state");
        let passed = field(line, "pass") == "1";
        let accepted = match *name {
            "D" | "cap" => (state == "settled" || state == "constrained") && !server_log.contains("over_cap"),
            "E_dropped" | "cap_drop" => state == "lifecycle" && passed,
            _ => state == "settled" && passed,
        };
        ok = ok && accepted && !line.is_empty();
        rows.push(json!({
            "name": name,
            "state": if state.is_empty() { "missing" } else { state },
            "pass": passed,
            "accepted": accepted,
            "line": line,
        }));
    }
    let watched: Vec<Value> = clients.iter()
        .filter_map(|(log, body)| body.as_ref().map(|b| (log, b)))
        .map(|(log, body)| {
            let watches: Vec<&str> = body.lines().filter(|ln| ln.contains("event=crowd_watch")).collect();
            json!({ "log": log, "watches": watches.len(), "last": watches.last().copied().unwrap_or("") })
        })
        .collect();
    let net: Vec<&str> = lines.iter().copied().filter(|ln| ln.contains("event=net_rate")).collect();
    let clock_ok = lines.iter().any(|ln| ln.contains(expect_clock) && ln.contains("offset=+2"));
    json!({
        "sha": sha,
        "case": case_name,
        "clock_ok": clock_ok,
        "ok": ok && clock_ok,
        "scenarios": rows,
        "clients": watched,
        "net_rate_last": net.last().copied().unwrap_or(""),
        "net_rate_samples": net.len(),
    })
}

/// `mb2 crowd-summary <out dir> <sha> <day|night> <expected clock line fragment>`
pub fn run(out: &str, sha: &str, case_name: &str, expect_clock: &str) -> u8 {
    let out = Path::new(out);
    let Some(server_log) = read_lossy(&out.join("server.log")) else {
        eprintln!("cannot read {}", out.join("server.log").display());
        return 2;
    };
    let clients: Vec<(&str, Option<String>)> = CLIENT_LOGS.iter().map(|log| (*log, read_lossy(&out.join(log)))).collect();
    let summary = summarize(&server_log, &clients, sha, case_name, expect_clock);
    let text = serde_json::to_string_pretty(&summary).unwrap();
    if let Err(error) = std::fs::write(out.join("summary.json"), &text) {
        eprintln!("cannot write {}: {error}", out.join("summary.json").display());
        return 2;
    }
    println!("{text}");
    u8::from(summary["ok"] != true)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLOCK: &str = r#"marburgum="Tuesday 18 June 1996 02:00""#;

    fn night_log(state: &str, pass: u8) -> String {
        format!(
            "event=clock {CLOCK} offset=+2\n\
             event=scenario name=night phase=start\n\
             event=net_rate bytes=10\n\
             event=scenario name=night state={state} pass={pass}\n\
             event=net_rate bytes=12\n"
        )
    }

    #[test]
    fn a_settled_night_on_summer_time_passes() {
        let clients = [("client-a.log", Some("event=crowd_watch n=1\nevent=crowd_watch n=2\n".to_string())), ("client-c.log", None)];
        let summary = summarize(&night_log("settled", 1), &clients, "abc", "night", CLOCK);
        assert_eq!(summary["ok"], true);
        assert_eq!(summary["scenarios"][0]["line"], "event=scenario name=night state=settled pass=1", "phase lines are skipped");
        assert_eq!(summary["clients"].as_array().unwrap().len(), 1, "missing client logs are left out");
        assert_eq!(summary["clients"][0]["watches"], 2);
        assert_eq!(summary["net_rate_last"], "event=net_rate bytes=12");
        assert_eq!(summary["net_rate_samples"], 2);
    }

    #[test]
    fn an_unsettled_scenario_a_missing_one_or_the_wrong_clock_fails() {
        assert_eq!(summarize(&night_log("timeout", 0), &[], "abc", "night", CLOCK)["ok"], false);
        assert_eq!(summarize(&night_log("settled", 1), &[], "abc", "night", "marburgum=\"Tuesday 18 June 1996 10:00\"")["ok"], false);
        let day = summarize(&night_log("settled", 1), &[], "abc", "day", CLOCK);
        assert_eq!(day["ok"], false);
        assert_eq!(day["scenarios"][0]["state"], "missing");
    }

    #[test]
    fn the_cap_scenarios_accept_constrained_but_never_over_cap() {
        let mut log = format!("event=clock {CLOCK} offset=+2\n");
        for name in DAY {
            let line = match name {
                "D" | "cap" => format!("event=scenario name={name} state=constrained pass=0\n"),
                "E_dropped" | "cap_drop" => format!("event=scenario name={name} state=lifecycle pass=1\n"),
                _ => format!("event=scenario name={name} state=settled pass=1\n"),
            };
            log.push_str(&line);
        }
        assert_eq!(summarize(&log, &[], "abc", "day", CLOCK)["ok"], true);
        log.push_str("warning over_cap people=49\n");
        assert_eq!(summarize(&log, &[], "abc", "day", CLOCK)["ok"], false);
    }

    #[test]
    fn fields_match_whole_keys_only() {
        assert_eq!(field("event=scenario name=C_back state=settled pass=1", "pass"), "1");
        assert_eq!(field("event=scenario bypass=0 name=x", "pass"), "");
        assert_eq!(field("state=a", "state"), "a");
    }
}
