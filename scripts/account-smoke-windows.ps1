# Windows twin of account-smoke-linux.sh (same phases, evidence lines and expectations).
# Accounts, the first character and world admission (ADR-055), end to end.
#
# Starts the account service (services\accounts) over HTTPS with a throwaway development CA, and a dedicated
# server that requires accounts. Then:
#   FIRST     a rendered client on the real screens: register, a wrong code, the code from the outbox, two
#             passwords that differ, username and password, every body in the creator's 3D preview (and one
#             turn), a refused name, the character (Špela Kovačič, Quinn); Connect enters the world as her,
#             wearing Quinn.
#   RETURN    a second rendered client: a wrong password, then the right one; no creator; the same character.
#   Admission, with a second account made through the HTTP API (curl):
#     a development key, no ticket and a forged ticket are refused;
#     a real ticket is admitted (ADMITTED, held in the world), the same ticket again is refused (single use),
#     and a second ticket for the same character is refused while the first is in the world.
#   Tickets name their server (QA 2026-11-19, AR1): the service lists this server and a second one (melje, own key,
#     not running). No ticket for an unlisted address or without one; melje's key cannot spend (or burn) this
#     server's ticket, which is then admitted; this server refuses a ticket asked for melje.
#   Encrypted transport (ADR-083): the ticket is the connection's encryption token and its key comes with it from
#     the service. A ticket stolen off the wire and redeemed first, without its key, admits nobody, and the player
#     then joins with that same ticket (ADR-088: its server may redeem a ticket up to three times). The client joins through a
#     recording UDP relay (mb2 wire-relay): the ticket shows in its hello, and nothing after it carries the map's
#     name, the game mode or the character's name in the clear. No server log holds a ticket or a key, and the
#     rendered clients never log the encryption token at all (AR13).
#   The service restarts and the returning player still signs in (durable store).
#   Secrets: no password, code, session, ticket or key appears in the account store; no password, session or
#   verification code in any game log.
#
# Usage: scripts\account-smoke-windows.ps1 [editor|staged]. Evidence: Saved\AccountSmoke\<UTC timestamp>\.
param([string]$Mode = 'editor')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 27714; if ($env:MB2_ACCOUNT_PORT) { $port = [int]$env:MB2_ACCOUNT_PORT }
$servicePort = 27715; if ($env:MB2_ACCOUNT_SERVICE_PORT) { $servicePort = [int]$env:MB2_ACCOUNT_SERVICE_PORT }
$map = '/Game/World/lent'
$targets = Resolve-SmokeTargets $Mode 'Usage: scripts\account-smoke-windows.ps1 [editor|staged]'
$outDir = New-SmokeOutDir 'AccountSmoke'
# A client stuck before its in-engine timeout starts (map load, a crash handler) must not hang the script: each one
# runs under a wall-clock limit that stops its whole job.
$wall = Get-ClientWallSeconds

# ---------------------------------------------------------------- script-local helpers
function New-RandomBytes([int]$Count) {
    $bytes = New-Object byte[] $Count
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($bytes); $rng.Dispose()
    , $bytes
}
# head -c N /dev/urandom | base64 | tr -d '\n/+='
function New-RandomKey([int]$Count) { [Convert]::ToBase64String((New-RandomBytes $Count)) -replace '[/+=]', '' }
# head -c 32 /dev/urandom | base64 | tr '+/' '-_' | tr -d '='
function New-RandomB64 { (([Convert]::ToBase64String((New-RandomBytes 32)) -replace '\+', '-') -replace '/', '_') -replace '=', '' }

# chmod 700: only the current user may read the secrets directory (files created in it inherit this).
function Protect-SmokeSecrets([string]$Dir) {
    $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $Dir /inheritance:r /grant:r "*${sid}:(OI)(CI)F" | Out-Null
    if ($LASTEXITCODE) { Exit-Failure "Could not restrict access to $Dir (icacls exit $LASTEXITCODE)" }
}

