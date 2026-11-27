# Refuses any engine but the exact pin in .unreal-engine-version. Windows twin of check-engine-version.sh.
# Usage: scripts\check-engine-version.ps1 [engine_root]   (default: $env:UE_ROOT)
param([string]$EngineRoot = $env:UE_ROOT)
$ErrorActionPreference = 'Stop'

if (-not $EngineRoot) {
    [Console]::Error.WriteLine('Usage: $env:UE_ROOT = "D:\path\to\UnrealEngine-5.8.4"; scripts\check-engine-version.ps1 [engine_root]')
    exit 2
}
$versionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    [Console]::Error.WriteLine("Missing Unreal Engine build metadata: $versionFile")
    exit 1
}
& (Join-Path $PSScriptRoot 'mb2.ps1') engine-version $EngineRoot
exit $LASTEXITCODE
