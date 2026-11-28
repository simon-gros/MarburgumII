# Fighting qualification (ADR-041) on the launch map, /Game/World/lent. Windows twin of combat-smoke-linux.sh (same
# clients, evidence lines and expectations). The server spawns the original's Lent fight cast; both clients start in
# front of the Belligerent drinker (?MB2Start=fight_lent_drunk), one after the other.
#
#   B  beaten (-nullrhi). Jabs the drinker once, so he turns on B, then stands with its hands down until his
#      blows put it on the floor: it shows the fall, cannot swing from the ground, and he gives up on it.
#   A  the fight (rendered offscreen). A jab out of reach misses and leaves him alone. In reach, a jab lands for
#      7 and a second at once is too soon. He turns on A and the Evening walker steps away. A's raised guard
#      (its clip shows) blocks his jab for a quarter and pays its stamina; hands down, his next jab lands in
#      full. A heavy swing lands for 14.7, then A keeps swinging until he is down (his fall plays); a jab at
#      somebody on the floor reaches nobody.
# The server must log the three people spawned and ready, the drinker struck down once and every swing ruled; the
# clients must log impact sounds for hits, the blocked blow and the fall.
#
# A renders offscreen (see interaction-smoke-windows.ps1 for the GPU note).
# Usage: scripts\combat-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\CombatSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27710; if ($env:MB2_COMBAT_PORT) { $port = [int]$env:MB2_COMBAT_PORT }
$map = '/Game/World/lent'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$keyA = "mb2a-$token"
$keyB = "mb2b-$token"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\combat-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'CombatSmoke'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs with a wall-clock limit, which stops its whole job.
$wall = Get-ClientWallSeconds

$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
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

# Invoke-Client <role> <character key> <extra args>; output to client-<role>.log. Returns the exit code.
function Invoke-Client([string]$Role, [string]$Key, [string[]]$More) {
    $url = "127.0.0.1:${port}?MB2Start=fight_lent_drunk?MB2Character=$Key"
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
        $code = Invoke-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log `
            -Arguments (@($url) + $commonArgs + $More + @('-MB2CombatSmoke', "-MB2CombatRole=$Role", '-MB2CombatTimeout=240'))
    }
    $code
}

function Test-Client([string]$Role, [int]$Code) {
    $log = Join-Path $outDir "client-$Role.log"
    if ($Code -eq 0 -and (Test-SmokeLog $log "MB2CSMOKE role=client tag=$Role event=pass")) {
        "  client ${Role}: PASS"
    } else {
        "  client ${Role}: FAIL (exit $Code)"
        Get-SmokeLogMatches $log "MB2CSMOKE role=client tag=$Role event=(fail|expectation_failed)" 'MB2CSMOKE' '    '
        $script:status = 1
    }
}

function Get-Shot([string]$Name) { $renderArgs + @("-MB2CombatShot=$(Join-Path $outDir $Name)") }

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

try {
    "Dedicated server ($Mode) on $map, port $port; evidence in $outDir"
    $server = Start-SmokeServer -Target $targets.Server -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store") + $commonArgs) `
        -Log (Join-Path $outDir 'server.log') -Port $port -PortVariable 'MB2_COMBAT_PORT' -ReadySeconds 120
    # B first: it is beaten down and leaves; then A fights the drinker, who has given up on B.
    $code = Invoke-Client 'B' $keyB @('-nullrhi'); Test-Client 'B' $code
    $code = Invoke-Client 'A' $keyA @(Get-Shot 'combat-a'); Test-Client 'A' $code
    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}

$log = Join-Path $outDir 'server.log'
$clientA = Join-Path $outDir 'client-A.log'
Test-Expect $log 'npc_spawned' '3' 'MB2COMBAT role=server event=npc_spawned npc='
Test-Expect $log 'npc_ready' '3' 'MB2COMBAT role=server event=npc_ready npc='
Test-Expect $log 'drinker_down' '1' 'MB2COMBAT role=server event=npc_struck npc=lent_drunk .* down=1'
Test-Expect $log 'drinker_strikes' '>=3' 'MB2COMBAT role=server event=npc_strike npc=lent_drunk '
Test-Expect $log 'blocked_by_guard' '>=1' 'MB2COMBAT role=server event=struck .* outcome=blocked '
Test-Expect $log 'player_beaten' '1' 'MB2COND role=server event=beaten '
Test-Expect $log 'swings_ruled' '>=6' 'MB2COMBAT role=server event=swing '
Test-Expect $log 'refused_too_soon' '>=1' 'MB2COMBAT role=server event=swing .* ok=0 reason=too_soon '
Test-Expect $log 'refused_from_floor' '1' 'MB2COMBAT role=server event=swing .* ok=0 reason=incapacitated '
Test-Expect $log 'walker_avoids' '>=1' 'MB2COMBAT role=server event=npc_.* npc=lent_passerby .* intent=avoid '
Test-Expect $log 'unknown_npc' '0' 'MB2COMBAT role=server event=npc_unknown'
Test-Expect $clientA 'impact_hit_sounds' '>=2' 'MB2COMBAT role=client event=impact outcome=hit .* sound=impactPunch_'
Test-Expect $clientA 'impact_blocked_sound' '>=1' 'MB2COMBAT role=client event=impact outcome=blocked .* sound=impactSoft_medium_'
Test-Expect $clientA 'impact_floor_sound' '>=1' 'MB2COMBAT role=client event=impact .* floored=1 .* fall=impactSoft_heavy_'

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII combat smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "host: $(Get-SmokeHost)"
    ''
) + @(Get-SummaryLines (Join-Path $outDir '*.log') 'MB2CSMOKE|MB2COMBAT|MB2COND role=server event=(beaten|recovery_point)|MB2SMOKE role=server event=(ready|login|logout)' 'MB2CSMOKE|MB2COMBAT|MB2COND|MB2SMOKE'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
