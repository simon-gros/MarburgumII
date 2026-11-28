# Windows twin of partition-world.sh (same steps, order, commandlets and arguments).
# Make /Game/World/lent a World Partition map and build its HLODs (ADR-091), with the editor commandlets:
#   convert   the engine's WorldPartitionConvertCommandlet: each actor in its own package under
#             Content/__ExternalActors__/World/lent, the landscape split into streaming proxies (skipped once done)
#   settings  MarburgumPartitionLevel: the streaming grid, the HLOD layer /Game/World/HLOD/HLOD_Lent, no server
#             streaming, and which actors stream
#   ground    MarburgumPartitionLevel -Ground: the street lamps', plants' and street furniture's ground on the whole level,
#             <placement file>_ground.json beside each, as a client holds only the cells round its player
#   hlods     the engine's HLOD builder: one merged Nanite mesh per 256 m cell under Content/__ExternalActors__
# Usage: scripts\partition-world.ps1 [convert|settings|ground|hlods|all]   (default: all)
# Build MarburgumIIEditor first. Conversion and the HLOD build render (no -nullrhi); -RenderOffScreen keeps them off
# the desktop. The five by five needs more than 16 GB per editor process, so run it with enough memory free for one
# such process and nothing else heavy on the host, as for packaging.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }
$map = '/Game/World/lent'

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('convert', 'settings', 'ground', 'hlods') -ccontains $_ } { $steps = @($Which) }
    'all' { $steps = @('convert', 'settings', 'ground', 'hlods') }
    default { Exit-Usage 'Usage: scripts\partition-world.ps1 [convert|settings|ground|hlods|all]' }
}

$ue = Assert-EngineRoot

# Runs one commandlet; a failure ends the script with its exit code (set -e in the bash twin).
function Invoke-Editor {
    & "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') @args `
        -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

foreach ($step in $steps) {
    switch ($step) {
        'convert' {
            if (Test-Path -LiteralPath (Join-Path $repoRoot 'Content\__ExternalActors__\World\lent') -PathType Container) {
                "$map is already partitioned; skipping the conversion."
                continue
            }
            Invoke-Editor '-run=WorldPartitionConvertCommandlet' $map '-AllowCommandletRendering' '-RenderOffScreen'
        }
        'settings' {
            Invoke-Editor '-run=MarburgumPartitionLevel' "-Map=$map" '-nullrhi'
        }
        'ground' {
            Invoke-Editor '-run=MarburgumPartitionLevel' "-Map=$map" '-Ground' '-nullrhi'
        }
        'hlods' {
            Invoke-Editor '-run=WorldPartitionBuilderCommandlet' $map '-Builder=WorldPartitionHLODsBuilder' `
                '-AllowCommandletRendering' '-RenderOffScreen'
        }
    }
}
