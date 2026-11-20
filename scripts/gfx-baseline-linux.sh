#!/usr/bin/env bash
# GFX-00 rendering baseline of /Game/World/lent (Docs/GRAPHICS_ROADMAP.md). Runs the visual survey
# (scripts/visual-survey-linux.sh) once per scene of a fixed time-of-day and weather matrix, with the graphics
# roadmap's river, quay and roof cameras (`mb2 gfx-views`) and the player's own camera with the HUD first, then one
# short scene at the Hotel Lent street door (ADR-076). Changes no setting: it only reads back what the renderer does.
#
#   scripts/gfx-baseline-linux.sh [editor|staged] [name]
#
# Scenes (summer, Maribor local time = UTC+2): morning 07:00, noon 12:00, dusk 21:00, night 23:30 (clear_dry);
# noon overcast; noon and night rain_wet; noon at the door. MB2_GFX_SCENES limits the run to a comma-separated
# subset (e.g. noon,night). MB2_GFX_SETTLE seconds per view (default 4). MB2_GFX_CLIENT_ARGS goes to every client (an A/B
# variant of the same build, e.g. -sm5 or -ini:Engine:[SystemSettings]:r.DynamicGlobalIlluminationMethod=0). MB2_SURVEY_PORT as for the survey (default
# 27891; never 7777 or 17777, which are for playtests).
#
# This host is shared: other sessions' smokes render on the same GPU and lengthen the frame-time tails. A monitor samples
# every 5 s how many other Unreal game, server and editor processes are running (gpu-monitor.tsv), and baseline.json
# names, per scene, how many samples inside its run saw one ("contended"). GPU utilisation is recorded but is no signal:
# the survey client runs uncapped and keeps the GPU near full on its own.
#
# Evidence in Saved/GfxBaseline/<name or UTC timestamp>/: meta.env (source and engine, cooked content hash for staged,
# GPU and driver, other GPU users at the start), gpu-monitor.tsv, one directory per scene (PNGs, stats.tsv, render_config.json, logs,
# summary.txt), and baseline.csv / baseline.json from `mb2 gfx-baseline`. Editor numbers are labelled editor_game_mode
# and compare only with each other; a frame budget needs the staged (packaged) run.
set -uo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-editor}"
name="${2:-$(date -u +%Y%m%dT%H%M%SZ)}"
out_dir="$repo_root/Saved/GfxBaseline/$name"
settle="${MB2_GFX_SETTLE:-4}"
door_start="door_r1539756" # Hotel Lent, the door smoke's default building

case "$mode" in
    staged | editor) ;;
    *)
        echo "Usage: $0 [editor|staged] [name]" >&2
        exit 2
        ;;
esac

# scene|UTC clock|weather|start|views (all: survey + gfx views; player: only the player's camera)
scenes=(
    "1_morning|2026-06-18T05:00:00Z|clear_dry||all"
    "2_noon|2026-06-18T10:00:00Z|clear_dry||all"
    "3_dusk|2026-06-18T19:00:00Z|clear_dry||all"
    "4_night|2026-06-18T21:30:00Z|clear_dry||all"
    "5_overcast|2026-06-18T10:00:00Z|overcast||all"
    "6_rain_noon|2026-06-18T10:00:00Z|rain_wet||all"
    "7_rain_night|2026-06-18T21:30:00Z|rain_wet||all"
    "8_door_noon|2026-06-18T10:00:00Z|clear_dry|$door_start|player"
)

mkdir -p "$out_dir"
survey_views="$("$repo_root/scripts/mb2.sh" survey-views "$repo_root/Content/Marburgum/Street/lent_walk_graph.json")" || exit 1
gfx_views="$("$repo_root/scripts/mb2.sh" gfx-views)" || exit 1

