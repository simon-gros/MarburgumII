#!/usr/bin/env bash
# Streets by the hour (ADR-067) on the launch map, /Game/World/lent. For each phase a dedicated server runs with its
# world clock pinned (and, for one, running) and one client, rendered offscreen, stands at the hatchback's driver door
# looking along the street and counts the people within 70 m every second (-MB2StreetCount). The server logs the hour
# it reads, how many people it keeps and who it sends out; the client what it sees.
#
#   day       Tuesday 18 June 1996, 10:00, dry: the day, 20 people, ADR-044's mix
#   evening   Tuesday 19:00: the evening, 18, night owls among them
#   late      Tuesday 22:30: the late evening, 9
#   night     Wednesday 03:00: the small hours, 3, mostly night owls
#   friday    Friday 21 June 1996, 21:30: still the evening, an hour later than a weekday's
#   sunday    Sunday 23 June 1996, 12:00: the day, but no commuters and the shops shut, so no shoppers
#   rain      Tuesday 10:00 in the rain_wet profile: under half the day's people
#   home      Tuesday 20:00 with the clock at x20: past 21:00 the evening's people go home, unseen ones only
#
# Evidence is written to Saved/StreetHoursSmoke/<UTC timestamp>/<phase>-*.
# Usage: scripts/street-hours-smoke-linux.sh [staged|editor]   (port: MB2_STREET_PORT, default 27762;
# phases: MB2_STREET_PHASES, default all of the above)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_STREET_PORT:-27762}"
phases="${MB2_STREET_PHASES:-day evening late night friday sunday rain home}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/StreetHoursSmoke/$(date -u +%Y%m%dT%H%M%SZ)"

case "$mode" in
    staged)
        server_cmd=("$repo_root/Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh")
        client_cmd=("$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh")
        ;;
    editor)
        if [[ -z "${UE_ROOT:-}" ]]; then
            echo "Set UE_ROOT to your Unreal Engine 5.8.4 installation root." >&2
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
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under timeout, which stops its whole process group (the staged .sh does not exec the binary).
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-3600}" "${client_cmd[@]}")

mkdir -p "$out_dir"
# -RenderOffScreen on every client, shot or not: a run under systemd-run inherits the desktop's DISPLAY and
# WAYLAND_DISPLAY, and a client without it opens a window on the owner's screen. The server ignores it.
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -RenderOffScreen)
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

# start_server <log> <server args...>
start_server() {
    local log="$1"
    shift
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_STREET_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2StreetSeed=1990 "$@" "${common_args[@]}" > "$log" 2>&1 &
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

# run_client <phase> <count seconds>
run_client() {
    local phase="$1" count="$2"
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=mb2h-$phase-t$(date +%s)$RANDOM"
    local log="$out_dir/$phase-client.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            # A renderer that could not create its Vulkan device never connected; retrying is safe.
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/$phase-client.vulkan-init-failed-$attempt.log"
            echo "  client: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "${render_args[@]}" -MB2StreetSmoke "-MB2StreetShot=$out_dir/$phase" \
            -MB2StreetCount="$count" -MB2StreetTimeout=$((count + 120)) > "$log" 2>&1 || code=$?
    done
    return "$code"
}

# expect <label> <ok: 0/1> <detail>
expect() {
    echo "  $1: $3 ($([[ "$2" -eq 1 ]] && echo ok || echo FAIL))"
    [[ "$2" -eq 1 ]] || status=1
}

count_lines() {
    grep -ac -- "$2" "$1" || true
}

# The street_hour line the server logged most recently for one player, as "period target".
hour_of() {
    grep -a "MB2_STREET role=server event=street_hour .* players=1 " "$1" | tail -1 | sed 's/.* period=\([a-z]*\) .* target=\([0-9]*\) .*/\1 \2/'
}

# run_phase <phase> <count seconds> <server args...>
run_phase() {
    local phase="$1" count="$2"
    shift 2
    echo "Phase $phase: server $*"
    start_server "$out_dir/$phase-server.log" "$@"
    local code=0
    run_client "$phase" "$count" || code=$?
    if [[ "$code" -eq 0 ]] && grep -aq "MB2SSMOKE role=client event=pass" "$out_dir/$phase-client.log"; then
        echo "  client: PASS"
    else
        echo "  client: FAIL (exit $code)"
        grep -ah "MB2SSMOKE role=client event=\(fail\|expectation_failed\)" "$out_dir/$phase-client.log" | sed 's/^.*MB2SSMOKE/    MB2SSMOKE/' || true
        status=1
    fi
    sleep 1
    stop_server
}

# check_phase <phase> <period> <target or a|b> <client mean min> <client mean max>
check_phase() {
    local phase="$1" want_period="$2" want_target="$3" low="$4" high="$5"
    local server="$out_dir/$phase-server.log" client="$out_dir/$phase-client.log"
    local got period target mean
    got=$(hour_of "$server")
    period="${got% *}"
    target="${got#* }"
    expect "$phase period" "$([[ "$period" == "$want_period" ]] && echo 1 || echo 0)" "$period (want $want_period)"
    expect "$phase target" "$([[ "|$want_target|" == *"|$target|"* ]] && echo 1 || echo 0)" "$target (want $want_target)"
    mean=$(grep -ao "MB2SSMOKE role=client event=counted .* mean=[0-9.]*" "$client" | tail -1 | sed 's/.*mean=//')
    mean="${mean:--1}"
    expect "$phase seen" "$(awk -v m="$mean" -v l="$low" -v h="$high" 'BEGIN { print (m >= l && m <= h) ? 1 : 0 }')" "mean $mean within 70 m (want $low..$high)"
    expect "$phase street_off" "$([[ $(count_lines "$server" "MB2_STREET role=server event=street_off") -eq 0 ]] && echo 1 || echo 0)" "street on"
}

# spawned <phase> <group>: how many of a group the server sent out
spawned() {
    count_lines "$out_dir/$1-server.log" "MB2_STREET role=server event=spawn .* group=$2 "
}

echo "Streets by the hour ($mode) on $map, port $port; evidence in $out_dir"
dry=(-MB2Weather=clear_dry)
for phase in $phases; do
    case "$phase" in
        day) run_phase day 50 "${dry[@]}" -MB2Clock=2026-06-18T08:00:00Z ;;
        evening) run_phase evening 50 "${dry[@]}" -MB2Clock=2026-06-18T17:00:00Z ;;
        late) run_phase late 50 "${dry[@]}" -MB2Clock=2026-06-18T20:30:00Z ;;
        night) run_phase night 50 "${dry[@]}" -MB2Clock=2026-06-19T01:00:00Z ;;
        friday) run_phase friday 50 "${dry[@]}" -MB2Clock=2026-06-21T19:30:00Z ;;
        sunday) run_phase sunday 50 "${dry[@]}" -MB2Clock=2026-06-23T10:00:00Z ;;
        rain) run_phase rain 50 -MB2Weather=rain_wet -MB2Clock=2026-06-18T08:00:00Z ;;
        home) run_phase home 210 "${dry[@]}" -MB2Clock=2026-06-18T18:00:00Z -MB2ClockSpeed=20 ;;
        *) echo "Unknown phase $phase" >&2; exit 2 ;;
    esac
