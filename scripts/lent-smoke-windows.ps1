# Windows twin of lent-smoke-linux.sh (same phases, evidence lines and expectations).
# Stage 1 Lent slice qualification: location, an NPC, a saved mission and the HUD that shows them,
# on /Game/Maps/Lent, durable across a server restart.
#
# Phase 1 (fresh world store, one injected disk-write fault):
#   M  starts at the angler: arrives in Lent (location banner), a refused faulty commit leaves no
#      mission and no item, the mission is accepted with its start item in one commit, the same
#      operation replayed is a Duplicate, the next step is refused for want of a fish, journal shown.
#      Stays online for W.
#   W  starts on Stari most, outside Lent: talking to the angler from there is refused out of range;
#      once M's mission is saved, W's copy of M's player state must carry none of M's progress,
#      inventory or location; then W walks north until the server places it in Lent.
#   K  no character key: talking is refused.
#   F  the fast-clock adversary (ADR-019): its world clock runs 2.5x real time while it walks from the
#      bridge into Lent, so its move timestamps claim more time than has passed.
# Movement (ADR-019), from the server's log: every walker (W, F, and T in phase 2) must not have covered
# more ground than MaxWalkSpeed * (elapsed + MovementTimeDiscrepancyMaxTimeMargin) + 0.5 m, plus the extra
# ground of any seconds the server itself simulated it running (ADR-020), between its walk start and the
# server placing it in Lent; the server must have detected F's clock and no other.
# Honest walkers also fail on their own if their world clock ran ahead of real time.
# The server is stopped and the store file checked: schema 7, one commit (the mission start).
# Phase 2 (same store, server restarted):
#   M2 M's character: mission, pinned objective and start item restored; the replay is a Duplicate.
#   T  a new character walks from the bridge into Lent: the location banner appears.
# The store must be byte-identical after phase 2: duplicates, refusals and walking never write.
#
# M, M2 and T render offscreen one at a time (see interaction-smoke-windows.ps1 for the GPU note).
# Usage: scripts\lent-smoke-windows.ps1 [staged|editor]
# MB2_LENT_SERVER_ARGS adds server arguments, e.g. a negative control that must FAIL the movement check:
#   $env:MB2_LENT_SERVER_ARGS = '-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False'
# MB2_LENT_CLIENT_ARGS adds arguments to every client; restoring the engine's frame-time floor must FAIL W:
#   $env:MB2_LENT_CLIENT_ARGS = '-ini:Game:[/Script/Engine.WorldSettings]:MinUndilatedFrameTime=0.0005'
# Evidence is written to Saved\LentSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27706; if ($env:MB2_LENT_PORT) { $port = [int]$env:MB2_LENT_PORT }
$map = '/Game/Maps/Lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyM = "mb2m-$token"
$keyW = "mb2w-$token"
$keyT = "mb2t-$token"
$keyF = "mb2f-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\lent-smoke-windows.ps1 [staged|editor]'
$firstOp = (@(Invoke-Mb2 new-op) -join "`n").Trim()
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$outDir = New-SmokeOutDir 'LentSmoke'
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
$signalFile = Join-Path $outDir 'm-mission-saved'
$holdFile = Join-Path $outDir 'release-m'
$movement = Join-Path $outDir 'movement.txt'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:server = $null
$script:status = 0
$script:clients = @()
$extraServerArgs = @()
if ($env:MB2_LENT_SERVER_ARGS) { $extraServerArgs = @($env:MB2_LENT_SERVER_ARGS -split '\s+' | Where-Object { $_ }) }
$extraClientArgs = @()
if ($env:MB2_LENT_CLIENT_ARGS) { $extraClientArgs = @($env:MB2_LENT_CLIENT_ARGS -split '\s+' | Where-Object { $_ }) }

function Start-Server([string]$Log, [string[]]$More = @()) {
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_LENT_PORT' `
        -ReadySeconds 120 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs + $More + $extraServerArgs)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

# run_client <role> <character key or ""> <start tag> <extra args>: starts one client (output to client-<role>.log).
function Start-Client([string]$Role, [string]$Key, [string]$Start, [string[]]$More = @()) {
    $url = "127.0.0.1:${port}?MB2Start=$Start"
    if ($Key) { $url = "${url}?MB2Character=$Key" }
    $c = @{
        Label = $Role
        Log = (Join-Path $outDir "client-$Role.log")
        Arguments = (@($url) + $commonArgs + $More + @('-MB2LentSmoke', "-MB2LentRole=$Role", "-MB2LentOp=$firstOp",
            '-MB2LentTimeout=150', "-MB2LentSignal=$signalFile", "-MB2LentHoldFile=$holdFile") + $extraClientArgs)
        Attempt = 0
        Code = $null
        Role = $null
    }
    $c.Role = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $c.Log -Arguments $c.Arguments
    $script:clients += $c
    $c
}

