# Streets by the hour (ADR-067) on the launch map, /Game/World/lent. Windows twin of street-hours-smoke-linux.sh
# (same phases, evidence lines and expectations). For each phase a dedicated server runs with its
# world clock pinned (and, for one, running) and one client, rendered offscreen, stands at the hatchback's driver door
# looking along the street and counts the people within 70 m every second (-MB2StreetCount). The server logs the hour
# it reads, how many people it keeps and who it sends out; the client what it sees.
#
#   day       Tuesday 18 June 1996, 10:00, dry: the day, 20 people, ADR-044's mix
#   evening   Tuesday 19:00: the evening, 18, night owls among them
#   late      Tuesday 22:30: the late evening, 9
#   night     Wednesday 03:00: the small hours, 3, mostly night owls
#   friday    Friday 21 June 1996, 21:30: still the evening, an hour later than a weekday's
#   sunday    Sunday 23 June 1996, 12:00: the day, but no commuters and the shops shut, so no shoppers
#   rain      Tuesday 10:00 in the rain_wet profile: under half the day's people
#   home      Tuesday 20:00 with the clock at x20: past 21:00 the evening's people go home, unseen ones only
#
# Evidence is written to Saved\StreetHoursSmoke\<UTC timestamp>\<phase>-*.
# Usage: scripts\street-hours-smoke-windows.ps1 [staged|editor]   (port: MB2_STREET_PORT, default 27762;
# phases: MB2_STREET_PHASES, default all of the above)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27762; if ($env:MB2_STREET_PORT) { $port = [int]$env:MB2_STREET_PORT }
$phases = 'day evening late night friday sunday rain home'; if ($env:MB2_STREET_PHASES) { $phases = $env:MB2_STREET_PHASES }
$phaseList = @($phases -split '\s+' | Where-Object { $_ })
$map = '/Game/World/lent'
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\street-hours-smoke-windows.ps1 [staged|editor]'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under a wall-clock limit that stops its whole job.
$wall = Get-ClientWallSeconds

$outDir = New-SmokeOutDir 'StreetHoursSmoke'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$server = $null
$status = 0
$invariant = [Globalization.CultureInfo]::InvariantCulture

# Start-StreetServer <log> <server args...>
function Start-StreetServer([string]$Log, [string[]]$More) {
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_STREET_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", '-MB2StreetSeed=1990') + $More + $commonArgs)
}

