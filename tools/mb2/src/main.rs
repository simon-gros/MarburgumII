//! `mb2`: Marburgum II repository tooling (source contract validators, the playtest freshness guard
//! and helpers the smoke-test scripts call). None of it compiles or runs Unreal; it checks sources and
//! files around the native build. Exit codes: 0 ok, 1 check failed, 2 usage error or blocked.

mod engine;
mod generate;
mod movement_check;
mod playtest;
mod render_probe;
mod repo;
mod store_check;
mod validate;

use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "\
Usage: mb2 <command>

  validate <ui-ontology|chat|gameplay|vehicles|combat|street|python-free|all>
                         Source-only contract checks (no Unreal build).
  generate gameplay      Write MarburgumGameplayCatalogue.h from Data/Gameplay/gameplay.v0.1.json.
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
                         Check a world-store file after a phase of a smoke test.
  movement-check <server log> <client log> <role> <character key> <honest|adversary> <DefaultGame.ini>
                         ADR-019 bound on how far a Lent smoke walker moved on the server.";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let words: Vec<&str> = args.iter().map(String::as_str).collect();
    let code = match words.as_slice() {
        ["validate", which] => validate::run(which),
        ["generate", which] => generate::run(which),
        ["engine-version", engine_root] => engine::run(&PathBuf::from(engine_root)),
        ["playtest", action, "--profile", profile] => playtest::run(action, &PathBuf::from(profile)),
        ["render-probe"] => render_probe::run(None),
        ["render-probe", engine_root] => render_probe::run(Some(&PathBuf::from(engine_root))),
        ["new-op"] => {
            println!("{}", uuid::Uuid::new_v4().simple().to_string().to_uppercase());
            0
        }
        ["store-check", "interaction", store, a, b, op] => store_check::interaction(store, a, b, op),
        ["store-check", "lent", store, m, w, op] => store_check::lent(store, m, w, op),
        ["store-check", "condition", store, r, c, s, phase] => store_check::condition(store, r, c, s, phase),
        ["store-check", "missions", store, key] => store_check::missions(store, key),
        ["store-check", "shop", store, key, op] => store_check::shop(store, key, op),
        ["movement-check", server, client, role, key, kind, ini] => movement_check::run(server, client, role, key, kind, ini),
        _ => {
            eprintln!("{USAGE}");
            2
        }
    };
    ExitCode::from(code)
}
