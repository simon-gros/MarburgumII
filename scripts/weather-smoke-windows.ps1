# Windows twin of weather-smoke-linux.sh (same phases, flags, evidence lines and expectations).
# Weather qualification (ADR-061) on the launch map, /Game/World/lent. One client starts beside the authored
# lent_hatchback's driver door (?MB2Start=lent_hatchback) against a real dedicated server, three times:
#
#   dry   the server runs the clear_dry profile. The client sees it replicated (and on the HUD's weather line), gets
#         in, drives off at 0.8 throttle for 3 s, brakes fully and reports the deceleration, gets out and walks for
#         4 s: its own footsteps are hard and none splash.
#   wet   the server runs rain_wet (soaked streets). The same drive and walk: the car decelerates less on the wet road
#         (the original's wet-asphalt traction, 0.82 of dry) and the steps on hard ground splash.
#   live  the server runs the Maribor climate on a review clock from 18 June 2026 10:00 CET (09:00 UTC) at an hour a
#         minute, a dry overcast morning before rain sets in at about 11:00 (MB2_WEATHER_MONTH=2026-06 calendar,
#         seed 1996). The client sees the hourly state turn and the streets soak past wetness 0.5, the HUD's weather
#         line following the replicated weather; then it walks and its steps splash.
#
# Every phase also checks what the client draws (ADR-062): rain curtain, clouds, the level's sun and fog against their
# own, and MPC_Weather's wetness. Six more phases only look and capture (-MB2WeatherLookOnly), all facing the same
# way along the promenade so their captures compare:
#
#   clear     the clear_dry profile: the level's daylight as built, a few clouds.
#   rain      the rain_wet profile: rain falling and leaning in the wind, the street soaked.
#   overcast  the overcast profile: a closed sky, a dimmer sun, the eye opened up, nothing falling.
#   snow      the winter_cold profile: five centimetres of snow lying, so the ground is white.
#   fog       the climate at 22 November 2026 08:30 CET, a fog morning (seed 1996): the haze many times its own.
#   storm     the climate at 2 July 2026 16:30 CET, a thunderstorm: rain, the storm's flashes, and one close strike
#             (FlashNow) captured.
#
# The world clock (ADR-064) turns the sun and brings the night, so every phase pins it: the profile phases at
# 18 June 2026 10:00 CEST, the fog and storm at their own daylit hours. Six more phases look at day and night under
# a clear_dry sky and check the clock, the banner's 1990s date and time, and the sun, moon and night light drawn:
#
#   noon      21 June 2026 12:57 CEST, the solstice sun at its highest (67 degrees).
#   evening   21 June 2026 20:30 CEST, the sun 3 degrees up in the north-west.
#   dusk      13 November 2026 16:50 CET, civil twilight: the sun 4.5 degrees under, the night light coming in.
#   moonlit   25 December 2026 00:00 CET, a full moon 66 degrees up lighting the street.
#   moonless  13 November 2026 22:00 CET, the moon under the horizon: only the city's skyglow.
#   clockrun  from 21 June 2026 04:00 CEST at a day a minute: the client's clock runs at the server's rate and the
#             sun it draws moves with it.
#
# scripts\daylight-smoke-windows.ps1 runs just those six.
#
# The script compares the dry and wet decelerations and checks the server's weather log in each phase.
# The client renders offscreen (a client that fails to create its GPU device before connecting is retried).
# MB2_WEATHER_PHASES="dry wet" runs only those phases (the braking comparison needs both dry and wet; default all).
# Usage: scripts\weather-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\WeatherSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

if (-not $Mode) { $Mode = 'staged' }
$repoRoot = Get-RepoRoot
$port = 27711; if ($env:MB2_WEATHER_PORT) { $port = [int]$env:MB2_WEATHER_PORT }
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$liveClock = '2026-06-18T09:00:00Z'
$liveSpeed = 60
$dayClock = '2026-06-18T08:00:00Z'
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\weather-smoke-windows.ps1 [staged|editor]'
$wall = Get-ClientWallSeconds

# Saved\${MB2_SMOKE_DIR:-WeatherSmoke}\<UTC timestamp>.
$outDir = New-SmokeOutDir 'WeatherSmoke'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen.
# The server ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:status = 0
# MB2_CLIENT_EXTRA: extra client options for calibration runs, e.g. -MB2NightExposure=0.5 (split like ${MB2_CLIENT_EXTRA:-}).
$clientExtra = @()
if ($env:MB2_CLIENT_EXTRA) { $clientExtra = @($env:MB2_CLIENT_EXTRA -split '\s+' | Where-Object { $_ }) }
$middot = [string][char]0x00B7

