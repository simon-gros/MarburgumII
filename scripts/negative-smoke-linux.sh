#!/usr/bin/env bash
# Chapter VI, The Negative, and the Frame 37 side story (ADR-084), played on /Game/World/lent against a real dedicated
# server through the same RPCs the E and P keys and the choice panel use.
#
# The world store is seeded (schema 6) with two characters who have finished the Prologue to Chapter III, Nika's Then
# and Now (the camera) and Kiosk 67: the first ended in MB2_NEGATIVE_OUTCOME (default object_sold), the second in
# left_alone. Each step is its own client process, so every step is also a disconnect and a reconnect.
#
#   Phase 1  both characters ask Nika at the same moment and both start the chapter; the second dates the print and stops.
#            The first plays it through: the order book and a photograph of the page, the light table refusing a
#            photograph of a sheet that is not there yet, Drago, Ivana Zupan and the Block 13 cage on the Tabor floor, the
#            contact sheet photographed, Nika offering the scrap tin (the side story) because there is no print to compare,
#            the loupe, a wrong deduction refused and the right one, Nika's print, the Melje date board photographed on the
#            Melje floor, Vera's chronology, the Glas strip, Albin's route for the Kiosk 67 outcome, the ticket, and Nika
#            with nothing more. The Journal and the Inventory are captured mid-chapter.
#   Phase 2  the server is restarted on the same store; the first character's last step is refused as already done, and
#            `mb2 store-check negative` reads the document itself.
#
# Usage: scripts/negative-smoke-linux.sh [staged|editor]   (MB2_NEGATIVE_PORT, default 27751; never 7777 or 17777;
#        MB2_NEGATIVE_OUTCOME temporary_work|tab_paid|debt_erased|object_sold|left_alone; MB2_NEGATIVE_CLOCK the world
#        clock, default Tuesday 10:00; MB2_NEGATIVE_CLIENT_ARGS extra client options, e.g. -culture=sl)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_NEGATIVE_PORT:-27751}"
outcome="${MB2_NEGATIVE_OUTCOME:-object_sold}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/NegativeSmoke/$(date -u +%Y%m%dT%H%M%SZ)-$outcome"
key="mb2n-t$(date +%s)$RANDOM"
other="mb2n-o$(date +%s)$RANDOM"

case "$outcome" in
    temporary_work) choice_kiosk=find_work ;;
    tab_paid) choice_kiosk=pay_tab ;;
    debt_erased) choice_kiosk=erase_debt ;;
    object_sold) choice_kiosk=sell_object ;;
    left_alone) choice_kiosk=leave_alone ;;
    *) echo "Unknown MB2_NEGATIVE_OUTCOME $outcome" >&2; exit 2 ;;
esac
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
    echo "Port $port is the owner's playtest; pick another MB2_NEGATIVE_PORT." >&2
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
        echo "UDP port $port is already in use; stop the other server or set MB2_NEGATIVE_PORT." >&2
        exit 1
    fi
    # A working morning (Tuesday 10:00): everyone is at their authored post and Nika's lab is open.
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" -MB2Clock="${MB2_NEGATIVE_CLOCK:-2026-06-16T08:00:00Z}" -MB2ClockSpeed=0.001 \
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
        # shellcheck disable=SC2086 # MB2_NEGATIVE_CLIENT_ARGS is a list of options, e.g. -culture=sl
        "${client_cmd[@]}" "$url" "${common_args[@]}" ${MB2_NEGATIVE_CLIENT_ARGS:-} "$@" -MB2MissionSmoke -MB2MissionRole="$role" -MB2MissionTimeout=120 \
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

# step <label> <target> <expected effect or refused:reason> [client args...]
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

# choice <label> <target> <mission> <choice> [expected effect or refused:reason; default: the mission completes]
choice() {
    local expect=()
    [[ -n "${5:-}" ]] && expect=(-MB2MissionExpect="$5")
    run "$1" CHOICE "mission_$2" "$key" $(shot "$1") -MB2MissionTarget="$2" -MB2MissionId="$3" -MB2MissionChoice="$4" "${expect[@]}"
}

