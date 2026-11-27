#!/usr/bin/env bash
# Chapter IV, its Maja branch, Photography and the two photography side missions (ADR-081), played on /Game/World/lent
# against a real dedicated server through the same RPCs the E and P keys and the choice panel use.
#
# The world store is seeded (schema 5, revision 1) with one character who has finished the prologue and Chapters I-III
# and told Maja in Chapter I (`maja_informed`), plus a second character with nothing. Each step is its own client
# process, so every step is also a disconnect and a reconnect in the middle of a mission.
#
#   Phase 1  Chapter IV from Vera to the decision at Tone's cab (ask_tone); Maja's folder (outcome gate); Nika lends the
#            camera; the Water Tower, the Judgement Tower and the bridge photographed through the server's frame check;
#            Nika's darkroom; Brane's signs including the Prehod sign on the Melje floor; a side photograph (the synagogue)
#            and the same again refused. Two characters talk to Nika at the same moment and both start her mission.
#   Phase 2  the server is restarted on the same store: the CHECK client finds every mission, outcome and skill restored,
#            and `mb2 store-check story` reads the document itself.
#
# Usage: scripts/story-smoke-linux.sh [staged|editor]   (MB2_STORY_PORT, default 27731; never 7777 or 17777;
#        MB2_STORY_CLOCK the server's world clock, default Tuesday 10:00; MB2_STORY_CLIENT_ARGS extra client options,
#        e.g. -culture=sl to play the whole sequence in Slovene)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_STORY_PORT:-27731}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/StorySmoke/$(date -u +%Y%m%dT%H%M%SZ)"
key="mb2s-t$(date +%s)$RANDOM"
other="mb2s-o$(date +%s)$RANDOM"

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
if [[ "$port" == 7777 || "$port" == 17777 ]]; then
    echo "Port $port is the owner's playtest; pick another MB2_STORY_PORT." >&2
    exit 2
fi
for executable in "${server_cmd[0]}" "${client_cmd[0]}"; do
    if [[ ! -x "$executable" ]]; then
        echo "Missing executable: $executable (run scripts/package-linux.sh first?)" >&2
        exit 1
    fi
done
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-1200}" "${client_cmd[@]}")

mkdir -p "$out_dir"
store="$out_dir/world-store.json"
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

start_server() {
    local log="$1"
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_STORY_PORT." >&2
        exit 1
    fi
    # A working morning by default (Tuesday 10:00): named people are at their authored posts (ADR-081) and every
    # counter is open. Set MB2_STORY_CLOCK to play the same sequence at another hour.
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" -MB2Clock="${MB2_STORY_CLOCK:-2026-06-16T08:00:00Z}" -MB2ClockSpeed=0.001 \
        "${common_args[@]}" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 360); do
        grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log" && return 0
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            exit 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port within 180 s; see $log" >&2
    exit 1
}

# run <label> <role> <start tag> <character> [client args...]
run() {
    local label="$1" role="$2" start="$3" who="$4"
    shift 4
    local url="127.0.0.1:$port?MB2Start=$start?MB2Character=$who"
    local log="$out_dir/client-$label.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$label.vulkan-init-failed-$attempt.log"
            sleep 3
            code=0
        fi
        # shellcheck disable=SC2086 # MB2_STORY_CLIENT_ARGS is a list of options, e.g. -culture=sl
        "${client_cmd[@]}" "$url" "${common_args[@]}" ${MB2_STORY_CLIENT_ARGS:-} "$@" -MB2MissionSmoke -MB2MissionRole="$role" -MB2MissionTimeout=120 \
            > "$log" 2>&1 || code=$?
    done
    if [[ "$code" -eq 0 ]] && grep -aq "MB2MISSION role=client tag=$role event=pass" "$log"; then
        echo "  client $label: PASS"
    else
        echo "  client $label: FAIL (exit $code)"
        grep -ah "MB2MISSION role=client tag=$role event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2MISSION/    MB2MISSION/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2MissionShot=$out_dir/$1"; }

# step <label> <target> <expected effect> [client args...]
step() {
    local label="$1" target="$2" expect="$3"
    shift 3
    run "$label" STEP "mission_$target" "$key" -MB2MissionTarget="$target" -MB2MissionExpect="$expect" "$@"
}

# photo <label> <subject> <expected effect or refused:reason> [client args...]
photo() {
    local label="$1" subject="$2" expect="$3"
    shift 3
    run "$label" PHOTO "photo_$subject" "$key" -MB2MissionTarget="$subject" -MB2MissionExpect="$expect" "$@"
}

# choice <label> <target> <mission> <choice>
choice() {
    run "$1" CHOICE "mission_$2" "$key" $(shot "$1") -MB2MissionTarget="$2" -MB2MissionId="$3" -MB2MissionChoice="$4"
}

