# CHAT-001 three-client qualification: one dedicated server, three separate client processes. Windows twin of
# chat-smoke-linux.sh (same roles, evidence lines and expectations).
#
#   A sends general chat, a private message to B, a private message to a nonexistent ID, invalid
#     text and a two-message burst, then an end marker and one operation submitted twice.
#   B replies privately to A.
#   C is the bystander: it must receive each general message once and nothing private.
#
# A and C render offscreen and save a screenshot of their chat panel; B runs without rendering.
# The script also checks that no private body reaches the server log or any client log.
#
# Usage: scripts\chat-smoke-windows.ps1 [staged|editor]
# Evidence is written to Saved\ChatSmoke\<UTC timestamp>\.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27702; if ($env:MB2_CHAT_PORT) { $port = [int]$env:MB2_CHAT_PORT }
$map = '/Game/Maps/NetSmoke'
$token = "t$(Get-UnixSeconds)$(Get-Random -Maximum 32768)"
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\chat-smoke-windows.ps1 [staged|editor]'
$outDir = New-SmokeOutDir 'ChatSmoke'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs with a wall-clock limit, which stops its whole job.
$wall = Get-ClientWallSeconds

# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080')

# grep -ah PATTERN | sed 's/^.*\(A\|B\)/\1/': matching lines cut to start at the last of several evidence prefixes.
function Get-SummaryLines([string[]]$Path, [string]$Pattern, [string]$Cut) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $cutRegex = New-Object System.Text.RegularExpressions.Regex("^.*($Cut)")
    foreach ($line in @(Get-SmokeLogLines $Path)) {
        if ($regex.IsMatch($line)) { $cutRegex.Replace($line, '$1', 1) }
    }
}

function Start-Client([string]$Role, [string[]]$Display) {
    Start-SmokeRole -Target $targets.Client -WallSeconds $wall -Log (Join-Path $outDir "client-$Role.log") `
        -Arguments (@("127.0.0.1:$port") + $commonArgs + $Display + @('-MB2ChatSmoke', "-MB2ChatRole=$Role", "-MB2ChatToken=$token", '-MB2ChatTimeout=150'))
}

# MB2_CHAT_RENDER lists the roles that render and screenshot their view (default: A and C).
$renderRoles = 'A C'; if ($env:MB2_CHAT_RENDER) { $renderRoles = $env:MB2_CHAT_RENDER }
$renderRoleList = @($renderRoles -split '\s+' | Where-Object { $_ })
function Get-DisplayArgs([string]$Role, [string]$Label) {
    if ($renderRoleList -contains $Role) {
        $renderArgs + @("-MB2ChatShot=$(Join-Path $outDir "chat-$Role-$Label-1920x1080.png")")
    } else {
        @('-nullrhi')
    }
}

$server = $null
$status = 0
try {
    "Starting dedicated server ($Mode) on port $port; evidence in $outDir"
    $serverLog = Join-Path $outDir 'server.log'
    $server = Start-SmokeServer -Target $targets.Server -Arguments (@($map, "-port=$port") + $commonArgs) `
        -Log $serverLog -Port $port -PortVariable 'MB2_CHAT_PORT' -ReadySeconds 120

    $clients = @{}
    $clients['B'] = Start-Client 'B' @(Get-DisplayArgs 'B' 'recipient')
    $clients['C'] = Start-Client 'C' @(Get-DisplayArgs 'C' 'bystander')
    $clients['A'] = Start-Client 'A' @(Get-DisplayArgs 'A' 'sender')

    foreach ($role in 'A', 'B', 'C') {
        $code = Wait-SmokeRole $clients[$role]
        $clients[$role].Dispose()
        $clientLog = Join-Path $outDir "client-$role.log"
        if ($code -eq 0 -and (Test-SmokeLog $clientLog "MB2CHAT role=client tag=$role event=pass")) {
            "  client ${role}: PASS"
        } else {
            "  client ${role}: FAIL (exit $code)"
            Get-SmokeLogMatches $clientLog "MB2CHAT role=client tag=$role event=fail" 'MB2CHAT' '    '
            $status = 1
        }
    }

    Start-Sleep -Seconds 1
} finally {
    Stop-SmokeRole $server
    $server = $null
}

# Five messages are accepted: G1, P1, R1, BURST1 and END. Refusals are not "accepted".
$accepted = Get-SmokeLogCount $serverLog 'Marburgum chat accepted'
"Server: accepted=$accepted (want 5)"
if ($accepted -ne 5) { $status = 1 }

# Privacy: no private body anywhere in server or client logs.
$allLogs = Join-Path $outDir '*.log'
$leakFiles = @(Resolve-SmokePaths $allLogs | Where-Object { Test-SmokeLog $_ "private-$token|reply-$token|ghost-$token" })
$leaks = $leakFiles -join "`n"
if ($leaks) {
    "  private text found in logs: $leaks"
    $status = 1
} else {
    '  private bodies in logs: none'
}
$leaksText = 'none'; if ($leaks) { $leaksText = $leaks }

$summary = Join-Path $outDir 'summary.txt'
Write-SmokeFile $summary (@(
    'MarburgumII CHAT-001 three-client chat smoke'
    "result: $(Get-PassFail $status)"
    "date_utc: $(Get-UtcIso)"
    "git_sha: $(Get-SmokeGitSha @('Source', 'Config', 'Content'))"
    "mode: $Mode"
    "host: $(Get-SmokeHost)"
    "server_accepted_messages: $accepted"
    "private_bodies_in_logs: $leaksText"
    ''
) + @(Get-SummaryLines $allLogs 'MB2CHAT|MB2SMOKE role=server event=(ready|login|logout)|Marburgum chat accepted' 'MB2CHAT|MB2SMOKE|Marburgum chat accepted'))
# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
Add-BuildStampCheck $summary $Mode

"Result: $(Get-PassFail $status) - $summary"
exit $status
