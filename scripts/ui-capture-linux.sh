#!/usr/bin/env bash
# Stage 0C rendered-client UI captures: real MarburgumIIClient processes rendering offscreen on the
# GPU, screenshotting the composited window (UMG included) at each settled session phase.
#
#   frontend  connection shell, idle, at several resolutions
#   gallery   UI-01 component gallery (labeled SAMPLE DATA)
#   fail      connect to a port with no server: truthful error state
#   recover   join a packaged dedicated server, stop the server while admitted, restart it, and
#             expect the client to show reconnecting and recover to admission on its own
#
# Usage: scripts/ui-capture-linux.sh [staged|editor]
#   staged  packaged builds from scripts/package-linux.sh (default)
#   editor  uncooked UnrealEditor -server / -game processes (requires UE_ROOT)
# Evidence (PNGs, logs, summary.txt) is written to Saved/UICapture/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_CAPTURE_PORT:-7777}"
dead_port="${MB2_CAPTURE_DEAD_PORT:-7790}"
map="/Game/Maps/NetSmoke"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/UICapture/$(date -u +%Y%m%dT%H%M%SZ)"

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
server_pid=""

# Same process-group handling as net-smoke-linux.sh: the staged launcher does not exec the binary.
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

port_busy() { ss -Hlun "sport = :$1" | grep -aq .; }

start_server() {
    local log="$1"
    if port_busy "$port"; then
        echo "UDP port $port is already in use; stop the other server or set MB2_CAPTURE_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" "${common_args[@]}" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 240); do
        if grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log"; then
            return 0
        fi
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            exit 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port within 120 s; see $log" >&2
    exit 1
}

# run_client <name> <width> <height> <script> [extra args...]; returns the client's exit status.
run_client() {
    local name="$1" width="$2" height="$3" script="$4"
    shift 4
    # Front-end is no longer the game default (ADR-025). These captures still open it by name.
    "${client_cmd[@]}" /Game/Maps/FrontEnd "${common_args[@]}" -RenderOffScreen -windowed -ForceRes -ResX="$width" -ResY="$height" \
        -MB2UICapture="$out_dir" -MB2UICaptureScript="$script" "$@" \
        > "$out_dir/client-$name.log" 2>&1
}

wait_for_line() {
    local file="$1" pattern="$2" seconds="$3"
    for _ in $(seq 1 $((seconds * 4))); do
        grep -aq -- "$pattern" "$file" 2>/dev/null && return 0
        sleep 0.25
    done
    return 1
}

status=0
record() {
    local name="$1" code="$2" expected="${3:-0}"
    if [[ "$code" -eq "$expected" ]]; then
        echo "  $name: PASS"
    else
        echo "  $name: FAIL (exit $code, expected $expected)"
        status=1
    fi
}

echo "UI captures ($mode); evidence in $out_dir"
if port_busy "$dead_port"; then
    echo "UDP port $dead_port must be unused for the failure capture; set MB2_CAPTURE_DEAD_PORT." >&2
    exit 1
fi

echo "Front-end shell"
for resolution in 1920x1080 1280x720 2560x1440 3840x2160; do
    code=0
    run_client "frontend-$resolution" "${resolution%x*}" "${resolution#*x}" frontend || code=$?
    record "frontend $resolution" "$code"
done

echo "Component gallery (sample data)"
for resolution in 1920x1080 2560x1440; do
    code=0
    run_client "gallery-$resolution" "${resolution%x*}" "${resolution#*x}" frontend -MB2UIGallery -MB2UICaptureLabel=gallery || code=$?
    record "gallery $resolution" "$code"
done

echo "Connection failure (no server on port $dead_port)"
code=0
run_client fail 1920 1080 fail -MB2Server="127.0.0.1:$dead_port" || code=$?
record "fail" "$code"

echo "Join, server loss and automatic recovery (port $port)"
start_server "$out_dir/server-1.log"
run_client recover 1920 1080 recover -MB2Server="127.0.0.1:$port" -MB2UIDiagnostics &
client_pid=$!
recover_log="$out_dir/client-recover.log"
if wait_for_line "$recover_log" "stage=await_loss" 90; then
    echo "  client admitted; stopping the server"
    stop_server
    if wait_for_line "$recover_log" "stage=saved .* phase=reconnecting" 30; then
        echo "  client shows reconnecting; restarting the server"
        start_server "$out_dir/server-2.log"
    else
        echo "  client never showed reconnecting"
    fi
else
    echo "  client was not admitted"
fi
code=0
wait "$client_pid" || code=$?
record "recover" "$code"
stop_server

{
    echo "MarburgumII Stage 0C rendered UI captures"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "host: $(uname -srm)"
    echo "gpu: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null || echo unknown)"
    echo "client_cmd: ${client_cmd[*]} ${common_args[*]} -RenderOffScreen -windowed -ForceRes -ResX=<w> -ResY=<h> -MB2UICapture=<dir> -MB2UICaptureScript=<script>"
    echo
    echo "screenshots:"
    (cd "$out_dir" && ls -1 ./*.png 2>/dev/null | sed 's|^\./|  |')
    echo
    grep -ah "MB2UI\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log | sed 's/^.*\(MB2UI\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
