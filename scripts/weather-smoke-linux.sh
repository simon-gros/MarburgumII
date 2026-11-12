#!/usr/bin/env bash
# Weather qualification (ADR-061) on the launch map, /Game/World/lent. One client starts beside the authored
# lent_hatchback's driver door (?MB2Start=lent_hatchback) against a real dedicated server, three times:
#
#   dry   the server runs the clear_dry profile. The client sees it replicated (and on the HUD's weather line), gets
#         in, drives off at 0.8 throttle for 3 s, brakes fully and reports the deceleration, gets out and walks for
#         4 s: its own footsteps are hard and none splash.
#   wet   the server runs rain_wet (soaked streets). The same drive and walk: the car decelerates less on the wet road
#         (the original's wet-asphalt traction, 0.82 of dry) and the steps on hard ground splash.
#   live  the server runs the Maribor climate on a review clock from 18 June 2026 10:00 CET (09:00 UTC) at an hour a
#         minute, a dry overcast morning before rain sets in at about 11:00 (MB2_WEATHER_MONTH=2026-06 calendar,
#         seed 1996). The client sees the hourly state turn and the streets soak past wetness 0.5, the HUD's weather
#         line following the replicated weather; then it walks and its steps splash.
#
# Every phase also checks what the client draws (ADR-062): rain curtain, clouds, the level's sun and fog against their
# own, and MPC_Weather's wetness. Six more phases only look and capture (-MB2WeatherLookOnly), all facing the same
# way along the promenade so their captures compare:
#
#   clear     the clear_dry profile: the level's daylight as built, a few clouds.
#   rain      the rain_wet profile: rain falling and leaning in the wind, the street soaked.
#   overcast  the overcast profile: a closed sky, a dimmer sun, the eye opened up, nothing falling.
#   snow      the winter_cold profile: five centimetres of snow lying, so the ground is white.
#   fog       the climate at 17 November 2026 07:00 CET, a fog morning (seed 1996): the haze many times its own.
#   storm     the climate at 12 November 2026 18:30 CET, a thunderstorm: rain, the storm's flashes, and one close
#             strike (FlashNow) captured.
#
# The script compares the dry and wet decelerations and checks the server's weather log in each phase.
# The client renders offscreen (see interaction-smoke-linux.sh for the Vulkan note).
# MB2_WEATHER_PHASES="dry wet" runs only those phases (the braking comparison needs both dry and wet; default all).
# Usage: scripts/weather-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/WeatherSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_WEATHER_PORT:-7777}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/WeatherSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
live_clock="2026-06-18T09:00:00Z"
live_speed=60

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

