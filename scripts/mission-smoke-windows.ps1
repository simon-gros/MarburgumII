# Windows twin of mission-smoke-linux.sh (same phases, evidence lines and expectations).
# Missions, fishing and skills from the original game (ADR-047..049), end to end on /Game/World/lent.
#
# Phase 1 (fresh world store), one character, one client process per step, each starting at the
# development start beside its target (?MB2Start=mission_<id>; the angler's own start for fishing):
#   FISH    the angler starts "Something in the Water" with a rod; no fish yet is refused; a strike before
#           the bite loses the cast; a recast inside the interval is refused; one fish gets away; then a
#           bite, a strike and a catch with Fishing XP; showing it completes the first stage.
#   STEP    the debris (six clues), Petra Kralj, the service room (the brass key) and Vera Horvat (mission
#           completed: 50 money and the "Blue Ledger" journal entry); Petra then offers Kiosk 67, and its
#           four witnesses each complete a stage.
#   CHOICE  at the missing customer the plain interaction is refused (a decision is due); the decision
#           panel pays his tab: -20 money, Reputation +15, outcome tab_paid, mission completed.
# The server is stopped and the store checked (mb2 store-check missions).
# Phase 2 (same store, restarted): CHECK sees both missions completed, the decision, 30 money, Fishing and
# Reputation XP and the clues restored, and opens the Skills panel and the journal. The store must be
# byte-identical afterwards.
#
# FISH, CHOICE and CHECK render offscreen (screenshots); the steps run -nullrhi.
# Usage: scripts\mission-smoke-windows.ps1 [staged|editor]. Evidence: Saved\MissionSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27716; if ($env:MB2_MISSION_PORT) { $port = [int]$env:MB2_MISSION_PORT }
$map = '/Game/World/lent'
$key = "mb2q-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\mission-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'MissionSmoke'
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:server = $null
$script:status = 0

