#!/usr/bin/env bash
# Two or three real clients on /Game/World/lent. Scenarios A-E plus the 48-person cap.
# Editor by default. Port MB2_CROWD_PORT (default 27853). Never 7777 or 17777.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-editor}"
port="${MB2_CROWD_PORT:-27853}"
if [[ "$port" == "7777" || "$port" == "17777" ]]; then
    echo "Refusing port $port" >&2
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
        echo "Usage: $0 [editor|staged]" >&2
        exit 2
        ;;
esac

out="$repo_root/Saved/StreetMulti/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out"
sha="$(git -C "$repo_root" rev-parse HEAD)"
map="/Game/World/lent"
common=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -nullrhi)
server_log="$out/server.log"
pids=()

stop_all() {
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
        fi
    done
}
trap stop_all EXIT

start_server() {
    local log="$1"
    shift
    setsid "${server_prefix[@]}" "$map" -server -port="$port" -MB2CrowdMatrix -MB2StreetPlaces=0 -MB2NetRate \
        -MB2Clock=2026-06-18T08:00:00Z -MB2Weather=clear_dry "$@" "${common[@]}" > "$log" 2>&1 &
    pids+=("$!")
}

start_client() {
    local name="$1" log="$2"
    setsid "${client_prefix[@]}" "127.0.0.1:$port?MB2Character=${name}" -game -MB2CrowdWatch "${common[@]}" > "$log" 2>&1 &
    pids+=("$!")
    echo "$!"
}

wait_log() {
    local file="$1" pattern="$2" seconds="$3"
    for _ in $(seq 1 "$seconds"); do
        if grep -aq "$pattern" "$file"; then
            return 0
        fi
        sleep 1
    done
    return 1
}

start_server "$server_log"
if ! wait_log "$server_log" 'event=street_ready' 180; then
    echo "server did not become ready" >&2
    exit 1
fi
if ! grep -aq 'cross_river=1' "$server_log" || ! grep -aq 'bridge_edges=185/185' "$server_log" || ! grep -aq 'edges_wet=0' "$server_log"; then
    echo "walk graph is not the qualified one" >&2
    exit 1
fi

client_a="$(start_client crowd-a "$out/client-a.log")"
client_b="$(start_client crowd-b "$out/client-b.log")"

if ! wait_log "$server_log" 'name=E phase=please_drop' 180; then
    echo "did not reach disconnect cue" >&2
    exit 1
fi
kill "$client_b" 2>/dev/null || true
if ! wait_log "$server_log" 'name=E_dropped pass=1' 50; then
    echo "disconnect did not drop the viewer" >&2
    exit 1
fi
client_b="$(start_client crowd-b "$out/client-b2.log")"
if ! wait_log "$server_log" 'name=cap phase=please_third' 60; then
    echo "did not reach the third-client cue" >&2
    exit 1
fi
client_c="$(start_client crowd-c "$out/client-c.log")"
if ! wait_log "$server_log" 'event=matrix_done' 80; then
    echo "matrix did not finish" >&2
    exit 1
fi

python3 - "$out" "$sha" << 'PY'
import json, re, sys
out, sha = sys.argv[1], sys.argv[2]
text = open(out + "/server.log", errors="replace").read()
rows = []
ok = True
for name in ("A", "B", "C_apart", "C_back", "D", "E", "cap"):
    found = re.findall(r"event=scenario name=%s pass=(\d) .*" % name, text)
    # take the last full line
    lines = [ln for ln in text.splitlines() if f"name={name} " in ln and "pass=" in ln and "phase=" not in ln]
    line = lines[-1] if lines else ""
    passed = "pass=1" in line
    ok = ok and passed
    rows.append({"name": name, "pass": passed, "line": line})
net = [ln for ln in text.splitlines() if "event=net_rate" in ln]
summary = {"sha": sha, "ok": ok, "scenarios": rows, "net_rate_last": net[-1] if net else ""}
open(out + "/summary.json", "w").write(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
sys.exit(0 if ok else 1)
PY
status=$?
echo "summary_status=$status"
exit "$status"
