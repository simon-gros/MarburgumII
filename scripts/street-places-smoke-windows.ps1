# The places that draw street people in (ADR-069) on the launch map, /Game/World/lent. Windows twin of
# street-places-smoke-linux.sh (same phases, evidence lines and expectations). For each phase a dedicated server
# runs with its world clock pinned and -MB2StreetRoute -MB2StreetRouteName=places, which sets the one client's player
# down two minutes at each of five stops by a cluster of places (MarburgumStreetRoute.h PlaceStops): the old town's
# cafés and restaurants by Glavni trg, the sights by the Drava, the shops in Magdalena, three bus stops among the houses
# and a shopping street with one of nearly every kind. The client, rendered offscreen, counts the people within 70 m
# every second, and how many of them are at or on their way to a place (-MB2StreetCount), and captures each stop 90 s
# after it is moved there. The server logs every visit (event=visit kind= action=), every arrival at a stop or a sight,
# everyone who went in at a door or came out of one, everyone found already waiting at a stop, and how open each kind is.
#
#   day      Tuesday 18 June 1996, 10:00: everything open but the bars' day share; people go to stops, cafés,
#            restaurants, shops and sights, wait and look, go in and come out
#   late     Tuesday 22:30: the shops are shut and nobody goes to one; the bars are at their busiest
#   sunday   Sunday 23 June 1996, 11:00: the shops are shut (1990s shop hours) and nobody goes to one; the cafés are open
#
# Evidence is written to Saved\StreetPlacesSmoke\<UTC timestamp>\<phase>-*.
# Usage: scripts\street-places-smoke-windows.ps1 [staged|editor]   (port: MB2_STREET_PORT, default 27764;
# phases: MB2_STREET_PHASES, default all of the above)
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27764; if ($env:MB2_STREET_PORT) { $port = [int]$env:MB2_STREET_PORT }
$phases = 'day late sunday'; if ($env:MB2_STREET_PHASES) { $phases = $env:MB2_STREET_PHASES }
$phaseList = @($phases -split '\s+' | Where-Object { $_ })
$map = '/Game/World/lent'
$stops = @('venues', 'sights', 'shops', 'bus_stops', 'mixed')
$kinds = @('bus_stop', 'cafe', 'restaurant', 'bar', 'fast_food', 'shop', 'sight')
# Five stops two minutes apart after the server's 6 s lead, and room for the client to connect and settle.
$countSeconds = 640
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\street-places-smoke-windows.ps1 [staged|editor]'

$outDir = New-SmokeOutDir 'StreetPlacesSmoke'
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')
$server = $null
$status = 0
$invariant = [Globalization.CultureInfo]::InvariantCulture
# Windows PowerShell 5.1 reads a BOM-less script as ANSI, so the evidence's e-acute is spelled as a code point.
$cafe = "caf$([char]0x00E9)"

# Start-StreetServer <log> <server args...>
function Start-StreetServer([string]$Log, [string[]]$More) {
    Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_STREET_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", '-MB2StreetSeed=1990',
            '-MB2StreetRoute', '-MB2StreetRouteName=places') + $More + $commonArgs)
}

