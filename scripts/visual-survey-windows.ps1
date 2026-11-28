# Windows twin of visual-survey-linux.sh (same server/client flags, MB2_SURVEY_* variables, evidence and exit code).
# Visual survey of /Game/World/lent (Docs/VISUAL_QUALITY_BACKLOG.md): a dedicated server and one offscreen 1920x1080
# client that photographs the fixed camera set of `mb2 survey-views` (aerials of the 25 windows, eye-height
# street views, the map edges, the angler, the Stari most deck) and reports assets the client could not load.
#
#   scripts\visual-survey-windows.ps1 [editor|staged] [name]
#
# MB2_SURVEY_CLOCK (default 2026-06-18T10:00:00Z, Maribor noon), MB2_SURVEY_WEATHER (default clear_dry),
# MB2_SURVEY_PORT (default 27891; never 7777 or 17777, which are for playtests), MB2_SURVEY_VIEWS (an MB2ViewShots string
# name@eye@target@fov;... in Unreal centimetres, instead of the fixed set, for close-ups of one defect).
# MB2_SURVEY_PLAYER_VIEW=1 first adds `player_hud`, the player's own camera with the HUD (character and interface);
# MB2_SURVEY_START picks the player's start (?MB2Start=; a door_<id> start also gives the server -MB2DoorStarts);
# MB2_SURVEY_SETTLE seconds per view (default 2.5; longer gives more frame samples). MB2_SURVEY_CLIENT_ARGS adds client
# arguments, split on spaces (render A/B: "-sm5", "-ExecCmds=r.DynamicGlobalIlluminationMethod 0" needs no spaces, so use
# -ini:Engine:[SystemSettings]:r.DynamicGlobalIlluminationMethod=0). Evidence goes to
# Saved\VisualSurvey\<name or UTC timestamp>\ (or MB2_SURVEY_OUT): one PNG per view, stats.tsv (per-view frame, thread
# and GPU percentiles, VRAM), render_config.json (the renderer's settings and the level's render inventory),
# server.log, client.log and summary.txt, which also names other GPU users at the start (they skew the numbers).
param([string]$Mode = 'editor', [string]$Name)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Mode) { $Mode = 'editor' }
if (-not $Name) { $Name = Get-UtcStamp }
$port = 27891; if ($env:MB2_SURVEY_PORT) { $port = [int]$env:MB2_SURVEY_PORT }
$clock = '2026-06-18T10:00:00Z'; if ($env:MB2_SURVEY_CLOCK) { $clock = $env:MB2_SURVEY_CLOCK }
$weather = 'clear_dry'; if ($env:MB2_SURVEY_WEATHER) { $weather = $env:MB2_SURVEY_WEATHER }
$map = '/Game/World/lent'
$outDir = Join-Path $repoRoot "Saved\VisualSurvey\$Name"; if ($env:MB2_SURVEY_OUT) { $outDir = $env:MB2_SURVEY_OUT }
$settle = '2.5'; if ($env:MB2_SURVEY_SETTLE) { $settle = $env:MB2_SURVEY_SETTLE }
$start = "$env:MB2_SURVEY_START"
$clientExtra = @()
if ($env:MB2_SURVEY_CLIENT_ARGS) { $clientExtra = @($env:MB2_SURVEY_CLIENT_ARGS -split '\s+' | Where-Object { $_ }) }
# One word in summary.txt, whose first line is space-separated key=value pairs.
$clientArgsWord = $clientExtra -join ','

$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\visual-survey-windows.ps1 [editor|staged] [name]'

# Other Unreal game, server and editor processes (pgrep -f on the Linux binaries' paths).
function Get-OtherUnrealCount {
    $pattern = '\\Binaries\\Win64\\(UnrealEditor(-Cmd)?|MarburgumII(Client|Server)[^\\]*)\.exe'
    try {
        @(Get-CimInstance Win32_Process -Property CommandLine -ErrorAction Stop |
            Where-Object { $_.CommandLine -and $_.CommandLine -match $pattern }).Count
    } catch { 0 }
}

# nvidia-smi --query-gpu=... | head -n 1, or empty without an NVIDIA driver.
function Get-NvidiaSmiLine([string]$Format) {
    if (-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)) { return '' }
    $ErrorActionPreference = 'Continue'
    $lines = @(& nvidia-smi '--query-gpu=utilization.gpu,memory.used' "--format=$Format" 2>$null)
    if ($lines.Count) { "$($lines[0])" } else { '' }
}

if (Test-UdpPortInUse $port) {
    Exit-Failure "UDP port $port is already in use; stop the other server or set MB2_SURVEY_PORT."
}
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if ($env:MB2_SURVEY_VIEWS) {
    $views = $env:MB2_SURVEY_VIEWS
} else {
    $views = (@(Invoke-Mb2 survey-views (Join-Path $repoRoot 'Content\Marburgum\Street\lent_walk_graph.json')) -join "`n").TrimEnd("`n")
    if ($LASTEXITCODE) { exit 1 }
}
# The HUD is removed at the first fixed view, so the player's view goes first.
if ($env:MB2_SURVEY_PLAYER_VIEW -eq '1') { $views = "player_hud@player@player@0;$views" }
$commonArgs = @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput')
$serverExtra = @()
$clientUrl = "127.0.0.1:${port}?MB2Character=mb2v-survey"
if ($start) {
    $clientUrl += "?MB2Start=$start"
    if ($start.StartsWith('door_')) { $serverExtra += '-MB2DoorStarts' }
}
# Anything else on the GPU (another session's smoke, a playtest) skews the frame numbers; name it in the summary.
$gpuOthers = Get-NvidiaSmiLine 'csv,noheader'
$ueOthers = Get-OtherUnrealCount

