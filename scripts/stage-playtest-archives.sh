#!/usr/bin/env bash
# Cook and stage the Linux client and dedicated server, then copy both trees into a
# playtest run directory. The freshness guard only seals archives that live inside that run.
# Usage: UE_ROOT=... scripts/stage-playtest-archives.sh <run-dir>
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
run_dir="${1:?playtest run directory}"

if [[ "$run_dir" != /* ]]; then
    echo "Run directory must be absolute: $run_dir" >&2
    exit 2
fi

"$repo_root/scripts/package-linux.sh"

rm -rf "$run_dir/LinuxClient" "$run_dir/LinuxServer"
mkdir -p "$run_dir"
cp -a "$repo_root/Saved/StagedBuilds/LinuxClient" "$run_dir/LinuxClient"
cp -a "$repo_root/Saved/StagedBuilds/LinuxServer" "$run_dir/LinuxServer"
echo "Staged playtest archives in $run_dir"
