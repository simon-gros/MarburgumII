#!/usr/bin/env bash
# The street's districts (ADR-068) on the launch map, /Game/World/lent. For each phase a dedicated server runs with its
# world clock pinned and -MB2StreetRoute, which sets the one client's player down at six stops a minute apart, each in
# one of the original's zones (MarburgumStreetRoute.h): an old-town lane, a shopping street, the Magdalena blocks, a
# street of houses, the works and a school. The client, rendered offscreen, counts the people within 70 m every second
# (-MB2StreetCount) and captures each stop 50 s after it is moved there. The server logs the zone it reads at each
# stop, the number it keeps and the zone and group of everyone it sends out.
#
#   day       Tuesday 18 June 1996, 10:00: old town 20, shops 18, blocks 12, houses 8, works 7, school 16
#   evening   Tuesday 19:00: 18, 14, 16, 11, 5, 7
#   night     Wednesday 03:00: 3, 2, 2, 1, 1, 1, and no commuters, shoppers or pensioners anywhere
#   sunday    Sunday 23 June 1996, 12:00: the day's numbers, no shoppers or commuters on the shopping street and
#             no students at the school
#
# Evidence is written to Saved/StreetDistrictSmoke/<UTC timestamp>/<phase>-*.
# Usage: scripts/street-district-smoke-linux.sh [staged|editor]   (port: MB2_STREET_PORT, default 27763;
# phases: MB2_STREET_PHASES, default all of the above)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_STREET_PORT:-27763}"
phases="${MB2_STREET_PHASES:-day evening night sunday}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/StreetDistrictSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
stops=(old_town commercial residential_blocks residential industrial school)
# Six stops a minute apart after the server's 6 s lead, and room for the client to connect and settle.
count_seconds=390

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