# Collects a client that has ended; a renderer that could not create its GPU device never connected, so it is
# retried (twice at most), keeping each failed log. Returns $true once the client is finished (exit code in .Code).
function Update-Client($C) {
    if ($null -ne $C.Code) { return $true }
    if (-not $C.Role.Wait(0)) { return $false }
    $code = $C.Role.ExitCode
    $C.Role.Dispose()
    $C.Attempt = $C.Attempt + 1
    if ($C.Attempt -le 2 -and $code -ne 0 -and (Test-GpuInitFailure $C.Log)) {
        Move-Item -LiteralPath $C.Log -Destination (Join-Path $outDir "client-$($C.Label).gpu-init-failed-$($C.Attempt).log") -Force
        Write-Host "  client $($C.Label): GPU device creation failed before connecting; retry $($C.Attempt) in 3 s"
        Start-Sleep -Seconds 3
        $C.Role = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $C.Log -Arguments $C.Arguments
        return $false
    }
    $C.Code = $code
    $true
}

# wait <client>: its final exit code; meanwhile other running clients are retried as they need it.
function Wait-Client($C) {
    while (-not (Update-Client $C)) {
        foreach ($other in $script:clients) {
            if (-not [object]::ReferenceEquals($other, $C)) { [void](Update-Client $other) }
        }
        Start-Sleep -Milliseconds 250
    }
    $C.Code
}

function Invoke-Client([string]$Role, [string]$Key, [string]$Start, [string[]]$More = @()) {
    Wait-Client (Start-Client $Role $Key $Start $More)
}

function Write-ClientResult([string]$Role, $Code) {
    $log = Join-Path $outDir "client-$Role.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2LENT role=client tag=$Role event=pass")) {
        "  client ${Role}: PASS"
    } else {
        "  client ${Role}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2LENT role=client tag=$Role event=(fail|expectation_failed)" 'MB2LENT' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2LentShot=$(Join-Path $outDir $Name)") }

function Get-StoreHash { if (Test-Path -LiteralPath $store -PathType Leaf) { Get-SmokeSha256 $store } else { '' } }

function Update-FileStamp([string]$Path) {
    if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).LastWriteTime = Get-Date } else { Write-SmokeText $Path '' }
}

