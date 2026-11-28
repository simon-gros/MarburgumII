# The street's districts (ADR-068) on the launch map, /Game/World/lent. Windows twin of street-district-smoke-linux.sh
# (same phases, evidence lines and expectations). For each phase a dedicated server runs with its
# world clock pinned and -MB2StreetRoute, which sets the one client's player down at six stops a minute apart, each in
# one of the original's zones (MarburgumStreetRoute.h): an old-town lane, a shopping street, the Magdalena blocks, a
# street of houses, the works and a school. The client, rendered offscreen, counts the people within 70 m every second
# (-MB2StreetCount) and captures each stop 50 s after it is moved there. The server logs the zone it reads at each
# stop, the number it keeps and the zone and group of everyone it sends out.
#
#   day       Tuesday 18 June 1996, 10:00: old town 20, shops 18, blocks 12, houses 8, works 7, school 16
#   evening   Tuesday 19:00: 18, 14, 16, 11, 5, 7
#   night     Wednesday 03:00: 3, 2, 2, 1, 1, 1, and no commuters, shoppers or pensioners anywhere
#   sunday    Sunday 23 June 1996, 12:00: the day's numbers, no shoppers or commuters on the shopping street and
#             no students at the school
#
# Evidence is written to Saved\StreetDistrictSmoke\<UTC timestamp>\<phase>-*.
# Usage: scripts\street-district-smoke-windows.ps1 [staged|editor]   (port: MB2_STREET_PORT, default 27763;
# phases: MB2_STREET_PHASES, default all of the above)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27763; if ($env:MB2_STREET_PORT) { $port = [int]$env:MB2_STREET_PORT }
$phases = 'day evening night sunday'; if ($env:MB2_STREET_PHASES) { $phases = $env:MB2_STREET_PHASES }
$phaseList = @($phases -split '\s+' | Where-Object { $_ })
$map = '/Game/World/lent'
$stops = @('old_town', 'commercial', 'residential_blocks', 'residential', 'industrial', 'school')
# Six stops a minute apart after the server's 6 s lead, and room for the client to connect and settle.
$countSeconds = 390
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\street-district-smoke-windows.ps1 [staged|editor]'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under a wall-clock limit that stops its whole job.
$wall = Get-ClientWallSeconds

$outDir = New-SmokeOutDir 'StreetDistrictSmoke'
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
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", '-MB2StreetSeed=1990',
            '-MB2StreetRoute') + $More + $commonArgs)
}

# Invoke-StreetClient <phase>; sets $script:clientCode to the last attempt's exit code.
function Invoke-StreetClient([string]$Phase) {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=mb2d-$Phase-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
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
                "-MB2StreetCount=$countSeconds", '-MB2StreetShotAfterMove=50', "-MB2StreetTimeout=$($countSeconds + 120)"))
        try { $code = Wait-SmokeRole $role } finally { $role.Dispose() }
    }
    $script:clientCode = $code
}

# Expect <label> <ok> <detail>
function Expect([string]$Label, [bool]$Ok, [string]$Detail) {
    if ($Ok) { "  ${Label}: $Detail (ok)" } else { "  ${Label}: $Detail (FAIL)"; $script:status = 1 }
}

