# Shared plumbing for the Windows smoke runners (scripts/*-windows.ps1), the twins of scripts/*-linux.sh.
# Windows PowerShell 5.1 compatible: no ternaries, no ?? / ?., no ForEach-Object -Parallel.
#
# What the bash runners get from setsid / timeout / kill -- -pgid / pgrep -g, this gets from one Win32 job object per
# role: every process a role starts stays in its job, the wall-clock limit terminates the job (exit code 124, like
# timeout), Stop-SmokeRole sends CTRL_BREAK (UE's clean exit request) before terminating the job, and the job is
# killed when this PowerShell exits for any reason (KILL_ON_JOB_CLOSE), so no server outlives its runner.
#
# Log patterns are .NET regular expressions matched line by line (grep -a), not grep BRE: escape literal ? + ( ) | { }.

Set-StrictMode -Version 2

if (-not ('MarburgumII.Smoke.Role' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace MarburgumII.Smoke
{
    public sealed class Role : IDisposable
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct STARTUPINFO
        {
            public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
            public int dwX; public int dwY; public int dwXSize; public int dwYSize; public int dwXCountChars;
            public int dwYCountChars; public int dwFillAttribute; public int dwFlags; public short wShowWindow;
            public short cbReserved2; public IntPtr lpReserved2; public IntPtr hStdInput; public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION { public IntPtr hProcess; public IntPtr hThread; public int dwProcessId; public int dwThreadId; }

        [StructLayout(LayoutKind.Sequential)]
        struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public int bInheritHandle; }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_LIMIT_INFORMATION
        {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct IO_COUNTERS
        {
            public ulong ReadOperationCount; public ulong WriteOperationCount; public ulong OtherOperationCount;
            public ulong ReadTransferCount; public ulong WriteTransferCount; public ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit; public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
        {
            public long TotalUserTime; public long TotalKernelTime; public long ThisPeriodTotalUserTime;
            public long ThisPeriodTotalKernelTime; public uint TotalPageFaultCount; public uint TotalProcesses;
            public uint ActiveProcesses; public uint TotalTerminatedProcesses;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
            IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr CreateFileW(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa, uint disposition,
            uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr CreateJobObjectW(IntPtr sa, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int cls, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, int size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryInformationJobObject(IntPtr job, int cls, out JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info,
            int size, IntPtr ret);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateJobObject(IntPtr job, uint code);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr handle, uint ms);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GenerateConsoleCtrlEvent(uint evt, uint group);
        [DllImport("kernel32.dll")]
        static extern IntPtr GetConsoleWindow();

        const uint CREATE_SUSPENDED = 0x4, CREATE_NEW_PROCESS_GROUP = 0x200, CREATE_UNICODE_ENVIRONMENT = 0x400,
            CREATE_NO_WINDOW = 0x08000000, STARTF_USESTDHANDLES = 0x100, KILL_ON_JOB_CLOSE = 0x2000;
        static readonly IntPtr Invalid = new IntPtr(-1);

        IntPtr job, process;
        Timer wall;
        volatile bool timedOut;
        bool sharesConsole;

        public int Pid { get; private set; }
        public string Log { get; private set; }
        public string CommandLine { get; private set; }
        public bool TimedOut { get { return timedOut; } }

        /// <summary>UE reads its raw command line, so a value with spaces is quoted after the '=' (-Key="a b"),
        /// which MSVC-style argv parsers (Rust, .NET) read the same way.</summary>
        public static string Quote(string arg)
        {
            if (arg.Length > 0 && arg.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return arg;
            int eq = arg.IndexOf('=');
            if (arg.StartsWith("-") && eq > 0 && arg.IndexOf(' ') > eq && arg.IndexOf('"') < 0)
                return arg.Substring(0, eq + 1) + QuoteWhole(arg.Substring(eq + 1));
            return QuoteWhole(arg);
        }

        static string QuoteWhole(string arg)
        {
            var sb = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in arg)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') { sb.Append('\\', slashes * 2 + 1); sb.Append('"'); slashes = 0; continue; }
                sb.Append('\\', slashes); slashes = 0; sb.Append(c);
            }
            sb.Append('\\', slashes * 2);
            return sb.Append('"').ToString();
        }

        public static Role Start(string exe, string[] args, string log, string workDir, int wallSeconds)
        {
            var cmd = new StringBuilder(QuoteWhole(exe));
            foreach (var a in args ?? new string[0]) { cmd.Append(' ').Append(Quote(a)); }
            var role = new Role();
            role.Log = log;
            role.CommandLine = cmd.ToString();
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(log)));
            var sa = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)), bInheritHandle = 1 };
            // FILE_APPEND_DATA | SYNCHRONIZE, shared, so stdout and stderr interleave and the runner can read meanwhile.
            IntPtr output = CreateFileW(log, 0x4 | 0x100000, 0x7, ref sa, 2, 0x80, IntPtr.Zero);
            if (output == Invalid) throw new IOException("cannot create " + log, Marshal.GetLastWin32Error());
            IntPtr input = CreateFileW("NUL", 0x80000000, 0x3, ref sa, 3, 0x80, IntPtr.Zero);
            try
            {
                var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)), dwFlags = (int)STARTF_USESTDHANDLES,
                    hStdInput = input, hStdOutput = output, hStdError = output };
                // Sharing this console (when there is one) is what lets CTRL_BREAK reach the role's own process group.
                uint flags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
                role.sharesConsole = GetConsoleWindow() != IntPtr.Zero;
                if (!role.sharesConsole) flags |= CREATE_NO_WINDOW;
                PROCESS_INFORMATION pi;
                if (!CreateProcessW(null, cmd, IntPtr.Zero, IntPtr.Zero, true, flags, IntPtr.Zero,
                        string.IsNullOrEmpty(workDir) ? null : workDir, ref si, out pi))
                    throw new IOException("cannot start " + exe, Marshal.GetLastWin32Error());
                role.process = pi.hProcess;
                role.Pid = pi.dwProcessId;
                role.job = CreateJobObjectW(IntPtr.Zero, null);
                var limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
                limits.BasicLimitInformation.LimitFlags = KILL_ON_JOB_CLOSE;
                bool contained = role.job != IntPtr.Zero
                    && SetInformationJobObject(role.job, 9, ref limits, Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
                    && AssignProcessToJobObject(role.job, pi.hProcess);
                if (!contained)
                {
                    int error = Marshal.GetLastWin32Error();
                    TerminateProcess(pi.hProcess, 1);
                    CloseHandle(pi.hThread);
                    role.Dispose();
                    throw new IOException("cannot contain " + exe + " in a job object", error);
                }
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
            }
            finally
            {
                CloseHandle(output);
                if (input != Invalid) CloseHandle(input);
            }
            if (wallSeconds > 0)
            {
                role.wall = new Timer(delegate
                {
                    if (role.Alive) { role.timedOut = true; TerminateJobObject(role.job, 124); }
                }, null, wallSeconds * 1000L, Timeout.Infinite);
            }
            return role;
        }

        /// <summary>Whether any process of the role (the one started, or anything it started) is still running.</summary>
        public bool Alive
        {
            get
            {
                if (job == IntPtr.Zero) return false;
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
                return QueryInformationJobObject(job, 1, out info, Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)), IntPtr.Zero)
                    && info.ActiveProcesses > 0;
            }
        }

        public bool HasExited { get { return process == IntPtr.Zero || WaitForSingleObject(process, 0) == 0; } }

        /// <summary>The started process's exit code; 124 when the wall-clock limit ended it (as timeout(1) reports).</summary>
        public int ExitCode
        {
            get
            {
                if (timedOut) return 124;
                uint code;
                if (process == IntPtr.Zero || !GetExitCodeProcess(process, out code) || code == 259) return -1;
                return unchecked((int)code);
            }
        }

        /// <summary>Waits for the whole role (every process in its job) to end; false when the wait timed out.</summary>
        public bool Wait(int milliseconds)
        {
            var deadline = milliseconds < 0 ? DateTime.MaxValue : DateTime.UtcNow.AddMilliseconds(milliseconds);
            if (process != IntPtr.Zero) WaitForSingleObject(process, milliseconds < 0 ? 0xFFFFFFFF : (uint)milliseconds);
            while (Alive)
            {
                if (DateTime.UtcNow >= deadline) return false;
                Thread.Sleep(100);
            }
            return true;
        }

        /// <summary>CTRL_BREAK (UE turns it into a clean exit), then the whole job is terminated after the grace.</summary>
        public void Stop(int graceMilliseconds)
        {
            if (!Alive) return;
            // Without a shared console CTRL_BREAK cannot reach the role, so the job is terminated straight away.
            if (graceMilliseconds > 0 && sharesConsole && GenerateConsoleCtrlEvent(1, (uint)Pid) && Wait(graceMilliseconds)) return;
            TerminateJobObject(job, 1);
            Wait(10000);
        }

        public void Dispose()
        {
            if (wall != null) { wall.Dispose(); wall = null; }
            if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
            if (process != IntPtr.Zero) { CloseHandle(process); process = IntPtr.Zero; }
        }
    }

    public static class LogText
    {
        /// <summary>A log's lines while its writer may still hold it open; CR stripped, invalid UTF-8 replaced.</summary>
        public static string[] Lines(string path)
        {
            if (!File.Exists(path)) return new string[0];
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream, new UTF8Encoding(false, false), true))
            {
                string text = reader.ReadToEnd().Replace("\r\n", "\n").Replace('\r', '\n').Replace("\0", "");
                if (text.EndsWith("\n")) text = text.Substring(0, text.Length - 1);
                return text.Length == 0 ? new string[0] : text.Split('\n');
            }
        }
    }
}
'@
}

