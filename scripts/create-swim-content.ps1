# Windows twin of create-swim-content.sh (same steps, same commandlet and arguments).
# Regenerate the content swimming needs (ADR-032) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Animations/A_Swim       the stroke, from the original game's swim cycle, on the mannequin skeleton
#   Content/Characters/Animations/A_SwimTread  treading water, upright, when the swimmer is not moving
#   Content/World/lent.umap                    water volumes over the Drava, cut from the level's water sheet
# Usage: scripts\create-swim-content.ps1 [animation|water|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('animation', 'water') -ccontains $_ } { $steps = $Which }
    'all' { $steps = 'animation,water' }
    default { Exit-Usage 'Usage: scripts\create-swim-content.ps1 [animation|water|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumSwimContent' "-Steps=$steps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
