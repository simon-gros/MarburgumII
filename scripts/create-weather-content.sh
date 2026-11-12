#!/usr/bin/env bash
# Regenerate the content weather and ground surfaces need (ADR-061) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/World/Ground/Surfaces/PM_*        physical materials naming what Lent's paving is made of, set on its paved
#                                             materials so tyres and feet can tell asphalt from setts
#   Content/Audio/Footsteps/                  Kenney Impact Sounds 1.0 footsteps (CC0) and the wet-street steps
#                                             generated from them (`scripts/mb2.sh generate wet-footsteps`)
# The landscape's surface map, Content/Marburgum/Ground/lent_surface.bin, comes from
#   tools/lent-ground: cargo run --release -- surface-kinds --surface Data/World/lent_ground/surface.png --out Content/Marburgum/Ground/lent_surface.bin
# Usage: scripts/create-weather-content.sh [surfaces|audio|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first. Rerun surfaces after a ground rebuild
# that creates the paved materials afresh.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
which="${1:-all}"

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

case "$which" in
    surfaces|audio) steps="$which" ;;
    all) steps="surfaces,audio" ;;
    *)
        echo "Usage: $0 [surfaces|audio|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumWeatherContent -Steps="$steps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