# Invoke-StreetClient <phase> <count seconds>; sets $script:clientCode to the last attempt's exit code.
function Invoke-StreetClient([string]$Phase, [int]$Count) {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=mb2h-$Phase-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
    $log = Join-Path $outDir "$Phase-client.log"
    $code = 0
    for ($attempt = 0; $attempt -le 2; $attempt++) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $log))) { break }
            Move-Item -LiteralPath $log -Destination (Join-Path $outDir "$Phase-client.gpu-init-failed-$attempt.log") -Force
            "  client: GPU device creation failed before connecting; retry $attempt in 3 s"
            Start-Sleep -Seconds 3
            $code = 0
        }
        $role = Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log $log `
            -Arguments (@($url) + $commonArgs + $renderArgs + @('-MB2StreetSmoke', "-MB2StreetShot=$(Join-Path $outDir $Phase)",
                "-MB2StreetCount=$Count", "-MB2StreetTimeout=$($Count + 120)"))
        try { $code = Wait-SmokeRole $role } finally { $role.Dispose() }
    }
    $script:clientCode = $code
}

# Expect <label> <ok> <detail>
function Expect([string]$Label, [bool]$Ok, [string]$Detail) {
    if ($Ok) { "  ${Label}: $Detail (ok)" } else { "  ${Label}: $Detail (FAIL)"; $script:status = 1 }
}

# bash runs under set -e -o pipefail: a grep pipeline in a command substitution that selects nothing ends the run (exit 1).
function Exit-Pipefail { exit 1 }

# awk's string + 0: the leading number, 0 when there is none.
function ConvertTo-AwkNumber([string]$Text) {
    $m = [regex]::Match($Text, '^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?')
    if ($m.Success) { [double]::Parse($m.Value, $invariant) } else { 0.0 }
}

# The last log line matching Pattern, $null when none does (grep | tail -1).
function Get-LastLine([string]$Log, [string]$Pattern) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $last = $null
    foreach ($line in @(Get-SmokeLogLines $Log)) { if ($regex.IsMatch($line)) { $last = $line } }
    $last
}

# sed 's/Pattern/Replacement/' on one line: unchanged when it does not match.
function Edit-Line([string]$Line, [string]$Pattern, [string]$Replacement) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    if ($regex.IsMatch($Line)) { $regex.Replace($Line, $Replacement, 1) } else { $Line }
}

# The street_hour line the server logged most recently for one player, as "period target".
function Get-HourOf([string]$Log) {
    $line = Get-LastLine $Log 'MB2_STREET role=server event=street_hour .* players=1 '
    if ($null -eq $line) { Exit-Pipefail }
    Edit-Line $line '^.* period=([a-z]*) .* target=([0-9]*) .*$' '$1 $2'
}

# Invoke-Phase <phase> <count seconds> <server args...>
function Invoke-Phase([string]$Phase, [int]$Count, [string[]]$More) {
    "Phase ${Phase}: server $($More -join ' ')"
    $script:server = Start-StreetServer (Join-Path $outDir "$Phase-server.log") $More
    $script:clientCode = 0
    Invoke-StreetClient $Phase $Count
    $clientLog = Join-Path $outDir "$Phase-client.log"
    if ($script:clientCode -eq 0 -and (Test-SmokeLog $clientLog 'MB2SSMOKE role=client event=pass')) {
        '  client: PASS'
    } else {
        "  client: FAIL (exit $($script:clientCode))"
        Get-SmokeLogMatches $clientLog 'MB2SSMOKE role=client event=(fail|expectation_failed)' 'MB2SSMOKE' '    '
        $script:status = 1
    }
    Start-Sleep -Seconds 1
    Stop-SmokeRole $script:server
    $script:server = $null
}

# Check-Phase <phase> <period> <target or a|b> <client mean min> <client mean max>
function Check-Phase([string]$Phase, [string]$WantPeriod, [string]$WantTarget, [double]$Low, [double]$High) {
    $serverLog = Join-Path $outDir "$Phase-server.log"
    $clientLog = Join-Path $outDir "$Phase-client.log"
    $got = Get-HourOf $serverLog
    # ${got% *} and ${got#* }
    $period = $got; $at = $got.LastIndexOf(' '); if ($at -ge 0) { $period = $got.Substring(0, $at) }
    $target = $got; $at = $got.IndexOf(' '); if ($at -ge 0) { $target = $got.Substring($at + 1) }
    Expect "$Phase period" ($period -ceq $WantPeriod) "$period (want $WantPeriod)"
    Expect "$Phase target" ("|$WantTarget|".Contains("|$target|")) "$target (want $WantTarget)"
    if ($null -eq (Get-LastLine $clientLog 'MB2SSMOKE role=client event=counted .* mean=[0-9.]*')) { Exit-Pipefail }
    $mean = Get-SmokeLogValue $clientLog 'MB2SSMOKE role=client event=counted .* mean=([0-9.]*)'
    if (-not $mean) { $mean = '-1' }
    $meanValue = ConvertTo-AwkNumber $mean
    Expect "$Phase seen" ($meanValue -ge $Low -and $meanValue -le $High) "mean $mean within 70 m (want $Low..$High)"
    Expect "$Phase street_off" ((Get-SmokeLogCount $serverLog 'MB2_STREET role=server event=street_off') -eq 0) 'street on'
}

# Get-Spawned <phase> <group>: how many of a group the server sent out
function Get-Spawned([string]$Phase, [string]$Group) {
    Get-SmokeLogCount (Join-Path $outDir "$Phase-server.log") "MB2_STREET role=server event=spawn .* group=$Group "
}

try {
    "Streets by the hour ($Mode) on $map, port $port; evidence in $outDir"
    $dry = @('-MB2Weather=clear_dry')
    foreach ($phase in $phaseList) {
        switch -CaseSensitive ($phase) {
            'day' { Invoke-Phase 'day' 50 ($dry + @('-MB2Clock=2026-06-18T08:00:00Z')) }
            'evening' { Invoke-Phase 'evening' 50 ($dry + @('-MB2Clock=2026-06-18T17:00:00Z')) }
            'late' { Invoke-Phase 'late' 50 ($dry + @('-MB2Clock=2026-06-18T20:30:00Z')) }
            'night' { Invoke-Phase 'night' 50 ($dry + @('-MB2Clock=2026-06-19T01:00:00Z')) }
            'friday' { Invoke-Phase 'friday' 50 ($dry + @('-MB2Clock=2026-06-21T19:30:00Z')) }
            'sunday' { Invoke-Phase 'sunday' 50 ($dry + @('-MB2Clock=2026-06-23T10:00:00Z')) }
            'rain' { Invoke-Phase 'rain' 50 @('-MB2Weather=rain_wet', '-MB2Clock=2026-06-18T08:00:00Z') }
            'home' { Invoke-Phase 'home' 210 ($dry + @('-MB2Clock=2026-06-18T18:00:00Z', '-MB2ClockSpeed=20')) }
            default { Exit-Usage "Unknown phase $phase" }
        }
    }
} finally {
    Stop-SmokeRole $server
    $server = $null
}

'Checks'
foreach ($phase in $phaseList) {
    switch -CaseSensitive ($phase) {
        'day' { Check-Phase 'day' 'day' '20' 12 22 }
        'evening' {
            Check-Phase 'evening' 'evening' '18' 10 20
            $n = Get-Spawned 'evening' 'night_people'
            Expect 'evening night owls' ($n -ge 1) "$n sent out"
        }
        'late' { Check-Phase 'late' 'late' '9' 4 11 }
        'night' {
            Check-Phase 'night' 'night' '3' 1 5
            $n = Get-Spawned 'night' 'night_people'
            Expect 'night owls' ($n -ge 1) "$n sent out"
            $n = Get-Spawned 'night' 'commuters'
            Expect 'night commuters' ($n -eq 0) "$n sent out"
        }
        'friday' { Check-Phase 'friday' 'evening' '18' 10 20 }
        'sunday' {
            Check-Phase 'sunday' 'day' '20' 12 22
            $n = (Get-Spawned 'sunday' 'commuters') + (Get-Spawned 'sunday' 'shoppers')
            Expect 'sunday commuters and shoppers' ($n -eq 0) "$n sent out"
            $n = Get-Spawned 'sunday' 'residents'
            Expect 'sunday residents' ($n -ge 5) "$n sent out"
        }
        'rain' { Check-Phase 'rain' 'day' '9|10' 5 12 }
        'home' {
            $log = Join-Path $outDir 'home-server.log'
            $eveningTarget = Get-SmokeLogCount $log 'MB2_STREET role=server event=street_hour .* period=evening .* players=1 target=18 '
            $lateTarget = Get-SmokeLogCount $log 'MB2_STREET role=server event=street_hour .* period=late .* players=1 target=9 '
            $wentHome = Get-SmokeLogCount $log 'MB2_STREET role=server event=despawn .* reason=home .* period=late'
            Expect 'home evening then late' ($eveningTarget -ge 1 -and $lateTarget -ge 1) `
                "evening target 18 logged $eveningTarget, late target 9 logged $lateTarget"
            Expect 'home went home' ($wentHome -ge 1) "$wentHome sent home after 21:00 (the rest of the surplus leaves by the usual cull and is not replaced)"
            # Nobody goes home closer to the player than anybody appears (22 m).
            $distances = @(Get-SmokeLogMatches $log 'MB2_STREET role=server event=despawn .* reason=home .* nearest_player_m=[0-9.]*' '' |
                ForEach-Object { [regex]::Match($_, 'MB2_STREET role=server event=despawn .* reason=home .* nearest_player_m=([0-9.]*)').Groups[1].Value })
            if (-not $distances.Count) { Exit-Pipefail }
            $tooClose = 0
            foreach ($d in $distances) {
                # awk '$1 < 22': numeric when the field looks like a number, else a string comparison.
                if ($d -match '^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$') {
                    if ([double]::Parse($d, $invariant) -lt 22) { $tooClose++ }
                } elseif ([string]::CompareOrdinal($d, '22') -lt 0) { $tooClose++ }
            }
            Expect 'home out of reach' ($tooClose -eq 0) "$tooClose sent home within 22 m"
            # Once the late evening has settled, the street holds about its nine.
            $line = Get-LastLine $log 'MB2_STREET role=server event=street_summary .* target=9 period=late '
            if ($null -eq $line) { Exit-Pipefail }
            $settled = Edit-Line $line '^.* people=([0-9]*) .*$' '$1'
            $settledOk = $false
            if ($settled -match '^[0-9]+$') { $settledOk = ([int64]$settled -le 11) }
            $shown = $settled; if (-not $shown) { $shown = 'none' }
            Expect 'home settled' ($settled -and $settledOk) "$shown people at the last late summary (want <= 11)"
        }
    }
}

