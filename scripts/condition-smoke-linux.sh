#!/usr/bin/env bash
# Stage 1 player-condition qualification (ADR-020): running costs breath, a run that empties the bar
# leaves the body winded, the server decides both, and the condition survives leaving and a restart.
# All clients start on the Lent promenade and run east.
#
# Phase 1 (fresh world store):
#   R  a new character: rested on the server, in its prediction, the HUD and the Character panel; runs
#      at run speed, is winded after about ten seconds and walks while still asking to run, runs again
#      once breath is back, and leaves the moment the server has it winded a second time.
#   C  the adversary (-nullrhi): ignores its breath and claims a full bar on every move. The server must
#      wind it after about ten seconds anyway, correct it and hold it to walking speed while winded.
# The server must save R's and C's condition at logout (two commits) and log C winded. Stopped, the store
#   is checked: R winded with almost no stamina, C tired, no custody written, revision 2.
# Phase 2 (same store, server restarted):
#   R2 R's character: restored winded and asking to run at once. The server recovers its breath from
#      login on, so the script checks the server log: R's breath came back with no running at all. R2
#      then runs once breath is back and rests to full; a rested body is saved as no entry at all.
#   S  a runner (-nullrhi) still connected when the script stops the server: the stop must save it.
# The store is checked again: R gone, C kept, S tired, revision 4.
#
# R and R2 render offscreen one at a time (see interaction-smoke-linux.sh for the Vulkan note).
# Usage: scripts/condition-smoke-linux.sh [staged|editor]
# MB2_CONDITION_SERVER_ARGS adds server arguments. Evidence is written to Saved/ConditionSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_CONDITION_PORT:-7777}"
map="/Game/Maps/Lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/ConditionSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"
key_r="mb2r-$token"
key_c="mb2c-$token"
key_s="mb2s-$token"

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
signal_file="$out_dir/s-running"
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
server_pid=""
status=0
read -r -a extra_server_args <<< "${MB2_CONDITION_SERVER_ARGS:-}"

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
        echo "UDP port $port is already in use; stop the other server or set MB2_CONDITION_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$store" "${common_args[@]}" "$@" \
        ${extra_server_args[@]+"${extra_server_args[@]}"} > "$log" 2>&1 &
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

# run_client <role> <character key> <extra args...>; output to client-<role>.log
run_client() {
    local role="$1" key="$2"
    shift 2
    local url="127.0.0.1:$port?MB2Start=arrival?MB2Character=$key"
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
            -MB2ConditionSmoke -MB2ConditionRole="$role" -MB2ConditionTimeout=90 -MB2ConditionSignal="$signal_file" \
            > "$log" 2>&1 || code=$?
    done
    return "$code"
}

check_client() {
    local role="$1" code="$2"
    if [[ "$code" -eq 0 ]] && grep -aq "MB2COND role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2COND role=client tag=$role event=\(fail\|expectation_failed\)" "$out_dir/client-$role.log" \
            | sed 's/^.*MB2COND/    MB2COND/' || true
        status=1
    fi
}

shot() { echo "${render_args[@]}" "-MB2ConditionShot=$out_dir/$1"; }

# expect_log <server log> <label> <want: number or ">=N"> <pattern>
expect_log() {
    local log="$1" label="$2" want="$3" pattern="$4" count
    count=$(grep -ac -- "$pattern" "$log" || true)
    echo "  server: $label=$count (want $want)"
    if [[ "$want" == ">="* ]]; then
        [[ "$count" -ge "${want#>=}" ]] || status=1
    else
        [[ "$count" -eq "$want" ]] || status=1
    fi
}

# ---------------------------------------------------------------- phase 1
echo "Phase 1: dedicated server ($mode) on $map, port $port, fresh store; evidence in $out_dir"
start_server "$out_dir/server-1.log"
code=0
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client R "$key_r" $(shot condition-r) || code=$?
check_client R "$code"
code=0
run_client C "$key_c" -nullrhi || code=$?
check_client C "$code"
sleep 1
stop_server
log1="$out_dir/server-1.log"
expect_log "$log1" "runner_restored_rested" 1 "MB2COND role=server event=condition_restored .* character=$key_r source=rested "
expect_log "$log1" "runner_winded" 2 "MB2COND role=server event=winded .* character=$key_r "
expect_log "$log1" "runner_saved_winded" 1 "MB2COND role=server event=condition_saved why=logout character=$key_r .* winded=1 "
expect_log "$log1" "cheater_winded" ">=1" "MB2COND role=server event=winded .* character=$key_c "
expect_log "$log1" "cheater_saved" 1 "MB2COND role=server event=condition_saved why=logout character=$key_c "
expect_log "$log1" "save_failures" 0 "MB2COND role=server event=condition_save_failed"

