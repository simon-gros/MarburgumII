# Windows twin of negative-smoke-linux.sh (same phases, evidence lines and expectations).
# Chapter VI, The Negative, and the Frame 37 side story (ADR-084), played on /Game/World/lent against a real dedicated
# server through the same RPCs the E and P keys and the choice panel use.
#
# The world store is seeded (schema 6) with two characters who have finished the Prologue to Chapter III, Nika's Then
# and Now (the camera) and Kiosk 67: the first ended in MB2_NEGATIVE_OUTCOME (default object_sold), the second in
# left_alone. Each step is its own client process, so every step is also a disconnect and a reconnect.
#
#   Phase 1  both characters ask Nika at the same moment and both start the chapter; the second dates the print and stops.
#            The first plays it through: the order book and a photograph of the page, the light table refusing a
#            photograph of a sheet that is not there yet, Drago, Ivana Zupan and the Block 13 cage on the Tabor floor, the
#            contact sheet photographed, Nika offering the scrap tin (the side story) because there is no print to compare,
#            the loupe, a wrong deduction refused and the right one, Nika's print, the Melje date board photographed on the
#            Melje floor, Vera's chronology, the Glas strip, Albin's route for the Kiosk 67 outcome, the ticket, and Nika
#            with nothing more. The Journal and the Inventory are captured mid-chapter.
#   Phase 2  the server is restarted on the same store; the first character's last step is refused as already done, and
#            `mb2 store-check negative` reads the document itself.
#
# Usage: scripts\negative-smoke-windows.ps1 [staged|editor]   (MB2_NEGATIVE_PORT, default 27751; never 7777 or 17777;
#        MB2_NEGATIVE_OUTCOME temporary_work|tab_paid|debt_erased|object_sold|left_alone; MB2_NEGATIVE_CLOCK the world
#        clock, default Tuesday 10:00; MB2_NEGATIVE_CLIENT_ARGS extra client options, e.g. -culture=sl)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27751; if ($env:MB2_NEGATIVE_PORT) { $port = [int]$env:MB2_NEGATIVE_PORT }
$outcome = 'object_sold'; if ($env:MB2_NEGATIVE_OUTCOME) { $outcome = $env:MB2_NEGATIVE_OUTCOME }
$map = '/Game/World/lent'
$key = "mb2n-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$other = "mb2n-o$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"

switch -CaseSensitive ($outcome) {
    'temporary_work' { $choiceKiosk = 'find_work' }
    'tab_paid' { $choiceKiosk = 'pay_tab' }
    'debt_erased' { $choiceKiosk = 'erase_debt' }
    'object_sold' { $choiceKiosk = 'sell_object' }
    'left_alone' { $choiceKiosk = 'leave_alone' }
    default { Exit-Usage "Unknown MB2_NEGATIVE_OUTCOME $outcome" }
}
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\negative-smoke-windows.ps1 [staged|editor]'
if ($port -eq 7777 -or $port -eq 17777) {
    Exit-Usage "Port $port is the owner's playtest; pick another MB2_NEGATIVE_PORT."
}
$wall = 1200; if ($env:MB2_CLIENT_WALL_SECONDS) { $wall = [int]$env:MB2_CLIENT_WALL_SECONDS }

