#!/usr/bin/env bash
# The Foto Vidmar investigation played with keyboard, mouse and gamepad input events (UI-02 input work), on /Game/World/lent
# against a real dedicated server, in a rendered client. Two scenes of the existing story are the acceptance cases:
#
#   Chapter VI, the order-book page at the counter (raeda_the_negative, copy_order_book): left stick, mouse look, the
#            right stick aiming the camera until the prompt offers the photograph, the prompt's key following the device
#            (P / RT), RT taking the photograph; View and Y opening the Journal and the Inventory, B closing them.
#   Frame 37, the deduction at the light table (nika_frame_37, what_matters): A for the loupe; the card opened with A and
#            held (nothing sent), the D-pad and a pointer move switching its hints, B and Escape cancelling; a double-click
#            on a wrong answer; a held number key on the other wrong answer; the D-pad and A on the right answer.
#
# The story before each scene is played through the RPC drivers (scripts/negative-smoke-linux.sh's steps, -nullrhi); each
# input scene is its own client process (UMarburgumInputSmokeSubsystem, -MB2InputSmoke). Between the light-table processes
# the world store the server commits is read: cancelling and the wrong answers must leave the stage, Photography XP, money
# and inventory exactly as they were, and the right answer must move the stage on once and add exactly 10 Photography XP.
#
# Usage: scripts/input-smoke-linux.sh [staged|editor]
#   MB2_INPUT_PORT (default 27763; never 7777 or 17777), MB2_INPUT_CULTURE en|sl (default en),
#   MB2_INPUT_RES WIDTHxHEIGHT (default 1920x1080), MB2_INPUT_CLOCK (default Tuesday 10:00),
#   MB2_INPUT_CLIENT_ARGS extra options for the rendered clients (e.g. -sm5),
#   MB2_INPUT_SCENES all|counter|table (default all; one scene skips the other's input clients).
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_INPUT_PORT:-27763}"
culture="${MB2_INPUT_CULTURE:-en}"
res="${MB2_INPUT_RES:-1920x1080}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/InputSmoke/$(date -u +%Y%m%dT%H%M%SZ)-$culture-$res"
key="mb2i-$(date +%s)$RANDOM"

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
if [[ "$port" == 7777 || "$port" == 17777 ]]; then
    echo "Port $port is the owner's playtest; pick another MB2_INPUT_PORT." >&2
    exit 2
fi
if [[ ! "$res" =~ ^([0-9]{3,4})x([0-9]{3,4})$ ]]; then
    echo "MB2_INPUT_RES must be WIDTHxHEIGHT, e.g. 1280x720." >&2
    exit 2
fi
res_x="${BASH_REMATCH[1]}"
res_y="${BASH_REMATCH[2]}"
case "$culture" in en|sl) ;; *) echo "MB2_INPUT_CULTURE must be en or sl." >&2; exit 2 ;; esac
for executable in "${server_cmd[0]}" "${client_cmd[0]}"; do
    if [[ ! -x "$executable" ]]; then
        echo "Missing executable: $executable (run scripts/package-linux.sh first?)" >&2
        exit 1
    fi
done
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-600}" "${client_cmd[@]}")

mkdir -p "$out_dir"
store="$out_dir/world-store.json"
# -RenderOffScreen on every client, shot or not: a run under systemd-run inherits the desktop's DISPLAY and
# WAYLAND_DISPLAY, and a client without it opens a window on the owner's screen. The server ignores it.
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -RenderOffScreen)
render_args=(-RenderOffScreen -windowed -ForceRes "-ResX=$res_x" "-ResY=$res_y" "-culture=$culture")
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
        echo "UDP port $port is already in use; stop the other server or set MB2_INPUT_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" -MB2Clock="${MB2_INPUT_CLOCK:-2026-06-16T08:00:00Z}" -MB2ClockSpeed=0.001 \
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

