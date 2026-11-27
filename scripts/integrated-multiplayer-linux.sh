#!/usr/bin/env bash
# One current Linux Client/Server pair, then the multiplayer regression.
#
#   scripts/integrated-multiplayer-linux.sh [--package] [smoke ...]
#
# --package builds, cooks and stages first (scripts/package-linux.sh). Without it, a stale or missing
# staged tree is a failure: this script does not treat a source compile as a packaged result.
#
# A package stays current when later commits touch only docs or scripts (scripts/build-stamp.sh). The
# identity file records both the packaged commit and HEAD, the executable hashes, and the cooked pak
# identity. services/accounts and tools/mb2 must match the packaged commit; a newer account service
# against an older game, or the reverse, fails closed.
#
# Default smokes are the acceptance set. Fault-injected net is separate from the clean net smoke.
# Ports are 281xx. Never 7777 or 17777.
set -uo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
ue_root="${UE_ROOT:-/mnt/data/unreal/5.8.4}"
export UE_ROOT="$ue_root"

do_package=0
if [[ "${1:-}" == "--package" ]]; then
    do_package=1
    shift
fi

run_id="$(date -u +%Y%m%dT%H%M%SZ)"
out="$repo_root/Saved/Integrated/$run_id"
mkdir -p "$out"
matrix="$out/matrix.jsonl"
summary="$out/SUMMARY.txt"
: > "$matrix"
: > "$summary"

json_line() { # name status exit started ended evidence port
    printf '{"name":"%s","status":"%s","exit":%s,"started":"%s","ended":"%s","evidence":"%s","port":"%s"}\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" | tee -a "$matrix"
}

record() { # name status exit evidence port
    local ended
    ended="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    json_line "$1" "$2" "$3" "${started:-$ended}" "$ended" "$4" "$5"
    echo "$1 $2 exit=$3 evidence=${4:--} port=$5" | tee -a "$summary"
}

if [[ "$do_package" -eq 1 ]]; then
    echo "Packaging Development Client and Server" | tee -a "$summary"
    if ! "$repo_root/scripts/check-engine-version.sh" "$ue_root" | tee -a "$out/engine.txt"; then
        record package BLOCKED 2 "" ""
        echo "Result: BLOCKED — engine" | tee -a "$summary"
        exit 3
    fi
    if ! "$repo_root/scripts/package-linux.sh" Development > "$out/package.log" 2>&1; then
        record package FAIL $? "$out/package.log" ""
        echo "Result: FAIL — package" | tee -a "$summary"
        exit 1
    fi
    record package PASS 0 "$out/package.log" ""
fi

{
    echo "integrated multiplayer $run_id"
    echo "head=$(git rev-parse HEAD)"
    echo "head_subject=$(git log -1 --format=%s)"
    echo "dirty_porcelain<<"
    git status --porcelain
    echo ">>"
    echo "ue_root=$ue_root"
} > "$out/identity.txt"
"$repo_root/scripts/check-engine-version.sh" "$ue_root" >> "$out/identity.txt" 2>&1 || echo "engine_check=failed" >> "$out/identity.txt"

if ! "$repo_root/scripts/build-stamp.sh" check > "$out/build-stamp.txt" 2>&1; then
    cat "$out/build-stamp.txt" | tee -a "$summary"
    record preflight BLOCKED 3 "$out/build-stamp.txt" ""
    echo "Result: BLOCKED — staged build is missing or stale. Re-run with --package." | tee -a "$summary"
    exit 3
fi
cat "$out/build-stamp.txt" >> "$out/identity.txt"
packaged_sha="$(sed -n 's/^build_git_sha=//p' "$out/build-stamp.txt")"
{
    echo "input_diff_since_package<<"
    git diff --stat "$packaged_sha" HEAD -- Source Config Content Data Plugins MarburgumII.uproject services/accounts tools/mb2
    echo ">>"
} >> "$out/identity.txt"

schema="$(sed -n 's/.*WorldStoreSchemaVersion = \([0-9][0-9]*\).*/\1/p' \
    Source/MarburgumIIShared/Public/MarburgumGameplayContract.h | head -1)"
echo "world_store_schema=$schema" | tee -a "$out/identity.txt"
if [[ "$schema" != 6 ]]; then
    record preflight FAIL 1 "world store schema is $schema, want 6" ""
    echo "Result: FAIL — world store schema" | tee -a "$summary"
    exit 1
fi
if ! git diff --quiet "$packaged_sha" HEAD -- services/accounts tools/mb2 \
    || [[ -n "$(git status --porcelain -- services/accounts tools/mb2)" ]]; then
    record preflight FAIL 1 "account service or mb2 differs from the packaged commit" ""
    echo "Result: FAIL — account tooling does not match the package" | tee -a "$summary"
    exit 1
fi

echo "Hashing cooked content" | tee -a "$summary"
find Saved/StagedBuilds -type f \( -name '*.pak' -o -name '*.ucas' -o -name '*.utoc' \) -print0 \
    | sort -z | xargs -0 sha256sum > "$out/cooked-identity.txt"
sha256sum "$out/cooked-identity.txt" | tee -a "$out/identity.txt" >> "$out/cooked-identity.sha"

unit() { # name manifest timeout
    local name="$1" manifest="$2" seconds="$3"
    started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    local log="$out/$name.log"
    timeout --kill-after=15 "$seconds" cargo test --manifest-path "$repo_root/$manifest" > "$log" 2>&1
    local code=$?
    if [[ "$code" -eq 0 ]]; then
        record "$name" PASS 0 "$log" ""
    else
        record "$name" FAIL "$code" "$log" ""
    fi
    return 0
}
unit accounts-unit services/accounts/Cargo.toml 900
unit mb2-unit tools/mb2/Cargo.toml 900

