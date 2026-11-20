#!/usr/bin/env bash
# QA U11: gather every player-facing LOCTEXT from the client and game modules, import the Slovene translation from
# Content/Localization/Game/sl/Game.po, compile the .locres files the client loads, and export the .po files again
# (new or changed strings appear there with an empty msgstr for the translator).
#
# Usage: UE_ROOT=/path/to/UE_5.8.3 scripts/localize-linux.sh
# Exit status 0 when the pipeline ran; the summary lists how many Slovene entries are still untranslated.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${UE_ROOT:-}" ]]; then
    echo "Set UE_ROOT to your Unreal Engine 5.8.3 installation root." >&2
    exit 2
fi
"$repo_root/scripts/check-engine-version.sh" "$UE_ROOT"

log="$repo_root/Saved/Logs/localize-$(date -u +%Y%m%dT%H%M%SZ).log"
mkdir -p "$(dirname "$log")"
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$repo_root/MarburgumII.uproject" -run=GatherText \
    -config="$repo_root/Config/Localization/Game.ini" -unattended -nop4 -nosplash -nullrhi -log -stdout >"$log" 2>&1 || {
    echo "GatherText failed; see $log" >&2
    tail -n 30 "$log" >&2
    exit 1
}

po="$repo_root/Content/Localization/Game/sl/Game.po"
if [[ ! -f "$po" ]]; then
    echo "No Slovene .po was written; see $log" >&2
    exit 1
fi
# An entry is untranslated when its msgstr is empty (the header entry has an empty msgid and does not count).
untranslated="$(awk '/^msgid "/ { id = $0 } /^msgstr ""$/ { if (id != "msgid \"\"") n++ } END { print n + 0 }' "$po")"
total="$(grep -c '^msgid "' "$po" || true)"
echo "Localization: $((total - 1)) entries, $untranslated without a Slovene translation"
for culture in en sl; do
    locres="$repo_root/Content/Localization/Game/$culture/Game.locres"
    [[ -f "$locres" ]] && echo "  $culture: $(stat -c %s "$locres") bytes in Game.locres"
done
echo "Log: $log"
