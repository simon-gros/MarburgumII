# Door and interior qualification (ADR-076) on the launch map, /Game/World/lent, against a real dedicated server started
# with -MB2DoorStarts. Windows twin of door-smoke-linux.sh (same phases, evidence lines and expectations). Players start
# at a building's street door (?MB2Start=door_<id>): building A (default Hotel Lent) and, at the same time, building B
# (default the vine museum, 172 m away) with emulated network lag on its clients.
#
# Phase 1, at each of A and B, two clients at once (only A's walker renders):
#   walker   (renders) opens the street door, sees the rival shut it and reverse the swing, stands in the swing while
#            the rival tries to shut it, then walks every room with ordinary movement input, opening each room door with
#            E, and back out to the street. One capture outside, in the doorway, in the front room and in every room.
#   rival    (no renderer) shuts the door and reverses it mid-swing, tries to shut it on the walker, asks for a room
#            door out of reach, and disconnects.
#   observer then joins while the walker is still there and must see exactly the door states the walker left.
#   tourist  (A only) joins while A's walker holds the building; the server (-MB2DoorTour) takes it 270 m away to
#            building C's door and back. A must stream out of its client and in again with the doors as the walker
#            left them (the door actors leave its relevancy on the way).
# Phase 1b, everyone gone from A: the server must drop A's interior (unstream) and a new observer must find every door
#   of A shut, built afresh, on the same server.
# Phase 2, server restarted: new observers at A and B must see every door shut (doors are not saved).
# The server log must show, for both buildings, the opening, the refusal with somebody in the swing, and the refusal
# out of reach.
#
# Usage: scripts\door-smoke-windows.ps1 [staged|editor] [building A] [building B]
# MB2_DOOR_LAG: the lag options for B's clients (default "-PktLag=120 -PktLagVariance=40 -PktLoss=1").
# MB2_DOOR_PORT (default 27895; never 7777 or 17777, which are for playtests).
# Evidence is written to Saved\DoorSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged', [string]$Building = 'r1539756', [string]$BuildingB = 'w539479841')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Building) { $Building = 'r1539756' }
if (-not $BuildingB) { $BuildingB = 'w539479841' }
$away = 'w158684433'
$lag = '-PktLag=120 -PktLagVariance=40 -PktLoss=1'; if ($env:MB2_DOOR_LAG) { $lag = $env:MB2_DOOR_LAG }
$lagArgs = @($lag -split '\s+' | Where-Object { $_ })
$port = 27895; if ($env:MB2_DOOR_PORT) { $port = [int]$env:MB2_DOOR_PORT }
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\door-smoke-windows.ps1 [staged|editor] [building id]'
$outDir = New-SmokeOutDir 'DoorSmoke'

# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$headlessArgs = @('-NullRHI')
$server = $null
$status = 0

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last of several evidence prefixes.
function Get-SummaryLines([string[]]$Path, [string]$Pattern, [string]$Cut) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cutRegex = New-Object System.Text.RegularExpressions.Regex("^.*($Cut)")
    foreach ($line in @(Get-SmokeLogLines $Path)) {
        if ($regex.IsMatch($line)) { $cutRegex.Replace($line, '$1', 1) }
    }
}

# Returns the server role, or $null (after the bash runner's message) when it did not come up; the caller exits 1.
function Start-DoorServer([string]$Log) {
    # A June noon, clear and dry, so the captures read the same on any day.
    $role = Start-SmokeRole -Target $targets.Server -Log $Log -Arguments (@($map, "-port=$port",
            "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", '-MB2Weather=clear_dry',
            '-MB2Clock=2026-06-18T10:00:00Z', '-MB2DoorStarts', "-MB2DoorTour=$away") + $commonArgs)
    if (Wait-SmokeLog $Log "MB2SMOKE role=server event=ready .* port=$port " 120 $role) { return $role }
    $alive = $role.Alive
    Stop-SmokeRole $role 0
    if ($alive) { Write-Warn "Server did not become ready; see $Log" } else { Write-Warn "Server exited before becoming ready; see $Log" }
    $null
}

