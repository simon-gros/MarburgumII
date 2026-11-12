#!/usr/bin/env bash
# Accounts, the first character and world admission (ADR-055), end to end.
#
# Starts the account service (services/accounts) over HTTPS with a throwaway development CA, and a dedicated
# server that requires accounts. Then:
#   FIRST     a rendered client on the real screens: register, a wrong code, the code from the outbox, two
#             passwords that differ, username and password, every body in the creator's 3D preview (and one
#             turn), a refused name, the character (Špela Kovačič, Quinn); Connect enters the world as her,
#             wearing Quinn.
#   RETURN    a second rendered client: a wrong password, then the right one; no creator; the same character.
#   Admission, with a second account made through the HTTP API (curl):
#     a development key, no ticket and a forged ticket are refused;
#     a real ticket is admitted (ADMITTED, held in the world), the same ticket again is refused (single use),
#     and a second ticket for the same character is refused while the first is in the world.
#   The service restarts and the returning player still signs in (durable store).
#   Secrets: no password, code, session or ticket appears in the account store; no password or session in any
#   game log.
#
# Usage: scripts/account-smoke-linux.sh [editor|staged]. Evidence: Saved/AccountSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-editor}"
port="${MB2_ACCOUNT_PORT:-7791}"
service_port="${MB2_ACCOUNT_SERVICE_PORT:-7792}"
map="/Game/World/lent"
uproject="$repo_root/MarburgumII.uproject"
out_dir="$repo_root/Saved/AccountSmoke/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out_dir"

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
        echo "Usage: $0 [editor|staged]" >&2
        exit 2
        ;;
esac

# ---------------------------------------------------------------- the account service
secrets="$out_dir/secrets"
mkdir -p "$secrets"
chmod 700 "$secrets"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj "/CN=MB2 smoke CA" -keyout "$secrets/ca.key" -out "$out_dir/ca.pem" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=127.0.0.1" -keyout "$secrets/service.key" -out "$secrets/service.csr" 2>/dev/null
openssl x509 -req -in "$secrets/service.csr" -CA "$out_dir/ca.pem" -CAkey "$secrets/ca.key" -CAcreateserial -days 1 \
    -extfile <(printf "subjectAltName=IP:127.0.0.1,DNS:localhost\nextendedKeyUsage=serverAuth\n") -out "$out_dir/service.pem" 2>/dev/null
