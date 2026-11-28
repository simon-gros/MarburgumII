# Windows twin of ui-capture-linux.sh (same scenarios, flags, evidence lines and exit codes).
# Stage 0C rendered-client UI captures: real MarburgumIIClient processes rendering offscreen on the
# GPU, screenshotting the composited window (UMG included) at each settled session phase.
#
#   frontend  connection shell, idle, at several resolutions
#   gallery   UI-01 component gallery (labeled SAMPLE DATA)
#   fail      connect to a port with no server: truthful error state
#   recover   join a packaged dedicated server, stop the server while admitted, restart it, and
#             expect the client to show reconnecting and recover to admission on its own
#   tour      join a dedicated server on /Game/World/lent (own throwaway world store) at the riverside angler
#             and walk the live session's surfaces: location banner, quiet HUD, E on the angler (receipt,
#             pinned objective), Character, Skills, Inventory, Journal, Settings, general chat, Escape menu
#   matrix    the same live session at every resolution and UI scale of the UI-07 matrix (720p to 4K,
#             ultrawide, 5:4; 80 % to 200 %), screenshotting the HUD and each management screen
#
# Usage: scripts\ui-capture-windows.ps1 [staged|editor] [scenario,scenario,...]
#   staged  packaged builds from scripts\package-windows.ps1 (default)
#   editor  uncooked UnrealEditor-Cmd -server / -game processes (requires UE_ROOT)
#   scenarios default to frontend,gallery,fail,recover; "all" adds tour and matrix.
#   MB2_CAPTURE_MATRIX="1920x1080@1.0 1280x720@1.5 ..." narrows the matrix.
#   MB2_CAPTURE_MATRIX_TOUR="explore,interact,equip:right_hand:fishing_rod,character:right_hand,..." replaces the matrix's
#   tour steps (character:<slot>, inventory:<item> and equip:<slot>:<item> included, ADR-085; settings:graphics opens
#   Settings on the DLSS 5 neural rendering row).
#   MB2_CAPTURE_CULTURE=sl renders every client in Slovene (-culture=sl; QA U11), with "-sl" in the evidence folder.
#   The live server's clock and weather are held (MB2_CAPTURE_CLOCK, default a June afternoon, clear_dry), so before
#   and after captures compare the interface and not the hour they were taken.
# Evidence (PNGs, logs, summary.txt) is written to Saved\UICapture\<UTC timestamp>\.
param([string]$Mode = 'staged', [string]$Scenarios = '')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

if (-not $Mode) { $Mode = 'staged' }
$repoRoot = Get-RepoRoot
if (-not $Scenarios) { $Scenarios = 'frontend,gallery,fail,recover' }
$scenarioList = ",$Scenarios,"
if ($scenarioList -ceq ',all,') { $scenarioList = ',frontend,gallery,fail,recover,tour,matrix,' }
function Test-Wants([string]$Name) { $scenarioList.Contains(",$Name,") }
$port = 27703; if ($env:MB2_CAPTURE_PORT) { $port = [int]$env:MB2_CAPTURE_PORT }
$deadPort = 27704; if ($env:MB2_CAPTURE_DEAD_PORT) { $deadPort = [int]$env:MB2_CAPTURE_DEAD_PORT }
$map = '/Game/Maps/NetSmoke'
$worldMap = '/Game/World/lent'
$captureClock = '2026-06-18T12:30:00Z'; if ($env:MB2_CAPTURE_CLOCK) { $captureClock = $env:MB2_CAPTURE_CLOCK }
# UI-07 matrix: resolution@scale. 1280x1024 is the 5:4 case.
$matrix = '1280x720@0.8 1280x720@1.0 1280x720@1.25 1280x720@1.5 1366x768@1.0 1920x1080@1.0 1920x1080@1.25 1920x1080@1.5 1920x1080@2.0 2560x1440@1.0 2560x1440@1.5 3440x1440@1.0 3840x2160@1.0 3840x2160@2.0 1280x1024@1.0 1280x1024@1.5'
if ($env:MB2_CAPTURE_MATRIX) { $matrix = $env:MB2_CAPTURE_MATRIX }
$captureCulture = "$env:MB2_CAPTURE_CULTURE"
$cultureArgs = @()
if ($captureCulture) { $cultureArgs = @("-culture=$captureCulture") }
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\ui-capture-windows.ps1 [staged|editor]'
$wall = Get-ClientWallSeconds

# The bash runner always writes to Saved/UICapture (it does not read MB2_SMOKE_DIR).
$cultureSuffix = ''
if ($captureCulture) { $cultureSuffix = "-$captureCulture" }
$outDir = Join-Path $repoRoot ('Saved\UICapture\' + (Get-UtcStamp) + $cultureSuffix)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$commonArgs = @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput')

