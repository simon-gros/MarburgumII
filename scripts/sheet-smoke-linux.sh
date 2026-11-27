#!/usr/bin/env bash
# Character sheet: attributes and equipment (ADR-085, LRP-01/02), end to end on /Game/World/lent.
#
# The world store is seeded as a schema 6 document (revision 1) with three characters: A carries a fishing rod, the
# borrowed camera and two chub, and its attributes are a legacy record that still says `wind`; B carries a rod and its
# record has both `wind` and `endurance` with different values; C has no attributes at all.
#
# Phase 1: the server migrates the store once as it loads (legacy alias and conflict reported, one commit, revision 2).
#   SHEET   (A, renders) attributes 12/14/9/11 on the sheet; the rod into the right hand from the sheet; the replay
#           answered as a duplicate; the rod into the left hand too, the camera on the head, an unknown slot, an unknown
#           item and emptying the empty bag refused; the camera into the left hand from the Inventory.
#   PEER    (B, -nullrhi, at the same time) Endurance 16 (the canonical id won); its rod into its right hand; it never
#           receives A's attributes, equipment or inventory.
# The server is stopped and the store checked (mb2 store-check sheet 1): schema 7, revision 5, no `wind` anywhere.
# Phase 2, same store: RESTORED (A) finds scores and both hands as left and takes the rod out from the sheet; the load
# migrates nothing. Store check 2: revision 6.
#
# Usage: scripts/sheet-smoke-linux.sh [staged|editor]. Evidence: Saved/SheetSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_SHEET_PORT:-27785}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/SheetSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
stamp="$(date +%s)$RANDOM"
key_a="mb2s-a$stamp"
key_b="mb2s-b$stamp"
key_c="mb2s-c$stamp"

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

for executable in "${server_cmd[0]}" "${client_cmd[0]}"; do
    if [[ ! -x "$executable" ]]; then
        echo "Missing executable: $executable (run scripts/package-linux.sh first?)" >&2
        exit 1
    fi
done
# A client stuck before its in-engine timeout starts must not hang the script (see shop-smoke-linux.sh).
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-3600}" "${client_cmd[@]}")

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
        echo "UDP port $port is already in use; stop the other server or set MB2_SHEET_PORT." >&2
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

# launch <label> <role> <key> <extra args...>: starts one client in the background; its pid is in $launched.
launch() {
    local label="$1" role="$2" key="$3"
    shift 3
    local url="127.0.0.1:$port?MB2Start=arrival?MB2Character=$key"
    "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2SheetSmoke -MB2SheetRole="$role" -MB2SheetTimeout=150 \
        > "$out_dir/client-$label.log" 2>&1 &
    launched=$!
}

