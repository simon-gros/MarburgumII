#!/usr/bin/env bash
# Regenerate the content swimming needs (ADR-032) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Animations/A_Swim  the original game's swim cycle on the mannequin skeleton
#   Content/World/lent.umap               water volumes over the Drava, cut from the level's water sheet
# Usage: scripts/create-swim-content.sh [animation|water|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
which="${1:-all}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

case "$which" in
    animation|water) steps="$which" ;;
    all) steps="animation,water" ;;
    *)
        echo "Usage: $0 [animation|water|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumSwimContent -Steps="$steps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
