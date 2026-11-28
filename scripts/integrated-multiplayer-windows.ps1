# Windows twin of integrated-multiplayer-linux.sh: one current Win64 Client/Server pair, then the multiplayer regression.
#
#   scripts\integrated-multiplayer-windows.ps1 [--package] [smoke ...]
#
# --package builds, cooks and stages first (scripts\package-windows.ps1). Without it, a stale or missing
# staged tree is a failure: this script does not treat a source compile as a packaged result.
#
# A package stays current when later commits touch only docs or scripts (scripts\build-stamp.ps1). The
# identity file records both the packaged commit and HEAD, the executable hashes, and the cooked pak
# identity. services/accounts and tools/mb2 must match the packaged commit; a newer account service
# against an older game, or the reverse, fails closed.
#
# Default smokes are the acceptance set. Fault-injected net is separate from the clean net smoke.
# Ports are 281xx. Never 7777 or 17777.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
Set-Location -LiteralPath $repoRoot
# The bash twin defaults to its Linux engine path; on Windows UE_ROOT must be set (an empty one fails the engine check).
$ueRoot = [string]$env:UE_ROOT
$env:UE_ROOT = $ueRoot
$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

$cliArgs = @($args)
$doPackage = 0
if ($cliArgs.Count -gt 0 -and $cliArgs[0] -eq '--package') {
    $doPackage = 1
    $cliArgs = @($cliArgs | Select-Object -Skip 1)
}

$runId = Get-UtcStamp
$out = Join-Path $repoRoot "Saved\Integrated\$runId"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$matrix = Join-Path $out 'matrix.jsonl'
$summary = Join-Path $out 'SUMMARY.txt'
Write-SmokeText $matrix ''
Write-SmokeText $summary ''
$started = $null

# echo ... | tee -a <file>
function Write-Tee([string]$File, [object[]]$Lines) {
    $Lines | ForEach-Object { "$_" }
    Add-SmokeFile $File $Lines
}

# JSON string content: Windows paths carry backslashes, which JSON must escape.
function ConvertTo-JsonText([string]$Text) { ($Text -replace '\\', '\\') -replace '"', '\"' }

function Write-JsonLine([string]$Name, [string]$State, [string]$Exit, [string]$Started, [string]$Ended, [string]$Evidence, [string]$Port) {
    Write-Tee $matrix @(('{{"name":"{0}","status":"{1}","exit":{2},"started":"{3}","ended":"{4}","evidence":"{5}","port":"{6}"}}' -f
        $Name, $State, $Exit, $Started, $Ended, (ConvertTo-JsonText $Evidence), $Port))
}

# record <name> <status> <exit> <evidence> <port>
function Write-Record([string]$Name, [string]$State, [string]$Exit, [string]$Evidence, [string]$Port) {
    $ended = Get-UtcIso
    $from = $script:started; if (-not $from) { $from = $ended }
    Write-JsonLine $Name $State $Exit $from $ended $Evidence $Port
    $shown = $Evidence; if (-not $shown) { $shown = '-' }
    Write-Tee $summary @("$Name $State exit=$Exit evidence=$shown port=$Port")
}

# A PowerShell script as its own role with stdout and stderr in one log; returns @{ Code; Lines }.
function Invoke-ScriptCaptured([string]$Script, [string[]]$ScriptArgs = @(), [int]$WallSeconds = 0, [string]$Log) {
    $temp = $false
    if (-not $Log) { $Log = Join-Path $out ('capture-' + [guid]::NewGuid().ToString('N') + '.log'); $temp = $true }
    $code = Invoke-SmokeRole -Exe $powershellExe -Log $Log -WorkDir $repoRoot -WallSeconds $WallSeconds `
        -Arguments (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Script) + $ScriptArgs)
    $lines = @(Get-SmokeLogLines $Log)
    if ($temp) { Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue }
    @{ Code = $code; Lines = $lines }
}

$checkEngine = Join-Path $PSScriptRoot 'check-engine-version.ps1'

if ($doPackage -eq 1) {
    Write-Tee $summary @('Packaging Development Client and Server')
    $global:LASTEXITCODE = 0
    $engineOut = @(& $checkEngine $ueRoot)
    $engineCode = $LASTEXITCODE
    Write-Tee (Join-Path $out 'engine.txt') $engineOut
    if ($engineCode) {
        Write-Record 'package' 'BLOCKED' '2' '' ''
        Write-Tee $summary @('Result: BLOCKED — engine')
        exit 3
    }
    $packageLog = Join-Path $out 'package.log'
    $package = Invoke-ScriptCaptured (Join-Path $PSScriptRoot 'package-windows.ps1') @('Development') -Log $packageLog
    if ($package.Code -ne 0) {
        # The bash twin records "$?" here, which is the status of the negated test (0); kept for parity.
        Write-Record 'package' 'FAIL' '0' $packageLog ''
        Write-Tee $summary @('Result: FAIL — package')
        exit 1
    }
    Write-Record 'package' 'PASS' '0' $packageLog ''
}

$identity = Join-Path $out 'identity.txt'
Write-SmokeFile $identity (@(
    "integrated multiplayer $runId"
    "head=$((git rev-parse HEAD).Trim())"
    "head_subject=$(@(git log -1 --format=%s) -join "`n")"
    'dirty_porcelain<<'
) + @(git status --porcelain) + @(
    '>>'
    "ue_root=$ueRoot"
))
$engine = Invoke-ScriptCaptured $checkEngine @($ueRoot)
Add-SmokeFile $identity $engine.Lines
if ($engine.Code -ne 0) { Add-SmokeFile $identity @('engine_check=failed') }

