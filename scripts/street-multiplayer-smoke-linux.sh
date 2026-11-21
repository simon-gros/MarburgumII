#!/usr/bin/env bash
# Real clients on /Game/World/lent.
#   editor|staged                 daytime matrix, including settlement and the cap
#   editor night|staged night     two clients sharing a residential-block street at night
#   drill                         process-group cleanup, no Unreal
# Port MB2_CROWD_PORT (default 27853). Never 7777 or 17777.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-editor}"
case_name="${2:-day}"
port="${MB2_CROWD_PORT:-27853}"
if [[ "$port" == "7777" || "$port" == "17777" ]]; then
    echo "Refusing port $port" >&2
    exit 2
fi

out="${MB2_CROWD_OUT:-$repo_root/Saved/StreetMulti/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$out"
sha="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || echo unknown)"
pids=()
last_pid=""
stopping=0

# Still running: a killed child stays a zombie (kill -0 succeeds) until this shell reaps it, so that is not alive.
running() {
    local state
    state="$(ps -o stat= -p "$1" 2>/dev/null)" || return 1
    [[ -n "$state" && "$state" != Z* ]]
}

kill_tree() {
    local pid="$1"
    if kill -0 "$pid" 2>/dev/null; then
        kill -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
    fi
}

stop_all() {
    local status=$?
    if [[ "$stopping" -eq 1 ]]; then
        return
    fi
    stopping=1
    local pid
    for pid in "${pids[@]}"; do
        kill_tree "$pid"
    done
    sleep 1
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    local tick any
    for tick in $(seq 1 25); do
        any=0
        for pid in "${pids[@]}"; do
            if running "$pid"; then
                any=1
            fi
        done
        [[ "$any" -eq 0 ]] && break
        sleep 0.2
    done
    : > "$out/cleanup.txt"
    for pid in "${pids[@]}"; do
        if running "$pid"; then
            echo "still_alive $pid" >> "$out/cleanup.txt"
            status=3
        else
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [[ "$mode" != "drill" ]] && ss -Hlun "sport = :$port" | grep -aq .; then
        echo "port_busy $port" >> "$out/cleanup.txt"
        status=3
    fi
    if [[ ! -s "$out/cleanup.txt" ]]; then
        echo "clear pids=${#pids[@]}" > "$out/cleanup.txt"
    fi
    printf '%s\n' "${pids[@]}" > "$out/pids.txt"
    trap - EXIT INT TERM
    exit "$status"
}
trap stop_all EXIT INT TERM

start_tracked() {
    setsid "$@" >/dev/null 2>&1 &
    last_pid="$!"
    pids+=("$last_pid")
}

if [[ "$mode" == "drill" ]]; then
    hold="${DRILL_SLEEP:-30}"
    start_tracked sleep "$hold"
    start_tracked sleep "$hold"
    start_tracked sleep "$hold"
    # DRILL_STUBBORN=1: one that ignores SIGTERM, as a slow Unreal shutdown does, and only goes on SIGKILL.
    if [[ "${DRILL_STUBBORN:-0}" == "1" ]]; then
        start_tracked bash -c "trap '' TERM; sleep $hold & wait"
    fi
    echo "drill_pids=${pids[*]}"
    sleep "${DRILL_HOLD:-1}"
    exit "${DRILL_EXIT:-0}"
fi

if [[ "$case_name" != "day" && "$case_name" != "night" ]]; then
    echo "Usage: $0 [editor|staged|drill] [day|night]" >&2
    exit 2
fi
if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use" >&2
    exit 2
fi

case "$mode" in
    editor)
        if [[ -z "${UE_ROOT:-}" ]]; then
            echo "Set UE_ROOT" >&2
            exit 2
        fi
        "$repo_root/scripts/check-engine-version.sh" "$UE_ROOT"
        bin="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
        server_prefix=("$bin" "$repo_root/MarburgumII.uproject")
        client_prefix=("$bin" "$repo_root/MarburgumII.uproject")
        ;;
    staged)
        bin_s="$repo_root/Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh"
        bin_c="$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh"
        [[ -x "$bin_s" && -x "$bin_c" ]] || { echo "No staged build" >&2; exit 2; }
        server_prefix=("$bin_s")
        client_prefix=("$bin_c")
        ;;
    *)
        echo "Usage: $0 [editor|staged|drill] [day|night]" >&2
        exit 2
        ;;
esac

