# Real clients on /Game/World/lent. Windows twin of street-multiplayer-smoke-linux.sh (same cues, files and exit codes).
#   editor|staged                 daytime matrix, including settlement and the cap
#   editor night|staged night     two clients sharing a residential-block street at night
#   drill                         job-object cleanup, no Unreal
# Port MB2_CROWD_PORT (default 27853). Never 7777 or 17777.
param([string]$Mode = 'editor', [string]$CaseName = 'day')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27853; if ($env:MB2_CROWD_PORT) { $port = [int]$env:MB2_CROWD_PORT }
if ($port -eq 7777 -or $port -eq 17777) { Exit-Usage "Refusing port $port" }

if ($env:MB2_CROWD_OUT) { $out = $env:MB2_CROWD_OUT } else { $out = Join-Path $repoRoot ("Saved\StreetMulti\" + (Get-UtcStamp)) }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$sha = 'unknown'
try {
    $head = git -C $repoRoot rev-parse HEAD 2>$null
    if (-not $LASTEXITCODE -and $head) { $sha = "$head".Trim() }
} catch { $sha = 'unknown' }
$roles = New-Object System.Collections.Generic.List[object]
$lastRole = $null
$scratchLogs = New-Object System.Collections.Generic.List[string]
$exitCode = 0

if (-not ('MarburgumII.Smoke.StreetMultiConsole' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace MarburgumII.Smoke
{
    public static class StreetMultiConsole
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool GenerateConsoleCtrlEvent(uint evt, uint group);
        [DllImport("kernel32.dll")]
        public static extern IntPtr GetConsoleWindow();
    }
}
'@
}

# kill -- -pid (SIGTERM to the group): CTRL_BREAK to the role's process group, UE's clean exit request, without
# waiting. Without a console to carry it, the role's job is terminated instead.
function Send-Term($Role) {
    if (-not $Role.Alive) { return }
    if ([MarburgumII.Smoke.StreetMultiConsole]::GetConsoleWindow() -ne [IntPtr]::Zero -and
        [MarburgumII.Smoke.StreetMultiConsole]::GenerateConsoleCtrlEvent(1, [uint32]$Role.Pid)) { return }
    $Role.Stop(0)
}

# The bash trap on EXIT/INT/TERM: stop every tracked role, record what is left, and return the exit code (3 when a
# process or the port outlived the run).
function Stop-All([int]$Status) {
    foreach ($role in $roles) { Send-Term $role }
    Start-Sleep -Seconds 1
    # kill -KILL -- -pid
    foreach ($role in $roles) { if ($role.Alive) { $role.Stop(0) } }
    for ($tick = 1; $tick -le 25; $tick++) {
        $any = $false
        foreach ($role in $roles) { if ($role.Alive) { $any = $true } }
        if (-not $any) { break }
        Start-Sleep -Milliseconds 200
    }
    $cleanup = @()
    foreach ($role in $roles) {
        if ($role.Alive) { $cleanup += "still_alive $($role.Pid)"; $Status = 3 }
    }
    if ($Mode -ne 'drill' -and (Test-UdpPortInUse $port)) { $cleanup += "port_busy $port"; $Status = 3 }
    if (-not $cleanup.Count) { $cleanup = @("clear pids=$($roles.Count)") }
    Write-SmokeFile (Join-Path $out 'cleanup.txt') $cleanup
    # printf '%s\n' "${pids[@]}": one empty line when there are none.
    if ($roles.Count) { Write-SmokeFile (Join-Path $out 'pids.txt') @($roles | ForEach-Object { $_.Pid }) }
    else { Write-SmokeText (Join-Path $out 'pids.txt') "`n" }
    foreach ($role in $roles) { $role.Dispose() }
    foreach ($log in $scratchLogs) { Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue }
    $Status
}

# setsid "$@" >/dev/null 2>&1 &: output goes to a scratch file outside the evidence directory, removed at the end.
function Start-Tracked([string]$Exe, [string[]]$Arguments) {
    $log = Join-Path ([IO.Path]::GetTempPath()) ("mb2-street-multi-" + [Guid]::NewGuid().ToString('N') + '.log')
    $scratchLogs.Add($log)
    $script:lastRole = Start-SmokeRole -Exe $Exe -Arguments $Arguments -Log $log
    $roles.Add($script:lastRole)
}

function Wait-Log([string]$File, [string]$Pattern, [int]$Seconds) {
    Wait-SmokeLog $File $Pattern $Seconds -PollSeconds 1
}

# scripts\mb2.ps1 <args>, with each argument quoted exactly: the expected clock carries double quotes and spaces, which
# Windows PowerShell 5.1 does not pass intact through mb2.ps1's own native call. Returns the exit code.
function Invoke-Mb2Exact([string[]]$Arguments) {
    if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
        [Console]::Error.WriteLine('Rust (cargo) is required for the repository tooling; install stable Rust (rustup, MSVC host).')
        return 2
    }
    cargo build --quiet --release --manifest-path (Join-Path $repoRoot 'tools\mb2\Cargo.toml') | Out-Host
    if ($LASTEXITCODE) { return $LASTEXITCODE }
    $psi = New-Object System.Diagnostics.ProcessStartInfo((Join-Path $repoRoot 'tools\mb2\target\release\mb2.exe'))
    $psi.UseShellExecute = $false
    $psi.Arguments = (@($Arguments | ForEach-Object { [MarburgumII.Smoke.Role]::Quote($_) }) -join ' ')
    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    $process.ExitCode
}

# Everything the bash runs under its trap; sets $script:exitCode and returns where the bash exits.
function Invoke-Main {
    if ($Mode -eq 'drill') {
        $hold = '30'; if ($env:DRILL_SLEEP) { $hold = $env:DRILL_SLEEP }
        $holdSeconds = [double]::Parse($hold, [Globalization.CultureInfo]::InvariantCulture)
        $sleeper = @('-NoProfile', '-NonInteractive', '-Command', "Start-Sleep -Milliseconds $([int]($holdSeconds * 1000))")
        Start-Tracked 'powershell.exe' $sleeper
        Start-Tracked 'powershell.exe' $sleeper
        Start-Tracked 'powershell.exe' $sleeper
        # DRILL_STUBBORN=1: one that ignores the clean exit request, as a slow Unreal shutdown does, and only goes when
        # its job is terminated (ping carries on through CTRL_BREAK).
        if ($env:DRILL_STUBBORN -eq '1') {
            Start-Tracked (Join-Path $env:SystemRoot 'System32\PING.EXE') @('-n', "$([int][Math]::Ceiling($holdSeconds) + 1)", '127.0.0.1')
        }
        "drill_pids=$(@($roles | ForEach-Object { $_.Pid }) -join ' ')"
        $drillHold = '1'; if ($env:DRILL_HOLD) { $drillHold = $env:DRILL_HOLD }
        Start-Sleep -Milliseconds ([int]([double]::Parse($drillHold, [Globalization.CultureInfo]::InvariantCulture) * 1000))
        $script:exitCode = 0; if ($env:DRILL_EXIT) { $script:exitCode = [int]$env:DRILL_EXIT }
        return
    }

    if ($CaseName -ne 'day' -and $CaseName -ne 'night') {
        Write-Warn 'Usage: scripts\street-multiplayer-smoke-windows.ps1 [editor|staged|drill] [day|night]'
        $script:exitCode = 2; return
    }
    if (Test-UdpPortInUse $port) {
        Write-Warn "UDP port $port is already in use"
        $script:exitCode = 2; return
    }

    $uproject = Join-Path $repoRoot 'MarburgumII.uproject'
    switch ($Mode) {
        'editor' {
            if (-not $env:UE_ROOT) { Write-Warn 'Set UE_ROOT'; $script:exitCode = 2; return }
            $global:LASTEXITCODE = 0
            & (Join-Path $repoRoot 'scripts\check-engine-version.ps1') $env:UE_ROOT | Out-Host
            if ($LASTEXITCODE) { $script:exitCode = $LASTEXITCODE; return }
            $bin = Join-Path $env:UE_ROOT 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
            $serverExe = $bin; $serverPrefix = @($uproject)
            $clientExe = $bin; $clientPrefix = @($uproject)
        }
        'staged' {
            $binS = Get-StagedExecutable 'Server'
            $binC = Get-StagedExecutable 'Client'
            if (-not ((Test-Path -LiteralPath $binS -PathType Leaf) -and (Test-Path -LiteralPath $binC -PathType Leaf))) {
                Write-Warn 'No staged build'; $script:exitCode = 2; return
            }
            $serverExe = $binS; $serverPrefix = @()
            $clientExe = $binC; $clientPrefix = @()
        }
        default {
            Write-Warn 'Usage: scripts\street-multiplayer-smoke-windows.ps1 [editor|staged|drill] [day|night]'
            $script:exitCode = 2; return
        }
    }

    $map = '/Game/World/lent'
    # -RenderOffScreen as well as -nullrhi, so a client never opens a window on the owner's screen.
    $common = @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput', '-nullrhi', '-RenderOffScreen')
    $serverLog = Join-Path $out 'server.log'
    if ($CaseName -eq 'night') {
        $clock = @('-MB2Clock=1996-06-18T00:00:00Z')
        $expectClock = 'marburgum="Tuesday 18 June 1996 02:00"'
        $nightFlag = @('-MB2CrowdNight')
    } else {
        $clock = @('-MB2Clock=1996-06-18T08:00:00Z')
        $expectClock = 'marburgum="Tuesday 18 June 1996 10:00"'
        $nightFlag = @()
    }

    $script:lastRole = Start-SmokeRole -Exe $serverExe -Log $serverLog `
        -Arguments ($serverPrefix + @($map, '-server', "-port=$port", '-MB2CrowdMatrix', '-MB2StreetPlaces=0', '-MB2NetRate') +
            $clock + @('-MB2Weather=clear_dry', '-MB2CrowdSettle=90') + $nightFlag + $common)
    $roles.Add($script:lastRole)

    function Start-Client([string]$Name, [string]$Log) {
        $script:lastRole = Start-SmokeRole -Exe $clientExe -Log $Log `
            -Arguments ($clientPrefix + @("127.0.0.1:${port}?MB2Character=$Name", '-game', '-MB2CrowdWatch') + $common)
        $roles.Add($script:lastRole)
    }

    if (-not (Wait-Log $serverLog 'event=street_ready' 180)) {
        Write-Warn 'server did not become ready'; $script:exitCode = 1; return
    }
    # The graph crosses the Drava, keeps every bridge edge (185 on the nine windows, 263 on the five by five) and is dry.
    if (-not (Test-SmokeLog $serverLog 'cross_river=1') -or -not (Test-SmokeLog $serverLog 'bridge_edges=([1-9][0-9]*)/\1\b') -or
        -not (Test-SmokeLog $serverLog 'edges_wet=0')) {
        Write-Warn 'walk graph is not the qualified one'; $script:exitCode = 1; return
    }
    if (-not (Wait-Log $serverLog $expectClock 30)) {
        Write-Warn "world clock was not $expectClock"; $script:exitCode = 1; return
    }
    if (-not (Test-SmokeLog $serverLog 'offset=\+2')) {
        Write-Warn 'world clock was not on summer time'; $script:exitCode = 1; return
    }

    if ($CaseName -eq 'night') {
        Start-Client 'crowd-a' (Join-Path $out 'client-a.log')
        Start-Client 'crowd-b' (Join-Path $out 'client-b.log')
        if (-not (Wait-Log $serverLog 'event=matrix_done' 220)) {
            Write-Warn 'night matrix did not finish'; $script:exitCode = 1; return
        }
    } else {
        Start-Client 'crowd-a' (Join-Path $out 'client-a.log')
        if (-not (Wait-Log $serverLog 'phase=please_second' 120)) {
            Write-Warn 'one-player commercial sample did not finish'; $script:exitCode = 1; return
        }
        Start-Client 'crowd-b' (Join-Path $out 'client-b.log')
        $clientB = $script:lastRole
        if (-not (Wait-Log $serverLog 'phase=please_drop ' 420)) {
            Write-Warn 'did not reach disconnect cue'; $script:exitCode = 1; return
        }
        Send-Term $clientB
        if (-not (Wait-Log $serverLog 'name=E_dropped state=lifecycle pass=1' 50)) {
            Write-Warn 'disconnect did not drop the viewer'; $script:exitCode = 1; return
        }
        Start-Client 'crowd-b' (Join-Path $out 'client-b2.log')
        if (-not (Wait-Log $serverLog 'phase=please_third' 120)) {
            Write-Warn 'did not reach the third-client cue'; $script:exitCode = 1; return
        }
        Start-Client 'crowd-c' (Join-Path $out 'client-c.log')
        $clientC = $script:lastRole
        if (-not (Wait-Log $serverLog 'phase=please_drop_cap' 200)) {
            Write-Warn 'did not reach the cap-drop cue'; $script:exitCode = 1; return
        }
        Send-Term $clientC
        if (-not (Wait-Log $serverLog 'event=matrix_done' 90)) {
            Write-Warn 'matrix did not finish'; $script:exitCode = 1; return
        }
    }

    # Scenario verdicts, client watch lines and net-rate samples into $out\summary.json.
    $status = Invoke-Mb2Exact @('crowd-summary', $out, $sha, $CaseName, $expectClock)
    # Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
    if ($Mode -eq 'staged') {
        $stamp = Join-Path $out 'build-stamp.txt'
        Remove-Item -LiteralPath $stamp -Force -ErrorAction SilentlyContinue
        Add-BuildStampCheck $stamp $Mode
    }
    "summary_status=$status"
    $script:exitCode = $status
}

$final = 1
try {
    Invoke-Main
} catch {
    # set -e: a failing command ends the run with status 1.
    Write-Warn "$_"
    $exitCode = 1
} finally {
    $final = Stop-All $exitCode
}
exit $final