$summary = Join-Path $outDir 'summary.txt'
$lines = @(
    'MarburgumII street hours smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "phases: $phases"
    "host: $(Get-SmokeHost)"
)
$evidenceRegex = New-Object System.Text.RegularExpressions.Regex('MB2_STREET role=server event=(street_hour|street_summary)|MB2_STREET role=server event=despawn .* reason=home|MB2SSMOKE role=client event=(counted|pass|fail)')
foreach ($phase in $phaseList) {
    $lines += ''
    $lines += "== $phase"
    $evidence = @()
    foreach ($line in @(Get-SmokeLogLines @((Join-Path $outDir "$phase-server.log"), (Join-Path $outDir "$phase-client.log")))) {
        if ($evidenceRegex.IsMatch($line)) { $evidence += [regex]::Replace($line, '^.*(MB2SSMOKE|MB2_STREET)', '$1') }
    }
    $lines += $evidence
    # bash runs under set -e -o pipefail: a summary grep that selects nothing ends the run there (exit 1).
    if (-not $evidence.Count) { Write-SmokeFile $summary $lines; exit 1 }
    $lines += "spawned: commuters=$(Get-Spawned $phase 'commuters') shoppers=$(Get-Spawned $phase 'shoppers') pensioners=$(Get-Spawned $phase 'pensioners')" +
        " students=$(Get-Spawned $phase 'students') residents=$(Get-Spawned $phase 'residents') night_people=$(Get-Spawned $phase 'night_people')"
}
Write-SmokeFile $summary $lines
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
