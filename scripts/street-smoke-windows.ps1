# Street people qualification (ADR-044) on the launch map, /Game/World/lent. Windows twin of street-smoke-linux.sh
# (same phases, evidence lines and expectations). The server fills the pavements round
# the player from the walk graph and, with -MB2StreetProbe, puts two people in the way of the first car a player
# drives off in: one who will not jump clear (and is easily out) and one further on who will. One client, rendered
# offscreen, starts at the hatchback's driver door (?MB2Start=lent_hatchback):
#
#   people appear round it, out of sight, and walk; it throws punches at the air and people near flee or watch;
#   it drives off: the first probe goes down under the car, the second jumps aside; people answer the run-over,
#   and once it has faded a passer-by sent along the pavement answers the body.
#   and, as it arrives, a person by the Drava is scared from the land side and runs along the bank, not into the
#   river (ADR-046); nobody on the street is ever in the water.
#
# The server must log the walk graph loaded, people spawned and despawned, reactions to the fight, the run-over,
# the dodge and the body. The server runs the clear_dry weather (ADR-061) so a real wet day never changes how the car
# meets the probes. Evidence is written to Saved\StreetSmoke\<UTC timestamp>\.
# Usage: scripts\street-smoke-windows.ps1 [staged|editor]   (port: MB2_STREET_PORT, default 27761; the street's seed:
# MB2_STREET_SEED, default 1990; MB2_STREET_PLACES=0 leaves out the places that draw people in, ADR-069)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27761; if ($env:MB2_STREET_PORT) { $port = [int]$env:MB2_STREET_PORT }
$seed = '1990'; if ($env:MB2_STREET_SEED) { $seed = $env:MB2_STREET_SEED }
$places = '1'; if ($env:MB2_STREET_PLACES) { $places = $env:MB2_STREET_PLACES }
$map = '/Game/World/lent'
$key = "mb2s-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\street-smoke-windows.ps1 [staged|editor]'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under a wall-clock limit that stops its whole job.
$wall = Get-ClientWallSeconds

$outDir = New-SmokeOutDir 'StreetSmoke'
$store = Join-Path $outDir 'world-store.json'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$server = $null
$status = 0

function Start-StreetServer([string]$Log) {
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_STREET_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$store", '-MB2StreetProbe', "-MB2StreetSeed=$seed",
            "-MB2StreetPlaces=$places", '-MB2Weather=clear_dry', '-MB2Clock=2026-06-18T08:00:00Z') + $commonArgs)
}

# Sets $script:clientCode to the last attempt's exit code.
function Invoke-StreetClient {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=$key"
    $log = Join-Path $outDir 'client.log'
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "client.gpu-init-failed-$attempt.log") -Force
            "  client: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $role = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log `
            -Arguments (@($url) + $commonArgs + $renderArgs + @('-MB2StreetSmoke', "-MB2StreetShot=$(Join-Path $outDir 'street')", '-MB2StreetTimeout=300'))
        try { $code = Wait-SmokeRole $role } finally { $role.Dispose() }
    }
    $script:clientCode = $code
}

# Expect-Log <log> <label> <want: number or ">=N"> <pattern>
function Expect-Log([string]$Log, [string]$Label, [string]$Want, [string]$Pattern) {
    $count = Get-SmokeLogCount $Log $Pattern
    "  $([IO.Path]::GetFileNameWithoutExtension($Log)): $Label=$count (want $Want)"
    if ($Want.StartsWith('>=')) {
        if (-not ($count -ge [int]$Want.Substring(2))) { $script:status = 1 }
    } else {
        if (-not ($count -eq [int]$Want)) { $script:status = 1 }
    }
}

try {
    "Dedicated server ($Mode) on $map, port $port; evidence in $outDir"
    $server = Start-StreetServer (Join-Path $outDir 'server.log')
    $clientCode = 0
    Invoke-StreetClient
    $clientLog = Join-Path $outDir 'client.log'
    if ($clientCode -eq 0 -and (Test-SmokeLog $clientLog 'MB2SSMOKE role=client event=pass')) {
        '  client: PASS'
    } else {
        "  client: FAIL (exit $clientCode)"
        Get-SmokeLogMatches $clientLog 'MB2SSMOKE role=client event=(fail|expectation_failed)' 'MB2SSMOKE' '    '
        $status = 1
    }
    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}

