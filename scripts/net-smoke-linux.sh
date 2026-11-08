#!/usr/bin/env bash
# Stage 0B network smoke: one dedicated server process, then separate client processes.
#
#   Phase A: one client joins, is refused a contract change, commits one idempotent
#            operation, disconnects, reconnects and confirms the replay is a duplicate.
#   Phase B: two clients join concurrently and both observe the same server-owned state.
#
# Usage: scripts/net-smoke-linux.sh [staged|editor]
#   staged  packaged MarburgumIIServer / MarburgumIIClient from scripts/package-linux.sh (default)
#   editor  uncooked UnrealEditor -server / -game processes (requires UE_ROOT)
# Evidence is written to Saved/NetSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_SMOKE_PORT:-7777}"
map="/Game/Maps/NetSmoke"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/NetSmoke/$(date -u +%Y%m%dT%H%M%SZ)"

case "$mode" in
    staged)
        server_cmd=("$repo_root/Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh")
        client_cmd=("$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh")
        ;;
    editor)
        if [[ -z "${UE_ROOT:-}" ]]; then
            echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
            exit 2
        fi
        "$repo_root/scripts/check-engine-version.sh" "$UE_ROOT"
        server_cmd=("$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$uproject" -server)
        client_cmd=("$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$uproject" -game)
        ;;
    *)
        echo "Usage: $0 [staged|editor]" >&2
        exit 2
        ;;
esac

for executable in "${server_cmd[0]}" "${client_cmd[0]}"; do
    if [[ ! -x "$executable" ]]; then
        echo "Missing executable: $executable (run scripts/package-linux.sh first?)" >&2
        exit 1
    fi
done

mkdir -p "$out_dir"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

run_client() {
    local tag="$1"
    shift
    "${client_cmd[@]}" "127.0.0.1:$port" "${common_args[@]}" -nullrhi \
        -MB2NetSmoke -MB2SmokeTag="$tag" -MB2SmokeTimeout=90 "$@" \
        > "$out_dir/client-$tag.log" 2>&1
}

client_passed() {
    grep -q "MB2SMOKE role=client tag=$1 event=pass" "$out_dir/client-$1.log"
}

echo "Starting dedicated server ($mode) on port $port; evidence in $out_dir"
"${server_cmd[@]}" "$map" -port="$port" "${common_args[@]}" > "$out_dir/server.log" 2>&1 &
server_pid=$!

for _ in $(seq 1 240); do
    if grep -q "MB2SMOKE role=server event=ready" "$out_dir/server.log"; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "Server exited before becoming ready; see $out_dir/server.log" >&2
        exit 1
    fi
    sleep 0.5
done
if ! grep -q "MB2SMOKE role=server event=ready" "$out_dir/server.log"; then
    echo "Server did not become ready within 120 s; see $out_dir/server.log" >&2
    exit 1
fi

status=0

echo "Phase A: one client, refused mutation, idempotent operation, disconnect/reconnect"
run_client solo -MB2SmokePeers=1 -MB2SmokeReconnect || true
if client_passed solo; then echo "  solo: PASS"; else echo "  solo: FAIL"; status=1; fi

echo "Phase B: two concurrent clients observing shared server-owned state"
run_client peer1 -MB2SmokePeers=2 &
peer1_pid=$!
run_client peer2 -MB2SmokePeers=2 &
peer2_pid=$!
wait "$peer1_pid" || true
wait "$peer2_pid" || true
for tag in peer1 peer2; do
    if client_passed "$tag"; then echo "  $tag: PASS"; else echo "  $tag: FAIL"; status=1; fi
done

sleep 2
cleanup
server_pid=""

# Authority-side expectations: 4 joins (solo twice), 3 refused contract changes,
# 3 committed operations and 4 duplicates (one per client plus solo's post-reconnect replay).
count() { grep -ac "$1" "$out_dir/server.log" || true; }
logins=$(count "MB2SMOKE role=server event=login")
refused=$(count "event=contract_change_refused")
applied=$(count "event=operation .* outcome=applied")
duplicates=$(count "event=operation .* outcome=duplicate")
echo "Server: logins=$logins refused=$refused applied=$applied duplicates=$duplicates"
if [[ "$logins" -ne 4 || "$refused" -ne 3 || "$applied" -ne 3 || "$duplicates" -ne 4 ]]; then
    echo "  server expectations: FAIL (want logins=4 refused=3 applied=3 duplicates=4)"
    status=1
fi

{
    echo "MarburgumII Stage 0B network smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config || echo ' (dirty)')"
    echo "mode: $mode"
    echo "host: $(uname -srm)"
    echo "server_cmd: ${server_cmd[*]} $map -port=$port ${common_args[*]}"
    echo "client_cmd: ${client_cmd[*]} 127.0.0.1:$port ${common_args[*]} -nullrhi -MB2NetSmoke ..."
    echo
    grep -ah "MB2SMOKE" "$out_dir/server.log" "$out_dir"/client-*.log | sed 's/^.*MB2SMOKE/MB2SMOKE/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