# Start-Server <log> <map> <server args>; same refusal / ready wait as the other runners, 180 s.
function Start-Server([string]$Log, [string]$ServerMap, [string[]]$More = @()) {
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_CAPTURE_PORT' -ReadySeconds 180 `
        -Arguments (@($ServerMap, "-port=$port") + $commonArgs + @($More))
}

# Before / after the last character: ${v%x*} and ${v#*x}.
function Get-Before([string]$Value, [string]$Sep) { $i = $Value.LastIndexOf($Sep); if ($i -lt 0) { $Value } else { $Value.Substring(0, $i) } }
function Get-After([string]$Value, [string]$Sep) { $i = $Value.IndexOf($Sep); if ($i -lt 0) { $Value } else { $Value.Substring($i + 1) } }

# Invoke-WorldClient <name> <width> <height> <scale> <tour steps> [extra args]: a client joining the world at the
# riverside angler, walking the tour. A second offscreen client can fail to create its GPU device while another
# renders on the same GPU; a client that fails that way before connecting is retried.
function Invoke-WorldClient([string]$Name, [string]$Width, [string]$Height, [string]$Scale, [string]$Steps, [string[]]$More = @()) {
    # A character key allows a-z, 0-9, - and _ (no dots from the scale), at most 40 characters.
    $url = "127.0.0.1:${port}?MB2Start=angler?MB2Character=mb2ui-$(Get-UnixSeconds)-$(Get-Random -Maximum 32768)"
    $log = Join-Path $outDir "client-$Name.log"
    $code = 0
    foreach ($attempt in 0, 1, 2) {
        if ($attempt -gt 0) {
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -Force -LiteralPath $log -Destination (Join-Path $outDir "client-$Name.gpu-init-failed-$attempt.log")
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log -Arguments (@($url) + $commonArgs + @(
                '-RenderOffScreen', '-windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height", "-MB2UIScale=$Scale",
                "-MB2UICapture=$outDir", '-MB2UICaptureScript=tour', "-MB2UICaptureLabel=$Name", "-MB2UITour=$Steps") +
            $cultureArgs + @($More))
    }
    $code
}

# Start-Client <name> <width> <height> <script> [extra args]: the started role (client-<name>.log).
function Start-Client([string]$Name, [string]$Width, [string]$Height, [string]$CaptureScript, [string[]]$More = @()) {
    # Front-end is no longer the game default (ADR-025). These captures still open it by name.
    Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir "client-$Name.log") -Arguments (
        @('/Game/Maps/FrontEnd') + $commonArgs + @('-RenderOffScreen', '-windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
            "-MB2UICapture=$outDir", "-MB2UICaptureScript=$CaptureScript") + $cultureArgs + @($More))
}

# Invoke-Client <name> <width> <height> <script> [extra args]; returns the client's exit status.
function Invoke-Client([string]$Name, [string]$Width, [string]$Height, [string]$CaptureScript, [string[]]$More = @()) {
    $role = Start-Client $Name $Width $Height $CaptureScript $More
    try { Wait-SmokeRole $role } finally { $role.Dispose() }
}

$script:status = 0
function Write-Record([string]$Name, [int]$Code, [int]$Expected = 0) {
    if ($Code -eq $Expected) {
        "  ${Name}: PASS"
    } else {
        "  ${Name}: FAIL (exit $Code, expected $Expected)"
        $script:status = 1
    }
}

# nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null || echo unknown
function Get-GpuLine {
    $ErrorActionPreference = 'Continue'
    try {
        $out = @(& nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>$null | ForEach-Object { "$_" })
        if ($LASTEXITCODE -eq 0) { return ($out -join "`n") }
        return (@($out + 'unknown') -join "`n")
    } catch { 'unknown' }
}

"UI captures ($Mode); evidence in $outDir"
if (Test-UdpPortInUse $deadPort) {
    Exit-Failure "UDP port $deadPort must be unused for the failure capture; set MB2_CAPTURE_DEAD_PORT."
}

