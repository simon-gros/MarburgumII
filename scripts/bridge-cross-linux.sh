#!/usr/bin/env bash
# One server-owned pedestrian walks an existing bridge route across the Drava (ADR-075 follow-up).
# Editor only. Port: MB2_BRIDGE_PORT (default 27851). Never 7777 or 17777.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
port="${MB2_BRIDGE_PORT:-27851}"
if [[ "$port" == "7777" || "$port" == "17777" ]]; then
    echo "Refusing port $port" >&2
    exit 2
fi
if [[ -z "${UE_ROOT:-}" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi
"$repo_root/scripts/check-engine-version.sh" "$UE_ROOT"

out_dir="$repo_root/Saved/BridgeCross/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out_dir"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
editor="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
common=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
server_log="$out_dir/server.log"
client_log="$out_dir/client.log"

if ss -Hlun "sport = :$port" | grep -aq .; then
    echo "UDP port $port is already in use" >&2
    exit 1
fi

setsid "$editor" "$uproject" "$map" -server -port="$port" -MB2BridgeCross -MB2StreetPlaces=0 \
    -MB2WalkGraphAudit="$out_dir/audit" "${common[@]}" > "$server_log" 2>&1 &
server_pid=$!
cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" 2>/dev/null || true
        sleep 1
        kill -9 "$server_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for _ in $(seq 1 180); do
    if grep -aq 'event=street_ready' "$server_log"; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "Server exited before the street was ready" >&2
        exit 1
    fi
    sleep 1
done
if ! grep -aq 'event=street_ready' "$server_log"; then
    echo "Street was not ready within 180s" >&2
    exit 1
fi

timeout --kill-after=15 260 "$editor" "$uproject" "127.0.0.1:$port?MB2Character=mb2bridge$RANDOM" \
    -game -MB2BridgeWatch -MB2StreetShot="$out_dir/bridge" -MB2StreetTimeout=240 \
    -RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080 "${common[@]}" > "$client_log" 2>&1 || true

# The crossing can still be in progress when the client has its shot. Wait for the server result.
for _ in $(seq 1 120); do
    if grep -aq 'event=bridge_cross result=\(arrived\|wet\|lost\|timeout\|no_bridge\|no_route\|no_spawn\)' "$server_log"; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        break
    fi
    sleep 1
done

grep -a 'event=bridge_cross ' "$server_log" | tail -20 || true
grep -a 'event=bridge_watch ' "$client_log" | tail -20 || true
if grep -aq 'event=bridge_cross result=arrived' "$server_log"; then
    echo "BRIDGE_CROSS arrived"
    exit 0
fi
echo "BRIDGE_CROSS failed" >&2
exit 1
