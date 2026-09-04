#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Sends TJS code to the plugin over TCP or controls the game.
.PARAMETER Code
    TJS code to execute. Reads from stdin when omitted.
.PARAMETER Restart
    Restart the game.
.PARAMETER Kill
    Forcefully terminate the game.
.PARAMETER Port
    TCP server port (default: 18700).
.PARAMETER GameExe
    Which copy of the game to start and stop. A second copy runs on its own port
    and log directory (see the plugin config), so -Restart has to be told which
    one is meant; the process to kill is derived from this path, so the other
    copy is left alone.
#>
param(
    [string]$Code,
    [switch]$Restart,
    [switch]$Kill,
    [int]$Port = 18700,
    [string]$GameExe = "D:\Games\ATRI-TestVersion\ATRI-MyDearMoments-.exe",
    [string]$WsUrl = "ws://127.0.0.1:8000",
    # procdump watches for the game and writes a full dump on an unhandled
    # exception. Two crashes were lost before this was wired in, because the
    # tool has to be running *before* the fault, and remembering it by hand does
    # not work. -NoDump opts out.
    [switch]$NoDump
)

# Both derived from -GameExe, so pointing at the copy targets only the copy: the
# two have different process names, and killing by name would otherwise be a
# coin toss over which instance dies.
$GameProcessName = [System.IO.Path]::GetFileNameWithoutExtension($GameExe)
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PluginDll = Join-Path $RepoRoot "build\prototype\Debug\Neuro-Integration.dll"
$GameDir = Split-Path -Parent $GameExe
$ProcDump = "C:\ProgramData\procdump\procdump.exe"
$DumpDir = "C:\Temp\dumps"

function Start-CrashWatcher {
    if (-not (Test-Path $ProcDump)) {
        Write-Host "procdump not found at $ProcDump; crashes will leave no dump."
        return
    }
    # One watcher at a time: procdump exits with the process it watched, so a
    # stale one from a previous run would sit waiting and catch nothing.
    Get-Process -Name procdump -ErrorAction SilentlyContinue | Stop-Process -Force
    New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null
    Start-Process -FilePath $ProcDump -WindowStyle Hidden -ArgumentList @(
        "-accepteula", "-e", "-ma", "-n", "5", "-w", "$GameProcessName.exe", $DumpDir
    ) -RedirectStandardOutput (Join-Path $DumpDir "procdump.log")
    Write-Host "Crash watcher armed: dumps go to $DumpDir"
}

function Restart-Game {
    $procs = Get-Process -Name $GameProcessName -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        Write-Host "Killing game process (PID $($p.Id))..."
        $p.Kill()
        if (-not $p.WaitForExit(5000)) {
            throw "Game process did not exit within 5 seconds (PID $($p.Id))."
        }
    }
    if (Get-Process -Name $GameProcessName -ErrorAction SilentlyContinue) {
        throw "Game process is still running; refusing to start a second instance."
    }
    Start-Sleep -Milliseconds 500

    $targetDll = Join-Path $GameDir "Neuro-Integration.dll"
    if (Test-Path $PluginDll) {
        Copy-Item -LiteralPath $PluginDll -Destination $targetDll -Force
        Write-Host "DLL updated: $PluginDll -> $targetDll"
    }

    # The TJS the plugin loads at run time lives beside the game, not in the
    # DLL, so a restart has to bring it along the way it brings the build.
    # Editing these in the repository alone changed nothing in the game, and
    # that cost a full session before anyone noticed the deployed copy was days
    # old.
    $scriptSource = Join-Path $RepoRoot "integration-assets\neuro_scripts"
    if (Test-Path $scriptSource) {
        $scriptTarget = Join-Path $GameDir "neuro_scripts"
        if (-not (Test-Path $scriptTarget)) {
            New-Item -ItemType Directory -Path $scriptTarget | Out-Null
        }
        # Recursive, and the folders come along: the scripts are loaded by
        # path (neuro_scripts/install/hooks.tjs), so a flat copy would leave
        # the plugin asking for files that are not there.
        $scripts = Get-ChildItem -Path $scriptSource -Filter *.tjs -File -Recurse
        foreach ($script in $scripts) {
            $relative = $script.FullName.Substring($scriptSource.Length).TrimStart('\')
            $destination = Join-Path $scriptTarget $relative
            $folder = Split-Path -Parent $destination
            if (-not (Test-Path $folder)) {
                New-Item -ItemType Directory -Path $folder -Force | Out-Null
            }
            Copy-Item -LiteralPath $script.FullName -Destination $destination -Force
        }
        Write-Host "TJS scripts updated: $($scripts.Count) file(s) -> $scriptTarget"
    }

    if (-not $NoDump) {
        # Before the game starts: -w waits for the next launch, so arming it
        # afterwards would watch nothing.
        Start-CrashWatcher
    }

    Write-Host "Starting game..."
    $env:NEURO_WS_URL = $WsUrl
    Start-Process -FilePath $GameExe -WorkingDirectory $GameDir
    Write-Host "Game started. Waiting for TCP server..."

    $waitStart = Get-Date
    $connected = $false
    while (-not $connected -and ((Get-Date) - $waitStart).TotalSeconds -lt 30) {
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $Port)
            $tcp.Close()
            $connected = $true
        } catch {
            Start-Sleep -Milliseconds 500
        }
    }
    if ($connected) {
        Write-Host "TCP server ready!"
    } else {
        Write-Warning "TCP server not responding after 30s"
    }
}

function Kill-Game {
    $procs = Get-Process -Name $GameProcessName -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        Write-Host "Killing game process (PID $($p.Id))..."
        $p.Kill()
        $p.WaitForExit(5000)
    }
}

if ($Kill) { Kill-Game; exit 0 }
if ($Restart) { Restart-Game; exit 0 }

# Read from stdin when Code was not supplied as a parameter.
if (-not $Code) {
    $Code = $input | ForEach-Object { $_ }
}

$codeToSend = "$Code".Trim()
if (-not $codeToSend) {
    Write-Host "Usage: .\neuro_tcp_client.ps1 -Code '<TJS code>'"
    Write-Host "   or: .\neuro_tcp_client.ps1 -Restart"
    Write-Host "   or: .\neuro_tcp_client.ps1 -Kill"
    Write-Host "   or: Get-Content script.tjs | .\neuro_tcp_client.ps1"
    exit 1
}

try {
    $tcp = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $Port)
    $stream = $tcp.GetStream()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $reader = [System.IO.BinaryReader]::new($stream)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($codeToSend)
    $writer.Write([uint32]$bytes.Length)
    $writer.Write($bytes)
    $writer.Flush()

    $responseLen = $reader.ReadUInt32()
    $responseBytes = $reader.ReadBytes($responseLen)
    $responseText = [System.Text.Encoding]::UTF8.GetString($responseBytes)

    Write-Host $responseText

    $reader.Close()
    $writer.Close()
    $stream.Close()
    $tcp.Close()
} catch {
    Write-Error "TCP error: $_"
    Write-Host "Tip: Use -Restart if game crashed"
    exit 1
}
