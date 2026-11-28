# Windows twin of daylight-smoke-linux.sh.
# Day and night qualification (ADR-064) on the launch map: the six daylight phases of weather-smoke-windows.ps1 under a
# clear_dry sky (noon, evening, dusk, moonlit, moonless, clockrun), each against a dedicated server whose world clock
# is pinned to that moment. See the phase list there.
# Usage: scripts\daylight-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\DaylightSmoke\<UTC timestamp>\.
$ErrorActionPreference = 'Stop'
$env:MB2_SMOKE_DIR = 'DaylightSmoke'
if (-not $env:MB2_WEATHER_PHASES) { $env:MB2_WEATHER_PHASES = 'noon evening dusk moonlit moonless clockrun' }
& (Join-Path $PSScriptRoot 'weather-smoke-windows.ps1') @args
exit $LASTEXITCODE
