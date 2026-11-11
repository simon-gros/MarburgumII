#!/usr/bin/env bash
# Fetches the OpenStreetMap extract the Lent ground is built from (ADR-040, ADR-045) through the public
# Overpass API, into Data/World/lent_ground/osm_lent.json.gz. The bounding box is the 1,536 m square
# of Lent and its eight neighbouring windows (plan ±768 m round the middle of Stari most) with 150 m
# to spare, in WGS84. Every way that is not a building (the footprints come from the original game), every multipolygon relation and every tagged node in it, with the
# nodes of those ways and relations. ODbL 1.0, © OpenStreetMap contributors.
set -euo pipefail
cd "$(dirname "$0")/.."
BBOX="46.54770,15.63387,46.56408,15.65801"
OUT=Data/World/lent_ground/osm_lent.json.gz
QUERY="[out:json][timeout:300];(way[!building](${BBOX});relation[type=multipolygon](${BBOX});node(${BBOX})(if:count_tags()>0););(._;>;);out body;"
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
curl --fail --silent --show-error --retry 3 --retry-delay 20 -A "MarburgumII lent-ground (github.com/simon-gros/MarburgumII)" -o "$TMP" \
    --data-urlencode "data=${QUERY}" https://overpass-api.de/api/interpreter
grep -q '"elements"' "$TMP"
gzip -9 -n -c "$TMP" > "$OUT"
echo "wrote $OUT ($(stat -c %s "$OUT") bytes)"
