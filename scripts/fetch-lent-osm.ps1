# Windows twin of fetch-lent-osm.sh (same query, same output file; curl.exe and .NET GZipStream for curl/gzip).
# Fetches the OpenStreetMap extract the Lent ground is built from (ADR-040, ADR-045, ADR-090) through the
# public Overpass API, into Data/World/lent_ground/osm_lent.json.gz. The bounding box is the 2,560 m square
# of Lent and the 24 windows round it (plan +/-1280 m round the middle of Stari most) with at least 150 m
# to spare, in WGS84. Every way that is not a building (the footprints come from the original game), every multipolygon relation and every tagged node in it, with the
# nodes of those ways and relations. ODbL 1.0, (c) OpenStreetMap contributors.
# The .gz is a valid gzip stream with no name and no timestamp (gzip -n), but .NET's deflate is not byte-identical to
# gzip -9; the decompressed JSON is what the ground build reads.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
Set-Location -LiteralPath $repoRoot
[Environment]::CurrentDirectory = $repoRoot
$bbox = '46.54289,15.62709,46.56889,15.66479'
$out = 'Data\World\lent_ground\osm_lent.json.gz'
$query = "[out:json][timeout:300];(way[!building]($bbox);relation[type=multipolygon]($bbox);node($bbox)(if:count_tags()>0););(._;>;);out body;"
$tmp = [IO.Path]::GetTempFileName()
try {
    & curl.exe --fail --silent --show-error --retry 3 --retry-delay 20 -A 'MarburgumII lent-ground (github.com/simon-gros/MarburgumII)' -o $tmp `
        --data-urlencode "data=$query" https://overpass-api.de/api/interpreter
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    # grep -q '"elements"'
    if (-not ([IO.File]::ReadAllText($tmp)).Contains('"elements"')) { exit 1 }
    # gzip -9 -n -c "$TMP" > "$OUT"
    $outPath = Join-Path $repoRoot $out
    $source = [IO.File]::OpenRead($tmp)
    try {
        $target = [IO.File]::Create($outPath)
        try {
            $gzip = New-Object System.IO.Compression.GZipStream($target, [System.IO.Compression.CompressionLevel]::Optimal)
            try { $source.CopyTo($gzip) } finally { $gzip.Dispose() }
        } finally { $target.Dispose() }
    } finally { $source.Dispose() }
    "wrote $out ($((Get-Item -LiteralPath $outPath).Length) bytes)"
} finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
}