declare -A ports=(
    [encryption-negative]=28170
    [street-day]=28153
    [street-night]=28154
    [account]=28114
    [door]=28120
    [story]=28131
    [story-sl]=28132
    [mission]=28116
    [shop]=28113
    [combat]=28110
    [vehicle]=28109
    [chat]=28102
    [net]=28101
    [net-fault]=28180
    [interaction]=28105
    [lent]=28106
    [weather]=28111
    [condition]=28107
    [swim]=28108
)

default_suite=(
    encryption-negative street-day street-night account door story story-sl
    mission shop combat vehicle chat net net-fault interaction lent weather condition swim
)
suite=("$@")
if [[ ${#suite[@]} -eq 0 ]]; then
    suite=("${default_suite[@]}")
fi

run_smoke() { # name seconds command...
    local name="$1" seconds="$2"
    shift 2
    local port="${ports[$name]:-}"
    if [[ "$port" == "7777" || "$port" == "17777" ]]; then
        record "$name" FAIL 2 "refused playtest port" "$port"
        return 0
    fi
    if [[ -n "$port" ]] && ss -Hlun "sport = :$port" | grep -aq .; then
        record "$name" BLOCKED 1 "port $port is in use" "$port"
        return 0
    fi
    started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    local log="$out/$name.out"
    timeout --kill-after=30 "$seconds" "$@" > "$log" 2>&1
    local code=$?
    local evidence
    evidence="$(grep -aoE 'Saved/[A-Za-z0-9]+/[0-9]{8}T[0-9]{6}Z' "$log" | head -1 || true)"
    local state="FAIL"
    if [[ "$code" -eq 0 ]]; then
        state="PASS"
    elif [[ "$code" -eq 124 || "$code" -eq 137 ]]; then
        state="FAIL"
    fi
    record "$name" "$state" "$code" "${evidence:-$log}" "$port"
    sleep 5
}

for name in "${suite[@]}"; do
    case "$name" in
        encryption-negative)
            MB2_ENC_NEG_PORT="${ports[$name]}" run_smoke "$name" 900 "$repo_root/scripts/encryption-negative-linux.sh" staged
            ;;
        street-day)
            MB2_CROWD_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/street-multiplayer-smoke-linux.sh" staged day
            ;;
        street-night)
            MB2_CROWD_PORT="${ports[$name]}" run_smoke "$name" 1200 "$repo_root/scripts/street-multiplayer-smoke-linux.sh" staged night
            ;;
        account)
            MB2_ACCOUNT_PORT="${ports[$name]}" MB2_ACCOUNT_SERVICE_PORT=28115 \
                run_smoke "$name" 4200 "$repo_root/scripts/account-smoke-linux.sh" staged
            ;;
        door)
            MB2_DOOR_PORT="${ports[$name]}" run_smoke "$name" 4200 "$repo_root/scripts/door-smoke-linux.sh" staged
            ;;
        story)
            MB2_STORY_PORT="${ports[$name]}" run_smoke "$name" 3600 "$repo_root/scripts/story-smoke-linux.sh" staged
            ;;
        story-sl)
            MB2_STORY_PORT="${ports[$name]}" MB2_STORY_CLIENT_ARGS="-culture=sl" \
                run_smoke "$name" 3600 "$repo_root/scripts/story-smoke-linux.sh" staged
            ;;
        mission)
            MB2_MISSION_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/mission-smoke-linux.sh" staged
            ;;
        shop)
            MB2_SHOP_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/shop-smoke-linux.sh" staged
            ;;
        combat)
            MB2_COMBAT_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/combat-smoke-linux.sh" staged
            ;;
        vehicle)
            MB2_VEHICLE_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/vehicle-smoke-linux.sh" staged
            ;;
        chat)
            MB2_CHAT_PORT="${ports[$name]}" run_smoke "$name" 1800 "$repo_root/scripts/chat-smoke-linux.sh" staged
            ;;
        net)
            MB2_SMOKE_PORT="${ports[$name]}" run_smoke "$name" 1800 "$repo_root/scripts/net-smoke-linux.sh" staged
            ;;
        net-fault)
            MB2_SMOKE_PORT="${ports[$name]}" MB2_SMOKE_EXTRA="-PktLag=120 -PktLagVariance=40 -PktLoss=1" \
                run_smoke "$name" 1800 "$repo_root/scripts/net-smoke-linux.sh" staged
            ;;
        interaction)
            MB2_INTERACT_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/interaction-smoke-linux.sh" staged
            ;;
        lent)
            MB2_LENT_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/lent-smoke-linux.sh" staged
            ;;
        weather)
            MB2_WEATHER_PORT="${ports[$name]}" run_smoke "$name" 2400 "$repo_root/scripts/weather-smoke-linux.sh" staged
            ;;
        condition)
            MB2_CONDITION_PORT="${ports[$name]}" run_smoke "$name" 1800 "$repo_root/scripts/condition-smoke-linux.sh" staged
            ;;
        swim)
            MB2_SWIM_PORT="${ports[$name]}" run_smoke "$name" 1800 "$repo_root/scripts/swim-smoke-linux.sh" staged
            ;;
        *)
            record "$name" FAIL 2 "unknown smoke" ""
            ;;
    esac
done

fails="$(grep -c '"status":"FAIL"' "$matrix" || true)"
blocked="$(grep -c '"status":"BLOCKED"' "$matrix" || true)"
echo "fails=$fails blocked=$blocked" | tee -a "$summary"
echo "Result: $([[ "$fails" -eq 0 && "$blocked" -eq 0 ]] && echo PASS || echo FAIL) — $out" | tee -a "$summary"
[[ "$fails" -eq 0 && "$blocked" -eq 0 ]]
