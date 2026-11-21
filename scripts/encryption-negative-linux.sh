#!/usr/bin/env bash
# Negative controls around ADR-083 that the account smoke does not cover.
#
#   unencrypted   A client that expects encryption (ticket on the URL, key on the command line) joins a
#                 development server that never encrypts. It must leave. Staying in that world is a failure.
#   service-down  An account-required server whose account service is not listening must refuse the hello
#                 and admit nobody.
#   service-stall An account-required server whose service accepts TLS and then sends no HTTP answer must
#                 time out the redeem and admit nobody. Skipped when openssl s_server cannot listen.
#
# Replay, duplication and out-of-order datagrams are not injected here: Marburgum.Net.StrictEncryption and
# Marburgum.Net.ReplayWindow cover tamper, a wrong key, plaintext-after-key and replayed packets (ADR-088), and the
# account smoke covers a stolen hello ticket redeemed before its player.
#
# Usage: scripts/encryption-negative-linux.sh [staged]
# Port MB2_ENC_NEG_PORT (default 28170). Never 7777 or 17777.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-staged}"
port="${MB2_ENC_NEG_PORT:-28170}"
if [[ "$port" == "7777" || "$port" == "17777" ]]; then
    echo "Refusing port $port" >&2
    exit 2
fi
if [[ "$mode" != staged ]]; then
    echo "Usage: $0 [staged]" >&2
    exit 2
fi

server_cmd=("$repo_root/Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh")
client_cmd=("$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh")
for executable in "${server_cmd[0]}" "${client_cmd[0]}"; do
    [[ -x "$executable" ]] || { echo "Missing executable: $executable" >&2; exit 1; }
done
"$repo_root/scripts/build-stamp.sh" check >/dev/null

out_dir="$repo_root/Saved/EncryptionNegative/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out_dir/secrets"
chmod 700 "$out_dir/secrets"
map="/Game/World/lent"
common=(-log -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -nullrhi)
status=0
server_pid=""