$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))
$script:Utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-RepoRoot { $script:RepoRoot }

# ---- process ------------------------------------------------------------------------------------------------------

function Exit-Usage([string]$Message) {
    [Console]::Error.WriteLine($Message)
    exit 2
}

function Exit-Failure([string]$Message, [int]$Code = 1) {
    [Console]::Error.WriteLine($Message)
    exit $Code
}

function Write-Warn([string]$Message) { [Console]::Error.WriteLine($Message) }

<# UE_ROOT set and exactly the pinned engine (check-engine-version.ps1), or exit 2/1 as the bash runners do. #>
function Assert-EngineRoot {
    if (-not $env:UE_ROOT) { Exit-Usage 'Set UE_ROOT to your Unreal Engine 5.8.4 installation root.' }
    $global:LASTEXITCODE = 0
    & (Join-Path $script:RepoRoot 'scripts\check-engine-version.ps1') $env:UE_ROOT | Out-Host
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $env:UE_ROOT
}

function Get-StagedExecutable([string]$Side) {
    # The staged root's MarburgumII<Side>.exe is UE's bootstrapper; the binary it starts is run directly, so its
    # stdout reaches the log and build-stamp.ps1 hashes the same file that ran.
    $dir = Join-Path $script:RepoRoot "Saved\StagedBuilds\Windows$Side\MarburgumII\Binaries\Win64"
    $exe = Get-ChildItem -LiteralPath $dir -Filter "MarburgumII$Side*.exe" -File -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -First 1 -ExpandProperty FullName
    if (-not $exe) { $exe = Join-Path $dir "MarburgumII$Side.exe" }
    $exe
}

