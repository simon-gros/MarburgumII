# Build, cook and stage the separate Win64 dedicated server and client. Windows twin of package-linux.sh.
# Output: Saved\StagedBuilds\WindowsServer and Saved\StagedBuilds\WindowsClient. Needs a source-built engine.
param(
    [ValidateSet('Development', 'Shipping')]
    [string]$Config = 'Development'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$ueRoot = $env:UE_ROOT

if (-not $ueRoot) {
    [Console]::Error.WriteLine('Set UE_ROOT to your Unreal Engine 5.8.4 installation root.')
    exit 2
}
& (Join-Path $PSScriptRoot 'check-engine-version.ps1') $ueRoot
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# -WaitForUATMutex: one UAT per engine install; another session's packaging is waited for, not failed on.
& (Join-Path $ueRoot 'Engine\Build\BatchFiles\RunUAT.bat') -WaitForUATMutex BuildCookRun `
    "-project=$(Join-Path $repoRoot 'MarburgumII.uproject')" `
    -noP4 -unattended -utf8output `
    -platform=Win64 -client "-clientconfig=$Config" `
    -server -serverplatform=Win64 "-serverconfig=$Config" `
    -build -cook -stage -pak `
    '-map=/Game/Maps/FrontEnd+/Game/Maps/NetSmoke+/Game/Maps/Lent+/Game/World/lent' `
    -ubtargs=-WaitMutex
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# The build stamp: which commit the staged trees came from and the digests of the two executables.
& (Join-Path $PSScriptRoot 'build-stamp.ps1') write $Config
exit $LASTEXITCODE