$buildStamp = Join-Path $out 'build-stamp.txt'
$stamp = Invoke-ScriptCaptured (Join-Path $PSScriptRoot 'build-stamp.ps1') @('check') -Log $buildStamp
if ($stamp.Code -ne 0) {
    Write-Tee $summary $stamp.Lines
    Write-Record 'preflight' 'BLOCKED' '3' $buildStamp ''
    Write-Tee $summary @('Result: BLOCKED — staged build is missing or stale. Re-run with --package.')
    exit 3
}
Add-SmokeFile $identity $stamp.Lines
$packagedSha = (@($stamp.Lines | Where-Object { $_ -like 'build_git_sha=*' } | ForEach-Object { $_.Substring('build_git_sha='.Length) }) -join "`n")
Add-SmokeFile $identity (@('input_diff_since_package<<') +
    @(git diff --stat $packagedSha HEAD -- Source Config Content Data Plugins MarburgumII.uproject services/accounts tools/mb2) + @('>>'))

$schemaMatch = Get-SmokeLogLines (Join-Path $repoRoot 'Source\MarburgumIIShared\Public\MarburgumGameplayContract.h') |
    ForEach-Object { [regex]::Match($_, '.*WorldStoreSchemaVersion = ([0-9][0-9]*).*') } | Where-Object { $_.Success } | Select-Object -First 1
$schema = ''; if ($schemaMatch) { $schema = $schemaMatch.Groups[1].Value }
Write-Tee $identity @("world_store_schema=$schema")
if ($schema -ne '6') {
    Write-Record 'preflight' 'FAIL' '1' "world store schema is $schema, want 6" ''
    Write-Tee $summary @('Result: FAIL — world store schema')
    exit 1
}
git diff --quiet $packagedSha HEAD -- services/accounts tools/mb2
$toolingDiffers = $LASTEXITCODE -ne 0
if ($toolingDiffers -or @(git status --porcelain -- services/accounts tools/mb2).Count -gt 0) {
    Write-Record 'preflight' 'FAIL' '1' 'account service or mb2 differs from the packaged commit' ''
    Write-Tee $summary @('Result: FAIL — account tooling does not match the package')
    exit 1
}

