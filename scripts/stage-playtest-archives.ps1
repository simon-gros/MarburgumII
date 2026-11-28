# Windows twin of stage-playtest-archives.sh (same steps and exit codes).
# Cook and stage the Win64 client and dedicated server, then copy both trees into a
# playtest run directory. The freshness guard only seals archives that live inside that run.
# Usage: $env:UE_ROOT = '...'; scripts\stage-playtest-archives.ps1 <run-dir>
param([string]$RunDir)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
# ${1:?playtest run directory}: bash reports the missing argument and exits 1.
if (-not $RunDir) { Exit-Failure "$($MyInvocation.MyCommand.Name): RunDir: playtest run directory" }

# Absolute: a drive letter and a root (C:\...) or a UNC path (\\server\share\...).
if ($RunDir -notmatch '^([A-Za-z]:[\\/]|\\\\)') { Exit-Usage "Run directory must be absolute: $RunDir" }

& (Join-Path $PSScriptRoot 'package-windows.ps1')
if ($LASTEXITCODE) { exit $LASTEXITCODE }

foreach ($side in 'WindowsClient', 'WindowsServer') {
    $dest = Join-Path $RunDir $side
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
# cp -a: Copy-Item keeps the files' contents and last-write times.
Copy-Item -LiteralPath (Join-Path $repoRoot 'Saved\StagedBuilds\WindowsClient') -Destination (Join-Path $RunDir 'WindowsClient') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'Saved\StagedBuilds\WindowsServer') -Destination (Join-Path $RunDir 'WindowsServer') -Recurse -Force
"Staged playtest archives in $RunDir"
