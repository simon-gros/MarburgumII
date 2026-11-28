# Windows twin of place-smoke-linux.sh (same phases, evidence lines and expectations).
# Place-name qualification (ADR-063) on the launch map, /Game/World/lent. A real dedicated server started with
# -MB2PlaceRoute sets the one client's player down at the ten stops of MarburgumPlaceRoute.h, six seconds each: Stari
# most, Vodni stolp and Zidovski stolp on Lent, the synagogue on Zidovski trg, Glavni trg and the Rotovz, the cathedral
# on Slomskov trg, the castle on Grajska ulica, Magdalena over the river, and back to the bridge. At each stop the
# client (-MB2PlaceSmoke) checks the location banner's place, street or square and landmark lines against the route,
# that the banner came up for the stop, and takes a capture. The server's place log is checked for every stop.
#
# The client renders offscreen (see interaction-smoke-windows.ps1 for the GPU note).
# Usage: scripts\place-smoke-windows.ps1 [staged|editor]
# MB2_PLACE_CLOCK=<ISO 8601 UTC> sets the server's world clock (default a June morning); a night walks the route under the
# street lamps (ADR-065).
# Evidence is written to Saved\PlaceSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27712; if ($env:MB2_PLACE_PORT) { $port = [int]$env:MB2_PLACE_PORT }
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$stops = 10
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\place-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'PlaceSmoke'
$wall = Get-ClientWallSeconds

# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$server = $null
$script:status = 0
$serverLog = Join-Path $outDir 'server.log'
$clientLog = Join-Path $outDir 'client.log'

# expect_log <log> <label> <want: number or ">=N"> <pattern>
function Test-Expected([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = Get-SmokeLogCount $Log $Pattern
    "  $Label=$count (want $Want)"
    if ($Want.StartsWith('>=')) {
        if (-not ($count -ge [int]$Want.Substring(2))) { $script:status = 1 }
    } else {
        if ($count -ne [int]$Want) { $script:status = 1 }
    }
}

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last evidence prefix.
function Get-CutMatches([string[]]$Path, [string]$Pattern, [string]$Prefixes) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cut = New-Object System.Text.RegularExpressions.Regex("^.*($Prefixes)")
    foreach ($line in @(Get-SmokeLogLines $Path)) { if ($regex.IsMatch($line)) { $cut.Replace($line, '$1', 1) } }
}

try {
    if (Test-UdpPortInUse $port) {
        Exit-Failure "UDP port $port is already in use; stop the other server or set MB2_PLACE_PORT."
    }
    "Evidence in $outDir"
    "Dedicated server ($Mode) on $map, port $port, walking the place route"
    # Clear dry weather, so the captures read the same on any day.
    $clock = '2026-06-18T08:00:00Z'; if ($env:MB2_PLACE_CLOCK) { $clock = $env:MB2_PLACE_CLOCK }
    $server = Start-SmokeServer -Target $targets.Server -Log $serverLog -Port $port -PortVariable 'MB2_PLACE_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", '-MB2Weather=clear_dry',
            "-MB2Clock=$clock", '-MB2PlaceRoute') + $commonArgs)

    $url = "127.0.0.1:${port}?MB2Character=mb2p-$token"
    $clientArgs = @($url) + $commonArgs + $renderArgs + @('-MB2PlaceSmoke', "-MB2PlaceShot=$(Join-Path $outDir 'stop')", '-MB2PlaceTimeout=180')
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $clientLog))) { break }
            Move-Item -LiteralPath $clientLog -Destination (Join-Path $outDir "client.gpu-init-failed-$attempt.log") -Force
            "  client: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $clientLog -Arguments $clientArgs
    }
    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}

if ($code -eq 0 -and (Test-SmokeLog $clientLog 'MB2PSMOKE role=client event=pass')) {
    '  client: PASS'
} else {
    "  client: FAIL (exit $code)"
    Get-SmokeLogMatches $clientLog 'MB2PSMOKE role=client event=(fail|expectation_failed)' 'MB2PSMOKE' '    '
    $script:status = 1
}

Test-Expected $clientLog 'client places_loaded' '1' 'MB2PSMOKE role=client event=start .* places_loaded=1 '
Test-Expected $clientLog 'client stops_matched' "$stops" 'MB2PSMOKE role=client event=stop .* match=1'
Test-Expected $serverLog 'server route_stops_set_down' "$stops" 'MB2PLACE role=server event=route .* ground=1 moved=1 '
Test-Expected $serverLog 'server places_loaded' '1' 'LogMarburgumPlaces: places: 27 districts, .* \(0 left out\)'
Test-Expected $serverLog 'server place_changes' ">=$stops" 'MB2INTERACT role=server event=place '

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII place smoke'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    ''
) + @(Get-CutMatches @($serverLog, $clientLog) `
    'MB2PSMOKE|MB2PLACE|MB2INTERACT role=server event=(place|area)|LogMarburgumPlaces|MB2SMOKE role=server event=(ready|login|logout)' `
    'MB2PSMOKE|MB2PLACE|MB2INTERACT|LogMarburgumPlaces|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