function Get-Url([string]$Suffix, [string]$Id = $Building) {
    "127.0.0.1:${port}?MB2Character=mb2d-$token-${Suffix}?MB2Start=door_$Id"
}

# Clients run without a wall-clock limit, as in the bash runner (no timeout wrapper there).
function Start-DoorClient([string]$LogName, [string[]]$Arguments) {
    Start-SmokeRole -Target $targets.Client -Log (Join-Path $outDir $LogName) -Arguments $Arguments
}

function Invoke-DoorClient([string]$LogName, [string[]]$Arguments) {
    Invoke-SmokeRole -Target $targets.Client -Log (Join-Path $outDir $LogName) -Arguments $Arguments
}

$routeDonePattern = 'MB2DSMOKE role=walker event=(route_done|fail)'
$routeStatesPattern = 'MB2DSMOKE role=walker event=route_done.* states=([a-z,]*)'

# Wait-Route <log> <role>: until that walker has finished its route (or failed, or exited); returns the states it left.
function Wait-Route([string]$Log, $Role) {
    [void](Wait-SmokeLog $Log $routeDonePattern 300 $Role)
    $states = Get-SmokeLogValue $Log $routeStatesPattern
    if ($null -eq $states) { '' } else { $states }
}

function Write-ClientResult([string]$Label, [string]$Log, [int]$Code) {
    if ($Code -eq 0 -and (Test-SmokeLog $Log 'MB2DSMOKE role=[a-z]* event=pass')) {
        "  ${Label}: PASS"
    } else {
        "  ${Label}: FAIL (exit $Code)"
        Get-SmokeLogMatches $Log 'MB2DSMOKE role=[a-z]* event=(fail|expectation_failed)' 'MB2DSMOKE' '    '
        $script:status = 1
    }
}

