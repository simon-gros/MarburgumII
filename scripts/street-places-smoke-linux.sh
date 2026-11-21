#!/usr/bin/env bash
# The places that draw street people in (ADR-069) on the launch map, /Game/World/lent. For each phase a dedicated server
# runs with its world clock pinned and -MB2StreetRoute -MB2StreetRouteName=places, which sets the one client's player
# down two minutes at each of five stops by a cluster of places (MarburgumStreetRoute.h PlaceStops): the old town's
# cafés and restaurants by Glavni trg, the sights by the Drava, the shops in Magdalena, three bus stops among the houses
# and a shopping street with one of nearly every kind. The client, rendered offscreen, counts the people within 70 m
# every second, and how many of them are at or on their way to a place (-MB2StreetCount), and captures each stop 90 s
# after it is moved there. The server logs every visit (event=visit kind= action=), every arrival at a stop or a sight,
# everyone who went in at a door or came out of one, everyone found already waiting at a stop, and how open each kind is.
#
#   day      Tuesday 18 June 1996, 10:00: everything open but the bars' day share; people go to stops, cafés,
#            restaurants, shops and sights, wait and look, go in and come out
#   late     Tuesday 22:30: the shops are shut and nobody goes to one; the bars are at their busiest
#   sunday   Sunday 23 June 1996, 11:00: the shops are shut (1990s shop hours) and nobody goes to one; the cafés are open
#
# Evidence is written to Saved/StreetPlacesSmoke/<UTC timestamp>/<phase>-*.
# Usage: scripts/street-places-smoke-linux.sh [staged|editor]   (port: MB2_STREET_PORT, default 27764;
# phases: MB2_STREET_PHASES, default all of the above)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_STREET_PORT:-27764}"
phases="${MB2_STREET_PHASES:-day late sunday}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/StreetPlacesSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
stops=(venues sights shops bus_stops mixed)
kinds=(bus_stop cafe restaurant bar fast_food shop sight)
# Five stops two minutes apart after the server's 6 s lead, and room for the client to connect and settle.
count_seconds=640

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
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2StreetSeed=1990 -MB2StreetRoute \
        -MB2StreetRouteName=places "$@" "${common_args[@]}" > "$log" 2>&1 &
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
    local url="127.0.0.1:$port?MB2Start=lent_hatchback?MB2Character=mb2p-$phase-t$(date +%s)$RANDOM"
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
            -MB2StreetCount="$count_seconds" -MB2StreetShotAfterMove=90 -MB2StreetTimeout=$((count_seconds + 120)) > "$log" 2>&1 || code=$?
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

# What went on at the places, per stop and kind, one line each:
#   <stop> <kind> <visits> <arrived> <went_in> <came_out> <found_waiting> <given_up>
# Each event is the stop's from the moment the player is set down there.
server_places() {
    awk -v kinds="${kinds[*]}" '
        BEGIN { nk = split(kinds, K, " ") }
        /MB2_STREET role=server event=route stop=/ { match($0, /stop=[a-z_]+/); stop = substr($0, RSTART + 5, RLENGTH - 5); order[++n] = stop; next }
        stop == "" { next }
        / event=visit / { match($0, / kind=[a-z_]+/); visits[stop, substr($0, RSTART + 6, RLENGTH - 6)]++ }
        / event=arrived / { match($0, / kind=[a-z_]+/); arrived[stop, substr($0, RSTART + 6, RLENGTH - 6)]++ }
        / event=went_in / { match($0, / kind=[a-z_]+/); went[stop, substr($0, RSTART + 6, RLENGTH - 6)]++ }
        / event=came_out / { match($0, / kind=[a-z_]+/); out[stop, substr($0, RSTART + 6, RLENGTH - 6)]++ }
        / event=visit_given_up / { match($0, / kind=[a-z_]+/); given[stop, substr($0, RSTART + 6, RLENGTH - 6)]++ }
        # Spawned already waiting: the visit line that follows names the kind.
        / event=spawn .* why=waiting / { waiting_next = 1; next }
        waiting_next && / event=visit / { match($0, / kind=[a-z_]+/); found[stop, substr($0, RSTART + 6, RLENGTH - 6)]++; waiting_next = 0 }
        END {
            for (i = 1; i <= n; i++)
                for (k = 1; k <= nk; k++) {
                    s = order[i]; c = K[k]
                    printf "%s %s %d %d %d %d %d %d\n", s, c, visits[s, c], arrived[s, c], went[s, c], out[s, c], found[s, c], given[s, c]
                }
        }' "$1"
}

