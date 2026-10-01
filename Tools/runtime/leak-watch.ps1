#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Plays a scene on the second copy of the game in the engine's object-tracking
    mode, and restarts it while nothing odd happens.
.DESCRIPTION
    The memory wall of a long run comes and goes: the same route reached it in one
    run and not in the next. This waits for it on the copy, which runs with
    -debug=yes (every TJS object remembers where it was created, and what is still
    alive is reported when the game exits).

    Each round starts the copy clean, enters -Scene, turns the speed knobs up and
    auto mode on, and samples the game's private bytes. A jump of -JumpMb within
    five minutes, or a "could not allocate" in the game's log, ends the watch with
    the game left alive and a full dump and heap snapshot taken from it. A round
    that reaches -Minutes without that is ended with a normal exit and the next
    one begins.
.PARAMETER Scene
    The scenario to enter from the title, e.g. b111.ks. The jump in the one run
    that hit the wall began inside b112, so b111 covers it.
.PARAMETER Minutes
    How long one round runs before a normal exit.
.PARAMETER JumpMb
    How much private memory may be gained within five minutes before the watch
    stops and takes its dump.
.PARAMETER MaxRounds
    How many rounds at most.
.PARAMETER Mode
    manual (the default) clicks the story on by itself, as the scripted engine's
    plain mode does: a scene takes about two minutes. auto leaves the game to turn
    its own pages, which takes about twelve.
#>
param(
    [string]$Scene = "b111.ks",
    [double]$Minutes = 25,
    [int]$JumpMb = 150,
    [int]$MaxRounds = 12,
    [ValidateSet("manual", "auto")][string]$Mode = "manual",
    [string]$ProcDump = "C:\ProgramData\procdump\procdump.exe"
)

$Client = Join-Path $PSScriptRoot "neuro_tcp_client.ps1"
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$CopyExe = "D:\Games\ATRI-TestVersion\ATRI-MyDearMoments-_Copy.exe"
$CopyLog = "D:\Games\ATRI-TestVersion\instance-copy\game_debug_log.txt"
$Port = 18701
$DumpDir = Join-Path $env:APPDATA "Aniplex\ATRI-MyDearMoments-"
$SampleSeconds = 15
$WindowSeconds = 300

# One click on a story waiting for one -- the conditions the plugin's advance action
# checks -- and a click on a movie, which waits for nothing.
$Advance = '(function(){ if (SystemHook.currentState != "game") return 0;' +
    ' if (kag.isMoviePlaying()) { runOnceDelayedTrigger(function { kag.onPrimaryClick(); } incontextof global); return 2; }' +
    ' if (!kag.clickWaiting) return 0; var c = kag.conductor;' +
    ' if (c.status != c.mWait || c.waitUntil["click"] === void) return 0;' +
    ' runOnceDelayedTrigger(function { kag.onPrimaryClick(); } incontextof global); return 1; })()'

function Say($text) { Write-Host ("{0} {1}" -f (Get-Date -Format "HH:mm:ss"), $text) }

# Whatever the client prints, as one string.
function Send([string]$code) {
    (& $Client -Port $Port -Code $code 6>&1 | Out-String).Trim()
}

function CopyProcess { Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($CopyExe)) -ErrorAction SilentlyContinue }

function PrivateMb($proc) { [math]::Round((Get-Process -Id $proc.Id).PrivateMemorySize64 / 1MB) }

