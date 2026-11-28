# Windows twin of create-maps.sh (same maps, same commandlet and arguments).
# Regenerate the generated maps with the C++ editor commandlet (MarburgumIIEditorTools):
#   Content/Maps/FrontEnd.umap  (client offline front-end, no gameplay authority)
#   Content/Maps/NetSmoke.umap  (Stage 0B network test map)
#   Content/Maps/Lent.umap      (Stage 1 authored Lent district, plus Content/Blockout/Materials)
# Usage: scripts\create-maps.ps1 [frontend|netsmoke|lent|all]   (default: all)
# The commandlet and the maps' classes live in the Editor target, so build MarburgumIIEditor first.
param([string]$Which = 'all')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
if (-not $Which) { $Which = 'all' }

if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }

switch -CaseSensitive ($Which) {
    { @('frontend', 'netsmoke', 'lent') -ccontains $_ } { $maps = $Which }
    'all' { $maps = 'frontend,netsmoke,lent' }
    default { Exit-Usage 'Usage: scripts\create-maps.ps1 [frontend|netsmoke|lent|all]' }
}

$ue = Assert-EngineRoot

& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Join-Path $repoRoot 'MarburgumII.uproject') `
    '-run=MarburgumCreateMaps' "-Maps=$maps" `
    -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
