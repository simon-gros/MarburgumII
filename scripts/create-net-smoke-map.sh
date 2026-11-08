#!/usr/bin/env bash
# Regenerate Content/Maps/NetSmoke.umap through the editor's Python commandlet.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=pythonscript -script="$repo_root/scripts/editor/create_net_smoke_map.py" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