# The kinds' openness the street reported while the player was about ("kind open" per line), and whether it held.
server_open() {
    grep -ao "MB2_STREET role=server event=street_hour .* players=[1-9].* open=[a-z_=0-9.,]*" "$1" | sed 's/.* open=//' | sort -u > "$2.all"
    head -1 "$2.all" | tr ',' '\n' | tr '=' ' ' > "$2"
}

# The client's most people at a place, and its mean count over each stop's second half ("move mean most_visiting").
client_stops() {
    awk '
        /MB2SSMOKE role=client event=moved / { match($0, /move=[0-9]+/); move = substr($0, RSTART + 5, RLENGTH - 5) + 0; first = -1; next }
        move > 0 && /MB2SSMOKE role=client event=count / {
            match($0, /t_s=[0-9]+/); t = substr($0, RSTART + 4, RLENGTH - 4) + 0
            match($0, / people=[0-9]+/); p = substr($0, RSTART + 8, RLENGTH - 8) + 0
            match($0, / visiting=[0-9]+/); v = substr($0, RSTART + 10, RLENGTH - 10) + 0
            if (first < 0) first = t
            if (t - first >= 60 && t - first < 118) { total[move] += p; samples[move]++ }
            if (v > most[move]) most[move] = v
        }
        END { for (m = 1; m <= 5; m++) printf "%d %.1f %d\n", m, (samples[m] ? total[m] / samples[m] : -1), most[m] }' "$1"
}

# The target the street kept at each stop ("stop target"), as the district smoke reads it.
server_targets() {
    awk '
        /MB2_STREET role=server event=route stop=/ { match($0, /stop=[a-z_]+/); stop = substr($0, RSTART + 5, RLENGTH - 5); order[++n] = stop; if (t != "") target[stop] = t; next }
        /MB2_STREET role=server event=street_hour .* players=[1-9]/ { match($0, / target=[0-9]+/); t = substr($0, RSTART + 8, RLENGTH - 8); if (stop != "") target[stop] = t }
        END { for (i = 1; i <= n; i++) printf "%s %s\n", order[i], (order[i] in target ? target[order[i]] : "-") }' "$1"
}

total_of() { # total_of <places file> <column 3..8> [kind]
    awk -v col="$2" -v kind="${3:-}" 'kind == "" || $2 == kind { n += $col } END { print n + 0 }' "$1"
}

