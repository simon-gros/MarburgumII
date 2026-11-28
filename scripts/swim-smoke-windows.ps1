# Windows twin of swim-smoke-linux.sh (same phases, flags, evidence lines and expectations).
# Swimming, held breath and drowning qualification (ADR-032) on the launch map, /Game/World/lent.
# Clients start at the arrival start on the Lent promenade, about 20 m east of the Drava.
#
# Phase 1 (fresh world store):
#   W  walks west into the river; swims at swim speed with the mouth up and the stroke playing; lets
#      go, turns upright treading water and slips under; holds its breath for ten seconds at about a minute per bar; swims up and back
#      east, and climbs out onto the bank; walks back in, lets go and drowns. Down, it is at the recovery
#      point and cannot move; it leaves while still down.
#   O  an observer (-nullrhi) connected while W swims: W's body is in the water on O's client and plays
#      the swim clip.
# The server must log W in the water, under, surfaced, out on the bank, drowned and at the recovery
#   point, and save it at logout with no vitality and the rest of its wait (recovery_s).
# Phase 2 (same store, server restarted):
#   W2 W's character: restored down at the recovery point with the wait it had left, cannot move, then
#      recovers with full vitality and breath and walks again. Saved rested, it is no entry at all.
#
# W and W2 render offscreen one at a time (a client that fails to create its GPU device before connecting is retried).
# Usage: scripts\swim-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\SwimSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

if (-not $Mode) { $Mode = 'staged' }
$repoRoot = Get-RepoRoot
$port = 27708; if ($env:MB2_SWIM_PORT) { $port = [int]$env:MB2_SWIM_PORT }
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyW = "mb2w-$token"
$keyO = "mb2o-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\swim-smoke-windows.ps1 [staged|editor]'
$wall = Get-ClientWallSeconds

# The bash runner always writes to Saved/SwimSmoke (it does not read MB2_SMOKE_DIR).
$outDir = Join-Path $repoRoot ('Saved\SwimSmoke\' + (Get-UtcStamp))
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen.
# The server ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:status = 0

function Start-Server([string]$Log) {
    Start-SmokeServer -Target $targets.Server -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs) `
        -Log $Log -Port $port -PortVariable 'MB2_SWIM_PORT' -ReadySeconds 120
}

# New-Client <role> <character key> <extra args>: starts client-<role>.log; Wait-Clients finishes it (with retries).
function New-Client([string]$Role, [string]$Key, [string[]]$More) {
    $c = @{ Role = $Role; Key = $Key; More = @($More); Attempt = 0; Code = 0; Proc = $null; RetryAt = $null; Done = $false
        Log = (Join-Path $outDir "client-$Role.log") }
    Start-ClientAttempt $c
    $c
}

function Start-ClientAttempt($c) {
    $url = "127.0.0.1:${port}?MB2Start=arrival?MB2Character=$($c.Key)"
    $c.Proc = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $c.Log `
        -Arguments (@($url) + $commonArgs + $c.More + @('-MB2SwimSmoke', "-MB2SwimRole=$($c.Role)", '-MB2SwimTimeout=300'))
}

