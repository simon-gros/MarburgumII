# Windows twin of interaction-smoke-linux.sh (same phases, evidence lines and expectations).
# Gate 1 qualification: one authoritative interaction, durable across a server restart.
#
# Phase 1 (fresh world store, one injected disk-write fault):
#   A  write fault refused with no effect, grant, replay of the same operation id (Duplicate),
#      out-of-range request (refused), second grant, then races B for the last unit.
#   B  requests the last unit as soon as it sees one left.
# The server is stopped and the store file checked: 3 grants, stock 0, one receipt per grant.
# Phase 2 (same store, server restarted):
#   A2 inventory and stock restored, A's first operation replayed (Duplicate), a new request
#      refused out of stock; stays online while D tries to log in as the same character.
#   A3 the same character after A2 logged out: admitted again, restored inventory on screen.
#   B2 inventory restored.   N  no character key: admitted, interaction refused.
#   D  A's key while A2 is online: refused at admission.   X  malformed key: refused.
# The store must be byte-identical after phase 2: duplicates and refusals never write.
#
# A, D and A3 render offscreen and save screenshots, one renderer at a time in phase 2 (on the Linux test host a
# second offscreen Vulkan client could not create its device while another was rendering). A client whose GPU device
# fails to come up before connecting is retried up to twice; each failed log is kept as evidence.
# Usage: scripts\interaction-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\InteractSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27705; if ($env:MB2_INTERACT_PORT) { $port = [int]$env:MB2_INTERACT_PORT }
$map = '/Game/Maps/NetSmoke'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyA = "mb2a-$token"
$keyB = "mb2b-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\interaction-smoke-windows.ps1 [staged|editor]'
$firstOp = (@(Invoke-Mb2 new-op) -join "`n").Trim()
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$outDir = New-SmokeOutDir 'InteractSmoke'
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
$holdFile = Join-Path $outDir 'release-a2'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:server = $null
$script:status = 0
$script:clients = @()

function Start-Server([string]$Log, [string[]]$More = @()) {
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_INTERACT_PORT' `
        -ReadySeconds 120 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs + $More)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

# run_client <role> <character key or ""> <extra args>: starts one client (output to client-<role>.log).
function Start-Client([string]$Role, [string]$Key, [string[]]$More = @()) {
    $url = "127.0.0.1:$port"
    if ($Key) { $url = "${url}?MB2Character=$Key" }
    $c = @{
        Label = $Role
        Log = (Join-Path $outDir "client-$Role.log")
        Arguments = (@($url) + $commonArgs + $More + @('-MB2InteractSmoke', "-MB2InteractRole=$Role",
            "-MB2InteractOp=$firstOp", '-MB2InteractTimeout=150'))
        Attempt = 0
        Code = $null
        Role = $null
    }
    $c.Role = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $c.Log -Arguments $c.Arguments
    $script:clients += $c
    $c
}

# Collects a client that has ended. An offscreen renderer occasionally fails to create its GPU device while another
# process is initialising or rendering. That happens before any network traffic, so retrying is safe; each failed log
# is kept as evidence. Returns $true once the client is finished for good (its exit code is in .Code).
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

function Invoke-Client([string]$Role, [string]$Key, [string[]]$More = @()) {
    Wait-Client (Start-Client $Role $Key $More)
}

