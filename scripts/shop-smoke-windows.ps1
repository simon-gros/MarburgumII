# Windows twin of shop-smoke-linux.sh (same phases, evidence lines and expectations).
# Kiosk 67, Petra Kralj's counter (ADR-060), end to end on /Game/World/lent.
#
# The world store is seeded as a schema 4 document (revision 1): one character with 50 money and a brass key, as a
# character that had been paid for a mission would have. The counters' clock is set with -MB2ClockHour and
# -MB2ClockWeekday, so the run does not depend on the hour it is started.
#
# Phase 1, Thursday 10:00, the counter open:
#   BUY     at Petra's development start, through the shop panel: the starting shelves; a newspaper bought (-20,
#           shelf 11, Trading +10 for the first purchase); that operation replayed (duplicate, nothing moves); a
#           postcard it cannot afford, six newspapers and the brass key refused; the newspaper sold back (+8, shelf
#           12 again); selling it twice refused; a pen bought (-15, no XP).
#   FAR     from the arrival start, a purchase at Petra is refused as out of range.
# The server is stopped and the store checked (mb2 store-check shop): rewritten at schema 7, three commits.
# Phase 2, Sunday 12:00, same store: CLOSED sees money, pen, XP and shelves as left, the counter shut, and a purchase
# refused as shop_closed. The store must be byte-identical afterwards.
#
# BUY and CLOSED render offscreen (screenshots); FAR runs -nullrhi.
# Usage: scripts\shop-smoke-windows.ps1 [staged|editor]. Evidence: Saved\ShopSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27713; if ($env:MB2_SHOP_PORT) { $port = [int]$env:MB2_SHOP_PORT }
$map = '/Game/World/lent'
$key = "mb2k-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\shop-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'ShopSmoke'
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$script:server = $null
$script:status = 0

# start_server <log> <clock args>
function Start-Server([string]$Log, [string[]]$Clock) {
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_SHOP_PORT' `
        -ReadySeconds 180 -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $Clock + $commonArgs)
}

function Stop-Server {
    Stop-SmokeRole $script:server
    $script:server = $null
}