$log = Join-Path $outDir 'server.log'
Expect-Log $log 'street_ready' '1' 'MB2_STREET role=server event=street_ready '
Expect-Log $log 'spawned' '>=10' 'MB2_STREET role=server event=spawn .* why=(in_view|out_of_view) '
Expect-Log $log 'spawned_too_close' '0' 'MB2_STREET role=server event=spawn .* why=(in_view|out_of_view) player_m=([0-9]|1[0-9]|2[01])\.'
Expect-Log $log 'fight_incident' '>=1' 'MB2_STREET role=server event=incident .* kind=fight player=1 '
Expect-Log $log 'fight_reactions' '>=1' 'MB2_STREET role=server event=react .* event=fight response=(flee|watch) '
Expect-Log $log 'probe' '1' 'MB2_STREET role=server event=probe vehicle=lent_hatchback '
# A street person's id is the FName street_<n>. An FName prints as the first spelling of its name the process registered,
# so where content registers "Street" first (the five by five does, ADR-090) the same people log as Street_<n>.
Expect-Log $log 'run_over' '>=1' 'MB2_STREET role=server event=run_over npc=[Ss]treet_.* vehicle=lent_hatchback .* player_driving=1 '
Expect-Log $log 'dodged' '>=1' 'MB2_STREET role=server event=dodge npc=[Ss]treet_'
Expect-Log $log 'answered_run_over' '>=1' 'MB2_STREET role=server event=react .* event=run_over '
Expect-Log $log 'probe_finder' '1' 'MB2_STREET role=server event=probe_finder npc=[Ss]treet_'
Expect-Log $log 'answered_body' '>=1' 'MB2_STREET role=server event=react .* event=body '
# The person the car put out lies there until nobody is close and nobody sees it: never culled like a walker.
$victim = $null
$victimRegex = New-Object System.Text.RegularExpressions.Regex('MB2_STREET role=server event=run_over npc=([Ss]treet_[0-9]*) .* out=1 ')
foreach ($line in @(Get-SmokeLogLines $log)) {
    $m = $victimRegex.Match($line)
    if ($m.Success) { $victim = $m.Groups[1].Value; break }
}
if ($victim) {
    Expect-Log $log 'body_culled_as_walker' '0' "MB2_STREET role=server event=despawn npc=$victim reason=(unseen|far) "
}
Expect-Log $log 'gathered_round_body' '>=0' 'MB2_STREET role=server event=gathered '
Expect-Log $log 'graph_off' '0' 'MB2_STREET role=server event=street_off'
# Nobody walks into the Drava (ADR-046): the graph is checked against the water, and nobody is ever in it.
Expect-Log $log 'graph_checked_for_water' '1' 'MB2_STREET role=server event=street_ready .* water_volumes=[1-9][0-9]* '
Expect-Log $log 'people_in_water' '0' 'MB2_STREET role=server event=street_summary .* swimming=[1-9]'
# The water probe: scared from the land side by the river, the person runs along the bank and never swims.
Expect-Log $log 'water_probe' '1' 'MB2_STREET role=server event=water_probe npc=[Ss]treet_.* straight_away_wet=1 '
Expect-Log $log 'water_probe_dry' '1' 'MB2_STREET role=server event=water_probe_done npc=[Ss]treet_.* swam=0 '

$summary = Join-Path $outDir 'summary.txt'
$evidence = @()
$evidenceRegex = New-Object System.Text.RegularExpressions.Regex('MB2SSMOKE|MB2_STREET role=server event=(street_ready|street_summary|incident|react|run_over|dodge|probe|gathered|despawn|water_edge|water_probe|water_probe_done|water_probe_skipped)|MB2SMOKE role=server event=(ready|login|logout)')
foreach ($line in @(Get-SmokeLogLines (Join-Path $outDir '*.log'))) {
    if ($evidenceRegex.IsMatch($line)) { $evidence += [regex]::Replace($line, '^.*(MB2SSMOKE|MB2_STREET|MB2SMOKE)', '$1') }
}
$lines = @(
    'MarburgumII street smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "seed: $seed"
    "places: $places"
    "host: $(Get-SmokeHost)"
    ''
) + $evidence
Write-SmokeFile $summary $lines
# bash runs under set -e -o pipefail: a summary grep that selects nothing ends the run there (exit 1).
if (-not $evidence.Count) { exit 1 }
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
