#!/usr/bin/env bash
# Missions, fishing and skills from the original game (ADR-047..049), end to end on /Game/World/lent.
#
# Phase 1 (fresh world store), one character, one client process per step, each starting at the
# development start beside its target (?MB2Start=mission_<id>; the angler's own start for fishing):
#   FISH    the angler starts "Something in the Water" with a rod; no fish yet is refused; a strike before
#           the bite loses the cast; a recast inside the interval is refused; one fish gets away; then a
#           bite, a strike and a catch with Fishing XP; showing it completes the first stage.
#   STEP    the debris (six clues), Petra Kralj, the service room (the brass key) and Vera Horvat (mission
#           completed: 50 money and the "Blue Ledger" journal entry); Petra then offers Kiosk 67, and its
#           four witnesses each complete a stage.
#   CHOICE  at the missing customer the plain interaction is refused (a decision is due); the decision
#           panel pays his tab: -20 money, Reputation +15, outcome tab_paid, mission completed.
# The server is stopped and the store checked (mb2 store-check missions).
# Phase 2 (same store, restarted): CHECK sees both missions completed, the decision, 30 money, Fishing and
# Reputation XP and the clues restored, and opens the Skills panel and the journal. The store must be
# byte-identical afterwards.
#
# FISH, CHOICE and CHECK render offscreen (screenshots); the steps run -nullrhi.
# Usage: scripts/mission-smoke-linux.sh [staged|editor]. Evidence: Saved/MissionSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_MISSION_PORT:-7781}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/MissionSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
key="mb2q-t$(date +%s)$RANDOM"

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
store="$out_dir/world-store.json"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
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
        echo "UDP port $port is already in use; stop the other server or set MB2_MISSION_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" "${common_args[@]}" > "$log" 2>&1 &
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

