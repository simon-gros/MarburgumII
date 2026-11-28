# Windows twin of create-vehicle-content.sh (same steps, same commandlet and arguments).
# Regenerate the content vehicles need (ADR-039) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Vehicles/<definition>/                 bodywork, wheels and doors for every catalogue car, from
#                                                  Data/Gameplay/vehicles.v0.1.json (the original's vehicle_mesh.cpp)
#   Content/Vehicles/Materials/M_Vehicle           the paint material, one MI_<definition> per car
#   Content/Characters/Animations/A_EnterVehicle   the original's door clips on the mannequin skeleton
#   Content/Characters/Animations/A_ExitVehicle
#   Content/World/lent.umap                        the authored lent_hatchback
# Usage: scripts\create-vehicle-content.ps1 [mesh|animation|place|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('mesh', 'animation', 'place') -ccontains $_ } { $steps = $Which }
    'all' { $steps = 'mesh,animation,place' }
    default { Exit-Usage 'Usage: scripts\create-vehicle-content.ps1 [mesh|animation|place|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumVehicleContent' "-Steps=$steps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