# Test-Expect <log> <label> <want: number or ">=N"> <pattern>
function Test-Expect([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = ''
    if (Test-Path -LiteralPath $Log) { $count = Get-SmokeLogCount $Log $Pattern }
    "  $Label=$count (want $Want)"
    $n = 0; [void][int]::TryParse("$count", [ref]$n)
    if ($Want.StartsWith('>=')) {
        if ($n -lt [int]$Want.Substring(2)) { $script:status = 1 }
    } elseif ($n -ne [int]$Want) {
        $script:status = 1
    }
}

function Out-Log([string]$Name) { Join-Path $outDir $Name }

# B's late joiner, run alongside the main flow (the bash subshell): waits for B's walker, then joins with its states.
$observerBScript = @'
param($ModulePath, $WalkerLog, $WalkerRole, $Target, $Arguments, $Log, $CodeFile, $DonePattern, $StatesPattern)
$ErrorActionPreference = 'Stop'
Import-Module $ModulePath -DisableNameChecking
[void](Wait-SmokeLog $WalkerLog $DonePattern 300 $WalkerRole)
$left = Get-SmokeLogValue $WalkerLog $StatesPattern
if (-not $left) {
    Write-SmokeFile $CodeFile @('skipped')
    return
}
$code = Invoke-SmokeRole -Target $Target -Log $Log -Arguments ([string[]]$Arguments + @("-MB2DoorExpect=$left", '-MB2DoorTimeout=180'))
Write-SmokeFile $CodeFile @("$code")
'@

$observerB = $null
try {
    if (Test-UdpPortInUse $port) {
        Exit-Failure "UDP port $port is already in use; stop the other server or set MB2_DOOR_PORT."
    }
    "Evidence in $outDir"
    "Phase 1: dedicated server ($Mode) on $map, port $port, building $Building"
    $server = Start-DoorServer (Out-Log 'server-1.log')
    if (-not $server) { exit 1 }

    # Building B first (headless, lagged): its route runs alongside A's.
    $walkerB = Start-DoorClient 'b-walker.log' (@(Get-Url 'ba' $BuildingB) + $commonArgs + $headlessArgs + $lagArgs + @(
            '-MB2DoorSmoke=walker', "-MB2DoorBuilding=$BuildingB", '-MB2DoorTimeout=420', '-MB2DoorHold=150'))
    $rivalB = Start-DoorClient 'b-rival.log' (@(Get-Url 'bb' $BuildingB) + $commonArgs + $headlessArgs + $lagArgs + @(
            '-MB2DoorSmoke=rival', "-MB2DoorBuilding=$BuildingB", '-MB2DoorTimeout=300'))
    # B's late joiner as soon as B's walker is done, while it still holds the building (it does not wait for A).
    $observerB = [powershell]::Create()
    [void]$observerB.AddScript($observerBScript)
    foreach ($a in @((Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1'), (Out-Log 'b-walker.log'), $walkerB, $targets.Client,
            [string[]](@(Get-Url 'bc' $BuildingB) + $commonArgs + $headlessArgs + $lagArgs + @('-MB2DoorSmoke=observer', "-MB2DoorBuilding=$BuildingB")),
            (Out-Log 'b-observer-1.log'), (Out-Log 'b-observer-1.code'), $routeDonePattern, $routeStatesPattern)) {
        [void]$observerB.AddArgument($a)
    }
    $observerBHandle = $observerB.BeginInvoke()

    $walker = Start-DoorClient 'walker.log' (@(Get-Url 'a') + $commonArgs + $renderArgs + @('-MB2DoorSmoke=walker',
            "-MB2DoorBuilding=$Building", "-MB2DoorShot=$(Out-Log 'walker')", '-MB2DoorTimeout=420', '-MB2DoorHold=150'))
    $code = Invoke-DoorClient 'rival.log' (@(Get-Url 'b') + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=rival',
            "-MB2DoorBuilding=$Building", '-MB2DoorTimeout=300'))
    Write-ClientResult 'rival' (Out-Log 'rival.log') $code

    # A's late joiner and the tourist, while the walker still holds the building: the same doors, in the same states.
    $left = Wait-Route (Out-Log 'walker.log') $walker
    if ($left) {
        $tourist = Start-DoorClient 'tourist.log' (@(Get-Url 'tour') + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=tourist',
                "-MB2DoorBuilding=$Building", "-MB2DoorExpect=$left", '-MB2DoorTimeout=180'))
        $code = Invoke-DoorClient 'observer-1.log' (@(Get-Url 'c') + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=observer',
                "-MB2DoorBuilding=$Building", "-MB2DoorExpect=$left", '-MB2DoorTimeout=180'))
        Write-ClientResult 'observer (late join)' (Out-Log 'observer-1.log') $code
        $code = Wait-SmokeRole $tourist; $tourist.Dispose()
        Write-ClientResult 'tourist (stream out and in)' (Out-Log 'tourist.log') $code
    } else {
        '  observer (late join): SKIPPED, the walker did not finish its route'
        '  tourist (stream out and in): SKIPPED'
        $status = 1
    }
    $code = Wait-SmokeRole $rivalB; $rivalB.Dispose()
    Write-ClientResult 'B rival' (Out-Log 'b-rival.log') $code
    try { [void]$observerB.EndInvoke($observerBHandle) } catch { Write-Warn "B observer runner failed: $($_.Exception.Message)" }
    $observerB.Dispose(); $observerB = $null
    $codeFile = Out-Log 'b-observer-1.code'
    $observerBCode = ''
    if (Test-Path -LiteralPath $codeFile) { $observerBCode = ([IO.File]::ReadAllText($codeFile)).Trim() }
    if ($observerBCode -match '^[0-9]+$') {
        Write-ClientResult 'B observer (late join, lagged)' (Out-Log 'b-observer-1.log') ([int]$observerBCode)
    } else {
        "  B observer (late join): SKIPPED, B's walker did not finish its route"
        $status = 1
    }
    $code = Wait-SmokeRole $walker; $walker.Dispose()
    Write-ClientResult 'walker' (Out-Log 'walker.log') $code
    $code = Wait-SmokeRole $walkerB; $walkerB.Dispose()
    Write-ClientResult 'B walker (lagged)' (Out-Log 'b-walker.log') $code

    "Phase 1b: everyone gone from $Building, same server"
    [void](Wait-SmokeLog (Out-Log 'server-1.log') "MB2INTERIOR event=unstream id=$Building\b" 30)
    $code = Invoke-DoorClient 'observer-empty.log' (@(Get-Url 'd') + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=observer',
            "-MB2DoorBuilding=$Building", '-MB2DoorExpect=closed', '-MB2DoorTimeout=180'))
    Write-ClientResult 'observer (after the building emptied)' (Out-Log 'observer-empty.log') $code
    Stop-SmokeRole $server
    $server = $null

    'Phase 2: server restarted'
    $server = Start-DoorServer (Out-Log 'server-2.log')
    if (-not $server) { exit 1 }
    $code = Invoke-DoorClient 'observer-2.log' (@(Get-Url 'e') + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=observer',
            "-MB2DoorBuilding=$Building", '-MB2DoorExpect=closed', '-MB2DoorTimeout=180'))
    Write-ClientResult 'observer (after restart)' (Out-Log 'observer-2.log') $code
    $code = Invoke-DoorClient 'b-observer-2.log' (@(Get-Url 'be' $BuildingB) + $commonArgs + $headlessArgs + @('-MB2DoorSmoke=observer',
            "-MB2DoorBuilding=$BuildingB", '-MB2DoorExpect=closed', '-MB2DoorTimeout=180'))
    Write-ClientResult 'B observer (after restart)' (Out-Log 'b-observer-2.log') $code
    Stop-SmokeRole $server
    $server = $null
} finally {
    Stop-SmokeRole $server
    $server = $null
    if ($observerB) { $observerB.Dispose() }
}

