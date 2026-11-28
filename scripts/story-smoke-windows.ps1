# Windows twin of story-smoke-linux.sh (same phases, evidence lines and expectations).
# Chapter IV, its Maja branch, Photography and the two photography side missions (ADR-081), played on /Game/World/lent
# against a real dedicated server through the same RPCs the E and P keys and the choice panel use.
#
# The world store is seeded (schema 5, revision 1) with one character who has finished the prologue and Chapters I-III
# and told Maja in Chapter I (`maja_informed`), plus a second character with nothing. Each step is its own client
# process, so every step is also a disconnect and a reconnect in the middle of a mission.
#
#   Phase 1  Chapter IV from Vera to the decision at Tone's cab (ask_tone); Maja's folder (outcome gate); Nika lends the
#            camera; the Water Tower, the Judgement Tower and the bridge photographed through the server's frame check;
#            Nika's darkroom; Brane's signs including the Prehod sign on the Melje floor; a side photograph (the synagogue)
#            and the same again refused. Two characters talk to Nika at the same moment and both start her mission.
#   Phase 2  the server is restarted on the same store: the CHECK client finds every mission, outcome and skill restored,
#            and `mb2 store-check story` reads the document itself.
#
# Usage: scripts\story-smoke-windows.ps1 [staged|editor]   (MB2_STORY_PORT, default 27731; never 7777 or 17777;
#        MB2_STORY_CLOCK the server's world clock, default Tuesday 10:00; MB2_STORY_CLIENT_ARGS extra client options,
#        e.g. -culture=sl to play the whole sequence in Slovene)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Mode) { $Mode = 'staged' }
$usage = 'Usage: scripts\story-smoke-windows.ps1 [staged|editor]'
if ($Mode -ne 'staged' -and $Mode -ne 'editor') { Exit-Usage $usage }
$port = 27731; if ($env:MB2_STORY_PORT) { $port = [int]$env:MB2_STORY_PORT }
$map = '/Game/World/lent'
$key = "mb2s-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$other = "mb2s-o$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
if ($Mode -eq 'editor' -and -not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }
if ($port -eq 7777 -or $port -eq 17777) { Exit-Usage "Port $port is the owner's playtest; pick another MB2_STORY_PORT." }
$targets = Resolve-SmokeTargets $Mode $usage
$wall = 1200; if ($env:MB2_CLIENT_WALL_SECONDS) { $wall = [int]$env:MB2_CLIENT_WALL_SECONDS }

$outDir = New-SmokeOutDir 'StorySmoke'
$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$clientExtra = @(); if ($env:MB2_STORY_CLIENT_ARGS) { $clientExtra = @($env:MB2_STORY_CLIENT_ARGS -split '\s+' | Where-Object { $_ }) }
$script:server = $null
$script:status = 0

function Start-Server([string]$Log) {
    # A working morning by default (Tuesday 10:00): named people are at their authored posts (ADR-081) and every
    # counter is open. Set MB2_STORY_CLOCK to play the same sequence at another hour.
    $clock = '2026-06-16T08:00:00Z'; if ($env:MB2_STORY_CLOCK) { $clock = $env:MB2_STORY_CLOCK }
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_STORY_PORT' `
        -ReadySeconds 180 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", "-MB2Clock=$clock", '-MB2ClockSpeed=0.001') + $commonArgs)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

function Get-ClientArgs([string]$Role, [string]$Start, [string]$Who, [string[]]$More) {
    @("127.0.0.1:${port}?MB2Start=${Start}?MB2Character=$Who") + $commonArgs + $clientExtra + $More +
        @('-MB2MissionSmoke', "-MB2MissionRole=$Role", '-MB2MissionTimeout=120')
}

# The verdict line for one client (and the status), as run() prints it.
function Write-Verdict([string]$Label, [string]$Role, [int]$Code) {
    $log = Join-Path $outDir "client-$Label.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2MISSION role=client tag=$Role event=pass")) {
        "  client ${Label}: PASS"
    } else {
        "  client ${Label}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2MISSION role=client tag=$Role event=(fail|expectation_failed)" 'MB2MISSION' '    '
        $script:status = 1
    }
}

# run <label> <role> <start tag> <character> [client args...]; retried twice when the GPU device fails to come up.
function Invoke-Client([string]$Label, [string]$Role, [string]$Start, [string]$Who, [string[]]$More = @(), [int]$FirstCode = -999) {
    $log = Join-Path $outDir "client-$Label.log"
    $arguments = Get-ClientArgs $Role $Start $Who $More
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client-$Label.gpu-init-failed-$attempt.log") -Force
            Start-Sleep -Seconds 3
            $code = 0
        }
        if ($attempt -eq 0 -and $FirstCode -ne -999) { $code = $FirstCode; continue }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log -Arguments $arguments
    }
    Write-Verdict $Label $Role $code
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2MissionShot=$(Join-Path $outDir $Name)") }