done

echo "Checks"
for phase in $phases; do
    case "$phase" in
        day) check_phase day day 20 12 22 ;;
        evening)
            check_phase evening evening 18 10 20
            expect "evening night owls" "$([[ $(spawned evening night_people) -ge 1 ]] && echo 1 || echo 0)" "$(spawned evening night_people) sent out"
            ;;
        late) check_phase late late 9 4 11 ;;
        night)
            check_phase night night 3 1 5
            expect "night owls" "$([[ $(spawned night night_people) -ge 1 ]] && echo 1 || echo 0)" "$(spawned night night_people) sent out"
            expect "night commuters" "$([[ $(spawned night commuters) -eq 0 ]] && echo 1 || echo 0)" "$(spawned night commuters) sent out"
            ;;
        friday) check_phase friday evening 18 10 20 ;;
        sunday)
            check_phase sunday day 20 12 22
            expect "sunday commuters and shoppers" "$([[ $(( $(spawned sunday commuters) + $(spawned sunday shoppers) )) -eq 0 ]] && echo 1 || echo 0)" \
                "$(( $(spawned sunday commuters) + $(spawned sunday shoppers) )) sent out"
            expect "sunday residents" "$([[ $(spawned sunday residents) -ge 5 ]] && echo 1 || echo 0)" "$(spawned sunday residents) sent out"
            ;;
        rain) check_phase rain day "9|10" 5 12 ;;
        home)
            log="$out_dir/home-server.log"
            evening_target=$(count_lines "$log" "MB2_STREET role=server event=street_hour .* period=evening .* players=1 target=18 ")
            late_target=$(count_lines "$log" "MB2_STREET role=server event=street_hour .* period=late .* players=1 target=9 ")
            went_home=$(count_lines "$log" "MB2_STREET role=server event=despawn .* reason=home .* period=late")
            expect "home evening then late" "$([[ "$evening_target" -ge 1 && "$late_target" -ge 1 ]] && echo 1 || echo 0)" \
                "evening target 18 logged $evening_target, late target 9 logged $late_target"
            expect "home went home" "$([[ "$went_home" -ge 1 ]] && echo 1 || echo 0)" "$went_home sent home after 21:00 (the rest of the surplus leaves by the usual cull and is not replaced)"
            # Nobody goes home closer to the player than anybody appears (22 m).
            too_close=$(grep -ao "MB2_STREET role=server event=despawn .* reason=home .* nearest_player_m=[0-9.]*" "$log" | sed 's/.*nearest_player_m=//' | awk '$1 < 22 { n++ } END { print n + 0 }')
            expect "home out of reach" "$([[ "$too_close" -eq 0 ]] && echo 1 || echo 0)" "$too_close sent home within 22 m"
            # Once the late evening has settled, the street holds about its nine.
            settled=$(grep -a "MB2_STREET role=server event=street_summary .* target=9 period=late " "$log" | tail -1 | sed 's/.* people=\([0-9]*\) .*/\1/')
            expect "home settled" "$([[ -n "$settled" && "$settled" -le 11 ]] && echo 1 || echo 0)" "${settled:-none} people at the last late summary (want <= 11)"
            ;;
    esac
done

{
    echo "MarburgumII street hours smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "phases: $phases"
    echo "host: $(uname -srm)"
    for phase in $phases; do
        echo
        echo "== $phase"
        grep -ah "MB2_STREET role=server event=\(street_hour\|street_summary\)\|MB2_STREET role=server event=despawn .* reason=home\|MB2SSMOKE role=client event=\(counted\|pass\|fail\)" \
            "$out_dir/$phase-server.log" "$out_dir/$phase-client.log" 2>/dev/null | sed 's/^.*\(MB2SSMOKE\|MB2_STREET\)/\1/'
        echo "spawned: commuters=$(spawned "$phase" commuters) shoppers=$(spawned "$phase" shoppers) pensioners=$(spawned "$phase" pensioners)" \
            "students=$(spawned "$phase" students) residents=$(spawned "$phase" residents) night_people=$(spawned "$phase" night_people)"
    done
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