# tour <label> <steps>: the UI capture tour as the first character, at Nika's lab (Journal, Inventory).
tour() {
    local label="$1" steps="$2" code=0
    # shellcheck disable=SC2086
    "${client_cmd[@]}" "127.0.0.1:$port?MB2Start=mission_nika_vidmar?MB2Character=$key" "${common_args[@]}" "${render_args[@]}" \
        ${MB2_NEGATIVE_CLIENT_ARGS:-} -MB2UICapture="$out_dir" -MB2UICaptureScript=tour -MB2UICaptureLabel="$label" -MB2UITour="$steps" \
        > "$out_dir/client-$label.log" 2>&1 || code=$?
    if [[ "$code" -eq 0 ]]; then
        echo "  tour $label: captured"
    else
        echo "  tour $label: FAIL (exit $code)"
        status=1
    fi
}

done_mission() {
    printf '"%s":{"completed":true,"reward_paid":true,"stage":"%s","started_revision":1%s}' "$1" "$2" "${3:-}"
}
seed_missions() {
    local kiosk_choice="$1" kiosk_outcome="$2"
    printf '%s,%s,%s,%s,%s,%s' \
        "$(done_mission lent_first_catch meet_vera ',"journal_entry":"blue_ledger_r90"')" \
        "$(done_mission raeda_names_no_longer_match decide_maja ',"choice":"tell_maja","outcome":"maja_informed"')" \
        "$(done_mission raeda_last_shift show_vera)" \
        "$(done_mission raeda_city_for_sale decide_milan ',"choice":"trust_milan","outcome":"milan_trusted"')" \
        "$(done_mission nika_then_and_now keep_or_archive ',"choice":"darkroom_lesson","outcome":"darkroom_lesson"')" \
        "$(done_mission kiosk_67_customer_stopped_coming resolution ",\"choice\":\"$kiosk_choice\",\"outcome\":\"$kiosk_outcome\"")"
}
# What those missions leave in custody (the prologue's cashbox papers, Chapter II's serial photographs, the camera).
papers='{"torn_gate_photograph":1,"faded_transport_ticket":1,"carbon_copy_receipt":1,"water_damaged_accounting_page":1,"brass_key":1,"drava_cashbox":1,"kainach_serial_photos":1,"yard_vehicle_log":1,"corrected_yard_schedule":1,"tone_vogrin_locker_card":1,"exchange_counterfoil_copy":1,"kainach_contract_reference":1,"prehod_classified_clippings":1,"pohorska_bank_file_locator":1,"k67_raeda_concession_copy":1,"camera":1,"nika_old_prints":1,"photo_vodni_stolp":1,"photo_sodni_stolp":1,"photo_stari_most":1}'
printf '{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{},"inventories":{"%s":%s,"%s":%s},"missions":{"%s":{%s},"%s":{%s}},"conditions":{},"skills":{},"money":{"%s":120,"%s":120},"discoveries":{},"receipts":{}}\n' \
    "$key" "$papers" "$other" "$papers" "$key" "$(seed_missions "$choice_kiosk" "$outcome")" "$other" "$(seed_missions leave_alone left_alone)" \
    "$key" "$other" > "$store"

echo "Phase 1: dedicated server ($mode) on $map, port $port; Kiosk 67 ended in $outcome; evidence in $out_dir"
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
for id in foto_vidmar_order_book foto_vidmar_light_table ivana_zupan block_13_storage_cage melje_date_board stamp_collector viole_terrace_regular; do
    grep -aq "event=content_spawned id=$id " "$out_dir/server-1.log" || { echo "  server: $id not spawned"; status=1; }
done
[[ "$skipped" -eq 0 && -z "${unplaced// /}" ]] || status=1

# Two characters at Nika at the same moment: each starts their own chapter.
run n6-offer-a STEP mission_nika_vidmar "$key" -nullrhi -MB2MissionTarget=nika_vidmar -MB2MissionExpect=mission_started &
pid_a=$!
run n6-offer-b STEP mission_nika_vidmar "$other" -nullrhi -MB2MissionTarget=nika_vidmar -MB2MissionExpect=mission_started &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
grep -aq "tag=STEP event=pass" "$out_dir/client-n6-offer-a.log" && grep -aq "tag=STEP event=pass" "$out_dir/client-n6-offer-b.log" || status=1
run n6-other-date STEP mission_nika_vidmar "$other" -nullrhi -MB2MissionTarget=nika_vidmar -MB2MissionExpect=stage_completed

