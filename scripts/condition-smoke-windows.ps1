# Stage 1 player-condition qualification (ADR-020): running costs breath, a run that empties the bar
# leaves the body winded, the server decides both, and the condition survives leaving and a restart.
# Windows twin of condition-smoke-linux.sh (same phases, evidence lines and expectations).
# All clients start on the Lent promenade and run east.
#
# Phase 1 (fresh world store):
#   R  a new character: rested on the server, in its prediction, the HUD and the Character panel; runs
#      at run speed, is winded after about ten seconds and walks while still asking to run, runs again
#      once breath is back, and leaves the moment the server has it winded a second time.
#   C  the adversary (-nullrhi): ignores its breath and claims a full bar on every move. The server must
#      wind it after about ten seconds anyway, correct it and hold it to walking speed while winded.
# The server must save R's and C's condition at logout (two commits) and log C winded. Stopped, the store
#   is checked: R winded with almost no stamina, C tired, no custody written, revision 2.
# Phase 2 (same store, server restarted):
#   R2 R's character: restored winded and asking to run at once. The server recovers its breath from
#      login on, so the script checks the server log: R's breath came back with no running at all. R2
#      then runs once breath is back and rests to full; a rested body is saved as no entry at all.
#   S  a runner (-nullrhi) still connected when the script stops the server: the stop must save it.
# The store is checked again: R gone, C kept, S tired, revision 4.
#
# R and R2 render offscreen one at a time (see interaction-smoke-windows.ps1 for the GPU note).
# Usage: scripts\condition-smoke-windows.ps1 [staged|editor]
# MB2_CONDITION_SERVER_ARGS adds server arguments. Evidence is written to Saved\ConditionSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27707; if ($env:MB2_CONDITION_PORT) { $port = [int]$env:MB2_CONDITION_PORT }
$map = '/Game/Maps/Lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyR = "mb2r-$token"
$keyC = "mb2c-$token"
$keyS = "mb2s-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\condition-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'ConditionSmoke'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs with a wall-clock limit, which stops its whole job.
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
$signalFile = Join-Path $outDir 's-running'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$server = $null
$status = 0
$extraServerArgs = @()
if ($env:MB2_CONDITION_SERVER_ARGS) { $extraServerArgs = @($env:MB2_CONDITION_SERVER_ARGS -split '\s+' | Where-Object { $_ }) }

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last of several evidence prefixes.
function Get-SummaryLines([string[]]$Path, [string]$Pattern, [string]$Cut) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cutRegex = New-Object System.Text.RegularExpressions.Regex("^.*($Cut)")
    foreach ($line in @(Get-SmokeLogLines $Path)) {
        if ($regex.IsMatch($line)) { $cutRegex.Replace($line, '$1', 1) }
    }
}

function Start-ConditionServer([string]$Log) {
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_CONDITION_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs + $extraServerArgs)
}

function Get-ClientArgs([string]$Role, [string]$Key, [string[]]$More) {
    @("127.0.0.1:${port}?MB2Start=arrival?MB2Character=$Key") + $commonArgs + $More + @('-MB2ConditionSmoke',
        "-MB2ConditionRole=$Role", '-MB2ConditionTimeout=90', "-MB2ConditionSignal=$signalFile")
}

# Invoke-Client <role> <character key> <extra args>; output to client-<role>.log. Returns the exit code.
function Invoke-Client([string]$Role, [string]$Key, [string[]]$More) {
    $log = Join-Path $outDir "client-$Role.log"
    $code = 0
    foreach ($attempt in 0, 1, 2) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client-$Role.gpu-init-failed-$attempt.log") -Force
            Write-Host "  client ${Role}: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log -Arguments (Get-ClientArgs $Role $Key $More)
    }
    $code
}

