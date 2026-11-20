#!/usr/bin/env bash
# Regenerate what an authored NPC's body needs (ADR-080) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Props/SM_FishingRod          a period spinning rod built from code on M_Vehicle (rod step;
#                                                   needs scripts/create-vehicle-content.sh's M_Vehicle)
#   fishing_rod_r on SK_Mannequin                   the grip socket on hand_r, fitted to the hold pose (animation step)
#   Content/Characters/Animations/A_FishHold/Cast/Wait/Reel   the angler's and the player's fishing clips, left arm solved
#                                                   onto the reel handle or the rod butt key by key (animation step)
#   Content/Characters/Animations/A_NpcTalk         conversation gestures (animation step)
# Usage: scripts/create-npc-content.sh [rod|animation|all]   (default: all)
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
    rod|animation) steps="$which" ;;
    all) steps="rod,animation" ;;
    *)
        echo "Usage: $0 [rod|animation|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumNpcContent -Steps="$steps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
