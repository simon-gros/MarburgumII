#!/usr/bin/env bash
# Regenerate the generated maps through the editor's Python commandlet:
#   Content/Maps/FrontEnd.umap  (client offline front-end, no gameplay authority)
#   Content/Maps/NetSmoke.umap  (Stage 0B network test map)
#   Content/Maps/Lent.umap      (Stage 1 fallback blockout district, plus Content/Blockout/Materials)
# Usage: scripts/create-maps.sh [frontend|netsmoke|lent|all]   (default: all)
# The maps reference Marburgum C++ classes, so build MarburgumIIEditor first.
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
    lent) run_script create_lent_map.py ;;
    all)
        run_script create_front_end_map.py
        run_script create_net_smoke_map.py
        run_script create_lent_map.py
        ;;
    *)
        echo "Usage: $0 [frontend|netsmoke|lent|all]" >&2
        exit 2
        ;;
esac