step n6-date nika_vidmar stage_completed $(shot n6-date)
step n6-book foto_vidmar_order_book stage_completed $(shot n6-book)
photo n6-page foto_vidmar_order_book_page stage_completed $(shot n6-page)
photo n6-sheet-early zupan_contact_sheet_copy refused:requirement_not_met -nullrhi
step n6-drago drago_potocnik stage_completed
step n6-cage-early block_13_storage_cage refused:nothing_to_do -nullrhi
step n6-ivana ivana_zupan stage_completed $(shot n6-ivana)
step n6-cage block_13_storage_cage stage_completed $(shot n6-cage)
photo n6-sheet zupan_contact_sheet_copy stage_completed $(shot n6-sheet)
# No print to compare yet: Nika offers the scrap tin.
step f37-offer nika_vidmar mission_started $(shot f37-offer)
step f37-loupe foto_vidmar_light_table stage_completed $(shot f37-loupe)
choice f37-wrong foto_vidmar_light_table nika_frame_37 the_man refused:wrong_conclusion
choice f37-right foto_vidmar_light_table nika_frame_37 the_place stage_completed
step f37-print nika_vidmar stage_completed
step n6-match nika_vidmar stage_completed
photo f37-yard melje_date_board_view stage_completed $(shot f37-yard)
step f37-show nika_vidmar stage_completed
choice f37-scrap nika_vidmar nika_frame_37 lab_safe
step n6-vera vera_horvat stage_completed $(shot n6-vera)
step n6-glas mariborski_glas_archive stage_completed $(shot n6-glas)
tour n6-ui journal,inventory:frame_37_print,inventory:zupan_contact_sheet,inventory:chronology_frame_37
case "$outcome" in
    temporary_work)
        step n6-albin kiosk_67_customer stage_completed $(shot n6-albin)
        ;;
    tab_paid|debt_erased)
        step n6-albin kiosk_67_customer stage_completed $(shot n6-albin)
        step n6-albin-again kiosk_67_customer refused:nothing_to_do -nullrhi
        step n6-petra petra_kralj stage_completed
        step n6-albin-2 kiosk_67_customer stage_completed $(shot n6-albin-2)
        ;;
    object_sold)
        step n6-albin kiosk_67_customer stage_completed $(shot n6-albin)
        choice n6-collector stamp_collector raeda_the_negative buy_stamp stage_completed
        step n6-albin-stamp kiosk_67_customer stage_completed $(shot n6-albin-stamp)
        ;;
    left_alone)
        step n6-albin kiosk_67_customer stage_completed $(shot n6-albin)
        step n6-albin-again kiosk_67_customer refused:nothing_to_do -nullrhi
        step n6-boris viole_terrace_regular stage_completed $(shot n6-boris)
        step n6-albin-2 kiosk_67_customer stage_completed $(shot n6-albin-2)
        ;;
esac
step n6-ticket nika_vidmar mission_completed $(shot n6-ticket)
step n6-again nika_vidmar refused:nothing_to_do -nullrhi
sleep 1
stop_server

echo "Phase 2: server restart on the same store"
start_server "$out_dir/server-2.log"
step n6-after-restart nika_vidmar refused:nothing_to_do -nullrhi
photo n6-yard-again melje_date_board_view refused:already_photographed -nullrhi
stop_server

store_check=0
"$repo_root/scripts/mb2.sh" store-check negative "$store" "$key" "$other" "$outcome" > "$out_dir/store-check.txt" 2>&1 || store_check=$?
sed 's/^/  store: /' "$out_dir/store-check.txt"
[[ "$store_check" -eq 0 ]] || status=1
photos=$(grep -ac "MB2INTERACT role=server event=photograph " "$out_dir/server-1.log" || true)
echo "  server: photographs committed=$photos (want 3)"
[[ "$photos" -eq 3 ]] || status=1

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

if [[ "$status" -eq 0 ]]; then
    echo "Negative smoke PASS ($mode, $outcome). Evidence: $out_dir"
else
    echo "Negative smoke FAIL ($mode, $outcome). Evidence: $out_dir" >&2
fi
exit "$status"