$outDir = Join-Path $repoRoot ('Saved\NegativeSmoke\' + (Get-UtcStamp) + "-$outcome")
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$clientExtra = @()
if ($env:MB2_NEGATIVE_CLIENT_ARGS) { $clientExtra = @($env:MB2_NEGATIVE_CLIENT_ARGS -split '\s+' | Where-Object { $_ }) }
$clock = '2026-06-16T08:00:00Z'; if ($env:MB2_NEGATIVE_CLOCK) { $clock = $env:MB2_NEGATIVE_CLOCK }
$server = $null
$status = 0

function Stop-GameServer { Stop-SmokeRole $script:server; $script:server = $null }

function Start-GameServer([string]$Log) {
    # A working morning (Tuesday 10:00): everyone is at their authored post and Nika's lab is open.
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_NEGATIVE_PORT' -ReadySeconds 180 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", "-MB2Clock=$clock", '-MB2ClockSpeed=0.001') + $commonArgs)
}

# run <label> <role> <start tag> <character> [client args...]. Start-Run starts the client (bash's "run ... &"),
# Complete-Run waits for it, retries a GPU start failure and reports.
function Start-Run([string]$Label, [string]$MissionRole, [string]$StartTag, [string]$Who, [string[]]$More = @()) {
    $url = "127.0.0.1:${port}?MB2Start=${StartTag}?MB2Character=$Who"
    $log = Join-Path $outDir "client-$Label.log"
    $proc = Start-SmokeRole -Target $targets.Client -Log $log -WallSeconds $wall -Arguments (
        @($url) + $commonArgs + $clientExtra + $More + @('-MB2MissionSmoke', "-MB2MissionRole=$MissionRole", '-MB2MissionTimeout=120'))
    @{ Label = $Label; MissionRole = $MissionRole; StartTag = $StartTag; Who = $Who; More = $More; Log = $log; Proc = $proc }
}

# -Detached: the client ran in the background (a bash subshell), so its FAIL does not change the status.
function Complete-Run($State, [switch]$Detached) {
    $code = Wait-SmokeRole $State.Proc; $State.Proc.Dispose()
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        if (-not ($code -ne 0 -and (Test-GpuInitFailure $State.Log))) { break }
        Write-Warn "  client $($State.Label): GPU device creation failed (exit $code); retrying"
        Move-Item -LiteralPath $State.Log -Destination (Join-Path $outDir "client-$($State.Label).gpu-init-failed-$attempt.log") -Force
        Start-Sleep -Seconds 3
        $next = Start-Run $State.Label $State.MissionRole $State.StartTag $State.Who $State.More
        $code = Wait-SmokeRole $next.Proc; $next.Proc.Dispose()
    }
    if ($code -eq 0 -and (Test-SmokeLog $State.Log "MB2MISSION role=client tag=$($State.MissionRole) event=pass")) {
        "  client $($State.Label): PASS"
    } else {
        "  client $($State.Label): FAIL (exit $code)"
        Get-SmokeLogMatches $State.Log "MB2MISSION role=client tag=$($State.MissionRole) event=(fail|expectation_failed)" 'MB2MISSION' '    '
        if (-not $Detached) { $script:status = 1 }
    }
}

function Invoke-Run([string]$Label, [string]$MissionRole, [string]$StartTag, [string]$Who, [string[]]$More = @()) {
    Complete-Run (Start-Run $Label $MissionRole $StartTag $Who $More)
}

function Get-Shot([string]$Label) { $renderArgs + @("-MB2MissionShot=$(Join-Path $outDir $Label)") }

# step <label> <target> <expected effect or refused:reason> [client args...]
function Invoke-Step([string]$Label, [string]$Target, [string]$Expect, [string[]]$More = @()) {
    Invoke-Run $Label 'STEP' "mission_$Target" $key (@("-MB2MissionTarget=$Target", "-MB2MissionExpect=$Expect") + $More)
}

# photo <label> <subject> <expected effect or refused:reason> [client args...]
function Invoke-Photo([string]$Label, [string]$Subject, [string]$Expect, [string[]]$More = @()) {
    Invoke-Run $Label 'PHOTO' "photo_$Subject" $key (@("-MB2MissionTarget=$Subject", "-MB2MissionExpect=$Expect") + $More)
}

# choice <label> <target> <mission> <choice> [expected effect or refused:reason; default: the mission completes]
function Invoke-Choice([string]$Label, [string]$Target, [string]$Mission, [string]$Choice, [string]$Expect) {
    $expectArgs = @()
    if ($Expect) { $expectArgs = @("-MB2MissionExpect=$Expect") }
    Invoke-Run $Label 'CHOICE' "mission_$Target" $key ((Get-Shot $Label) + @("-MB2MissionTarget=$Target", "-MB2MissionId=$Mission", "-MB2MissionChoice=$Choice") + $expectArgs)
}