# Waits for the clients (all of them, or for -Seconds), retrying a client whose renderer could not create its GPU
# device before connecting (it never connected; retrying is safe), at most twice, 3 s apart.
function Wait-Clients([object[]]$Clients, [double]$Seconds = 0) {
    $deadline = $null
    if ($Seconds -gt 0) { $deadline = (Get-Date).AddSeconds($Seconds) }
    while (@($Clients | Where-Object { -not $_.Done }).Count) {
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
    if ($c.Code -eq 0 -and (Test-SmokeLog $c.Log "MB2SWIM role=client tag=$role event=pass")) {
        "  client ${role}: PASS"
    } else {
        "  client ${role}: FAIL (exit $($c.Code))"
        Get-SmokeLogMatches $c.Log "MB2SWIM role=client tag=$role event=(fail|expectation_failed)" 'MB2SWIM' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2SwimShot=$(Join-Path $outDir $Name)") }

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

# grep -ah PATTERN | grep -v EXCLUDE | sed 's/^.*\(A\|B\)/\1/'
function Get-SummaryLines([string[]]$Paths, [string]$Pattern, [string]$Cut, [string]$Exclude) {
    $re = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cutRe = New-Object System.Text.RegularExpressions.Regex("^.*($Cut)")
    $exRe = $null
    if ($Exclude) { $exRe = New-Object System.Text.RegularExpressions.Regex($Exclude) }
    foreach ($line in @(Get-SmokeLogLines $Paths)) {
        if (-not $re.IsMatch($line)) { continue }
        if ($null -ne $exRe -and $exRe.IsMatch($line)) { continue }
        $cutRe.Replace($line, '$1')
    }
}

$server = $null
try {
    # ---------------------------------------------------------------- phase 1
    "Phase 1: dedicated server ($Mode) on $map, port $port, fresh store; evidence in $outDir"
    $server = Start-Server (Join-Path $outDir 'server-1.log')
    $o = New-Client 'O' $keyO @('-nullrhi')
    $w = New-Client 'W' $keyW (Get-Shot 'swim-w')
    Wait-Clients @($w, $o)
    Test-Client $w
    Test-Client $o
    Start-Sleep -Seconds 1
    Stop-SmokeRole $server; $server = $null
    $log1 = Join-Path $outDir 'server-1.log'
    Test-Expect $log1 'swimmer_under' '>=2' "MB2COND role=server event=under .* character=$keyW "
    Test-Expect $log1 'swimmer_surfaced' '>=1' "MB2COND role=server event=surfaced .* character=$keyW "
    Test-Expect $log1 'swimmer_in_water' '>=2' "MB2COND role=server event=in_water .* character=$keyW "
    Test-Expect $log1 'swimmer_left_water' '>=1' "MB2COND role=server event=left_water .* character=$keyW "
    # Climbing out (ADR-032) only fires where the bank is a step; the Lent bank here is a slope a swimmer wades up.
    Test-Expect $log1 'swimmer_climbed_out' '>=0' "MB2COND role=server event=climbed_out .* character=$keyW "
    Test-Expect $log1 'swimmer_drowned' '1' "MB2COND role=server event=drowned .* character=$keyW "
    Test-Expect $log1 'swimmer_recovery_point' '1' "MB2COND role=server event=recovery_point .* character=$keyW "
    Test-Expect $log1 'swimmer_recovered_early' '0' "MB2COND role=server event=recovered .* character=$keyW "
    Test-Expect $log1 'swimmer_saved_down' '1' ("MB2COND role=server event=condition_saved why=logout character=$keyW vitality=0 .* recovery_s=[45][0-9]" + '$')
    Test-Expect $log1 'save_failures' '0' 'MB2COND role=server event=condition_save_failed'
    # grep -m1 ... | sed 's/.*event=condition_saved //'
    $savedDown = $null
    $savedRe = New-Object System.Text.RegularExpressions.Regex("event=condition_saved why=logout character=$keyW ")
    foreach ($line in @(Get-SmokeLogLines $log1)) {
        if ($savedRe.IsMatch($line)) {
            $cut = 'event=condition_saved '
            $savedDown = $line.Substring($line.LastIndexOf($cut) + $cut.Length)
            break
        }
    }
    try { Copy-Item -LiteralPath $store -Destination (Join-Path $outDir 'world-store-after-phase-1.json') -Force } catch { }

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server = Start-Server (Join-Path $outDir 'server-2.log')
    $w2 = New-Client 'W2' $keyW (Get-Shot 'swim-w2')
    Wait-Clients @($w2)
    Test-Client $w2
    Start-Sleep -Seconds 1
    Stop-SmokeRole $server; $server = $null
} finally {
    Stop-SmokeRole $server
    $server = $null
}
$log2 = Join-Path $outDir 'server-2.log'
Test-Expect $log2 'restored_down' '1' "MB2COND role=server event=condition_restored .* character=$keyW source=store vitality=0 "
Test-Expect $log2 'recovery_point' '1' "MB2COND role=server event=recovery_point .* character=$keyW "
Test-Expect $log2 'recovered' '1' "MB2COND role=server event=recovered .* character=$keyW "
Test-Expect $log2 'saved_rested' '1' "MB2COND role=server event=condition_saved why=logout character=$keyW vitality=100 stamina=100 winded=0 "
Test-Expect $log2 'save_failures' '0' 'MB2COND role=server event=condition_save_failed'
# tr -d ' \t\n' < store | grep -o '"conditions":{[^}]*}'
$storeConditions = ''
if (Test-Path -LiteralPath $store -PathType Leaf) {
    $flat = [IO.File]::ReadAllText($store) -replace '[ \t\n]', ''
    $storeConditions = (@([regex]::Matches($flat, '"conditions":\{[^}]*\}') | ForEach-Object { $_.Value }) -join "`n")
}
$shown = $storeConditions; if (-not $shown) { $shown = 'none' }
"  store conditions after phase 2: $shown (want empty: a rested body is no entry)"
if ($storeConditions -cne '"conditions":{}') { $script:status = 1 }
$status = $script:status

$savedShown = $savedDown; if (-not $savedShown) { $savedShown = 'none' }
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII swimming smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "saved_down: $savedShown"
    ''
) + @(Get-SummaryLines @(Join-Path $outDir '*.log') 'MB2SWIM|MB2COND|MB2SMOKE role=server event=(ready|login|logout)' `
        'MB2SWIM|MB2COND|MB2SMOKE' 'MB2COND role=server event=(winded|breath_back)'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
