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
    # Restart the game every this many scenes. 0 (the default) means never on a
    # schedule -- the game is only brought back if a scene actually fails.
    #
    # Routine restarts were here first, after a sweep died on
    # "Cannot allocate memory for Bitmap (3840x2160)". Three measurements since
    # then found nothing accumulating: private memory returns to its baseline
    # across scene jumps, the largest contiguous free block does not shrink, and
    # GPU memory sits flat. With no mechanism to point at, restarting on a
    # schedule is cost without a reason -- while recovering from a failure costs
    # nothing until something fails.
    [int]$RestartEvery = 0,
    [string]$OutFile,
    # Leave the sped-up playback settings in place when the sweep ends.
    [switch]$KeepSettings,
    # Which instance to drive. A second copy of the game runs on its own port
    # (see the plugin config), so a sweep can go against it while the first copy
    # is busy. The baseline file is per port for the same reason.
    [int]$Port = 18700
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
    $lines = & $client -Code $Code -Port $Port 6>&1 | ForEach-Object { "$_" }
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

function Format-Row([string]$Entry, [string]$Row) {
    # The collector stamps each event with the scene it happened in, so the row
    # already reads "scene<tab>kind<tab>file". The driver only adds where it
    # asked the game to go -- which is not the same thing, and the difference is
    # the point: it learns of a scene change on its next poll, so the tail of a
    # batch used to be filed under a scene the game had already left.
    $parts = $Row -split "`t"
    if ($parts.Count -ge 3) {
        # getCurrentInfo can come back empty between scenarios; the entry is the
        # only thing left to say where this happened.
        if (-not $parts[0]) { $parts[0] = $Entry }
        return (($parts -join "`t") + "`t$Entry")
    }
    # An older collector, still supported: nothing better than the entry to go on.
    return "$Entry`t$Row`t$Entry"
}

function Get-SpeedSettings {
    $raw = Invoke-Tjs '(function(){ return kag.textspeed + "|" + kag.autospeed + "|" + kag.drawspeed + "|" + kag.autoModeWaitVoice; })()'
    $parts = ("" + $raw).Trim() -split '\|'
    if ($parts.Count -lt 4) { return $null }
    [pscustomobject]@{
        TextSpeed = $parts[0]
        AutoSpeed = $parts[1]
        DrawSpeed = $parts[2]
        WaitVoice = $parts[3]
    }
}

function Set-SpeedSettings($Settings) {
    if (-not $Settings) { return }
    Invoke-Tjs "(function(){ kag.textspeed = $($Settings.TextSpeed); kag.autospeed = $($Settings.AutoSpeed); kag.drawspeed = $($Settings.DrawSpeed); kag.autoModeWaitVoice = $($Settings.WaitVoice); return 1; })()" | Out-Null
}

# The player's settings do not survive a process -- the game starts on its own
# values every time and nothing writes a choice back -- so the only copy that
# matters is the one taken before the first change. Asking the game for it is
# not enough: a run that died before restoring leaves the game sped up, and the
# live values then say nothing about what preceded them. It is kept on disk,
# written the moment it is taken and removed only after it has been handed back.
# Kept next to the driver that owns it, not with the sweep output: this is
# machine state for the next run, not a result. The directory is excluded
# locally (.git/info/exclude), the same way the tool binaries are.
$baselineDir = Join-Path $PSScriptRoot "state"
$baselineFile = Join-Path $baselineDir "sweep-baseline-$Port.json"

function Read-SavedBaseline {
    if (-not (Test-Path -LiteralPath $baselineFile)) { return $null }
    try { Get-Content -LiteralPath $baselineFile -Raw | ConvertFrom-Json } catch { $null }
}

function Save-Baseline($Settings) {
    if (-not $Settings) { return }
    if (-not (Test-Path -LiteralPath $baselineDir)) {
        New-Item -ItemType Directory -Path $baselineDir -Force | Out-Null
    }
    $Settings | ConvertTo-Json | Set-Content -LiteralPath $baselineFile -Encoding utf8
}

function Clear-SavedBaseline {
    Remove-Item -LiteralPath $baselineFile -ErrorAction SilentlyContinue
}