# step <label> <target> <expected effect> [client args...]
function Invoke-Step([string]$Label, [string]$Target, [string]$Expect, [string[]]$More = @()) {
    Invoke-Client $Label 'STEP' "mission_$Target" $key (@("-MB2MissionTarget=$Target", "-MB2MissionExpect=$Expect") + $More)
}

# photo <label> <subject> <expected effect or refused:reason> [client args...]
function Invoke-Photo([string]$Label, [string]$Subject, [string]$Expect, [string[]]$More = @()) {
    Invoke-Client $Label 'PHOTO' "photo_$Subject" $key (@("-MB2MissionTarget=$Subject", "-MB2MissionExpect=$Expect") + $More)
}

# choice <label> <target> <mission> <choice>
function Invoke-Choice([string]$Label, [string]$Target, [string]$Mission, [string]$Choice) {
    Invoke-Client $Label 'CHOICE' "mission_$Target" $key ((Get-Shot $Label) + @("-MB2MissionTarget=$Target", "-MB2MissionId=$Mission", "-MB2MissionChoice=$Choice"))
}

function Get-DoneMission([string]$Id, [string]$Stage, [string]$More = '') {
    '"{0}":{{"completed":true,"reward_paid":true,"stage":"{1}","started_revision":1{2}}}' -f $Id, $Stage, $More
}

$missions = @(
    (Get-DoneMission 'lent_first_catch' 'meet_vera' ',"journal_entry":"blue_ledger_r90"')
    (Get-DoneMission 'raeda_names_no_longer_match' 'decide_maja' ',"choice":"tell_maja","outcome":"maja_informed"')
    (Get-DoneMission 'raeda_last_shift' 'show_vera')
    (Get-DoneMission 'raeda_city_for_sale' 'decide_milan' ',"choice":"trust_milan","outcome":"milan_trusted"')
) -join ','
Write-SmokeText $store (('{{"format":"marburgumii.world_store","schema_version":5,"revision":1,"stocks":{{}},"inventories":{{}},"missions":{{"{0}":{{{1}}}}},"conditions":{{}},"skills":{{"{0}":{{"reputation":40}}}},"money":{{"{0}":120}},"receipts":{{}}}}' -f $key, $missions) + "`n")