done_mission() {
    printf '"%s":{"completed":true,"reward_paid":true,"stage":"%s","started_revision":1%s}' "$1" "$2" "${3:-}"
}
missions="$(done_mission lent_first_catch meet_vera ',"journal_entry":"blue_ledger_r90"'),$(done_mission raeda_names_no_longer_match decide_maja ',"choice":"tell_maja","outcome":"maja_informed"'),$(done_mission raeda_last_shift show_vera),$(done_mission raeda_city_for_sale decide_milan ',"choice":"trust_milan","outcome":"milan_trusted"')"
printf '{"format":"marburgumii.world_store","schema_version":5,"revision":1,"stocks":{},"inventories":{},"missions":{"%s":{%s}},"conditions":{},"skills":{"%s":{"reputation":40}},"money":{"%s":120},"receipts":{}}\n' \
    "$key" "$missions" "$key" "$key" > "$store"

echo "Phase 1: dedicated server ($mode) on $map, port $port, seeded store (Chapters I-III done, maja_informed); evidence in $out_dir"
start_server "$out_dir/server-1.log"
for _ in $(seq 1 80); do
    grep -aq 'event=content_ready' "$out_dir/server-1.log" && break
    sleep 0.25
done
spawned=$(grep -aoc "MB2INTERACT role=server event=content_spawned" "$out_dir/server-1.log" || true)
skipped=$(grep -aoc "MB2INTERACT role=server event=content_skipped" "$out_dir/server-1.log" || true)
photo_starts=$(grep -aoc "MB2INTERACT role=server event=photo_start" "$out_dir/server-1.log" || true)
unplaced=$(grep -a "MB2INTERACT role=server event=photo_unplaced" "$out_dir/server-1.log" | sed 's/.*id=\([a-z0-9_]*\).*/\1/' | tr '\n' ' ' || true)
echo "  server: authored content spawned=$spawned skipped=$skipped; photo starts=$photo_starts unplaced=[ $unplaced]"
[[ "$spawned" -ge 33 && "$skipped" -eq 0 && "$photo_starts" -ge 9 ]] || status=1

# The angler is a person with a rod now; a STEP there checks the body as well as the line (it has nothing to offer).
run angler-look STEP angler "$other" $(shot angler) -MB2MissionTarget=lent_angler -MB2MissionExpect=mission_started

# Chapter IV.
step ch4-offer vera_horvat mission_started $(shot ch4-offer)
step ch4-recorder jozica_rozman stage_completed $(shot ch4-recorder)
step ch4-drago drago_potocnik stage_completed
step ch4-tone tone_vogrin stage_completed $(shot ch4-tone)
step ch4-andrej andrej_mlakar stage_completed $(shot ch4-andrej)
step ch4-vera vera_horvat stage_completed
choice ch4-decision tone_vogrin raeda_voices_on_tape ask_tone
# Maja's folder: offered because Chapter I ended in maja_informed.
step folder-offer maja_koren mission_started
step folder-archive mariborski_glas_archive stage_completed
step folder-maja maja_koren mission_completed

# Two characters at Nika at the same moment: each starts the mission, each with a camera.
run nika-a STEP mission_nika_vidmar "$key" -nullrhi -MB2MissionTarget=nika_vidmar -MB2MissionExpect=mission_started &
pid_a=$!
run nika-b STEP mission_nika_vidmar "$other" -nullrhi -MB2MissionTarget=nika_vidmar -MB2MissionExpect=mission_started &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
grep -aq "tag=STEP event=pass" "$out_dir/client-nika-a.log" && grep -aq "tag=STEP event=pass" "$out_dir/client-nika-b.log" || status=1

photo photo-vodni vodni_stolp stage_completed $(shot photo-vodni)
photo photo-sodni sodni_stolp stage_completed $(shot photo-sodni)
photo photo-most stari_most stage_completed $(shot photo-most)
step nika-prints nika_vidmar stage_completed
choice nika-darkroom nika_vidmar nika_then_and_now darkroom_lesson
step brane-offer brane_sign_painter mission_started
photo photo-kvik kvik_lettering stage_completed
photo photo-kavarna velika_kavarna_lettering stage_completed
photo photo-prehod prehod_sign_melje stage_completed $(shot photo-prehod)
step brane-prints brane_sign_painter stage_completed $(shot brane-prints)
choice brane-settle brane_sign_painter brane_old_letters give_copies
photo photo-sinagoga sinagoga photographed
photo photo-sinagoga-again sinagoga refused:already_photographed
sleep 1
stop_server

echo "Phase 2: server restart on the same store"
start_server "$out_dir/server-2.log"
run check STORYCHECK arrival "$key" -nullrhi
stop_server

store_check=0
"$repo_root/scripts/mb2.sh" store-check story "$store" "$key" > "$out_dir/store-check.txt" 2>&1 || store_check=$?
sed 's/^/  store: /' "$out_dir/store-check.txt"
[[ "$store_check" -eq 0 ]] || status=1
photos=$(grep -ac "MB2INTERACT role=server event=photograph " "$out_dir/server-1.log" || true)
echo "  server: photographs committed=$photos (want 7)"
[[ "$photos" -eq 7 ]] || status=1

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

if [[ "$status" -eq 0 ]]; then
    echo "Story smoke PASS ($mode). Evidence: $out_dir"
else
    echo "Story smoke FAIL ($mode). Evidence: $out_dir" >&2
fi
exit "$status"
