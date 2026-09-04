[CmdletBinding()]
param(
    # Scenes to visit, e.g. b101.ks. Default: every scene in the decompiled tree.
    [string[]]$Scenes,
    # Where the decompiled scenes live, used only to build the default list.
    [string]$SceneDir = "D:\Games\ATRI-TestVersion\ks-scn-decompiled",
    # Give up on a scene after this long and move to the next one.
    [int]$SceneTimeoutSec = 900,
    # How often to drain the collector and check progress.
    [int]$PollMs = 700,
    [string]$OutFile,
    # Leave the sped-up playback settings in place when the sweep ends.
    [switch]$KeepSettings
)

# Walks the game scene by scene and records every sound it hears, so the result
# can be compared against what the scenario files declare.
#
# Scenes are entered by an explicit jump rather than by letting the story run on:
# that keeps the sweep away from choices, which would otherwise send it down one
# branch and silently skip the rest.
#
# Playback is sped up rather than skipped. Skip suppresses one-shot SE entirely
# (design/audio-context.md), so a skipped sweep would only ever see the polled
# sources.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$client = Join-Path $PSScriptRoot "neuro_tcp_client.ps1"

function Invoke-Tjs([string]$Code) {
    # neuro_tcp_client.ps1 prints the reply with Write-Host and returns nothing,
    # so the value has to be taken off the information stream (6) rather than the
    # pipeline. Its own status lines are filtered out; what is left is the reply.
    $lines = & $client -Code $Code 6>&1 | ForEach-Object { "$_" }
    $reply = $lines | Where-Object {
        $_ -notmatch '^(Tip:|Usage:|Starting game|Game started|TCP server|DLL updated|TJS scripts updated|Killing game)'
    }
    $value = ($reply | Select-Object -Last 1)
    # The client prints "(empty)" for an empty reply; callers want an empty string.
    if ($value -eq "(empty)") { return "" }
    $value
}

function Invoke-TjsFile([string]$RelativePath) {
    Invoke-Tjs (Get-Content (Join-Path $root $RelativePath) -Raw)
}

function Get-Position {
    # file, line and the flags the sweep has to react to, in one round trip.
    $raw = Invoke-Tjs '(function(){ var i = kag.getCurrentInfo(); return i.file + "|" + i.line + "|" + (kag.selectShowing ? 1 : 0) + "|" + (kag.autoMode ? 1 : 0) + "|" + (kag.clickWaiting ? 1 : 0); })()'
    $parts = ("" + $raw).Trim() -split '\|'
    if ($parts.Count -lt 5) { return $null }
    [pscustomobject]@{
        File       = $parts[0]
        Line       = [int]$parts[1]
        Choice     = $parts[2] -eq "1"
        AutoMode   = $parts[3] -eq "1"
        ClickWait  = $parts[4] -eq "1"
    }
}

function Read-SweepLog {
    # Drain rather than accumulate: the log is emptied on the game side each time,
    # so a long scene never builds one huge response.
    $raw = Invoke-Tjs '(function(){ var s = global.__sweepLog.join("\n"); global.__sweepLog.clear(); return s; })()'
    if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
    ("" + $raw) -split "`n" | Where-Object { $_.Trim() -ne "" }
}

if (-not $Scenes -or $Scenes.Count -eq 0) {
    if (-not (Test-Path -LiteralPath $SceneDir)) {
        throw "Scene directory not found: $SceneDir. Pass -Scenes explicitly."
    }
    $Scenes = Get-ChildItem -LiteralPath $SceneDir -Directory |
        Where-Object { $_.Name -like "*.ks" } |
        Sort-Object Name |
        ForEach-Object { $_.Name }
}

if (-not $OutFile) {
    $stamp = Get-Date -Format "yyyy-MM-dd-HHmm"
    $OutFile = Join-Path $root "docs-local\research\audio\sweep-$stamp.tsv"
}

Write-Host "Sweeping $($Scenes.Count) scene(s) -> $OutFile"
"scene`tkind`tfile" | Set-Content -LiteralPath $OutFile -Encoding utf8

$applied = $false
try {
    Invoke-TjsFile "Tools\tjs\fast-playback-on.tjs" | Out-Null
    $applied = $true

    foreach ($scene in $Scenes) {
        Write-Host "== $scene"
        Invoke-Tjs ("(function(){ tf.start_storage = `"$scene`"; tf.start_target = `"*start`"; kag.process(`"start.ks`", `"*envstart`"); return 1; })()") | Out-Null

        # Wait for the jump to land before arming: the collector's conductor hook
        # has to go onto the conductor this scene actually runs on.
        $landed = $false
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 250
            $pos = Get-Position
            if ($pos -and $pos.File -eq $scene) { $landed = $true; break }
        }
        if (-not $landed) {
            Write-Warning "$scene : never became the current scene, skipping."
            continue
        }

        Invoke-TjsFile "Tools\tjs\audio-sweep-on.tjs" | Out-Null
        # Sample once right away: a scene sets its ambient loops and music on entry,
        # before any tag has run.
        Invoke-Tjs '(function(){ global.__sweepSample(); return 1; })()' | Out-Null
        Invoke-Tjs '(function(){ kag.autoMode = 1; return 1; })()' | Out-Null

        $deadline = (Get-Date).AddSeconds($SceneTimeoutSec)
        $lastLine = -1
        $stalled = 0
        $count = 0

        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds $PollMs

            foreach ($entry in Read-SweepLog) {
                "$scene`t$entry" | Add-Content -LiteralPath $OutFile -Encoding utf8
                $count++
            }

            $pos = Get-Position
            if (-not $pos) { continue }

            if ($pos.File -ne $scene) {
                Write-Host "   left the scene at $($pos.File):$($pos.Line)"
                break
            }
            if ($pos.Choice) {
                # A choice would fork the story. The sweep visits scenes directly,
                # so it stops here rather than picking a branch.
                Write-Host "   choice on screen, moving on"
                break
            }
            if ($pos.Line -eq $lastLine) {
                $stalled++
                if ($stalled -ge 25) {
                    Write-Host "   stopped advancing at line $($pos.Line), moving on"
                    break
                }
                # Auto gets cancelled by things like a click or a dialog; put it back.
                if (-not $pos.AutoMode) {
                    Invoke-Tjs '(function(){ kag.autoMode = 1; return 1; })()' | Out-Null
                }
                # Auto does not pass a full stop: the scene sits with clickWaiting
                # set until something clicks. Nudge it the way the engine does.
                if ($pos.ClickWait -or $stalled -ge 4) {
                    Invoke-Tjs '(function(){ kag.onPrimaryClick(1); return 1; })()' | Out-Null
                }
            } else {
                $stalled = 0
                $lastLine = $pos.Line
            }
        }

        foreach ($entry in Read-SweepLog) {
            "$scene`t$entry" | Add-Content -LiteralPath $OutFile -Encoding utf8
            $count++
        }
        Write-Host "   $count entries"
    }
} finally {
    # Leave the game standing still rather than running on by itself: the sweep
    # turned auto on, and with the player's own page wait restored it would sit
    # there advancing every few seconds, which reads as a hang.
    Invoke-Tjs '(function(){ kag.autoMode = 0; return 1; })()' | Out-Null
    Invoke-TjsFile "Tools\tjs\audio-sweep-off.tjs" | Out-Null
    if ($applied -and -not $KeepSettings) {
        Invoke-TjsFile "Tools\tjs\fast-playback-off.tjs" | Out-Null
    }
    Write-Host "Done -> $OutFile"
}
