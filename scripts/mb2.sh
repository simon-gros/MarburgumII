#!/usr/bin/env bash
# Run the Rust repository tool (tools/mb2), building it first when its sources changed.
# Usage: scripts/mb2.sh <command> ...   (scripts/mb2.sh with no arguments lists the commands)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$repo_root/tools/mb2/Cargo.toml"

if ! command -v cargo >/dev/null; then
    echo "Rust (cargo) is required for the repository tooling; install stable Rust." >&2
    exit 2
fi
cargo build --quiet --release --manifest-path "$manifest"
exec "$repo_root/tools/mb2/target/release/mb2" "$@"
