#!/usr/bin/env bash
# Regenerate the content vehicles need (ADR-039) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Vehicles/<definition>/                 bodywork, wheels and doors for every catalogue car, from
#                                                  Data/Gameplay/vehicles.v0.1.json (the original's vehicle_mesh.cpp)
#   Content/Vehicles/Materials/M_Vehicle           the paint material, one MI_<definition> per car
#   Content/Characters/Animations/A_EnterVehicle   the original's door clips on the mannequin skeleton
#   Content/Characters/Animations/A_ExitVehicle
#   Content/World/lent.umap                        the authored lent_hatchback
# Usage: scripts/create-vehicle-content.sh [mesh|animation|place|all]   (default: all)
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
    mesh|animation|place) steps="$which" ;;
    all) steps="mesh,animation,place" ;;
    *)
        echo "Usage: $0 [mesh|animation|place|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

"$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" \
    -run=MarburgumVehicleContent -Steps="$steps" \
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