function Test-Client([string]$Role, [int]$Code) {
    $log = Join-Path $outDir "client-$Role.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2COND role=client tag=$Role event=pass")) {
        "  client ${Role}: PASS"
    } else {
        "  client ${Role}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2COND role=client tag=$Role event=(fail|expectation_failed)" 'MB2COND' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2ConditionShot=$(Join-Path $outDir $Name)") }

# Test-Expect <server log> <label> <want: number or ">=N"> <pattern>
function Test-Expect([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = ''
    if (Test-Path -LiteralPath $Log) { $count = Get-SmokeLogCount $Log $Pattern }
    "  server: $Label=$count (want $Want)"
    $n = 0; [void][int]::TryParse("$count", [ref]$n)
    if ($Want.StartsWith('>=')) {
        if ($n -lt [int]$Want.Substring(2)) { $script:status = 1 }
    } elseif ($n -ne [int]$Want) {
        $script:status = 1
    }
}

# scripts\mb2.ps1 store-check ... > <file>: returns the first line's whitespace-separated fields (read -r a b c).
function Invoke-StoreCheck([string]$OutFile, [string]$Phase) {
    $out = @(Invoke-Mb2 store-check condition $store $keyR $keyC $keyS $Phase)
    $code = $LASTEXITCODE
    Write-SmokeFile $OutFile $out
    if ($code) { $script:status = 1 }
    $fields = @('', '', '')
    if ($out.Count) {
        $parts = @("$($out[0])".Trim() -split '\s+', 3)
        for ($i = 0; $i -lt $parts.Count; $i++) { $fields[$i] = $parts[$i] }
    }
    , $fields
}

function Get-OrNone([string]$Value) { if ($Value) { $Value } else { 'none' } }

try {
    # ---------------------------------------------------------------- phase 1
    "Phase 1: dedicated server ($Mode) on $map, port $port, fresh store; evidence in $outDir"
    $server = Start-ConditionServer (Join-Path $outDir 'server-1.log')
    $code = Invoke-Client 'R' $keyR @(Get-Shot 'condition-r')
    Test-Client 'R' $code
    $code = Invoke-Client 'C' $keyC @('-nullrhi')
    Test-Client 'C' $code
    Start-Sleep -Seconds 1
    Stop-SmokeRole $server
    $server = $null
    $log1 = Join-Path $outDir 'server-1.log'
    Test-Expect $log1 'runner_restored_rested' '1' "MB2COND role=server event=condition_restored .* character=$keyR source=rested "
    Test-Expect $log1 'runner_winded' '2' "MB2COND role=server event=winded .* character=$keyR "
    Test-Expect $log1 'runner_saved_winded' '1' "MB2COND role=server event=condition_saved why=logout character=$keyR .* winded=1 "
    Test-Expect $log1 'cheater_winded' '>=1' "MB2COND role=server event=winded .* character=$keyC "
    Test-Expect $log1 'cheater_saved' '1' "MB2COND role=server event=condition_saved why=logout character=$keyC "
    Test-Expect $log1 'save_failures' '0' 'MB2COND role=server event=condition_save_failed'

    $check1 = Invoke-StoreCheck (Join-Path $outDir 'store-check-1.txt') '1'
    $revision1 = $check1[0]; $stamina1 = $check1[1]; $winded1 = $check1[2]
    "  store after phase 1: revision=$(Get-OrNone $revision1) R stamina=$(Get-OrNone $stamina1) winded=$(Get-OrNone $winded1) (want 2, <=5, 1)"
    Copy-Item -LiteralPath $store -Destination (Join-Path $outDir 'world-store-after-phase-1.json') -ErrorAction SilentlyContinue

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server = Start-ConditionServer (Join-Path $outDir 'server-2.log')
    $code = Invoke-Client 'R2' $keyR @(Get-Shot 'condition-r2')
    Test-Client 'R2' $code
    Remove-Item -LiteralPath $signalFile -Force -ErrorAction SilentlyContinue
    $runnerS = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir 'client-S.log') `
        -Arguments (Get-ClientArgs 'S' $keyS @('-nullrhi'))
    for ($i = 0; $i -lt 120; $i++) {
        if (Test-Path -LiteralPath $signalFile) { break }
        if (-not $runnerS.Alive) { break }
        Start-Sleep -Milliseconds 250
    }
    if (Test-Path -LiteralPath $signalFile) {
        '  S is running; stopping the server with S connected'
    } else {
        '  S never signalled that it was running'
        $status = 1
    }
    Stop-SmokeRole $server
    $server = $null
    $code = Wait-SmokeRole $runnerS; $runnerS.Dispose(); Test-Client 'S' $code
} finally {
    Stop-SmokeRole $server
    $server = $null
}

$log2 = Join-Path $outDir 'server-2.log'
Test-Expect $log2 'runner_restored_from_store' '1' "MB2COND role=server event=condition_restored .* character=$keyR source=store .* winded=1"
# Asked to run from the moment it could, R2 got its breath back without the server running it once.
$firstBack = ''
$firstRegex = New-Object System.Text.RegularExpressions.Regex("MB2COND role=server event=breath_back .* character=$keyR ")
foreach ($line in @(Get-SmokeLogLines $log2)) { if ($firstRegex.IsMatch($line)) { $firstBack = $line; break } }
$firstBackTail = $firstBack
$at = $firstBack.LastIndexOf('event=breath_back ')
if ($at -ge 0) { $firstBackTail = $firstBack.Substring($at + 'event=breath_back '.Length) }
"  server: runner first breath back: $firstBackTail (want run_s=0.00)"
if (-not $firstBack.Contains(' run_s=0.00')) { $status = 1 }
Test-Expect $log2 'runner_saved_rested' '1' "MB2COND role=server event=condition_saved why=logout character=$keyR vitality=100 stamina=100 winded=0 "
# A stop may close S's connection (logout) before the world tears down (shutdown); either saves it.
Test-Expect $log2 'stopper_saved' '1' "MB2COND role=server event=condition_saved why=(logout|shutdown) character=$keyS "
Test-Expect $log2 'save_failures' '0' 'MB2COND role=server event=condition_save_failed'

$check2 = Invoke-StoreCheck (Join-Path $outDir 'store-check-2.txt') '2'
$revision2 = $check2[0]
"  store after phase 2: revision=$(Get-OrNone $revision2) (want 4; R removed, C kept, S saved)"
$storeHash = ''
if (Test-Path -LiteralPath $store -PathType Leaf) { $storeHash = Get-SmokeSha256 $store }

$serverArgsExtra = 'none'; if ($env:MB2_CONDITION_SERVER_ARGS) { $serverArgsExtra = $env:MB2_CONDITION_SERVER_ARGS }
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII Stage 1 player-condition smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "store_after_phase1: revision=$(Get-OrNone $revision1) runner_stamina=$(Get-OrNone $stamina1) runner_winded=$(Get-OrNone $winded1)"
    "store_after_phase2: revision=$(Get-OrNone $revision2) sha256=$storeHash"
    "server_args_extra: $serverArgsExtra"
    ''
) + @(Get-SummaryLines (Join-Path $outDir '*.log') 'MB2COND|MB2SMOKE role=server event=(ready|login|logout)|MB2INTERACT role=server event=store_loaded' 'MB2COND|MB2SMOKE|MB2INTERACT'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