<#
.SYNOPSIS
  Server and client commands for a runner's mode: @{ Server = @{ Exe; Args }; Client = @{ Exe; Args }; Mode }.
  staged: the packaged Win64 server/client (scripts\package-windows.ps1). editor: uncooked UnrealEditor-Cmd -server/-game.
  Exits 2 on an unknown mode and 1 when an executable is missing, like the bash runners.
#>
function Resolve-SmokeTargets([string]$Mode, [string]$Usage) {
    $uproject = Join-Path $script:RepoRoot 'MarburgumII.uproject'
    switch ($Mode) {
        'staged' {
            $targets = @{
                Mode = $Mode
                Server = @{ Exe = (Get-StagedExecutable 'Server'); Args = @() }
                Client = @{ Exe = (Get-StagedExecutable 'Client'); Args = @() }
            }
        }
        'editor' {
            $ue = Assert-EngineRoot
            # The -Cmd build is the console-subsystem editor: its stdout reliably reaches the log file.
            $editor = Join-Path $ue 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
            $targets = @{
                Mode = $Mode
                Server = @{ Exe = $editor; Args = @($uproject, '-server') }
                Client = @{ Exe = $editor; Args = @($uproject, '-game') }
            }
        }
        default {
            if (-not $Usage) { $Usage = 'Usage: <runner>.ps1 [staged|editor]' }
            Exit-Usage $Usage
        }
    }
    foreach ($side in 'Server', 'Client') {
        if (-not (Test-Path -LiteralPath $targets[$side].Exe -PathType Leaf)) {
            Exit-Failure "Missing executable: $($targets[$side].Exe) (run scripts\package-windows.ps1 first?)"
        }
    }
    $targets
}

