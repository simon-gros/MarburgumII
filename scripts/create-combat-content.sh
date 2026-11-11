#!/usr/bin/env bash
# Regenerate the content fighting needs (ADR-041) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Animations/A_FightGuard   the original's guard loop on the mannequin skeleton
#   Content/Characters/Animations/A_FightJab     the original's jab
#   Content/Characters/Animations/A_FightHeavy   the original's committed swing
#   Content/Audio/Combat/                        Kenney Impact Sounds 1.0 (CC0) from SourceArt/Audio/Combat
# Usage: scripts/create-combat-content.sh [animation|audio|all]   (default: all)
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
    animation|audio) steps="$which" ;;
    all) steps="animation,audio" ;;
    *)
        echo "Usage: $0 [animation|audio|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumCombatContent -Steps="$steps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