# curl.exe with a JSON body from a file (Windows PowerShell 5.1 mangles quotes in native arguments). Schannel cannot
# check revocation for the throwaway CA, so every call passes --ssl-no-revoke next to --cacert.
function Invoke-CurlWithBody([string[]]$CurlArgs, [string]$Body, [string]$Url) {
    $bodyFile = Join-Path $secrets ('curl-body-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        [IO.File]::WriteAllText($bodyFile, $Body, (New-Object System.Text.UTF8Encoding($false)))
        $answer = & curl.exe @CurlArgs --data-binary "@$bodyFile" $Url
        @($answer) -join "`n"
    } finally {
        Remove-Item -LiteralPath $bodyFile -Force -ErrorAction SilentlyContinue
    }
}

$serviceBin = $null
$serviceUrl = "https://127.0.0.1:$servicePort"
$service = $null
$server = $null
$relay = $null
$status = 0

function Start-AccountService([string]$Log) {
    $script:service = Start-SmokeRole -Exe $serviceBin -Log $Log -WorkDir $repoRoot -Arguments @(
        '--bind', "127.0.0.1:$servicePort", '--store', (Join-Path $outDir 'accounts.json'), '--outbox', (Join-Path $outDir 'outbox'),
        '--models', (Join-Path $repoRoot 'Data\Accounts\character_models.v0.1.json'), '--servers', (Join-Path $secrets 'servers.json'),
        '--tls-cert', (Join-Path $outDir 'service.pem'), '--tls-key', (Join-Path $secrets 'service.key'))
    for ($i = 0; $i -lt 40; $i++) {
        & curl.exe -sf --ssl-no-revoke --cacert $caPem -o NUL "$serviceUrl/v1/health" | Out-Null
        if ($LASTEXITCODE -eq 0) { return }
        Start-Sleep -Milliseconds 250
    }
    Exit-Failure "The account service did not start; see $Log"
}

function Stop-Relay { Stop-SmokeRole $script:relay; $script:relay = $null }
function Stop-AccountService { Stop-SmokeRole $script:service; $script:service = $null }
function Stop-GameServer { Stop-SmokeRole $script:server; $script:server = $null }

# api <method> <path> <json> [bearer]
function Invoke-Api([string]$Method, [string]$Path, [string]$Json, [string]$Bearer) {
    $curlArgs = @('-s', '--ssl-no-revoke', '--cacert', $caPem, '-X', $Method, '-H', 'Content-Type: application/json')
    if ($Bearer) { $curlArgs += @('-H', "Authorization: Bearer $Bearer") }
    Invoke-CurlWithBody $curlArgs $Json "$serviceUrl$Path"
}

# redeem_as <server key file> <ticket>: the HTTP status of a redeem by that server
function Get-RedeemStatus([string]$KeyFile, [string]$Ticket) {
    $serverKey = ([IO.File]::ReadAllText($KeyFile)).TrimEnd("`r", "`n")
    Invoke-CurlWithBody @('-s', '-o', 'NUL', '-w', '%{http_code}', '--ssl-no-revoke', '--cacert', $caPem, '-X', 'POST',
        '-H', 'Content-Type: application/json', '-H', "Authorization: MB2-Server $serverKey") "{`"ticket`":`"$Ticket`"}" "$serviceUrl/v1/server/redeem"
}

# expect_eq <what> <got> <want>
function Test-Equal([string]$What, [string]$Got, [string]$Want) {
    if ($Got -ceq $Want) {
        "  ${What}: yes"
    } else {
        "  ${What}: NO (got '$Got', want '$Want')"
        $script:status = 1
    }
}

# json <key>: the first string field of that name
function Get-JsonField([string]$Text, [string]$Key) {
    $m = [regex]::Match([string]$Text, "`"$Key`":`"([^`"]*)`"")
    if ($m.Success) { $m.Groups[1].Value } else { '' }
}

# the code in the newest outbox message to $1
function Get-CodeFor([string]$Address) {
    $file = @(Get-ChildItem -Path (Join-Path $outDir 'outbox\*.txt') -File -ErrorAction SilentlyContinue |
        Where-Object { Test-SmokeLog $_.FullName "^To: $Address`$" } | ForEach-Object { $_.FullName } | Sort-Object) |
        Select-Object -Last 1
    if (-not $file) { return '' }
    @(Get-SmokeLogMatches $file '^\s+[A-Z2-9]{8}\s*$' '' | ForEach-Object { $_ -replace ' ', '' }) -join "`n"
}

