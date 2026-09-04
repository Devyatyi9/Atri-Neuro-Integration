[CmdletBinding()]
param(
    # Scenes to visit, e.g. b101.ks. Default: every scene in the decompiled tree.
    [string[]]$Scenes,
    # Where the decompiled scenes live, used only to build the default list.
    [string]$SceneDir = "D:\Games\ATRI-TestVersion\ks-scn-decompiled",
    # Safety net only -- a scene normally ends by the current scenario changing.
    # 0 means work it out from the scene's length.
    [int]$SceneTimeoutSec = 0,
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
    # The conductor says what it is doing, which beats guessing from a timer.
    # status: 0 stopped, 1 running, 2 waiting on a condition (Conductor.tjs).
    # In state 2 the conditions themselves are listed in waitUntil, so "waiting
    # for a click" is told apart from "playing a transition" by name.
    $raw = Invoke-Tjs '(function(){ var i = kag.getCurrentInfo(); var c = kag.conductor; var w = ""; if (typeof c.waitUntil != "undefined") { w = Scripts.getObjectKeys(c.waitUntil).join(","); } return i.file + "|" + i.line + "|" + (kag.selectShowing ? 1 : 0) + "|" + (kag.autoMode ? 1 : 0) + "|" + (kag.clickWaiting ? 1 : 0) + "|" + (kag.isMoviePlaying() ? 1 : 0) + "|" + c.status + "|" + w; })()'
    $parts = ("" + $raw).Trim() -split '\|'
    if ($parts.Count -lt 7) { return $null }
    [pscustomobject]@{
        File       = $parts[0]
        Line       = [int]$parts[1]
        Choice     = $parts[2] -eq "1"
        AutoMode   = $parts[3] -eq "1"
        ClickWait  = $parts[4] -eq "1"
        Movie      = $parts[5] -eq "1"
        Status     = [int]$parts[6]
        WaitOn     = if ($parts.Count -ge 8) { $parts[7] } else { "" }
    }
}

function Get-ChoiceCount {
    # How many options the choice actually offers. Not always two: availability
    # depends on progress flags, so the count is read rather than assumed.
    $raw = Invoke-Tjs '(function(){ var l = kag.selectLayer; if (l == void || l.selects == void) return 0; var n = 0; for (var i = 0; i < l.selects.count; i++) { if (isvalid l.selects[i]) n++; } return n; })()'
    $n = 0
    if ([int]::TryParse(("" + $raw).Trim(), [ref]$n)) { return $n }
    0
}

function Select-Choice([int]$Index) {
    Invoke-Tjs "(function(){ var l = kag.selectLayer; var b = l.selects[$Index]; if (!isvalid b) return 0; l.onButtonClick(b); return 1; })()"
}

function Get-SceneTimeout([string]$Scene) {
    # The real end-of-scene signal is the current scenario changing; this is only
    # the fallback for a scene that never gets there. A flat number is wrong in
    # both directions -- 420 s truncated b102, while short scenes would sit idle
    # for a quarter of an hour.
    #
    # Length is taken from the decompiled file's size rather than by parsing it:
    # across all 34 scenes the ratio holds between 1074 and 1302 bytes per line
    # (1173 on average), which is far more accuracy than a safety net needs.
    if ($SceneTimeoutSec -gt 0) { return $SceneTimeoutSec }
    $path = Join-Path (Join-Path $SceneDir $Scene) "$Scene.json"
    if (-not (Test-Path -LiteralPath $path)) { return 900 }
    $lines = (Get-Item -LiteralPath $path).Length / 1173
    # Measured playback is about 4.2 lines a second; three times that is room for
    # transitions and stops without letting a stuck scene hold the sweep for long.
    $seconds = [int]($lines / 4.2 * 3)
    [Math]::Min([Math]::Max($seconds, 240), 2400)
}

