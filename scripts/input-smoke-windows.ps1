# The Foto Vidmar investigation played with keyboard, mouse and gamepad input events (UI-02 input work), on /Game/World/lent
# against a real dedicated server, in a rendered client. Windows twin of input-smoke-linux.sh (same scenes, evidence
# lines and expectations). Two scenes of the existing story are the acceptance cases:
#
#   Chapter VI, the order-book page at the counter (raeda_the_negative, copy_order_book): left stick, mouse look, the
#            right stick aiming the camera until the prompt offers the photograph, the prompt's key following the device
#            (P / RT), RT taking the photograph; View and Y opening the Journal and the Inventory, B closing them.
#   Frame 37, the deduction at the light table (nika_frame_37, what_matters): A for the loupe; the card opened with A and
#            held (nothing sent), the D-pad and a pointer move switching its hints, B and Escape cancelling; a double-click
#            on a wrong answer; a held number key on the other wrong answer; the D-pad and A on the right answer.
#
# The story before each scene is played through the RPC drivers (scripts\negative-smoke-windows.ps1's steps, -nullrhi);
# each input scene is its own client process (UMarburgumInputSmokeSubsystem, -MB2InputSmoke). Between the light-table
# processes the world store the server commits is read: cancelling and the wrong answers must leave the stage, Photography
# XP, money and inventory exactly as they were, and the right answer must move the stage on once and add exactly 10
# Photography XP.
#
# Usage: scripts\input-smoke-windows.ps1 [staged|editor]
#   MB2_INPUT_PORT (default 27763; never 7777 or 17777), MB2_INPUT_CULTURE en|sl (default en),
#   MB2_INPUT_RES WIDTHxHEIGHT (default 1920x1080), MB2_INPUT_CLOCK (default Tuesday 10:00),
#   MB2_INPUT_CLIENT_ARGS extra options for the rendered clients (e.g. -sm5),
#   MB2_INPUT_SCENES all|counter|table (default all; one scene skips the other's input clients).
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = '27763'; if ($env:MB2_INPUT_PORT) { $port = $env:MB2_INPUT_PORT }
$culture = 'en'; if ($env:MB2_INPUT_CULTURE) { $culture = $env:MB2_INPUT_CULTURE }
$res = '1920x1080'; if ($env:MB2_INPUT_RES) { $res = $env:MB2_INPUT_RES }
$map = '/Game/World/lent'
$stamp = Get-UtcStamp
$key = "mb2i-$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$usage = 'Usage: scripts\input-smoke-windows.ps1 [staged|editor]'

# The bash order: the mode, then the port, resolution and culture checks, then the executables.
if ($Mode -cne 'staged' -and $Mode -cne 'editor') { Exit-Usage $usage }
if ($port -eq '7777' -or $port -eq '17777') {
    Exit-Usage "Port $port is the owner's playtest; pick another MB2_INPUT_PORT."
}
if ($res -cnotmatch '^([0-9]{3,4})x([0-9]{3,4})$') {
    Exit-Usage 'MB2_INPUT_RES must be WIDTHxHEIGHT, e.g. 1280x720.'
}
$resX = $Matches[1]
$resY = $Matches[2]
if ($culture -cne 'en' -and $culture -cne 'sl') { Exit-Usage 'MB2_INPUT_CULTURE must be en or sl.' }
$targets = Resolve-SmokeTargets $Mode $usage
$wall = 600; if ($env:MB2_CLIENT_WALL_SECONDS) { $wall = [int]$env:MB2_CLIENT_WALL_SECONDS }

$outDir = Join-Path $repoRoot "Saved\InputSmoke\$stamp-$culture-$res"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$store = Join-Path $outDir 'world-store.json'
$stateFile = Join-Path $outDir 'server-state.txt'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', "-ResX=$resX", "-ResY=$resY", "-culture=$culture")
$inputClientArgs = @()
if ($env:MB2_INPUT_CLIENT_ARGS) { $inputClientArgs = @($env:MB2_INPUT_CLIENT_ARGS -split '\s+' | Where-Object { $_ }) }
$server = $null
$status = 0