# The process arguments of a running role (ps -o args=), or the command line it was started with.
function Get-ProcessArgs($Proc) {
    $line = $null
    try { $line = (Get-CimInstance Win32_Process -Filter "ProcessId=$($Proc.Pid)" -ErrorAction Stop).CommandLine } catch { }
    if (-not $line) { $line = $Proc.CommandLine }
    $line
}

# ---------------------------------------------------------------- the account service
$secrets = Join-Path $outDir 'secrets'
New-Item -ItemType Directory -Force -Path $secrets | Out-Null
Protect-SmokeSecrets $secrets
$certs = New-SmokeCertificates -OutDir $outDir -SecretsDir $secrets -CaName 'MB2 smoke CA' -Name 'service' -CertDir $outDir
$caPem = $certs.Ca
Write-SmokeText (Join-Path $secrets 'server.key') (New-RandomKey 36)
Write-SmokeText (Join-Path $secrets 'melje.key') (New-RandomKey 36)
$meljeAddress = "127.0.0.1:$($port + 2)"
Write-SmokeText (Join-Path $secrets 'servers.json') (
    "{ `"servers`": [`n" +
    "    { `"id`": `"lent-smoke`", `"addresses`": [`"127.0.0.1:$port`", `"localhost:$port`"], `"key_file`": `"server.key`" },`n" +
    "    { `"id`": `"melje`", `"addresses`": [`"$meljeAddress`"], `"key_file`": `"melje.key`" } ] }`n")

cargo build -q --release --manifest-path (Join-Path $repoRoot 'services\accounts\Cargo.toml')
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$serviceBin = Join-Path $repoRoot 'services\accounts\target\release\mb2-accounts.exe'

$tlsArgs = @("-ini:Engine:[SSL]:OverrideCertificateBundlePath=$caPem")
$accountsArgs = @("-MB2Accounts=$serviceUrl") + $tlsArgs
# -RenderOffScreen on every client, shot or not, so a client never opens a window on the owner's screen. The server
# ignores it.
$commonArgs = @(Get-CommonArgs)
# MB2_ACCOUNT_RES=WxH and MB2_ACCOUNT_UI_SCALE=f render the screenshot clients at another size and interface scale
# (QA U9: the account and creator screens at 150 % on 5:4).
$accountRes = '1920x1080'; if ($env:MB2_ACCOUNT_RES) { $accountRes = $env:MB2_ACCOUNT_RES }
$renderArgs = @('-RenderOffScreen', '-windowed', '-ForceRes', "-ResX=$($accountRes -replace 'x[^x]*$', '')", "-ResY=$($accountRes -replace '^[^x]*x', '')")
if ($env:MB2_ACCOUNT_UI_SCALE) { $renderArgs += "-MB2UIScale=$env:MB2_ACCOUNT_UI_SCALE" }
$serverLog = Join-Path $outDir 'server.log'