# run <label> <role> <start tag> <extra args>; output to client-<label>.log
function Invoke-Client([string]$Label, [string]$Role, [string]$Start, [string[]]$More = @()) {
    $url = "127.0.0.1:${port}?MB2Start=${Start}?MB2Character=$key"
    $log = Join-Path $outDir "client-$Label.log"
    $arguments = @($url) + $commonArgs + $More + @('-MB2ShopSmoke', "-MB2ShopRole=$Role", '-MB2ShopTimeout=120')
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client-$Label.gpu-init-failed-$attempt.log") -Force
            "  client ${Label}: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log -Arguments $arguments
    }
    if ($code -eq 0 -and (Test-SmokeLog $log "MB2SHOP role=client tag=$Role event=pass")) {
        "  client ${Label}: PASS"
    } else {
        "  client ${Label}: FAIL (exit $code)"
        Get-SmokeLogMatches $log "MB2SHOP role=client tag=$Role event=(fail|expectation_failed)" 'MB2SHOP' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2ShopShot=$(Join-Path $outDir $Name)") }

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
    Write-SmokeText $store ('{"format":"marburgumii.world_store","schema_version":4,"revision":1,"stocks":{},"inventories":{"' +
        $key + '":{"brass_key":1}},"missions":{},"conditions":{},"skills":{},"money":{"' + $key + '":50},"receipts":{}}' + "`n")
    "Phase 1: dedicated server ($Mode) on $map, port $port, Thursday 10:00, seeded store; evidence in $outDir"
    $server1Log = Join-Path $outDir 'server-1.log'
    Start-Server $server1Log @('-MB2ClockWeekday=3', '-MB2ClockHour=10')
    [void](Wait-SmokeLog $server1Log 'MB2SHOP role=server event=counter' 20 -PollSeconds 0.25)

    Invoke-Client 'buy' 'BUY' 'mission_petra_kralj' (Get-Shot 'buy')
    Invoke-Client 'far' 'FAR' 'arrival' @('-nullrhi')
    Start-Sleep -Seconds 1
    Stop-Server

    $firstOp = @(Get-SmokeLogLines (Join-Path $outDir 'client-buy.log') |
        Where-Object { $_ -cmatch 'MB2SHOP role=client tag=BUY event=receipt .* effect=item_bought item=mariborski_glas' } |
        Select-Object -First 1 | ForEach-Object { [regex]::Replace($_, '.* op=([0-9A-F]*) .*', '$1') })
    $opArg = 'none'; if ($firstOp.Count -and $firstOp[0]) { $opArg = $firstOp[0] }
    $storeCheckFile = Join-Path $outDir 'store-check.txt'
    $storeCheckOut = @(Invoke-Mb2Merged @('store-check', 'shop', $store, $key, $opArg))
    $storeCheck = $LASTEXITCODE
    Write-SmokeFile $storeCheckFile $storeCheckOut
    $storeCheckOut | ForEach-Object { "  store: $_" }
    if ($storeCheck -ne 0) { $script:status = 1 }
    $loaded1 = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=store_loaded .* schema=4 revision=1 '
    $trades = -join @(Get-SmokeLogLines $server1Log | Where-Object { $_ -cmatch 'MB2SHOP role=server event=trade ' } |
        ForEach-Object { ([regex]::Replace($_, '.* action=([a-z]*) item=([a-z_]*) quantity=([0-9-]*) .* decision=([a-z_]*).*', '$1:$2:$3:$4')) + ' ' })
    # The replay is answered from the stored receipt before the counter rule runs, so it has no trade line.
    $wantTrades = 'buy:mariborski_glas:1:item_bought buy:maribor_postcard:1:insufficient_money buy:mariborski_glas:6:invalid_quantity ' +
        'sell:brass_key:1:item_not_accepted sell:mariborski_glas:1:item_sold sell:mariborski_glas:1:insufficient_items buy:ballpoint_pen:1:item_bought '
    $farRefused = Get-SmokeLogCount $server1Log 'MB2INTERACT role=server event=interaction .* reason=out_of_range'
    "  server phase 1: store_loaded(schema 4)=$loaded1 (want 1); out_of_range=$farRefused (want 1)"
    "  trades: [ $trades]"
    if (-not ($loaded1 -eq 1 -and $farRefused -eq 1 -and $trades -ceq $wantTrades)) { "  want:   [ $wantTrades]"; $script:status = 1 }
    $storeHash = Get-StoreHash

    # ---------------------------------------------------------------- phase 2
    'Phase 2: restart the server on the same store, Sunday 12:00'
    $server2Log = Join-Path $outDir 'server-2.log'
    Start-Server $server2Log @('-MB2ClockWeekday=6', '-MB2ClockHour=12')
    Invoke-Client 'closed' 'CLOSED' 'mission_petra_kralj' (Get-Shot 'closed')
    Start-Sleep -Seconds 1
    Stop-Server
} finally {
    Stop-Server
}
$loaded2 = Get-SmokeLogCount $server2Log 'MB2INTERACT role=server event=store_loaded .* schema=7 revision=4 '
$closed = Get-SmokeLogCount $server2Log 'MB2SHOP role=server event=trade .* decision=shop_closed'
$storeHash2 = Get-StoreHash
$unchanged = 'NO'; if ($storeHash -eq $storeHash2) { $unchanged = 'yes' }
"  server phase 2: store_loaded(schema 7, revision 4)=$loaded2 (want 1); shop_closed=$closed (want 1); store unchanged: $unchanged"
if (-not ($loaded2 -eq 1 -and $closed -eq 1 -and $storeHash -eq $storeHash2)) { $script:status = 1 }

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII Kiosk 67 counter smoke (ADR-060)'
    "result: $(Get-PassFail $script:status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content', 'Data'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    "character: $key"
    "trades: $trades"
    "store_sha256: $storeHash"
    ''
) + @(Get-SmokeLogLines $storeCheckFile | ForEach-Object { "store: $_" }) + @('') +
    @(Get-CutMatches (Join-Path $outDir '*.log') 'MB2SHOP|MB2INTERACT role=server event=(interaction|store_loaded)' 'MB2SHOP|MB2INTERACT'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $script:status) - $summary"
exit $script:status
