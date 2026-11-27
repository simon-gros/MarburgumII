# The staged builds' stamp (Saved\StagedBuilds\BUILD_STAMP). Windows twin of build-stamp.sh, same keys and exit codes.
#
#   build-stamp.ps1 write <config>   after packaging (package-windows.ps1 does it)
#   build-stamp.ps1 check            prints the stamp and "build_fresh: yes|NO (...)"; exits 3 when the stamp is
#                                    missing, the executables changed since, or the game's inputs differ from the
#                                    packaged commit or have uncommitted edits
param([string]$Command, [string]$Config)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$staged = Join-Path $repoRoot 'Saved\StagedBuilds'
$stamp = Join-Path $staged 'BUILD_STAMP'
# What the packaged game is built from; a change anywhere here makes an older package stale.
$inputs = @('Source', 'Config', 'Content', 'Data', 'Plugins', 'MarburgumII.uproject')

function Executable([string]$Side) {
    # Development: MarburgumIIServer.exe; Shipping: MarburgumIIServer-Win64-Shipping.exe.
    $dir = Join-Path $staged "Windows$Side\MarburgumII\Binaries\Win64"
    Get-ChildItem -LiteralPath $dir -Filter "MarburgumII$Side*.exe" -File -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -First 1 -ExpandProperty FullName
}
function Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Dirty { if (git -C $repoRoot status --porcelain -- @inputs) { '1' } else { '0' } }
function StampValue([string]$Key) {
    (Get-Content -LiteralPath $stamp | Where-Object { $_ -like "$Key=*" } | Select-Object -First 1) -replace "^$Key=", ''
}

switch ($Command) {
    'write' {
        if (-not $Config) { [Console]::Error.WriteLine('Usage: build-stamp.ps1 write <config>'); exit 2 }
        $lines = @(
            "git_sha=$(git -C $repoRoot rev-parse HEAD)"
            "dirty=$(Dirty)"
            "config=$Config"
            "platform=Win64"
            "packaged_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        )
        foreach ($side in 'Server', 'Client') {
            $bin = Executable $side
            if (-not $bin) { [Console]::Error.WriteLine("build-stamp: no staged MarburgumII$side executable under $staged"); exit 1 }
            $lines += "sha256_$side=$(Sha $bin)"
        }
        $tmp = "$stamp.tmp"
        [IO.File]::WriteAllText($tmp, (($lines -join "`n") + "`n"))
        Move-Item -LiteralPath $tmp -Destination $stamp -Force
        Get-Content -LiteralPath $stamp
    }
    'check' {
        if (-not (Test-Path -LiteralPath $stamp)) {
            "build_fresh: NO (no $stamp; package with scripts\package-windows.ps1)"
            exit 3
        }
        Get-Content -LiteralPath $stamp | ForEach-Object { "build_$_" }
        $problems = @()
        foreach ($side in 'Server', 'Client') {
            $want = StampValue "sha256_$side"
            $bin = Executable $side
            if (-not $bin) { $problems += "$side executable missing" }
            elseif ((Sha $bin) -ne $want) { $problems += "$side executable changed since packaging" }
        }
        # A later commit that touches only docs or scripts leaves the build current; one that touches its inputs does not.
        $packagedSha = StampValue 'git_sha'
        if ($packagedSha -ne (git -C $repoRoot rev-parse HEAD)) {
            git -C $repoRoot cat-file -e "$packagedSha^{commit}" 2>$null
            if ($LASTEXITCODE) { $problems += 'packaged from an unknown commit' }
            else {
                git -C $repoRoot diff --quiet $packagedSha HEAD -- @inputs
                if ($LASTEXITCODE) { $problems += 'Source, Config, Content, Data or Plugins changed since the packaged commit' }
            }
        }
        if ((StampValue 'dirty') -ne '0') { $problems += 'packaged from a dirty tree' }
        if ((Dirty) -ne '0') { $problems += 'the tree has uncommitted changes since' }
        if ($problems) { "build_fresh: NO ($($problems -join ';'))"; exit 3 }
        'build_fresh: yes'
    }
    default { [Console]::Error.WriteLine('Usage: build-stamp.ps1 write <config> | check'); exit 2 }
}