function Get-ChoiceTargets {
    # While a choice is on screen, f.selectInfos describes its options, and each
    # carries the label its branch starts at. Those labels are the other entry
    # points into this scene, which is how the sweep covers both branches without
    # touching progress flags (research/tjs/objects/Flags.md, "Выборы").
    $raw = Invoke-Tjs '(function(){ if (typeof f.selectInfos == "undefined") return ""; var s = f.selectInfos; var r = ""; for (var i = 0; i < s.count; i++) { var e = s[i]; if (typeof e.target == "undefined") continue; r += e.target + " "; } return r; })()'
    ("" + $raw).Trim() -split '\s+' | Where-Object { $_ -like '*' -and $_ -ne "" }
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
      # A scene with a choice is not one run but several: the branches are just
      # other labels to enter at. The queue starts with the normal entry and grows
      # when a choice reveals where its options lead.
      $entryQueue = [System.Collections.Generic.Queue[string]]::new()
      $entryQueue.Enqueue("*start")
      $entered = @{}

      while ($entryQueue.Count -gt 0) {
        $entry = $entryQueue.Dequeue()
        if ($entered.ContainsKey($entry)) { continue }
        $entered[$entry] = $true
        $tag = if ($entry -eq "*start") { $scene } else { "$scene$entry" }

        Write-Host "== $scene $entry"
        Invoke-Tjs ("(function(){ tf.start_storage = `"$scene`"; tf.start_target = `"$entry`"; kag.process(`"start.ks`", `"*envstart`"); return 1; })()") | Out-Null

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

        $timeout = Get-SceneTimeout $scene
        $deadline = (Get-Date).AddSeconds($timeout)
        $lastLine = -1
        $stalled = 0
        $count = 0

        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds $PollMs

            $fresh = 0
            foreach ($row in Read-SweepLog) {
                "$tag`t$row" | Add-Content -LiteralPath $OutFile -Encoding utf8
                $count++
                $fresh++
            }
            # Sound arriving is proof the scene is alive even while the line stands
            # still. Without this a long animation -- a background pan holding one
            # line for many seconds -- looks exactly like a stuck scene and gets
            # abandoned mid-way.
            if ($fresh -gt 0) { $stalled = 0 }

            $pos = Get-Position
            if (-not $pos) { continue }

            if ($pos.File -ne $scene) {
                Write-Host "   left the scene at $($pos.File):$($pos.Line)"
                break
            }
            if ($pos.Choice) {
                # Note where the other options lead before taking one: once the
                # choice is pressed it disappears, and with it the only place the
                # branch labels are readable.
                $options = Get-ChoiceCount
                $targets = @(Get-ChoiceTargets)
                foreach ($target in $targets) {
                    if (-not $entered.ContainsKey($target)) { $entryQueue.Enqueue($target) }
                }
                "$tag`tchoice`t$options options -> $($targets -join ',')" | Add-Content -LiteralPath $OutFile -Encoding utf8
                Write-Host "   choice with $options option(s) -> $($targets -join ',')"
                [void](Select-Choice 0)
                # Pressing an option lands on that option's label, so this run
                # already covers that branch. Mark it taken, or the queue would
                # replay it as if it had never been seen.
                Start-Sleep -Milliseconds 800
                $landedLabel = ("" + (Invoke-Tjs '(function(){ return "" + kag.getCurrentInfo().label; })()')).Trim()
                if ($landedLabel -ne "") {
                    $entered[$landedLabel] = $true
                    Write-Host "   took $landedLabel"
                }
                $stalled = 0
                continue
            }
            if ($pos.Movie) {
                # OP and ED run minutes of video while the scenario stands still.
                # Their audio is inside the movie file, not on the sound layers, so
                # sitting through them collects nothing -- click past instead. The
                # stall counter is reset so the wait for the movie to actually end
                # is not mistaken for a stuck scene.
                Invoke-Tjs '(function(){ kag.onPrimaryClick(1); return 1; })()' | Out-Null
                $stalled = 0
                continue
            }

            # Ask the engine instead of inferring. Running means it is working;
            # waiting on anything that is not input means it will get there by
            # itself -- a transition or an animation holding one line is normal,
            # not a stall.
            $needsInput = $pos.ClickWait -or ($pos.WaitOn -split ',' | Where-Object { $_ -eq 'click' })
            if ($pos.Status -eq 1 -or ($pos.Status -eq 2 -and -not $needsInput)) {
                $stalled = 0
                if ($pos.Line -ne $lastLine) { $lastLine = $pos.Line }
                continue
            }

            if ($pos.Line -eq $lastLine) {
                $stalled++
                # Generous on purpose: a silent animation can hold one line for a
                # good while, and cutting it short loses the rest of the scene.
                # The click nudges below start much earlier, so anything that is
                # merely waiting for input is already moving by now.
                if ($stalled -ge 45) {
                    Write-Warning "$tag : no progress and no sound for $([int]($stalled * $PollMs / 1000))s at line $($pos.Line), moving on"
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

        if ((Get-Date) -ge $deadline) {
            # Worth saying out loud: a truncated run looks exactly like a complete
            # one in the output file, and the missing sounds get blamed on the
            # capture instead of the clock.
            Write-Warning "$tag : hit the ${timeout}s safety timeout -- the scene did not finish, so its tail is missing"
        }

        foreach ($row in Read-SweepLog) {
            "$tag`t$row" | Add-Content -LiteralPath $OutFile -Encoding utf8
            $count++
        }
        Write-Host "   $count entries"
      }
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
