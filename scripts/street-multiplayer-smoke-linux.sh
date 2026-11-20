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
    sleep 0.3
    : > "$out/cleanup.txt"
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "still_alive $pid" >> "$out/cleanup.txt"
            status=3
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
common=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -nullrhi)
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
if ! grep -aq 'cross_river=1' "$server_log" || ! grep -aq 'bridge_edges=185/185' "$server_log" || ! grep -aq 'edges_wet=0' "$server_log"; then
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

python3 - "$out" "$sha" "$case_name" "$expect_clock" << 'PY'
import json, re, sys
out, sha, case_name, expect_clock = sys.argv[1:5]
text = open(out + "/server.log", errors="replace").read()
lines = text.splitlines()

def scenario_lines(name):
    found = []
    for ln in lines:
        if f"name={name} " in ln and "event=scenario" in ln and "phase=" not in ln:
            found.append(ln)
    return found

def field(ln, key):
    m = re.search(rf"(?:^|\s){key}=(\S+)", ln)
    return m.group(1) if m else ""

rows = []
ok = True
if case_name == "night":
    wanted = ("night",)
else:
    wanted = ("solo", "A", "B", "C_apart", "C_back", "D", "E_dropped", "E", "cap", "cap_drop")
for name in wanted:
    found = scenario_lines(name)
    ln = found[-1] if found else ""
    state = field(ln, "state")
    passed = field(ln, "pass") == "1"
    if name in ("D", "cap"):
        accepted = state in ("settled", "constrained") and "over_cap" not in text
    elif name in ("E_dropped", "cap_drop"):
        accepted = state == "lifecycle" and passed
    else:
        accepted = state == "settled" and passed
    ok = ok and accepted and bool(ln)
    rows.append({"name": name, "state": state or "missing", "pass": passed, "accepted": accepted, "line": ln})

clients = []
for name in ("client-a.log", "client-b.log", "client-b2.log", "client-c.log"):
    path = out + "/" + name
    try:
        body = open(path, errors="replace").read().splitlines()
    except FileNotFoundError:
        continue
    watches = [ln for ln in body if "event=crowd_watch" in ln]
    last = watches[-1] if watches else ""
    clients.append({"log": name, "watches": len(watches), "last": last})

net = [ln for ln in lines if "event=net_rate" in ln]
clock_ok = any(expect_clock in ln and "offset=+2" in ln for ln in lines)
ok = ok and clock_ok
summary = {
    "sha": sha,
    "case": case_name,
    "clock_ok": clock_ok,
    "ok": ok,
    "scenarios": rows,
    "clients": clients,
    "net_rate_last": net[-1] if net else "",
    "net_rate_samples": len(net),
}
open(out + "/summary.json", "w").write(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
sys.exit(0 if ok else 1)
PY
status=$?
echo "summary_status=$status"
exit "$status"