map="/Game/World/lent"
# -RenderOffScreen as well as -nullrhi: under systemd-run the clients inherit the desktop's DISPLAY and WAYLAND_DISPLAY.
common=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -nullrhi -RenderOffScreen)
server_log="$out/server.log"
if [[ "$case_name" == "night" ]]; then
    clock=(-MB2Clock=1996-06-18T00:00:00Z)
    expect_clock='marburgum="Tuesday 18 June 1996 02:00"'
    night_flag=(-MB2CrowdNight)
else
    clock=(-MB2Clock=1996-06-18T08:00:00Z)
    expect_clock='marburgum="Tuesday 18 June 1996 10:00"'
    night_flag=()
fi

wait_log() {
    local file="$1" pattern="$2" seconds="$3"
    local _
    for _ in $(seq 1 "$seconds"); do
        if [[ -f "$file" ]] && grep -aq "$pattern" "$file"; then
            return 0
        fi
        sleep 1
    done
    return 1
}

launch_server() {
    setsid "${server_prefix[@]}" "$map" -server -port="$port" -MB2CrowdMatrix -MB2StreetPlaces=0 -MB2NetRate \
        "${clock[@]}" -MB2Weather=clear_dry -MB2CrowdSettle=90 "${night_flag[@]}" "${common[@]}" > "$server_log" 2>&1 &
    last_pid="$!"
    pids+=("$last_pid")
}

launch_client() {
    local name="$1" log="$2"
    setsid "${client_prefix[@]}" "127.0.0.1:$port?MB2Character=${name}" -game -MB2CrowdWatch "${common[@]}" > "$log" 2>&1 &
    last_pid="$!"
    pids+=("$last_pid")
}

drop_client() {
    local pid="$1"
    kill_tree "$pid"
}

launch_server
if ! wait_log "$server_log" 'event=street_ready' 180; then
    echo "server did not become ready" >&2
    exit 1
fi
# The graph crosses the Drava, keeps every bridge edge (185 on the nine windows, 263 on the five by five) and is dry.
if ! grep -aq 'cross_river=1' "$server_log" || ! grep -aq 'bridge_edges=\([1-9][0-9]*\)/\1\b' "$server_log" || ! grep -aq 'edges_wet=0' "$server_log"; then
    echo "walk graph is not the qualified one" >&2
    exit 1
fi
if ! wait_log "$server_log" "$expect_clock" 30; then
    echo "world clock was not $expect_clock" >&2
    exit 1
fi
if ! grep -aq 'offset=+2' "$server_log"; then
    echo "world clock was not on summer time" >&2
    exit 1
fi

if [[ "$case_name" == "night" ]]; then
    launch_client crowd-a "$out/client-a.log"
    launch_client crowd-b "$out/client-b.log"
    if ! wait_log "$server_log" 'event=matrix_done' 220; then
        echo "night matrix did not finish" >&2
        exit 1
    fi
else
    launch_client crowd-a "$out/client-a.log"
    if ! wait_log "$server_log" 'phase=please_second' 120; then
        echo "one-player commercial sample did not finish" >&2
        exit 1
    fi
    launch_client crowd-b "$out/client-b.log"
    client_b="$last_pid"
    if ! wait_log "$server_log" 'phase=please_drop ' 420; then
        echo "did not reach disconnect cue" >&2
        exit 1
    fi
    drop_client "$client_b"
    if ! wait_log "$server_log" 'name=E_dropped state=lifecycle pass=1' 50; then
        echo "disconnect did not drop the viewer" >&2
        exit 1
    fi
    launch_client crowd-b "$out/client-b2.log"
    if ! wait_log "$server_log" 'phase=please_third' 120; then
        echo "did not reach the third-client cue" >&2
        exit 1
    fi
    launch_client crowd-c "$out/client-c.log"
    client_c="$last_pid"
    if ! wait_log "$server_log" 'phase=please_drop_cap' 200; then
        echo "did not reach the cap-drop cue" >&2
        exit 1
    fi
    drop_client "$client_c"
    if ! wait_log "$server_log" 'event=matrix_done' 90; then
        echo "matrix did not finish" >&2
        exit 1
    fi
fi

# Scenario verdicts, client watch lines and net-rate samples into $out/summary.json.
status=0
"$repo_root/scripts/mb2.sh" crowd-summary "$out" "$sha" "$case_name" "$expect_clock" || status=$?
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check > "$out/build-stamp.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out/build-stamp.txt" >&2; }
echo "summary_status=$status"
exit "$status"