# tour <label> <steps>: the UI capture tour as the first character, at Nika's lab (Journal, Inventory).
function Invoke-Tour([string]$Label, [string]$Steps) {
    $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir "client-$Label.log") -Arguments (
        @("127.0.0.1:${port}?MB2Start=mission_nika_vidmar?MB2Character=$key") + $commonArgs + $renderArgs + $clientExtra +
        @("-MB2UICapture=$outDir", '-MB2UICaptureScript=tour', "-MB2UICaptureLabel=$Label", "-MB2UITour=$Steps"))
    if ($code -eq 0) {
        "  tour ${Label}: captured"
    } else {
        "  tour ${Label}: FAIL (exit $code)"
        $script:status = 1
    }
}

function Get-DoneMission([string]$Id, [string]$Stage, [string]$More = '') {
    '"{0}":{{"completed":true,"reward_paid":true,"stage":"{1}","started_revision":1{2}}}' -f $Id, $Stage, $More
}
function Get-SeedMissions([string]$KioskChoice, [string]$KioskOutcome) {
    @(
        (Get-DoneMission 'lent_first_catch' 'meet_vera' ',"journal_entry":"blue_ledger_r90"')
        (Get-DoneMission 'raeda_names_no_longer_match' 'decide_maja' ',"choice":"tell_maja","outcome":"maja_informed"')
        (Get-DoneMission 'raeda_last_shift' 'show_vera')
        (Get-DoneMission 'raeda_city_for_sale' 'decide_milan' ',"choice":"trust_milan","outcome":"milan_trusted"')
        (Get-DoneMission 'nika_then_and_now' 'keep_or_archive' ',"choice":"darkroom_lesson","outcome":"darkroom_lesson"')
        (Get-DoneMission 'kiosk_67_customer_stopped_coming' 'resolution' ",`"choice`":`"$KioskChoice`",`"outcome`":`"$KioskOutcome`"")
    ) -join ','
}
# What those missions leave in custody (the prologue's cashbox papers, Chapter II's serial photographs, the camera).
$papers = '{"torn_gate_photograph":1,"faded_transport_ticket":1,"carbon_copy_receipt":1,"water_damaged_accounting_page":1,"brass_key":1,"drava_cashbox":1,"kainach_serial_photos":1,"yard_vehicle_log":1,"corrected_yard_schedule":1,"tone_vogrin_locker_card":1,"exchange_counterfoil_copy":1,"kainach_contract_reference":1,"prehod_classified_clippings":1,"pohorska_bank_file_locator":1,"k67_raeda_concession_copy":1,"camera":1,"nika_old_prints":1,"photo_vodni_stolp":1,"photo_sodni_stolp":1,"photo_stari_most":1}'
Write-SmokeText $store (('{{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{{}},"inventories":{{"{0}":{1},"{2}":{3}}},"missions":{{"{4}":{{{5}}},"{6}":{{{7}}}}},"conditions":{{}},"skills":{{}},"money":{{"{8}":120,"{9}":120}},"discoveries":{{}},"receipts":{{}}}}' -f
    $key, $papers, $other, $papers, $key, (Get-SeedMissions $choiceKiosk $outcome), $other, (Get-SeedMissions 'leave_alone' 'left_alone'), $key, $other) + "`n")

try {
    "Phase 1: dedicated server ($Mode) on $map, port $port; Kiosk 67 ended in $outcome; evidence in $outDir"
    $serverLog1 = Join-Path $outDir 'server-1.log'
    Start-GameServer $serverLog1
    [void](Wait-SmokeLog $serverLog1 'event=content_ready' 20 $null 0.25)
    $spawned = Get-SmokeLogCount $serverLog1 'MB2INTERACT role=server event=content_spawned'
    $skipped = Get-SmokeLogCount $serverLog1 'MB2INTERACT role=server event=content_skipped'
    $photoStarts = Get-SmokeLogCount $serverLog1 'MB2INTERACT role=server event=photo_start'
    $unplaced = (@(Get-SmokeLogMatches $serverLog1 'MB2INTERACT role=server event=photo_unplaced' '' |
        ForEach-Object { ($_ -replace '.*id=([a-z0-9_]*).*', '$1') + ' ' }) -join '')
    "  server: authored content spawned=$spawned skipped=$skipped; photo starts=$photoStarts unplaced=[ $unplaced]"
    foreach ($id in 'foto_vidmar_order_book', 'foto_vidmar_light_table', 'ivana_zupan', 'block_13_storage_cage', 'melje_date_board', 'stamp_collector', 'viole_terrace_regular') {
        if (-not (Test-SmokeLog $serverLog1 "event=content_spawned id=$id ")) { "  server: $id not spawned"; $status = 1 }
    }
    if (-not ($skipped -eq 0 -and -not ($unplaced -replace ' ', ''))) { $status = 1 }

    # Two characters at Nika at the same moment: each starts their own chapter.
    $runA = Start-Run 'n6-offer-a' 'STEP' 'mission_nika_vidmar' $key @('-nullrhi', '-MB2MissionTarget=nika_vidmar', '-MB2MissionExpect=mission_started')
    $runB = Start-Run 'n6-offer-b' 'STEP' 'mission_nika_vidmar' $other @('-nullrhi', '-MB2MissionTarget=nika_vidmar', '-MB2MissionExpect=mission_started')
    Complete-Run $runA -Detached
    Complete-Run $runB -Detached
    if (-not ((Test-SmokeLog (Join-Path $outDir 'client-n6-offer-a.log') 'tag=STEP event=pass') -and
              (Test-SmokeLog (Join-Path $outDir 'client-n6-offer-b.log') 'tag=STEP event=pass'))) { $status = 1 }
    Invoke-Run 'n6-other-date' 'STEP' 'mission_nika_vidmar' $other @('-nullrhi', '-MB2MissionTarget=nika_vidmar', '-MB2MissionExpect=stage_completed')

    Invoke-Step 'n6-date' 'nika_vidmar' 'stage_completed' (Get-Shot 'n6-date')
    Invoke-Step 'n6-book' 'foto_vidmar_order_book' 'stage_completed' (Get-Shot 'n6-book')
    Invoke-Photo 'n6-page' 'foto_vidmar_order_book_page' 'stage_completed' (Get-Shot 'n6-page')
    Invoke-Photo 'n6-sheet-early' 'zupan_contact_sheet_copy' 'refused:requirement_not_met' @('-nullrhi')
    Invoke-Step 'n6-drago' 'drago_potocnik' 'stage_completed'
    Invoke-Step 'n6-cage-early' 'block_13_storage_cage' 'refused:nothing_to_do' @('-nullrhi')
    Invoke-Step 'n6-ivana' 'ivana_zupan' 'stage_completed' (Get-Shot 'n6-ivana')
    Invoke-Step 'n6-cage' 'block_13_storage_cage' 'stage_completed' (Get-Shot 'n6-cage')
    Invoke-Photo 'n6-sheet' 'zupan_contact_sheet_copy' 'stage_completed' (Get-Shot 'n6-sheet')
    # No print to compare yet: Nika offers the scrap tin.
    Invoke-Step 'f37-offer' 'nika_vidmar' 'mission_started' (Get-Shot 'f37-offer')
    Invoke-Step 'f37-loupe' 'foto_vidmar_light_table' 'stage_completed' (Get-Shot 'f37-loupe')
    Invoke-Choice 'f37-wrong' 'foto_vidmar_light_table' 'nika_frame_37' 'the_man' 'refused:wrong_conclusion'
    Invoke-Choice 'f37-right' 'foto_vidmar_light_table' 'nika_frame_37' 'the_place' 'stage_completed'
    Invoke-Step 'f37-print' 'nika_vidmar' 'stage_completed'
    Invoke-Step 'n6-match' 'nika_vidmar' 'stage_completed'
    Invoke-Photo 'f37-yard' 'melje_date_board_view' 'stage_completed' (Get-Shot 'f37-yard')
    Invoke-Step 'f37-show' 'nika_vidmar' 'stage_completed'
    Invoke-Choice 'f37-scrap' 'nika_vidmar' 'nika_frame_37' 'lab_safe'
    Invoke-Step 'n6-vera' 'vera_horvat' 'stage_completed' (Get-Shot 'n6-vera')
    Invoke-Step 'n6-glas' 'mariborski_glas_archive' 'stage_completed' (Get-Shot 'n6-glas')
    Invoke-Tour 'n6-ui' 'journal,inventory:frame_37_print,inventory:zupan_contact_sheet,inventory:chronology_frame_37'
    switch -CaseSensitive ($outcome) {
        'temporary_work' {
            Invoke-Step 'n6-albin' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin')
        }
        { $_ -ceq 'tab_paid' -or $_ -ceq 'debt_erased' } {
            Invoke-Step 'n6-albin' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin')
            Invoke-Step 'n6-albin-again' 'kiosk_67_customer' 'refused:nothing_to_do' @('-nullrhi')
            Invoke-Step 'n6-petra' 'petra_kralj' 'stage_completed'
            Invoke-Step 'n6-albin-2' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin-2')
        }
        'object_sold' {
            Invoke-Step 'n6-albin' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin')
            Invoke-Choice 'n6-collector' 'stamp_collector' 'raeda_the_negative' 'buy_stamp' 'stage_completed'
            Invoke-Step 'n6-albin-stamp' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin-stamp')
        }
        'left_alone' {
            Invoke-Step 'n6-albin' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin')
            Invoke-Step 'n6-albin-again' 'kiosk_67_customer' 'refused:nothing_to_do' @('-nullrhi')
            Invoke-Step 'n6-boris' 'viole_terrace_regular' 'stage_completed' (Get-Shot 'n6-boris')
            Invoke-Step 'n6-albin-2' 'kiosk_67_customer' 'stage_completed' (Get-Shot 'n6-albin-2')
        }
    }
    Invoke-Step 'n6-ticket' 'nika_vidmar' 'mission_completed' (Get-Shot 'n6-ticket')
    Invoke-Step 'n6-again' 'nika_vidmar' 'refused:nothing_to_do' @('-nullrhi')
    Start-Sleep -Seconds 1
    Stop-GameServer

    'Phase 2: server restart on the same store'
    Start-GameServer (Join-Path $outDir 'server-2.log')
    Invoke-Step 'n6-after-restart' 'nika_vidmar' 'refused:nothing_to_do' @('-nullrhi')
    Invoke-Photo 'n6-yard-again' 'melje_date_board_view' 'refused:already_photographed' @('-nullrhi')
    Stop-GameServer
} finally {
    Stop-GameServer
}

# mb2.sh store-check ... > store-check.txt 2>&1: run as its own role so stdout and stderr both reach the file.
$storeCheckFile = Join-Path $outDir 'store-check.txt'
$storeCheck = Invoke-SmokeRole -Exe (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -Log $storeCheckFile -WorkDir $repoRoot `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'mb2.ps1'), 'store-check', 'negative', $store, $key, $other, $outcome)
Get-SmokeLogLines $storeCheckFile | ForEach-Object { "  store: $_" }
if ($storeCheck -ne 0) { $status = 1 }
$photos = Get-SmokeLogCount $serverLog1 'MB2INTERACT role=server event=photograph '
"  server: photographs committed=$photos (want 3)"
if ($photos -ne 3) { $status = 1 }

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck (Join-Path $outDir 'summary.txt') $Mode

if ($status -eq 0) {
    "Negative smoke PASS ($Mode, $outcome). Evidence: $outDir"
} else {
    [Console]::Error.WriteLine("Negative smoke FAIL ($Mode, $outcome). Evidence: $outDir")
}
exit $status
