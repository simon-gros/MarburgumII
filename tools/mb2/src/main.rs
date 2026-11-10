//! `mb2`: Marburgum II repository tooling (source contract validators, the playtest freshness guard
//! and helpers the smoke-test scripts call). None of it compiles or runs Unreal; it checks sources and
//! files around the native build. Exit codes: 0 ok, 1 check failed, 2 usage error or blocked.

mod engine;
mod playtest;
mod repo;
mod store_check;
mod validate;

use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "\
Usage: mb2 <command>

  validate <ui-ontology|chat|gameplay|python-free|all>
                         Source-only contract checks (no Unreal build).
  engine-version <engine_root>
                         Check Engine/Build/Build.version against .unreal-engine-version.
  playtest <build|verify|launch> --profile <file>
                         Fail-closed freshness guard for native client/server playtests.
  new-op                 Print a fresh operation id (32 upper-case hex digits).
  store-check interaction <store> <key_a> <key_b> <first_op>
  store-check lent <store> <key_m> <key_w> <first_op>
                         Check a world-store file after phase 1 of a smoke test.";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let words: Vec<&str> = args.iter().map(String::as_str).collect();
    let code = match words.as_slice() {
        ["validate", which] => validate::run(which),
        ["engine-version", engine_root] => engine::run(&PathBuf::from(engine_root)),
        ["playtest", action, "--profile", profile] => playtest::run(action, &PathBuf::from(profile)),
        ["new-op"] => {
            println!("{}", uuid::Uuid::new_v4().simple().to_string().to_uppercase());
            0
        }
        ["store-check", "interaction", store, a, b, op] => store_check::interaction(store, a, b, op),
        ["store-check", "lent", store, m, w, op] => store_check::lent(store, m, w, op),
        _ => {
            eprintln!("{USAGE}");
            2
        }
    };
    ExitCode::from(code)
}