<# The flags every role gets (common_args in the bash runners). #>
function Get-CommonArgs {
    @('-log', '-unattended', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput', '-RenderOffScreen')
}

<# MB2_SMOKE_EXTRA split like bash's read -a (fault injection; never part of an acceptance run). #>
function Get-SmokeExtraArgs {
    if ($env:MB2_SMOKE_EXTRA) { @($env:MB2_SMOKE_EXTRA -split '\s+' | Where-Object { $_ }) } else { @() }
}

function Get-ClientWallSeconds {
    if ($env:MB2_CLIENT_WALL_SECONDS) { [int]$env:MB2_CLIENT_WALL_SECONDS } else { 3600 }
}

<#
.SYNOPSIS
  Starts one role in its own job object with stdout and stderr to Log. Returns the role ([MarburgumII.Smoke.Role]).
  -Target is a Resolve-SmokeTargets side (@{ Exe; Args }) whose Args come first, or use -Exe directly.
  -WallSeconds 0 = no limit (servers); clients pass Get-ClientWallSeconds (timeout --kill-after in bash).
#>
function Start-SmokeRole {
    param(
        [hashtable]$Target,
        [string]$Exe,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$Log,
        [int]$WallSeconds = 0,
        [string]$WorkDir
    )
    $all = @()
    if ($Target) { $Exe = $Target.Exe; $all += $Target.Args }
    $all += $Arguments
    if (-not $WorkDir) { $WorkDir = Split-Path -Parent $Exe }
    [MarburgumII.Smoke.Role]::Start($Exe, [string[]]$all, $Log, $WorkDir, $WallSeconds)
}

<# Starts a role and waits for all of it to end; returns its exit code (124 on the wall-clock limit). #>
function Invoke-SmokeRole {
    param(
        [hashtable]$Target,
        [string]$Exe,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$Log,
        [int]$WallSeconds = 0,
        [string]$WorkDir
    )
    $role = Start-SmokeRole -Target $Target -Exe $Exe -Arguments $Arguments -Log $Log -WallSeconds $WallSeconds -WorkDir $WorkDir
    try { Wait-SmokeRole $role } finally { $role.Dispose() }
}

<# Waits for a started role to end completely and returns its exit code. #>
function Wait-SmokeRole($Role) {
    while (-not $Role.Wait(500)) { }
    $Role.ExitCode
}

<# Stops a role (CTRL_BREAK, then the whole job after GraceSeconds) and releases it. Safe on $null. #>
function Stop-SmokeRole($Role, [int]$GraceSeconds = 10) {
    if ($null -eq $Role) { return }
    try { $Role.Stop($GraceSeconds * 1000) } finally { $Role.Dispose() }
}

# ---- logs ---------------------------------------------------------------------------------------------------------

function Get-SmokeLogLines([string[]]$Path) {
    foreach ($p in $Path) {
        foreach ($resolved in @(Resolve-SmokePaths $p)) { [MarburgumII.Smoke.LogText]::Lines($resolved) }
    }
}

function Resolve-SmokePaths([string]$Path) {
    if ($Path -match '[\*\?]') {
        Get-ChildItem -Path $Path -File -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { $_.FullName }
    } else { $Path }
}

<# grep -aq: whether any line of the log(s) matches the regex. #>
function Test-SmokeLog([string[]]$Path, [string]$Pattern) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    foreach ($line in @(Get-SmokeLogLines $Path)) { if ($regex.IsMatch($line)) { return $true } }
    $false
}

