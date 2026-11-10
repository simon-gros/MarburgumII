#!/usr/bin/env bash
# Gate 1 qualification: one authoritative interaction, durable across a server restart.
#
# Phase 1 (fresh world store, one injected disk-write fault):
#   A  write fault refused with no effect, grant, replay of the same operation id (Duplicate),
#      out-of-range request (refused), second grant, then races B for the last unit.
#   B  requests the last unit as soon as it sees one left.
# The server is stopped and the store file checked: 3 grants, stock 0, one receipt per grant.
# Phase 2 (same store, server restarted):
#   A2 inventory and stock restored, A's first operation replayed (Duplicate), a new request
#      refused out of stock; stays online while D tries to log in as the same character.
#   A3 the same character after A2 logged out: admitted again, restored inventory on screen.
#   B2 inventory restored.   N  no character key: admitted, interaction refused.
#   D  A's key while A2 is online: refused at admission.   X  malformed key: refused.
# The store must be byte-identical after phase 2: duplicates and refusals never write.
#
# A, D and A3 render offscreen and save screenshots, one renderer at a time in phase 2 (a second
# offscreen Vulkan client could not create its device while another was rendering on the test host).
# Usage: scripts/interaction-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/InteractSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_INTERACT_PORT:-7777}"
map="/Game/Maps/NetSmoke"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/InteractSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_a="mb2a-$token"
key_b="mb2b-$token"
first_op="$(python3 -c 'import uuid; print(uuid.uuid4().hex.upper())')"

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
hold_file="$out_dir/release-a2"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
server_pid=""
status=0

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

start_server() {
    local log="$1"
    shift
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_INTERACT_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" "${common_args[@]}" "$@" > "$log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 240); do
        grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log" && return 0
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            exit 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port within 120 s; see $log" >&2
    exit 1
}

