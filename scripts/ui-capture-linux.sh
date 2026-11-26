#!/usr/bin/env bash
# Stage 0C rendered-client UI captures: real MarburgumIIClient processes rendering offscreen on the
# GPU, screenshotting the composited window (UMG included) at each settled session phase.
#
#   frontend  connection shell, idle, at several resolutions
#   gallery   UI-01 component gallery (labeled SAMPLE DATA)
#   fail      connect to a port with no server: truthful error state
#   recover   join a packaged dedicated server, stop the server while admitted, restart it, and
#             expect the client to show reconnecting and recover to admission on its own
#   tour      join a dedicated server on /Game/World/lent (own throwaway world store) at the riverside angler
#             and walk the live session's surfaces: location banner, quiet HUD, E on the angler (receipt,
#             pinned objective), Character, Skills, Inventory, Journal, Settings, general chat, Escape menu
#   matrix    the same live session at every resolution and UI scale of the UI-07 matrix (720p to 4K,
#             ultrawide, 5:4; 80 % to 200 %), screenshotting the HUD and each management screen
#
# Usage: scripts/ui-capture-linux.sh [staged|editor] [scenario,scenario,...]
#   staged  packaged builds from scripts/package-linux.sh (default)
#   editor  uncooked UnrealEditor -server / -game processes (requires UE_ROOT)
#   scenarios default to frontend,gallery,fail,recover; "all" adds tour and matrix.
#   MB2_CAPTURE_MATRIX="1920x1080@1.0 1280x720@1.5 ..." narrows the matrix.
#   MB2_CAPTURE_MATRIX_TOUR="explore,interact,equip:right_hand:fishing_rod,character:right_hand,..." replaces the matrix's
#   tour steps (character:<slot>, inventory:<item> and equip:<slot>:<item> included, ADR-085; settings:graphics opens
#   Settings on the DLSS 5 neural rendering row).
#   MB2_CAPTURE_CULTURE=sl renders every client in Slovene (-culture=sl; QA U11), with "-sl" in the evidence folder.
#   The live server's clock and weather are held (MB2_CAPTURE_CLOCK, default a June afternoon, clear_dry), so before
#   and after captures compare the interface and not the hour they were taken.
# Evidence (PNGs, logs, summary.txt) is written to Saved/UICapture/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
scenarios=",${2:-frontend,gallery,fail,recover},"
[[ "$scenarios" == ",all," ]] && scenarios=",frontend,gallery,fail,recover,tour,matrix,"
wants() { [[ "$scenarios" == *",$1,"* ]]; }
port="${MB2_CAPTURE_PORT:-27703}"
dead_port="${MB2_CAPTURE_DEAD_PORT:-27704}"
map="/Game/Maps/NetSmoke"
world_map="/Game/World/lent"
capture_clock="${MB2_CAPTURE_CLOCK:-2026-06-18T12:30:00Z}"
# UI-07 matrix: resolution@scale. 1280x1024 is the 5:4 case.
matrix="${MB2_CAPTURE_MATRIX:-1280x720@0.8 1280x720@1.0 1280x720@1.25 1280x720@1.5 1366x768@1.0 1920x1080@1.0 1920x1080@1.25 1920x1080@1.5 1920x1080@2.0 2560x1440@1.0 2560x1440@1.5 3440x1440@1.0 3840x2160@1.0 3840x2160@2.0 1280x1024@1.0 1280x1024@1.5}"
uproject="$repo_root/MarburgumII.uproject"
capture_culture="${MB2_CAPTURE_CULTURE:-}"
culture_args=()
[[ -n "$capture_culture" ]] && culture_args=("-culture=$capture_culture")
out_dir="$repo_root/Saved/UICapture/$(date -u +%Y%m%dT%H%M%SZ)${capture_culture:+-$capture_culture}"

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
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under timeout, which stops its whole process group (the staged .sh does not exec the binary).
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-3600}" "${client_cmd[@]}")

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
    local log="$1" server_map="${2:-$map}"
    shift 2 || true
    if port_busy "$port"; then
        echo "UDP port $port is already in use; stop the other server or set MB2_CAPTURE_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$server_map" -port="$port" "${common_args[@]}" "$@" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 360); do
        if grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log"; then
            return 0
        fi
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            exit 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port within 180 s; see $log" >&2
    exit 1
}

