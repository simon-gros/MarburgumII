# Windows twin of create-combat-content.sh (same steps, same commandlet and arguments).
# Regenerate the content fighting needs (ADR-041) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Animations/A_FightGuard   the original's guard loop on the mannequin skeleton
#   Content/Characters/Animations/A_FightJab     the original's jab
#   Content/Characters/Animations/A_FightHeavy   the original's committed swing
#   Content/Audio/Combat/                        Kenney Impact Sounds 1.0 (CC0) from SourceArt/Audio/Combat
# Usage: scripts\create-combat-content.ps1 [animation|audio|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('animation', 'audio') -ccontains $_ } { $steps = $Which }
    'all' { $steps = 'animation,audio' }
    default { Exit-Usage 'Usage: scripts\create-combat-content.ps1 [animation|audio|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumCombatContent' "-Steps=$steps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