# client <label> <log tag> <start tag> [client args...]: one client process; retried only when Vulkan failed to start.
client() {
    local label="$1" tag="$2" start="$3"
    shift 3
    local log="$out_dir/client-$label.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$label.vulkan-init-failed-$attempt.log"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "127.0.0.1:$port?MB2Start=$start?MB2Character=$key" "${common_args[@]}" "$@" > "$log" 2>&1 || code=$?
    done
    if [[ "$code" -eq 0 ]] && grep -aq "$tag event=pass" "$log"; then
        echo "  client $label: PASS"
    else
        echo "  client $label: FAIL (exit $code)"
        grep -ah "$tag event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*\(MB2[A-Z]*\)/    \1/' || true
        status=1
    fi
}

# step <label> <target> <expected effect>: the story's RPC driver, no renderer.
step() {
    client "$1" "MB2MISSION role=client tag=STEP" "mission_$2" -nullrhi -MB2MissionSmoke -MB2MissionRole=STEP -MB2MissionTimeout=120 \
        -MB2MissionTarget="$2" -MB2MissionExpect="$3"
}
photo() {
    client "$1" "MB2MISSION role=client tag=PHOTO" "photo_$2" -nullrhi -MB2MissionSmoke -MB2MissionRole=PHOTO -MB2MissionTimeout=120 \
        -MB2MissionTarget="$2" -MB2MissionExpect="$3"
}
# play <label> <role> <start tag> [input args...]: a rendered client driven by input events.
play() {
    local label="$1" role="$2" start="$3"
    shift 3
    # shellcheck disable=SC2086 # MB2_INPUT_CLIENT_ARGS is a list of options
    client "$label" "MB2INPUT role=client tag=$role" "$start" "${render_args[@]}" ${MB2_INPUT_CLIENT_ARGS:-} -MB2InputSmoke -MB2InputRole="$role" -MB2InputTimeout=150 \
        -MB2InputShot="$out_dir/$label" "$@"
}

# snapshot <label>: what the server has committed for the character, one line (stage, XP, money, items, revision).
snapshot() {
    local line
    line="$("$repo_root/scripts/mb2.sh" store-check snapshot "$store" "$key")" || status=1
    echo "$1: $line" | tee -a "$out_dir/server-state.txt"
}
# same <a> <b>: the two snapshots agree on everything but the store revision.
same() {
    local a b
    a="$(grep -a "^$1:" "$out_dir/server-state.txt" | tail -1 | sed 's/^[^:]*: //; s/ revision=.*//')"
    b="$(grep -a "^$2:" "$out_dir/server-state.txt" | tail -1 | sed 's/^[^:]*: //; s/ revision=.*//')"
    if [[ "$a" == "$b" ]]; then
        echo "  server state: $2 = $1 (progress, XP, money and inventory unchanged)"
    else
        echo "  server state: $2 differs from $1"
        echo "    $1: $a"
        echo "    $2: $b"
        status=1
    fi
}
field() { grep -a "^$1:" "$out_dir/server-state.txt" | tail -1 | grep -ao "$2=[^ ]*" | cut -d= -f2; }

"$repo_root/scripts/seed-chapter-vi-store.sh" "$store" "$key:object_sold" > /dev/null
echo "Input smoke ($mode) on $map, port $port, culture $culture, $res; evidence in $out_dir"
start_server "$out_dir/server.log"
for _ in $(seq 1 80); do
    grep -aq 'event=content_ready' "$out_dir/server.log" && break
    sleep 0.25
done

echo "Chapter VI up to the order book (RPC drivers)"
step n6-offer nika_vidmar mission_started
step n6-date nika_vidmar stage_completed
step n6-book foto_vidmar_order_book stage_completed
snapshot before-page

scenes="${MB2_INPUT_SCENES:-all}"
echo "Scene 1: the order-book page at the counter (input events)"
if [[ "$scenes" == table ]]; then
    photo n6-page foto_vidmar_order_book_page stage_completed
