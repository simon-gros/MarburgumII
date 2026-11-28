# Windows twin of gfx-baseline-linux.sh (same scene matrix, MB2_GFX_* variables, evidence files and exit code).
# GFX-00 rendering baseline of /Game/World/lent (Docs/GRAPHICS_ROADMAP.md). Runs the visual survey
# (scripts\visual-survey-windows.ps1) once per scene of a fixed time-of-day and weather matrix, with the graphics
# roadmap's river, quay and roof cameras (`mb2 gfx-views`) and the player's own camera with the HUD first, then one
# short scene at the Hotel Lent street door (ADR-076). Changes no setting: it only reads back what the renderer does.
#
#   scripts\gfx-baseline-windows.ps1 [editor|staged] [name]
#
# Scenes (summer, Maribor local time = UTC+2): morning 07:00, noon 12:00, dusk 21:00, night 23:30 (clear_dry);
# noon overcast; noon and night rain_wet; noon at the door. MB2_GFX_SCENES limits the run to a comma-separated
# subset (e.g. noon,night). MB2_GFX_SETTLE seconds per view (default 4). MB2_GFX_CLIENT_ARGS goes to every client (an A/B
# variant of the same build, e.g. -sm5 or -ini:Engine:[SystemSettings]:r.DynamicGlobalIlluminationMethod=0). MB2_SURVEY_PORT as for the survey (default
# 27891; never 7777 or 17777, which are for playtests).
#
# This host is shared: other sessions' smokes render on the same GPU and lengthen the frame-time tails. A monitor samples
# every 5 s how many other Unreal game, server and editor processes are running (gpu-monitor.tsv), and baseline.json
# names, per scene, how many samples inside its run saw one ("contended"). GPU utilisation is recorded but is no signal:
# the survey client runs uncapped and keeps the GPU near full on its own.
#
# Evidence in Saved\GfxBaseline\<name or UTC timestamp>\: meta.env (source and engine, cooked content hash for staged,
# GPU and driver, other GPU users at the start), gpu-monitor.tsv, one directory per scene (PNGs, stats.tsv, render_config.json, logs,
# summary.txt), and baseline.csv / baseline.json from `mb2 gfx-baseline`. Editor numbers are labelled editor_game_mode
# and compare only with each other; a frame budget needs the staged (packaged) run.
param([string]$Mode = 'editor', [string]$Name)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Mode) { $Mode = 'editor' }
if (-not $Name) { $Name = Get-UtcStamp }
$outDir = Join-Path $repoRoot "Saved\GfxBaseline\$Name"
$settle = '4'; if ($env:MB2_GFX_SETTLE) { $settle = $env:MB2_GFX_SETTLE }
$doorStart = 'door_r1539756' # Hotel Lent, the door smoke's default building

if (@('staged', 'editor') -cnotcontains $Mode) { Exit-Usage 'Usage: scripts\gfx-baseline-windows.ps1 [editor|staged] [name]' }

# scene|UTC clock|weather|start|views (all: survey + gfx views; player: only the player's camera)
$scenes = @(
    "1_morning|2026-06-18T05:00:00Z|clear_dry||all"
    "2_noon|2026-06-18T10:00:00Z|clear_dry||all"
    "3_dusk|2026-06-18T19:00:00Z|clear_dry||all"
    "4_night|2026-06-18T21:30:00Z|clear_dry||all"
    "5_overcast|2026-06-18T10:00:00Z|overcast||all"
    "6_rain_noon|2026-06-18T10:00:00Z|rain_wet||all"
    "7_rain_night|2026-06-18T21:30:00Z|rain_wet||all"
    "8_door_noon|2026-06-18T10:00:00Z|clear_dry|$doorStart|player"
)

# How many Unreal game, server and editor processes run whose command line does not contain Exclude (pgrep -af on the
# Linux binaries' paths | grep -vcF). Also run inside the monitor job, so it is a self-contained script block.
$countUnreal = {
    param([string]$Exclude)
    $pattern = '\\Binaries\\Win64\\(UnrealEditor(-Cmd)?|MarburgumII(Client|Server)[^\\]*)\.exe'
    try {
        @(Get-CimInstance Win32_Process -Property CommandLine -ErrorAction Stop | Where-Object {
            $_.CommandLine -and $_.CommandLine -match $pattern -and -not ($Exclude -and $_.CommandLine.Contains($Exclude))
        }).Count
    } catch { 0 }
}