<# grep -ac: how many lines match. #>
function Get-SmokeLogCount([string[]]$Path, [string]$Pattern) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    @(Get-SmokeLogLines $Path | Where-Object { $regex.IsMatch($_) }).Count
}

<#
  grep -ah PATTERN | sed 's/^.*FROM/INDENT FROM/': the matching lines, each cut to start at its last FROM (the evidence
  prefix, e.g. MB2SMOKE), with an optional indent.
#>
function Get-SmokeLogMatches([string[]]$Path, [string]$Pattern, [string]$From, [string]$Indent = '') {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    foreach ($line in @(Get-SmokeLogLines $Path)) {
        if (-not $regex.IsMatch($line)) { continue }
        if ($From) {
            $at = $line.LastIndexOf($From)
            if ($at -ge 0) { $line = $line.Substring($at) }
        }
        $Indent + $line
    }
}

<# The first capture group of the last line matching the regex (sed -n 's/.*x=\([^ ]*\).*/\1/p' | tail -n 1). #>
function Get-SmokeLogValue([string[]]$Path, [string]$Pattern) {
    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    $value = $null
    foreach ($line in @(Get-SmokeLogLines $Path)) {
        $m = $regex.Match($line)
        if ($m.Success) { if ($m.Groups.Count -gt 1) { $value = $m.Groups[1].Value } else { $value = $m.Value } }
    }
    $value
}

<#
  Polls until a log line matches. Returns $true, or $false on timeout or when -Role ended first (its log then says why).
#>
function Wait-SmokeLog([string[]]$Path, [string]$Pattern, [double]$TimeoutSeconds, $Role = $null, [double]$PollSeconds = 0.5) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        if (Test-SmokeLog $Path $Pattern) { return $true }
        if ($null -ne $Role -and -not $Role.Alive) { return (Test-SmokeLog $Path $Pattern) }
        if ((Get-Date) -ge $deadline) { return $false }
        Start-Sleep -Milliseconds ([int]($PollSeconds * 1000))
    }
}

<#
  Starts a dedicated server and waits for "MB2SMOKE role=server event=ready .* port=<port> " in its log; exits the
  runner (1) like the bash start_server when the port is taken, the server dies, or it is not ready in time.
#>
function Start-SmokeServer {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Target,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Log,
        [Parameter(Mandatory = $true)][int]$Port,
        [string]$PortVariable = 'MB2_SMOKE_PORT',
        [double]$ReadySeconds = 180,
        [string]$ReadyPattern
    )
    if (Test-UdpPortInUse $Port) {
        Exit-Failure "UDP port $Port is already in use; stop the other server or set $PortVariable."
    }
    if (-not $ReadyPattern) { $ReadyPattern = "MB2SMOKE role=server event=ready .* port=$Port " }
    $role = Start-SmokeRole -Target $Target -Arguments $Arguments -Log $Log
    if (-not (Wait-SmokeLog $Log $ReadyPattern $ReadySeconds $role)) {
        $alive = $role.Alive
        Stop-SmokeRole $role 0
        if (-not $alive) { Exit-Failure "Server exited before becoming ready; see $Log" }
        Exit-Failure "Server did not become ready on port $Port within $ReadySeconds s; see $Log"
    }
    # The ready line is logged from BeginPlay, before an uncooked (editor) server has finished loading World Partition
    # content: it can sit at frame 0 for most of a minute, longer than a client's 20 s connection timeout. Wait for the
    # first logged frame after 0 (bounded by the same budget; a server that never logs a later frame is not held up).
    [void](Wait-SmokeLog $Log '^\[[^\]]+\]\[\s*[1-9][0-9]*\]' $ReadySeconds $role)
    $role
}