function Write-ClientResult([string]$Role, $Code) {
    $log = Join-Path $outDir "client-$Role.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2INTERACT role=client tag=$Role event=pass")) {
        "  client ${Role}: PASS"
    } else {
        "  client ${Role}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2INTERACT role=client tag=$Role event=(fail|expectation_failed)" 'MB2INTERACT' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2InteractShot=$(Join-Path $outDir $Name)") }

function Get-StoreHash { if (Test-Path -LiteralPath $store -PathType Leaf) { Get-SmokeSha256 $store } else { '' } }

function Update-FileStamp([string]$Path) {
    if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).LastWriteTime = Get-Date } else { Write-SmokeText $Path '' }
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
    "Phase 1: dedicated server ($Mode) on port $port, fresh store; evidence in $outDir"
    Write-SmokeFile "$store.fail" @('1')
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log @('-MB2StoreFaultInjection')

    $b = Start-Client 'B' $keyB @('-nullrhi')
    $a = Start-Client 'A' $keyA (Get-Shot 'interact-a-session1')
    Write-ClientResult 'A' (Wait-Client $a)
    Write-ClientResult 'B' (Wait-Client $b)
    Start-Sleep -Seconds 1
    Stop-Server

    # The durable record after phase 1: three grants in total, nothing else.
    $storeCheckOut = @(Invoke-Mb2 store-check interaction $store $keyA $keyB $firstOp)
    $storeCheck = $LASTEXITCODE
    Write-SmokeFile (Join-Path $outDir 'store-check.txt') $storeCheckOut
    if ($storeCheck -ne 0) { $script:status = 1 }
    $fields = @()
    if ($storeCheckOut.Count) { $fields = @("$($storeCheckOut[0])".Trim() -split '\s+', 3) }
    $itemsA = Get-Field $fields 0 '0'
    $itemsB = Get-Field $fields 1 '0'
    $revision = Get-Field $fields 2 'none'
    $storeHash = Get-StoreHash
    $faultLeft = ''
    if (Test-Path -LiteralPath "$store.fail" -PathType Leaf) { $faultLeft = [IO.File]::ReadAllText("$store.fail") -replace '\s', '' }
    "  store after phase 1: revision=$revision items A=$itemsA B=$itemsB (sum must be 3), injected faults left=$faultLeft"
    if ($faultLeft -ne '0') { $script:status = 1 }
    $applied1 = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=interaction .* outcome=applied'
    $commitFailed = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=commit_failed'
    "  server phase 1: applied=$applied1 (want 3) commit_failed=$commitFailed (want 1)"
    if (-not ($applied1 -eq 3 -and $commitFailed -eq 1)) { $script:status = 1 }

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server2Log = Join-Path $outDir 'server-2.log'
    Start-Server $server2Log

    $a2 = Start-Client 'A2' $keyA @('-nullrhi', "-MB2InteractExpect=$itemsA", "-MB2InteractHoldFile=$holdFile")
    $b2 = Start-Client 'B2' $keyB @('-nullrhi', "-MB2InteractExpect=$itemsB")
    $n = Start-Client 'N' '' @('-nullrhi')

    for ($i = 0; $i -lt 240; $i++) {
        if (Test-SmokeLog $a2.Log 'MB2INTERACT role=client tag=A2 event=holding') { break }
        foreach ($c in $script:clients) { [void](Update-Client $c) }
        if ($null -ne $a2.Code) { break }
        Start-Sleep -Milliseconds 500
    }
    Write-ClientResult 'X' (Invoke-Client 'X' 'x' @('-nullrhi'))
    Write-ClientResult 'D' (Invoke-Client 'D' $keyA (Get-Shot 'interact-d-duplicate-login'))
    Update-FileStamp $holdFile
    Write-ClientResult 'A2' (Wait-Client $a2)
    Write-ClientResult 'B2' (Wait-Client $b2)
    Write-ClientResult 'N' (Wait-Client $n)
    # The character is free again once A2 has logged out; a new session shows the restored custody.
    Write-ClientResult 'A3' (Invoke-Client 'A3' $keyA ((Get-Shot 'interact-a-session3') + @("-MB2InteractExpect=$itemsA")))
    Start-Sleep -Seconds 1
    Stop-Server
} finally {
    Stop-Server
}

$refused = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=admission_refused'
$applied2 = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=interaction .* outcome=applied'
$loaded = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=store_loaded .* revision=3 '
$storeHash2 = Get-StoreHash
"  server phase 2: store_loaded(revision 3)=$loaded admission_refused=$refused (want 2) applied=$applied2 (want 0)"
if (-not ($loaded -eq 1 -and $refused -eq 2 -and $applied2 -eq 0)) { $script:status = 1 }
if ($storeHash -eq $storeHash2) {
    '  store unchanged by phase 2: yes'
} else {
    '  store unchanged by phase 2: NO'
    $script:status = 1
}

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII Gate 1 interaction smoke'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "host: $(Get-SmokeHost)"
    "store_after_phase1: revision=$revision items_a=$itemsA items_b=$itemsB sha256=$storeHash"
    "store_after_phase2_sha256: $storeHash2"
    "server_phase1: applied=$applied1 commit_failed=$commitFailed"
    "server_phase2: store_loaded=$loaded admission_refused=$refused applied=$applied2"
    ''
) + @(Get-CutMatches (Join-Path $outDir '*.log') 'MB2INTERACT|MB2SMOKE role=server event=(ready|login|logout)' 'MB2INTERACT|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
