# Windows twin of encryption-negative-linux.sh (same cases, evidence lines and expectations).
# Negative controls around ADR-083 that the account smoke does not cover.
#
#   unencrypted   A client that expects encryption (ticket on the URL, key on the command line) joins a
#                 development server that never encrypts. It must leave. Staying in that world is a failure.
#   service-down  An account-required server whose account service is not listening must refuse the hello
#                 and admit nobody.
#   service-stall An account-required server whose service accepts TLS and then sends no HTTP answer must
#                 time out the redeem and admit nobody. Skipped when the TLS stall listener cannot listen.
#
# Replay, duplication and out-of-order datagrams are not injected here: Marburgum.Net.StrictEncryption and
# Marburgum.Net.ReplayWindow cover tamper, a wrong key, plaintext-after-key and replayed packets (ADR-088), and the
# account smoke covers a stolen hello ticket redeemed before its player.
#
# Usage: scripts\encryption-negative-windows.ps1 [staged]
# Port MB2_ENC_NEG_PORT (default 28170). Never 7777 or 17777.
param([string]$Mode = 'staged')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

$repoRoot = Get-RepoRoot
$port = 28170; if ($env:MB2_ENC_NEG_PORT) { $port = [int]$env:MB2_ENC_NEG_PORT }
if ($port -eq 7777 -or $port -eq 17777) { Exit-Usage "Refusing port $port" }
if ($Mode -ne 'staged') { Exit-Usage 'Usage: scripts\encryption-negative-windows.ps1 [staged]' }

$targets = Resolve-SmokeTargets 'staged' 'Usage: scripts\encryption-negative-windows.ps1 [staged]'
# A stale staged build stops the run here (exit 3), as set -e does in the bash runner.
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-stamp.ps1') check | Out-Null
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$outDir = New-SmokeOutDir 'EncryptionNegative'
$secrets = Join-Path $outDir 'secrets'
New-Item -ItemType Directory -Force -Path $secrets | Out-Null
# chmod 700: only the current user may read the secrets directory (files created in it inherit this).
$sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
& icacls.exe $secrets /inheritance:r /grant:r "*${sid}:(OI)(CI)F" | Out-Null
if ($LASTEXITCODE) { Exit-Failure "Could not restrict access to $secrets (icacls exit $LASTEXITCODE)" }
$map = '/Game/World/lent'
$common = @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
$status = 0
$server = $null
$stall = $null
$summary = Join-Path $outDir 'summary.txt'

function New-RandomBytes([int]$Count) {
    $bytes = New-Object byte[] $Count
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($bytes); $rng.Dispose()
    , $bytes
}
# head -c 32 /dev/urandom | base64 | tr '+/' '-_' | tr -d '='
function New-B64x32 { (([Convert]::ToBase64String((New-RandomBytes 32)) -replace '\+', '-') -replace '/', '_') -replace '=', '' }
$ticket = New-B64x32
$key = New-B64x32
if ($ticket.Length -ne 43 -or $key.Length -ne 43) { Exit-Failure 'ticket/key encoding was not 43 characters' }

function Stop-GameServer { Stop-SmokeRole $script:server; $script:server = $null }

function Start-GameServer([string[]]$Arguments, [string]$Log) {
    $script:server = Start-SmokeRole -Target $targets.Server -Arguments $Arguments -Log $Log
}

function Wait-Ready([string]$Log) {
    if (Wait-SmokeLog $Log "MB2SMOKE role=server event=ready .* port=$port " 180 $script:server) { return $true }
    if (-not $script:server.Alive) {
        Write-Warn "Server exited before becoming ready; see $Log"
    } else {
        Write-Warn "Server did not become ready on port $port; see $Log"
    }
    $false
}

function Test-PortFree { -not (Test-UdpPortInUse $port) }

# echo ... | tee -a summary.txt
function Write-Note([string]$Text) {
    $Text
    Add-SmokeFile $summary @($Text)
}

Write-SmokeFile $summary @(
    'MarburgumII encryption negative controls'
    "date_utc: $(Get-UtcIso)"
    "git_sha: $((git -C $repoRoot rev-parse HEAD).Trim())"
    "port: $port"
)
Add-BuildStampCheck $summary 'staged'

