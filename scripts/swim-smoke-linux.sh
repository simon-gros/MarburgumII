#!/usr/bin/env bash
# Swimming, held breath and drowning qualification (ADR-032) on the launch map, /Game/World/lent.
# Clients start at the arrival start on the Lent promenade, about 20 m east of the Drava.
#
# Phase 1 (fresh world store):
#   W  walks west into the river; swims at swim speed with the mouth up and the stroke playing; lets
#      go, turns upright treading water and slips under; holds its breath for ten seconds at about a minute per bar; swims up and back
#      east, and climbs out onto the bank; walks back in, lets go and drowns. Down, it is at the recovery
#      point and cannot move; it leaves while still down.
#   O  an observer (-nullrhi) connected while W swims: W's body is in the water on O's client and plays
#      the swim clip.
# The server must log W in the water, under, surfaced, out on the bank, drowned and at the recovery
#   point, and save it at logout with no vitality and the rest of its wait (recovery_s).
# Phase 2 (same store, server restarted):
#   W2 W's character: restored down at the recovery point with the wait it had left, cannot move, then
#      recovers with full vitality and breath and walks again. Saved rested, it is no entry at all.
#
# W and W2 render offscreen one at a time (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/swim-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/SwimSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_SWIM_PORT:-7777}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/SwimSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_w="mb2w-$token"
key_o="mb2o-$token"

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
store="$out_dir/world-store.json"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
server_pid=""
status=0

stop_server() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill -- "-$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    if [[ -n "$server_pid" ]]; then
        for _ in $(seq 1 40); do
            pgrep -g "$server_pid" >/dev/null || break
            sleep 0.25
        done
        pkill -KILL -g "$server_pid" 2>/dev/null || true
    fi
    server_pid=""
}
trap stop_server EXIT

start_server() {
    local log="$1"
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_SWIM_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" "${common_args[@]}" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 240); do
        grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log" && return 0
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            exit 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port within 120 s; see $log" >&2
    exit 1
}

# run_client <role> <character key> <extra args...>; output to client-<role>.log
run_client() {
    local role="$1" key="$2"
    shift 2
    local url="127.0.0.1:$port?MB2Start=arrival?MB2Character=$key"
    local log="$out_dir/client-$role.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            # A renderer that could not create its Vulkan device never connected; retrying is safe.
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$role.vulkan-init-failed-$attempt.log"
            echo "  client $role: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2SwimSmoke -MB2SwimRole="$role" -MB2SwimTimeout=300 \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2SWIM role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2SWIM role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2SWIM/    MB2SWIM/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2SwimShot=$out_dir/$1"; }

# expect_log <server log> <label> <want: number or ">=N"> <pattern>
expect_log() {
    local log="$1" label="$2" want="$3" pattern="$4" count
    count=$(grep -ac -- "$pattern" "$log" || true)
    echo "  server: $label=$count (want $want)"
    if [[ "$want" == ">="* ]]; then
        [[ "$count" -ge "${want#>=}" ]] || status=1
    else
        [[ "$count" -eq "$want" ]] || status=1
    fi
}

# ---------------------------------------------------------------- phase 1
echo "Phase 1: dedicated server ($mode) on $map, port $port, fresh store; evidence in $out_dir"
start_server "$out_dir/server-1.log"
run_client O "$key_o" -nullrhi &
pid_o=$!
code=0
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client W "$key_w" $(shot swim-w) || code=$?
check_client W "$code"
code=0; wait "$pid_o" || code=$?; check_client O "$code"
sleep 1
stop_server
log1="$out_dir/server-1.log"
expect_log "$log1" "swimmer_under" ">=2" "MB2COND role=server event=under .* character=$key_w "
expect_log "$log1" "swimmer_surfaced" ">=1" "MB2COND role=server event=surfaced .* character=$key_w "
expect_log "$log1" "swimmer_in_water" ">=2" "MB2COND role=server event=in_water .* character=$key_w "
expect_log "$log1" "swimmer_left_water" ">=1" "MB2COND role=server event=left_water .* character=$key_w "
# Climbing out (ADR-032) only fires where the bank is a step; the Lent bank here is a slope a swimmer wades up.
expect_log "$log1" "swimmer_climbed_out" ">=0" "MB2COND role=server event=climbed_out .* character=$key_w "
expect_log "$log1" "swimmer_drowned" 1 "MB2COND role=server event=drowned .* character=$key_w "
expect_log "$log1" "swimmer_recovery_point" 1 "MB2COND role=server event=recovery_point .* character=$key_w "
expect_log "$log1" "swimmer_recovered_early" 0 "MB2COND role=server event=recovered .* character=$key_w "
expect_log "$log1" "swimmer_saved_down" 1 "MB2COND role=server event=condition_saved why=logout character=$key_w vitality=0 .* recovery_s=[45][0-9]$"
expect_log "$log1" "save_failures" 0 "MB2COND role=server event=condition_save_failed"
saved_down="$(grep -a -m1 "event=condition_saved why=logout character=$key_w " "$log1" | sed 's/.*event=condition_saved //' || true)"
cp "$store" "$out_dir/world-store-after-phase-1.json" 2>/dev/null || true

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"
code=0
# shellcheck disable=SC2046
run_client W2 "$key_w" $(shot swim-w2) || code=$?
check_client W2 "$code"
sleep 1
stop_server
log2="$out_dir/server-2.log"
expect_log "$log2" "restored_down" 1 "MB2COND role=server event=condition_restored .* character=$key_w source=store vitality=0 "
expect_log "$log2" "recovery_point" 1 "MB2COND role=server event=recovery_point .* character=$key_w "
expect_log "$log2" "recovered" 1 "MB2COND role=server event=recovered .* character=$key_w "
expect_log "$log2" "saved_rested" 1 "MB2COND role=server event=condition_saved why=logout character=$key_w vitality=100 stamina=100 winded=0 "
expect_log "$log2" "save_failures" 0 "MB2COND role=server event=condition_save_failed"
store_conditions="$(tr -d ' \t\n' < "$store" | grep -o '"conditions":{[^}]*}' || true)"
echo "  store conditions after phase 2: ${store_conditions:-none} (want empty: a rested body is no entry)"
[[ "$store_conditions" == '"conditions":{}' ]] || status=1

{
    echo "MarburgumII swimming smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "saved_down: ${saved_down:-none}"
    echo
    grep -ah "MB2SWIM\|MB2COND\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log \
        | grep -v "MB2COND role=server event=\(winded\|breath_back\)" \
        | sed 's/^.*\(MB2SWIM\|MB2COND\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
