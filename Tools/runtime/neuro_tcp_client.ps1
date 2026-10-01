#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Sends TJS code to the plugin over TCP or controls the game.
.PARAMETER Code
    TJS code to execute. Reads from stdin when omitted.
.PARAMETER Restart
    Restart the copy named by -GameExe, leaving any other instance alone. The
    DLL is only replaced if no instance is holding it, so this is the mode for
    bringing back an instance that died or wedged.
.PARAMETER Deploy
    Close every instance first, then deploy and start the one named by -GameExe.
    A new build cannot be deployed any other way: all copies load the same DLL
    from the same folder, and it is locked while any of them runs.
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
    [switch]$Deploy,
    [switch]$Kill,
    [int]$Port = 18700,
    [string]$GameExe = "D:\Games\ATRI-TestVersion\ATRI-MyDearMoments-.exe",
    [string]$WsUrl = "ws://127.0.0.1:8000",
    # Extra arguments for the game, passed through as typed. The plugin reads
    # its own -neuro-<key>=<value> ones straight from the command line, which is
    # the only way to set something that matters before the config is read.
    [string[]]$GameArgs = @(),
    # procdump watches for the game and writes a full dump on an unhandled
    # exception. Two crashes were lost before this was wired in, because the
    # tool has to be running *before* the fault, and remembering it by hand does
    # not work. -NoDump opts out.
    [switch]$NoDump,
    # With -Restart: deletes Neuro's notebook pages before the game starts, for
    # a clean test. Never by default -- they may be what she wrote in a real run.
    [switch]$ClearNotebooks
)

# Both derived from -GameExe, so pointing at the copy targets only the copy: the
# two have different process names, and killing by name would otherwise be a
# coin toss over which instance dies.
$GameProcessName = [System.IO.Path]::GetFileNameWithoutExtension($GameExe)
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PluginDll = Join-Path $RepoRoot "build\prototype\Debug\Neuro-Integration.dll"
$GameDir = Split-Path -Parent $GameExe
$ProcDump = "C:\ProgramData\procdump\procdump.exe"
# The folder the engine writes its own crash dumps into. A temporary folder gets
# emptied, and a dump is only worth taking if it is still there next week.
$DumpDir = Join-Path $env:APPDATA "Aniplex\ATRI-MyDearMoments-"

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

function Restart-Game([switch]$CloseEveryInstance) {
    # Two modes, because they want opposite things. Bringing back a wedged
    # instance must not disturb the other one, and that is -Restart. Deploying a
    # new build must close them all, because every copy loads the same DLL from
    # the same folder and it stays locked while any of them runs -- otherwise the
    # copy quietly starts on yesterday's build.
    $procs = if ($CloseEveryInstance) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($GameExe)
        $family = ($stem -split '_')[0]
        Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -like "$family*" }
    } else {
        Get-Process -Name $GameProcessName -ErrorAction SilentlyContinue
    }
    foreach ($p in $procs) {
        Write-Host "Killing game process $($p.ProcessName) (PID $($p.Id))..."
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
        # Both copies of the game load the same DLL from the same folder, so
        # while either is running the file is locked and cannot be replaced.
        # That is not a reason to refuse the restart: the instance being
        # restarted still comes back, just on the DLL already deployed. Said out
        # loud, because a silent skip here would mean testing yesterday's build.
        try {
            Copy-Item -LiteralPath $PluginDll -Destination $targetDll -Force -ErrorAction Stop
            Write-Host "DLL updated: $PluginDll -> $targetDll"
        } catch {
            $holders = Get-Process -ErrorAction SilentlyContinue |
                Where-Object { $_.ProcessName -like 'ATRI*' -and $_.ProcessName -ne $GameProcessName } |
                ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }
            Write-Warning "DLL NOT updated -- the file is held by another instance: $($holders -join ', ')"
            Write-Warning "Starting on the DLL already in $GameDir. Close the other instance first to deploy a new build."
        }
    }

    # The TJS the plugin loads at run time lives beside the game, not in the
    # DLL, so a restart has to bring it along the way it brings the build.
    # Editing these in the repository alone changed nothing in the game, and
    # that cost a full session before anyone noticed the deployed copy was days
    # old.
    $scriptSource = Join-Path $RepoRoot "integration-assets\neuro-assets"
    if (Test-Path $scriptSource) {
        $scriptTarget = Join-Path $GameDir "neuro-assets"
        if (-not (Test-Path $scriptTarget)) {
            New-Item -ItemType Directory -Path $scriptTarget | Out-Null
        }
        # Recursive, and the folders come along: the scripts are loaded by
        # path (neuro-assets/install/hooks.tjs), so a flat copy would leave
        # the plugin asking for files that are not there.
        # images\ holds pictures stored without an extension (read through neuro://),
        # so everything there goes along, not only .png.
        $scripts = Get-ChildItem -Path $scriptSource -File -Recurse |
            Where-Object { $_.Extension -in '.tjs', '.png', '.tsv' -or $_.Directory.Name -eq 'images' }
        foreach ($script in $scripts) {
            $relative = $script.FullName.Substring($scriptSource.Length).TrimStart('\')
            $destination = Join-Path $scriptTarget $relative
            $folder = Split-Path -Parent $destination
            if (-not (Test-Path $folder)) {
                New-Item -ItemType Directory -Path $folder -Force | Out-Null
            }
            Copy-Item -LiteralPath $script.FullName -Destination $destination -Force
        }
        Write-Host "Assets updated: $($scripts.Count) file(s) -> $scriptTarget"
    }

    if ($ClearNotebooks) {
        $notebooks = Join-Path $GameDir "neuro-assets\notebooks"
        if (Test-Path $notebooks) {
            Remove-Item -LiteralPath $notebooks -Recurse -Force
            Write-Host "Notebooks cleared: $notebooks"
        }
    }

    if (-not $NoDump) {
        # Before the game starts: -w waits for the next launch, so arming it
        # afterwards would watch nothing.
        Start-CrashWatcher
    }

    Write-Host "Starting game..."
    $env:NEURO_WS_URL = $WsUrl
    if ($GameArgs.Count -gt 0) {
        Write-Host "Game arguments: $($GameArgs -join ' ')"
        Start-Process -FilePath $GameExe -WorkingDirectory $GameDir -ArgumentList $GameArgs
    } else {
        Start-Process -FilePath $GameExe -WorkingDirectory $GameDir
    }
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
if ($Deploy) { Restart-Game -CloseEveryInstance; exit 0 }
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
