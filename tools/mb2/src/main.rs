//! `mb2`: Marburgum II repository tooling (source contract validators, the playtest freshness guard
//! and helpers the smoke-test scripts call). None of it compiles or runs Unreal; it checks sources and
//! files around the native build. Exit codes: 0 ok, 1 check failed, 2 usage error or blocked.

mod crowd_summary;
mod engine;
mod generate;
mod gfx_baseline;
mod history;
mod movement_check;
mod playtest;
mod render_probe;
mod repo;
mod store_check;
mod survey_views;
mod validate;
mod wet_footsteps;
mod wire;

use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "\
Usage: mb2 <command>

  validate <ui-ontology|chat|gameplay|vehicles|combat|street|lights|python-free|scripts|accounts|history|all>
                         Source-only contract checks (no Unreal build).
  generate gameplay      Write MarburgumGameplayCatalogue.h from Data/Gameplay/gameplay.v0.1.json.
  generate wet-footsteps Write the wet-street footsteps (SourceArt/Audio/Footsteps/Generated) from the dry ones.
  engine-version <engine_root>
                         Check Engine/Build/Build.version against .unreal-engine-version.
  playtest <build|verify|launch> --profile <file>
                         Fail-closed freshness guard for native client/server playtests.
  render-probe [engine_root]
                         JSON report of the NVIDIA plugins, GPU, driver runtime and Smooth Motion
                         layer the optional render features need (ADR-029).
  new-op                 Print a fresh operation id (32 upper-case hex digits).
  store-check interaction <store> <key_a> <key_b> <first_op>
  store-check lent <store> <key_m> <key_w> <first_op>
  store-check condition <store> <key_r> <key_c> <key_s> <1|2>
  store-check missions <store> <key>
  store-check shop <store> <key> <first_op>
  store-check story <store> <key>
                         Check a world-store file after a phase of a smoke test.
  movement-check <server log> <client log> <role> <character key> <honest|adversary> <DefaultGame.ini>
                         ADR-019 bound on how far a Lent smoke walker moved on the server.
  survey-views <walk graph> [seed]
                         The visual survey's fixed cameras as one MB2ViewShots string (ADR-072).
  gfx-views              The graphics roadmap's extra cameras (river, quay, roofs) as an MB2ViewShots string.
  gfx-baseline <dir>     GFX-00: baseline.csv and baseline.json from scripts/gfx-baseline-linux.sh's scenes.
  crowd-summary <out dir> <sha> <day|night> <expected clock>
                         Verdict and summary.json of scripts/street-multiplayer-smoke-linux.sh.
  fetch-art-reference [--output <dir>] [--force]
                         Download the auto_fetch=yes rows of the licensed art-reference manifest (HTTPS only,
                         outside Git by default) and write their SHA-256 checksums (ADR-077).
  wire-relay <listen addr> <server addr> <record file>
                         Forward one game client's UDP to the server and record every datagram (ADR-083).
  wire-scan <record file> <text>...
                         How many recorded datagrams from each side carry each text, at any bit offset.";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let words: Vec<&str> = args.iter().map(String::as_str).collect();
    let code = match words.as_slice() {
        ["validate", which] => validate::run(which),
        ["generate", "wet-footsteps"] => wet_footsteps::run(),
        ["generate", which] => generate::run(which),
        ["engine-version", engine_root] => engine::run(&PathBuf::from(engine_root)),
        ["playtest", action, "--profile", profile] => {
            playtest::run(action, &PathBuf::from(profile))
        }
        ["render-probe"] => render_probe::run(None),
        ["render-probe", engine_root] => render_probe::run(Some(&PathBuf::from(engine_root))),
        ["new-op"] => {
            println!(
                "{}",
                uuid::Uuid::new_v4().simple().to_string().to_uppercase()
            );
            0
        }
        ["store-check", "interaction", store, a, b, op] => {
            store_check::interaction(store, a, b, op)
        }
        ["store-check", "lent", store, m, w, op] => store_check::lent(store, m, w, op),
        ["store-check", "condition", store, r, c, s, phase] => {
            store_check::condition(store, r, c, s, phase)
        }
        ["store-check", "missions", store, key] => store_check::missions(store, key),
        ["store-check", "shop", store, key, op] => store_check::shop(store, key, op),
        ["store-check", "story", store, key] => store_check::story(store, key),
        ["movement-check", server, client, role, key, kind, ini] => {
            movement_check::run(server, client, role, key, kind, ini)
        }
        ["survey-views", graph] => survey_views::run(graph, None),
        ["survey-views", graph, seed] => survey_views::run(graph, Some(seed)),
        ["gfx-views"] => gfx_baseline::print_views(),
        ["gfx-baseline", dir] => gfx_baseline::run(dir),
        ["crowd-summary", out, sha, case_name, clock] => {
            crowd_summary::run(out, sha, case_name, clock)
        }
        ["fetch-art-reference", rest @ ..] => history::fetch(rest),
        ["wire-relay", listen, server, record] => wire::relay(listen, server, record),
        ["wire-scan", record, needles @ ..] if !needles.is_empty() => wire::scan(record, needles),
        _ => {
            eprintln!("{USAGE}");
            2
        }
    };
    ExitCode::from(code)
}
