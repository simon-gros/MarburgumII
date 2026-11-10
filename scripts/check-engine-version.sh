#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${1:-${UE_ROOT:-}}"

if [[ -z "$ue_root" ]]; then
    echo "Usage: UE_ROOT=/path/to/UnrealEngine-5.8.3 $0 [engine_root]" >&2
    exit 2
fi

version_file="$ue_root/Engine/Build/Build.version"
if [[ ! -f "$version_file" ]]; then
    echo "Missing Unreal Engine build metadata: $version_file" >&2
    exit 1
fi

"$repo_root/scripts/mb2.sh" engine-version "$ue_root"
