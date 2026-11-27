#!/usr/bin/env bash
# Make /Game/World/lent a World Partition map and build its HLODs (ADR-091), with the editor commandlets:
#   convert   the engine's WorldPartitionConvertCommandlet: each actor in its own package under
#             Content/__ExternalActors__/World/lent, the landscape split into streaming proxies (skipped once done)
#   settings  MarburgumPartitionLevel: the streaming grid, the HLOD layer /Game/World/HLOD/HLOD_Lent, no server
#             streaming, and which actors stream
#   ground    MarburgumPartitionLevel -Ground: the street lamps', plants' and street furniture's ground on the whole level,
#             <placement file>_ground.json beside each, as a client holds only the cells round its player
#   hlods     the engine's HLOD builder: one merged Nanite mesh per 256 m cell under Content/__ExternalActors__
# Usage: scripts/partition-world.sh [convert|settings|ground|hlods|all]   (default: all)
# Build MarburgumIIEditor first. Conversion and the HLOD build render (no -nullrhi); -RenderOffScreen keeps them off
# the desktop. The five by five needs more than 16 GB per editor process, so run this as its own unit
# (systemd-run --user ... -p MemoryMax=28G), as for packaging.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
which="${1:-all}"
map=/Game/World/lent

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.4 installation root." >&2
    exit 2
fi

case "$which" in
    convert|settings|ground|hlods) steps="$which" ;;
    all) steps="convert settings ground hlods" ;;
    *)
        echo "Usage: $0 [convert|settings|ground|hlods|all]" >&2
        exit 2
        ;;
esac

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

editor() {
    "$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" "$@" \
        -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
}

for step in $steps; do
    case "$step" in
        convert)
            if [[ -d "$repo_root/Content/__ExternalActors__/World/lent" ]]; then
                echo "$map is already partitioned; skipping the conversion."
                continue
            fi
            editor -run=WorldPartitionConvertCommandlet "$map" -AllowCommandletRendering -RenderOffScreen
            ;;
        settings)
            editor -run=MarburgumPartitionLevel -Map="$map" -nullrhi
            ;;
        ground)
            editor -run=MarburgumPartitionLevel -Map="$map" -Ground -nullrhi
            ;;
        hlods)
            editor -run=WorldPartitionBuilderCommandlet "$map" -Builder=WorldPartitionHLODsBuilder \
                -AllowCommandletRendering -RenderOffScreen
            ;;
    esac
done
