#!/usr/bin/env bash
# Fighting qualification (ADR-041) on the launch map, /Game/World/lent. The server spawns the original's Lent fight
# cast; both clients start in front of the Belligerent drinker (?MB2Start=fight_lent_drunk), one after the other.
#
#   B  beaten (-nullrhi). Jabs the drinker once, so he turns on B, then stands with its hands down until his
#      blows put it on the floor: it shows the fall, cannot swing from the ground, and he gives up on it.
#   A  the fight (rendered offscreen). A jab out of reach misses and leaves him alone. In reach, a jab lands for
#      7 and a second at once is too soon. He turns on A and the Evening walker steps away. A's raised guard
#      (its clip shows) blocks his jab for a quarter and pays its stamina; hands down, his next jab lands in
#      full. A heavy swing lands for 14.7, then A keeps swinging until he is down (his fall plays); a jab at
#      somebody on the floor reaches nobody.
# The server must log the three people spawned and ready, the drinker struck down once and every swing ruled; the
# clients must log impact sounds for hits, the blocked blow and the fall.
#
# A renders offscreen (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/combat-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/CombatSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_COMBAT_PORT:-7777}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/CombatSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_a="mb2a-$token"
key_b="mb2b-$token"

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
        echo "UDP port $port is already in use; stop the other server or set MB2_COMBAT_PORT." >&2
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
    local url="127.0.0.1:$port?MB2Start=fight_lent_drunk?MB2Character=$key"
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
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2CombatSmoke -MB2CombatRole="$role" -MB2CombatTimeout=240 \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2CSMOKE role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2CSMOKE role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2CSMOKE/    MB2CSMOKE/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2CombatShot=$out_dir/$1"; }

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
# B first: it is beaten down and leaves; then A fights the drinker, who has given up on B.
code=0; run_client B "$key_b" -nullrhi || code=$?; check_client B "$code"
# shellcheck disable=SC2046  # word splitting of the argument list is intended
code=0; run_client A "$key_a" $(shot combat-a) || code=$?; check_client A "$code"
sleep 1
stop_server
log="$out_dir/server.log"
expect_log "$log" "npc_spawned" 3 "MB2COMBAT role=server event=npc_spawned npc="
expect_log "$log" "npc_ready" 3 "MB2COMBAT role=server event=npc_ready npc="
expect_log "$log" "drinker_down" 1 "MB2COMBAT role=server event=npc_struck npc=lent_drunk .* down=1"
expect_log "$log" "drinker_strikes" ">=3" "MB2COMBAT role=server event=npc_strike npc=lent_drunk "
expect_log "$log" "blocked_by_guard" ">=1" "MB2COMBAT role=server event=struck .* outcome=blocked "
expect_log "$log" "player_beaten" 1 "MB2COND role=server event=beaten "
expect_log "$log" "swings_ruled" ">=6" "MB2COMBAT role=server event=swing "
expect_log "$log" "refused_too_soon" ">=1" "MB2COMBAT role=server event=swing .* ok=0 reason=too_soon "
expect_log "$log" "refused_from_floor" 1 "MB2COMBAT role=server event=swing .* ok=0 reason=incapacitated "
expect_log "$log" "walker_avoids" ">=1" "MB2COMBAT role=server event=npc_.* npc=lent_passerby .* intent=avoid "
expect_log "$log" "unknown_npc" 0 "MB2COMBAT role=server event=npc_unknown"
expect_log "$out_dir/client-A.log" "impact_hit_sounds" ">=2" "MB2COMBAT role=client event=impact outcome=hit .* sound=impactPunch_"
expect_log "$out_dir/client-A.log" "impact_blocked_sound" ">=1" "MB2COMBAT role=client event=impact outcome=blocked .* sound=impactSoft_medium_"
expect_log "$out_dir/client-A.log" "impact_floor_sound" ">=1" "MB2COMBAT role=client event=impact .* floored=1 .* fall=impactSoft_heavy_"

{
    echo "MarburgumII combat smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo
    grep -ah "MB2CSMOKE\|MB2COMBAT\|MB2COND role=server event=\(beaten\|recovery_point\)\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2CSMOKE\|MB2COMBAT\|MB2COND\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