function Start-GameServer([string]$Log) {
    $script:server = Start-SmokeServer -Target $targets.Server -Log $Log -Port $port -PortVariable 'MB2_ACCOUNT_PORT' -ReadySeconds 180 `
        -Arguments (@($map, "-port=$port", "-MB2WorldStore=$(Join-Path $outDir 'world-store.json')") + $accountsArgs +
            @("-MB2AccountsKeyFile=$(Join-Path $secrets 'server.key')") + $commonArgs)
}

# client <label> <role> <join url or ""> <args...>; output to client-<label>.log. Start-AccountClient starts one
# (as bash's "client ... &"), Complete-AccountClient waits for it, retries a GPU start failure and reports.
function Start-AccountClient([string]$Label, [string]$AccountRole, [string]$Url, [string[]]$More = @()) {
    $log = Join-Path $outDir "client-$Label.log"
    $arguments = @()
    if ($Url) { $arguments += $Url }
    $arguments += $commonArgs + $More + @('-MB2AccountSmoke', "-MB2AccountRole=$AccountRole")
    $proc = Start-SmokeRole -Target $targets.Client -Arguments $arguments -Log $log -WallSeconds $wall
    Start-Sleep -Milliseconds 400
    Write-SmokeFile (Join-Path $outDir "cmdline-$Label.txt") @("pid=$($proc.Pid)", (Get-ProcessArgs $proc))
    @{ Label = $Label; AccountRole = $AccountRole; Url = $Url; More = $More; Log = $log; Proc = $proc }
}

# -Detached: the client ran in the background (a bash subshell), so its FAIL does not change the status.
function Complete-AccountClient($State, [switch]$Detached) {
    $code = Wait-SmokeRole $State.Proc; $State.Proc.Dispose()
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        if (-not ($code -ne 0 -and (Test-GpuInitFailure $State.Log))) { break }
        Write-Warn "  client $($State.Label): GPU device creation failed (exit $code); retrying"
        Move-Item -LiteralPath $State.Log -Destination (Join-Path $outDir "client-$($State.Label).gpu-init-failed-$attempt.log") -Force
        Start-Sleep -Seconds 3
        $next = Start-AccountClient $State.Label $State.AccountRole $State.Url $State.More
        $code = Wait-SmokeRole $next.Proc; $next.Proc.Dispose()
    }
    if ($code -eq 0 -and (Test-SmokeLog $State.Log "MB2ACCOUNT role=client tag=$($State.AccountRole) event=pass")) {
        "  client $($State.Label): PASS"
    } else {
        "  client $($State.Label): FAIL (exit $code)"
        Get-SmokeLogMatches $State.Log "MB2ACCOUNT role=client tag=$($State.AccountRole) event=(fail|expectation_failed)" 'MB2ACCOUNT' '    '
        if (-not $Detached) { $script:status = 1 }
    }
}

function Invoke-AccountClient([string]$Label, [string]$AccountRole, [string]$Url, [string[]]$More = @()) {
    Complete-AccountClient (Start-AccountClient $Label $AccountRole $Url $More)
}

# expect_server <what> <pattern>
function Test-Server([string]$What, [string]$Pattern) {
    if (Test-SmokeLog $serverLog $Pattern) {
        "  server ${What}: yes"
    } else {
        "  server ${What}: NO (no '$Pattern' in server.log)"
        $script:status = 1
    }
}

# A role for scripts\mb2.ps1 (mb2.sh ... > log 2>&1 &).
function Start-Mb2Role([string[]]$Mb2Args, [string]$Log) {
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    Start-SmokeRole -Exe $ps -Log $Log -WorkDir $repoRoot `
        -Arguments (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'mb2.ps1')) + $Mb2Args)
}

function Get-CharacterId([string]$Log, [string]$Tag) {
    $re = [regex]"tag=$Tag event=character_ready character=c_[0-9a-f]*"
    $ids = foreach ($line in @(Get-SmokeLogLines $Log)) { foreach ($m in $re.Matches($line)) { $m.Value -replace '.*character=', '' } }
    @($ids) -join "`n"
}

function ConvertTo-Count([string]$Value) {
    $first = @(([string]$Value) -split "`n")[0]
    if ($first -match '^[0-9]+$') { [int]$first } else { 0 }
}

$playerPassword = Join-Path $secrets 'player.password'
Write-SmokeText $playerPassword 'drava-bridge-1990'
$player = @('-MB2AccountEmail=spela.kovacic@example.si', '-MB2AccountUser=spela_k', "-MB2AccountPasswordFile=$playerPassword",
    '-MB2AccountFirst=Špela', '-MB2AccountLast=Kovačič', '-MB2AccountModel=quinn')

