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
#   Tickets name their server (QA 2026-11-19, AR1): the service lists this server and a second one (melje, own key,
#     not running). No ticket for an unlisted address or without one; melje's key cannot spend (or burn) this
#     server's ticket, which is then admitted; this server refuses a ticket asked for melje.
#   Encrypted transport (ADR-083): the ticket is the connection's encryption token and its key comes with it from
#     the service. A stolen ticket without its key is spent but admits nobody. The admitted client joins through a
#     recording UDP relay (mb2 wire-relay): the ticket shows in its hello, and nothing after it carries the map's
#     name, the game mode or the character's name in the clear. No server log holds a ticket or a key, and the
#     rendered clients never log the encryption token at all (AR13).
#   The service restarts and the returning player still signs in (durable store).
#   Secrets: no password, code, session, ticket or key appears in the account store; no password, session or
#   verification code in any game log.
#
# Usage: scripts/account-smoke-linux.sh [editor|staged]. Evidence: Saved/AccountSmoke/<UTC timestamp>/.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-editor}"
port="${MB2_ACCOUNT_PORT:-27714}"
service_port="${MB2_ACCOUNT_SERVICE_PORT:-27715}"
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
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under timeout, which stops its whole process group (the staged .sh does not exec the binary).
client_cmd=(timeout --kill-after=15 "${MB2_CLIENT_WALL_SECONDS:-3600}" "${client_cmd[@]}")

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
head -c 36 /dev/urandom | base64 | tr -d '\n/+=' > "$secrets/melje.key"
melje_address="127.0.0.1:$((port + 2))"
cat > "$secrets/servers.json" <<EOF_SERVERS
{ "servers": [
    { "id": "lent-smoke", "addresses": ["127.0.0.1:$port", "localhost:$port"], "key_file": "server.key" },
    { "id": "melje", "addresses": ["$melje_address"], "key_file": "melje.key" } ] }
EOF_SERVERS
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
        --models "$repo_root/Data/Accounts/character_models.v0.1.json" --servers "$secrets/servers.json" \
        --tls-cert "$out_dir/service.pem" --tls-key "$secrets/service.key" > "$log" 2>&1 &
    service_pid=$!
    for _ in $(seq 1 40); do
        curl -sf --cacert "$out_dir/ca.pem" "$service_url/v1/health" >/dev/null && return 0
        sleep 0.25
    done
    echo "The account service did not start; see $log" >&2
    exit 1
}

