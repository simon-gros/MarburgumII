#!/usr/bin/env bash
# Regenerate the generated maps through the editor's Python commandlet:
#   Content/Maps/FrontEnd.umap  (client offline front-end, no gameplay authority)
#   Content/Maps/NetSmoke.umap  (Stage 0B network test map)
# Usage: scripts/create-maps.sh [frontend|netsmoke|all]   (default: all)
# The FrontEnd map references AMarburgumFrontEndGameMode, so build MarburgumIIEditor first.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
which="${1:-all}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

run_script() {
    "$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
        -run=pythonscript -script="$repo_root/scripts/editor/$1" \
        -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
}

case "$which" in
    frontend) run_script create_front_end_map.py ;;
    netsmoke) run_script create_net_smoke_map.py ;;
    all)
        run_script create_front_end_map.py
        run_script create_net_smoke_map.py
        ;;
    *)
        echo "Usage: $0 [frontend|netsmoke|all]" >&2
        exit 2
        ;;
esac