# Recognises the values fast-playback-on sets. Informational only: the game
# starts on exactly these -- measured on a pristine instance, text 100, auto at
# the ceiling, draw 0.1, waitVoice 0 -- so they cannot tell "left fast by a run
# that died" from "just launched". Refusing to record a baseline on this test,
# as an earlier version did, therefore refused on every fresh game. What that
# test was meant to protect against is covered by the copy on disk instead: a
# run that dies leaves its record for the next one.
function Test-IsFastProfile($Settings) {
    if (-not $Settings) { return $false }
    $max = ("" + (Invoke-Tjs '(function(){ return kag._autoSpeedMax; })()')).Trim()
    return ([string]$Settings.TextSpeed -eq "100" -and
            [string]$Settings.DrawSpeed -eq "0.1" -and
            [string]$Settings.AutoSpeed -eq $max)
}

# Where the player's settings come from, best source first.
function Get-PlayerBaseline {
    $saved = Read-SavedBaseline
    if ($saved) {
        Write-Host "player settings taken from the previous run's saved copy (it did not restore them)"
        return $saved
    }

    # fast-playback-on keeps its own snapshot in the game, taken before it
    # changed anything. It is gone after a restart, but while it is there it is
    # what preceded the change and the live values are not.
    $held = ("" + (Invoke-Tjs '(function(){ var o = kag.__neuroSpeedOriginal; if (typeof o != "Object") return ""; return o.textspeed + "|" + o.autospeed + "|" + o.drawspeed + "|" + o.autoModeWaitVoice; })()')).Trim()
    if ($held) {
        $parts = $held -split '\|'
        if ($parts.Count -ge 4) {
            $settings = [pscustomobject]@{
                TextSpeed = $parts[0]; AutoSpeed = $parts[1]
                DrawSpeed = $parts[2]; WaitVoice = $parts[3]
            }
            Write-Host "player settings recovered from the snapshot still held in the game"
            Save-Baseline $settings
            return $settings
        }
    }

    $live = Get-SpeedSettings
    if (Test-IsFastProfile $live) {
        # Almost always a freshly started game rather than a mess left behind,
        # since these are the values it launches with. Recorded either way:
        # putting back what was found is right in both cases.
        Write-Host "note: the game is on its startup speed settings"
    }
    Save-Baseline $live
    return $live
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
    # Filtered by the scene on purpose: selectInfos keeps what earlier choices put
    # there, so an unfiltered read at the second choice scene would queue labels
    # belonging to the first one -- and those labels do not exist here.
    $raw = Invoke-Tjs '(function(){ if (typeof f.selectInfos == "undefined") return ""; var here = "" + kag.getCurrentInfo().file; var s = f.selectInfos; var r = ""; for (var i = 0; i < s.count; i++) { var e = s[i]; if (typeof e.target == "undefined") continue; if (typeof e.storage != "undefined" && ("" + e.storage) != here) continue; r += e.target + " "; } return r; })()'
    ("" + $raw).Trim() -split '\s+' | Where-Object { $_ -like '*' -and $_ -ne "" }
}