$server1 = Out-Log 'server-1.log'
Test-Expect $server1 'server door_starts' '1' 'MB2INTERIOR event=door_starts starts=[1-9]'
foreach ($b in $Building, $BuildingB) {
    Test-Expect $server1 "server $b entrance_open" '1' "MB2INTERIOR event=entrance_open id=$b "
    Test-Expect $server1 "server $b door_opening" '>=2' "event=door_opening .* building=$b"
    Test-Expect $server1 "server $b door_closing" '>=1' "event=door_closing .* building=$b"
    Test-Expect $server1 "server $b refused_in_swing" '>=1' "event=door_refused_blocked .* building=$b"
    Test-Expect $server1 "server $b refused_reach" '>=1' "event=door_refused .* building=$b reason=reach"
}
Test-Expect $server1 'server tour away' '1' "MB2DOORTOUR role=server event=away .* to=$away moved=1"
Test-Expect $server1 'server tour back' '1' 'MB2DOORTOUR role=server event=back .* moved=1'
# A streamed in for the walker, out once its last player left, and in again for the observer that followed.
Test-Expect $server1 "server $Building stream" '>=2' "MB2INTERIOR event=stream id=$Building "
Test-Expect $server1 "server $Building unstream" '>=1' "MB2INTERIOR event=unstream id=$Building`$"
Test-Expect (Out-Log 'tourist.log') 'tourist streamed_out' '1' 'MB2DSMOKE role=tourist event=streamed_out'
Test-Expect (Out-Log 'tourist.log') 'tourist back with the doors as left' '1' 'MB2DSMOKE role=tourist event=tour_back .* match=1'
Test-Expect (Out-Log 'walker.log') 'walker rooms_shot' '>=1' 'MB2DSMOKE role=walker event=route_done .* rooms_shot=[1-9]'
Test-Expect (Out-Log 'b-walker.log') 'B walker rooms walked' '>=1' 'MB2DSMOKE role=walker event=route_done legs=[1-9]'

$summary = Out-Log 'summary.txt'
$summaryLogs = @((Out-Log 'server-*.log'), (Out-Log 'walker.log'), (Out-Log 'rival.log'), (Out-Log 'observer-*.log'),
    (Out-Log 'tourist.log'), (Out-Log 'b-*.log'))
Write-SmokeFile $summary (@(
    'MarburgumII door smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "buildings: $Building (A) $BuildingB (B, lag: $($lagArgs -join ' ')) away: $away"
    "host: $(Get-SmokeHost)"
    ''
) + @(Get-SummaryLines $summaryLogs 'MB2DSMOKE|MB2DOORTOUR|MB2INTERIOR event=(classified|door_starts|entrance|stream|unstream)|event=door_|MB2SMOKE role=server event=(ready|login|logout)' 'MB2DSMOKE|MB2INTERIOR|MB2INTERACT|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
