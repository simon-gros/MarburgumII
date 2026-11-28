# Windows twin of create-weather-content.sh (same steps, same commandlet and arguments).
# Regenerate the content weather and ground surfaces need (ADR-061, ADR-062) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Marburgum/Weather/{SM,M}_WeatherCurtain   the rain and snow curtain round the camera (ADR-062)
#   Content/Marburgum/Weather/MI_WeatherClouds        the engine's simple volumetric cloud, so a cooked client carries it
#   Content/Marburgum/Weather/MPC_Weather             the ground's wetness and snow cover, read by Lent's paving and landscape
#                                                     (their materials come from scripts\rebuild-ground.ps1 materials)
#   Content/Marburgum/Lights/{SM_StreetLampMast,SM_StreetLampLantern,M_StreetLampMetal,M_StreetLampGlass}
#                                                     the street lamps the night is lit by (ADR-065); where they stand is
#                                                     Content/Marburgum/Lights/lent_street_lights.json (lent-ground --mode lamps)
#   Content/Marburgum/Lights/{M_WindowGlass,MPC_LitWindows}
#                                                     the facades' glass, lit at night by each pane's household (ADR-066);
#                                                     run before -run=MarburgumRebuildFacades, which writes the households
#   Content/World/Ground/Surfaces/PM_*        physical materials naming what Lent's paving is made of, set on its paved
#                                             materials so tyres and feet can tell asphalt from setts
#   Content/Audio/Footsteps/                  Kenney Impact Sounds 1.0 footsteps (CC0) and the wet-street steps
#                                             generated from them (`scripts\mb2.ps1 generate wet-footsteps`)
# The landscape's surface map, Content/Marburgum/Ground/lent_surface.bin, comes from
#   tools/lent-ground: cargo run --release -- surface-kinds --surface Data/World/lent_ground/surface.png --out Content/Marburgum/Ground/lent_surface.bin
#   Content/Vegetation/Materials/{M_PlantFoliage,M_PlantBark} and each species' MI_<id>_{Foliage,Bark}
#                                             the trees' and bushes' materials (GFX-01), set on the original's species meshes;
#                                             where they stand is Content/Marburgum/Vegetation/lent_plants.json (lent-ground --mode plants)
#   Content/World/StreetFurniture/SM_Street_*_01 and M_MarburgumPaint
#                                             the original's street kit (GFX-01): compiled for instancing, bounds and collision logged;
#                                             where it stands is Content/Marburgum/Furniture/lent_street_furniture.json (lent-ground --mode furniture)
# Usage: scripts\create-weather-content.ps1 [look|lamps|windows|surfaces|audio|plants|furniture|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first. Rerun surfaces after a ground rebuild
# that creates the paved materials afresh.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('look', 'lamps', 'windows', 'surfaces', 'audio', 'plants', 'furniture') -ccontains $_ } { $steps = $Which }
    'all' { $steps = 'look,lamps,windows,surfaces,audio,plants,furniture' }
    default { Exit-Usage 'Usage: scripts\create-weather-content.ps1 [look|lamps|windows|surfaces|audio|plants|furniture|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumWeatherContent' "-Steps=$steps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