<# A GPU device that failed to come up before connecting (the bash runners' Vulkan retry; D3D12 on Windows). #>
function Test-GpuInitFailure([string]$Log) {
    Test-SmokeLog $Log 'Vulkan device creation failed|D3D12.*(device creation failed|CreateDevice failed)|Failed to create (the )?D3D12 device|No compatible graphics hardware'
}

# ---- network ------------------------------------------------------------------------------------------------------

function Test-UdpPortInUse([int]$Port) {
    $props = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
    [bool]@($props.GetActiveUdpListeners() | Where-Object { $_.Port -eq $Port }).Count
}

function Test-TcpPortInUse([int]$Port) {
    $props = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
    [bool]@($props.GetActiveTcpListeners() | Where-Object { $_.Port -eq $Port }).Count
}

<#
  A throwaway CA and a 127.0.0.1 leaf for the account service's TLS (openssl req/x509 in the bash runners).
  Writes <OutDir>\ca.pem, <SecretsDir>\ca.key, <SecretsDir>\<Name>.pem and <SecretsDir>\<Name>.key (PKCS#8 PEM).
  Returns @{ Ca; CaKey; Cert; Key }.
#>
function New-SmokeCertificates([string]$OutDir, [string]$SecretsDir, [string]$CaName = 'MB2 smoke CA', [string]$Name = 'service', [string]$CertDir) {
    if (-not $CertDir) { $CertDir = $SecretsDir }
    New-Item -ItemType Directory -Force -Path $OutDir, $SecretsDir, $CertDir | Out-Null
    $X = [System.Security.Cryptography.X509Certificates.X509Certificate2]
    $sha = [System.Security.Cryptography.HashAlgorithmName]::SHA256
    $pad = [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
    $caKey = New-SmokeRsaKey
    $caReq = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest("CN=$CaName", $caKey, $sha, $pad)
    $caReq.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension($true, $false, 0, $true)))
    $caReq.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509KeyUsageExtension(([System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]'KeyCertSign, CrlSign'), $true)))
    $now = [DateTimeOffset]::UtcNow.AddMinutes(-5)
    $ca = $caReq.CreateSelfSigned($now, $now.AddDays(1))

    $leafKey = New-SmokeRsaKey
    $leafReq = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest('CN=127.0.0.1', $leafKey, $sha, $pad)
    $san = New-Object System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder
    $san.AddIpAddress([System.Net.IPAddress]::Parse('127.0.0.1'))
    $san.AddDnsName('localhost')
    $leafReq.CertificateExtensions.Add($san.Build())
    $leafReq.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension($false, $false, 0, $false)))
    $serial = New-Object byte[] 16
    (New-Object System.Random).NextBytes($serial); $serial[0] = $serial[0] -band 0x7f
    $leaf = $leafReq.Create($ca, $now, $now.AddDays(1), $serial)

    $paths = @{
        Ca = (Join-Path $OutDir 'ca.pem'); CaKey = (Join-Path $SecretsDir 'ca.key')
        Cert = (Join-Path $CertDir "$Name.pem"); Key = (Join-Path $SecretsDir "$Name.key")
    }
    Write-SmokeFile $paths.Ca (ConvertTo-Pem 'CERTIFICATE' $ca.RawData)
    Write-SmokeFile $paths.CaKey (ConvertTo-Pem 'PRIVATE KEY' $caKey.Key.Export([System.Security.Cryptography.CngKeyBlobFormat]::Pkcs8PrivateBlob))
    Write-SmokeFile $paths.Cert (ConvertTo-Pem 'CERTIFICATE' $leaf.RawData)
    Write-SmokeFile $paths.Key (ConvertTo-Pem 'PRIVATE KEY' $leafKey.Key.Export([System.Security.Cryptography.CngKeyBlobFormat]::Pkcs8PrivateBlob))
    $paths
}