try {
    # ---------------------------------------------------------------- a world that never encrypts
    # A signed-in client is one started with -MB2Accounts (https). The engine fails the hello when that client
    # offers a token to a server that never encrypts (PendingConnectionFailure), so the post-join guard
    # refused_unencrypted_world is not what this path hits. The client must still end the session and must not
    # keep the city as a local authority world (front_end_enforced).
    Write-Note 'Case unencrypted: development server, signed-in client expects encryption'
    if (-not (Test-PortFree)) {
        Write-Note "  unencrypted: FAIL (port $port busy)"
        $status = 1
    } else {
        $serverLog = Join-Path $outDir 'unencrypted-server.log'
        Start-GameServer (@($map, "-port=$port") + $common) $serverLog
        if (Wait-Ready $serverLog) {
            $clientLog = Join-Path $outDir 'unencrypted-client.log'
            [void](Invoke-SmokeRole -Target $targets.Client -WallSeconds 90 -Log $clientLog -Arguments (
                @("127.0.0.1:${port}?MB2Character=enc-neg-player?EncryptionToken=$ticket") + $common +
                @("-MB2AdmissionKey=$key", '-MB2Accounts=https://127.0.0.1:9')))
            $refused = 0
            if (Test-SmokeLog $clientLog '(event=refused_unencrypted_world|phase=error fault=refused)') { $refused = 1 }
            $front = 0
            if (Test-SmokeLog $clientLog 'event=front_end_enforced') { $front = 1 }
            if ($refused -eq 1 -and $front -eq 1) {
                Write-Note '  unencrypted: PASS (session refused, city not kept as a local world)'
            } else {
                Write-Note "  unencrypted: FAIL (refused=$refused front_end_enforced=$front)"
                $status = 1
            }
            if (Test-SmokeLog $serverLog '(MB2ACCOUNT role=server event=admitted|event=ticket_redeemed)') {
                Write-Note '  unencrypted server admitted someone: FAIL'
                $status = 1
            }
        } else {
            Write-Note '  unencrypted: FAIL (server)'
            $status = 1
        }
        Stop-GameServer
    }

    # ---------------------------------------------------------------- account service not listening
    Write-Note 'Case service-down: nothing accepts the redeem'
    $downPort = $port + 1
    $serverKey = Join-Path $secrets 'server.key'
    # head -c 48 /dev/urandom | base64 | tr -d '\n/+='
    Write-SmokeText $serverKey ([Convert]::ToBase64String((New-RandomBytes 48)) -replace '[/+=]', '')
    if (-not (Test-PortFree)) {
        Write-Note "  service-down: FAIL (port $port still busy)"
        $status = 1
    } else {
        $serverLog = Join-Path $outDir 'down-server.log'
        Start-GameServer (@($map, "-port=$port", "-MB2Accounts=https://127.0.0.1:$downPort", "-MB2AccountsKeyFile=$serverKey") + $common) $serverLog
        if (Wait-Ready $serverLog) {
            [void](Invoke-SmokeRole -Target $targets.Client -WallSeconds 40 -Log (Join-Path $outDir 'down-client.log') -Arguments (
                @("127.0.0.1:${port}?EncryptionToken=$ticket") + $common + @("-MB2AdmissionKey=$key")))
            Start-Sleep -Seconds 2
            if ((Test-SmokeLog $serverLog 'admission_refused stage=redeem reason="The account service is unavailable') -and
                -not (Test-SmokeLog $serverLog 'event=ticket_redeemed') -and
                -not (Test-SmokeLog $serverLog 'event=admitted')) {
                Write-Note '  service-down: PASS'
            } else {
                Write-Note '  service-down: FAIL (see down-server.log)'
                $status = 1
            }
            if ((Test-SmokeLog $serverLog ([regex]::Escape($ticket))) -or (Test-SmokeLog $serverLog ([regex]::Escape($key)))) {
                Write-Note '  service-down leaked the ticket or key into the server log: FAIL'
                $status = 1
            }
        } else {
            Write-Note '  service-down: FAIL (server)'
            $status = 1
        }
        Stop-GameServer
    }

    # ---------------------------------------------------------------- TLS accepts, HTTP never answers
    Write-Note 'Case service-stall: TLS up, no HTTP response'
    $stallPort = $port + 2
    $certs = New-SmokeCertificates -OutDir $outDir -SecretsDir $secrets -CaName 'MB2 stall CA' -Name 'stall' -CertDir $outDir
    # openssl s_server -accept <port> -naccept 1 stand-in: a separate PowerShell that accepts one TLS connection with
    # the stall certificate, completes the handshake, prints what it receives and never answers.
    $stallScript = @'
param([string]$CertPem, [string]$KeyPem, [int]$Port)
$ErrorActionPreference = 'Stop'
function Der([string]$Path) { [Convert]::FromBase64String(((@([IO.File]::ReadAllLines($Path)) | Where-Object { $_ -notmatch '^-----' }) -join '')) }
function Tlv([byte[]]$B, [int]$At) {
    $len = [int]$B[$At + 1]; $hdr = 2
    if ($len -band 0x80) { $n = $len -band 0x7f; $len = 0; for ($i = 0; $i -lt $n; $i++) { $len = ($len -shl 8) -bor $B[$At + 2 + $i] }; $hdr = 2 + $n }
    @{ Start = $At + $hdr; End = $At + $hdr + $len }
}
function Int([byte[]]$B, $T, [int]$Size) {
    $v = [byte[]]$B[$T.Start..($T.End - 1)]
    $skip = 0; while ($skip -lt $v.Length - 1 -and $v[$skip] -eq 0) { $skip++ }
    $v = [byte[]]$v[$skip..($v.Length - 1)]
    if ($Size -gt $v.Length) { $v = [byte[]]((New-Object byte[] ($Size - $v.Length)) + $v) }
    , $v
}
# PKCS#8 PrivateKeyInfo -> RSAPrivateKey -> RSAParameters (an imported CNG key cannot be exported for Schannel).
$pk = [byte[]](Der $KeyPem)
$outer = Tlv $pk 0; $ver = Tlv $pk $outer.Start; $alg = Tlv $pk $ver.End; $oct = Tlv $pk $alg.End
$seq = Tlv $pk $oct.Start; $t = Tlv $pk $seq.Start
$ints = @(); $at = $t.End
for ($i = 0; $i -lt 8; $i++) { $x = Tlv $pk $at; $ints += , $x; $at = $x.End }
$modulus = Int $pk $ints[0] 0; $half = [int]($modulus.Length / 2)
$prm = New-Object System.Security.Cryptography.RSAParameters
$prm.Modulus = $modulus; $prm.Exponent = Int $pk $ints[1] 0; $prm.D = Int $pk $ints[2] $modulus.Length
$prm.P = Int $pk $ints[3] $half; $prm.Q = Int $pk $ints[4] $half; $prm.DP = Int $pk $ints[5] $half
$prm.DQ = Int $pk $ints[6] $half; $prm.InverseQ = Int $pk $ints[7] $half
$rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
$rsa.ImportParameters($prm)
$cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new([byte[]](Der $CertPem))
$withKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::CopyWithPrivateKey($cert, $rsa)
$serverCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new([byte[]]$withKey.Export('Pfx'))
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
'ACCEPT'
$client = $listener.AcceptTcpClient()
$listener.Stop()
$ssl = [System.Net.Security.SslStream]::new($client.GetStream(), $false)
$ssl.AuthenticateAsServer($serverCert, $false, [System.Security.Authentication.SslProtocols]::None, $false)
"handshake done protocol=$($ssl.SslProtocol)"
$buffer = New-Object byte[] 4096
try {
    while (($n = $ssl.Read($buffer, 0, $buffer.Length)) -gt 0) { [Text.Encoding]::UTF8.GetString($buffer, 0, $n) }
} catch { "read ended: $($_.Exception.Message)" }
'DONE'
'@
    $stallStarted = $false
    $stallLog = Join-Path $outDir 'stall.log'
    try {
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(
            "& { $stallScript } -CertPem '$($certs.Cert -replace "'", "''")' -KeyPem '$($certs.Key -replace "'", "''")' -Port $stallPort"))
        $stall = Start-SmokeRole -Exe (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -Log $stallLog `
            -WorkDir $outDir -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
        # PowerShell starts slower than openssl: wait for the listener instead of a fixed 0.5 s.
        $stallStarted = Wait-SmokeLog $stallLog '^ACCEPT$' 20 $stall
    } catch {
        $stallStarted = $false
    }
    if (-not $stallStarted -or -not $stall.Alive) {
        Write-Note '  service-stall: SKIPPED (TLS stall listener did not start)'
        Stop-SmokeRole $stall
        $stall = $null
    } else {
        if (-not (Test-PortFree)) {
            Write-Note '  service-stall: FAIL (game port busy)'
            $status = 1
        } else {
            $serverLog = Join-Path $outDir 'stall-server.log'
            Start-GameServer (@($map, "-port=$port", "-MB2Accounts=https://127.0.0.1:$stallPort", "-MB2AccountsKeyFile=$serverKey",
                "-ini:Engine:[SSL]:OverrideCertificateBundlePath=$($certs.Ca)") + $common) $serverLog
            if (Wait-Ready $serverLog) {
                [void](Invoke-SmokeRole -Target $targets.Client -WallSeconds 40 -Log (Join-Path $outDir 'stall-client.log') -Arguments (
                    @("127.0.0.1:${port}?EncryptionToken=$(New-B64x32)") + $common + @("-MB2AdmissionKey=$(New-B64x32)")))
                Start-Sleep -Seconds 12
                if ((Test-SmokeLog $serverLog 'admission_refused stage=redeem') -and
                    -not (Test-SmokeLog $serverLog '(event=ticket_redeemed|event=admitted)')) {
                    Write-Note '  service-stall: PASS'
                } else {
                    Write-Note '  service-stall: FAIL (redeem was not refused, or somebody was admitted)'
                    $status = 1
                }
            } else {
                Write-Note '  service-stall: FAIL (server)'
                $status = 1
            }
            Stop-GameServer
        }
        Stop-SmokeRole $stall
        $stall = $null
    }
} finally {
    Stop-GameServer
    Stop-SmokeRole $stall
}

Write-Note 'Not claimed: datagram-level replay injection (unit-tested only, ADR-088) or session-key rotation.'
Write-Note "Result: $(Get-PassFail $status) — $outDir"
exit $status
