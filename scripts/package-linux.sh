#!/usr/bin/env bash
# Build, cook and stage the separate Linux dedicated server and client.
# Output: Saved/StagedBuilds/LinuxServer and Saved/StagedBuilds/LinuxClient.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ue_root="${UE_ROOT:-}"
config="${1:-Development}"

case "$config" in
    Development|Shipping) ;;
    *)
        echo "Usage: $0 [Development|Shipping]" >&2
        exit 2
        ;;
esac

if [[ -z "$ue_root" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

bash "$ue_root/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
    -project="$repo_root/MarburgumII.uproject" \
    -noP4 -unattended -utf8output \
    -platform=Linux -client -clientconfig="$config" \
    -server -serverplatform=Linux -serverconfig="$config" \
    -build -cook -stage -pak \
    -map=/Game/Maps/NetSmoke
