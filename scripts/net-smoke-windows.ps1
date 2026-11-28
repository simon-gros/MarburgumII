# Stage 0B network smoke: one dedicated server process, then separate client processes. Windows twin of
# net-smoke-linux.sh (same phases, evidence lines and expectations).
#
#   Phase A: one client joins, is refused a contract change, commits one idempotent
#            operation, disconnects, reconnects and confirms the replay is a duplicate.
#   Phase B: two clients join concurrently and both observe the same server-owned state.
#
# Usage: scripts\net-smoke-windows.ps1 [staged|editor]
#   staged  packaged MarburgumIIServer / MarburgumIIClient from scripts\package-windows.ps1 (default)
#   editor  uncooked UnrealEditor-Cmd -server / -game processes (requires UE_ROOT)
# Evidence is written to Saved\NetSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27701; if ($env:MB2_SMOKE_PORT) { $port = [int]$env:MB2_SMOKE_PORT }
$map = '/Game/Maps/NetSmoke'
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\net-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'NetSmoke'
$wall = Get-ClientWallSeconds

$commonArgs = @(Get-CommonArgs)
# Fault injection stays out of the acceptance run. MB2_SMOKE_EXTRA="-PktLag=120 -PktLagVariance=40 -PktLoss=1"
# is a separate scenario; do not treat its result as the clean-network Gate 0B result.
$extra = @(Get-SmokeExtraArgs)
if ($extra.Count) { $commonArgs += $extra; "fault injection: $($extra -join ' ')" }

function Start-Client([string]$Tag, [string[]]$More) {
    Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir "client-$Tag.log") `
        -Arguments (@("127.0.0.1:$port") + $commonArgs + @('-nullrhi', '-MB2NetSmoke', "-MB2SmokeTag=$Tag", '-MB2SmokeTimeout=90') + $More)
}

function Test-ClientPassed([string]$Tag) {
    Test-SmokeLog (Join-Path $outDir "client-$Tag.log") "MB2SMOKE role=client tag=$Tag event=pass"
}

$server = $null
$status = 0
try {
    "Starting dedicated server ($Mode) on port $port; evidence in $outDir"
    $serverLog = Join-Path $outDir 'server.log'
    # Start-SmokeServer refuses a taken port and waits for the ready line on this exact port: Unreal silently picks
    # the next port if the requested one is taken, and clients would then reach the wrong server.
    $server = Start-SmokeServer -Target $targets.Server -Arguments (@($map, "-port=$port") + $commonArgs) `
        -Log $serverLog -Port $port -ReadySeconds 120

    'Phase A: one client, refused mutation, idempotent operation, disconnect/reconnect'
    $solo = Start-Client 'solo' @('-MB2SmokePeers=1', '-MB2SmokeReconnect')
    [void](Wait-SmokeRole $solo); $solo.Dispose()
    if (Test-ClientPassed 'solo') { '  solo: PASS' } else { '  solo: FAIL'; $status = 1 }

    'Phase B: two concurrent clients observing shared server-owned state'
    $peers = @((Start-Client 'peer1' @('-MB2SmokePeers=2')), (Start-Client 'peer2' @('-MB2SmokePeers=2')))
    foreach ($peer in $peers) { [void](Wait-SmokeRole $peer); $peer.Dispose() }
    foreach ($tag in 'peer1', 'peer2') {
        if (Test-ClientPassed $tag) { "  ${tag}: PASS" } else { "  ${tag}: FAIL"; $status = 1 }
    }

    Start-Sleep -Seconds 2
} finally {
    Stop-SmokeRole $server
    $server = $null
}

# Authority-side expectations: 4 joins (solo twice), 3 refused contract changes,
# 3 committed operations and 4 duplicates (one per client plus solo's post-reconnect replay).
$logins = Get-SmokeLogCount $serverLog 'MB2SMOKE role=server event=login'
$refused = Get-SmokeLogCount $serverLog 'event=contract_change_refused'
$applied = Get-SmokeLogCount $serverLog 'event=operation .* outcome=applied'
$duplicates = Get-SmokeLogCount $serverLog 'event=operation .* outcome=duplicate'
"Server: logins=$logins refused=$refused applied=$applied duplicates=$duplicates"
if ($logins -ne 4 -or $refused -ne 3 -or $applied -ne 3 -or $duplicates -ne 4) {
    '  server expectations: FAIL (want logins=4 refused=3 applied=3 duplicates=4)'
    $status = 1
}

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII Stage 0B network smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha)"
    "mode: $Mode"
    "host: $(Get-SmokeHost)"
    "server_cmd: $($targets.Server.Exe) $($targets.Server.Args -join ' ') $map -port=$port $($commonArgs -join ' ')"
    "client_cmd: $($targets.Client.Exe) $($targets.Client.Args -join ' ') 127.0.0.1:$port $($commonArgs -join ' ') -nullrhi -MB2NetSmoke ..."
    ''
) + @(Get-SmokeLogMatches @($serverLog, (Join-Path $outDir 'client-*.log')) 'MB2SMOKE' 'MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