# Invoke-StreetClient <phase>; sets $script:clientCode to the last attempt's exit code. Like the bash twin, this
# runner puts no wall-clock limit on the client (-MB2StreetTimeout ends it).
function Invoke-StreetClient([string]$Phase) {
    $url = "127.0.0.1:${port}?MB2Start=lent_hatchback?MB2Character=mb2p-$Phase-t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
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
        $role = Start-SmokeRole -Target $targets.Client -Log $log `
            -Arguments (@($url) + $commonArgs + $renderArgs + @('-MB2StreetSmoke', "-MB2StreetShot=$(Join-Path $outDir $Phase)",
                "-MB2StreetCount=$countSeconds", '-MB2StreetShotAfterMove=90', "-MB2StreetTimeout=$($countSeconds + 120)"))
        try { $code = Wait-SmokeRole $role } finally { $role.Dispose() }
    }
    $script:clientCode = $code
}

# Expect <label> <ok> <detail>
function Expect([string]$Label, [bool]$Ok, [string]$Detail) {
    if ($Ok) { "  ${Label}: $Detail (ok)" } else { "  ${Label}: $Detail (FAIL)"; $script:status = 1 }
}

# bash runs under set -e -o pipefail: a grep pipeline that selects nothing, or a read at end of input, ends the run (exit 1).
function Exit-Pipefail { exit 1 }

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

# Whether a string is a number to awk (a strnum from -v or a field), which then compares numerically.
function Test-AwkNumeric([string]$Text) { $Text -match '^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?\s*$' }

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

function Add-MapInt($Map, [string]$Key) { $Map[$Key] = (Get-MapInt $Map $Key) + 1 }

# What went on at the places, per stop and kind, one line each:
#   <stop> <kind> <visits> <arrived> <went_in> <came_out> <found_waiting> <given_up>
# Each event is the stop's from the moment the player is set down there.
function Get-ServerPlaces([string]$Log) {
    $order = New-Object System.Collections.Generic.List[string]
    $visits = New-Map; $arrived = New-Map; $went = New-Map; $out = New-Map; $given = New-Map; $found = New-Map
    $stop = ''; $waitingNext = $false
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        if ($line -cmatch 'MB2_STREET role=server event=route stop=') {
            $stop = Get-AwkMatch $line 'stop=[a-z_]+' 5; $order.Add($stop); continue
        }
        if ($stop -eq '') { continue }
        $kind = Get-AwkMatch $line ' kind=[a-z_]+' 6
        if ($line -cmatch ' event=visit ') { Add-MapInt $visits "$stop`t$kind" }
        if ($line -cmatch ' event=arrived ') { Add-MapInt $arrived "$stop`t$kind" }
        if ($line -cmatch ' event=went_in ') { Add-MapInt $went "$stop`t$kind" }
        if ($line -cmatch ' event=came_out ') { Add-MapInt $out "$stop`t$kind" }
        if ($line -cmatch ' event=visit_given_up ') { Add-MapInt $given "$stop`t$kind" }
        # Spawned already waiting: the visit line that follows names the kind.
        if ($line -cmatch ' event=spawn .* why=waiting ') { $waitingNext = $true; continue }
        if ($waitingNext -and $line -cmatch ' event=visit ') { Add-MapInt $found "$stop`t$kind"; $waitingNext = $false }
    }
    foreach ($s in $order) {
        foreach ($c in $kinds) {
            $k = "$s`t$c"
            '{0} {1} {2} {3} {4} {5} {6} {7}' -f $s, $c, (Get-MapInt $visits $k), (Get-MapInt $arrived $k), (Get-MapInt $went $k),
                (Get-MapInt $out $k), (Get-MapInt $found $k), (Get-MapInt $given $k)
        }
    }
}

# The kinds' openness the street reported while the player was about ("kind open" per line), and whether it held.
function Write-ServerOpen([string]$Log, [string]$OpenFile) {
    $values = @()
    $regex = New-Object System.Text.RegularExpressions.Regex('MB2_STREET role=server event=street_hour .* players=[1-9].* open=([a-z_=0-9.,]*)')
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        $m = $regex.Match($line)
        if ($m.Success) { $values += $m.Groups[1].Value }
    }
    # sort -u (byte order)
    $unique = New-Object 'System.Collections.Generic.SortedSet[string]' ([StringComparer]::Ordinal)
    foreach ($v in $values) { [void]$unique.Add($v) }
    $all = @($unique)
    Write-SmokeFile "$OpenFile.all" $all
    if (-not $values.Count) { Exit-Pipefail }
    $rows = @()
    if ($all.Count) { $rows = @($all[0].Split(',') | ForEach-Object { $_.Replace('=', ' ') }) }
    Write-SmokeFile $OpenFile $rows
}

# The client's most people at a place, and its mean count over each stop's second half ("move mean most_visiting").
function Get-ClientStops([string]$Log) {
    $move = 0; $first = -1
    $total = New-Map; $samples = New-Map; $most = New-Map
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        if ($line -cmatch 'MB2SSMOKE role=client event=moved ') {
            $move = ConvertTo-AwkNumber (Get-AwkMatch $line 'move=[0-9]+' 5); $first = -1
            continue
        }
        if ($move -gt 0 -and $line -cmatch 'MB2SSMOKE role=client event=count ') {
            $t = ConvertTo-AwkNumber (Get-AwkMatch $line 't_s=[0-9]+' 4)
            $p = ConvertTo-AwkNumber (Get-AwkMatch $line ' people=[0-9]+' 8)
            $v = ConvertTo-AwkNumber (Get-AwkMatch $line ' visiting=[0-9]+' 10)
            if ($first -lt 0) { $first = $t }
            if ($t - $first -ge 60 -and $t - $first -lt 118) {
                $total["$move"] = (Get-MapInt $total "$move") + $p
                $samples["$move"] = (Get-MapInt $samples "$move") + 1
            }
            if ($v -gt (Get-MapInt $most "$move")) { $most["$move"] = $v }
        }
    }
    for ($m = 1; $m -le 5; $m++) {
        $n = Get-MapInt $samples "$m"
        $mean = -1.0; if ($n) { $mean = [double](Get-MapInt $total "$m") / $n }
        "$m $(Format-Fixed1 $mean) $(Get-MapInt $most "$m")"
    }
}

