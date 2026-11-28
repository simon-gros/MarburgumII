# Windows twin of seed-chapter-vi-store.sh (same store text byte for byte, same messages and exit codes).
# Writes a world store (schema 6) for an owner playtest of Chapter VI, The Negative (ADR-084): one character per given
# Kiosk 67 outcome, each having finished the Prologue to Chapter III, Nika's Then and Now (the camera) and Kiosk 67 with
# that outcome, holding the papers those missions leave in custody and 120 money. Chapters IV and V are not played.
#
# Usage: scripts\seed-chapter-vi-store.ps1 <store.json> <character>:<outcome> [<character>:<outcome> ...]
#   outcome: temporary_work | tab_paid | debt_erased | object_sold | left_alone
# Refuses to overwrite an existing file. Characters are development keys (3-40 of a-z, 0-9, - and _).
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$argv = @($args | ForEach-Object { "$_" })
if ($argv.Count -lt 2) { Exit-Usage 'Usage: scripts\seed-chapter-vi-store.ps1 <store.json> <character>:<outcome> [...]' }
$store = $argv[0]
$entries = @($argv | Select-Object -Skip 1)
# Relative to the caller's location, as the shell resolves it (not the process's .NET current directory).
$storePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($store)
if (Test-Path -LiteralPath $storePath) {
    Exit-Failure "$store exists; choose a new path (a playtest store is never overwritten)."
}

function Get-DoneMission([string]$Mission, [string]$Stage, [string]$More = '') {
    '"{0}":{{"completed":true,"reward_paid":true,"stage":"{1}","started_revision":1{2}}}' -f $Mission, $Stage, $More
}
$papers = '{"torn_gate_photograph":1,"faded_transport_ticket":1,"carbon_copy_receipt":1,"water_damaged_accounting_page":1,"brass_key":1,"drava_cashbox":1,"kainach_serial_photos":1,"yard_vehicle_log":1,"corrected_yard_schedule":1,"tone_vogrin_locker_card":1,"exchange_counterfoil_copy":1,"kainach_contract_reference":1,"prehod_classified_clippings":1,"pohorska_bank_file_locator":1,"k67_raeda_concession_copy":1,"camera":1,"nika_old_prints":1,"photo_vodni_stolp":1,"photo_sodni_stolp":1,"photo_stari_most":1,"fishing_rod":1}'
$inventories = @()
$missions = @()
$money = @()
foreach ($entry in $entries) {
    # ${entry%%:*} and ${entry#*:}: before and after the first colon (both the whole entry when there is none).
    $colon = $entry.IndexOf(':')
    if ($colon -ge 0) { $who = $entry.Substring(0, $colon); $outcome = $entry.Substring($colon + 1) }
    else { $who = $entry; $outcome = $entry }
    if ($who -cnotmatch '^[a-z0-9_-]{3,40}$') { Exit-Usage "Not a character key: $who" }
    switch -CaseSensitive ($outcome) {
        'temporary_work' { $choice = 'find_work' }
        'tab_paid' { $choice = 'pay_tab' }
        'debt_erased' { $choice = 'erase_debt' }
        'object_sold' { $choice = 'sell_object' }
        'left_alone' { $choice = 'leave_alone' }
        default { Exit-Usage "Unknown Kiosk 67 outcome: $outcome" }
    }
    $played = @(
        (Get-DoneMission 'lent_first_catch' 'meet_vera' ',"journal_entry":"blue_ledger_r90"')
        (Get-DoneMission 'raeda_names_no_longer_match' 'decide_maja' ',"choice":"tell_maja","outcome":"maja_informed"')
        (Get-DoneMission 'raeda_last_shift' 'show_vera')
        (Get-DoneMission 'raeda_city_for_sale' 'decide_milan' ',"choice":"trust_milan","outcome":"milan_trusted"')
        (Get-DoneMission 'nika_then_and_now' 'keep_or_archive' ',"choice":"darkroom_lesson","outcome":"darkroom_lesson"')
        (Get-DoneMission 'kiosk_67_customer_stopped_coming' 'resolution' (',"choice":"{0}","outcome":"{1}"' -f $choice, $outcome))
    ) -join ','
    $inventories += "`"$who`":$papers"
    $missions += "`"$who`":{$played}"
    $money += "`"$who`":120"
}
$parent = Split-Path -Parent $storePath
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
Write-SmokeText $storePath (('{{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{{}},"inventories":{{{0}}},"missions":{{{1}}},"conditions":{{}},"skills":{{}},"money":{{{2}}},"discoveries":{{}},"receipts":{{}}}}' -f ($inventories -join ','), ($missions -join ','), ($money -join ',')) + "`n")
"Wrote $store for: $($argv -join ' ')"