head -c 36 /dev/urandom | base64 | tr -d '\n/+=' > "$secrets/server.key"
chmod 600 "$secrets"/*

cargo build -q --release --manifest-path "$repo_root/services/accounts/Cargo.toml"
service_bin="$repo_root/services/accounts/target/release/mb2-accounts"
service_url="https://127.0.0.1:$service_port"
service_pid=""
server_pid=""
status=0

start_service() {
    local log="$1"
    "$service_bin" --bind "127.0.0.1:$service_port" --store "$out_dir/accounts.json" --outbox "$out_dir/outbox" \
        --models "$repo_root/Data/Accounts/character_models.v0.1.json" --server-key-file "$secrets/server.key" \
        --tls-cert "$out_dir/service.pem" --tls-key "$secrets/service.key" > "$log" 2>&1 &
    service_pid=$!
    for _ in $(seq 1 40); do
        curl -sf --cacert "$out_dir/ca.pem" "$service_url/v1/health" >/dev/null && return 0
        sleep 0.25
    done
    echo "The account service did not start; see $log" >&2
    exit 1
}

stop_service() {
    if [[ -n "$service_pid" ]]; then
        kill "$service_pid" 2>/dev/null || true
        wait "$service_pid" 2>/dev/null || true
    fi
    service_pid=""
}

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
trap 'stop_server; stop_service' EXIT

api() { # api <method> <path> <json> [bearer]
    local auth=()
    [[ -n "${4:-}" ]] && auth=(-H "Authorization: Bearer $4")
    curl -s --cacert "$out_dir/ca.pem" -X "$1" -H 'Content-Type: application/json' "${auth[@]}" -d "$3" "$service_url$2"
}

json() { grep -o "\"$1\":\"[^\"]*\"" | head -1 | cut -d'"' -f4; } # json <key>: the first string field of that name

code_for() { # the code in the newest outbox message to $1
    local file
    file=$(grep -l "^To: $1\$" "$out_dir"/outbox/*.txt | sort | tail -1)
    grep -E '^\s+[A-Z2-9]{8}\s*$' "$file" | tr -d ' '
}

tls_args=("-ini:Engine:[SSL]:OverrideCertificateBundlePath=$out_dir/ca.pem")
accounts_args=("-MB2Accounts=$service_url" "${tls_args[@]}")
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput)
render_args=(-RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080)

start_server() {
    local log="$1"
    if ss -Hlun "sport = :$port" | grep -aq .; then
        echo "UDP port $port is already in use; stop the other server or set MB2_ACCOUNT_PORT." >&2
        exit 1
    fi
    setsid "${server_cmd[@]}" "$map" -port="$port" -MB2WorldStore="$out_dir/world-store.json" "${accounts_args[@]}" \
        "-MB2AccountsKeyFile=$secrets/server.key" "${common_args[@]}" > "$log" 2>&1 &
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

# client <label> <role> <join url or ""> <args...>; output to client-<label>.log
client() {
    local label="$1" role="$2" url="$3"
    shift 3
    local log="$out_dir/client-$label.log" code=0 attempt
    local target=()
    [[ -n "$url" ]] && target=("$url")
    for attempt in 0 1 2; do
        if [[ "$attempt" -gt 0 ]]; then
            [[ "$code" -ne 0 ]] && grep -aq "Vulkan device creation failed" "$log" || break
            mv "$log" "$out_dir/client-$label.vulkan-init-failed-$attempt.log"
            sleep 3
            code=0
        fi
        "${client_cmd[@]}" "${target[@]}" "${common_args[@]}" "$@" -MB2AccountSmoke -MB2AccountRole="$role" > "$log" 2>&1 || code=$?
    done
    if [[ "$code" -eq 0 ]] && grep -aq "MB2ACCOUNT role=client tag=$role event=pass" "$log"; then
        echo "  client $label: PASS"
    else
        echo "  client $label: FAIL (exit $code)"
        grep -ah "MB2ACCOUNT role=client tag=$role event=\(fail\|expectation_failed\)" "$log" | sed 's/^.*MB2ACCOUNT/    MB2ACCOUNT/' || true
        status=1
    fi
}

expect_server() { # expect_server <what> <pattern>
    if grep -aq -- "$2" "$out_dir/server.log"; then
        echo "  server $1: yes"
    else
        echo "  server $1: NO (no '$2' in server.log)"
        status=1
    fi
}

printf 'drava-bridge-1990' > "$secrets/player.password"
chmod 600 "$secrets/player.password"
player=(-MB2AccountEmail=spela.kovacic@example.si -MB2AccountUser=spela_k "-MB2AccountPasswordFile=$secrets/player.password"
    -MB2AccountFirst=Špela -MB2AccountLast=Kovačič -MB2AccountModel=quinn)

echo "Account service on $service_url, dedicated server ($mode) on $map port $port; evidence in $out_dir"
start_service "$out_dir/service-1.log"
start_server "$out_dir/server.log"

echo "First launch and returning player (rendered)"
client first FIRST /Game/Maps/FrontEnd "${accounts_args[@]}" "-MB2Server=127.0.0.1:$port" "${render_args[@]}" "-MB2AccountShot=$out_dir/first" \
    "-MB2AccountOutbox=$out_dir/outbox" -MB2AccountTimeout=150 "${player[@]}"
expect_server "admitted the new character" 'MB2ACCOUNT role=server event=admitted .* model=quinn'
client return RETURN /Game/Maps/FrontEnd "${accounts_args[@]}" "-MB2Server=127.0.0.1:$port" "${render_args[@]}" "-MB2AccountShot=$out_dir/return" \
    -MB2AccountTimeout=150 "${player[@]}"
first_id=$(grep -aoh 'tag=FIRST event=character_ready character=c_[0-9a-f]*' "$out_dir/client-first.log" | sed 's/.*character=//')
return_id=$(grep -aoh 'tag=RETURN event=character_ready character=c_[0-9a-f]*' "$out_dir/client-return.log" | sed 's/.*character=//')
if [[ -n "$first_id" && "$first_id" == "$return_id" ]]; then
    echo "  same character on return: yes ($first_id)"
else
    echo "  same character on return: NO (first=$first_id return=$return_id)"
    status=1
fi

echo "Admission"
api POST /v1/register '{"email":"bor@example.si"}' >/dev/null
ticket=$(api POST /v1/verify "{\"email\":\"bor@example.si\",\"code\":\"$(code_for bor@example.si)\"}" | json ticket)
api POST /v1/setup "{\"ticket\":\"$ticket\",\"username\":\"bor_m\",\"password\":\"pohorje-snow-1991\"}" >/dev/null
session=$(api POST /v1/login '{"username":"bor_m","password":"pohorje-snow-1991"}' | json session)
api POST /v1/character '{"request":"r1","first_name":"Bor","last_name":"Mlakar","model":"manny"}' "$session" >/dev/null
forged=$(head -c 32 /dev/urandom | base64 | tr '+/' '-_' | tr -d '=')
nullrhi=(-nullrhi -MB2AccountTimeout=60)
client dev-key REFUSED "127.0.0.1:$port?MB2Character=dev-key-player" "${nullrhi[@]}"
expect_server "refused a development key" 'admission_refused stage=prelogin reason=development_key'
client no-ticket REFUSED "127.0.0.1:$port" "${nullrhi[@]}"
expect_server "refused a join without a ticket" 'admission_refused stage=prelogin reason=no_ticket'
client forged REFUSED "127.0.0.1:$port?MB2Ticket=$forged" "${nullrhi[@]}"
expect_server "refused a forged ticket" 'admission_refused stage=redeem reason="Your sign-in for this world has expired'
# Each ticket is asked for just before its join: a ticket lives one minute, about four client starts.
ticket_1=$(api POST /v1/admission '{}' "$session" | json ticket)
client admitted ADMITTED "127.0.0.1:$port?MB2Ticket=$ticket_1" "${nullrhi[@]}" -MB2AccountHold=40 -MB2AccountFirst=Bor \
    -MB2AccountLast=Mlakar -MB2AccountModel=manny &
admitted_pid=$!
for _ in $(seq 1 240); do
    grep -aq 'tag=ADMITTED event=in_world' "$out_dir/client-admitted.log" 2>/dev/null && break
    sleep 0.5
done
client replay REFUSED "127.0.0.1:$port?MB2Ticket=$ticket_1" "${nullrhi[@]}"
ticket_2=$(api POST /v1/admission '{}' "$session" | json ticket)
client twice REFUSED "127.0.0.1:$port?MB2Ticket=$ticket_2" "${nullrhi[@]}"
expect_server "refused the character a second time" 'admission_refused stage=redeem reason=already_in_world'
wait "$admitted_pid" || true
grep -aq 'tag=ADMITTED event=pass' "$out_dir/client-admitted.log" || status=1
replays=$(grep -ac 'admission_refused stage=redeem reason="Your sign-in for this world has expired' "$out_dir/server.log" || true)
if [[ "$replays" -ge 2 ]]; then
    echo "  server refused the spent ticket: yes"
else
    echo "  server refused the spent ticket: NO ($replays expired-ticket refusals, want 2: forged and replay)"
    status=1
fi

echo "Restart the account service"
stop_service
start_service "$out_dir/service-2.log"
relogin=$(api POST /v1/login '{"username":"spela_k","password":"drava-bridge-1990"}' | json id)
if [[ "$relogin" == "$first_id" ]]; then
    echo "  returning player after service restart: yes"
else
    echo "  returning player after service restart: NO ($relogin)"
    status=1
fi
stop_server

echo "Secrets"
leaks=0
for secret in drava-bridge-1990 pohorje-snow-1991 "$session" "$ticket_1" "$ticket_2"; do
    if grep -q -- "$secret" "$out_dir/accounts.json"; then
        echo "  account store holds a secret in the clear: ${secret:0:6}…"
        leaks=1
    fi
done
for secret in drava-bridge-1990 pohorje-snow-1991 "$session"; do
    if grep -aq -- "$secret" "$out_dir"/server.log "$out_dir"/client-*.log "$out_dir"/service-*.log; then
        echo "  a log holds a password or a session: ${secret:0:6}…"
        leaks=1
    fi
done
[[ "$leaks" -eq 0 ]] && echo "  none in the store or the logs" || status=1

if [[ "$status" -eq 0 ]]; then
    echo "Result: PASS — $out_dir"
else
    echo "Result: FAIL — $out_dir"
fi
exit "$status"