$server = $null
try {
    if (Test-Wants 'frontend') {
        'Front-end shell'
        foreach ($resolution in '1920x1080', '1280x720', '2560x1440', '3840x2160') {
            $code = Invoke-Client "frontend-$resolution" (Get-Before $resolution 'x') (Get-After $resolution 'x') 'frontend'
            Write-Record "frontend $resolution" $code
        }
    }

    if (Test-Wants 'gallery') {
        'Component gallery (sample data)'
        foreach ($resolution in '1920x1080', '2560x1440') {
            $code = Invoke-Client "gallery-$resolution" (Get-Before $resolution 'x') (Get-After $resolution 'x') 'frontend' @('-MB2UIGallery', '-MB2UICaptureLabel=gallery')
            Write-Record "gallery $resolution" $code
        }
    }

    if (Test-Wants 'fail') {
        "Connection failure (no server on port $deadPort)"
        $code = Invoke-Client 'fail' '1920' '1080' 'fail' @("-MB2Server=127.0.0.1:$deadPort")
        Write-Record 'fail' $code
    }

    if (Test-Wants 'recover') {
        "Join, server loss and automatic recovery (port $port)"
        $server = Start-Server (Join-Path $outDir 'server-1.log') $map
        $recoverClient = Start-Client 'recover' '1920' '1080' 'recover' @("-MB2Server=127.0.0.1:$port")
        $recoverLog = Join-Path $outDir 'client-recover.log'
        if (Wait-SmokeLog $recoverLog 'stage=await_loss' 90 $null 0.25) {
            '  client admitted; stopping the server'
            Stop-SmokeRole $server; $server = $null
            if (Wait-SmokeLog $recoverLog 'stage=saved .* phase=reconnecting' 30 $null 0.25) {
                '  client shows reconnecting; restarting the server'
                $server = Start-Server (Join-Path $outDir 'server-2.log') $map
            } else {
                '  client never showed reconnecting'
            }
        } else {
            '  client was not admitted'
        }
        $code = Wait-SmokeRole $recoverClient
        $recoverClient.Dispose()
        Write-Record 'recover' $code
        Stop-SmokeRole $server; $server = $null
    }

    if ((Test-Wants 'tour') -or (Test-Wants 'matrix')) {
        # One server and one throwaway world store for every live capture: each client is a new character.
        $server = Start-Server (Join-Path $outDir 'server-world.log') $worldMap @("-MB2WorldStore=$(Join-Path $outDir 'world-store.json')",
            "-MB2Clock=$captureClock", '-MB2ClockSpeed=0.001', '-MB2Weather=clear_dry')
        if (Test-Wants 'tour') {
            "Live tour at 1920x1080 (port $port)"
            $code = Invoke-WorldClient 'tour' '1920' '1080' '1.0' 'arrival,explore,interact,character,skills,inventory,journal,settings,chat,menu'
            Write-Record 'tour' $code
        }
        if (Test-Wants 'matrix') {
            'Resolution and scale matrix'
            $matrixTour = 'explore,interact,character,skills,inventory,journal,settings,menu'
            if ($env:MB2_CAPTURE_MATRIX_TOUR) { $matrixTour = $env:MB2_CAPTURE_MATRIX_TOUR }
            foreach ($case in @($matrix -split '\s+' | Where-Object { $_ })) {
                $resolution = Get-Before $case '@'
                $scale = Get-After $case '@'
                $code = Invoke-WorldClient "m-$resolution-$scale" (Get-Before $resolution 'x') (Get-After $resolution 'x') $scale $matrixTour
                Write-Record "matrix $resolution @ $scale" $code
            }
        }
        Stop-SmokeRole $server; $server = $null
    }
} finally {
    Stop-SmokeRole $server
    $server = $null
}
$status = $script:status

$summary = Join-Path $outDir 'summary.txt'
$screenshots = @(Get-ChildItem -LiteralPath $outDir -Filter '*.png' -File -ErrorAction SilentlyContinue |
    Sort-Object Name | ForEach-Object { "  $($_.Name)" })
$pick = New-Object System.Text.RegularExpressions.Regex('MB2UI|MB2SMOKE role=server event=(ready|login|logout)')
$cut = New-Object System.Text.RegularExpressions.Regex('^.*(MB2UI|MB2SMOKE)')
$logLines = @(Get-SmokeLogLines @(Join-Path $outDir '*.log') | Where-Object { $pick.IsMatch($_) } | ForEach-Object { $cut.Replace($_, '$1') })
Write-SmokeFile $summary (@(
    'MarburgumII Stage 0C rendered UI captures'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "scenarios: $($scenarioList.Replace(',', ' '))"
    "data: front-end, gallery (SAMPLE DATA), fail and recover use no world; tour and matrix are live sessions on $worldMap against a dedicated server and a throwaway world store"
    "host: $(Get-SmokeHost)"
    "gpu: $(Get-GpuLine)"
    "client_cmd: $($targets.Client.Exe) $($targets.Client.Args -join ' ') $($commonArgs -join ' ') -RenderOffScreen -windowed -ForceRes -ResX=<w> -ResY=<h> -MB2UICapture=<dir> -MB2UICaptureScript=<script>"
    ''
    'screenshots:'
) + $screenshots + @('') + $logLines)
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
