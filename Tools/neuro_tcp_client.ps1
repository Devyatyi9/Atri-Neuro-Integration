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
#>
param(
    [string]$Code,
    [switch]$Restart,
    [switch]$Kill,
    [int]$Port = 18700
)

$GameExe = "D:\Games\ATRI-TestVersion\ATRI-MyDearMoments-.exe"
$GameProcessName = "ATRI-MyDearMoments-"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$PluginDll = Join-Path $RepoRoot "out\build\win32-prototype-debug\Debug\Neuro-Integration.dll"
$GameDir = "D:\Games\ATRI-TestVersion"

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

    Write-Host "Starting game..."
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