# start_server <log> <server args...>
start_server() {
    local log="$1"
    shift
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_STREET_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2StreetSeed=1990 -MB2StreetRoute \
        "$@" "${common_args[@]}" > "$log" 2>&1 &
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

# run_client <phase>
run_client() {
    local phase="$1"
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=mb2d-$phase-t$(date +%s)$RANDOM"
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
            -MB2StreetCount="$count_seconds" -MB2StreetShotAfterMove=50 -MB2StreetTimeout=$((count_seconds + 120)) > "$log" 2>&1 || code=$?
    done
    return "$code"
}

# expect <label> <ok: 0/1> <detail>
expect() {
    echo "  $1: $3 ($([[ "$2" -eq 1 ]] && echo ok || echo FAIL))"
    [[ "$2" -eq 1 ]] || status=1
}

run_phase() {
    local phase="$1"
    shift
    echo "Phase $phase: server $*"
    start_server "$out_dir/$phase-server.log" "$@"
    local code=0
    run_client "$phase" || code=$?
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

# The server's view of each stop, one line each:
#   <stop> <want zone> <zone read> <last target> <last street_hour zone> <spawns> <spawns in the stop's zone> <group=n,...>
# where the groups count only the spawns in the stop's zone.
server_stops() {
    awk '
        /MB2_STREET role=server event=route stop=/ {
            match($0, /stop=[a-z_]+/); stop = substr($0, RSTART + 5, RLENGTH - 5)
            match($0, /want_zone=[a-z_]+/); want[stop] = substr($0, RSTART + 10, RLENGTH - 10)
            match($0, / zone=[a-z_-]+/); read_zone[stop] = substr($0, RSTART + 6, RLENGTH - 6)
            order[++n] = stop
            # The hour in force when the player arrives holds until the street logs another.
            if (current_target != "") { target[stop] = current_target; hour_zone[stop] = current_zone }
            next
        }
        /MB2_STREET role=server event=street_hour .* players=[1-9]/ {
            match($0, / target=[0-9]+/); current_target = substr($0, RSTART + 8, RLENGTH - 8)
            match($0, / zone=[a-z_]+/); current_zone = substr($0, RSTART + 6, RLENGTH - 6)
            if (stop != "") { target[stop] = current_target; hour_zone[stop] = current_zone }
        }
        stop != "" && /MB2_STREET role=server event=spawn .* why=(in_view|out_of_view) / {
            match($0, / zone=[a-z_]+/); z = substr($0, RSTART + 6, RLENGTH - 6)
            match($0, / group=[a-z_]+/); g = substr($0, RSTART + 7, RLENGTH - 7)
            spawns[stop]++
            if (z == want[stop]) { inzone[stop]++; groups[stop, g]++; seen[g] = 1 }
        }
        END {
            for (i = 1; i <= n; i++) {
                s = order[i]; list = ""
                for (g in seen) if ((s, g) in groups) list = list (list == "" ? "" : ",") g "=" groups[s, g]
                printf "%s %s %s %s %s %d %d %s\n", s, want[s], read_zone[s], (s in target ? target[s] : "-"), (s in hour_zone ? hour_zone[s] : "-"), spawns[s], inzone[s], (list == "" ? "-" : list)
            }
        }' "$1"
}

# The client's mean count over the second half of each stop (30 s and more after it saw the move), "move mean" per line.
client_stops() {
    awk '
        /MB2SSMOKE role=client event=moved / { match($0, /move=[0-9]+/); move = substr($0, RSTART + 5, RLENGTH - 5) + 0; first = -1; next }
        move > 0 && /MB2SSMOKE role=client event=count / {
            match($0, /t_s=[0-9]+/); t = substr($0, RSTART + 4, RLENGTH - 4) + 0
            match($0, / people=[0-9]+/); p = substr($0, RSTART + 8, RLENGTH - 8) + 0
            if (first < 0) first = t
            if (t - first >= 30 && t - first < 58) { total[move] += p; samples[move]++ }
        }
        END { for (m = 1; m <= 6; m++) printf "%d %.1f\n", m, (samples[m] ? total[m] / samples[m] : -1) }' "$1"
}

group_count() {
    local list="$1" group="$2"
    echo "$list" | tr ',' '\n' | awk -F= -v g="$group" '$1 == g { n += $2 } END { print n + 0 }'
}

# check_phase <phase> <targets for the six stops, in order>
check_phase() {
    local phase="$1"
    shift
    local targets=("$@")
    local server="$out_dir/$phase-server.log" client="$out_dir/$phase-client.log"
    server_stops "$server" > "$out_dir/$phase-stops.txt"
    client_stops "$client" > "$out_dir/$phase-seen.txt"
    local i=0 stop want read target hour_zone spawns inzone groups mean low high
    expect "$phase stops" "$([[ $(wc -l < "$out_dir/$phase-stops.txt") -eq ${#stops[@]} ]] && echo 1 || echo 0)" "$(wc -l < "$out_dir/$phase-stops.txt") of ${#stops[@]} visited"
    while read -r stop want read target hour_zone spawns inzone groups; do
        expect "$phase $stop zone" "$([[ "$read" == "$want" && "$hour_zone" == "$want" ]] && echo 1 || echo 0)" "read $read, hour $hour_zone (want $want)"
        expect "$phase $stop target" "$([[ "$target" == "${targets[$i]}" ]] && echo 1 || echo 0)" "$target (want ${targets[$i]})"
        # Who appears round the stop is that district's people: at least 70% of them on its own zone.
        expect "$phase $stop district" "$(awk -v a="$inzone" -v b="$spawns" 'BEGIN { print (b == 0 || a >= 0.7 * b) ? 1 : 0 }')" "$inzone of $spawns sent out on $want ($groups)"
        mean=$(awk -v m=$((i + 1)) '$1 == m { print $2 }' "$out_dir/$phase-seen.txt")
        mean="${mean:--1}"
        low=$(awk -v t="${targets[$i]}" 'BEGIN { l = int(t * 0.5) - 1; print (l < 0 ? 0 : l) }')
        high=$((targets[$i] + 3))
        expect "$phase $stop seen" "$(awk -v m="$mean" -v l="$low" -v h="$high" 'BEGIN { print (m >= l && m <= h) ? 1 : 0 }')" "mean $mean within 70 m (want $low..$high)"
        case "$phase:$stop" in
            day:industrial|evening:industrial)
                expect "$phase works" "$([[ $(group_count "$groups" workers) -ge 1 && $(group_count "$groups" workers) -ge $(group_count "$groups" residents) ]] && echo 1 || echo 0)" \
                    "$(group_count "$groups" workers) workers" ;;
            day:school)
                expect "$phase school" "$([[ $(group_count "$groups" students) -ge 1 ]] && echo 1 || echo 0)" "$(group_count "$groups" students) students" ;;
            sunday:commercial)
                expect "sunday shops shut" "$([[ $(( $(group_count "$groups" shoppers) + $(group_count "$groups" commuters) )) -eq 0 ]] && echo 1 || echo 0)" \
                    "$(( $(group_count "$groups" shoppers) + $(group_count "$groups" commuters) )) shoppers and commuters" ;;
            sunday:school)
                expect "sunday no school" "$([[ $(group_count "$groups" students) -eq 0 ]] && echo 1 || echo 0)" "$(group_count "$groups" students) students" ;;
            evening:residential)
                expect "evening out walking" "$([[ $(group_count "$groups" outdoor) -ge 1 || "$inzone" -lt 5 ]] && echo 1 || echo 0)" \
                    "$(group_count "$groups" outdoor) out walking of $inzone" ;;
        esac
        i=$((i + 1))
    done < "$out_dir/$phase-stops.txt"
    if [[ "$phase" == night ]]; then
        local daytime
        daytime=$(grep -ac "MB2_STREET role=server event=spawn .* group=\(commuters\|shoppers\|pensioners\) .* why=\(in_view\|out_of_view\)" "$server" || true)
        expect "night nobody of the day" "$([[ "$daytime" -eq 0 ]] && echo 1 || echo 0)" "$daytime commuters, shoppers or pensioners sent out"
    fi
}