{
    echo "mode=$mode"
    echo "source_sha=$(git -C "$repo_root" rev-parse HEAD)"
    echo "source_dirty=$([[ -n "$(git -C "$repo_root" status --porcelain -- Source Config Content Data Plugins)" ]] && echo 1 || echo 0)"
    echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "settle_s=$settle"
    echo "client_args=${MB2_GFX_CLIENT_ARGS:-}"
    echo "resolution=1920x1080"
    echo "host_kernel=$(uname -r)"
    echo "gpu=$(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>/dev/null | head -n 1)"
    echo "gpu_at_start=$(nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader 2>/dev/null | head -n 1)"
    echo "other_unreal_processes=$(pgrep -f '/Binaries/Linux/(UnrealEditor|MarburgumII(Client|Server))( |$)' | wc -l)"
    if [[ "$mode" == editor ]]; then
        echo "engine_build_version=$(tr -d '[:space:]' < "${UE_ROOT:-/nonexistent}/Engine/Build/Build.version" 2>/dev/null | grep -o '"MajorVersion":[0-9]*,"MinorVersion":[0-9]*,"PatchVersion":[0-9]*')"
    else
        # The cooked content the client actually loaded: SHA-256 of each pak/IoStore container, then of that list.
        paks="$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumII/Content/Paks"
        (cd "$paks" 2>/dev/null && sha256sum -- *) > "$out_dir/cooked-content.sha256"
        echo "cooked_content_sha256=$(sha256sum < "$out_dir/cooked-content.sha256" | cut -d' ' -f1)"
        "$repo_root/scripts/build-stamp.sh" check 2>&1 | sed 's/: /=/'
    fi
} > "$out_dir/meta.env"
cat "$out_dir/meta.env"
[[ "$(sed -n 's/^other_unreal_processes=//p' "$out_dir/meta.env")" != 0 ]] &&
    echo "WARNING: other Unreal processes are running; they share the GPU and skew the numbers (named in meta.env)." >&2

# epoch, GPU utilisation %, GPU memory MiB, other Unreal processes (ours carry this run's directory on their command line)
monitor() {
    while true; do
        printf '%s\t%s\t%s\n' "$(date +%s)" \
            "$(nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>/dev/null | head -n 1 | tr -d ' ' | tr ',' '\t')" \
            "$(pgrep -af '/Binaries/Linux/(UnrealEditor|MarburgumII(Client|Server))( |$)' | grep -vcF "$out_dir")"
        sleep 5
    done
}
printf 'epoch\tgpu_util\tgpu_mem_mb\tother_unreal\n' > "$out_dir/gpu-monitor.tsv"
monitor >> "$out_dir/gpu-monitor.tsv" &
monitor_pid=$!
trap 'kill "$monitor_pid" 2>/dev/null' EXIT

failed=0
for entry in "${scenes[@]}"; do
    IFS='|' read -r scene clock weather start which <<< "$entry"
    if [[ -n "${MB2_GFX_SCENES:-}" && ",${MB2_GFX_SCENES}," != *",${scene#*_},"* ]]; then
        continue
    fi
    if [[ "$which" == player ]]; then
        views="player_hud@player@player@0"
    else
        views="player_hud@player@player@0;$gfx_views;$survey_views"
    fi
    echo "== $scene ($clock, $weather${start:+, start $start})"
    MB2_SURVEY_OUT="$out_dir/$scene" MB2_SURVEY_CLOCK="$clock" MB2_SURVEY_WEATHER="$weather" MB2_SURVEY_START="$start" \
        MB2_SURVEY_VIEWS="$views" MB2_SURVEY_SETTLE="$settle" MB2_SURVEY_CLIENT_ARGS="${MB2_GFX_CLIENT_ARGS:-}" \
        "$repo_root/scripts/visual-survey-linux.sh" "$mode" "$name-$scene" > "$out_dir/$scene.out" 2>&1 || failed=1
    head -n 3 "$out_dir/$scene/summary.txt" 2>/dev/null || { echo "  no summary; see $out_dir/$scene.out"; failed=1; }
done

"$repo_root/scripts/mb2.sh" gfx-baseline "$out_dir" || failed=1
exit "$failed"
