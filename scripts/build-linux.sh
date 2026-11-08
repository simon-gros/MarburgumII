#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

build_script="$ue_root/Engine/Build/BatchFiles/Linux/Build.sh"
if [[ ! -f "$build_script" ]]; then
    echo "Missing UnrealBuildTool script: $build_script" >&2
    exit 1
fi

bash "$build_script" MarburgumIIEditor Linux Development \
    -Project="$repo_root/MarburgumII.uproject" -WaitMutex