$serverLog = Join-Path $outDir 'server.log'
$clientLog = Join-Path $outDir 'client.log'
$server = $null
$code = 0
try {
    "Evidence in $outDir"
    $epochStart = Get-UnixSeconds
    $server = Start-SmokeServer -Target $targets.Server -Log $serverLog -Port $port -PortVariable 'MB2_SURVEY_PORT' -ReadySeconds 120 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')", "-MB2Weather=$weather",
            "-MB2Clock=$clock", '-MB2ClockSpeed=0.001') + $serverExtra + $commonArgs)

    $expected = @($views -split ';' | Where-Object { $_ -match '.' }).Count
    # awk -v s="$settle": a non-number counts as 0.
    $settleSeconds = 0.0
    [void][double]::TryParse($settle, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$settleSeconds)
    $t = 300 + $expected * ($settleSeconds + 10)
    if ($t -lt 900) { $clientTimeout = 900 } else { $clientTimeout = [int][Math]::Truncate($t) }
    $code = 0
    foreach ($attempt in 0, 1, 2) {
        if ($attempt -gt 0) {
            # A renderer that could not create its GPU device never connected; retrying is safe.
            if (-not ($code -ne 0 -and (Test-GpuInitFailure $clientLog))) { break }
            Move-Item -LiteralPath $clientLog -Destination (Join-Path $outDir "client.gpu-init-failed-$attempt.log") -Force
            Start-Sleep -Seconds 3
            $code = 0
        }
        $code = Invoke-SmokeRole -Target $targets.Client -Log $clientLog -WallSeconds $clientTimeout -Arguments (@($clientUrl) +
            $commonArgs + @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080', "-MB2ViewShots=$views",
                "-MB2ViewShotDir=$outDir", "-MB2ViewShotSettle=$settle") + $clientExtra)
    }
} finally {
    Stop-SmokeRole $server
    $server = $null
}
$epochEnd = Get-UnixSeconds

$shots = @(Get-ChildItem -LiteralPath $outDir -Filter '*.png' -File -ErrorAction SilentlyContinue).Count
$startWord = $start; if (-not $startWord) { $startWord = 'default' }
$argsWord = $clientArgsWord; if (-not $argsWord) { $argsWord = 'none' }
$gpuWord = $gpuOthers; if (-not $gpuWord) { $gpuWord = 'unknown' }
$commit = (git -C $repoRoot rev-parse --short HEAD) | Out-String
$commit = $commit.Trim()
git -C $repoRoot diff --quiet HEAD -- Content Source
if ($LASTEXITCODE) { $commit += '+dirty' }

$lines = @(
    "mode=$Mode clock=$clock weather=$weather start=$startWord settle_s=$settle client_args=$argsWord client_exit=$code views=$expected shots=$shots epoch_start=$epochStart epoch_end=$epochEnd"
    "gpu_at_start=$gpuWord (utilization, memory used) other_unreal_processes=$ueOthers"
    "commit=$commit"
    "skipped_packages=$(Get-SmokeLogCount $clientLog 'SkipPackage')"
)
# grep SkipPackage | sed 's/.*SkipPackage: //; s/ (0x.*//' | sort -u | sed 's/^/  missing /'
$missing = New-Object 'System.Collections.Generic.SortedSet[string]' ([StringComparer]::Ordinal)
foreach ($line in @(Get-SmokeLogLines $clientLog | Where-Object { $_.Contains('SkipPackage') })) {
    $line = $line -replace '^.*SkipPackage: ', ''
    $cut = $line.IndexOf(' (0x')
    if ($cut -ge 0) { $line = $line.Substring(0, $cut) }
    [void]$missing.Add($line)
}
foreach ($m in $missing) { $lines += "  missing $m" }
$lines += 'client_warnings (Marburgum, materials, meshes, textures):'
# grep -E ... | sed 's/^\[[^]]*\]\[[^]]*\]/  /' | sort | uniq -c | sort -rn | head -30
$warningPattern = 'LogMarburgum[A-Za-z]*: (Warning|Error)|LogMaterial: (Warning|Error)|LogStaticMesh: (Warning|Error)|LogTexture: (Warning|Error)|LogStreaming: (Warning|Error)'
$counts = @{}
foreach ($line in @(Get-SmokeLogLines $clientLog | Where-Object { $_ -cmatch $warningPattern })) {
    $line = $line -replace '^\[[^\]]*\]\[[^\]]*\]', '  '
    if ($counts.ContainsKey($line)) { $counts[$line]++ } else { $counts[$line] = 1 }
}
$rows = @($counts.Keys | ForEach-Object { New-Object PSObject -Property @{ Count = $counts[$_]; Text = ('{0,7} {1}' -f $counts[$_], $_) } })
$rows = @($rows | Sort-Object -Property @{ Expression = { $_.Count }; Descending = $true }, @{ Expression = { $_.Text }; Descending = $true } |
    Select-Object -First 30)
foreach ($row in $rows) { $lines += $row.Text }
$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary $lines
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode
Get-SmokeLogLines $summary
if ($code -eq 0 -and $shots -eq $expected) { exit 0 }
exit 1