relay_pid=""
stop_relay() {
    [[ -n "$relay_pid" ]] && { kill "$relay_pid" 2>/dev/null || true; wait "$relay_pid" 2>/dev/null || true; }
    relay_pid=""
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
trap 'stop_relay; stop_server; stop_service' EXIT

api() { # api <method> <path> <json> [bearer]
    local auth=()
    [[ -n "${4:-}" ]] && auth=(-H "Authorization: Bearer $4")
    curl -s --cacert "$out_dir/ca.pem" -X "$1" -H 'Content-Type: application/json' "${auth[@]}" -d "$3" "$service_url$2"
}

redeem_as() { # redeem_as <server key file> <ticket>: the HTTP status of a redeem by that server
    curl -s -o /dev/null -w '%{http_code}' --cacert "$out_dir/ca.pem" -X POST -H 'Content-Type: application/json' \
        -H "Authorization: MB2-Server $(cat "$1")" -d "{\"ticket\":\"$2\"}" "$service_url/v1/server/redeem"
}

expect_eq() { # expect_eq <what> <got> <want>
    if [[ "$2" == "$3" ]]; then
        echo "  $1: yes"
    else
        echo "  $1: NO (got '$2', want '$3')"
        status=1
    fi
}

json() { grep -o "\"$1\":\"[^\"]*\"" | head -1 | cut -d'"' -f4; } # json <key>: the first string field of that name

code_for() { # the code in the newest outbox message to $1
    local file
    file=$(grep -l "^To: $1\$" "$out_dir"/outbox/*.txt | sort | tail -1)
    grep -E '^\s+[A-Z2-9]{8}\s*$' "$file" | tr -d ' '
}

tls_args=("-ini:Engine:[SSL]:OverrideCertificateBundlePath=$out_dir/ca.pem")
accounts_args=("-MB2Accounts=$service_url" "${tls_args[@]}")
# -RenderOffScreen on every client, shot or not: a run under systemd-run inherits the desktop's DISPLAY and
# WAYLAND_DISPLAY, and a client without it opens a window on the owner's screen. The server ignores it.
common_args=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -RenderOffScreen)
# MB2_ACCOUNT_RES=WxH and MB2_ACCOUNT_UI_SCALE=f render the screenshot clients at another size and interface scale
# (QA U9: the account and creator screens at 150 % on 5:4).
account_res="${MB2_ACCOUNT_RES:-1920x1080}"
render_args=(-RenderOffScreen -windowed -ForceRes "-ResX=${account_res%x*}" "-ResY=${account_res#*x}")
[[ -n "${MB2_ACCOUNT_UI_SCALE:-}" ]] && render_args+=("-MB2UIScale=$MB2_ACCOUNT_UI_SCALE")

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
random_b64() { head -c 32 /dev/urandom | base64 | tr '+/' '-_' | tr -d '='; }
# admit <server address> <name>: ask for a ticket; sets <name>_ticket and <name>_key.
admit() {
    local answer
    answer=$(api POST /v1/admission "{\"server\":\"$1\"}" "$session")
    printf -v "$2_ticket" '%s' "$(json ticket <<< "$answer")"
    printf -v "$2_key" '%s' "$(json key <<< "$answer")"
}
forged=$(random_b64)
nullrhi=(-nullrhi -MB2AccountTimeout=60)
client dev-key REFUSED "127.0.0.1:$port?MB2Character=dev-key-player" "${nullrhi[@]}"
client no-ticket REFUSED "127.0.0.1:$port" "${nullrhi[@]}"
no_ticket=$({ grep -ac 'admission_refused stage=hello reason=no_ticket' "$out_dir/server.log" || true; })
expect_eq "refused a development key and a join without a ticket at the hello" "$([[ $no_ticket -ge 2 ]] && echo yes || echo "no ($no_ticket)")" yes
client forged REFUSED "127.0.0.1:$port?EncryptionToken=$forged" "${nullrhi[@]}" "-MB2AdmissionKey=$(random_b64)"
expect_server "refused a forged ticket" 'admission_refused stage=redeem reason="Your sign-in for this world has expired'
echo "Tickets name their server (AR1)"
expect_eq "no ticket without a server" "$(api POST /v1/admission '{}' "$session" | json error)" request_invalid
expect_eq "no ticket for an unlisted address" "$(api POST /v1/admission '{"server":"evil.example.si:7777"}' "$session" | json error)" server_unknown
expect_eq "no ticket for another port" "$(api POST /v1/admission "{\"server\":\"127.0.0.1:$((port + 1))\"}" "$session" | json error)" server_unknown
admit "$melje_address" melje
client other-server REFUSED "127.0.0.1:$port?EncryptionToken=$melje_ticket" "${nullrhi[@]}" "-MB2AdmissionKey=$melje_key"
expect_eq "melje can spend its own ticket after this server refused it" "$(redeem_as "$secrets/melje.key" "$melje_ticket")" 200
echo "Encrypted transport (ADR-083)"
# A ticket stolen off the wire (it is in the clear in the hello) but without its key: spent, and nobody admitted.
admit "127.0.0.1:$port" stolen
client stolen REFUSED "127.0.0.1:$port?EncryptionToken=$stolen_ticket" -nullrhi -MB2AccountTimeout=120 "-MB2AdmissionKey=$(random_b64)"
expect_server "spent the stolen ticket at the hello" 'MB2ACCOUNT role=server event=ticket_redeemed .* model=manny encrypted=1'
admitted_manny=$({ grep -ac 'MB2ACCOUNT role=server event=admitted .* model=manny' "$out_dir/server.log" || true; })
expect_eq "admitted nobody with the stolen ticket" "$admitted_manny" 0
# Each ticket is asked for just before its join: a ticket lives one minute, about four client starts.
admit "localhost:$port" t1
ticket_1=$t1_ticket
expect_eq "melje cannot spend this server's ticket" "$(redeem_as "$secrets/melje.key" "$ticket_1")" 403
# The admitted client joins through the recording relay, as an observer on the path would see it.
relay_port=$((port + 3))
"$repo_root/scripts/mb2.sh" wire-relay "127.0.0.1:$relay_port" "127.0.0.1:$port" "$out_dir/wire-admitted.txt" > "$out_dir/relay.log" 2>&1 &
relay_pid=$!
sleep 1
client admitted ADMITTED "127.0.0.1:$relay_port?EncryptionToken=$ticket_1" "${nullrhi[@]}" "-MB2AdmissionKey=$t1_key" -MB2AccountHold=40 \
    -MB2AccountFirst=Bor -MB2AccountLast=Mlakar -MB2AccountModel=manny &
admitted_pid=$!
for _ in $(seq 1 240); do
    grep -aq 'tag=ADMITTED event=in_world' "$out_dir/client-admitted.log" 2>/dev/null && break
    sleep 0.5
done
client replay REFUSED "127.0.0.1:$port?EncryptionToken=$ticket_1" "${nullrhi[@]}" "-MB2AdmissionKey=$t1_key"
admit "127.0.0.1:$port" t2
ticket_2=$t2_ticket
client twice REFUSED "127.0.0.1:$port?EncryptionToken=$ticket_2" "${nullrhi[@]}" "-MB2AdmissionKey=$t2_key"
expect_server "refused the character a second time" 'admission_refused stage=redeem reason=already_in_world'
wait "$admitted_pid" || true
grep -aq 'tag=ADMITTED event=pass' "$out_dir/client-admitted.log" || status=1
stop_relay
# On the wire: the ticket is in the hello (the scan's own control: it finds a string the engine bit-packed), and
# nothing the server sends after it, nor the client's login, carries these in the clear.
"$repo_root/scripts/mb2.sh" wire-scan "$out_dir/wire-admitted.txt" "$ticket_1" /Game/World/lent MarburgumGameMode Mlakar manny \
    > "$out_dir/wire-scan.txt"
sed 's/needle=[A-Za-z0-9_-]\{43\} /needle=<ticket> /; s/^/  /' "$out_dir/wire-scan.txt"
wire_hits() { grep "needle=$1 " "$out_dir/wire-scan.txt" | sed -n "s/.* $2_hits=\([0-9]*\).*/\1/p"; }
wire_packets=$(sed -n 's/^wire packets=\([0-9]*\).*/\1/p' "$out_dir/wire-scan.txt")
expect_eq "the relay carried the session (100+ datagrams)" "$([[ ${wire_packets:-0} -ge 100 ]] && echo yes || echo "no (${wire_packets:-0})")" yes
expect_eq "the hello carried the ticket in the clear (scan control)" "$([[ $(wire_hits "$ticket_1" client) -ge 1 ]] && echo yes || echo no)" yes
for needle in /Game/World/lent MarburgumGameMode Mlakar manny; do
    expect_eq "no '$needle' on the wire" "$(( $(wire_hits "$needle" client) + $(wire_hits "$needle" server) ))" 0
done
replays=$(grep -ac 'admission_refused stage=redeem reason="Your sign-in for this world has expired' "$out_dir/server.log" || true)
if [[ "$replays" -ge 3 ]]; then
    echo "  server refused the spent and the other server's tickets: yes"
else
    echo "  server refused the spent and the other server's tickets: NO ($replays expired-ticket refusals, want 3: forged, melje's, replay)"
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
# Every verification code the service mailed (the outbox), as the header promises.
mapfile -t codes < <(grep -hE '^\s+[A-Z2-9]{8}\s*$' "$out_dir"/outbox/*.txt | tr -d ' ' | sort -u)
echo "  verification codes checked: ${#codes[@]}"
for secret in drava-bridge-1990 pohorje-snow-1991 "$session" "$ticket_1" "$ticket_2" "$t1_key" "$t2_key" "$stolen_key" "${codes[@]}"; do
    if grep -q -- "$secret" "$out_dir/accounts.json"; then
        echo "  account store holds a secret in the clear: ${secret:0:6}…"
        leaks=1
    fi
done
for secret in drava-bridge-1990 pohorje-snow-1991 "$session" "${codes[@]}"; do
    if grep -aq -- "$secret" "$out_dir"/server.log "$out_dir"/client-*.log "$out_dir"/service-*.log; then
        echo "  a log holds a password, a session or a verification code: ${secret:0:6}…"
        leaks=1
    fi
done
[[ "$leaks" -eq 0 ]] && echo "  none in the store or the logs" || status=1
# AR13: the server never logs a ticket or a key; the clients that signed in through the screens never log the token.
# (grep -c exits 1 on no match, which set -e must not take for a failure.)
server_secrets=0
for secret in "$ticket_1" "$ticket_2" "$stolen_ticket" "$melje_ticket" "$t1_key" "$t2_key" "$stolen_key"; do
    grep -aq -- "$secret" "$out_dir/server.log" "$out_dir"/service-*.log && server_secrets=$((server_secrets + 1))
done
expect_eq "the server and service logs hold no ticket or key" "$server_secrets" 0
token_lines=$(cat "$out_dir"/client-first.log "$out_dir"/client-return.log | { grep -ac 'EncryptionToken=\|MB2Ticket=' || true; })
expect_eq "rendered clients logged no encryption token" "$token_lines" 0
expect_eq "rendered clients encrypted their sessions" "$(cat "$out_dir"/client-first.log "$out_dir"/client-return.log | { grep -ac 'MB2ACCOUNT role=client event=encrypted' || true; })" 2
expect_eq "the service logged a redeem for another server" "$({ grep -ac 'event=redeem_refused reason=other_server caller=melje ticket_server=lent-smoke' "$out_dir"/service-1.log || true; })" 1

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts/build-stamp.sh).
if [[ "$mode" == staged ]]; then
    echo "Build"
    "$repo_root/scripts/build-stamp.sh" check | sed 's/^/  /' || echo "WARNING: the staged build is not fresh" >&2
fi

if [[ "$status" -eq 0 ]]; then
    echo "Result: PASS — $out_dir"
else
    echo "Result: FAIL — $out_dir"
fi
exit "$status"
