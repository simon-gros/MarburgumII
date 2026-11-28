# Windows twin of bridge-cross-linux.sh.
# One server-owned pedestrian walks an existing bridge route across the Drava (ADR-075 follow-up).
# Editor only. Port: MB2_BRIDGE_PORT (default 27851). Never 7777 or 17777.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = '27851'; if ($env:MB2_BRIDGE_PORT) { $port = $env:MB2_BRIDGE_PORT }
if ($port -eq '7777' -or $port -eq '17777') {
    Exit-Usage "Refusing port $port"
}
$ue = Assert-EngineRoot

$outDir = Join-Path $repoRoot ('Saved\BridgeCross\' + (Get-UtcStamp))
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$map = '/Game/World/lent'
$uproject = Join-Path $repoRoot 'MarburgumII.uproject'
$editor = Join-Path $ue 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$common = @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput')
$serverLog = Join-Path $outDir 'server.log'
$clientLog = Join-Path $outDir 'client.log'

if (Test-UdpPortInUse ([int]$port)) {
    Exit-Failure "UDP port $port is already in use"
}

$server = Start-SmokeRole -Exe $editor -Log $serverLog -Arguments (@($uproject, $map, '-server', "-port=$port", '-MB2BridgeCross',
        '-MB2StreetPlaces=0', "-MB2WalkGraphAudit=$(Join-Path $outDir 'audit')") + $common)
try {
    # 180 x 1 s for the street, as the bash loop.
    if (-not (Wait-SmokeLog $serverLog 'event=street_ready' 180 $server 1)) {
        if (-not $server.Alive -and -not (Test-SmokeLog $serverLog 'event=street_ready')) {
            Exit-Failure 'Server exited before the street was ready'
        }
        Exit-Failure 'Street was not ready within 180s'
    }

    $clientCode = Invoke-SmokeRole -Exe $editor -WallSeconds 820 -Log $clientLog -Arguments (@($uproject,
            "127.0.0.1:${port}?MB2Character=mb2bridge$(Get-Random -Maximum 32768)", '-game', '-MB2BridgeWatch',
            "-MB2StreetShot=$(Join-Path $outDir 'bridge')", '-MB2StreetTimeout=760', '-RenderOffScreen', '-windowed', '-ForceRes',
            '-ResX=1920', '-ResY=1080') + $common)

    $resultPattern = 'event=bridge_cross result=arrived_both|event=bridge_cross result=timeout|event=bridge_cross result=wet|event=bridge_cross result=lost|event=bridge_cross result=no_stari_route|event=bridge_cross result=route_too_short'
    for ($i = 0; $i -lt 30; $i++) {
        if (Test-SmokeLog $serverLog $resultPattern) { break }
        if (-not $server.Alive) { break }
        Start-Sleep -Seconds 1
    }

    Get-SmokeLogMatches $serverLog 'event=bridge_cross result=started|event=bridge_cross result=arrived'
    Get-SmokeLogMatches $clientLog 'event=bridge_watch result=shot'
    "client_exit=$clientCode"
    if ((Test-SmokeLog $serverLog 'event=bridge_cross result=arrived leg=south_to_north') -and
        (Test-SmokeLog $serverLog 'event=bridge_cross result=arrived_both') -and
        (Test-SmokeLog $clientLog 'event=bridge_watch result=walking')) {
        'BRIDGE_CROSS arrived_both'
        $code = 0
    } else {
        [Console]::Error.WriteLine('BRIDGE_CROSS failed')
        $code = 1
    }
} finally {
    # kill, 1 s, kill -9.
    Stop-SmokeRole $server 1
}
exit $code