function Read-SweepLog {
    # Drain rather than accumulate: the log is emptied on the game side each time,
    # so a long scene never builds one huge response.
    $raw = Invoke-Tjs '(function(){ var s = global.__sweepLog.join("\n"); global.__sweepLog.clear(); return s; })()'
    if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
    # A failed call answers with the client's own error text, and without this it
    # lands in the results as though the game had played a sound by that name.
    ("" + $raw) -split "`n" |
        Where-Object { $_.Trim() -ne "" -and $_ -notmatch '^(error:|!!!TJS_EXCEPTION!!!)' }
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
"scene`tkind`tfile`tentry" | Set-Content -LiteralPath $OutFile -Encoding utf8

$applied = $false
$baseline = $null
try {
    # Only when nothing will restart the game: with restarts on, the settings are
    # applied after each one, and touching the game here would fail outright if it
    # is not currently up (a previous run may have left it dead).
    if ($RestartEvery -le 0) {
        $baseline = Get-PlayerBaseline
        if ($baseline) { Write-Host "player settings: text $($baseline.TextSpeed), auto $($baseline.AutoSpeed), draw $($baseline.DrawSpeed)" }
        Invoke-TjsFile "Tools\tjs\fast-playback-on.tjs" | Out-Null
    }
    $applied = $true

    $sceneNo = 0
    foreach ($scene in $Scenes) {
      # The game is a 32-bit process and jumping between scenes accumulates
      # graphics memory it never gives back. Left alone it dies with
      # "Cannot allocate memory for Bitmap ... (3840x2160)" and a run of D3D
      # allocation failures -- which is what ended the first attempt at this
      # sweep, mid-scene, on a process that had been up for a while. Restarting
      # on a schedule keeps the address space from filling.
      if ($RestartEvery -gt 0 -and ($sceneNo % $RestartEvery) -eq 0) {
          Write-Host "-- restarting the game"
          & $client -Restart -Port $Port 6>&1 | Out-Null
          Start-Sleep -Seconds 3
          # Taken on the first contact and only once; after that the game has
          # already been sped up and asking it again would return that.
          if (-not $baseline) {
              $baseline = Get-PlayerBaseline
              if ($baseline) { Write-Host "   player settings: text $($baseline.TextSpeed), auto $($baseline.AutoSpeed), draw $($baseline.DrawSpeed)" }
          }
          Invoke-TjsFile "Tools\tjs\fast-playback-on.tjs" | Out-Null
      }
      $sceneNo++

      # A scene with a choice is not one run but several: the branches are just
      # other labels to enter at. The queue starts with the normal entry and grows
      # when a choice reveals where its options lead.
      $entryQueue = [System.Collections.Generic.Queue[string]]::new()
      $entryQueue.Enqueue("*start")
      $entered = @{}

      # One scene dying must not end the sweep. The game can run out of memory
      # part way through, and everything already written stays valid -- so note
      # it, bring the game back, and carry on with the next scene.
      try {
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
                (Format-Row $tag $row) | Add-Content -LiteralPath $OutFile -Encoding utf8
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
                "$tag`tchoice`t$options options -> $($targets -join ',')`t$tag" | Add-Content -LiteralPath $OutFile -Encoding utf8
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
            (Format-Row $tag $row) | Add-Content -LiteralPath $OutFile -Encoding utf8
            $count++
        }
        Write-Host "   $count entries"
      }
      } catch {
          # Recovery is not tied to the restart schedule: -RestartEvery says how
          # often to restart when nothing is wrong, while this runs because
          # something is. Tying the two together once turned a single lost scene
          # into a lost three-hour sweep.
          Write-Warning "$scene : run stopped ($($_.Exception.Message))"
          Write-Host "-- bringing the game back"
          try {
              & $client -Restart -Port $Port 6>&1 | Out-Null
              Start-Sleep -Seconds 3
              Invoke-TjsFile "Tools\tjs\fast-playback-on.tjs" | Out-Null
              # Counted as a fresh start, so a schedule, if one is set, does not
              # restart again on the very next scene.
              $sceneNo = 1
          } catch {
              Write-Warning "could not bring the game back, stopping"
              throw
          }
      }
    }
} finally {
    # Cleanup talks to the game, and the game may well be gone -- it can run out
    # of memory mid-sweep, and that is exactly when this block runs. Failing here
    # would bury the real error under a connection one.
    try {
        # Leave the game standing still rather than running on by itself: the sweep
        # turned auto on, and with the player's own page wait restored it would sit
        # there advancing every few seconds, which reads as a hang.
        Invoke-Tjs '(function(){ kag.autoMode = 0; return 1; })()' | Out-Null
        Invoke-TjsFile "Tools\tjs\audio-sweep-off.tjs" | Out-Null
        if ($applied -and -not $KeepSettings) {
            # Written back from the snapshot taken here rather than from the one
            # inside the game: with restarts the in-game copy may describe a
            # process that no longer exists.
            if ($baseline) {
                # Off first, then the disk copy over the top. Off is what clears
                # the snapshot fast-playback-on keeps in the game; left behind,
                # it holds the fast values, and the next run's -on refuses to
                # overwrite an existing snapshot -- so the poison carries over.
                Invoke-TjsFile "Tools\tjs\fast-playback-off.tjs" | Out-Null
                Set-SpeedSettings $baseline
                # Read back rather than assume: the saved copy is dropped only
                # once the game actually holds those values again, so a restore
                # that silently failed leaves the copy for the next run to use.
                $now = Get-SpeedSettings
                if ($now -and [string]$now.TextSpeed -eq [string]$baseline.TextSpeed -and
                    [string]$now.DrawSpeed -eq [string]$baseline.DrawSpeed) {
                    Clear-SavedBaseline
                    Write-Host "Restored player settings: text $($now.TextSpeed), auto $($now.AutoSpeed), draw $($now.DrawSpeed)"
                } else {
                    Write-Warning "The settings did not come back; the saved copy is kept at $baselineFile and the next run will use it."
                }
            } else {
                Invoke-TjsFile "Tools\tjs\fast-playback-off.tjs" | Out-Null
                Write-Warning "No record of the player's settings existed, so the game is left on the fast ones."
            }
        }
    } catch {
        Write-Warning "Cleanup skipped: the game did not answer ($($_.Exception.Message))"
    }
    Write-Host "Done -> $OutFile"
}