# Invoke-Client <label> <log tag> <start tag> [client args]: one client process; retried only when the GPU failed to start.
function Invoke-Client([string]$Label, [string]$Tag, [string]$Start, [string[]]$More) {
    $log = Join-Path $outDir "client-$Label.log"
    $code = 0
    foreach ($attempt in 0, 1, 2) {
        if ($attempt -gt 0) {
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client-$Label.gpu-init-failed-$attempt.log") -Force
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log `
            -Arguments (@("127.0.0.1:${port}?MB2Start=${Start}?MB2Character=$key") + $commonArgs + $More)
    }
    if ($code -eq 0 -and (Test-SmokeLog $log "$Tag event=pass")) {
        "  client ${Label}: PASS"
    } else {
        "  client ${Label}: FAIL (exit $code)"
        Get-SmokeLogMatches $log "$Tag event=(fail|expectation_failed)" 'MB2' '    '
        $script:status = 1
    }
}

# Invoke-Step <label> <target> <expected effect>: the story's RPC driver, no renderer.
function Invoke-Step([string]$Label, [string]$Target, [string]$Expect) {
    Invoke-Client $Label 'MB2MISSION role=client tag=STEP' "mission_$Target" @('-nullrhi', '-MB2MissionSmoke', '-MB2MissionRole=STEP',
        '-MB2MissionTimeout=120', "-MB2MissionTarget=$Target", "-MB2MissionExpect=$Expect")
}
function Invoke-Photo([string]$Label, [string]$Target, [string]$Expect) {
    Invoke-Client $Label 'MB2MISSION role=client tag=PHOTO' "photo_$Target" @('-nullrhi', '-MB2MissionSmoke', '-MB2MissionRole=PHOTO',
        '-MB2MissionTimeout=120', "-MB2MissionTarget=$Target", "-MB2MissionExpect=$Expect")
}
# Invoke-Play <label> <role> <start tag> [input args]: a rendered client driven by input events.
function Invoke-Play([string]$Label, [string]$Role, [string]$Start, [string[]]$More = @()) {
    Invoke-Client $Label "MB2INPUT role=client tag=$Role" $Start ($renderArgs + $inputClientArgs + @('-MB2InputSmoke',
        "-MB2InputRole=$Role", '-MB2InputTimeout=150', "-MB2InputShot=$(Join-Path $outDir $Label)") + $More)
}

# Save-Snapshot <label>: what the server has committed for the character, one line (stage, XP, money, items, revision).
function Save-Snapshot([string]$Label) {
    $out = @(Invoke-Mb2 store-check snapshot $store $key)
    if ($LASTEXITCODE) { $script:status = 1 }
    $line = "${Label}: $(($out | ForEach-Object { "$_" }) -join "`n")"
    Add-SmokeFile $stateFile @($line)
    $line
}

function Get-StateLine([string]$Label) {
    $regex = New-Object System.Text.RegularExpressions.Regex('^' + [regex]::Escape($Label) + ':')
    $last = ''
    foreach ($l in @(Get-SmokeLogLines $stateFile)) { if ($regex.IsMatch($l)) { $last = $l } }
    $last
}

# Test-Same <a> <b>: the two snapshots agree on everything but the store revision.
function Test-Same([string]$A, [string]$B) {
    $va = (Get-StateLine $A) -replace '^[^:]*: ', '' -replace ' revision=.*', ''
    $vb = (Get-StateLine $B) -replace '^[^:]*: ', '' -replace ' revision=.*', ''
    if ($va -ceq $vb) {
        "  server state: $B = $A (progress, XP, money and inventory unchanged)"
    } else {
        "  server state: $B differs from $A"
        "    ${A}: $va"
        "    ${B}: $vb"
        $script:status = 1
    }
}

# Get-Field <snapshot label> <field>: the value of field=value in the last snapshot with that label.
function Get-Field([string]$Label, [string]$Name) {
    $line = Get-StateLine $Label
    $values = @([regex]::Matches($line, [regex]::Escape($Name) + '=[^ ]*') | ForEach-Object { ($_.Value -split '=')[1] })
    $values -join "`n"
}

function ConvertTo-Number([string]$Value) { $n = 0L; [void][long]::TryParse($Value, [ref]$n); $n }

# The Windows twin of seed-chapter-vi-store.sh (stdout dropped, as > /dev/null there; a failure stops the run).
$seedScript = Join-Path $PSScriptRoot 'seed-chapter-vi-store.ps1'
if (-not (Test-Path -LiteralPath $seedScript -PathType Leaf)) {
    Exit-Failure "Missing $seedScript (the Windows twin of seed-chapter-vi-store.sh)"
}
$seedOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $seedScript $store "${key}:object_sold"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$seedOut = $null

try {
    "Input smoke ($Mode) on $map, port $port, culture $culture, $res; evidence in $outDir"
    $serverLog = Join-Path $outDir 'server.log'
    $clock = '2026-06-16T08:00:00Z'; if ($env:MB2_INPUT_CLOCK) { $clock = $env:MB2_INPUT_CLOCK }
    $server = Start-SmokeServer -Target $targets.Server -Log $serverLog -Port ([int]$port) -PortVariable 'MB2_INPUT_PORT' `
        -ReadySeconds 180 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", "-MB2Clock=$clock", '-MB2ClockSpeed=0.001') + $commonArgs)
    [void](Wait-SmokeLog $serverLog 'event=content_ready' 20 $null 0.25)

    'Chapter VI up to the order book (RPC drivers)'
    Invoke-Step 'n6-offer' 'nika_vidmar' 'mission_started'
    Invoke-Step 'n6-date' 'nika_vidmar' 'stage_completed'
    Invoke-Step 'n6-book' 'foto_vidmar_order_book' 'stage_completed'
    Save-Snapshot 'before-page'

    $scenes = 'all'; if ($env:MB2_INPUT_SCENES) { $scenes = $env:MB2_INPUT_SCENES }
    'Scene 1: the order-book page at the counter (input events)'
    if ($scenes -ceq 'table') {
        Invoke-Photo 'n6-page' 'foto_vidmar_order_book_page' 'stage_completed'
    } else {
        Invoke-Play 'counter' 'COUNTER' 'mission_foto_vidmar_order_book' @('-MB2InputTarget=foto_vidmar_order_book_page')
        Save-Snapshot 'after-page'
        if ((Get-Field 'after-page' 'negative') -cne (Get-Field 'before-page' 'negative')) {
            "  server state: the page photograph moved Chapter VI on ($(Get-Field 'before-page' 'negative') -> $(Get-Field 'after-page' 'negative'))"
        } else {
            '  server state: the page photograph did not move Chapter VI on'
            $status = 1
        }
    }
    if ($scenes -ceq 'counter') {
        Stop-SmokeRole $server
        $server = $null
        if ($status -eq 0) { 'Input smoke PASS (counter only)' } else { Write-Warn 'Input smoke FAIL (counter only)' }
        exit $status
    }

    'Chapter VI to the light table (RPC drivers)'
    Invoke-Step 'n6-drago' 'drago_potocnik' 'stage_completed'
    Invoke-Step 'n6-ivana' 'ivana_zupan' 'stage_completed'
    Invoke-Step 'n6-cage' 'block_13_storage_cage' 'stage_completed'
    Invoke-Photo 'n6-sheet' 'zupan_contact_sheet_copy' 'stage_completed'
    Invoke-Step 'f37-offer' 'nika_vidmar' 'mission_started'

    'Scene 2: Frame 37 at the light table (input events)'
    Invoke-Play 'loupe' 'LOUPE' 'mission_foto_vidmar_light_table' @('-MB2InputTarget=foto_vidmar_light_table')
    Save-Snapshot 'deduction-due'
    if ((Get-Field 'deduction-due' 'frame_37') -cne 'what_matters') { '  server state: Frame 37 is not at the deduction'; $status = 1 }
    Invoke-Play 'cancel' 'CANCEL' 'mission_foto_vidmar_light_table' @('-MB2InputTarget=foto_vidmar_light_table')
    Save-Snapshot 'after-cancel'
    Test-Same 'deduction-due' 'after-cancel'
    Invoke-Play 'mouse-wrong' 'MOUSEWRONG' 'mission_foto_vidmar_light_table' @('-MB2InputTarget=foto_vidmar_light_table', '-MB2InputWrong=the_man')
    Save-Snapshot 'after-mouse-wrong'
    Test-Same 'deduction-due' 'after-mouse-wrong'
    Invoke-Play 'number-wrong' 'NUMBERWRONG' 'mission_foto_vidmar_light_table' @('-MB2InputTarget=foto_vidmar_light_table', '-MB2InputWrong=the_light_leak')
    Save-Snapshot 'after-number-wrong'
    Test-Same 'deduction-due' 'after-number-wrong'
    Invoke-Play 'pad-right' 'PADRIGHT' 'mission_foto_vidmar_light_table' @('-MB2InputTarget=foto_vidmar_light_table', '-MB2InputRight=the_place')
    Save-Snapshot 'after-right'
    $xpBefore = Get-Field 'deduction-due' 'photography_xp'
    $xpAfter = Get-Field 'after-right' 'photography_xp'
    $stageAfter = Get-Field 'after-right' 'frame_37'
    if ($stageAfter -ceq 'nika_prints' -and ((ConvertTo-Number $xpAfter) - (ConvertTo-Number $xpBefore)) -eq 10) {
        "  server state: the right answer moved Frame 37 on once (what_matters -> $stageAfter) and added exactly 10 Photography XP ($xpBefore -> $xpAfter)"
    } else {
        "  server state: after the right answer frame_37=$stageAfter photography_xp $xpBefore -> $xpAfter (want nika_prints and +10)"
        $status = 1
    }
    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}

# The server's own record of every answer: one line per request, with what it decided.
$answerRegex = New-Object System.Text.RegularExpressions.Regex('.*event=interaction .* outcome=([a-z_]*) reason=([a-z_]*) effect=([a-z_]*) mission=([a-z_0-9]*) stage=([a-z_]*) .* choice=([a-z_-]*) .* xp=(.*)$')
$answerLines = @(Get-SmokeLogLines $serverLog | Where-Object { $_.Contains('role=server event=interaction') -and $_.Contains('target=foto_vidmar_light_table') } |
    ForEach-Object { $answerRegex.Replace($_, '  server: light table outcome=$1 reason=$2 effect=$3 stage=$5 choice=$6 xp=$7', 1) })
$answerLines
Write-SmokeFile (Join-Path $outDir 'server-answers.txt') $answerLines
$answers = @($answerLines | Where-Object { $_.Contains('choice=the_') }).Count
"  server: answers received=$answers (want 3: the_man, the_light_leak, the_place; the cancels and held keys send none)"
if ($answers -ne 3) { $status = 1 }
$photos = Get-SmokeLogCount $serverLog 'MB2INTERACT role=server event=photograph '
"  server: photographs committed=$photos (want 2: the page by RT, the contact sheet by the RPC driver)"
if ($photos -ne 2) { $status = 1 }

Add-BuildStampCheck (Join-Path $outDir 'summary.txt') $Mode
if ($status -eq 0) {
    "Input smoke PASS ($Mode, $culture, $res). Evidence: $outDir"
} else {
    Write-Warn "Input smoke FAIL ($Mode, $culture, $res). Evidence: $outDir"
}
exit $status