function Invoke-Phase([string]$Phase, [string[]]$More) {
    "Phase ${Phase}: server $($More -join ' ')"
    $script:server = Start-StreetServer (Join-Path $outDir "$Phase-server.log") $More
    $script:clientCode = 0
    Invoke-StreetClient $Phase
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

# awk's match($0, /re/) then substr($0, RSTART + Skip, RLENGTH - Skip): '' when nothing matches.
function Get-AwkMatch([string]$Line, [string]$Pattern, [int]$Skip) {
    $m = [regex]::Match($Line, $Pattern)
    if ($m.Success) { $m.Value.Substring($Skip) } else { '' }
}

# awk's string + 0: the leading number, 0 when there is none.
function ConvertTo-AwkNumber([string]$Text) {
    $m = [regex]::Match($Text, '^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?')
    if ($m.Success) { [double]::Parse($m.Value, $invariant) } else { 0.0 }
}

# printf "%.1f" as glibc does it: the exact binary value rounded half to even (.NET Framework rounds differently).
function Format-Fixed1([double]$Value) {
    $bits = [BitConverter]::DoubleToInt64Bits([Math]::Abs($Value))
    $exponent = [int](($bits -shr 52) -band 0x7FF)
    $mantissa = [System.Numerics.BigInteger]($bits -band 0xFFFFFFFFFFFFF)
    if ($exponent -eq 0) { $exponent = 1 } else { $mantissa += [System.Numerics.BigInteger]::Pow(2, 52) }
    $shift = $exponent - 1075
    $tenths = $mantissa * 10
    if ($shift -ge 0) {
        $q = $tenths * [System.Numerics.BigInteger]::Pow(2, $shift)
    } else {
        $den = [System.Numerics.BigInteger]::Pow(2, -$shift)
        $rem = [System.Numerics.BigInteger]::Zero
        $q = [System.Numerics.BigInteger]::DivRem($tenths, $den, [ref]$rem)
        $twice = $rem * 2
        if ($twice -gt $den -or ($twice -eq $den -and -not $q.IsEven)) { $q += 1 }
    }
    $text = '{0}.{1}' -f [System.Numerics.BigInteger]::Divide($q, 10), [System.Numerics.BigInteger]::Remainder($q, 10)
    if ($Value -lt 0) { "-$text" } else { $text }
}

# bash read -r a b c ...: whitespace-separated, empty fields collapse, the last name takes the rest of the line.
function Split-Read([string]$Line, [int]$Count) {
    $rest = $Line.Trim()
    $fields = @()
    for ($i = 0; $i -lt $Count; $i++) {
        if ($i -eq $Count - 1) { $fields += $rest; break }
        $m = [regex]::Match($rest, '^(\S*)\s*(.*)$')
        $fields += $m.Groups[1].Value
        $rest = $m.Groups[2].Value
    }
    , $fields
}

function New-Map { New-Object System.Collections.Hashtable }

function Get-MapInt($Map, [string]$Key) { if ($Map.ContainsKey($Key)) { [int]$Map[$Key] } else { 0 } }

# The server's view of each stop, one line each:
#   <stop> <want zone> <zone read> <last target> <last street_hour zone> <spawns> <spawns in the stop's zone> <group=n,...>
# where the groups count only the spawns in the stop's zone.
function Get-ServerStops([string]$Log) {
    $order = New-Object System.Collections.Generic.List[string]
    $want = New-Map; $readZone = New-Map; $target = New-Map; $hourZone = New-Map
    $spawns = New-Map; $inzone = New-Map; $groups = New-Map
    $seen = New-Object System.Collections.Generic.List[string]
    $stop = ''; $currentTarget = ''; $currentZone = ''
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        if ($line -cmatch 'MB2_STREET role=server event=route stop=') {
            $stop = Get-AwkMatch $line 'stop=[a-z_]+' 5
            $want[$stop] = Get-AwkMatch $line 'want_zone=[a-z_]+' 10
            $readZone[$stop] = Get-AwkMatch $line ' zone=[a-z_-]+' 6
            $order.Add($stop)
            # The hour in force when the player arrives holds until the street logs another.
            if ($currentTarget -ne '') { $target[$stop] = $currentTarget; $hourZone[$stop] = $currentZone }
            continue
        }
        if ($line -cmatch 'MB2_STREET role=server event=street_hour .* players=[1-9]') {
            $currentTarget = Get-AwkMatch $line ' target=[0-9]+' 8
            $currentZone = Get-AwkMatch $line ' zone=[a-z_]+' 6
            if ($stop -ne '') { $target[$stop] = $currentTarget; $hourZone[$stop] = $currentZone }
        }
        if ($stop -ne '' -and $line -cmatch 'MB2_STREET role=server event=spawn .* why=(in_view|out_of_view) ') {
            $z = Get-AwkMatch $line ' zone=[a-z_]+' 6
            $g = Get-AwkMatch $line ' group=[a-z_]+' 7
            $spawns[$stop] = (Get-MapInt $spawns $stop) + 1
            $wanted = ''; if ($want.ContainsKey($stop)) { $wanted = $want[$stop] }
            if ($z -ceq $wanted) {
                $inzone[$stop] = (Get-MapInt $inzone $stop) + 1
                $groups["$stop`t$g"] = (Get-MapInt $groups "$stop`t$g") + 1
                if (-not $seen.Contains($g)) { $seen.Add($g) }
            }
        }
    }
    foreach ($s in $order) {
        $list = ''
        foreach ($g in $seen) {
            if ($groups.ContainsKey("$s`t$g")) {
                if ($list -ne '') { $list += ',' }
                $list += "$g=$($groups["$s`t$g"])"
            }
        }
        if ($list -eq '') { $list = '-' }
        $t = '-'; if ($target.ContainsKey($s)) { $t = $target[$s] }
        $h = '-'; if ($hourZone.ContainsKey($s)) { $h = $hourZone[$s] }
        '{0} {1} {2} {3} {4} {5} {6} {7}' -f $s, $want[$s], $readZone[$s], $t, $h, (Get-MapInt $spawns $s), (Get-MapInt $inzone $s), $list
    }
}

# The client's mean count over the second half of each stop (30 s and more after it saw the move), "move mean" per line.
function Get-ClientStops([string]$Log) {
    $move = 0; $first = -1
    $total = New-Map; $samples = New-Map
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        if ($line -cmatch 'MB2SSMOKE role=client event=moved ') {
            $move = ConvertTo-AwkNumber (Get-AwkMatch $line 'move=[0-9]+' 5); $first = -1
            continue
        }
        if ($move -gt 0 -and $line -cmatch 'MB2SSMOKE role=client event=count ') {
            $t = ConvertTo-AwkNumber (Get-AwkMatch $line 't_s=[0-9]+' 4)
            $p = ConvertTo-AwkNumber (Get-AwkMatch $line ' people=[0-9]+' 8)
            if ($first -lt 0) { $first = $t }
            if ($t - $first -ge 30 -and $t - $first -lt 58) {
                $total["$move"] = (Get-MapInt $total "$move") + $p
                $samples["$move"] = (Get-MapInt $samples "$move") + 1
            }
        }
    }
    for ($m = 1; $m -le 6; $m++) {
        $n = Get-MapInt $samples "$m"
        $mean = -1.0; if ($n) { $mean = [double](Get-MapInt $total "$m") / $n }
        "$m $(Format-Fixed1 $mean)"
    }
}

function Get-GroupCount([string]$List, [string]$Group) {
    $n = 0.0
    foreach ($item in $List.Split(',')) {
        $parts = $item.Split('=')
        if ($parts[0] -ceq $Group) { if ($parts.Count -gt 1) { $n += ConvertTo-AwkNumber $parts[1] } }
    }
    [int]$n
}

# Check-Phase <phase> <targets for the six stops, in order>
function Check-Phase([string]$Phase, [int[]]$Want) {
    $serverLog = Join-Path $outDir "$Phase-server.log"
    $clientLog = Join-Path $outDir "$Phase-client.log"
    $stopsFile = Join-Path $outDir "$Phase-stops.txt"
    $seenFile = Join-Path $outDir "$Phase-seen.txt"
    $stopLines = @(Get-ServerStops $serverLog)
    Write-SmokeFile $stopsFile $stopLines
    $seenLines = @(Get-ClientStops $clientLog)
    Write-SmokeFile $seenFile $seenLines
    Expect "$Phase stops" ($stopLines.Count -eq $stops.Count) "$($stopLines.Count) of $($stops.Count) visited"
    $i = 0
    foreach ($row in $stopLines) {
        $f = Split-Read $row 8
        $stop = $f[0]; $wantZone = $f[1]; $read = $f[2]; $target = $f[3]; $hourZone = $f[4]
        $spawns = $f[5]; $inzone = $f[6]; $groups = $f[7]
        $wantTarget = ''; if ($i -lt $Want.Count) { $wantTarget = "$($Want[$i])" }
        Expect "$Phase $stop zone" ($read -ceq $wantZone -and $hourZone -ceq $wantZone) "read $read, hour $hourZone (want $wantZone)"
        Expect "$Phase $stop target" ($target -ceq $wantTarget) "$target (want $wantTarget)"
        # Who appears round the stop is that district's people: at least 70% of them on its own zone.
        $a = ConvertTo-AwkNumber $inzone; $b = ConvertTo-AwkNumber $spawns
        Expect "$Phase $stop district" ($b -eq 0 -or $a -ge 0.7 * $b) "$inzone of $spawns sent out on $wantZone ($groups)"
        $mean = ''
        foreach ($s in $seenLines) { $sf = Split-Read $s 3; if ((ConvertTo-AwkNumber $sf[0]) -eq ($i + 1)) { $mean = $sf[1] } }
        if (-not $mean) { $mean = '-1' }
        $t = ConvertTo-AwkNumber $wantTarget
        $low = [int][Math]::Truncate($t * 0.5) - 1; if ($low -lt 0) { $low = 0 }
        $high = [int]$t + 3
        $meanValue = ConvertTo-AwkNumber $mean
        Expect "$Phase $stop seen" ($meanValue -ge $low -and $meanValue -le $high) "mean $mean within 70 m (want $low..$high)"
        switch ("${Phase}:$stop") {
            { $_ -ceq 'day:industrial' -or $_ -ceq 'evening:industrial' } {
                $workers = Get-GroupCount $groups 'workers'
                Expect "$Phase works" ($workers -ge 1 -and $workers -ge (Get-GroupCount $groups 'residents')) "$workers workers"
            }
            { $_ -ceq 'day:school' } {
                $students = Get-GroupCount $groups 'students'
                Expect "$Phase school" ($students -ge 1) "$students students"
            }
            { $_ -ceq 'sunday:commercial' } {
                $open = (Get-GroupCount $groups 'shoppers') + (Get-GroupCount $groups 'commuters')
                Expect 'sunday shops shut' ($open -eq 0) "$open shoppers and commuters"
            }
            { $_ -ceq 'sunday:school' } {
                $students = Get-GroupCount $groups 'students'
                Expect 'sunday no school' ($students -eq 0) "$students students"
            }
            { $_ -ceq 'evening:residential' } {
                $outdoor = Get-GroupCount $groups 'outdoor'
                Expect 'evening out walking' ($outdoor -ge 1 -or (ConvertTo-AwkNumber $inzone) -lt 5) "$outdoor out walking of $inzone"
            }
        }
        $i++
    }
    if ($Phase -ceq 'night') {
        $daytime = Get-SmokeLogCount $serverLog 'MB2_STREET role=server event=spawn .* group=(commuters|shoppers|pensioners) .* why=(in_view|out_of_view)'
        Expect 'night nobody of the day' ($daytime -eq 0) "$daytime commuters, shoppers or pensioners sent out"
    }
}

try {
    "Street districts ($Mode) on $map, port $port; evidence in $outDir"
    $dry = @('-MB2Weather=clear_dry')
    foreach ($phase in $phaseList) {
        switch -CaseSensitive ($phase) {
            'day' { Invoke-Phase 'day' ($dry + @('-MB2Clock=2026-06-18T08:00:00Z')) }
            'evening' { Invoke-Phase 'evening' ($dry + @('-MB2Clock=2026-06-18T17:00:00Z')) }
            'night' { Invoke-Phase 'night' ($dry + @('-MB2Clock=2026-06-19T01:00:00Z')) }
            'sunday' { Invoke-Phase 'sunday' ($dry + @('-MB2Clock=2026-06-23T10:00:00Z')) }
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
        'day' { Check-Phase 'day' @(20, 18, 12, 8, 7, 16) }
        'sunday' { Check-Phase 'sunday' @(20, 18, 12, 8, 7, 16) }
        'evening' { Check-Phase 'evening' @(18, 14, 16, 11, 5, 7) }
        'night' { Check-Phase 'night' @(3, 2, 2, 1, 1, 1) }
    }
}

$summary = Join-Path $outDir 'summary.txt'
$lines = @(
    'MarburgumII street districts smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "map: $map"
    "phases: $phases"
    "host: $(Get-SmokeHost)"
)
foreach ($phase in $phaseList) {
    $lines += ''
    $lines += "== $phase (stop want_zone read_zone target hour_zone spawns spawns_in_zone groups_in_zone)"
    $lines += @(Get-SmokeLogLines (Join-Path $outDir "$phase-stops.txt"))
    $lines += "-- seen within 70 m over each stop's second half (move mean)"
    $lines += @(Get-SmokeLogLines (Join-Path $outDir "$phase-seen.txt"))
    $clientLines = @(Get-SmokeLogMatches (Join-Path $outDir "$phase-client.log") 'MB2SSMOKE role=client event=(counted|pass|fail)' 'MB2SSMOKE')
    $lines += $clientLines
    # bash runs under set -e -o pipefail: a summary grep that selects nothing ends the run there (exit 1).
    if (-not $clientLines.Count) { Write-SmokeFile $summary $lines; exit 1 }
}
Write-SmokeFile $summary $lines
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
