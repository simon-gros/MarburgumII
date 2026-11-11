#!/usr/bin/env bash
# Street people qualification (ADR-044) on the launch map, /Game/World/lent. The server fills the pavements round
# the player from the walk graph and, with -MB2StreetProbe, puts two people in the way of the first car a player
# drives off in: one who will not jump clear (and is easily out) and one further on who will. One client, rendered
# offscreen, starts at the hatchback's driver door (?MB2Start=lent_hatchback):
#
#   people appear round it, out of sight, and walk; it throws punches at the air and people near flee or watch;
#   it drives off: the first probe goes down under the car, the second jumps aside; people answer the body.
#   and, as it arrives, a person by the Drava is scared from the land side and runs along the bank, not into the
#   river (ADR-046); nobody on the street is ever in the water.
#
# The server must log the walk graph loaded, people spawned and despawned, reactions to the fight, the run-over,
# the dodge and the body. Evidence is written to Saved/StreetSmoke/<UTC timestamp>/.
# Usage: scripts/street-smoke-linux.sh [staged|editor]   (port: MB2_STREET_PORT, default 27761)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_STREET_PORT:-27761}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/StreetSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
key="mb2s-t$(date +%s)$RANDOM"

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
        echo "UDP port $port is already in use; stop the other server or set MB2_STREET_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" -MB2StreetProbe -MB2StreetSeed=1990 "${common_args[@]}" > "$log" 2>&1 &
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

run_client() {
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=$key"
    local log="$out_dir/client.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            # A renderer that could not create its Vulkan device never connected; retrying is safe.
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client.vulkan-init-failed-$attempt.log"
            echo "  client: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "${render_args[@]}" -MB2StreetSmoke "-MB2StreetShot=$out_dir/street" -MB2StreetTimeout=300 \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

# expect_log <log> <label> <want: number or ">=N"> <pattern>
expect_log() {
    local log="$1" label="$2" want="$3" pattern="$4" count
    count=$(grep -ac -- "$pattern" "$log" || true)
    echo "  $(basename "$log" .log): $label=$count (want $want)"
    if [[ "$want" == ">="* ]]; then
        [[ "$count" -ge "${want#>=}" ]] || status=1
    else
        [[ "$count" -eq "$want" ]] || status=1
    fi
}

echo "Dedicated server ($mode) on $map, port $port; evidence in $out_dir"
start_server "$out_dir/server.log"
code=0; run_client || code=$?
if [[ "$code" -eq 0 ]] && grep -aq "MB2SSMOKE role=client event=pass" "$out_dir/client.log"; then
    echo "  client: PASS"
else
    echo "  client: FAIL (exit $code)"
    grep -ah "MB2SSMOKE role=client event=\(fail\|expectation_failed\)" "$out_dir/client.log" | sed 's/^.*MB2SSMOKE/    MB2SSMOKE/' || true
    status=1
fi
sleep 1
stop_server
log="$out_dir/server.log"
expect_log "$log" "street_ready" 1 "MB2_STREET role=server event=street_ready "
expect_log "$log" "spawned" ">=10" "MB2_STREET role=server event=spawn .* why=\(in_view\|out_of_view\) "
expect_log "$log" "spawned_too_close" 0 "MB2_STREET role=server event=spawn .* why=\(in_view\|out_of_view\) player_m=\([0-9]\|1[0-9]\|2[01]\)\."
expect_log "$log" "fight_incident" ">=1" "MB2_STREET role=server event=incident .* kind=fight player=1 "
expect_log "$log" "fight_reactions" ">=1" "MB2_STREET role=server event=react .* event=fight response=\(flee\|watch\) "
expect_log "$log" "probe" 1 "MB2_STREET role=server event=probe vehicle=lent_hatchback "
expect_log "$log" "run_over" ">=1" "MB2_STREET role=server event=run_over npc=street_.* vehicle=lent_hatchback .* player_driving=1 "
expect_log "$log" "dodged" ">=1" "MB2_STREET role=server event=dodge npc=street_"
expect_log "$log" "answered_run_over" ">=1" "MB2_STREET role=server event=react .* event=run_over "
expect_log "$log" "answered_body" ">=1" "MB2_STREET role=server event=react .* event=body "
# The person the car put out lies there until nobody is close and nobody sees it: never culled like a walker.
victim=$(grep -ao "MB2_STREET role=server event=run_over npc=street_[0-9]* .* out=1 " "$log" | head -1 | sed 's/.* npc=\(street_[0-9]*\) .*/\1/' || true)
if [[ -n "$victim" ]]; then
    expect_log "$log" "body_culled_as_walker" 0 "MB2_STREET role=server event=despawn npc=$victim reason=\(unseen\|far\) "
fi
expect_log "$log" "gathered_round_body" ">=0" "MB2_STREET role=server event=gathered "
expect_log "$log" "graph_off" 0 "MB2_STREET role=server event=street_off"
# Nobody walks into the Drava (ADR-046): the graph is checked against the water, and nobody is ever in it.
expect_log "$log" "graph_checked_for_water" 1 "MB2_STREET role=server event=street_ready .* water_volumes=[1-9][0-9]* "
expect_log "$log" "people_in_water" 0 "MB2_STREET role=server event=street_summary .* swimming=[1-9]"
# The water probe: scared from the land side by the river, the person runs along the bank and never swims.
expect_log "$log" "water_probe" 1 "MB2_STREET role=server event=water_probe npc=street_.* straight_away_wet=1 "
expect_log "$log" "water_probe_dry" 1 "MB2_STREET role=server event=water_probe_done npc=street_.* swam=0 "

{
    echo "MarburgumII street smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo
    grep -ah "MB2SSMOKE\|MB2_STREET role=server event=\(street_ready\|street_summary\|incident\|react\|run_over\|dodge\|probe\|gathered\|despawn\|water_edge\|water_probe\|water_probe_done\|water_probe_skipped\)\|MB2SMOKE role=server event=\(ready\|login\|logout\)" \
        "$out_dir"/*.log | sed 's/^.*\(MB2SSMOKE\|MB2_STREET\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
