#!/usr/bin/env bash
# CHAT-001 three-client qualification: one dedicated server, three separate client processes.
#
#   A sends general chat, a private message to B, a private message to a nonexistent ID, invalid
#     text and a two-message burst, then an end marker and one operation submitted twice.
#   B replies privately to A.
#   C is the bystander: it must receive each general message once and nothing private.
#
# A and C render offscreen and save a screenshot of their chat panel; B runs without rendering.
# The script also checks that no private body reaches the server log or any client log.
#
# Usage: scripts/chat-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/ChatSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_CHAT_PORT:-7777}"
map="/Game/Maps/NetSmoke"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/ChatSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
token="t$(date +%s)$RANDOM"

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
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)
server_pid=""

# Same process-group handling as net-smoke-linux.sh: the staged launcher does not exec the binary.
cleanup() {
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
trap cleanup EXIT

if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use; stop the other server or set MB2_CHAT_PORT." >&2
    exit 1
fi

echo "Starting dedicated server ($mode) on port $port; evidence in $out_dir"
setsid "${server_cmd[@]}" "$map" -port="$port" "${common_args[@]}" > "$out_dir/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 1 240); do
    grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$out_dir/server.log" && break
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "Server exited before becoming ready; see $out_dir/server.log" >&2
        exit 1
    fi
    sleep 0.5
done
if ! grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$out_dir/server.log"; then
    echo "Server did not become ready on port $port within 120 s; see $out_dir/server.log" >&2
    exit 1
fi

run_client() {
    local role="$1"
    shift
    "${client_cmd[@]}" "127.0.0.1:$port" "${common_args[@]}" "$@" \
        -MB2ChatSmoke -MB2ChatRole="$role" -MB2ChatToken="$token" -MB2ChatTimeout=150 \
        > "$out_dir/client-$role.log" 2>&1
}

# MB2_CHAT_RENDER lists the roles that render and screenshot their view (default: A and C).
render_roles="${MB2_CHAT_RENDER:-A C}"
display_args() {
    local role="$1" label="$2"
    if [[ " $render_roles " == *" $role "* ]]; then
        echo "${render_args[@]}" "-MB2ChatShot=$out_dir/chat-$role-$label-1920x1080.png"
    else
        echo -nullrhi
    fi
}
# shellcheck disable=SC2046  # word splitting of the argument list is intended
run_client B $(display_args B recipient) &
pid_b=$!
# shellcheck disable=SC2046
run_client C $(display_args C bystander) &
pid_c=$!
# shellcheck disable=SC2046
run_client A $(display_args A sender) &
pid_a=$!

status=0
for role in A B C; do
    pid_var="pid_${role,,}"
    code=0
    wait "${!pid_var}" || code=$?
    if [[ "$code" -eq 0 ]] && grep -aq "MB2CHAT role=client tag=$role event=pass" "$out_dir/client-$role.log"; then
        echo "  client $role: PASS"
    else
        echo "  client $role: FAIL (exit $code)"
        grep -ah "MB2CHAT role=client tag=$role event=fail" "$out_dir/client-$role.log" | sed 's/^.*MB2CHAT/    MB2CHAT/' || true
        status=1
    fi
done

sleep 1
cleanup

# Five messages are accepted: G1, P1, R1, BURST1 and END. Refusals are not "accepted".
accepted=$(grep -ac "Marburgum chat accepted" "$out_dir/server.log" || true)
echo "Server: accepted=$accepted (want 5)"
[[ "$accepted" -eq 5 ]] || status=1

# Privacy: no private body anywhere in server or client logs.
leaks=$(grep -al -e "private-$token" -e "reply-$token" -e "ghost-$token" "$out_dir"/*.log || true)
if [[ -n "$leaks" ]]; then
    echo "  private text found in logs: $leaks"
    status=1
else
    echo "  private bodies in logs: none"
fi

{
    echo "MarburgumII CHAT-001 three-client chat smoke"
    echo "result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL)"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)$(git -C "$repo_root" diff --quiet HEAD -- Source Config Content || echo ' (dirty)')"
    echo "mode: $mode"
    echo "host: $(uname -srm)"
    echo "server_accepted_messages: $accepted"
    echo "private_bodies_in_logs: $([[ -n "$leaks" ]] && echo "$leaks" || echo none)"
    echo
    grep -ah "MB2CHAT\|MB2SMOKE role=server event=\(ready\|login\|logout\)\|Marburgum chat accepted" "$out_dir"/*.log \
        | sed 's/^.*\(MB2CHAT\|MB2SMOKE\|Marburgum chat accepted\)/\1/'
} > "$out_dir/summary.txt"

echo "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir/summary.txt"
exit "$status"
