# Windows twin of localize-linux.sh (same GatherText run, same summary lines and exit codes).
# QA U11: gather every player-facing LOCTEXT from the client and game modules, import the Slovene translation from
# Content/Localization/Game/sl/Game.po, compile the .locres files the client loads, and export the .po files again
# (new or changed strings appear there with an empty msgstr for the translator).
#
# Usage: $env:UE_ROOT = 'D:\path\to\UE_5.8.4'; scripts\localize-windows.ps1
# Exit status 0 when the pipeline ran; the summary lists how many Slovene entries are still untranslated.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$ue = Assert-EngineRoot

$log = Join-Path $repoRoot "Saved\Logs\localize-$(Get-UtcStamp).log"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
$code = Invoke-SmokeRole -Exe "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -Log $log -WorkDir $repoRoot -Arguments @(
    (Join-Path $repoRoot 'MarburgumII.uproject'), '-run=GatherText',
    "-config=$(Join-Path $repoRoot 'Config\Localization\Game.ini')", '-unattended', '-nop4', '-nosplash', '-nullrhi', '-log', '-stdout')
if ($code -ne 0) {
    [Console]::Error.WriteLine("GatherText failed; see $log")
    foreach ($line in @(Get-SmokeLogLines $log | Select-Object -Last 30)) { [Console]::Error.WriteLine($line) }
    exit 1
}

$po = Join-Path $repoRoot 'Content\Localization\Game\sl\Game.po'
if (-not (Test-Path -LiteralPath $po -PathType Leaf)) { Exit-Failure "No Slovene .po was written; see $log" }
# An entry is untranslated when its msgstr is empty (the header entry has an empty msgid and does not count).
$untranslated = 0
$total = 0
$id = $null
foreach ($line in @(Get-SmokeLogLines $po)) {
    if ($line.StartsWith('msgid "')) { $id = $line; $total++ }
    if ($line -ceq 'msgstr ""' -and $id -cne 'msgid ""') { $untranslated++ }
}
"Localization: $($total - 1) entries, $untranslated without a Slovene translation"
foreach ($culture in 'en', 'sl') {
    $locres = Join-Path $repoRoot "Content\Localization\Game\$culture\Game.locres"
    if (Test-Path -LiteralPath $locres -PathType Leaf) { "  ${culture}: $((Get-Item -LiteralPath $locres).Length) bytes in Game.locres" }
}
"Log: $log"
