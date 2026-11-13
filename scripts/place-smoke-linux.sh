#!/usr/bin/env bash
# Place-name qualification (ADR-063) on the launch map, /Game/World/lent. A real dedicated server started with
# -MB2PlaceRoute sets the one client's player down at the ten stops of MarburgumPlaceRoute.h, six seconds each: Stari
# most, Vodni stolp and Židovski stolp on Lent, the synagogue on Židovski trg, Glavni trg and the Rotovž, the cathedral
# on Slomškov trg, the castle on Grajska ulica, Magdalena over the river, and back to the bridge. At each stop the
# client (-MB2PlaceSmoke) checks the location banner's place, street or square and landmark lines against the route,
# that the banner came up for the stop, and takes a capture. The server's place log is checked for every stop.
#
# The client renders offscreen (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/place-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/PlaceSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_PLACE_PORT:-7777}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/PlaceSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
stops=10

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
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
server_pid=""
status=0
server_log="$out_dir/server.log"
client_log="$out_dir/client.log"

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

if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use; stop the other server or set MB2_PLACE_PORT." >&2
    exit 1
fi
echo "Evidence in $out_dir"
echo "Dedicated server ($mode) on $map, port $port, walking the place route"
# Clear dry weather, so the captures read the same on any day.
setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2Weather=clear_dry -MB2Clock=2026-06-18T08:00:00Z -MB2PlaceRoute \
    "${common_args[@]}" > "$server_log" 2>&1 &
server_pid=$!
for _ in $(seq 1 240); do
    grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$server_log" && break
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "Server exited before becoming ready; see $server_log" >&2
        exit 1
    fi
    sleep 0.5
done
grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$server_log" || { echo "Server did not become ready; see $server_log" >&2; exit 1; }

url="127.0.0.1:$port?MB2Character=mb2p-$token"
code=0
for attempt in 0 1 2; do
    if [[ "$attempt" -gt 0 ]]; then
        # A renderer that could not create its Vulkan device never connected; retrying is safe.
        [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$client_log" || break
        mv "$client_log" "$out_dir/client.vulkan-init-failed-$attempt.log"
        echo "  client: Vulkan device creation failed before connecting; retry $attempt in 3 s"
        sleep 3
        code=0
    fi
    "${client_cmd[@]}" "$url" "${common_args[@]}" "${render_args[@]}" -MB2PlaceSmoke -MB2PlaceShot="$out_dir/stop" \
        -MB2PlaceTimeout=180 > "$client_log" 2>&1 || code=$?
done
sleep 1
stop_server

if [[ "$code" -eq 0 ]] && grep -aq "MB2PSMOKE role=client event=pass" "$client_log"; then
    echo "  client: PASS"
else
    echo "  client: FAIL (exit $code)"
    grep -ah "MB2PSMOKE role=client event=\(fail\|expectation_failed\)" "$client_log" | sed 's/^.*MB2PSMOKE/    MB2PSMOKE/' || true
    status=1
fi

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
expect_log "$client_log" "client places_loaded" 1 "MB2PSMOKE role=client event=start .* places_loaded=1 "
expect_log "$client_log" "client stops_matched" "$stops" "MB2PSMOKE role=client event=stop .* match=1"
expect_log "$server_log" "server route_stops_set_down" "$stops" "MB2PLACE role=server event=route .* ground=1 moved=1 "
expect_log "$server_log" "server places_loaded" 1 "LogMarburgumPlaces: places: 27 districts, .* (0 left out)"
expect_log "$server_log" "server place_changes" ">=$stops" "MB2INTERACT role=server event=place "

{
    echo "MarburgumII place smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo
    grep -ah "MB2PSMOKE\|MB2PLACE\|MB2INTERACT role=server event=\(place\|area\)\|LogMarburgumPlaces\|MB2SMOKE role=server event=\(ready\|login\|logout\)" \
        "$server_log" "$client_log" | sed 's/^.*\(MB2PSMOKE\|MB2PLACE\|MB2INTERACT\|LogMarburgumPlaces\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
