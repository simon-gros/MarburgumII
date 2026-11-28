# Windows twin of sheet-smoke-linux.sh (same phases, evidence lines and expectations).
# Character sheet: attributes and equipment (ADR-085, LRP-01/02), end to end on /Game/World/lent.
#
# The world store is seeded as a schema 6 document (revision 1) with three characters: A carries a fishing rod, the
# borrowed camera and two chub, and its attributes are a legacy record that still says `wind`; B carries a rod and its
# record has both `wind` and `endurance` with different values; C has no attributes at all.
#
# Phase 1: the server migrates the store once as it loads (legacy alias and conflict reported, one commit, revision 2).
#   SHEET   (A, renders) attributes 12/14/9/11 on the sheet; the rod into the right hand from the sheet; the replay
#           answered as a duplicate; the rod into the left hand too, the camera on the head, an unknown slot, an unknown
#           item and emptying the empty bag refused; the camera into the left hand from the Inventory.
#   PEER    (B, -nullrhi, at the same time) Endurance 16 (the canonical id won); its rod into its right hand; it never
#           receives A's attributes, equipment or inventory.
# The server is stopped and the store checked (mb2 store-check sheet 1): schema 7, revision 5, no `wind` anywhere.
# Phase 2, same store: RESTORED (A) finds scores and both hands as left and takes the rod out from the sheet; the load
# migrates nothing. Store check 2: revision 6.
#
# Usage: scripts\sheet-smoke-windows.ps1 [staged|editor]. Evidence: Saved\SheetSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27785; if ($env:MB2_SHEET_PORT) { $port = [int]$env:MB2_SHEET_PORT }
$map = '/Game/World/lent'
$stamp = "$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyA = "mb2s-a$stamp"
$keyB = "mb2s-b$stamp"
$keyC = "mb2s-c$stamp"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\sheet-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'SheetSmoke'
# A client stuck before its in-engine timeout starts must not hang the script (see shop-smoke-windows.ps1).
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:server = $null
$script:status = 0