function Start-Server([string]$Log) {
    # A working morning by default (Tuesday 10:00): named people are at their authored posts (ADR-081) and every
    # counter is open. Set MB2_MISSION_CLOCK to play the same sequence at another hour.
    $clock = '2026-06-16T08:00:00Z'; if ($env:MB2_MISSION_CLOCK) { $clock = $env:MB2_MISSION_CLOCK }
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_MISSION_PORT' `
        -ReadySeconds 180 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", "-MB2Clock=$clock", '-MB2ClockSpeed=0.001') + $commonArgs)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

# run <label> <role> <start tag> <extra args>; output to client-<label>.log
function Invoke-Client([string]$Label, [string]$Role, [string]$Start, [string[]]$More = @()) {
    $url = "127.0.0.1:${port}?MB2Start=${Start}?MB2Character=$key"
    $log = Join-Path $outDir "client-$Label.log"
    $arguments = @($url) + $commonArgs + $More + @('-MB2MissionSmoke', "-MB2MissionRole=$Role", '-MB2MissionTimeout=120')
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client-$Label.gpu-init-failed-$attempt.log") -Force
            "  client ${Label}: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log -Arguments $arguments
    }
    if ($code -eq 0 -and (Test-SmokeLog $log "MB2MISSION role=client tag=$Role event=pass")) {
        "  client ${Label}: PASS"
    } else {
        "  client ${Label}: FAIL (exit $code)"
        Get-SmokeLogMatches $log "MB2MISSION role=client tag=$Role event=(fail|expectation_failed)" 'MB2MISSION' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2MissionShot=$(Join-Path $outDir $Name)") }

# step <target> <expected effect>
function Invoke-Step([string]$Target, [string]$Expect) {
    Invoke-Client "step-$Target" 'STEP' "mission_$Target" @('-nullrhi', "-MB2MissionTarget=$Target", "-MB2MissionExpect=$Expect")
}

# mv <client-x.log> <client-y.log> 2>/dev/null || true
function Rename-ClientLog([string]$From, [string]$To) {
    Move-Item -LiteralPath (Join-Path $outDir $From) -Destination (Join-Path $outDir $To) -Force -ErrorAction SilentlyContinue
}

function Get-StoreHash { if (Test-Path -LiteralPath $store -PathType Leaf) { Get-SmokeSha256 $store } else { '' } }

# scripts\mb2.ps1 <args> 2>&1: stdout and stderr lines together; the exit code is in $LASTEXITCODE.
function Invoke-Mb2Merged([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'scripts\mb2.ps1') @Arguments 2>&1 |
        ForEach-Object { "$_" }
}

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last evidence prefix.
function Get-CutMatches([string[]]$Path, [string]$Pattern, [string]$Prefixes) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cut = New-Object System.Text.RegularExpressions.Regex("^.*($Prefixes)")
    foreach ($line in @(Get-SmokeLogLines $Path)) { if ($regex.IsMatch($line)) { $cut.Replace($line, '$1', 1) } }
}

try {
    # ---------------------------------------------------------------- phase 1
    "Phase 1: dedicated server ($Mode) on $map, port $port, fresh store; evidence in $outDir"
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log
    # Spawn is logged after the ready line, and the dedicated-server log is block-buffered.
    [void](Wait-SmokeLog $server1Log 'event=content_ready' 20 -PollSeconds 0.25)
    $spawned = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=content_spawned'
    $skipped = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=content_skipped'
    "  server: authored content spawned=$spawned skipped=$skipped"
    if (-not ($spawned -ge 30 -and $skipped -eq 0)) { $script:status = 1 }

    Invoke-Client 'fish' 'FISH' 'angler' (Get-Shot 'fish')
    Invoke-Step 'drava_cashbox_debris' 'stage_completed'
    Invoke-Step 'petra_kralj' 'stage_completed'
    Invoke-Step 'raeda_service_room' 'stage_completed'
    Invoke-Step 'vera_horvat' 'mission_completed'
    # Names No Longer Match (ADR-050): Vera offers it only once the first catch is done.
    Invoke-Step 'vera_horvat' 'mission_started'
    Rename-ClientLog 'client-step-vera_horvat.log' 'client-step-vera_horvat-names.log'
    Invoke-Step 'raeda_directory_entry' 'stage_completed'
    Invoke-Step 'raeda_successor_sign' 'stage_completed'
    Invoke-Step 'marija_sever' 'stage_completed'
    Invoke-Step 'petra_kralj' 'stage_completed'
    Rename-ClientLog 'client-step-petra_kralj.log' 'client-step-petra_kralj-names.log'
    Invoke-Step 'mariborski_glas_archive' 'stage_completed'
    Invoke-Client 'names' 'CHOICE' 'mission_maja_koren' ((Get-Shot 'names') + @('-MB2MissionTarget=maja_koren',
        '-MB2MissionId=raeda_names_no_longer_match', '-MB2MissionChoice=tell_maja'))
    Invoke-Step 'petra_kralj' 'mission_started'
    Rename-ClientLog 'client-step-petra_kralj.log' 'client-step-petra_kralj-kiosk.log'
    Invoke-Step 'kiosk_67_bus_regular' 'stage_completed'
    Invoke-Step 'kiosk_67_cafe_regular' 'stage_completed'
    Invoke-Step 'kiosk_67_neighbour' 'stage_completed'
    Invoke-Step 'kiosk_67_customer' 'stage_completed'
    Invoke-Client 'choice' 'CHOICE' 'mission_kiosk_67_customer' ((Get-Shot 'choice') + @('-MB2MissionTarget=kiosk_67_customer',
        '-MB2MissionId=kiosk_67_customer_stopped_coming', '-MB2MissionChoice=pay_tab'))
    # The other three missions whose places stand on this map (ADR-051).
    Invoke-Step 'lent_dispatcher' 'mission_started'
    Invoke-Step 'lent_workshop_mechanic' 'stage_completed'
    Invoke-Client 'fee' 'CHOICE' 'mission_lent_workshop_mechanic' ((Get-Shot 'fee') + @('-MB2MissionTarget=lent_workshop_mechanic',
        '-MB2MissionId=lent_late_delivery', '-MB2MissionChoice=leave_clerk_share'))
    Invoke-Step 'matej_hall_three' 'mission_started'
    Invoke-Step 'hall_three_machine_plate' 'stage_completed'
    Invoke-Step 'hall_three_labelled_boxes' 'stage_completed'
    Invoke-Step 'hall_three_worker' 'stage_completed'
    Invoke-Step 'hall_three_key_hook' 'stage_completed'
    Invoke-Step 'hall_three_night_guard' 'stage_completed'
    Invoke-Step 'hall_three_steel_cabinet' 'stage_completed'
    Invoke-Client 'hall' 'CHOICE' 'mission_matej_hall_three' ((Get-Shot 'hall') + @('-MB2MissionTarget=matej_hall_three',
        '-MB2MissionId=hall_three_last_shift', '-MB2MissionChoice=negotiate_settlement'))
    Invoke-Step 'under_paint_new_owner' 'mission_started'
    Invoke-Step 'under_paint_layer_one' 'stage_completed'
    Invoke-Step 'under_paint_layer_two' 'stage_completed'
    Invoke-Step 'under_paint_layer_three' 'stage_completed'
    Invoke-Step 'under_paint_layer_four' 'stage_completed'
    Invoke-Step 'brane_sign_painter' 'stage_completed'
    Invoke-Step 'maja_koren' 'stage_completed'
    Invoke-Step 'petra_kralj' 'stage_completed'
    Rename-ClientLog 'client-step-petra_kralj.log' 'client-step-petra_kralj-paint.log'
    Invoke-Client 'sign' 'CHOICE' 'mission_under_paint_new_owner' ((Get-Shot 'sign') + @('-MB2MissionTarget=under_paint_new_owner',
        '-MB2MissionId=under_the_paint_four_names', '-MB2MissionChoice=preserve_layers'))
    # The Melje yard stands on a temporary pad (ADR-052); the Tabor trail on the Tabor window's own ground (ADR-090).
    Invoke-Step 'matej_hall_three' 'mission_started'
    Invoke-Step 'melje_yard_gate' 'stage_completed'
    Invoke-Step 'melje_dispatch_lockers' 'stage_completed'
    Invoke-Step 'melje_kainach_trucks' 'stage_completed'
    Invoke-Step 'melje_vehicle_log_shelf' 'stage_completed'
    Invoke-Step 'melje_archive_boxes' 'stage_completed'
    Invoke-Step 'matej_hall_three' 'stage_completed'
    Invoke-Step 'vera_horvat' 'mission_completed'
    Invoke-Step 'vera_horvat' 'mission_started'
    Invoke-Step 'tabor_exchange_counter' 'stage_completed'
    Invoke-Step 'irena_vogrin' 'stage_completed'
    Invoke-Step 'mariborski_glas_archive' 'stage_completed'
    Invoke-Step 'pohorska_bank_archive_desk' 'stage_completed'
    Invoke-Step 'milan_solar' 'stage_completed'
    Invoke-Step 'petra_kralj' 'stage_completed'
    Invoke-Client 'milan' 'CHOICE' 'mission_milan_solar' ((Get-Shot 'milan') + @('-MB2MissionTarget=milan_solar',
        '-MB2MissionId=raeda_city_for_sale', '-MB2MissionChoice=trust_milan'))
    Start-Sleep -Seconds 1
    Stop-Server

    $storeCheckFile = Join-Path $outDir 'store-check.txt'
    $storeCheckOut = @(Invoke-Mb2Merged @('store-check', 'missions', $store, $key))
    $storeCheck = $LASTEXITCODE
    Write-SmokeFile $storeCheckFile $storeCheckOut
    $storeCheckOut | ForEach-Object { "  store: $_" }
    if ($storeCheck -ne 0) { $script:status = 1 }
    $storeHash = Get-StoreHash
    $casts = Get-SmokeLogCount $server1Log 'MB2FISH role=server event=cast '
    $bites = Get-SmokeLogCount $server1Log 'MB2FISH role=server event=bite '
    $ends = -join @(Get-SmokeLogLines $server1Log | Where-Object { $_ -cmatch 'MB2FISH role=server event=cast_end' } |
        ForEach-Object { ([regex]::Replace($_, '.* end=([a-z_]*).*', '$1')) + ' ' })
    "  fishing: casts=$casts bites=$bites ends=[ $ends] (want 3 casts, 2 bites, too_early escaped caught)"
    if (-not ($casts -eq 3 -and $bites -eq 2 -and $ends -ceq 'too_early escaped caught ')) { $script:status = 1 }

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server2Log = Join-Path $outDir 'server-2.log'
    Start-Server $server2Log
    Invoke-Client 'check' 'CHECK' 'arrival' (Get-Shot 'check')
    Start-Sleep -Seconds 1
    Stop-Server
} finally {
    Stop-Server
}
# Counted once the server has exited: the dedicated-server log is block-buffered, so store_loaded
# may not be on disk yet when the ready line is.
$loaded = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=store_loaded .* schema=7 '
$storeHash2 = Get-StoreHash
$unchanged = 'NO'; if ($storeHash -eq $storeHash2) { $unchanged = 'yes' }
"  server phase 2: store_loaded(schema 7)=$loaded (want 1); store unchanged: $unchanged"
if (-not ($loaded -eq 1 -and $storeHash -eq $storeHash2)) { $script:status = 1 }

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII missions, fishing and skills smoke (ADR-047..049)'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content', 'Data'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "character: $key"
    "content: spawned=$spawned skipped=$skipped"
    "fishing: casts=$casts bites=$bites ends=$ends"
    "store_sha256: $storeHash"
    ''
) + @(Get-SmokeLogLines $storeCheckFile | ForEach-Object { "store: $_" }) + @('') +
    @(Get-CutMatches (Join-Path $outDir '*.log') 'MB2MISSION|MB2FISH|MB2INTERACT role=server event=(interaction|store_loaded)' 'MB2MISSION|MB2FISH|MB2INTERACT'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