# The target the street kept at each stop ("stop target"), as the district smoke reads it.
function Get-ServerTargets([string]$Log) {
    $order = New-Object System.Collections.Generic.List[string]
    $target = New-Map
    $stop = ''; $t = ''
    foreach ($line in @(Get-SmokeLogLines $Log)) {
        if ($line -cmatch 'MB2_STREET role=server event=route stop=') {
            $stop = Get-AwkMatch $line 'stop=[a-z_]+' 5; $order.Add($stop)
            if ($t -ne '') { $target[$stop] = $t }
            continue
        }
        if ($line -cmatch 'MB2_STREET role=server event=street_hour .* players=[1-9]') {
            $t = Get-AwkMatch $line ' target=[0-9]+' 8
            if ($stop -ne '') { $target[$stop] = $t }
        }
    }
    foreach ($s in $order) {
        $v = '-'; if ($target.ContainsKey($s)) { $v = $target[$s] }
        "$s $v"
    }
}

# Get-TotalOf <places rows> <column 3..8> [kind]
function Get-TotalOf([string[]]$Rows, [int]$Column, [string]$Kind = '') {
    $n = 0.0
    foreach ($row in $Rows) {
        $f = @($row.Trim() -split '\s+')
        $second = ''; if ($f.Count -gt 1) { $second = $f[1] }
        if ($Kind -eq '' -or $second -ceq $Kind) { if ($f.Count -ge $Column) { $n += ConvertTo-AwkNumber $f[$Column - 1] } }
    }
    [int]$n
}