# run_world_client <name> <width> <height> <scale> <tour steps> [extra args...]: a client joining the world at the
# riverside angler, walking the tour. A second offscreen Vulkan client can fail to create its device while another
# renders on the same GPU; a client that fails that way before connecting is retried.
run_world_client() {
    local name="$1" width="$2" height="$3" scale="$4" steps="$5"
    shift 5
    # A character key allows a-z, 0-9, - and _ (no dots from the scale), at most 40 characters.
    local url="127.0.0.1:$port?MB2Start=angler?MB2Character=mb2ui-$(date +%s)-$RANDOM"
    local log="$out_dir/client-$name.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$name.vulkan-init-failed-$attempt.log"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" -RenderOffScreen -windowed -ForceRes -ResX="$width" -ResY="$height" \
            -MB2UIScale="$scale" -MB2UICapture="$out_dir" -MB2UICaptureScript=tour -MB2UICaptureLabel="$name" -MB2UITour="$steps" \
            "${culture_args[@]}" "$@" \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

# run_client <name> <width> <height> <script> [extra args...]; returns the client's exit status.
run_client() {
    local name="$1" width="$2" height="$3" script="$4"
    shift 4
    # Front-end is no longer the game default (ADR-025). These captures still open it by name.
    "${client_cmd[@]}" /Game/Maps/FrontEnd "${common_args[@]}" -RenderOffScreen -windowed -ForceRes -ResX="$width" -ResY="$height" \
        -MB2UICapture="$out_dir" -MB2UICaptureScript="$script" "${culture_args[@]}" "$@" \
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

if wants frontend; then
    echo "Front-end shell"
    for resolution in 1920x1080 1280x720 2560x1440 3840x2160; do
        code=0
        run_client "frontend-$resolution" "${resolution%x*}" "${resolution#*x}" frontend || code=$?
        record "frontend $resolution" "$code"
    done
fi

if wants gallery; then
    echo "Component gallery (sample data)"
    for resolution in 1920x1080 2560x1440; do
        code=0
        run_client "gallery-$resolution" "${resolution%x*}" "${resolution#*x}" frontend -MB2UIGallery -MB2UICaptureLabel=gallery || code=$?
        record "gallery $resolution" "$code"
    done
fi

if wants fail; then
    echo "Connection failure (no server on port $dead_port)"
    code=0
    run_client fail 1920 1080 fail -MB2Server="127.0.0.1:$dead_port" || code=$?
    record "fail" "$code"
fi

if wants recover; then
    echo "Join, server loss and automatic recovery (port $port)"
    start_server "$out_dir/server-1.log" "$map"
    run_client recover 1920 1080 recover -MB2Server="127.0.0.1:$port" &
    client_pid=$!
    recover_log="$out_dir/client-recover.log"
    if wait_for_line "$recover_log" "stage=await_loss" 90; then
        echo "  client admitted; stopping the server"
        stop_server
        if wait_for_line "$recover_log" "stage=saved .* phase=reconnecting" 30; then
            echo "  client shows reconnecting; restarting the server"
            start_server "$out_dir/server-2.log" "$map"
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
fi

if wants tour || wants matrix; then
    # One server and one throwaway world store for every live capture: each client is a new character.
    start_server "$out_dir/server-world.log" "$world_map" -MB2WorldStore="$out_dir/world-store.json" \
        -MB2Clock="$capture_clock" -MB2ClockSpeed=0.001 -MB2Weather=clear_dry
    if wants tour; then
        echo "Live tour at 1920x1080 (port $port)"
        code=0
        run_world_client tour 1920 1080 1.0 "arrival,explore,interact,character,skills,inventory,journal,settings,chat,menu" || code=$?
        record "tour" "$code"
    fi
    if wants matrix; then
        echo "Resolution and scale matrix"
        for case in $matrix; do
            resolution="${case%@*}"
            scale="${case#*@}"
            code=0
            run_world_client "m-$resolution-$scale" "${resolution%x*}" "${resolution#*x}" "$scale" \
                "${MB2_CAPTURE_MATRIX_TOUR:-explore,interact,character,skills,inventory,journal,settings,menu}" || code=$?
            record "matrix $resolution @ $scale" "$code"
        done
    fi
    stop_server
fi

{
    echo "MarburgumII Stage 0C rendered UI captures"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "scenarios: ${scenarios//,/ }"
    echo "data: front-end, gallery (SAMPLE DATA), fail and recover use no world; tour and matrix are live sessions on $world_map against a dedicated server and a throwaway world store"
    echo "host: $(uname -srm)"
    echo "gpu: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null || echo unknown)"
    echo "client_cmd: ${client_cmd[*]} ${common_args[*]} -RenderOffScreen -windowed -ForceRes -ResX=<w> -ResY=<h> -MB2UICapture=<dir> -MB2UICaptureScript=<script>"
    echo
    echo "screenshots:"
    (cd "$out_dir" && ls -1 ./*.png 2>/dev/null | sed 's|^\./|  |')
    echo
    grep -ah "MB2UI\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log | sed 's/^.*\(MB2UI\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
