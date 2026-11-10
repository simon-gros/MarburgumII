#!/usr/bin/env bash
# Stage 1 Lent slice qualification: location, an NPC, a saved mission and the HUD that shows them,
# on /Game/Maps/Lent, durable across a server restart.
#
# Phase 1 (fresh world store, one injected disk-write fault):
#   M  starts at the angler: arrives in Lent (location banner), a refused faulty commit leaves no
#      mission and no item, the mission is accepted with its start item in one commit, the same
#      operation replayed is a Duplicate, the next step is refused for want of a fish, journal shown.
#      Stays online for W.
#   W  starts on Stari most, outside Lent: talking to the angler from there is refused out of range;
#      once M's mission is saved, W's copy of M's player state must carry none of M's progress,
#      inventory or location; then W walks north until the server places it in Lent.
#   K  no character key: talking is refused.
# The server is stopped and the store file checked: schema 2, one commit (the mission start).
# Phase 2 (same store, server restarted):
#   M2 M's character: mission, pinned objective and start item restored; the replay is a Duplicate.
#   T  a new character walks from the bridge into Lent: the location banner appears.
# The store must be byte-identical after phase 2: duplicates, refusals and walking never write.
#
# M, M2 and T render offscreen one at a time (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/lent-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/LentSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_LENT_PORT:-7777}"
map="/Game/Maps/Lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/LentSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_m="mb2m-$token"
key_w="mb2w-$token"
key_t="mb2t-$token"
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
signal_file="$out_dir/m-mission-saved"
hold_file="$out_dir/release-m"
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
    shift
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_LENT_PORT." >&2
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

# run_client <role> <character key or ""> <start tag> <extra args...>; output to client-<role>.log
run_client() {
    local role="$1" key="$2" start="$3"
    shift 3
    local url="127.0.0.1:$port?MB2Start=$start"
    [[ -n "$key" ]] && url="$url?MB2Character=$key"
    local log="$out_dir/client-$role.log" code=0 attempt
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            # A renderer that could not create its Vulkan device never connected; retrying is safe.
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$role.vulkan-init-failed-$attempt.log"
            echo "  client $role: Vulkan device creation failed before connecting; retry $attempt in 3 s"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "$url" "${common_args[@]}" "$@" \
            -MB2LentSmoke -MB2LentRole="$role" -MB2LentOp="$first_op" -MB2LentTimeout=150 \
            -MB2LentSignal="$signal_file" -MB2LentHoldFile="$hold_file" \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2LENT role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2LENT role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2LENT/    MB2LENT/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2LentShot=$out_dir/$1"; }

# ---------------------------------------------------------------- phase 1
echo "Phase 1: dedicated server ($mode) on $map, port $port, fresh store; evidence in $out_dir"
echo 1 > "$store.fail"
start_server "$out_dir/server-1.log" -MB2StoreFaultInjection

run_client W "$key_w" bridge -nullrhi &
pid_w=$!
run_client K "" angler -nullrhi &
pid_k=$!
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client M "$key_m" angler $(shot lent-m) &
pid_m=$!
code=0; wait "$pid_w" || code=$?; check_client W "$code"
code=0; wait "$pid_k" || code=$?; check_client K "$code"
touch "$hold_file"
code=0; wait "$pid_m" || code=$?; check_client M "$code"
sleep 1
stop_server

store_check=0
python3 -I - "$store" "$key_m" "$key_w" "$first_op" > "$out_dir/store-check.txt" <<'EOF' || store_check=$?
import json, sys
path, key_m, key_w, first_op = sys.argv[1:5]
doc = json.load(open(path))
problems = []
if doc["format"] != "marburgumii.world_store" or doc["schema_version"] != 2: problems.append("format")
if doc["revision"] != 1: problems.append("revision=%s" % doc["revision"])
mission = doc.get("missions", {}).get(key_m, {}).get("lent_first_catch", {})
if mission.get("stage") != "catch_first_fish" or mission.get("started_revision") != 1: problems.append("mission=%s" % mission)
if doc["inventories"].get(key_m, {}).get("fishing_rod") != 1: problems.append("rods=%s" % doc["inventories"].get(key_m))
if key_w in doc["inventories"] or key_w in doc.get("missions", {}): problems.append("walker_written")
receipt = doc["receipts"].get(first_op, {})
if len(doc["receipts"]) != 1 or receipt.get("kind") != "mission_started" or receipt.get("character") != key_m \
        or receipt.get("item") != "fishing_rod" or receipt.get("mission") != "lent_first_catch":
    problems.append("receipts=%s" % doc["receipts"])
if problems:
    print("store check FAILED: " + ",".join(problems), file=sys.stderr)
print(doc["revision"], mission.get("stage", "-"))
sys.exit(1 if problems else 0)
EOF
[[ "$store_check" -eq 0 ]] || status=1
read -r revision stage < "$out_dir/store-check.txt" || true
store_hash="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
fault_left="$(tr -d '[:space:]' < "$store.fail" 2>/dev/null || true)"
echo "  store after phase 1: schema 2, revision=${revision:-none} stage=${stage:-none} (want 1, catch_first_fish), injected faults left=$fault_left"
[[ "$fault_left" == "0" ]] || status=1
applied_1=$(grep -ac "MB2INTERACT role=server event=interaction .* outcome=applied" "$out_dir/server-1.log" || true)
commit_failed=$(grep -ac "MB2INTERACT role=server event=commit_failed" "$out_dir/server-1.log" || true)
# FName keeps the first casing it saw, so the area id may print as "Lent".
entered=$(grep -aci "MB2INTERACT role=server event=area .* character=$key_w area=lent" "$out_dir/server-1.log" || true)
echo "  server phase 1: applied=$applied_1 (want 1) commit_failed=$commit_failed (want 1) walker_entered_lent=$entered (want 1)"
[[ "$applied_1" -eq 1 && "$commit_failed" -eq 1 && "$entered" -eq 1 ]] || status=1

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"
code=0
# shellcheck disable=SC2046
run_client M2 "$key_m" arrival $(shot lent-m2) || code=$?
check_client M2 "$code"
code=0
# shellcheck disable=SC2046
run_client T "$key_t" bridge $(shot lent-t) || code=$?
check_client T "$code"
sleep 1
stop_server

applied_2=$(grep -ac "MB2INTERACT role=server event=interaction .* outcome=applied" "$out_dir/server-2.log" || true)
loaded=$(grep -ac "MB2INTERACT role=server event=store_loaded .* schema=2 revision=1 " "$out_dir/server-2.log" || true)
store_hash_2="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"
echo "  server phase 2: store_loaded(schema 2, revision 1)=$loaded applied=$applied_2 (want 0)"
[[ "$loaded" -eq 1 && "$applied_2" -eq 0 ]] || status=1
if [[ "$store_hash" == "$store_hash_2" ]]; then
    echo "  store unchanged by phase 2: yes"
else
    echo "  store unchanged by phase 2: NO"
    status=1
fi

{
    echo "MarburgumII Stage 1 Lent slice smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "store_after_phase1: revision=${revision:-none} stage=${stage:-none} sha256=$store_hash"
    echo "store_after_phase2_sha256: $store_hash_2"
    echo "server_phase1: applied=$applied_1 commit_failed=$commit_failed walker_entered_lent=$entered"
    echo "server_phase2: store_loaded=$loaded applied=$applied_2"
    echo
    grep -ah "MB2LENT\|MB2INTERACT\|MB2SMOKE role=server event=\(ready\|login\|logout\)" "$out_dir"/*.log \
        | sed 's/^.*\(MB2LENT\|MB2INTERACT\|MB2SMOKE\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
