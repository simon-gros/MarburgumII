# Windows twin of create-npc-content.sh (same steps, same commandlet and arguments).
# Regenerate what an authored NPC's body needs (ADR-081) with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Characters/Props/SM_FishingRod          a period spinning rod built from code on M_Vehicle (rod step;
#                                                   needs scripts\create-vehicle-content.ps1's M_Vehicle)
#   fishing_rod_r on SK_Mannequin                   the grip socket on hand_r, fitted to the hold pose (animation step)
#   Content/Characters/Animations/A_FishHold/Cast/Wait/Reel   the angler's and the player's fishing clips, left arm solved
#                                                   onto the reel handle or the rod butt key by key (animation step)
#   Content/Characters/Animations/A_NpcTalk         conversation gestures (animation step)
# Usage: scripts\create-npc-content.ps1 [rod|animation|all]   (default: all)
# The commandlet lives in the Editor target, so build MarburgumIIEditor first.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('rod', 'animation') -ccontains $_ } { $steps = $Which }
    'all' { $steps = 'rod,animation' }
    default { Exit-Usage 'Usage: scripts\create-npc-content.ps1 [rod|animation|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumNpcContent' "-Steps=$steps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