try {
    "Phase 1: dedicated server ($Mode) on $map, port $port, seeded store (Chapters I-III done, maja_informed); evidence in $outDir"
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log
    [void](Wait-SmokeLog $server1Log 'event=content_ready' 20 -PollSeconds 0.25)
    $spawned = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=content_spawned'
    $skipped = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=content_skipped'
    $photoStarts = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=photo_start'
    $unplaced = (@(Get-SmokeLogLines $server1Log | Where-Object { $_ -cmatch 'MB2INTERACT role=server event=photo_unplaced' } |
        ForEach-Object { if ($_ -cmatch 'id=([a-z0-9_]*)') { $Matches[1] } else { $_ } }) | ForEach-Object { "$_ " }) -join ''
    "  server: authored content spawned=$spawned skipped=$skipped; photo starts=$photoStarts unplaced=[ $unplaced]"
    if (-not ($spawned -ge 33 -and $skipped -eq 0 -and $photoStarts -ge 9)) { $script:status = 1 }

    # The angler is a person with a rod now; a STEP there checks the body as well as the line (it has nothing to offer).
    Invoke-Client 'angler-look' 'STEP' 'angler' $other ((Get-Shot 'angler') + @('-MB2MissionTarget=lent_angler', '-MB2MissionExpect=mission_started'))

    # Chapter IV.
    Invoke-Step 'ch4-offer' 'vera_horvat' 'mission_started' (Get-Shot 'ch4-offer')
    Invoke-Step 'ch4-recorder' 'jozica_rozman' 'stage_completed' (Get-Shot 'ch4-recorder')
    Invoke-Step 'ch4-drago' 'drago_potocnik' 'stage_completed'
    Invoke-Step 'ch4-tone' 'tone_vogrin' 'stage_completed' (Get-Shot 'ch4-tone')
    Invoke-Step 'ch4-andrej' 'andrej_mlakar' 'stage_completed' (Get-Shot 'ch4-andrej')
    Invoke-Step 'ch4-vera' 'vera_horvat' 'stage_completed'
    Invoke-Choice 'ch4-decision' 'tone_vogrin' 'raeda_voices_on_tape' 'ask_tone'
    # Maja's folder: offered because Chapter I ended in maja_informed.
    Invoke-Step 'folder-offer' 'maja_koren' 'mission_started'
    Invoke-Step 'folder-archive' 'mariborski_glas_archive' 'stage_completed'
    Invoke-Step 'folder-maja' 'maja_koren' 'mission_completed'

    # Two characters at Nika at the same moment: each starts the mission, each with a camera. Both start together;
    # a GPU-init retry, if one is needed, then runs on its own as run() does.
    $nikaArgs = @('-nullrhi', '-MB2MissionTarget=nika_vidmar', '-MB2MissionExpect=mission_started')
    $nikaA = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir 'client-nika-a.log') -Arguments (Get-ClientArgs 'STEP' 'mission_nika_vidmar' $key $nikaArgs)
    $nikaB = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir 'client-nika-b.log') -Arguments (Get-ClientArgs 'STEP' 'mission_nika_vidmar' $other $nikaArgs)
    $codeA = Wait-SmokeRole $nikaA; $nikaA.Dispose()
    $codeB = Wait-SmokeRole $nikaB; $nikaB.Dispose()
    Invoke-Client 'nika-a' 'STEP' 'mission_nika_vidmar' $key $nikaArgs $codeA
    Invoke-Client 'nika-b' 'STEP' 'mission_nika_vidmar' $other $nikaArgs $codeB
    if (-not ((Test-SmokeLog (Join-Path $outDir 'client-nika-a.log') 'tag=STEP event=pass') -and
              (Test-SmokeLog (Join-Path $outDir 'client-nika-b.log') 'tag=STEP event=pass'))) { $script:status = 1 }

    Invoke-Photo 'photo-vodni' 'vodni_stolp' 'stage_completed' (Get-Shot 'photo-vodni')
    Invoke-Photo 'photo-sodni' 'sodni_stolp' 'stage_completed' (Get-Shot 'photo-sodni')
    Invoke-Photo 'photo-most' 'stari_most' 'stage_completed' (Get-Shot 'photo-most')
    Invoke-Step 'nika-prints' 'nika_vidmar' 'stage_completed'
    Invoke-Choice 'nika-darkroom' 'nika_vidmar' 'nika_then_and_now' 'darkroom_lesson'
    Invoke-Step 'brane-offer' 'brane_sign_painter' 'mission_started'
    Invoke-Photo 'photo-kvik' 'kvik_lettering' 'stage_completed'
    Invoke-Photo 'photo-kavarna' 'velika_kavarna_lettering' 'stage_completed'
    Invoke-Photo 'photo-prehod' 'prehod_sign_melje' 'stage_completed' (Get-Shot 'photo-prehod')
    Invoke-Step 'brane-prints' 'brane_sign_painter' 'stage_completed' (Get-Shot 'brane-prints')
    Invoke-Choice 'brane-settle' 'brane_sign_painter' 'brane_old_letters' 'give_copies'
    Invoke-Photo 'photo-sinagoga' 'sinagoga' 'photographed'
    Invoke-Photo 'photo-sinagoga-again' 'sinagoga' 'refused:already_photographed'
    Start-Sleep -Seconds 1
    Stop-Server

    'Phase 2: server restart on the same store'
    Start-Server (Join-Path $outDir 'server-2.log')
    Invoke-Client 'check' 'STORYCHECK' 'arrival' $key @('-nullrhi')
    Stop-Server
} finally {
    Stop-Server
}

# mb2 store-check story, stdout and stderr together in store-check.txt as 2>&1 does.
$storeCheckFile = Join-Path $outDir 'store-check.txt'
$storeCheck = Invoke-SmokeRole -Exe (Get-Command powershell).Source -Log $storeCheckFile -WorkDir $repoRoot `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repoRoot 'scripts\mb2.ps1'), 'store-check', 'story', $store, $key)
Get-SmokeLogLines $storeCheckFile | ForEach-Object { "  store: $_" }
if ($storeCheck -ne 0) { $script:status = 1 }
$photos = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=photograph '
"  server: photographs committed=$photos (want 7)"
if ($photos -ne 7) { $script:status = 1 }

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck (Join-Path $outDir 'summary.txt') $Mode

if ($script:status -eq 0) {
    "Story smoke PASS ($Mode). Evidence: $outDir"
} else {
    [Console]::Error.WriteLine("Story smoke FAIL ($Mode). Evidence: $outDir")
}
exit $script:status