b64_32() { head -c 32 /dev/urandom | base64 | tr '+/' '-_' | tr -d '='; }
ticket="$(b64_32)"
key="$(b64_32)"
[[ ${#ticket} -eq 43 && ${#key} -eq 43 ]] || { echo "ticket/key encoding was not 43 characters" >&2; exit 1; }

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

wait_ready() {
    local log="$1"
    local _
    for _ in $(seq 1 360); do
        grep -aq "MB2SMOKE role=server event=ready .* port=$port " "$log" && return 0
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "Server exited before becoming ready; see $log" >&2
            return 1
        fi
        sleep 0.5
    done
    echo "Server did not become ready on port $port; see $log" >&2
    return 1
}

port_free() {
    ! ss -Hlun "sport = :$port" | grep -aq .
}

note() { echo "$*" | tee -a "$out_dir/summary.txt"; }

{
    echo "MarburgumII encryption negative controls"
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "git_sha: $(git -C "$repo_root" rev-parse HEAD)"
    echo "port: $port"
} > "$out_dir/summary.txt"
"$repo_root/scripts/build-stamp.sh" check >> "$out_dir/summary.txt"

# ---------------------------------------------------------------- a world that never encrypts
# A signed-in client is one started with -MB2Accounts (https). The engine fails the hello when that client
# offers a token to a server that never encrypts (PendingConnectionFailure), so the post-join guard
# refused_unencrypted_world is not what this path hits. The client must still end the session and must not
# keep the city as a local authority world (front_end_enforced).
note "Case unencrypted: development server, signed-in client expects encryption"
if ! port_free; then
    note "  unencrypted: FAIL (port $port busy)"
    status=1
else
    setsid "${server_cmd[@]}" "$map" -port="$port" "${common[@]}" > "$out_dir/unencrypted-server.log" 2>&1 &
    server_pid=$!
    if wait_ready "$out_dir/unencrypted-server.log"; then
        timeout --kill-after=15 90 "${client_cmd[@]}" \
            "127.0.0.1:$port?MB2Character=enc-neg-player?EncryptionToken=$ticket" \
            "${common[@]}" "-MB2AdmissionKey=$key" "-MB2Accounts=https://127.0.0.1:9" \
            > "$out_dir/unencrypted-client.log" 2>&1 || true
        refused=0
        if grep -aq 'event=refused_unencrypted_world\|phase=error fault=refused' "$out_dir/unencrypted-client.log"; then
            refused=1
        fi
        front=0
        if grep -aq 'event=front_end_enforced' "$out_dir/unencrypted-client.log"; then
            front=1
        fi
        if [[ "$refused" -eq 1 && "$front" -eq 1 ]]; then
            note "  unencrypted: PASS (session refused, city not kept as a local world)"
        else
            note "  unencrypted: FAIL (refused=$refused front_end_enforced=$front)"
            status=1
        fi
        if grep -aq 'MB2ACCOUNT role=server event=admitted\|event=ticket_redeemed' "$out_dir/unencrypted-server.log"; then
            note "  unencrypted server admitted someone: FAIL"
            status=1
        fi
    else
        note "  unencrypted: FAIL (server)"
        status=1
    fi
    stop_server
fi

# ---------------------------------------------------------------- account service not listening
note "Case service-down: nothing accepts the redeem"
down_port=$((port + 1))
head -c 48 /dev/urandom | base64 | tr -d '\n/+=' > "$out_dir/secrets/server.key"
chmod 600 "$out_dir/secrets/server.key"
if ! port_free; then
    note "  service-down: FAIL (port $port still busy)"
    status=1
else
    setsid "${server_cmd[@]}" "$map" -port="$port" \
        "-MB2Accounts=https://127.0.0.1:$down_port" "-MB2AccountsKeyFile=$out_dir/secrets/server.key" \
        "${common[@]}" > "$out_dir/down-server.log" 2>&1 &
    server_pid=$!
    if wait_ready "$out_dir/down-server.log"; then
        timeout --kill-after=15 40 "${client_cmd[@]}" \
            "127.0.0.1:$port?EncryptionToken=$ticket" "${common[@]}" "-MB2AdmissionKey=$key" \
            > "$out_dir/down-client.log" 2>&1 || true
        sleep 2
        if grep -aq 'admission_refused stage=redeem reason="The account service is unavailable' "$out_dir/down-server.log" \
            && ! grep -aq 'event=ticket_redeemed' "$out_dir/down-server.log" \
            && ! grep -aq 'event=admitted' "$out_dir/down-server.log"; then
            note "  service-down: PASS"
        else
            note "  service-down: FAIL (see down-server.log)"
            status=1
        fi
        if grep -aq -- "$ticket" "$out_dir/down-server.log" || grep -aq -- "$key" "$out_dir/down-server.log"; then
            note "  service-down leaked the ticket or key into the server log: FAIL"
            status=1
        fi
    else
        note "  service-down: FAIL (server)"
        status=1
    fi
    stop_server
fi

# ---------------------------------------------------------------- TLS accepts, HTTP never answers
note "Case service-stall: TLS up, no HTTP response"
stall_port=$((port + 2))
stall_pid=""
stop_stall() {
    if [[ -n "$stall_pid" ]]; then
        kill "$stall_pid" 2>/dev/null || true
        wait "$stall_pid" 2>/dev/null || true
        stall_pid=""
    fi
}
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj "/CN=MB2 stall CA" -keyout "$out_dir/secrets/ca.key" \
    -out "$out_dir/ca.pem" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=127.0.0.1" -keyout "$out_dir/secrets/stall.key" \
    -out "$out_dir/secrets/stall.csr" 2>/dev/null
openssl x509 -req -in "$out_dir/secrets/stall.csr" -CA "$out_dir/ca.pem" -CAkey "$out_dir/secrets/ca.key" \
    -CAcreateserial -days 1 \
    -extfile <(printf "subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n") \
    -out "$out_dir/stall.pem" 2>/dev/null
openssl s_server -accept "$stall_port" -cert "$out_dir/stall.pem" -key "$out_dir/secrets/stall.key" -naccept 1 \
    > "$out_dir/stall.log" 2>&1 &
stall_pid=$!
sleep 0.5
if ! kill -0 "$stall_pid" 2>/dev/null; then
    note "  service-stall: SKIPPED (openssl s_server did not start)"
    stall_pid=""
else
    if ! port_free; then
        note "  service-stall: FAIL (game port busy)"
        status=1
    else
        setsid "${server_cmd[@]}" "$map" -port="$port" \
            "-MB2Accounts=https://127.0.0.1:$stall_port" "-MB2AccountsKeyFile=$out_dir/secrets/server.key" \
            "-ini:Engine:[SSL]:OverrideCertificateBundlePath=$out_dir/ca.pem" \
            "${common[@]}" > "$out_dir/stall-server.log" 2>&1 &
        server_pid=$!
        if wait_ready "$out_dir/stall-server.log"; then
            timeout --kill-after=15 40 "${client_cmd[@]}" \
                "127.0.0.1:$port?EncryptionToken=$(b64_32)" "${common[@]}" "-MB2AdmissionKey=$(b64_32)" \
                > "$out_dir/stall-client.log" 2>&1 || true
            sleep 12
            if grep -aq 'admission_refused stage=redeem' "$out_dir/stall-server.log" \
                && ! grep -aq 'event=ticket_redeemed\|event=admitted' "$out_dir/stall-server.log"; then
                note "  service-stall: PASS"
            else
                note "  service-stall: FAIL (redeem was not refused, or somebody was admitted)"
                status=1
            fi
        else
            note "  service-stall: FAIL (server)"
            status=1
        fi
        stop_server
    fi
    stop_stall
fi

note "Not claimed: datagram-level replay injection (unit-tested only, ADR-088) or session-key rotation."
note "Result: $([[ $status -eq 0 ]] && echo PASS || echo FAIL) — $out_dir"
exit "$status"
