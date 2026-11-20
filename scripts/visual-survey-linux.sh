#!/usr/bin/env bash
# Visual survey of /Game/World/lent (Docs/VISUAL_QUALITY_BACKLOG.md): a dedicated server and one offscreen 1920x1080
# client that photographs the fixed camera set of `mb2 survey-views` (aerials of the nine windows, eye-height
# street views, the map edges, the angler, the Stari most deck) and reports assets the client could not load.
#
#   scripts/visual-survey-linux.sh [editor|staged] [name]
#
# MB2_SURVEY_CLOCK (default 2026-06-18T10:00:00Z, Maribor noon), MB2_SURVEY_WEATHER (default clear_dry),
# MB2_SURVEY_PORT (default 27891; never 7777 or 17777, which are for playtests), MB2_SURVEY_VIEWS (an MB2ViewShots string
# name@eye@target@fov;... in Unreal centimetres, instead of the fixed set, for close-ups of one defect).
# MB2_SURVEY_PLAYER_VIEW=1 first adds `player_hud`, the player's own camera with the HUD (character and interface);
# MB2_SURVEY_START picks the player's start (?MB2Start=; a door_<id> start also gives the server -MB2DoorStarts);
# MB2_SURVEY_SETTLE seconds per view (default 2.5; longer gives more frame samples). Evidence goes to
# Saved/VisualSurvey/<name or UTC timestamp>/ (or MB2_SURVEY_OUT): one PNG per view, stats.tsv (per-view frame, thread
# and GPU percentiles, VRAM), render_config.json (the renderer's settings and the level's render inventory),
# server.log, client.log and summary.txt, which also names other GPU users at the start (they skew the numbers).
set -uo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
uproject="$repo_root/MarburgumII.uproject"
mode="${1:-editor}"
name="${2:-$(date -u +%Y%m%dT%H%M%SZ)}"
port="${MB2_SURVEY_PORT:-27891}"
clock="${MB2_SURVEY_CLOCK:-2026-06-18T10:00:00Z}"
weather="${MB2_SURVEY_WEATHER:-clear_dry}"
map="/Game/World/lent"
out_dir="${MB2_SURVEY_OUT:-$repo_root/Saved/VisualSurvey/$name}"
settle="${MB2_SURVEY_SETTLE:-2.5}"
start="${MB2_SURVEY_START:-}"

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
        echo "Usage: $0 [editor|staged] [name]" >&2
        exit 2
        ;;
esac

if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use; stop the other server or set MB2_SURVEY_PORT." >&2
    exit 1
fi
mkdir -p "$out_dir"
views="${MB2_SURVEY_VIEWS:-$("$repo_root/scripts/mb2.sh" survey-views "$repo_root/Content/Marburgum/Street/lent_walk_graph.json")}" || exit 1
# The HUD is removed at the first fixed view, so the player's view goes first.
[[ "${MB2_SURVEY_PLAYER_VIEW:-0}" == 1 ]] && views="player_hud@player@player@0;$views"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
server_extra=()
client_url="127.0.0.1:$port?MB2Character=mb2v-survey"
if [[ -n "$start" ]]; then
    client_url+="?MB2Start=$start"
    [[ "$start" == door_* ]] && server_extra+=(-MB2DoorStarts)
fi
# Anything else on the GPU (another session's smoke, a playtest) skews the frame numbers; name it in the summary.
gpu_others="$(nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader 2>/dev/null | head -n 1)"
ue_others="$(pgrep -f '/Binaries/Linux/(UnrealEditor|MarburgumII(Client|Server))( |$)' | wc -l)"
server_pid=""
stop_server() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill -- "-$server_pid" 2>/dev/null || true
        for _ in $(seq 1 40); do
            pgrep -g "$server_pid" >/dev/null || break
            sleep 0.25
        done
        pkill -KILL -g "$server_pid" 2>/dev/null || true
    fi
    server_pid=""
}
trap stop_server EXIT

echo "Evidence in $out_dir"
epoch_start="$(date +%s)"
setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" -MB2Weather="$weather" -MB2Clock="$clock" \
    -MB2ClockSpeed=0.001 "${server_extra[@]}" "${common_args[@]}" > "$out_dir/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 1 240); do
    grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$out_dir/server.log" && break
    kill -0 "$server_pid" 2>/dev/null || { echo "Server exited before becoming ready; see $out_dir/server.log" >&2; exit 1; }
    sleep 0.5
done
grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$out_dir/server.log" || { echo "Server did not become ready" >&2; exit 1; }

expected="$(tr ';' '\n' <<< "$views" | grep -c .)"
client_timeout="$(awk -v n="$expected" -v s="$settle" 'BEGIN { t = 300 + n * (s + 10); print (t < 900 ? 900 : int(t)) }')"
code=0
for attempt in 0 1 2; do
    if [[ "$attempt" -gt 0 ]]; then
        # A renderer that could not create its Vulkan device never connected; retrying is safe.
        [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$out_dir/client.log" || break
        mv "$out_dir/client.log" "$out_dir/client.vulkan-init-failed-$attempt.log"
        sleep 3
        code=0
    fi
    timeout --kill-after=15 "$client_timeout" "${client_cmd[@]}" "$client_url" "${common_args[@]}" \
        -RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080 -MB2ViewShots="$views" -MB2ViewShotDir="$out_dir" \
        -MB2ViewShotSettle="$settle" \
        > "$out_dir/client.log" 2>&1 || code=$?
done
stop_server
epoch_end="$(date +%s)"

shots="$(find "$out_dir" -maxdepth 1 -name '*.png' | wc -l)"
{
    echo "mode=$mode clock=$clock weather=$weather start=${start:-default} settle_s=$settle client_exit=$code views=$expected shots=$shots epoch_start=$epoch_start epoch_end=$epoch_end"
    echo "gpu_at_start=${gpu_others:-unknown} (utilization, memory used) other_unreal_processes=$ue_others"
    echo "commit=$(git -C "$repo_root" rev-parse --short HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Content Source || echo '+dirty')"
    echo "skipped_packages=$(grep -ac 'SkipPackage' "$out_dir/client.log")"
    grep -a 'SkipPackage' "$out_dir/client.log" | sed 's/.*SkipPackage: //; s/ (0x.*//' | sort -u | sed 's/^/  missing /'
    echo "client_warnings (Marburgum, materials, meshes, textures):"
    grep -aE 'LogMarburgum[A-Za-z]*: (Warning|Error)|LogMaterial: (Warning|Error)|LogStaticMesh: (Warning|Error)|LogTexture: (Warning|Error)|LogStreaming: (Warning|Error)' \
        "$out_dir/client.log" | sed 's/^\[[^]]*\]\[[^]]*\]/  /' | sort | uniq -c | sort -rn | head -30
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }
cat "$out_dir/summary.txt"
[[ "$code" -eq 0 && "$shots" -eq "$expected" ]]