# check <label> <role> <exit code>
check() {
    local label="$1" role="$2" code="$3" log="$out_dir/client-$1.log"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2SHEET role=client tag=$role event=pass" "$log"; then
        echo "  client $label: PASS"
    else
        echo "  client $label: FAIL (exit $code)"
        grep -ah "MB2SHEET role=client tag=$role event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2SHEET/    MB2SHEET/' || true
        grep -aq "Vulkan device creation failed" "$log" && echo "    (Vulkan device creation failed: another client may hold the GPU)"
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2SheetShot=$out_dir/$1"; }

# ---------------------------------------------------------------- phase 1
printf '{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{},"inventories":{"%s":{"fishing_rod":1,"camera":1,"chub":2},"%s":{"fishing_rod":1}},"missions":{},"conditions":{},"skills":{},"money":{},"attributes":{"%s":{"might":12,"wind":14,"eye":9,"poise":11},"%s":{"might":10,"wind":7,"endurance":16,"eye":10,"poise":10}},"receipts":{}}\n' \
    "$key_a" "$key_b" "$key_a" "$key_b" > "$store"
echo "Phase 1: dedicated server ($mode) on $map, port $port, seeded legacy store; evidence in $out_dir"
start_server "$out_dir/server-1.log"

# shellcheck disable=SC2046  # word splitting of the argument list is intended
launch sheet SHEET "$key_a" $(shot sheet)
pid_a=$launched
sleep 2
launch peer PEER "$key_b" -nullrhi
pid_b=$launched
code=0; wait "$pid_a" || code=$?; check sheet SHEET "$code"
code=0; wait "$pid_b" || code=$?; check peer PEER "$code"
sleep 1
stop_server

store_check=0
"$repo_root/scripts/mb2.sh" store-check sheet "$store" "$key_a" "$key_b" "$key_c" 1 > "$out_dir/store-check-1.txt" 2>&1 || store_check=$?
sed 's/^/  store 1: /' "$out_dir/store-check-1.txt"
[[ "$store_check" -eq 0 ]] || status=1
migrated=$(grep -ac "MB2SHEET role=server event=sheets_loaded schema=6 attributes=2 equipment=0 legacy_aliases=2 conflicts=1 repaired=0 reconciled=0" "$out_dir/server-1.log" || true)
committed=$(grep -ac "MB2SHEET role=server event=sheet_migration_committed from_schema=6 revision=2 " "$out_dir/server-1.log" || true)
decisions=$(grep -a "MB2SHEET role=server event=equip character=$key_a " "$out_dir/server-1.log" | sed 's/.* slot=\([a-z_-]*\) item=\([a-z_-]*\) .* decision=\([a-z_]*\).*/\1:\2:\3/' \
    | tr '\n' ' ' || true)
# The replay is answered from the stored receipt before the equipment rule runs, so it has no decision line.
want_decisions="right_hand:fishing_rod:item_equipped left_hand:fishing_rod:insufficient_items head:camera:not_equippable \
-:camera:unknown_slot right_hand:unknown_item:not_equippable bag:-:nothing_to_do left_hand:camera:item_equipped "
peer_decisions=$(grep -ac "MB2SHEET role=server event=equip character=$key_b slot=right_hand item=fishing_rod .* decision=item_equipped" "$out_dir/server-1.log" || true)
echo "  server phase 1: sheets_loaded(legacy 2, conflict 1)=$migrated (want 1); migration committed at revision 2=$committed (want 1); peer equipped=$peer_decisions (want 1)"
echo "  decisions: [ $decisions]"
[[ "$migrated" -eq 1 && "$committed" -eq 1 && "$peer_decisions" -eq 1 && "$decisions" == "$want_decisions" ]] || { echo "  want:      [ $want_decisions]"; status=1; }

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"
# shellcheck disable=SC2046
launch restored RESTORED "$key_a" $(shot restored)
code=0; wait "$launched" || code=$?; check restored RESTORED "$code"
sleep 1
stop_server
store_check=0
"$repo_root/scripts/mb2.sh" store-check sheet "$store" "$key_a" "$key_b" "$key_c" 2 > "$out_dir/store-check-2.txt" 2>&1 || store_check=$?
sed 's/^/  store 2: /' "$out_dir/store-check-2.txt"
[[ "$store_check" -eq 0 ]] || status=1
clean=$(grep -ac "MB2SHEET role=server event=sheets_loaded schema=7 attributes=2 equipment=2 legacy_aliases=0 conflicts=0 repaired=0 reconciled=0" "$out_dir/server-2.log" || true)
no_commit=$(grep -ac "MB2SHEET role=server event=sheet_migration_committed" "$out_dir/server-2.log" || true)
echo "  server phase 2: clean schema 7 load=$clean (want 1); migration commits=$no_commit (want 0)"
[[ "$clean" -eq 1 && "$no_commit" -eq 0 ]] || status=1

{
    echo "MarburgumII character sheet smoke (ADR-085)"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content Data || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "characters: $key_a $key_b $key_c"
    echo "decisions: $decisions"
    echo "store_sha256: $(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
    echo
    sed 's/^/store 1: /' "$out_dir/store-check-1.txt"
    sed 's/^/store 2: /' "$out_dir/store-check-2.txt"
    echo
    grep -ah "MB2SHEET\|MB2INTERACT role=server event=store_loaded" "$out_dir"/*.log | sed 's/^.*\(MB2SHEET\|MB2INTERACT\)/\1/'
} > "$out_dir/summary.txt"
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
[[ "$mode" == staged ]] && { "$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt" 2>&1 || echo "WARNING: the staged build is not fresh; see $out_dir/summary.txt" >&2; }

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