function Start-Round {
    & $Client -Restart -GameExe $CopyExe -Port $Port -WsUrl "ws://127.0.0.1:8000" -GameArgs "-debug=yes" -NoDump 6>&1 | Out-Null
    Start-Sleep 25
    $enter = '(function(){ if (SystemHook.currentState != "title") return "not title: " + SystemHook.currentState; ' +
             'tf.start_storage = "' + $Scene + '"; tf.start_target = "*start"; ' +
             'kag.process("start.ks", "*envstart"); return "ok"; })()'
    Say ("enter {0}: {1}" -f $Scene, (Send $enter))
    Start-Sleep 9
    # The way into a scene from the title leaves the quick menu unbuilt.
    Send 'kag.QuickMenu.setVisible(%[fadein: 1, notify: 1])' | Out-Null
    $fast = Get-Content (Join-Path $Repo "Tools\tjs\fast-playback-on.tjs") -Raw
    $fast | & $Client -Port $Port 6>&1 | Out-Null
    if ($Mode -eq "auto") {
        Say ("auto mode: " + (Send 'kag.enterAutoMode(), kag.autoMode'))
    } else {
        Say "manual mode: the story is clicked on from here"
    }
}

# A normal exit, which is what makes the engine write its report of live objects.
function Stop-Round {
    Send 'kag.close()' | Out-Null
    Start-Sleep 2
    $answer = 'runOnceDelayedTrigger(function { try { var d = kag.currentDialog; if (typeof d != "Object" || !d) return; ' +
              'var b = d.names["yes"]; if (typeof b == "Object") d.processLink(b.linkNum); } catch (e) {} } incontextof global)'
    Send $answer | Out-Null
    for ($i = 0; $i -lt 40 -and (CopyProcess); $i++) { Start-Sleep 1 }
    if (CopyProcess) { Say "the copy did not exit by itself" } else { Say "the copy exited" }
}

function Take-Evidence($proc, $why) {
    Say "ANOMALY: $why -- game left alive"
    New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null
    & $ProcDump -accepteula -ma $proc.Id $DumpDir 2>&1 | Select-Object -Last 3 | ForEach-Object { Say "procdump: $_" }
    $snapshot = Join-Path $Repo "Tools\runtime\heap-snapshot.py"
    if (Test-Path $snapshot) {
        python $snapshot --port $Port --log $CopyLog 2>&1 | Select-Object -First 12 | ForEach-Object { Say "heap: $_" }
    }
}

for ($round = 1; $round -le $MaxRounds; $round++) {
    Say "round $round of ${MaxRounds}: $Scene for $Minutes min, $Mode mode, jump limit $JumpMb MB"
    Start-Round
    $started = Get-Date
    $samples = New-Object System.Collections.ArrayList
    $logBefore = if (Test-Path $CopyLog) { (Get-Item $CopyLog).Length } else { 0 }
    while (((Get-Date) - $started).TotalMinutes -lt $Minutes) {
        $until = (Get-Date).AddSeconds($SampleSeconds)
        while ((Get-Date) -lt $until) {
            if ($Mode -eq "manual") { Send $Advance | Out-Null; Start-Sleep -Milliseconds 120 } else { Start-Sleep 1 }
        }
        $proc = CopyProcess
        if (-not $proc) { Say "the copy is gone; ending the watch"; return }
        $now = Get-Date
        [void]$samples.Add([pscustomobject]@{ At = $now; Mb = (PrivateMb $proc) })
        $recent = $samples | Where-Object { ($now - $_.At).TotalSeconds -le $WindowSeconds }
        $low = ($recent | Measure-Object Mb -Minimum).Minimum
        $current = $samples[$samples.Count - 1].Mb
        if ($current - $low -ge $JumpMb) {
            Take-Evidence $proc "private $low -> $current MB within $([int]($WindowSeconds / 60)) min"
            return
        }
        if ((Test-Path $CopyLog) -and (Select-String -Path $CopyLog -Pattern "could not allocate" -Quiet)) {
            Take-Evidence $proc "the game's log reports an allocation failure"
            return
        }
    }
    $last = $samples[$samples.Count - 1].Mb
    $peak = ($samples | Measure-Object Mb -Maximum).Maximum
    Say "round $round done: private ended at $last MB, peak $peak MB, nothing odd; exiting normally"
    Stop-Round
}
Say "no anomaly in $MaxRounds round(s)"
