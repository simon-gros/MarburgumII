//! Source-level consistency checks. Passing them says nothing about runtime behaviour; the native
//! smoke tests (scripts/*-smoke-linux.sh) are the runtime tests.

mod chat;
mod combat;
mod gameplay;
mod python_free;
mod ui_ontology;
mod vehicles;

use crate::repo::{self, Problems};

/// A check returns its success summary lines, or records problems.
type Check = fn(&std::path::Path, &mut Problems) -> Vec<String>;

const CHECKS: [(&str, &str, Check); 6] = [
    ("ui-ontology", "UI/HUD ontology", ui_ontology::check),
    ("chat", "Chat source contract", chat::check),
    ("gameplay", "Gameplay contract", gameplay::check),
    ("vehicles", "Vehicle contract", vehicles::check),
    ("combat", "Combat contract", combat::check),
    ("python-free", "Python-free tooling", python_free::check),
];

pub fn run(which: &str) -> u8 {
    let selected: Vec<_> = CHECKS.iter().filter(|(name, _, _)| which == "all" || which == *name).collect();
    if selected.is_empty() {
        eprintln!("Unknown check '{which}'; use ui-ontology, chat, gameplay, vehicles, combat, python-free or all.");
        return 2;
    }
    let root = repo::root();
    let mut failed = false;
    for (_, label, check) in selected {
        let mut problems = Problems::default();
        let summary = check(&root, &mut problems);
        if problems.is_empty() {
            for line in summary {
                println!("{line}");
            }
        } else {
            failed = true;
            eprintln!("{label} check FAILED:");
            for problem in &problems.0 {
                eprintln!("  - {problem}");
            }
        }
    }
    u8::from(failed)
}