try {
    "Account service on $serviceUrl, dedicated server ($Mode) on $map port $port; evidence in $outDir"
    Start-AccountService (Join-Path $outDir 'service-1.log')
    Start-GameServer $serverLog

    'First launch and returning player (rendered)'
    Invoke-AccountClient 'first' 'FIRST' '/Game/Maps/FrontEnd' ($accountsArgs + @("-MB2Server=127.0.0.1:$port") + $renderArgs +
        @("-MB2AccountShot=$(Join-Path $outDir 'first')", "-MB2AccountOutbox=$(Join-Path $outDir 'outbox')", '-MB2AccountTimeout=150') + $player)
    Test-Server 'admitted the new character' 'MB2ACCOUNT role=server event=admitted .* model=quinn'
    Invoke-AccountClient 'return' 'RETURN' '/Game/Maps/FrontEnd' ($accountsArgs + @("-MB2Server=127.0.0.1:$port") + $renderArgs +
        @("-MB2AccountShot=$(Join-Path $outDir 'return')", '-MB2AccountTimeout=150') + $player)
    $firstId = Get-CharacterId (Join-Path $outDir 'client-first.log') 'FIRST'
    $returnId = Get-CharacterId (Join-Path $outDir 'client-return.log') 'RETURN'
    if ($firstId -and $firstId -ceq $returnId) {
        "  same character on return: yes ($firstId)"
    } else {
        "  same character on return: NO (first=$firstId return=$returnId)"
        $status = 1
    }

    'Admission'
    [void](Invoke-Api 'POST' '/v1/register' '{"email":"bor@example.si"}')
    $ticket = Get-JsonField (Invoke-Api 'POST' '/v1/verify' "{`"email`":`"bor@example.si`",`"code`":`"$(Get-CodeFor 'bor@example.si')`"}") 'ticket'
    [void](Invoke-Api 'POST' '/v1/setup' "{`"ticket`":`"$ticket`",`"username`":`"bor_m`",`"password`":`"pohorje-snow-1991`"}")
    $session = Get-JsonField (Invoke-Api 'POST' '/v1/login' '{"username":"bor_m","password":"pohorje-snow-1991"}') 'session'
    [void](Invoke-Api 'POST' '/v1/character' '{"request":"r1","first_name":"Bor","last_name":"Mlakar","model":"manny"}' $session)
    # admit <server address>: ask for a ticket; returns its ticket and key.
    function Get-Admission([string]$Address) {
        $answer = Invoke-Api 'POST' '/v1/admission' "{`"server`":`"$Address`"}" $session
        @{ Ticket = (Get-JsonField $answer 'ticket'); Key = (Get-JsonField $answer 'key') }
    }
    $forged = New-RandomB64
    $nullrhi = @('-nullrhi', '-MB2AccountTimeout=60')
    Invoke-AccountClient 'dev-key' 'REFUSED' "127.0.0.1:${port}?MB2Character=dev-key-player" $nullrhi
    Invoke-AccountClient 'no-ticket' 'REFUSED' "127.0.0.1:$port" $nullrhi
    $noTicket = Get-SmokeLogCount $serverLog 'admission_refused stage=hello reason=no_ticket'
    $noTicketOk = 'yes'; if ($noTicket -lt 2) { $noTicketOk = "no ($noTicket)" }
    Test-Equal 'refused a development key and a join without a ticket at the hello' $noTicketOk 'yes'
    Invoke-AccountClient 'forged' 'REFUSED' "127.0.0.1:${port}?EncryptionToken=$forged" ($nullrhi + @("-MB2AdmissionKey=$(New-RandomB64)"))
    Test-Server 'refused a forged ticket' 'admission_refused stage=redeem reason="Your sign-in for this world has expired'
    'Tickets name their server (AR1)'
    Test-Equal 'no ticket without a server' (Get-JsonField (Invoke-Api 'POST' '/v1/admission' '{}' $session) 'error') 'request_invalid'
    Test-Equal 'no ticket for an unlisted address' (Get-JsonField (Invoke-Api 'POST' '/v1/admission' '{"server":"evil.example.si:7777"}' $session) 'error') 'server_unknown'
    Test-Equal 'no ticket for another port' (Get-JsonField (Invoke-Api 'POST' '/v1/admission' "{`"server`":`"127.0.0.1:$($port + 1)`"}" $session) 'error') 'server_unknown'
    $melje = Get-Admission $meljeAddress
    Invoke-AccountClient 'other-server' 'REFUSED' "127.0.0.1:${port}?EncryptionToken=$($melje.Ticket)" ($nullrhi + @("-MB2AdmissionKey=$($melje.Key)"))
    Test-Equal 'melje can spend its own ticket after this server refused it' (Get-RedeemStatus (Join-Path $secrets 'melje.key') $melje.Ticket) '200'
    'Encrypted transport (ADR-083)'
    # A ticket stolen off the wire (it is in the clear in the hello) and redeemed first, without its key: nobody is
    # admitted, and the same ticket still admits its player afterwards (ADR-088). A ticket lives one minute, so the thief
    # runs in the background and the player follows as soon as the server has redeemed it.
    $t1 = Get-Admission "localhost:$port"
    $ticket1 = $t1.Ticket
    Test-Equal "melje cannot spend this server's ticket" (Get-RedeemStatus (Join-Path $secrets 'melje.key') $ticket1) '403'
    $redeemPattern = 'MB2ACCOUNT role=server event=ticket_redeemed .* model=manny encrypted=1'
    $redeemedBefore = Get-SmokeLogCount $serverLog $redeemPattern
    $stolen = Start-AccountClient 'stolen' 'REFUSED' "127.0.0.1:${port}?EncryptionToken=$ticket1" @('-nullrhi', '-MB2AccountTimeout=120', "-MB2AdmissionKey=$(New-RandomB64)")
    for ($i = 0; $i -lt 240; $i++) {
        if ((Get-SmokeLogCount $serverLog $redeemPattern) -gt $redeemedBefore) { break }
        Start-Sleep -Milliseconds 250
    }
    Test-Server "redeemed the stolen ticket at the thief's hello" $redeemPattern
    # The admitted client joins through the recording relay, as an observer on the path would see it.
    $relayPort = $port + 3
    $wireFile = Join-Path $outDir 'wire-admitted.txt'
    $relay = Start-Mb2Role @('wire-relay', "127.0.0.1:$relayPort", "127.0.0.1:$port", $wireFile) (Join-Path $outDir 'relay.log')
    Start-Sleep -Seconds 1
    $admitted = Start-AccountClient 'admitted' 'ADMITTED' "127.0.0.1:${relayPort}?EncryptionToken=$ticket1" ($nullrhi + @("-MB2AdmissionKey=$($t1.Key)",
        '-MB2AccountHold=40', '-MB2AccountFirst=Bor', '-MB2AccountLast=Mlakar', '-MB2AccountModel=manny'))
    [void](Wait-SmokeLog (Join-Path $outDir 'client-admitted.log') 'tag=ADMITTED event=in_world' 120)
    Complete-AccountClient $stolen -Detached
    $admittedManny = Get-SmokeLogCount $serverLog 'MB2ACCOUNT role=server event=admitted .* model=manny'
    Test-Equal 'admitted the player, once, with the ticket the thief redeemed first' $admittedManny '1'
    # The player's own ticket and key again, while it is in the world: the ticket is redeemable, the character is not.
    Invoke-AccountClient 'replay' 'REFUSED' "127.0.0.1:${port}?EncryptionToken=$ticket1" ($nullrhi + @("-MB2AdmissionKey=$($t1.Key)"))
    $t2 = Get-Admission "127.0.0.1:$port"
    $ticket2 = $t2.Ticket
    Invoke-AccountClient 'twice' 'REFUSED' "127.0.0.1:${port}?EncryptionToken=$ticket2" ($nullrhi + @("-MB2AdmissionKey=$($t2.Key)"))
    Test-Server 'refused the character a second time' 'admission_refused stage=redeem reason=already_in_world'
    Complete-AccountClient $admitted -Detached
    if (-not (Test-SmokeLog (Join-Path $outDir 'client-admitted.log') 'tag=ADMITTED event=pass')) { $status = 1 }
    Stop-Relay
    # On the wire: the ticket is in the hello (the scan's own control: it finds a string the engine bit-packed), and
    # nothing the server sends after it, nor the client's login, carries these in the clear.
    $wireScan = Join-Path $outDir 'wire-scan.txt'
    $scan = @(Invoke-Mb2 wire-scan $wireFile $ticket1 /Game/World/lent MarburgumGameMode Mlakar manny)
    $scanCode = $LASTEXITCODE
    Write-SmokeFile $wireScan $scan
    if ($scanCode) { exit $scanCode }
    $needleRe = New-Object System.Text.RegularExpressions.Regex('needle=[A-Za-z0-9_-]{43} ')
    foreach ($line in @(Get-SmokeLogLines $wireScan)) { '  ' + $needleRe.Replace($line, 'needle=<ticket> ', 1) }
    function Get-WireHits([string]$Needle, [string]$Side) {
        $values = foreach ($line in @(Get-SmokeLogLines $wireScan)) {
            if ($line.Contains("needle=$Needle ")) {
                $m = [regex]::Match($line, ".* ${Side}_hits=([0-9]*).*")
                if ($m.Success) { $m.Groups[1].Value }
            }
        }
        @($values) -join "`n"
    }
    $wirePackets = ConvertTo-Count (Get-SmokeLogValue $wireScan '^wire packets=([0-9]*)')
    $packetsOk = 'yes'; if ($wirePackets -lt 100) { $packetsOk = "no ($wirePackets)" }
    Test-Equal 'the relay carried the session (100+ datagrams)' $packetsOk 'yes'
    $helloOk = 'no'; if ((ConvertTo-Count (Get-WireHits $ticket1 'client')) -ge 1) { $helloOk = 'yes' }
    Test-Equal 'the hello carried the ticket in the clear (scan control)' $helloOk 'yes'
    foreach ($needle in '/Game/World/lent', 'MarburgumGameMode', 'Mlakar', 'manny') {
        Test-Equal "no '$needle' on the wire" ((ConvertTo-Count (Get-WireHits $needle 'client')) + (ConvertTo-Count (Get-WireHits $needle 'server'))) '0'
    }
    $replays = Get-SmokeLogCount $serverLog 'admission_refused stage=redeem reason="Your sign-in for this world has expired'
    $inWorld = Get-SmokeLogCount $serverLog 'admission_refused stage=redeem reason=already_in_world'
    if ($replays -ge 2 -and $inWorld -ge 2) {
        "  server refused the forged and the other server's tickets, and the character twice: yes"
    } else {
        "  server refused the forged and the other server's tickets, and the character twice: NO ($replays expired, want 2; $inWorld in world, want 2)"
        $status = 1
    }

    'Restart the account service'
    Stop-AccountService
    Start-AccountService (Join-Path $outDir 'service-2.log')
    $relogin = Get-JsonField (Invoke-Api 'POST' '/v1/login' '{"username":"spela_k","password":"drava-bridge-1990"}') 'id'
    if ($relogin -ceq $firstId) {
        '  returning player after service restart: yes'
    } else {
        "  returning player after service restart: NO ($relogin)"
        $status = 1
    }
    Stop-GameServer

    'Secrets'
    $leaks = 0
    $store = Join-Path $outDir 'accounts.json'
    $gameLogs = @($serverLog, (Join-Path $outDir 'client-*.log'), (Join-Path $outDir 'service-*.log'))
    # Every verification code the service mailed (the outbox), as the header promises.
    $codes = @(Get-SmokeLogMatches (Join-Path $outDir 'outbox\*.txt') '^\s+[A-Z2-9]{8}\s*$' '' |
        ForEach-Object { $_ -replace ' ', '' } | Sort-Object -Unique -CaseSensitive)
    "  verification codes checked: $($codes.Count)"
    function Get-SecretPrefix([string]$Secret) { $Secret.Substring(0, [Math]::Min(6, $Secret.Length)) }
    foreach ($secret in (@('drava-bridge-1990', 'pohorje-snow-1991', [string]$session, [string]$ticket1, [string]$ticket2, [string]$t1.Key, [string]$t2.Key) + $codes)) {
        if (Test-SmokeLog $store ([regex]::Escape($secret))) {
            "  account store holds a secret in the clear: $(Get-SecretPrefix $secret)…"
            $leaks = 1
        }
    }
    foreach ($secret in (@('drava-bridge-1990', 'pohorje-snow-1991', [string]$session) + $codes)) {
        if (Test-SmokeLog $gameLogs ([regex]::Escape($secret))) {
            "  a log holds a password, a session or a verification code: $(Get-SecretPrefix $secret)…"
            $leaks = 1
        }
    }
    if ($leaks -eq 0) { '  none in the store or the logs' } else { $status = 1 }
    # AR13: the server never logs a ticket or a key; the clients that signed in through the screens never log the token.
    $serverSecrets = 0
    foreach ($secret in @([string]$ticket1, [string]$ticket2, [string]$melje.Ticket, [string]$t1.Key, [string]$t2.Key)) {
        if (Test-SmokeLog @($serverLog, (Join-Path $outDir 'service-*.log')) ([regex]::Escape($secret))) { $serverSecrets++ }
    }
    Test-Equal 'the server and service logs hold no ticket or key' $serverSecrets '0'
    $renderedLogs = @((Join-Path $outDir 'client-first.log'), (Join-Path $outDir 'client-return.log'))
    $tokenLines = Get-SmokeLogCount $renderedLogs 'EncryptionToken=|MB2Ticket='
    Test-Equal 'rendered clients logged no encryption token' $tokenLines '0'
    # The rendered clients sign in through the session UI. Their process arguments must not carry a ticket either
    # (the null-RHI admission clients put EncryptionToken on the URL on purpose; that path is the test harness).
    $cmdlineTokens = 0
    foreach ($label in 'first', 'return') {
        $cmdline = Join-Path $outDir "cmdline-$label.txt"
        if (-not (Test-Path -LiteralPath $cmdline -PathType Leaf) -or (Get-Item -LiteralPath $cmdline).Length -eq 0) {
            "  rendered client ${label}: no process arguments captured"
            $status = 1
            continue
        }
        if (Test-SmokeLog $cmdline 'drava-bridge-1990|pohorje-snow-1991') {
            "  rendered client $label process arguments hold a password"
            $status = 1
        }
    }
    if (Test-SmokeLog @((Join-Path $outDir 'cmdline-first.txt'), (Join-Path $outDir 'cmdline-return.txt')) 'EncryptionToken=|MB2Ticket=') {
        $cmdlineTokens = 1
    }
    Test-Equal 'rendered client process arguments hold no ticket' $cmdlineTokens '0'
    Test-Equal 'rendered clients encrypted their sessions' (Get-SmokeLogCount $renderedLogs 'MB2ACCOUNT role=client event=encrypted') '2'
    Test-Equal 'the service logged a redeem for another server' (Get-SmokeLogCount (Join-Path $outDir 'service-1.log') 'event=redeem_refused reason=other_server caller=melje ticket_server=lent-smoke') '1'
} finally {
    Stop-Relay
    Stop-GameServer
    Stop-AccountService
}

# Which build ran: the staged trees' stamp, or build_fresh: NO with the reason (scripts\build-stamp.ps1).
if ($Mode -eq 'staged') {
    'Build'
    $stampFile = Join-Path $outDir 'build-stamp.txt'
    Add-BuildStampCheck $stampFile $Mode
    Get-SmokeLogLines $stampFile | ForEach-Object { "  $_" }
}

if ($status -eq 0) {
    "Result: PASS — $outDir"
} else {
    "Result: FAIL — $outDir"
}
exit $status