# nvidia-smi ... | head -n 1, or empty without an NVIDIA driver.
function Get-NvidiaSmiLine([string]$Query, [string]$Format) {
    if (-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)) { return '' }
    $ErrorActionPreference = 'Continue'
    $lines = @(& nvidia-smi "--query-gpu=$Query" "--format=$Format" 2>$null)
    if ($lines.Count) { "$($lines[0])" } else { '' }
}

# build-stamp.ps1 check 2>&1 (its exit code is not the runner's).
function Get-BuildStampCheck {
    $ErrorActionPreference = 'Continue'
    @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-stamp.ps1') check 2>&1 | ForEach-Object { "$_" })
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$surveyViews = (@(Invoke-Mb2 survey-views (Join-Path $repoRoot 'Content\Marburgum\Street\lent_walk_graph.json')) -join "`n").TrimEnd("`n")
if ($LASTEXITCODE) { exit 1 }
$gfxViews = (@(Invoke-Mb2 gfx-views) -join "`n").TrimEnd("`n")
if ($LASTEXITCODE) { exit 1 }

$meta = @("mode=$Mode")
$meta += "source_sha=$((git -C $repoRoot rev-parse HEAD | Out-String).Trim())"
if (git -C $repoRoot status --porcelain -- Source Config Content Data Plugins) { $meta += 'source_dirty=1' } else { $meta += 'source_dirty=0' }
$meta += "started_utc=$(Get-UtcIso)"
$meta += "settle_s=$settle"
$meta += "client_args=$env:MB2_GFX_CLIENT_ARGS"
$meta += 'resolution=1920x1080'
$os = [Environment]::OSVersion.Version
$meta += "host_kernel=$($os.Major).$($os.Minor).$($os.Build)"
$meta += "gpu=$(Get-NvidiaSmiLine 'name,driver_version,memory.total' 'csv,noheader')"
$meta += "gpu_at_start=$(Get-NvidiaSmiLine 'utilization.gpu,memory.used' 'csv,noheader')"
$otherUnreal = & $countUnreal ''
$meta += "other_unreal_processes=$otherUnreal"
if ($Mode -eq 'editor') {
    $engineVersion = ''
    if ($env:UE_ROOT) {
        $buildVersion = Join-Path $env:UE_ROOT 'Engine\Build\Build.version'
        if (Test-Path -LiteralPath $buildVersion -PathType Leaf) {
            $compact = [IO.File]::ReadAllText($buildVersion) -replace '\s', ''
            $engineVersion = (@([regex]::Matches($compact, '"MajorVersion":[0-9]*,"MinorVersion":[0-9]*,"PatchVersion":[0-9]*') |
                ForEach-Object { $_.Value }) -join "`n")
        }
    }
    $meta += "engine_build_version=$engineVersion"
} else {
    # The cooked content the client actually loaded: SHA-256 of each pak/IoStore container, then of that list.
    $paks = Join-Path $repoRoot 'Saved\StagedBuilds\WindowsClient\MarburgumII\Content\Paks'
    $digests = @()
    if (Test-Path -LiteralPath $paks -PathType Container) {
        $names = [string[]]@(Get-ChildItem -LiteralPath $paks -Force | Where-Object { -not $_.Name.StartsWith('.') } |
            ForEach-Object { $_.Name })
        [Array]::Sort($names, [StringComparer]::Ordinal)
        foreach ($n in $names) {
            $item = Join-Path $paks $n
            if (Test-Path -LiteralPath $item -PathType Leaf) { $digests += "$(Get-SmokeSha256 $item)  $n" }
        }
    }
    $cooked = Join-Path $outDir 'cooked-content.sha256'
    Write-SmokeFile $cooked $digests
    $meta += "cooked_content_sha256=$(Get-SmokeSha256 $cooked)"
    # build-stamp check 2>&1 | sed 's/: /=/'
    $firstColon = New-Object regex ': '
    $meta += @(Get-BuildStampCheck | ForEach-Object { $firstColon.Replace($_, '=', 1) })
}
$metaFile = Join-Path $outDir 'meta.env'
Write-SmokeFile $metaFile $meta
Get-SmokeLogLines $metaFile
if ("$otherUnreal" -ne '0') {
    Write-Warn 'WARNING: other Unreal processes are running; they share the GPU and skew the numbers (named in meta.env).'
}

# epoch, GPU utilisation %, GPU memory MiB, other Unreal processes (ours carry this run's directory on their command line)
$monitorFile = Join-Path $outDir 'gpu-monitor.tsv'
Write-SmokeText $monitorFile "epoch`tgpu_util`tgpu_mem_mb`tother_unreal`n"
$monitor = Start-Job -ArgumentList $monitorFile, $outDir, $countUnreal.ToString() -ScriptBlock {
    param([string]$File, [string]$OutDir, [string]$CountSource)
    $count = [scriptblock]::Create($CountSource)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $smi = [bool](Get-Command nvidia-smi -ErrorAction SilentlyContinue)
    while ($true) {
        $gpu = ''
        if ($smi) {
            $lines = @(& nvidia-smi '--query-gpu=utilization.gpu,memory.used' '--format=csv,noheader,nounits' 2>$null)
            if ($lines.Count) { $gpu = "$($lines[0])".Replace(' ', '').Replace(',', "`t") }
        }
        $epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        [IO.File]::AppendAllText($File, "$epoch`t$gpu`t$(& $count $OutDir)`n", $utf8)
        Start-Sleep -Seconds 5
    }
}

$failed = 0
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$survey = Join-Path $PSScriptRoot 'visual-survey-windows.ps1'
try {
    foreach ($entry in $scenes) {
        $scene, $clock, $weather, $start, $which = $entry.Split('|')
        if ($env:MB2_GFX_SCENES -and -not (",$($env:MB2_GFX_SCENES),".Contains(",$($scene.Substring($scene.IndexOf('_') + 1)),"))) {
            continue
        }
        if ($which -eq 'player') {
            $views = 'player_hud@player@player@0'
        } else {
            $views = "player_hud@player@player@0;$gfxViews;$surveyViews"
        }
        $startNote = ''; if ($start) { $startNote = ", start $start" }
        "== $scene ($clock, $weather$startNote)"
        $env:MB2_SURVEY_OUT = Join-Path $outDir $scene
        $env:MB2_SURVEY_CLOCK = $clock
        $env:MB2_SURVEY_WEATHER = $weather
        $env:MB2_SURVEY_START = $start
        $env:MB2_SURVEY_VIEWS = $views
        $env:MB2_SURVEY_SETTLE = $settle
        $env:MB2_SURVEY_CLIENT_ARGS = $env:MB2_GFX_CLIENT_ARGS
        # visual-survey ... > "$out_dir/$scene.out" 2>&1
        $code = Invoke-SmokeRole -Exe $powershell -WorkDir $repoRoot -Log (Join-Path $outDir "$scene.out") `
            -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $survey, $Mode, "$Name-$scene")
        if ($code -ne 0) { $failed = 1 }
        $sceneSummary = Join-Path $outDir "$scene\summary.txt"
        if (Test-Path -LiteralPath $sceneSummary -PathType Leaf) {
            Get-SmokeLogLines $sceneSummary | Select-Object -First 3
        } else {
            "  no summary; see $(Join-Path $outDir "$scene.out")"
            $failed = 1
        }
    }

    Invoke-Mb2 gfx-baseline $outDir
    if ($LASTEXITCODE) { $failed = 1 }
} finally {
    # trap 'kill "$monitor_pid"' EXIT: the monitor samples until the very end, as in the bash twin.
    Stop-Job -Job $monitor -ErrorAction SilentlyContinue
    Remove-Job -Job $monitor -Force -ErrorAction SilentlyContinue
}
exit $failed