function New-SmokeRsaKey {
    $params = New-Object System.Security.Cryptography.CngKeyCreationParameters
    $params.ExportPolicy = [System.Security.Cryptography.CngExportPolicies]'AllowPlaintextExport'
    $params.Parameters.Add((New-Object System.Security.Cryptography.CngProperty('Length', [BitConverter]::GetBytes(2048), [System.Security.Cryptography.CngPropertyOptions]::None)))
    $key = [System.Security.Cryptography.CngKey]::Create([System.Security.Cryptography.CngAlgorithm]::Rsa, [NullString]::Value, $params)
    New-Object System.Security.Cryptography.RSACng($key)
}

function ConvertTo-Pem([string]$Label, [byte[]]$Bytes) {
    $b64 = [Convert]::ToBase64String($Bytes)
    $lines = for ($i = 0; $i -lt $b64.Length; $i += 64) { $b64.Substring($i, [Math]::Min(64, $b64.Length - $i)) }
    @("-----BEGIN $Label-----") + $lines + @("-----END $Label-----")
}

<#
  curl.exe (in Windows since 1803) for the account service: returns @{ Status = <int>; Body = <string> }.
  -CaFile pins the throwaway CA; -Headers is a hashtable.
#>
function Invoke-SmokeHttp([string]$Method, [string]$Url, [string]$Body, [string]$CaFile, [hashtable]$Headers = @{}, [int]$TimeoutSeconds = 30) {
    $bodyFile = [IO.Path]::GetTempFileName()
    $outFile = [IO.Path]::GetTempFileName()
    try {
        $curlArgs = @('-s', '-o', $outFile, '-w', '%{http_code}', '--max-time', "$TimeoutSeconds", '-X', $Method)
        # Schannel cannot check revocation for a throwaway CA; --ssl-no-revoke keeps the pin without that lookup.
        if ($CaFile) { $curlArgs += @('--cacert', $CaFile, '--ssl-no-revoke') }
        foreach ($k in $Headers.Keys) { $curlArgs += @('-H', "${k}: $($Headers[$k])") }
        if ($PSBoundParameters.ContainsKey('Body')) {
            [IO.File]::WriteAllText($bodyFile, $Body, $script:Utf8)
            $curlArgs += @('-H', 'Content-Type: application/json', '--data-binary', "@$bodyFile")
        }
        $status = & curl.exe @curlArgs $Url 2>$null
        $code = 0
        [void][int]::TryParse(($status | Out-String).Trim(), [ref]$code)
        @{ Status = $code; Body = [IO.File]::ReadAllText($outFile, $script:Utf8) }
    } finally {
        Remove-Item -LiteralPath $bodyFile, $outFile -ErrorAction SilentlyContinue
    }
}

# ---- files and evidence -------------------------------------------------------------------------------------------

<# UTF-8 without BOM, LF line ends: evidence files compare byte for byte with the Linux runners'. #>
function Write-SmokeFile([string]$Path, [object[]]$Lines) {
    $text = (@($Lines | ForEach-Object { "$_" }) -join "`n")
    if ($text.Length) { $text += "`n" }
    [IO.File]::WriteAllText($Path, $text, $script:Utf8)
}

