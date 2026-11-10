#!/usr/bin/env bash
# Regenerate the generated maps with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Maps/FrontEnd.umap  (client offline front-end, no gameplay authority)
#   Content/Maps/NetSmoke.umap  (Stage 0B network test map)
#   Content/Maps/Lent.umap      (Stage 1 fallback blockout district, plus Content/Blockout/Materials)
# Usage: scripts/create-maps.sh [frontend|netsmoke|lent|all]   (default: all)
# The commandlet and the maps' classes live in the Editor target, so build MarburgumIIEditor first.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
which="${1:-all}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

case "$which" in
    frontend|netsmoke|lent) maps="$which" ;;
    all) maps="frontend,netsmoke,lent" ;;
    *)
        echo "Usage: $0 [frontend|netsmoke|lent|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumCreateMaps -Maps="$maps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