store_check=0
"$repo_root/scripts/mb2.sh" store-check condition "$store" "$key_r" "$key_c" "$key_s" 1 > "$out_dir/store-check-1.txt" || store_check=$?
[[ "$store_check" -eq 0 ]] || status=1
read -r revision_1 stamina_1 winded_1 < "$out_dir/store-check-1.txt" || true
echo "  store after phase 1: revision=${revision_1:-none} R stamina=${stamina_1:-none} winded=${winded_1:-none} (want 2, <=5, 1)"
cp "$store" "$out_dir/world-store-after-phase-1.json" 2>/dev/null || true

# ---------------------------------------------------------------- phase 2
echo "Phase 2: restart the server on the same store"
start_server "$out_dir/server-2.log"
code=0
# shellcheck disable=SC2046
run_client R2 "$key_r" $(shot condition-r2) || code=$?
check_client R2 "$code"
rm -f "$signal_file"
run_client S "$key_s" -nullrhi &
pid_s=$!
for _ in $(seq 1 120); do
    [[ -f "$signal_file" ]] && break
    kill -0 "$pid_s" 2>/dev/null || break
    sleep 0.25
done
if [[ -f "$signal_file" ]]; then
    echo "  S is running; stopping the server with S connected"
else
    echo "  S never signalled that it was running"
    status=1
fi
stop_server
code=0; wait "$pid_s" || code=$?; check_client S "$code"
log2="$out_dir/server-2.log"
expect_log "$log2" "runner_restored_from_store" 1 "MB2COND role=server event=condition_restored .* character=$key_r source=store .* winded=1"
# Asked to run from the moment it could, R2 got its breath back without the server running it once.
first_back=$(grep -a -m1 "MB2COND role=server event=breath_back .* character=$key_r " "$log2" || true)
echo "  server: runner first breath back: ${first_back##*event=breath_back } (want run_s=0.00)"
[[ "$first_back" == *" run_s=0.00"* ]] || status=1
expect_log "$log2" "runner_saved_rested" 1 "MB2COND role=server event=condition_saved why=logout character=$key_r vitality=100 stamina=100 winded=0 "
# A stop may close S's connection (logout) before the world tears down (shutdown); either saves it.
expect_log "$log2" "stopper_saved" 1 "MB2COND role=server event=condition_saved why=\(logout\|shutdown\) character=$key_s "
expect_log "$log2" "save_failures" 0 "MB2COND role=server event=condition_save_failed"

store_check=0
"$repo_root/scripts/mb2.sh" store-check condition "$store" "$key_r" "$key_c" "$key_s" 2 > "$out_dir/store-check-2.txt" || store_check=$?
[[ "$store_check" -eq 0 ]] || status=1
read -r revision_2 _ _ < "$out_dir/store-check-2.txt" || true
echo "  store after phase 2: revision=${revision_2:-none} (want 4; R removed, C kept, S saved)"
store_hash="$(sha256sum "$store" 2>/dev/null | cut -d' ' -f1 || true)"

{
    echo "MarburgumII Stage 1 player-condition smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "map: $map"
    echo "host: $(uname -srm)"
    echo "store_after_phase1: revision=${revision_1:-none} runner_stamina=${stamina_1:-none} runner_winded=${winded_1:-none}"
    echo "store_after_phase2: revision=${revision_2:-none} sha256=$store_hash"
    echo "server_args_extra: ${MB2_CONDITION_SERVER_ARGS:-none}"
    echo
    grep -ah "MB2COND\|MB2SMOKE role=server event=\(ready\|login\|logout\)\|MB2INTERACT role=server event=store_loaded" "$out_dir"/*.log \
        | sed 's/^.*\(MB2COND\|MB2SMOKE\|MB2INTERACT\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
