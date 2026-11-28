# Windows twin of vehicle-smoke-linux.sh (same phases, flags, evidence lines and expectations).
# Driving qualification (ADR-039) on the launch map, /Game/World/lent. Both clients start beside the
# authored lent_hatchback's driver door (?MB2Start=lent_hatchback), about 160 m west of the arrival start.
#
#   D  the driver (rendered offscreen). The parked car rests level on four wheels without creeping. D gets
#      in at the driver's door: the enter clip plays, then the body is hidden in the seat and the HUD names
#      the car at 0 km/h. With the passenger aboard it drives forward on the throttle, steers right, brakes
#      to a stop, reverses (REV on the HUD), stops, waits for the passenger to get out, gets out at its own
#      door and walks away; the car stays where it was left.
#   P  a passenger (-nullrhi). Once D holds the wheel it asks for the driver's seat (refused: seat_taken),
#      gets into the nearest free passenger seat, rides while D drives, and gets out once the car has stopped.
# The server must log the car ready, both entries and exits, the one refusal, and no impacts. At each exit
# it logs how far from the car it took that player's view to be: the camera behind the car while seated
# (about 11 m), not where they got in (ADR-056).
#
# The server runs the clear_dry weather (ADR-061), so the real day's rain never changes the car's grip under the
# thresholds below; scripts\weather-smoke-windows.ps1 drives on the wet road.
#
# MB2_VEHICLE_NET_CULL_M=<metres> shrinks the distance the server sends the car within. Below the seated
# camera's 11 m, only the occupants' relevancy rule keeps the car updating for D and P while they ride.
#
# D renders offscreen (a client that fails to create its GPU device before connecting is retried).
# Usage: scripts\vehicle-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\VehicleSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

if (-not $Mode) { $Mode = 'staged' }
$repoRoot = Get-RepoRoot
$port = 27709; if ($env:MB2_VEHICLE_PORT) { $port = [int]$env:MB2_VEHICLE_PORT }
$netCullM = "$env:MB2_VEHICLE_NET_CULL_M"
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyD = "mb2d-$token"
$keyP = "mb2p-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\vehicle-smoke-windows.ps1 [staged|editor]'
$wall = Get-ClientWallSeconds

