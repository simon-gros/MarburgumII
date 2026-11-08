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

python3 - "$version_file" "$repo_root/.unreal-engine-version" <<'PY'
import json
import pathlib
import sys

build_file = pathlib.Path(sys.argv[1])
pin_file = pathlib.Path(sys.argv[2])
required = pin_file.read_text(encoding="utf-8").strip()
data = json.loads(build_file.read_text(encoding="utf-8"))
actual = ".".join(str(data[key]) for key in ("MajorVersion", "MinorVersion", "PatchVersion"))
if actual != required:
    print(f"ERROR: Unreal Engine {actual} installed; Marburgum II requires {required}.", file=sys.stderr)
    sys.exit(1)
print(f"Verified Unreal Engine {actual} from {build_file}")
PY