else
play counter COUNTER mission_foto_vidmar_order_book -MB2InputTarget=foto_vidmar_order_book_page
snapshot after-page
[[ "$(field after-page negative)" != "$(field before-page negative)" ]] \
    && echo "  server state: the page photograph moved Chapter VI on ($(field before-page negative) -> $(field after-page negative))" \
    || { echo "  server state: the page photograph did not move Chapter VI on"; status=1; }
fi
if [[ "$scenes" == counter ]]; then
    stop_server
    [[ "$status" -eq 0 ]] && echo "Input smoke PASS (counter only)" || echo "Input smoke FAIL (counter only)" >&2
    exit "$status"
fi

echo "Chapter VI to the light table (RPC drivers)"
step n6-drago drago_potocnik stage_completed
step n6-ivana ivana_zupan stage_completed
step n6-cage block_13_storage_cage stage_completed
photo n6-sheet zupan_contact_sheet_copy stage_completed
step f37-offer nika_vidmar mission_started

echo "Scene 2: Frame 37 at the light table (input events)"
play loupe LOUPE mission_foto_vidmar_light_table -MB2InputTarget=foto_vidmar_light_table
snapshot deduction-due
[[ "$(field deduction-due frame_37)" == what_matters ]] || { echo "  server state: Frame 37 is not at the deduction"; status=1; }
play cancel CANCEL mission_foto_vidmar_light_table -MB2InputTarget=foto_vidmar_light_table
snapshot after-cancel
same deduction-due after-cancel
play mouse-wrong MOUSEWRONG mission_foto_vidmar_light_table -MB2InputTarget=foto_vidmar_light_table -MB2InputWrong=the_man
snapshot after-mouse-wrong
same deduction-due after-mouse-wrong
play number-wrong NUMBERWRONG mission_foto_vidmar_light_table -MB2InputTarget=foto_vidmar_light_table -MB2InputWrong=the_light_leak
snapshot after-number-wrong
same deduction-due after-number-wrong
play pad-right PADRIGHT mission_foto_vidmar_light_table -MB2InputTarget=foto_vidmar_light_table -MB2InputRight=the_place
snapshot after-right
xp_before="$(field deduction-due photography_xp)"
xp_after="$(field after-right photography_xp)"
stage_after="$(field after-right frame_37)"
if [[ "$stage_after" == nika_prints && $((xp_after - xp_before)) -eq 10 ]]; then
    echo "  server state: the right answer moved Frame 37 on once (what_matters -> $stage_after) and added exactly 10 Photography XP ($xp_before -> $xp_after)"
else
    echo "  server state: after the right answer frame_37=$stage_after photography_xp $xp_before -> $xp_after (want nika_prints and +10)"
    status=1
fi
sleep 1
stop_server

# The server's own record of every answer: one line per request, with what it decided.
grep -a "role=server event=interaction" "$out_dir/server.log" | grep -a "target=foto_vidmar_light_table" \
    | sed 's/.*event=interaction .* outcome=\([a-z_]*\) reason=\([a-z_]*\) effect=\([a-z_]*\) mission=\([a-z_0-9]*\) stage=\([a-z_]*\) .* choice=\([a-z_-]*\) .* xp=\(.*\)$/  server: light table outcome=\1 reason=\2 effect=\3 stage=\5 choice=\6 xp=\7/' \
    | tee "$out_dir/server-answers.txt"
answers="$(grep -c "choice=the_" "$out_dir/server-answers.txt" || true)"
echo "  server: answers received=$answers (want 3: the_man, the_light_leak, the_place; the cancels and held keys send none)"
[[ "$answers" -eq 3 ]] || status=1
photos=$(grep -ac "MB2INTERACT role=server event=photograph " "$out_dir/server.log" || true)
echo "  server: photographs committed=$photos (want 2: the page by RT, the contact sheet by the RPC driver)"
[[ "$photos" -eq 2 ]] || status=1

[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }
if [[ "$status" -eq 0 ]]; then
    echo "Input smoke PASS ($mode, $culture, $res). Evidence: $out_dir"
else
    echo "Input smoke FAIL ($mode, $culture, $res). Evidence: $out_dir" >&2
fi
exit "$status"
