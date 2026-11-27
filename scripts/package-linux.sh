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
    echo "Set UE_ROOT to your Unreal Engine 5.8.4 installation root." >&2
    exit 2
fi

"$repo_root/scripts/check-engine-version.sh" "$ue_root"

# -WaitForUATMutex: one UAT per engine install; another session's packaging is waited for, not failed on.
bash "$ue_root/Engine/Build/BatchFiles/RunUAT.sh" -WaitForUATMutex BuildCookRun \
    -project="$repo_root/MarburgumII.uproject" \
    -noP4 -unattended -utf8output \
    -platform=Linux -client -clientconfig="$config" \
    -server -serverplatform=Linux -serverconfig="$config" \
    -build -cook -stage -pak \
    -map=/Game/Maps/FrontEnd+/Game/Maps/NetSmoke+/Game/Maps/Lent+/Game/World/lent \
    -ubtargs=-WaitMutex

# The build stamp: which commit the staged trees came from and the digests of the two executables, so a smoke or
# a playtest can tell this build from a stale one (scripts/build-stamp.sh).
"$repo_root/scripts/build-stamp.sh" write "$config"
