#!/usr/bin/env bash
# Driving qualification (ADR-039) on the launch map, /Game/World/lent. Both clients start beside the
# authored lent_hatchback's driver door (?MB2Start=lent_hatchback), about 160 m west of the arrival start.
#
#   D  the driver (rendered offscreen). The parked car rests level on four wheels without creeping. D gets
#      in at the driver's door: the enter clip plays, then the body is hidden in the seat and the HUD names
#      the car at 0 km/h. With the passenger aboard it drives forward on the throttle, steers right, brakes
#      to a stop, reverses (REV on the HUD), stops, waits for the passenger to get out, gets out at its own
#      door and walks away; the car stays where it was left.
#   P  a passenger (-nullrhi). Once D holds the wheel it asks for the driver's seat (refused: seat_taken),
#      gets into the nearest free passenger seat, rides while D drives, and gets out once the car has stopped.
# The server must log the car ready, both entries and exits, the one refusal, and no impacts.
#
# D renders offscreen (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/vehicle-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/VehicleSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_VEHICLE_PORT:-7777}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/VehicleSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_d="mb2d-$token"
key_p="mb2p-$token"

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
        echo "UDP port $port is already in use; stop the other server or set MB2_VEHICLE_PORT." >&2
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
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=$key"
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
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2VehicleSmoke -MB2VehicleRole="$role" -MB2VehicleTimeout=300 \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2VSMOKE role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2VSMOKE role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2VSMOKE/    MB2VSMOKE/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2VehicleShot=$out_dir/$1"; }

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

echo "Dedicated server ($mode) on $map, port $port; evidence in $out_dir"
start_server "$out_dir/server.log"
# D first, so it is at the car before the passenger arrives; P follows a few seconds later.
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client D "$key_d" $(shot vehicle-d) &
pid_d=$!
sleep 6
run_client P "$key_p" -nullrhi &
pid_p=$!
code=0; wait "$pid_d" || code=$?; check_client D "$code"
code=0; wait "$pid_p" || code=$?; check_client P "$code"
sleep 1
stop_server
log="$out_dir/server.log"
expect_log "$log" "vehicle_ready" 1 "MB2VEHICLE role=server event=vehicle_ready vehicle=lent_hatchback definition=city_hatchback_a "
expect_log "$log" "entered_driver" 1 "MB2VEHICLE role=server event=entered vehicle=lent_hatchback .* seat=driver "
expect_log "$log" "entered_passenger" 1 "MB2VEHICLE role=server event=entered vehicle=lent_hatchback .* seat=\(front_passenger\|rear_left\|rear_right\) "
expect_log "$log" "refused_seat_taken" 1 "MB2VEHICLE role=server event=enter_refused vehicle=lent_hatchback .* seat=driver reason=seat_taken "
expect_log "$log" "exited" 2 "MB2VEHICLE role=server event=exited vehicle=lent_hatchback "
expect_log "$log" "exit_refused" 0 "MB2VEHICLE role=server event=exit_refused"
expect_log "$log" "seat_released_on_leave" 0 "MB2VEHICLE role=server event=seat_released"
expect_log "$log" "impacts" 0 "MB2VEHICLE role=server event=impact vehicle=lent_hatchback "
expect_log "$log" "unknown_definitions" 0 "MB2VEHICLE role=.* event=unknown_definition"

{
    echo "MarburgumII vehicle smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo
    grep -ah "MB2VSMOKE\|MB2VEHICLE\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2VSMOKE\|MB2VEHICLE\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
