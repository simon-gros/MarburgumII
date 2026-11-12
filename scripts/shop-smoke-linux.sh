#!/usr/bin/env bash
# Kiosk 67, Petra Kralj's counter (ADR-060), end to end on /Game/World/lent.
#
# The world store is seeded as a schema 4 document (revision 1): one character with 50 money and a brass key, as a
# character that had been paid for a mission would have. The counters' clock is set with -MB2ClockHour and
# -MB2ClockWeekday, so the run does not depend on the hour it is started.
#
# Phase 1, Thursday 10:00, the counter open:
#   BUY     at Petra's development start, through the shop panel: the starting shelves; a newspaper bought (-20,
#           shelf 11, Trading +10 for the first purchase); that operation replayed (duplicate, nothing moves); a
#           postcard it cannot afford, six newspapers and the brass key refused; the newspaper sold back (+8, shelf
#           12 again); selling it twice refused; a pen bought (-15, no XP).
#   FAR     from the arrival start, a purchase at Petra is refused as out of range.
# The server is stopped and the store checked (mb2 store-check shop): rewritten at schema 5, three commits.
# Phase 2, Sunday 12:00, same store: CLOSED sees money, pen, XP and shelves as left, the counter shut, and a purchase
# refused as shop_closed. The store must be byte-identical afterwards.
#
# BUY and CLOSED render offscreen (screenshots); FAR runs -nullrhi.
# Usage: scripts/shop-smoke-linux.sh [staged|editor]. Evidence: Saved/ShopSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_SHOP_PORT:-7791}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/ShopSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
key="mb2k-t$(date +%s)$RANDOM"

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

# start_server <log> <clock args...>
start_server() {
    local log="$1"
    shift
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_SHOP_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" "$@" "${common_args[@]}" > "$log" 2>&1 &
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
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" -MB2ShopSmoke -MB2ShopRole="$role" -MB2ShopTimeout=120 \
            > "$log" 2>&1 || code=$?
    done
    if [[ "$code" -eq 0 ]] && grep -aq "MB2SHOP role=client tag=$role event=pass" "$log"; then
        echo "  client $label: PASS"
    else
        echo "  client $label: FAIL (exit $code)"
        grep -ah "MB2SHOP role=client tag=$role event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2SHOP/    MB2SHOP/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2ShopShot=$out_dir/$1"; }

# ---------------------------------------------------------------- phase 1
printf '{"format":"marburgumii.world_store","schema_version":4,"revision":1,"stocks":{},"inventories":{"%s":{"brass_key":1}},"missions":{},"conditions":{},"skills":{},"money":{"%s":50},"receipts":{}}\n' \
    "$key" "$key" > "$store"
echo "Phase 1: dedicated server ($mode) on $map, port $port, Thursday 10:00, seeded store; evidence in $out_dir"
start_server "$out_dir/server-1.log" -MB2ClockWeekday=3 -MB2ClockHour=10
for _ in $(seq 1 80); do
    grep -aq 'MB2SHOP role=server event=counter' "$out_dir/server-1.log" && break
    sleep 0.25
done

# shellcheck disable=SC2046  # word splitting of the argument list is intended
run buy BUY mission_petra_kralj $(shot buy)
run far FAR arrival -nullrhi
sleep 1
stop_server

store_check=0
first_op=$(grep -ah "MB2SHOP role=client tag=BUY event=receipt .* effect=item_bought item=mariborski_glas" "$out_dir/client-buy.log" \
    | head -1 | sed 's/.* op=\([0-9A-F]*\) .*/\1/' || true)
"$repo_root/scripts/mb2.sh" store-check shop "$store" "$key" "${first_op:-none}" > "$out_dir/store-check.txt" 2>&1 || store_check=$?
sed 's/^/  store: /' "$out_dir/store-check.txt"
[[ "$store_check" -eq 0 ]] || status=1
loaded_1=$(grep -ac "MB2INTERACT role=server event=store_loaded .* schema=4 revision=1 " "$out_dir/server-1.log" || true)
trades=$(grep -a "MB2SHOP role=server event=trade " "$out_dir/server-1.log" | sed 's/.* action=\([a-z]*\) item=\([a-z_]*\) quantity=\([0-9-]*\) .* decision=\([a-z_]*\).*/\1:\2:\3:\4/' \
    | tr '\n' ' ' || true)
# The replay is answered from the stored receipt before the counter rule runs, so it has no trade line.
want_trades="buy:mariborski_glas:1:item_bought buy:maribor_postcard:1:insufficient_money buy:mariborski_glas:6:invalid_quantity \
sell:brass_key:1:item_not_accepted sell:mariborski_glas:1:item_sold sell:mariborski_glas:1:insufficient_items buy:ballpoint_pen:1:item_bought "
far_refused=$(grep -ac "MB2INTERACT role=server event=interaction .* reason=out_of_range" "$out_dir/server-1.log" || true)
echo "  server phase 1: store_loaded(schema 4)=$loaded_1 (want 1); out_of_range=$far_refused (want 1)"
echo "  trades: [ $trades]"
[[ "$loaded_1" -eq 1 && "$far_refused" -eq 1 && "$trades" == "$want_trades" ]] || { echo "  want:   [ $want_trades]"; status=1; }
store_hash="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store, Sunday 12:00"
start_server "$out_dir/server-2.log" -MB2ClockWeekday=6 -MB2ClockHour=12
# shellcheck disable=SC2046
run closed CLOSED mission_petra_kralj $(shot closed)
sleep 1
stop_server
loaded_2=$(grep -ac "MB2INTERACT role=server event=store_loaded .* schema=5 revision=4 " "$out_dir/server-2.log" || true)
closed=$(grep -ac "MB2SHOP role=server event=trade .* decision=shop_closed" "$out_dir/server-2.log" || true)
store_hash_2="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
echo "  server phase 2: store_loaded(schema 5, revision 4)=$loaded_2 (want 1); shop_closed=$closed (want 1); store unchanged: $([[ "$store_hash" == "$store_hash_2" ]] && echo yes || echo NO)"
[[ "$loaded_2" -eq 1 && "$closed" -eq 1 && "$store_hash" == "$store_hash_2" ]] || status=1

{
    echo "MarburgumII Kiosk 67 counter smoke (ADR-060)"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content Data || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "character: $key"
    echo "trades: $trades"
    echo "store_sha256: $store_hash"
    echo
    sed 's/^/store: /' "$out_dir/store-check.txt"
    echo
    grep -ah "MB2SHOP\|MB2INTERACT role=server event=\(interaction\|store_loaded\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2SHOP\|MB2INTERACT\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