# start_server <phase> <server args...>
start_server() {
    local phase="$1"
    shift
    local log="$out_dir/server-$phase.log"
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_WEATHER_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store-$phase.json" "${common_args[@]}" "$@" > "$log" 2>&1 &
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

# run_client <phase> <expect> <extra client args...>; output to client-<phase>.log
run_client() {
    local phase="$1" expect="$2"
    shift 2
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=mb2w-$phase-$token"
    local log="$out_dir/client-$phase.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            # A renderer that could not create its Vulkan device never connected; retrying is safe.
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$phase.vulkan-init-failed-$attempt.log"
            echo "  client $phase: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "${render_args[@]}" "$@" -MB2WeatherSmoke -MB2WeatherExpect="$expect" \
            -MB2WeatherShot="$out_dir/$phase" -MB2WeatherTimeout=300 > "$log" 2>&1 || code=$?
    done
    if [[ "$code" -eq 0 ]] && grep -aq "MB2WSMOKE role=client expect=$expect event=pass" "$log"; then
        echo "  client $phase: PASS"
    else
        echo "  client $phase: FAIL (exit $code)"
        grep -ah "MB2WSMOKE role=client expect=$expect event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2WSMOKE/    MB2WSMOKE/' || true
        status=1
    fi
}

# expect_log <log> <label> <want: number or ">=N"> <pattern>
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

# client_value <phase> <event> <field>: the field of the client's first line for that event
client_value() {
    [[ -f "$out_dir/client-$1.log" ]] || return 0
    grep -ao "MB2WSMOKE role=client .* event=$2 .*" "$out_dir/client-$1.log" | head -1 | grep -o " $3=[^ ]*" | head -1 | cut -d= -f2
}

run_phase() {
    local phase="$1" expect="$2"
    shift 2
    echo "Phase $phase: dedicated server ($mode) on $map, port $port, weather $expect"
    start_server "$phase" "$@"
    run_client "$phase" "$expect" ${look_only:+-MB2WeatherLookOnly="$look_only"}
    sleep 1
    stop_server
    local log="$out_dir/server-$phase.log"
    expect_log "$log" "weather_started" 1 "MB2WEATHER role=server event=started source=$expect "
    expect_log "$log" "weather_errors" 0 "MB2WEATHER role=server event=\(no_climate\|unknown_profile\|bad_clock\)"
}

phases="${MB2_WEATHER_PHASES:-dry wet live clear overcast rain snow fog storm}"
look_only=""
echo "Evidence in $out_dir (phases: $phases)"
for phase in $phases; do
    case "$phase" in
        dry) run_phase dry clear_dry -MB2Weather=clear_dry ;;
        wet) run_phase wet rain_wet -MB2Weather=rain_wet ;;
        live)
            run_phase live auto -MB2Weather=auto -MB2WeatherClock="$live_clock" -MB2WeatherSpeed="$live_speed"
            expect_log "$out_dir/server-live.log" "live_weather_turns" ">=1" "MB2WEATHER role=server event=changed .* source=auto state=light_rain type=rain"
            ;;
        clear) look_only=clear run_phase clear clear_dry -MB2Weather=clear_dry ;;
        overcast) look_only=overcast run_phase overcast overcast -MB2Weather=overcast ;;
        rain) look_only=rain run_phase rain rain_wet -MB2Weather=rain_wet ;;
        snow) look_only=snow_lying run_phase snow winter_cold -MB2Weather=winter_cold ;;
        fog) look_only=fog run_phase fog auto -MB2Weather=auto -MB2WeatherClock=2026-11-17T06:00:00Z -MB2WeatherSpeed=1 ;;
        storm) look_only=storm run_phase storm auto -MB2Weather=auto -MB2WeatherClock=2026-11-12T17:30:00Z -MB2WeatherSpeed=1 ;;
        *) echo "Unknown phase $phase" >&2; exit 2 ;;
    esac
done

for phase in $phases; do
    log="$out_dir/client-$phase.log"
    [[ -f "$log" ]] || continue
    looks=$(grep -ac "MB2WSMOKE role=client .* event=look[a-z_]* want=" "$log" || true)
    echo "  client $phase: look checks=$looks (want >= 1)"
    [[ "$looks" -ge 1 ]] || status=1
done

# The scene's brightness in each look capture (mean grey of the promenade and river right of the player, clear of the
# HUD, the player and the car's roof; 0..255), for calibrating the look.
for phase in clear overcast rain snow fog storm; do
    shot="$out_dir/$phase-look.png"
    [[ -f "$shot" ]] || continue
    mean=$(magick "$shot" -resize 1920x1080! -crop 860x260+1060+440 +repage -colorspace gray -format "%[fx:mean*255]" info: 2>/dev/null || echo "?")
    echo "  capture $phase: scene mean grey ${mean}"
    echo "$phase $mean" >> "$out_dir/brightness.txt"
done

dry_decel="$(client_value dry braked decel)"
wet_decel="$(client_value wet braked decel)"
dry_surface="$(client_value dry braked surface)"
wet_surface="$(client_value wet braked surface)"
if [[ " $phases " == *" dry "* && " $phases " == *" wet "* ]]; then
    echo "  braking: dry decel=${dry_decel:-?} m/s2 on ${dry_surface:-?}, wet decel=${wet_decel:-?} m/s2 on ${wet_surface:-?} (want wet <= 0.95 x dry)"
    if [[ -z "$dry_decel" || -z "$wet_decel" ]] || ! awk -v d="$dry_decel" -v w="$wet_decel" 'BEGIN { exit !(d > 0 && w <= 0.95 * d) }'; then
        status=1
    fi
fi

{
    echo "MarburgumII weather smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "live_clock: $live_clock x$live_speed"
    echo "host: $(uname -srm)"
    echo "braking: dry decel=${dry_decel:-?} wet decel=${wet_decel:-?}"
    echo
    for phase in $phases; do
        echo "== $phase"
        grep -ah "MB2WSMOKE\|MB2WEATHER role=server event=\(started\|changed\)\|MB2SMOKE role=server event=\(ready\|login\|logout\)" \
            "$out_dir/server-$phase.log" "$out_dir/client-$phase.log" | sed 's/^.*\(MB2WSMOKE\|MB2WEATHER\|MB2SMOKE\)/\1/'
    done
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