function Add-SmokeFile([string]$Path, [object[]]$Lines) {
    $text = (@($Lines | ForEach-Object { "$_" }) -join "`n")
    if ($text.Length) { $text += "`n" }
    [IO.File]::AppendAllText($Path, $text, $script:Utf8)
}

<# A raw string as given (printf without a trailing newline unless it is in the text). #>
function Write-SmokeText([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $script:Utf8) }

function Get-UtcStamp { (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') }
function Get-UtcIso { (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }
function Get-UnixSeconds { [int64]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) }

<# Saved\<Name>\<UTC timestamp>, created. MB2_SMOKE_DIR overrides Name, as the bash wrappers do. #>
function New-SmokeOutDir([string]$Name) {
    if ($env:MB2_SMOKE_DIR) { $Name = $env:MB2_SMOKE_DIR }
    $dir = Join-Path $script:RepoRoot ("Saved\$Name\" + (Get-UtcStamp))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $dir
}

<# uname -srm. #>
function Get-SmokeHost {
    $os = [Environment]::OSVersion.Version
    "Windows_NT $($os.Major).$($os.Minor).$($os.Build) $($env:PROCESSOR_ARCHITECTURE)"
}

<# git rev-parse HEAD, plus " (dirty)" when the given paths differ from HEAD. #>
function Get-SmokeGitSha([string[]]$Paths = @('Source', 'Config')) {
    $sha = (git -C $script:RepoRoot rev-parse HEAD).Trim()
    git -C $script:RepoRoot diff --quiet HEAD -- @Paths
    if ($LASTEXITCODE) { "$sha (dirty)" } else { $sha }
}

function Get-SmokeSha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

<# Appends build-stamp.ps1 check to the summary (staged runs); warns when the staged build is not fresh. #>
function Add-BuildStampCheck([string]$Summary, [string]$Mode = 'staged') {
    if ($Mode -ne 'staged') { return }
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:RepoRoot 'scripts\build-stamp.ps1') check 2>&1
    $code = $LASTEXITCODE
    Add-SmokeFile $Summary @($out | ForEach-Object { "$_" })
    if ($code) { Write-Warn "WARNING: the staged build is not fresh; see $Summary" }
}

<# scripts\mb2.ps1 <args>: output lines; the exit code is in $LASTEXITCODE. #>
function Invoke-Mb2 {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:RepoRoot 'scripts\mb2.ps1') @args
}

function Get-PassFail([int]$Status) { if ($Status -eq 0) { 'PASS' } else { 'FAIL' } }

<#
  Mean grey (0..255) of a screenshot region after scaling to 1920x1080, alpha ignored (magick ... -colorspace gray
  -format %[fx:mean*255] in weather-smoke-linux.sh). Rec. 709 luma, as ImageMagick's gray. "?" when unreadable.
#>
function Measure-ImageMeanGrey([string]$Path, [int]$X, [int]$Y, [int]$Width, [int]$Height) {
    Add-Type -AssemblyName System.Drawing
    try {
        $src = [System.Drawing.Image]::FromFile($Path)
        try {
            $bmp = New-Object System.Drawing.Bitmap(1920, 1080, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.DrawImage($src, 0, 0, 1920, 1080)
            $g.Dispose()
            $rect = New-Object System.Drawing.Rectangle($X, $Y, $Width, $Height)
            $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $bmp.PixelFormat)
            $bytes = New-Object byte[] ($data.Stride * $Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
            $bmp.UnlockBits($data); $bmp.Dispose()
            $sum = 0.0
            for ($row = 0; $row -lt $Height; $row++) {
                $o = $row * $data.Stride
                for ($col = 0; $col -lt $Width; $col++) {
                    $i = $o + $col * 3
                    $sum += 0.0722 * $bytes[$i] + 0.7152 * $bytes[$i + 1] + 0.2126 * $bytes[$i + 2]
                }
            }
            '{0:0.######}' -f ($sum / ($Width * $Height))
        } finally { $src.Dispose() }
    } catch { '?' }
}

Export-ModuleMember -Function *
