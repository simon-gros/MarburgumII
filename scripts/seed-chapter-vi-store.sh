#!/usr/bin/env bash
# Writes a world store (schema 6) for an owner playtest of Chapter VI, The Negative (ADR-083): one character per given
# Kiosk 67 outcome, each having finished the Prologue to Chapter III, Nika's Then and Now (the camera) and Kiosk 67 with
# that outcome, holding the papers those missions leave in custody and 120 money. Chapters IV and V are not played.
#
# Usage: scripts/seed-chapter-vi-store.sh <store.json> <character>:<outcome> [<character>:<outcome> ...]
#   outcome: temporary_work | tab_paid | debt_erased | object_sold | left_alone
# Refuses to overwrite an existing file. Characters are development keys (3-40 of a-z, 0-9, - and _).
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <store.json> <character>:<outcome> [...]" >&2
    exit 2
fi
store="$1"
shift
if [[ -e "$store" ]]; then
    echo "$store exists; choose a new path (a playtest store is never overwritten)." >&2
    exit 1
fi

done_mission() {
    printf '"%s":{"completed":true,"reward_paid":true,"stage":"%s","started_revision":1%s}' "$1" "$2" "${3:-}"
}
papers='{"torn_gate_photograph":1,"faded_transport_ticket":1,"carbon_copy_receipt":1,"water_damaged_accounting_page":1,"brass_key":1,"drava_cashbox":1,"kainach_serial_photos":1,"yard_vehicle_log":1,"corrected_yard_schedule":1,"tone_vogrin_locker_card":1,"exchange_counterfoil_copy":1,"kainach_contract_reference":1,"prehod_classified_clippings":1,"pohorska_bank_file_locator":1,"k67_raeda_concession_copy":1,"camera":1,"nika_old_prints":1,"photo_vodni_stolp":1,"photo_sodni_stolp":1,"photo_stari_most":1,"fishing_rod":1}'
inventories=""
missions=""
money=""
for entry in "$@"; do
    who="${entry%%:*}"
    outcome="${entry#*:}"
    if [[ ! "$who" =~ ^[a-z0-9_-]{3,40}$ ]]; then
        echo "Not a character key: $who" >&2
        exit 2
    fi
    case "$outcome" in
        temporary_work) choice=find_work ;;
        tab_paid) choice=pay_tab ;;
        debt_erased) choice=erase_debt ;;
        object_sold) choice=sell_object ;;
        left_alone) choice=leave_alone ;;
        *) echo "Unknown Kiosk 67 outcome: $outcome" >&2; exit 2 ;;
    esac
    played="$(done_mission lent_first_catch meet_vera ',"journal_entry":"blue_ledger_r90"'),$(done_mission raeda_names_no_longer_match decide_maja ',"choice":"tell_maja","outcome":"maja_informed"'),$(done_mission raeda_last_shift show_vera),$(done_mission raeda_city_for_sale decide_milan ',"choice":"trust_milan","outcome":"milan_trusted"'),$(done_mission nika_then_and_now keep_or_archive ',"choice":"darkroom_lesson","outcome":"darkroom_lesson"'),$(done_mission kiosk_67_customer_stopped_coming resolution ",\"choice\":\"$choice\",\"outcome\":\"$outcome\"")"
    inventories="${inventories:+$inventories,}\"$who\":$papers"
    missions="${missions:+$missions,}\"$who\":{$played}"
    money="${money:+$money,}\"$who\":120"
done
mkdir -p "$(dirname "$store")"
printf '{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{},"inventories":{%s},"missions":{%s},"conditions":{},"skills":{},"money":{%s},"discoveries":{},"receipts":{}}\n' \
    "$inventories" "$missions" "$money" > "$store"
echo "Wrote $store for: $*"