function Start-Server([string]$Log) {
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_SHEET_PORT' `
        -ReadySeconds 180 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

# launch <label> <role> <key> <extra args>: starts one client in the background and returns it.
function Start-Client([string]$Label, [string]$Role, [string]$Key, [string[]]$More = @()) {
    $url = "127.0.0.1:${port}?MB2Start=arrival?MB2Character=$Key"
    Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir "client-$Label.log") `
        -Arguments (@($url) + $commonArgs + $More + @('-MB2SheetSmoke', "-MB2SheetRole=$Role", '-MB2SheetTimeout=150'))
}

function Wait-Client($Client) {
    $code = Wait-SmokeRole $Client
    $Client.Dispose()
    $code
}

# check <label> <role> <exit code>
function Write-ClientResult([string]$Label, [string]$Role, $Code) {
    $log = Join-Path $outDir "client-$Label.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2SHEET role=client tag=$Role event=pass")) {
        "  client ${Label}: PASS"
    } else {
        "  client ${Label}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2SHEET role=client tag=$Role event=(fail|expectation_failed)" 'MB2SHEET' '    '
        if (Test-GpuInitFailure $log) { '    (GPU device creation failed: another client may hold the GPU)' }
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2SheetShot=$(Join-Path $outDir $Name)") }

function Get-StoreHash { if (Test-Path -LiteralPath $store -PathType Leaf) { Get-SmokeSha256 $store } else { '' } }

# scripts\mb2.ps1 <args> 2>&1: stdout and stderr lines together; the exit code is in $LASTEXITCODE.
function Invoke-Mb2Merged([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'scripts\mb2.ps1') @Arguments 2>&1 |
        ForEach-Object { "$_" }
}

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last evidence prefix.
function Get-CutMatches([string[]]$Path, [string]$Pattern, [string]$Prefixes) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cut = New-Object System.Text.RegularExpressions.Regex("^.*($Prefixes)")
    foreach ($line in @(Get-SmokeLogLines $Path)) { if ($regex.IsMatch($line)) { $cut.Replace($line, '$1', 1) } }
}

try {
    # ---------------------------------------------------------------- phase 1
    Write-SmokeText $store ('{"format":"marburgumii.world_store","schema_version":6,"revision":1,"stocks":{},"inventories":{"' +
        $keyA + '":{"fishing_rod":1,"camera":1,"chub":2},"' + $keyB + '":{"fishing_rod":1}},"missions":{},"conditions":{},' +
        '"skills":{},"money":{},"attributes":{"' + $keyA + '":{"might":12,"wind":14,"eye":9,"poise":11},"' + $keyB +
        '":{"might":10,"wind":7,"endurance":16,"eye":10,"poise":10}},"receipts":{}}' + "`n")
    "Phase 1: dedicated server ($Mode) on $map, port $port, seeded legacy store; evidence in $outDir"
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log

    $clientA = Start-Client 'sheet' 'SHEET' $keyA (Get-Shot 'sheet')
    Start-Sleep -Seconds 2
    $clientB = Start-Client 'peer' 'PEER' $keyB @('-nullrhi')
    Write-ClientResult 'sheet' 'SHEET' (Wait-Client $clientA)
    Write-ClientResult 'peer' 'PEER' (Wait-Client $clientB)
    Start-Sleep -Seconds 1
    Stop-Server

    $storeCheck1File = Join-Path $outDir 'store-check-1.txt'
    $storeCheckOut = @(Invoke-Mb2Merged @('store-check', 'sheet', $store, $keyA, $keyB, $keyC, '1'))
    $storeCheck = $LASTEXITCODE
    Write-SmokeFile $storeCheck1File $storeCheckOut
    $storeCheckOut | ForEach-Object { "  store 1: $_" }
    if ($storeCheck -ne 0) { $script:status = 1 }
    $migrated = Get-SmokeLogCount $server1Log 'MB2SHEET role=server event=sheets_loaded schema=6 attributes=2 equipment=0 legacy_aliases=2 conflicts=1 repaired=0 reconciled=0'
    $committed = Get-SmokeLogCount $server1Log 'MB2SHEET role=server event=sheet_migration_committed from_schema=6 revision=2 '
    $decisions = -join @(Get-SmokeLogLines $server1Log | Where-Object { $_ -cmatch "MB2SHEET role=server event=equip character=$keyA " } |
        ForEach-Object { ([regex]::Replace($_, '.* slot=([a-z_-]*) item=([a-z_-]*) .* decision=([a-z_]*).*', '$1:$2:$3')) + ' ' })
    # The replay is answered from the stored receipt before the equipment rule runs, so it has no decision line.
    $wantDecisions = 'right_hand:fishing_rod:item_equipped left_hand:fishing_rod:insufficient_items head:camera:not_equippable ' +
        '-:camera:unknown_slot right_hand:unknown_item:not_equippable bag:-:nothing_to_do left_hand:camera:item_equipped '
    $peerDecisions = Get-SmokeLogCount $server1Log "MB2SHEET role=server event=equip character=$keyB slot=right_hand item=fishing_rod .* decision=item_equipped"
    "  server phase 1: sheets_loaded(legacy 2, conflict 1)=$migrated (want 1); migration committed at revision 2=$committed (want 1); peer equipped=$peerDecisions (want 1)"
    "  decisions: [ $decisions]"
    if (-not ($migrated -eq 1 -and $committed -eq 1 -and $peerDecisions -eq 1 -and $decisions -ceq $wantDecisions)) {
        "  want:      [ $wantDecisions]"
        $script:status = 1
    }

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store'
    $server2Log = Join-Path $outDir 'server-2.log'
    Start-Server $server2Log
    $restored = Start-Client 'restored' 'RESTORED' $keyA (Get-Shot 'restored')
    Write-ClientResult 'restored' 'RESTORED' (Wait-Client $restored)
    Start-Sleep -Seconds 1
    Stop-Server
} finally {
    Stop-Server
}
$storeCheck2File = Join-Path $outDir 'store-check-2.txt'
$storeCheckOut = @(Invoke-Mb2Merged @('store-check', 'sheet', $store, $keyA, $keyB, $keyC, '2'))
$storeCheck = $LASTEXITCODE
Write-SmokeFile $storeCheck2File $storeCheckOut
$storeCheckOut | ForEach-Object { "  store 2: $_" }
if ($storeCheck -ne 0) { $script:status = 1 }
$clean = Get-SmokeLogCount $server2Log 'MB2SHEET role=server event=sheets_loaded schema=7 attributes=2 equipment=2 legacy_aliases=0 conflicts=0 repaired=0 reconciled=0'
$noCommit = Get-SmokeLogCount $server2Log 'MB2SHEET role=server event=sheet_migration_committed'
"  server phase 2: clean schema 7 load=$clean (want 1); migration commits=$noCommit (want 0)"
if (-not ($clean -eq 1 -and $noCommit -eq 0)) { $script:status = 1 }

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII character sheet smoke (ADR-085)'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content', 'Data'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "characters: $keyA $keyB $keyC"
    "decisions: $decisions"
    "store_sha256: $(Get-StoreHash)"
    ''
) + @(Get-SmokeLogLines $storeCheck1File | ForEach-Object { "store 1: $_" }) +
    @(Get-SmokeLogLines $storeCheck2File | ForEach-Object { "store 2: $_" }) + @('') +
    @(Get-CutMatches (Join-Path $outDir '*.log') 'MB2SHEET|MB2INTERACT role=server event=store_loaded' 'MB2SHEET|MB2INTERACT'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
