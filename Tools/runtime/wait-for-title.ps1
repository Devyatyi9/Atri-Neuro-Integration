<#
.SYNOPSIS
    Waits until the title screen is genuinely interactive, then reports which
    main-menu buttons are clickable.
.DESCRIPTION
    After -Restart the TCP server answers well before the title exists: the game
    is still playing its startup splashes for several seconds, and an action sent
    in that window fails with "Continue is unavailable." rather than doing
    anything. This polls the same conditions the continue_game action checks, so
    a test can wait for the real thing instead of a fixed sleep.

    Exit code 0 when the requested button is ready, 1 on timeout.
.PARAMETER Button
    Which main-menu button to wait for. "cont" (Continue) also requires a
    suspend-save to exist, which is what SystemAction.canContinue reports.
.PARAMETER TimeoutSeconds
    Give up after this long instead of blocking forever. A cold start measured
    24s here, so 30 is the practical ceiling with headroom; raise it only if the
    game grows a much heavier plugin set.
.EXAMPLE
    .\Tools\runtime\wait-for-title.ps1
    .\Tools\runtime\wait-for-title.ps1 -Button start -TimeoutSeconds 45
#>
param(
    [string]$Button = "cont",
    [int]$TimeoutSeconds = 30,
    [int]$Port = 18700
)

$client = Join-Path $PSScriptRoot "neuro_tcp_client.ps1"

function Invoke-Tjs([string]$code) {
    # neuro_tcp_client.ps1 writes through Write-Host, so its output has to be
    # captured from the information stream rather than the pipeline.
    try {
        return (& $client -Code $code -Port $Port 6>&1 | Out-String).Trim()
    } catch {
        return ""
    }
}

$buttonReadyProbe = @"
(function(){
  if (typeof kag == 'undefined' || typeof kag.current == 'undefined' || typeof kag.current.names == 'undefined') return 0;
  var button = kag.current.names['$Button'];
  if (typeof button == 'undefined' || !isvalid button || !button.visible || !button.enabled) return 0;
  if (typeof button.controlOwner == 'undefined' || typeof button.controlOwner.onButtonClick != 'Object') return 0;
  return 1;
})()
"@ -replace "`r?`n", " "

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastState = ""
while ((Get-Date) -lt $deadline) {
    $state = Invoke-Tjs "typeof SystemHook != 'undefined' ? SystemHook.currentState : ''"
    if ($state -ne $lastState) {
        Write-Host "state: '$state'"
        $lastState = $state
    }

    if ($state -eq "option") {
        # The first-run language screen reports itself as "option" too, and the
        # title never arrives while it is up: waiting the full timeout there
        # only reports "last state: option", which says nothing about why.
        # Detected by its controls, the same way the plugin does it.
        $isLanguageScreen = Invoke-Tjs "(function(){ if (typeof kag.current.names['lang_text'] != 'Object') return 0; if (typeof kag.current.names['title'] == 'Object') return 0; return 1; })()"
        if ($isLanguageScreen -eq "1") {
            Write-Host "BLOCKED: the first-run language screen is up; the title is behind it."
            Write-Host "         Press its OK (kag.current.names['back']) or send the confirm_language action."
            exit 1
        }
    }

    if ($state -eq "title") {
        $ready = Invoke-Tjs $buttonReadyProbe
        # Continue additionally needs a suspend-save to exist.
        $canContinue = "1"
        if ($Button -eq "cont") {
            $canContinue = Invoke-Tjs "typeof SystemAction != 'undefined' && SystemAction.canContinue ? 1 : 0"
        }
        if ($ready -eq "1" -and $canContinue -eq "1") {
            $elapsed = [math]::Round(($TimeoutSeconds - ($deadline - (Get-Date)).TotalSeconds), 1)
            Write-Host "READY: '$Button' is clickable after ${elapsed}s"
            exit 0
        }
    }
    Start-Sleep -Milliseconds 500
}

Write-Host "TIMEOUT: '$Button' not clickable within ${TimeoutSeconds}s (last state: '$lastState')"
exit 1