check_phase() {
    local phase="$1"
    local server="$out_dir/$phase-server.log" client="$out_dir/$phase-client.log"
    local places="$out_dir/$phase-places.txt" open="$out_dir/$phase-open.txt"
    server_places "$server" > "$places"
    server_open "$server" "$open"
    client_stops "$client" > "$out_dir/$phase-seen.txt"
    server_targets "$server" > "$out_dir/$phase-targets.txt"
    expect "$phase stops" "$([[ $(wc -l < "$out_dir/$phase-targets.txt") -eq ${#stops[@]} ]] && echo 1 || echo 0)" "$(wc -l < "$out_dir/$phase-targets.txt") of ${#stops[@]} visited"
    expect "$phase hours held" "$([[ $(wc -l < "$open.all") -eq 1 ]] && echo 1 || echo 0)" "$(tr '\n' ' ' < "$open.all")"

    local visits arrived went came found given
    visits=$(total_of "$places" 3); arrived=$(total_of "$places" 4); went=$(total_of "$places" 5)
    came=$(total_of "$places" 6); found=$(total_of "$places" 7); given=$(total_of "$places" 8)
    echo "  $phase: $visits visits, $arrived arrived at a stop or sight, $went went in, $came came out, $found found waiting, $given gave up"
    # A spot nobody can reach would show as visits given up.
    expect "$phase spots reached" "$(awk -v g="$given" -v v="$visits" 'BEGIN { print (g <= 0.2 * v + 1) ? 1 : 0 }')" "$given of $visits visits given up"
    # Nobody goes to, comes out of or waits at a place that is shut.
    local kind share
    while read -r kind share; do
        if awk -v s="$share" 'BEGIN { exit !(s == 0) }'; then
            local any=$(( $(total_of "$places" 3 "$kind") + $(total_of "$places" 6 "$kind") ))
            expect "$phase $kind shut" "$([[ "$any" -eq 0 ]] && echo 1 || echo 0)" "$any visits or people out of one"
        fi
    done < "$open"

    # Every stop keeps the street's number: people going in are replaced.
    local i=0 stop target move mean most low high
    while read -r stop target; do
        read -r move mean most < <(awk -v m=$((i + 1)) '$1 == m' "$out_dir/$phase-seen.txt")
        mean="${mean:--1}"
        low=$(awk -v t="$target" 'BEGIN { l = int(t * 0.5) - 1; print (l < 0 ? 0 : l) }')
        high=$((target + 3))
        expect "$phase $stop seen" "$(awk -v m="$mean" -v l="$low" -v h="$high" 'BEGIN { print (m >= l && m <= h) ? 1 : 0 }')" \
            "mean $mean within 70 m, target $target (want $low..$high); most at a place at once ${most:-0}"
        i=$((i + 1))
    done < "$out_dir/$phase-targets.txt"

    case "$phase" in
        day)
            for kind in bus_stop shop sight; do
                expect "day goes to a $kind" "$([[ $(total_of "$places" 3 "$kind") -ge 1 ]] && echo 1 || echo 0)" "$(total_of "$places" 3 "$kind") visits"
            done
            expect "day goes to a café or restaurant" "$([[ $(( $(total_of "$places" 3 cafe) + $(total_of "$places" 3 restaurant) )) -ge 1 ]] && echo 1 || echo 0)" \
                "$(( $(total_of "$places" 3 cafe) + $(total_of "$places" 3 restaurant) )) visits"
            expect "day waits at a stop" "$([[ $(total_of "$places" 4 bus_stop) -ge 1 ]] && echo 1 || echo 0)" "$(total_of "$places" 4 bus_stop) arrived"
            expect "day looks at a sight" "$([[ $(total_of "$places" 4 sight) -ge 1 ]] && echo 1 || echo 0)" "$(total_of "$places" 4 sight) arrived"
            expect "day goes in" "$([[ "$went" -ge 3 ]] && echo 1 || echo 0)" "$went went in"
            expect "day comes out" "$([[ "$came" -ge 3 ]] && echo 1 || echo 0)" "$came came out"
            expect "day found waiting" "$([[ "$found" -ge 1 ]] && echo 1 || echo 0)" "$found already waiting at a stop"
            expect "day seen at a place" "$(awk '$3 > 0 { n++ } END { print (n > 0) ? 1 : 0 }' "$out_dir/$phase-seen.txt")" "client saw somebody at a place at $(awk '$3 > 0 { n++ } END { print n + 0 }' "$out_dir/$phase-seen.txt") stops"
            ;;
        late)
            expect "late goes to a bar or fast food" "$([[ $(( $(total_of "$places" 3 bar) + $(total_of "$places" 3 fast_food) )) -ge 1 ]] && echo 1 || echo 0)" \
                "$(( $(total_of "$places" 3 bar) + $(total_of "$places" 3 fast_food) )) visits"
            ;;
        sunday)
            expect "sunday goes to a café" "$([[ $(total_of "$places" 3 cafe) -ge 1 ]] && echo 1 || echo 0)" "$(total_of "$places" 3 cafe) visits"
            ;;
    esac
    local swimming
    swimming=$(grep -ao "event=street_summary .* swimming=[0-9]*" "$server" | sed 's/.*swimming=//' | sort -n | tail -1)
    expect "$phase nobody in the river" "$([[ "${swimming:-0}" -eq 0 ]] && echo 1 || echo 0)" "most swimming at once ${swimming:-0}"
}

echo "Street places ($mode) on $map, port $port; evidence in $out_dir"
dry=(-MB2Weather=clear_dry)
for phase in $phases; do
    case "$phase" in
        day) run_phase day "${dry[@]}" -MB2Clock=2026-06-18T08:00:00Z ;;
        late) run_phase late "${dry[@]}" -MB2Clock=2026-06-18T20:30:00Z ;;
        sunday) run_phase sunday "${dry[@]}" -MB2Clock=2026-06-23T09:00:00Z ;;
        *) echo "Unknown phase $phase" >&2; exit 2 ;;
    esac
done

echo "Checks"
for phase in $phases; do
    check_phase "$phase"
done

{
    echo "MarburgumII street places smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "phases: $phases"
    echo "host: $(uname -srm)"
    for phase in $phases; do
        echo
        echo "== $phase (stop kind visits arrived went_in came_out found_waiting given_up), rows with anything"
        awk '$3 + $4 + $5 + $6 + $7 + $8 > 0' "$out_dir/$phase-places.txt" 2>/dev/null || true
        echo "-- open"
        tr '\n' ' ' < "$out_dir/$phase-open.txt" 2>/dev/null; echo
        echo "-- targets (stop target)"
        cat "$out_dir/$phase-targets.txt" 2>/dev/null || true
        echo "-- seen within 70 m over each stop's second half (move mean most_at_a_place)"
        cat "$out_dir/$phase-seen.txt" 2>/dev/null || true
        grep -ah "MB2SSMOKE role=client event=\(counted\|pass\|fail\)" "$out_dir/$phase-client.log" 2>/dev/null | sed 's/^.*MB2SSMOKE/MB2SSMOKE/'
    done
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
