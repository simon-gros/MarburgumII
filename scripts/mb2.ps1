# Run the Rust repository tool (tools/mb2), building it first when its sources changed. Windows twin of mb2.sh.
# Usage: scripts\mb2.ps1 <command> ...   (no arguments lists the commands)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $repoRoot 'tools\mb2\Cargo.toml'

if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('Rust (cargo) is required for the repository tooling; install stable Rust (rustup, MSVC host).')
    exit 2
}
cargo build --quiet --release --manifest-path $manifest
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& (Join-Path $repoRoot 'tools\mb2\target\release\mb2.exe') @args
exit $LASTEXITCODE