function Check-Phase([string]$Phase) {
    $serverLog = Join-Path $outDir "$Phase-server.log"
    $clientLog = Join-Path $outDir "$Phase-client.log"
    $placesFile = Join-Path $outDir "$Phase-places.txt"
    $openFile = Join-Path $outDir "$Phase-open.txt"
    $seenFile = Join-Path $outDir "$Phase-seen.txt"
    $targetsFile = Join-Path $outDir "$Phase-targets.txt"
    $places = @(Get-ServerPlaces $serverLog)
    Write-SmokeFile $placesFile $places
    Write-ServerOpen $serverLog $openFile
    $seen = @(Get-ClientStops $clientLog)
    Write-SmokeFile $seenFile $seen
    $stopTargets = @(Get-ServerTargets $serverLog)
    Write-SmokeFile $targetsFile $stopTargets
    $openAll = @(Get-SmokeLogLines "$openFile.all")
    Expect "$Phase stops" ($stopTargets.Count -eq $stops.Count) "$($stopTargets.Count) of $($stops.Count) visited"
    Expect "$Phase hours held" ($openAll.Count -eq 1) (-join @($openAll | ForEach-Object { "$_ " }))

    $visits = Get-TotalOf $places 3; $arrived = Get-TotalOf $places 4; $went = Get-TotalOf $places 5
    $came = Get-TotalOf $places 6; $found = Get-TotalOf $places 7; $given = Get-TotalOf $places 8
    "  ${Phase}: $visits visits, $arrived arrived at a stop or sight, $went went in, $came came out, $found found waiting, $given gave up"
    # A spot nobody can reach would show as visits given up.
    Expect "$Phase spots reached" ($given -le 0.2 * $visits + 1) "$given of $visits visits given up"
    # Nobody goes to, comes out of or waits at a place that is shut.
    foreach ($row in @(Get-SmokeLogLines $openFile)) {
        $f = Split-Read $row 2
        $kind = $f[0]; $share = $f[1]
        if ((Test-AwkNumeric $share) -and (ConvertTo-AwkNumber $share) -eq 0) {
            $any = (Get-TotalOf $places 3 $kind) + (Get-TotalOf $places 6 $kind)
            Expect "$Phase $kind shut" ($any -eq 0) "$any visits or people out of one"
        }
    }

    # Every stop keeps the street's number: people going in are replaced.
    $i = 0
    foreach ($row in $stopTargets) {
        $f = Split-Read $row 2
        $stop = $f[0]; $target = $f[1]
        $seenRow = $null
        foreach ($s in $seen) { if ((ConvertTo-AwkNumber (Split-Read $s 3)[0]) -eq ($i + 1)) { if ($null -eq $seenRow) { $seenRow = $s } } }
        if ($null -eq $seenRow) { Exit-Pipefail }
        $sf = Split-Read $seenRow 3
        $mean = $sf[1]; $most = $sf[2]
        if (-not $mean) { $mean = '-1' }
        $t = ConvertTo-AwkNumber $target
        $low = [int][Math]::Truncate($t * 0.5) - 1; if ($low -lt 0) { $low = 0 }
        # $((target + 3)) in bash arithmetic: "" is 0 and "-" (no target) reads as - + 3; anything else is an
        # arithmetic error that ends the run.
        if ($target -eq '') { $high = 3 }
        elseif ($target -eq '-') { $high = -3 }
        elseif ($target -match '^[0-9]+$') { $high = [int64]$target + 3 }
        else { Exit-Pipefail }
        $meanValue = ConvertTo-AwkNumber $mean
        $mostShown = $most; if (-not $mostShown) { $mostShown = '0' }
        Expect "$Phase $stop seen" ($meanValue -ge $low -and $meanValue -le $high) `
            "mean $mean within 70 m, target $target (want $low..$high); most at a place at once $mostShown"
        $i++
    }

    switch -CaseSensitive ($Phase) {
        'day' {
            foreach ($kind in 'bus_stop', 'shop', 'sight') {
                $n = Get-TotalOf $places 3 $kind
                Expect "day goes to a $kind" ($n -ge 1) "$n visits"
            }
            $n = (Get-TotalOf $places 3 'cafe') + (Get-TotalOf $places 3 'restaurant')
            Expect "day goes to a $cafe or restaurant" ($n -ge 1) "$n visits"
            $n = Get-TotalOf $places 4 'bus_stop'
            Expect 'day waits at a stop' ($n -ge 1) "$n arrived"
            $n = Get-TotalOf $places 4 'sight'
            Expect 'day looks at a sight' ($n -ge 1) "$n arrived"
            Expect 'day goes in' ($went -ge 3) "$went went in"
            Expect 'day comes out' ($came -ge 3) "$came came out"
            Expect 'day found waiting' ($found -ge 1) "$found already waiting at a stop"
            $atPlace = @($seen | Where-Object { (ConvertTo-AwkNumber (Split-Read $_ 3)[2]) -gt 0 }).Count
            Expect 'day seen at a place' ($atPlace -gt 0) "client saw somebody at a place at $atPlace stops"
        }
        'late' {
            $n = (Get-TotalOf $places 3 'bar') + (Get-TotalOf $places 3 'fast_food')
            Expect 'late goes to a bar or fast food' ($n -ge 1) "$n visits"
        }
        'sunday' {
            $n = Get-TotalOf $places 3 'cafe'
            Expect "sunday goes to a $cafe" ($n -ge 1) "$n visits"
        }
    }
    $swimmingValues = @(Get-SmokeLogMatches $serverLog 'event=street_summary .* swimming=[0-9]*' '' |
        ForEach-Object { [regex]::Match($_, 'event=street_summary .* swimming=([0-9]*)').Groups[1].Value })
    if (-not $swimmingValues.Count) { Exit-Pipefail }
    # sort -n | tail -1
    $swimming = @($swimmingValues | Sort-Object { ConvertTo-AwkNumber $_ }, { $_ })[-1]
    $swimmingShown = $swimming; if (-not $swimmingShown) { $swimmingShown = '0' }
    Expect "$Phase nobody in the river" ((ConvertTo-AwkNumber $swimmingShown) -eq 0) "most swimming at once $swimmingShown"
}

try {
    "Street places ($Mode) on $map, port $port; evidence in $outDir"
    $dry = @('-MB2Weather=clear_dry')
    foreach ($phase in $phaseList) {
        switch -CaseSensitive ($phase) {
            'day' { Invoke-Phase 'day' ($dry + @('-MB2Clock=2026-06-18T08:00:00Z')) }
            'late' { Invoke-Phase 'late' ($dry + @('-MB2Clock=2026-06-18T20:30:00Z')) }
            'sunday' { Invoke-Phase 'sunday' ($dry + @('-MB2Clock=2026-06-23T09:00:00Z')) }
            default { Exit-Usage "Unknown phase $phase" }
        }
    }
} finally {
    Stop-SmokeRole $server
    $server = $null
}

'Checks'
foreach ($phase in $phaseList) {
    Check-Phase $phase
}

$summary = Join-Path $outDir 'summary.txt'
$lines = @(
    'MarburgumII street places smoke'
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
    $lines += "== $phase (stop kind visits arrived went_in came_out found_waiting given_up), rows with anything"
    $lines += @(Get-SmokeLogLines (Join-Path $outDir "$phase-places.txt") | Where-Object {
        $f = @($_.Trim() -split '\s+'); $sum = 0.0
        for ($c = 2; $c -le 7; $c++) { if ($c -lt $f.Count) { $sum += ConvertTo-AwkNumber $f[$c] } }
        $sum -gt 0 })
    $lines += '-- open'
    $lines += (-join @(Get-SmokeLogLines (Join-Path $outDir "$phase-open.txt") | ForEach-Object { "$_ " }))
    $lines += '-- targets (stop target)'
    $lines += @(Get-SmokeLogLines (Join-Path $outDir "$phase-targets.txt"))
    $lines += "-- seen within 70 m over each stop's second half (move mean most_at_a_place)"
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