Write-Tee $summary @('Hashing cooked content')
# find Saved/StagedBuilds -type f (*.pak|*.ucas|*.utoc) | sort | xargs sha256sum: "<sha256>  <relative path>".
$stagedRoot = Join-Path $repoRoot 'Saved\StagedBuilds'
$cooked = [string[]]@(Get-ChildItem -LiteralPath $stagedRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -eq '.pak' -or $_.Extension -eq '.ucas' -or $_.Extension -eq '.utoc' } |
    ForEach-Object { $_.FullName.Substring($repoRoot.Length).TrimStart('\') })
[Array]::Sort($cooked, [StringComparer]::Ordinal)
$cookedIdentity = Join-Path $out 'cooked-identity.txt'
if ($cooked.Count -gt 0) {
    Write-SmokeFile $cookedIdentity @($cooked | ForEach-Object { "$(Get-SmokeSha256 (Join-Path $repoRoot $_))  $_" })
} else {
    # xargs runs sha256sum once with no files, which hashes its empty standard input.
    Write-SmokeFile $cookedIdentity @('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  -')
}
$cookedLine = "$(Get-SmokeSha256 $cookedIdentity)  $cookedIdentity"
Add-SmokeFile $identity @($cookedLine)
Add-SmokeFile (Join-Path $out 'cooked-identity.sha') @($cookedLine)

# unit <name> <manifest> <timeout>
function Invoke-Unit([string]$Name, [string]$Manifest, [int]$Seconds) {
    $script:started = Get-UtcIso
    $log = Join-Path $out "$Name.log"
    $cargo = (Get-Command cargo -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    if (-not $cargo) { $cargo = 'cargo.exe' }
    $code = 127
    try {
        $code = Invoke-SmokeRole -Exe $cargo -Log $log -WorkDir $repoRoot -WallSeconds $Seconds `
            -Arguments @('test', '--manifest-path', (Join-Path $repoRoot $Manifest))
    } catch {
        Add-SmokeFile $log @("cannot start cargo: $($_.Exception.Message)")
    }
    if ($code -eq 0) {
        Write-Record $Name 'PASS' '0' $log ''
    } else {
        Write-Record $Name 'FAIL' "$code" $log ''
    }
}
Invoke-Unit 'accounts-unit' 'services\accounts\Cargo.toml' 900
Invoke-Unit 'mb2-unit' 'tools\mb2\Cargo.toml' 900

$ports = @{
    'encryption-negative' = 28170
    'street-day' = 28153
    'street-night' = 28154
    'account' = 28114
    'door' = 28120
    'story' = 28131
    'story-sl' = 28132
    'mission' = 28116
    'shop' = 28113
    'combat' = 28110
    'vehicle' = 28109
    'chat' = 28102
    'net' = 28101
    'net-fault' = 28180
    'interaction' = 28105
    'lent' = 28106
    'weather' = 28111
    'condition' = 28107
    'swim' = 28108
}

$defaultSuite = @(
    'encryption-negative', 'street-day', 'street-night', 'account', 'door', 'story', 'story-sl',
    'mission', 'shop', 'combat', 'vehicle', 'chat', 'net', 'net-fault', 'interaction', 'lent', 'weather', 'condition', 'swim'
)
$suite = $cliArgs
if ($suite.Count -eq 0) { $suite = $defaultSuite }

# run_smoke <name> <seconds> <env> <script> <args...>: the smoke's environment applies to that run only.
function Invoke-Smoke([string]$Name, [int]$Seconds, [hashtable]$Environment, [string]$Script, [string[]]$ScriptArgs) {
    $port = ''
    if ($ports.ContainsKey($Name)) { $port = [string]$ports[$Name] }
    if ($port -eq '7777' -or $port -eq '17777') {
        Write-Record $Name 'FAIL' '2' 'refused playtest port' $port
        return
    }
    if ($port -and (Test-UdpPortInUse ([int]$port))) {
        Write-Record $Name 'BLOCKED' '1' "port $port is in use" $port
        return
    }
    $script:started = Get-UtcIso
    $log = Join-Path $out "$Name.out"
    $saved = @{}
    foreach ($k in $Environment.Keys) {
        $saved[$k] = [Environment]::GetEnvironmentVariable($k, 'Process')
        [Environment]::SetEnvironmentVariable($k, [string]$Environment[$k], 'Process')
    }
    try {
        $run = Invoke-ScriptCaptured (Join-Path $PSScriptRoot $Script) $ScriptArgs $Seconds $log
    } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process') }
    }
    $code = $run.Code
    $evidence = ''
    foreach ($line in $run.Lines) {
        $m = [regex]::Match($line, 'Saved[\\/][A-Za-z0-9]+[\\/][0-9]{8}T[0-9]{6}Z')
        if ($m.Success) { $evidence = $m.Value; break }
    }
    $state = 'FAIL'
    if ($code -eq 0) {
        $state = 'PASS'
    } elseif ($code -eq 124 -or $code -eq 137) {
        $state = 'FAIL'
    }
    if (-not $evidence) { $evidence = $log }
    Write-Record $Name $state "$code" $evidence $port
    Start-Sleep -Seconds 5
}

foreach ($name in $suite) {
    switch -CaseSensitive ($name) {
        'encryption-negative' {
            Invoke-Smoke $name 900 @{ MB2_ENC_NEG_PORT = $ports[$name] } 'encryption-negative-windows.ps1' @('staged')
        }
        'street-day' {
            Invoke-Smoke $name 2400 @{ MB2_CROWD_PORT = $ports[$name] } 'street-multiplayer-smoke-windows.ps1' @('staged', 'day')
        }
        'street-night' {
            Invoke-Smoke $name 1200 @{ MB2_CROWD_PORT = $ports[$name] } 'street-multiplayer-smoke-windows.ps1' @('staged', 'night')
        }
        'account' {
            Invoke-Smoke $name 4200 @{ MB2_ACCOUNT_PORT = $ports[$name]; MB2_ACCOUNT_SERVICE_PORT = 28115 } 'account-smoke-windows.ps1' @('staged')
        }
        'door' {
            Invoke-Smoke $name 4200 @{ MB2_DOOR_PORT = $ports[$name] } 'door-smoke-windows.ps1' @('staged')
        }
        'story' {
            Invoke-Smoke $name 3600 @{ MB2_STORY_PORT = $ports[$name] } 'story-smoke-windows.ps1' @('staged')
        }
        'story-sl' {
            Invoke-Smoke $name 3600 @{ MB2_STORY_PORT = $ports[$name]; MB2_STORY_CLIENT_ARGS = '-culture=sl' } 'story-smoke-windows.ps1' @('staged')
        }
        'mission' {
            Invoke-Smoke $name 2400 @{ MB2_MISSION_PORT = $ports[$name] } 'mission-smoke-windows.ps1' @('staged')
        }
        'shop' {
            Invoke-Smoke $name 2400 @{ MB2_SHOP_PORT = $ports[$name] } 'shop-smoke-windows.ps1' @('staged')
        }
        'combat' {
            Invoke-Smoke $name 2400 @{ MB2_COMBAT_PORT = $ports[$name] } 'combat-smoke-windows.ps1' @('staged')
        }
        'vehicle' {
            Invoke-Smoke $name 2400 @{ MB2_VEHICLE_PORT = $ports[$name] } 'vehicle-smoke-windows.ps1' @('staged')
        }
        'chat' {
            Invoke-Smoke $name 1800 @{ MB2_CHAT_PORT = $ports[$name] } 'chat-smoke-windows.ps1' @('staged')
        }
        'net' {
            Invoke-Smoke $name 1800 @{ MB2_SMOKE_PORT = $ports[$name] } 'net-smoke-windows.ps1' @('staged')
        }
        'net-fault' {
            Invoke-Smoke $name 1800 @{ MB2_SMOKE_PORT = $ports[$name]; MB2_SMOKE_EXTRA = '-PktLag=120 -PktLagVariance=40 -PktLoss=1' } 'net-smoke-windows.ps1' @('staged')
        }
        'interaction' {
            Invoke-Smoke $name 2400 @{ MB2_INTERACT_PORT = $ports[$name] } 'interaction-smoke-windows.ps1' @('staged')
        }
        'lent' {
            Invoke-Smoke $name 2400 @{ MB2_LENT_PORT = $ports[$name] } 'lent-smoke-windows.ps1' @('staged')
        }
        'weather' {
            Invoke-Smoke $name 2400 @{ MB2_WEATHER_PORT = $ports[$name] } 'weather-smoke-windows.ps1' @('staged')
        }
        'condition' {
            Invoke-Smoke $name 1800 @{ MB2_CONDITION_PORT = $ports[$name] } 'condition-smoke-windows.ps1' @('staged')
        }
        'swim' {
            Invoke-Smoke $name 1800 @{ MB2_SWIM_PORT = $ports[$name] } 'swim-smoke-windows.ps1' @('staged')
        }
        default {
            Write-Record $name 'FAIL' '2' 'unknown smoke' ''
        }
    }
}

$fails = @(Get-SmokeLogLines $matrix | Where-Object { $_.Contains('"status":"FAIL"') }).Count
$blocked = @(Get-SmokeLogLines $matrix | Where-Object { $_.Contains('"status":"BLOCKED"') }).Count
Write-Tee $summary @("fails=$fails blocked=$blocked")
$result = 'FAIL'; if ($fails -eq 0 -and $blocked -eq 0) { $result = 'PASS' }
Write-Tee $summary @("Result: $result — $out")
if ($fails -eq 0 -and $blocked -eq 0) { exit 0 }
exit 1
