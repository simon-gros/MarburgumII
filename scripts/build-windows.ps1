# Build one target for Win64 Development. Windows twin of build-linux.sh.
# Usage: $env:UE_ROOT = "D:\path\to\UnrealEngine-5.8.4"; scripts\build-windows.ps1 [MarburgumIIEditor|MarburgumIIClient|MarburgumIIServer|MarburgumII]
# MarburgumIIServer and MarburgumIIClient need a source-built engine; the Epic Games Launcher build refuses them.
param(
    [ValidateSet('MarburgumIIEditor', 'MarburgumIIClient', 'MarburgumIIServer', 'MarburgumII')]
    [string]$Target = 'MarburgumIIEditor'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$ueRoot = $env:UE_ROOT

if (-not $ueRoot) {
    [Console]::Error.WriteLine('Set UE_ROOT to your Unreal Engine 5.8.4 installation root.')
    exit 2
}
& (Join-Path $PSScriptRoot 'check-engine-version.ps1') $ueRoot
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$buildScript = Join-Path $ueRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    [Console]::Error.WriteLine("Missing UnrealBuildTool script: $buildScript")
    exit 1
}
& $buildScript $Target Win64 Development "-Project=$(Join-Path $repoRoot 'MarburgumII.uproject')" -WaitMutex
exit $LASTEXITCODE