# The bash runner always writes to Saved/VehicleSmoke (it does not read MB2_SMOKE_DIR).
$outDir = Join-Path $repoRoot ('Saved\VehicleSmoke\' + (Get-UtcStamp))
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen.
# The server ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:status = 0

function Start-Server([string]$Log) {
    $extra = @()
    if ($netCullM) { $extra += "-MB2VehicleNetCullM=$netCullM" }
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_VEHICLE_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", '-MB2Weather=clear_dry', '-MB2Clock=2026-06-18T08:00:00Z') + $commonArgs + $extra)
}

# New-Client <role> <character key> <extra args>: starts client-<role>.log; Wait-Clients finishes it (with retries).
function New-Client([string]$Role, [string]$Key, [string[]]$More) {
    $c = @{ Role = $Role; Key = $Key; More = @($More); Attempt = 0; Code = 0; Proc = $null; RetryAt = $null; Done = $false
        Log = (Join-Path $outDir "client-$Role.log") }
    Start-ClientAttempt $c
    $c
}

function Start-ClientAttempt($c) {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=$($c.Key)"
    $c.Proc = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $c.Log `
        -Arguments (@($url) + $commonArgs + $c.More + @('-MB2VehicleSmoke', "-MB2VehicleRole=$($c.Role)", '-MB2VehicleTimeout=300'))
}

# Waits for the clients (all of them, or for -Seconds), retrying a client whose renderer could not create its GPU
# device before connecting (it never connected; retrying is safe), at most twice, 3 s apart.
function Wait-Clients([object[]]$Clients, [double]$Seconds = 0) {
    $deadline = $null
    if ($Seconds -gt 0) { $deadline = (Get-Date).AddSeconds($Seconds) }
    while ($true) {
        if (-not @($Clients | Where-Object { -not $_.Done }).Count) {
            # Like bash's sleep 6 with a client in the background: the time passes even if the clients are done.
            if ($null -ne $deadline -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250; continue }
            return
        }
        foreach ($c in $Clients) {
            if ($c.Done) { continue }
            if ($null -ne $c.Proc) {
                if (-not $c.Proc.Wait(0)) { continue }
                $c.Code = $c.Proc.ExitCode
                $c.Proc.Dispose(); $c.Proc = $null
                $c.Attempt++
                if ($c.Attempt -le 2 -and $c.Code -ne 0 -and (Test-GpuInitFailure $c.Log)) {
                    Move-Item -Force -LiteralPath $c.Log -Destination (Join-Path $outDir "client-$($c.Role).gpu-init-failed-$($c.Attempt).log")
                    "  client $($c.Role): GPU device creation failed before connecting; retry $($c.Attempt) in 3 s"
                    $c.RetryAt = (Get-Date).AddSeconds(3)
                } else {
                    $c.Done = $true
                }
            } elseif ((Get-Date) -ge $c.RetryAt) {
                $c.Code = 0
                Start-ClientAttempt $c
            }
        }
        if ($null -ne $deadline -and (Get-Date) -ge $deadline) { return }
        Start-Sleep -Milliseconds 250
    }
}

function Test-Client($c) {
    $role = $c.Role
    if ($c.Code -eq 0 -and (Test-SmokeLog $c.Log "MB2VSMOKE role=client tag=$role event=pass")) {
        "  client ${role}: PASS"
    } else {
        "  client ${role}: FAIL (exit $($c.Code))"
        Get-SmokeLogMatches $c.Log "MB2VSMOKE role=client tag=$role event=(fail|expectation_failed)" 'MB2VSMOKE' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2VehicleShot=$(Join-Path $outDir $Name)") }

# Test-Expect <server log> <label> <want: number or ">=N"> <pattern>
function Test-Expect([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = Get-SmokeLogCount $Log $Pattern
    "  server: $Label=$count (want $Want)"
    if ($Want.StartsWith('>=')) {
        if ($count -lt [int]$Want.Substring(2)) { $script:status = 1 }
    } else {
        if ($count -ne [int]$Want) { $script:status = 1 }
    }
}

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/'
function Get-SummaryLines([string[]]$Paths, [string]$Pattern, [string]$Cut) {
    $re = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cutRe = New-Object System.Text.RegularExpressions.Regex("^.*($Cut)")
    foreach ($line in @(Get-SmokeLogLines $Paths)) {
        if ($re.IsMatch($line)) { $cutRe.Replace($line, '$1') }
    }
}

# awk's view of a string as a number: its leading numeric prefix, 0 when there is none.
function ConvertTo-AwkNumber([string]$Text) {
    $m = [regex]::Match("$Text", '^\s*[-+]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?')
    if (-not $m.Success) { return 0.0 }
    [double]::Parse($m.Value.Trim(), [Globalization.CultureInfo]::InvariantCulture)
}

$cullNote = ''
if ($netCullM) { $cullNote = ", car sent within $netCullM m" }
$server = $null
try {
    "Dedicated server ($Mode) on $map, port $port$cullNote; evidence in $outDir"
    $server = Start-Server (Join-Path $outDir 'server.log')
    # D first, so it is at the car before the passenger arrives; P follows a few seconds later.
    $d = New-Client 'D' $keyD (Get-Shot 'vehicle-d')
    Wait-Clients @($d) 6
    $p = New-Client 'P' $keyP @('-nullrhi')
    Wait-Clients @($d, $p)
    Test-Client $d
    Test-Client $p
    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}
$log = Join-Path $outDir 'server.log'
Test-Expect $log 'vehicle_ready' '1' 'MB2VEHICLE role=server event=vehicle_ready vehicle=lent_hatchback definition=city_hatchback_a '
Test-Expect $log 'entered_driver' '1' 'MB2VEHICLE role=server event=entered vehicle=lent_hatchback .* seat=driver '
Test-Expect $log 'entered_passenger' '1' 'MB2VEHICLE role=server event=entered vehicle=lent_hatchback .* seat=(front_passenger|rear_left|rear_right) '
Test-Expect $log 'refused_seat_taken' '1' 'MB2VEHICLE role=server event=enter_refused vehicle=lent_hatchback .* seat=driver reason=seat_taken '
Test-Expect $log 'exited' '2' 'MB2VEHICLE role=server event=exited vehicle=lent_hatchback '
Test-Expect $log 'exit_refused' '0' 'MB2VEHICLE role=server event=exit_refused'
Test-Expect $log 'seat_released_on_leave' '0' 'MB2VEHICLE role=server event=seat_released'
Test-Expect $log 'impacts' '0' 'MB2VEHICLE role=server event=impact vehicle=lent_hatchback '
Test-Expect $log 'unknown_definitions' '0' 'MB2VEHICLE role=.* event=unknown_definition'
if ($netCullM) {
    Test-Expect $log "net_cull_m=$netCullM" '1' ("MB2VEHICLE role=server event=vehicle_ready .* net_cull_m=$netCullM" + '$')
}
# The server's view of a player getting out is the seated camera, behind the car, not where they got in.
Test-Expect $log 'exit_view' '2' 'MB2VEHICLE role=server event=exit_view vehicle=lent_hatchback '
$maxViewM = 16
# grep -ao "event=exit_view ... view_m=[0-9.]*" | sed 's/.*view_m=//'
foreach ($line in @(Get-SmokeLogLines $log)) {
    foreach ($m in [regex]::Matches($line, 'event=exit_view vehicle=lent_hatchback player=[0-9-]* view_m=[0-9.]*')) {
        $viewM = $m.Value.Substring($m.Value.LastIndexOf('view_m=') + 7)
        "  server: exit view_m=$viewM (want <= $maxViewM)"
        if (-not ((ConvertTo-AwkNumber $viewM) -le $maxViewM)) { $script:status = 1 }
    }
}
$status = $script:status

$cullShown = $netCullM; if (-not $cullShown) { $cullShown = 'engine default (150)' }
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII vehicle smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "net_cull_m: $cullShown"
    "map: $map"
    "host: $(Get-SmokeHost)"
    ''
) + @(Get-SummaryLines @(Join-Path $outDir '*.log') 'MB2VSMOKE|MB2VEHICLE|MB2SMOKE role=server event=(ready|login|logout)' `
        'MB2VSMOKE|MB2VEHICLE|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