# run <label> <role> <start tag> <extra args...>; output to client-<label>.log
run() {
    local label="$1" role="$2" start="$3"
    shift 3
    local url="127.0.0.1:$port?MB2Start=$start?MB2Character=$key"
    local log="$out_dir/client-$label.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$label.vulkan-init-failed-$attempt.log"
            echo "  client $label: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2MissionSmoke -MB2MissionRole="$role" -MB2MissionTimeout=120 \
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

# step <target> <expected effect>
step() {
    run "step-$1" STEP "mission_$1" -nullrhi -MB2MissionTarget="$1" -MB2MissionExpect="$2"
}

# ---------------------------------------------------------------- phase 1
echo "Phase 1: dedicated server ($mode) on $map, port $port, fresh store; evidence in $out_dir"
start_server "$out_dir/server-1.log"
# Spawn is logged after the ready line, and the dedicated-server log is block-buffered.
for _ in $(seq 1 80); do
    grep -aq 'event=content_ready' "$out_dir/server-1.log" && break
    sleep 0.25
done
spawned=$(grep -aoc "MB2INTERACT role=server event=content_spawned" "$out_dir/server-1.log" || true)
skipped=$(grep -aoc "MB2INTERACT role=server event=content_skipped" "$out_dir/server-1.log" || true)
echo "  server: authored content spawned=$spawned skipped=$skipped"
[[ "$spawned" -ge 30 && "$skipped" -eq 0 ]] || status=1

# shellcheck disable=SC2046  # word splitting of the argument list is intended
run fish FISH angler $(shot fish)
step drava_cashbox_debris stage_completed
step petra_kralj stage_completed
step raeda_service_room stage_completed
step vera_horvat mission_completed
# Names No Longer Match (ADR-050): Vera offers it only once the first catch is done.
step vera_horvat mission_started
mv "$out_dir/client-step-vera_horvat.log" "$out_dir/client-step-vera_horvat-names.log" 2>/dev/null || true
step raeda_directory_entry stage_completed
step raeda_successor_sign stage_completed
step marija_sever stage_completed
step petra_kralj stage_completed
mv "$out_dir/client-step-petra_kralj.log" "$out_dir/client-step-petra_kralj-names.log" 2>/dev/null || true
step mariborski_glas_archive stage_completed
# shellcheck disable=SC2046
run names CHOICE mission_maja_koren $(shot names) -MB2MissionTarget=maja_koren \
    -MB2MissionId=raeda_names_no_longer_match -MB2MissionChoice=tell_maja
step petra_kralj mission_started
mv "$out_dir/client-step-petra_kralj.log" "$out_dir/client-step-petra_kralj-kiosk.log" 2>/dev/null || true
step kiosk_67_bus_regular stage_completed
step kiosk_67_cafe_regular stage_completed
step kiosk_67_neighbour stage_completed
step kiosk_67_customer stage_completed
# shellcheck disable=SC2046
run choice CHOICE mission_kiosk_67_customer $(shot choice) -MB2MissionTarget=kiosk_67_customer \
    -MB2MissionId=kiosk_67_customer_stopped_coming -MB2MissionChoice=pay_tab
# The other three missions whose places stand on this map (ADR-051).
step lent_dispatcher mission_started
step lent_workshop_mechanic stage_completed
# shellcheck disable=SC2046
run fee CHOICE mission_lent_workshop_mechanic $(shot fee) -MB2MissionTarget=lent_workshop_mechanic \
    -MB2MissionId=lent_late_delivery -MB2MissionChoice=leave_clerk_share
step matej_hall_three mission_started
step hall_three_machine_plate stage_completed
step hall_three_labelled_boxes stage_completed
step hall_three_worker stage_completed
step hall_three_key_hook stage_completed
step hall_three_night_guard stage_completed
step hall_three_steel_cabinet stage_completed
# shellcheck disable=SC2046
run hall CHOICE mission_matej_hall_three $(shot hall) -MB2MissionTarget=matej_hall_three \
    -MB2MissionId=hall_three_last_shift -MB2MissionChoice=negotiate_settlement
step under_paint_new_owner mission_started
step under_paint_layer_one stage_completed
step under_paint_layer_two stage_completed
step under_paint_layer_three stage_completed
step under_paint_layer_four stage_completed
step brane_sign_painter stage_completed
step maja_koren stage_completed
step petra_kralj stage_completed
mv "$out_dir/client-step-petra_kralj.log" "$out_dir/client-step-petra_kralj-paint.log" 2>/dev/null || true
# shellcheck disable=SC2046
run sign CHOICE mission_under_paint_new_owner $(shot sign) -MB2MissionTarget=under_paint_new_owner \
    -MB2MissionId=under_the_paint_four_names -MB2MissionChoice=preserve_layers
# The Melje yard and the Tabor trail stand on temporary pads (ADR-052).
step matej_hall_three mission_started
step melje_yard_gate stage_completed
step melje_dispatch_lockers stage_completed
step melje_kainach_trucks stage_completed
step melje_vehicle_log_shelf stage_completed
step melje_archive_boxes stage_completed
step matej_hall_three stage_completed
step vera_horvat mission_completed
step vera_horvat mission_started
step tabor_exchange_counter stage_completed
step irena_vogrin stage_completed
step mariborski_glas_archive stage_completed
step pohorska_bank_archive_desk stage_completed
step milan_solar stage_completed
step petra_kralj stage_completed
# shellcheck disable=SC2046
run milan CHOICE mission_milan_solar $(shot milan) -MB2MissionTarget=milan_solar \
    -MB2MissionId=raeda_city_for_sale -MB2MissionChoice=trust_milan
sleep 1
stop_server

store_check=0
"$repo_root/scripts/mb2.sh" store-check missions "$store" "$key" > "$out_dir/store-check.txt" 2>&1 || store_check=$?
sed 's/^/  store: /' "$out_dir/store-check.txt"
[[ "$store_check" -eq 0 ]] || status=1
store_hash="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
casts=$(grep -ac "MB2FISH role=server event=cast " "$out_dir/server-1.log" || true)
bites=$(grep -ac "MB2FISH role=server event=bite " "$out_dir/server-1.log" || true)
ends=$(grep -a "MB2FISH role=server event=cast_end" "$out_dir/server-1.log" | sed 's/.* end=\([a-z_]*\).*/\1/' | tr '\n' ' ' || true)
echo "  fishing: casts=$casts bites=$bites ends=[ $ends] (want 3 casts, 2 bites, too_early escaped caught)"
[[ "$casts" -eq 3 && "$bites" -eq 2 && "$ends" == "too_early escaped caught " ]] || status=1

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"
# shellcheck disable=SC2046
run check CHECK arrival $(shot check)
sleep 1
stop_server
# Counted once the server has exited: the dedicated-server log is block-buffered, so store_loaded
# may not be on disk yet when the ready line is.
loaded=$(grep -ac "MB2INTERACT role=server event=store_loaded .* schema=5 " "$out_dir/server-2.log" || true)
store_hash_2="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
echo "  server phase 2: store_loaded(schema 5)=$loaded (want 1); store unchanged: $([[ "$store_hash" == "$store_hash_2" ]] && echo yes || echo NO)"
[[ "$loaded" -eq 1 && "$store_hash" == "$store_hash_2" ]] || status=1

{
    echo "MarburgumII missions, fishing and skills smoke (ADR-047..049)"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content Data || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "character: $key"
    echo "content: spawned=$spawned skipped=$skipped"
    echo "fishing: casts=$casts bites=$bites ends=$ends"
    echo "store_sha256: $store_hash"
    echo
    sed 's/^/store: /' "$out_dir/store-check.txt"
    echo
    grep -ah "MB2MISSION\|MB2FISH\|MB2INTERACT role=server event=\(interaction\|store_loaded\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2MISSION\|MB2FISH\|MB2INTERACT\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
