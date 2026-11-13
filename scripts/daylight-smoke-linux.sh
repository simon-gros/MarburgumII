#!/usr/bin/env bash
# Day and night qualification (ADR-064) on the launch map: the six daylight phases of weather-smoke-linux.sh under a
# clear_dry sky (noon, evening, dusk, moonlit, moonless, clockrun), each against a dedicated server whose world clock
# is pinned to that moment. See the phase list there.
# Usage: scripts/daylight-smoke-linux.sh [staged|editor]
# Evidence is written to Saved/DaylightSmoke/<UTC timestamp>/.
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MB2_SMOKE_DIR=DaylightSmoke MB2_WEATHER_PHASES="${MB2_WEATHER_PHASES:-noon evening dusk moonlit moonless clockrun}" \
    exec "$repo_root/scripts/weather-smoke-linux.sh" "$@"