echo "Street districts ($mode) on $map, port $port; evidence in $out_dir"
dry=(-MB2Weather=clear_dry)
for phase in $phases; do
    case "$phase" in
        day) run_phase day "${dry[@]}" -MB2Clock=2026-06-18T08:00:00Z ;;
        evening) run_phase evening "${dry[@]}" -MB2Clock=2026-06-18T17:00:00Z ;;
        night) run_phase night "${dry[@]}" -MB2Clock=2026-06-19T01:00:00Z ;;
        sunday) run_phase sunday "${dry[@]}" -MB2Clock=2026-06-23T10:00:00Z ;;
        *) echo "Unknown phase $phase" >&2; exit 2 ;;
    esac
done

echo "Checks"
for phase in $phases; do
    case "$phase" in
        day|sunday) check_phase "$phase" 20 18 12 8 7 16 ;;
        evening) check_phase evening 18 14 16 11 5 7 ;;
        night) check_phase night 3 2 2 1 1 1 ;;
    esac
done

{
    echo "MarburgumII street districts smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "phases: $phases"
    echo "host: $(uname -srm)"
    for phase in $phases; do
        echo
        echo "== $phase (stop want_zone read_zone target hour_zone spawns spawns_in_zone groups_in_zone)"
        cat "$out_dir/$phase-stops.txt" 2>/dev/null || true
        echo "-- seen within 70 m over each stop's second half (move mean)"
        cat "$out_dir/$phase-seen.txt" 2>/dev/null || true
        grep -ah "MB2SSMOKE role=client event=\(counted\|pass\|fail\)" "$out_dir/$phase-client.log" 2>/dev/null | sed 's/^.*MB2SSMOKE/MB2SSMOKE/'
    done
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