# run_client <role> <character key or ""> <extra args...>; output to client-<role>.log
run_client() {
    local role="$1" key="$2"
    shift 2
    local url="127.0.0.1:$port"
    [[ -n "$key" ]] && url="$url?MB2Character=$key"
    local log="$out_dir/client-$role.log" code=0
    "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" \
        -MB2InteractSmoke -MB2InteractRole="$role" -MB2InteractOp="$first_op" -MB2InteractTimeout=150 \
        > "$log" 2>&1 || code=$?
    # An offscreen renderer occasionally fails to create its Vulkan device while another process is
    # initialising or rendering. That happens before any network traffic, so retrying is safe; each
    # failed log is kept as evidence.
    local attempt
    for attempt in 1 2; do
        [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
        mv "$log" "$out_dir/client-$role.vulkan-init-failed-$attempt.log"
        echo "  client $role: Vulkan device creation failed before connecting; retry $attempt in 3 s"
        sleep 3
        code=0
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" \
            -MB2InteractSmoke -MB2InteractRole="$role" -MB2InteractOp="$first_op" -MB2InteractTimeout=150 \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2INTERACT role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2INTERACT role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2INTERACT/    MB2INTERACT/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2InteractShot=$out_dir/$1"; }

# ---------------------------------------------------------------- phase 1
echo "Phase 1: dedicated server ($mode) on port $port, fresh store; evidence in $out_dir"
echo 1 > "$store.fail"
start_server "$out_dir/server-1.log" -MB2StoreFaultInjection

run_client B "$key_b" -nullrhi &
pid_b=$!
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client A "$key_a" $(shot interact-a-session1) &
pid_a=$!
code=0; wait "$pid_a" || code=$?; check_client A "$code"
code=0; wait "$pid_b" || code=$?; check_client B "$code"
sleep 1
stop_server

# The durable record after phase 1: three grants in total, nothing else.
store_check=0
python3 -I - "$store" "$key_a" "$key_b" "$first_op" > "$out_dir/store-check.txt" <<'EOF' || store_check=$?
import json, sys
path, key_a, key_b, first_op = sys.argv[1:5]
doc = json.load(open(path))
inv = doc["inventories"]
a = inv.get(key_a, {}).get("fishing_rod", 0)
b = inv.get(key_b, {}).get("fishing_rod", 0)
problems = []
if doc["format"] != "marburgumii.world_store" or doc["schema_version"] != 1: problems.append("format")
if doc["revision"] != 3: problems.append("revision=%s" % doc["revision"])
if doc["stocks"].get("dev_supply_crate") != 0: problems.append("stock=%s" % doc["stocks"].get("dev_supply_crate"))
if "dev_supply_crate_far" in doc["stocks"]: problems.append("far_stock_written")
if a + b != 3: problems.append("granted=%d" % (a + b))
if len(doc["receipts"]) != 3: problems.append("receipts=%d" % len(doc["receipts"]))
if doc["receipts"].get(first_op, {}).get("character") != key_a: problems.append("first_op_receipt")
if problems:
    print("store check FAILED: " + ",".join(problems), file=sys.stderr)
print(a, b, doc["revision"])
sys.exit(1 if problems else 0)
EOF
[[ "$store_check" -eq 0 ]] || status=1
read -r items_a items_b revision < "$out_dir/store-check.txt" || true
items_a="${items_a:-0}"; items_b="${items_b:-0}"; revision="${revision:-none}"
store_hash="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
fault_left="$(tr -d '[:space:]' < "$store.fail" 2>/dev/null || true)"
echo "  store after phase 1: revision=$revision items A=$items_a B=$items_b (sum must be 3), injected faults left=$fault_left"
[[ "$fault_left" == "0" ]] || status=1
applied_1=$(grep -ac "MB2INTERACT role=server event=interaction .* outcome=applied" "$out_dir/server-1.log" || true)
commit_failed=$(grep -ac "MB2INTERACT role=server event=commit_failed" "$out_dir/server-1.log" || true)
echo "  server phase 1: applied=$applied_1 (want 3) commit_failed=$commit_failed (want 1)"
[[ "$applied_1" -eq 3 && "$commit_failed" -eq 1 ]] || status=1

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"

run_client A2 "$key_a" -nullrhi -MB2InteractExpect="$items_a" -MB2InteractHoldFile="$hold_file" &
pid_a2=$!
run_client B2 "$key_b" -nullrhi -MB2InteractExpect="$items_b" &
pid_b2=$!
run_client N "" -nullrhi &
pid_n=$!

for _ in $(seq 1 240); do
    grep -aq "MB2INTERACT role=client tag=A2 event=holding" "$out_dir/client-A2.log" 2>/dev/null && break
    kill -0 "$pid_a2" 2>/dev/null || break
    sleep 0.5
done
code=0; run_client X "x" -nullrhi || code=$?; check_client X "$code"
code=0
# shellcheck disable=SC2046
run_client D "$key_a" $(shot interact-d-duplicate-login) || code=$?
check_client D "$code"
touch "$hold_file"
code=0; wait "$pid_a2" || code=$?; check_client A2 "$code"
code=0; wait "$pid_b2" || code=$?; check_client B2 "$code"
code=0; wait "$pid_n" || code=$?; check_client N "$code"
# The character is free again once A2 has logged out; a new session shows the restored custody.
code=0
# shellcheck disable=SC2046
run_client A3 "$key_a" $(shot interact-a-session3) -MB2InteractExpect="$items_a" || code=$?
check_client A3 "$code"
sleep 1
stop_server

refused=$(grep -ac "MB2INTERACT role=server event=admission_refused" "$out_dir/server-2.log" || true)
applied_2=$(grep -ac "MB2INTERACT role=server event=interaction .* outcome=applied" "$out_dir/server-2.log" || true)
loaded=$(grep -ac "MB2INTERACT role=server event=store_loaded .* revision=3 " "$out_dir/server-2.log" || true)
store_hash_2="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
echo "  server phase 2: store_loaded(revision 3)=$loaded admission_refused=$refused (want 2) applied=$applied_2 (want 0)"
[[ "$loaded" -eq 1 && "$refused" -eq 2 && "$applied_2" -eq 0 ]] || status=1
if [[ "$store_hash" == "$store_hash_2" ]]; then
    echo "  store unchanged by phase 2: yes"
else
    echo "  store unchanged by phase 2: NO"
    status=1
fi

{
    echo "MarburgumII Gate 1 interaction smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "host: $(uname -srm)"
    echo "store_after_phase1: revision=$revision items_a=$items_a items_b=$items_b sha256=$store_hash"
    echo "store_after_phase2_sha256: $store_hash_2"
    echo "server_phase1: applied=$applied_1 commit_failed=$commit_failed"
    echo "server_phase2: store_loaded=$loaded admission_refused=$refused applied=$applied_2"
    echo
    grep -ah "MB2INTERACT\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2INTERACT\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