# Start-Server <phase> <server args>
function Start-Server([string]$Phase, [string[]]$More) {
    $log = Join-Path $outDir "server-$Phase.log"
    Start-SmokeServer -Target $targets.Server -Log $log -Port $port -PortVariable 'MB2_WEATHER_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir "world-store-$Phase.json")") + $commonArgs + @($More))
}

# Invoke-Client <phase> <expect> <extra client args>; output to client-<phase>.log
function Invoke-Client([string]$Phase, [string]$Expect, [string[]]$More) {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=mb2w-$Phase-$token"
    $log = Join-Path $outDir "client-$Phase.log"
    $code = 0
    foreach ($attempt in 0, 1, 2) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -Force -LiteralPath $log -Destination (Join-Path $outDir "client-$Phase.gpu-init-failed-$attempt.log")
            "  client ${Phase}: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log `
            -Arguments (@($url) + $commonArgs + $renderArgs + @($More) + $clientExtra + @('-MB2WeatherSmoke', "-MB2WeatherExpect=$Expect",
                "-MB2WeatherShot=$(Join-Path $outDir $Phase)", '-MB2WeatherTimeout=300'))
    }
    if ($code -eq 0 -and (Test-SmokeLog $log "MB2WSMOKE role=client expect=$Expect event=pass")) {
        "  client ${Phase}: PASS"
    } else {
        "  client ${Phase}: FAIL (exit $code)"
        Get-SmokeLogMatches $log "MB2WSMOKE role=client expect=$Expect event=(fail|expectation_failed)" 'MB2WSMOKE' '    '
        $script:status = 1
    }
}

