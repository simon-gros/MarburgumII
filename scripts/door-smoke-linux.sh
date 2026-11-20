#!/usr/bin/env bash
# Door and interior qualification (ADR-076) on the launch map, /Game/World/lent, against a real dedicated server started
# with -MB2DoorStarts. Both players start at one building's street door (?MB2Start=door_<id>, default Hotel Lent).
#
# Phase 1, two clients at once:
#   walker   (renders) opens the street door, sees the rival shut it and reverse the swing, stands in the swing while
#            the rival tries to shut it, then walks every room with ordinary movement input, opening each room door with
#            E, and back out to the street. One capture outside, in the doorway, in the front room and in every room.
#   rival    (no renderer) shuts the door and reverses it mid-swing, tries to shut it on the walker, asks for a room
#            door out of reach, and disconnects.
#   observer then joins while the walker is still there and must see exactly the door states the walker left.
# Phase 2, server restarted: a new observer must see every door of the building shut (doors are not saved).
# The server log must show the opening, the refusal with somebody in the swing, and the refusal out of reach.
#
# Usage: scripts/door-smoke-linux.sh [staged|editor] [building id]
# MB2_DOOR_PORT (default 27895; never 7777 or 17777, which are for playtests).
# Evidence is written to Saved/DoorSmoke/<UTC timestamp>/.
set -uo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
building="${2:-r1539756}"
port="${MB2_DOOR_PORT:-27895}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/DoorSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"

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
        echo "Usage: $0 [staged|editor] [building id]" >&2
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
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
headless_args=(-NullRHI)
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
    # A June noon, clear and dry, so the captures read the same on any day.
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2Weather=clear_dry \
        -MB2Clock=2026-06-18T10:00:00Z -MB2DoorStarts "${common_args[@]}" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 240); do
        grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log" && return 0
        kill -0 "$server_pid" 2>/dev/null || { echo "Server exited before becoming ready; see $log" >&2; return 1; }
        sleep 0.5
    done
    echo "Server did not become ready; see $log" >&2
    return 1
}

url() {
    echo "127.0.0.1:$port?MB2Character=mb2d-$token-$1?MB2Start=door_$building"
}

client_result() {
    local label="$1" log="$2" code="$3"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2DSMOKE role=[a-z]* event=pass" "$log"; then
        echo "  $label: PASS"
    else
        echo "  $label: FAIL (exit $code)"
        grep -ah "MB2DSMOKE role=[a-z]* event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2DSMOKE/    MB2DSMOKE/' || true
        status=1
    fi
}

if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use; stop the other server or set MB2_DOOR_PORT." >&2
    exit 1
fi
echo "Evidence in $out_dir"
echo "Phase 1: dedicated server ($mode) on $map, port $port, building $building"
start_server "$out_dir/server-1.log" || exit 1

"${client_cmd[@]}" "$(url a)" "${common_args[@]}" "${render_args[@]}" -MB2DoorSmoke=walker -MB2DoorBuilding="$building" \
    -MB2DoorShot="$out_dir/walker" -MB2DoorTimeout=420 -MB2DoorHold=150 > "$out_dir/walker.log" 2>&1 &
walker_pid=$!
"${client_cmd[@]}" "$(url b)" "${common_args[@]}" "${headless_args[@]}" -MB2DoorSmoke=rival -MB2DoorBuilding="$building" \
    -MB2DoorTimeout=300 > "$out_dir/rival.log" 2>&1
rival_code=$?
client_result rival "$out_dir/rival.log" "$rival_code"

# The late joiner, while the walker still stands outside: the same doors, in the same states.
for _ in $(seq 1 600); do
    grep -aq "MB2DSMOKE role=walker event=\(route_done\|fail\)" "$out_dir/walker.log" && break
    kill -0 "$walker_pid" 2>/dev/null || break
    sleep 0.5
done
left=$(grep -ah "MB2DSMOKE role=walker event=route_done" "$out_dir/walker.log" | sed -n 's/.* states=\([a-z,]*\).*/\1/p' | tail -1)
if [[ -n "$left" ]]; then
    "${client_cmd[@]}" "$(url c)" "${common_args[@]}" "${headless_args[@]}" -MB2DoorSmoke=observer -MB2DoorBuilding="$building" \
        -MB2DoorExpect="$left" -MB2DoorTimeout=180 > "$out_dir/observer-1.log" 2>&1
    client_result "observer (late join)" "$out_dir/observer-1.log" $?
else
    echo "  observer (late join): SKIPPED, the walker did not finish its route"
    status=1
fi
wait "$walker_pid"
client_result walker "$out_dir/walker.log" $?
stop_server

echo "Phase 2: server restarted"
start_server "$out_dir/server-2.log" || exit 1
"${client_cmd[@]}" "$(url c)" "${common_args[@]}" "${headless_args[@]}" -MB2DoorSmoke=observer -MB2DoorBuilding="$building" \
    -MB2DoorExpect=closed -MB2DoorTimeout=180 > "$out_dir/observer-2.log" 2>&1
client_result "observer (after restart)" "$out_dir/observer-2.log" $?
stop_server

# expect_log <log> <label> <want: number or ">=N"> <pattern>
expect_log() {
    local log="$1" label="$2" want="$3" pattern="$4" count
    count=$(grep -ac -- "$pattern" "$log" || true)
    echo "  $label=$count (want $want)"
    if [[ "$want" == ">="* ]]; then
        [[ "$count" -ge "${want#>=}" ]] || status=1
    else
        [[ "$count" -eq "$want" ]] || status=1
    fi
}
expect_log "$out_dir/server-1.log" "server door_starts" 1 "MB2INTERIOR event=door_starts starts=[1-9]"
expect_log "$out_dir/server-1.log" "server entrance_open" 1 "MB2INTERIOR event=entrance_open id=$building "
expect_log "$out_dir/server-1.log" "server door_opening" ">=2" "event=door_opening .* building=$building"
expect_log "$out_dir/server-1.log" "server door_closing" ">=1" "event=door_closing .* building=$building"
expect_log "$out_dir/server-1.log" "server refused_in_swing" ">=1" "event=door_refused_blocked .* building=$building"
expect_log "$out_dir/server-1.log" "server refused_reach" ">=1" "event=door_refused .* building=$building reason=reach"
expect_log "$out_dir/walker.log" "walker rooms_shot" ">=1" "MB2DSMOKE role=walker event=route_done .* rooms_shot=[1-9]"

{
    echo "MarburgumII door smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "building: $building"
    echo "host: $(uname -srm)"
    echo
    grep -ah "MB2DSMOKE\|MB2INTERIOR event=\(classified\|door_starts\|entrance\|stream\|unstream\)\|event=door_\|MB2SMOKE role=server event=\(ready\|login\|logout\)" \
        "$out_dir"/server-*.log "$out_dir"/walker.log "$out_dir"/rival.log "$out_dir"/observer-*.log \
        | sed 's/^.*\(MB2DSMOKE\|MB2INTERIOR\|MB2INTERACT\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