# check_walk <server log> <role> <character key> [adversary]: the ADR-019 movement bound, from the
# server's own positions. Appends one line to movement.txt; fails the run if the bound is broken.
function Test-Walk([string]$ServerLog, [string]$Role, [string]$Key, [string]$Kind = 'honest') {
    $out = @(Invoke-Mb2 movement-check $ServerLog (Join-Path $outDir "client-$Role.log") $Role $Key $Kind `
        (Join-Path $repoRoot 'Config\DefaultGame.ini'))
    $code = $LASTEXITCODE
    Add-SmokeFile $movement $out
    $lines = @(Get-SmokeLogLines $movement)
    if ($lines.Count) { "  movement: $($lines[$lines.Count - 1])" }
    if ($code -ne 0) { $script:status = 1 }
}

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last evidence prefix.
function Get-CutMatches([string[]]$Path, [string]$Pattern, [string]$Prefixes) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cut = New-Object System.Text.RegularExpressions.Regex("^.*($Prefixes)")
    foreach ($line in @(Get-SmokeLogLines $Path)) { if ($regex.IsMatch($line)) { $cut.Replace($line, '$1', 1) } }
}

function Get-Field([object[]]$Fields, [int]$Index, [string]$Default) {
    if ($Fields.Count -gt $Index -and $Fields[$Index]) { $Fields[$Index] } else { $Default }
}

try {
    # ---------------------------------------------------------------- phase 1
    "Phase 1: dedicated server ($Mode) on $map, port $port, fresh store; evidence in $outDir"
    Write-SmokeFile "$store.fail" @('1')
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log @('-MB2StoreFaultInjection')

    $w = Start-Client 'W' $keyW 'bridge' @('-nullrhi')
    $k = Start-Client 'K' '' 'angler' @('-nullrhi')
    $f = Start-Client 'F' $keyF 'bridge' @('-nullrhi', '-MB2LentClockRate=2.5')
    $m = Start-Client 'M' $keyM 'angler' (Get-Shot 'lent-m')
    Write-ClientResult 'W' (Wait-Client $w)
    Write-ClientResult 'K' (Wait-Client $k)
    Write-ClientResult 'F' (Wait-Client $f)
    Update-FileStamp $holdFile
    Write-ClientResult 'M' (Wait-Client $m)
    Start-Sleep -Seconds 1
    Stop-Server

    $storeCheckOut = @(Invoke-Mb2 store-check lent $store $keyM $keyW $firstOp)
    $storeCheck = $LASTEXITCODE
    Write-SmokeFile (Join-Path $outDir 'store-check.txt') $storeCheckOut
    if ($storeCheck -ne 0) { $script:status = 1 }
    $fields = @()
    if ($storeCheckOut.Count) { $fields = @("$($storeCheckOut[0])".Trim() -split '\s+', 2) }
    $revision = Get-Field $fields 0 'none'
    $stage = Get-Field $fields 1 'none'
    $storeHash = Get-StoreHash
    $faultLeft = ''
    if (Test-Path -LiteralPath "$store.fail" -PathType Leaf) { $faultLeft = [IO.File]::ReadAllText("$store.fail") -replace '\s', '' }
    "  store after phase 1: schema 7, revision=$revision stage=$stage (want 1, catch_first_fish), injected faults left=$faultLeft"
    if ($faultLeft -ne '0') { $script:status = 1 }
    $applied1 = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=interaction .* outcome=applied'
    $commitFailed = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=commit_failed'
    # FName keeps the first casing it saw, so the area id may print as "Lent".
    $entered = Get-SmokeLogCount $server1Log "(?i)MB2INTERACT role=server event=area .* character=$keyW area=lent"
    "  server phase 1: applied=$applied1 (want 1) commit_failed=$commitFailed (want 1) walker_entered_lent=$entered (want 1)"
    if (-not ($applied1 -eq 1 -and $commitFailed -eq 1 -and $entered -eq 1)) { $script:status = 1 }
    Test-Walk $server1Log 'W' $keyW
    Test-Walk $server1Log 'F' $keyF 'adversary'
    # Detection must catch the adversary and nobody else (a false positive would rubber-band an honest player).
    $detectedF = Get-SmokeLogCount $server1Log "MB2INTERACT role=server event=time_discrepancy .* character=$keyF "
    $detectedOther = @(Get-SmokeLogLines $server1Log | Where-Object {
        $_ -cmatch 'MB2INTERACT role=server event=time_discrepancy' -and $_ -cnotmatch " character=$keyF " }).Count
    "  server phase 1: time_discrepancy adversary=$detectedF (want >=1) others=$detectedOther (want 0)"
    if (-not ($detectedF -ge 1 -and $detectedOther -eq 0)) { $script:status = 1 }

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server2Log = Join-Path $outDir 'server-2.log'
    Start-Server $server2Log
    Write-ClientResult 'M2' (Invoke-Client 'M2' $keyM 'arrival' (Get-Shot 'lent-m2'))
    Write-ClientResult 'T' (Invoke-Client 'T' $keyT 'bridge' (Get-Shot 'lent-t'))
    Start-Sleep -Seconds 1
    Stop-Server
} finally {
    Stop-Server
}
Test-Walk $server2Log 'T' $keyT
$detected2 = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=time_discrepancy'
"  server phase 2: time_discrepancy=$detected2 (want 0)"
if ($detected2 -ne 0) { $script:status = 1 }

$applied2 = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=interaction .* outcome=applied'
$loaded = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=store_loaded .* schema=7 revision=1 '
$storeHash2 = Get-StoreHash
"  server phase 2: store_loaded(schema 7, revision 1)=$loaded applied=$applied2 (want 0)"
if (-not ($loaded -eq 1 -and $applied2 -eq 0)) { $script:status = 1 }
if ($storeHash -eq $storeHash2) {
    '  store unchanged by phase 2: yes'
} else {
    '  store unchanged by phase 2: NO'
    $script:status = 1
}

$serverArgsExtra = 'none'; if ($env:MB2_LENT_SERVER_ARGS) { $serverArgsExtra = $env:MB2_LENT_SERVER_ARGS }
$clientArgsExtra = 'none'; if ($env:MB2_LENT_CLIENT_ARGS) { $clientArgsExtra = $env:MB2_LENT_CLIENT_ARGS }
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII Stage 1 Lent slice smoke'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "store_after_phase1: revision=$revision stage=$stage sha256=$storeHash"
    "store_after_phase2_sha256: $storeHash2"
    "server_phase1: applied=$applied1 commit_failed=$commitFailed walker_entered_lent=$entered"
    "server_phase2: store_loaded=$loaded applied=$applied2"
    "server_args_extra: $serverArgsExtra"
    "client_args_extra: $clientArgsExtra"
    "time_discrepancy: phase1 adversary=$detectedF others=$detectedOther phase2=$detected2"
) + @(Get-SmokeLogLines $movement | ForEach-Object { "movement: $_" }) + @('') +
    @(Get-CutMatches (Join-Path $outDir '*.log') 'MB2LENT|MB2INTERACT|MB2SMOKE role=server event=(ready|login|logout)' 'MB2LENT|MB2INTERACT|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