# Test-Expect <log> <label> <want: number or ">=N"> <pattern>
function Test-Expect([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = Get-SmokeLogCount $Log $Pattern
    "  server: $Label=$count (want $Want)"
    if ($Want.StartsWith('>=')) {
        if ($count -lt [int]$Want.Substring(2)) { $script:status = 1 }
    } else {
        if ($count -ne [int]$Want) { $script:status = 1 }
    }
}

# Get-ClientValue <phase> <event> <field>: the field of the client's first line for that event
# (grep -ao "MB2WSMOKE role=client .* event=E .*" | head -1 | grep -o " F=[^ ]*" | head -1 | cut -d= -f2).
function Get-ClientValue([string]$Phase, [string]$EventName, [string]$Field) {
    $log = Join-Path $outDir "client-$Phase.log"
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { return '' }
    $re = New-Object System.Text.RegularExpressions.Regex("MB2WSMOKE role=client .* event=$EventName .*")
    foreach ($line in @(Get-SmokeLogLines $log)) {
        $m = $re.Match($line)
        if (-not $m.Success) { continue }
        $f = [regex]::Match($m.Value, " $Field=[^ ]*")
        if (-not $f.Success) { return '' }
        return $f.Value.Split('=')[1]
    }
    ''
}

# awk's view of a string as a number: its leading numeric prefix, 0 when there is none.
function ConvertTo-AwkNumber([string]$Text) {
    $m = [regex]::Match("$Text", '^\s*[-+]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?')
    if (-not $m.Success) { return 0.0 }
    [double]::Parse($m.Value.Trim(), [Globalization.CultureInfo]::InvariantCulture)
}

# Invoke-Phase <phase> <expect> <look only or ''> <server args>
function Invoke-Phase([string]$Phase, [string]$Expect, [string]$LookOnly, [string[]]$ServerArgs) {
    "Phase ${Phase}: dedicated server ($Mode) on $map, port $port, weather $Expect"
    $script:server = Start-Server $Phase $ServerArgs
    $more = @()
    if ($LookOnly) { $more = @("-MB2WeatherLookOnly=$LookOnly") }
    Invoke-Client $Phase $Expect $more
    Start-Sleep -Seconds 1
    Stop-SmokeRole $script:server; $script:server = $null
    $log = Join-Path $outDir "server-$Phase.log"
    Test-Expect $log 'weather_started' '1' "MB2WEATHER role=server event=started source=$Expect "
    Test-Expect $log 'weather_errors' '0' 'MB2WEATHER role=server event=(no_climate|unknown_profile|bad_clock)'
    # The street lamps stand on the server too, so their posts block there (ADR-065).
    Test-Expect $log 'lamps_placed' '1' 'MB2LAMPS role=server event=placed lamps=[0-9]{4,} .* meshes=1'
    Test-Expect (Join-Path $outDir "client-$Phase.log") 'client_lamps_placed' '1' 'MB2LAMPS role=client event=placed lamps=[0-9]{4,} .* meshes=1'
}

$phasesText = 'dry wet live clear overcast rain snow fog storm noon evening dusk moonlit moonless clockrun'
if ($env:MB2_WEATHER_PHASES) { $phasesText = $env:MB2_WEATHER_PHASES }
$phases = @($phasesText -split '\s+' | Where-Object { $_ })

# Test-Clock <phase> <banner clock pattern>: the client's daylight line shows that 1990s date and time
function Test-Clock([string]$Phase, [string]$Banner) {
    $log = Join-Path $outDir "client-$Phase.log"
    $count = Get-SmokeLogCount $log "MB2WSMOKE role=client .* event=look_daylight .* banner_clock=`"$Banner`""
    "  client ${Phase}: banner_clock ~ `"$Banner`" lines=$count (want 1)"
    if ($count -ne 1) { $script:status = 1 }
}

$script:server = $null
try {
    "Evidence in $outDir (phases: $phasesText)"
    foreach ($phase in $phases) {
        switch -CaseSensitive ($phase) {
            'dry' { Invoke-Phase 'dry' 'clear_dry' '' @('-MB2Weather=clear_dry', "-MB2Clock=$dayClock") }
            'wet' { Invoke-Phase 'wet' 'rain_wet' '' @('-MB2Weather=rain_wet', "-MB2Clock=$dayClock") }
            'live' {
                Invoke-Phase 'live' 'auto' '' @('-MB2Weather=auto', "-MB2WeatherClock=$liveClock", "-MB2WeatherSpeed=$liveSpeed")
                Test-Expect (Join-Path $outDir 'server-live.log') 'live_weather_turns' '>=1' 'MB2WEATHER role=server event=changed .* source=auto state=light_rain type=rain'
            }
            'clear' { Invoke-Phase 'clear' 'clear_dry' 'clear' @('-MB2Weather=clear_dry', "-MB2Clock=$dayClock") }
            'overcast' { Invoke-Phase 'overcast' 'overcast' 'overcast' @('-MB2Weather=overcast', "-MB2Clock=$dayClock") }
            'rain' { Invoke-Phase 'rain' 'rain_wet' 'rain' @('-MB2Weather=rain_wet', "-MB2Clock=$dayClock") }
            'snow' { Invoke-Phase 'snow' 'winter_cold' 'snow_lying' @('-MB2Weather=winter_cold', "-MB2Clock=$dayClock") }
            'fog' { Invoke-Phase 'fog' 'auto' 'fog' @('-MB2Weather=auto', '-MB2Clock=2026-11-22T07:30:00Z') }
            'storm' { Invoke-Phase 'storm' 'auto' 'storm' @('-MB2Weather=auto', '-MB2Clock=2026-07-02T15:30:00Z') }
            'noon' {
                Invoke-Phase 'noon' 'clear_dry' 'noon' @('-MB2Weather=clear_dry', '-MB2Clock=2026-06-21T10:57:00Z')
                Test-Clock 'noon' "Fri 21 Jun 1996  $middot  12:5[789]"
            }
            'evening' {
                Invoke-Phase 'evening' 'clear_dry' 'evening' @('-MB2Weather=clear_dry', '-MB2Clock=2026-06-21T18:30:00Z')
                Test-Clock 'evening' "Fri 21 Jun 1996  $middot  20:3[012]"
            }
            'dusk' {
                Invoke-Phase 'dusk' 'clear_dry' 'dusk' @('-MB2Weather=clear_dry', '-MB2Clock=2026-11-13T15:50:00Z')
                Test-Clock 'dusk' "Wed 13 Nov 1996  $middot  16:5[012]"
            }
            'moonlit' {
                Invoke-Phase 'moonlit' 'clear_dry' 'moonlit' @('-MB2Weather=clear_dry', '-MB2Clock=2026-12-24T23:00:00Z')
                Test-Clock 'moonlit' "Wed 25 Dec 1996  $middot  00:0[012]"
            }
            'moonless' {
                Invoke-Phase 'moonless' 'clear_dry' 'moonless' @('-MB2Weather=clear_dry', '-MB2Clock=2026-11-13T21:00:00Z')
                Test-Clock 'moonless' "Wed 13 Nov 1996  $middot  22:0[012]"
            }
            'clockrun' {
                Invoke-Phase 'clockrun' 'clear_dry' 'clockrun' @('-MB2Weather=clear_dry', '-MB2Clock=2026-06-21T02:00:00Z', '-MB2ClockSpeed=1440')
                Test-Expect (Join-Path $outDir 'server-clockrun.log') 'clock_started_fast' '1' 'MB2CLOCK role=server event=started .* speed=1440.000 review=1'
            }
            default { Exit-Usage "Unknown phase $phase" }
        }
    }
} finally {
    Stop-SmokeRole $script:server
    $script:server = $null
}

foreach ($phase in $phases) {
    $log = Join-Path $outDir "client-$phase.log"
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { continue }
    $looks = Get-SmokeLogCount $log 'MB2WSMOKE role=client .* event=look[a-z_]* want='
    "  client ${phase}: look checks=$looks (want >= 1)"
    if ($looks -lt 1) { $script:status = 1 }
}

# The scene's brightness in each look capture (mean grey of the promenade and river right of the player, clear of the
# HUD, the player and the car's roof; 0..255), for calibrating the look.
foreach ($phase in 'clear', 'overcast', 'rain', 'snow', 'fog', 'storm', 'noon', 'evening', 'dusk', 'moonlit', 'moonless') {
    $shot = Join-Path $outDir "$phase-look.png"
    if (-not (Test-Path -LiteralPath $shot -PathType Leaf)) { continue }
    # Alpha ignored: the engine's screenshots carry an alpha channel, which must not count as brightness (ADR-064).
    # Crop 860x260+1060+440 after scaling to 1920x1080.
    $mean = Measure-ImageMeanGrey $shot 1060 440 860 260
    "  capture ${phase}: scene mean grey $mean"
    Add-SmokeFile (Join-Path $outDir 'brightness.txt') @("$phase $mean")
}

$dryDecel = Get-ClientValue 'dry' 'braked' 'decel'
$wetDecel = Get-ClientValue 'wet' 'braked' 'decel'
$drySurface = Get-ClientValue 'dry' 'braked' 'surface'
$wetSurface = Get-ClientValue 'wet' 'braked' 'surface'
function Get-OrQ([string]$Value) { if ($Value) { $Value } else { '?' } }
if ($phases -ccontains 'dry' -and $phases -ccontains 'wet') {
    "  braking: dry decel=$(Get-OrQ $dryDecel) m/s2 on $(Get-OrQ $drySurface), wet decel=$(Get-OrQ $wetDecel) m/s2 on $(Get-OrQ $wetSurface) (want wet <= 0.95 x dry)"
    if (-not $dryDecel -or -not $wetDecel) {
        $script:status = 1
    } else {
        $d = ConvertTo-AwkNumber $dryDecel
        $w = ConvertTo-AwkNumber $wetDecel
        if (-not ($d -gt 0 -and $w -le 0.95 * $d)) { $script:status = 1 }
    }
}
$status = $script:status

$lines = @(
    'MarburgumII weather smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "live_clock: $liveClock x$liveSpeed"
    "host: $(Get-SmokeHost)"
    "braking: dry decel=$(Get-OrQ $dryDecel) wet decel=$(Get-OrQ $wetDecel)"
    ''
)
# grep -ah "MB2WSMOKE\|MB2CLOCK role=server\|MB2WEATHER ...\|MB2SMOKE ..." | sed 's/^.*\(MB2WSMOKE\|MB2WEATHER\|MB2SMOKE\)/\1/'
$pick = New-Object System.Text.RegularExpressions.Regex('MB2WSMOKE|MB2CLOCK role=server|MB2WEATHER role=server event=(started|changed)|MB2SMOKE role=server event=(ready|login|logout)')
$cut = New-Object System.Text.RegularExpressions.Regex('^.*(MB2WSMOKE|MB2WEATHER|MB2SMOKE)')
foreach ($phase in $phases) {
    $lines += "== $phase"
    foreach ($line in @(Get-SmokeLogLines @((Join-Path $outDir "server-$phase.log"), (Join-Path $outDir "client-$phase.log")))) {
        if ($pick.IsMatch($line)) { $lines += $cut.Replace($line, '$1') }
    }
}
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary $lines
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
