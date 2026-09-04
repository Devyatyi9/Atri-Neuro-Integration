#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Plays a voice line, sound effect or music track in the running game.
.DESCRIPTION
    For listening to a file by name without finding it in the story first --
    identifying what a name actually sounds like, checking that an asset is
    really there, comparing two takes.

    Nothing here is a general "play a file" call: each kind of sound has its own
    buffer with its own signature, and they are not interchangeable.

      voice   kag.voices[0].open(<name>.opus) then play()
      se      kag.se[0].play(%[ storage: <name> ])
      bgm     kag.bgm.play(%[ storage: <name>, loop: true ])

    Music replaces whatever was playing. -Restore puts the previous track back
    afterwards, which is usually what you want on an instance you are also using.

    Do not point this at an instance running a sweep: the collector polls the
    music and looping-SE layers, so a track played by hand lands in its output as
    if the story had played it.
.PARAMETER Name
    File name without extension, e.g. ATR_b203_044, se171, BGM09.
.PARAMETER Kind
    voice, se, bgm, or auto (the default), which guesses from the name.
.PARAMETER Stop
    Stop that kind instead of playing.
.PARAMETER Restore
    bgm only: play the previous track again once this one has been heard.
.EXAMPLE
    .\play-sound.ps1 ATR_b203_044
    .\play-sound.ps1 BGM09 -Seconds 20 -Restore
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Name,
    [ValidateSet('auto', 'voice', 'se', 'bgm')]
    [string]$Kind = 'auto',
    [switch]$Stop,
    [switch]$Restore,
    # How long to let music play before restoring. Ignored without -Restore.
    [int]$Seconds = 15,
    [int]$Port = 18700
)

$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'neuro_tcp_client.ps1'
$check = Join-Path (Split-Path -Parent $PSScriptRoot) 'tjscheck.exe'

function Invoke-Game([string]$Code) {
    # Syntax first, always: the live runtime is not a syntax checker, and a
    # malformed expression sent to it is a fault inside the game rather than a
    # message here.
    if (Test-Path -LiteralPath $check) {
        $out = & $check --expression $Code 2>&1
        if ($LASTEXITCODE -ne 0) { throw "tjscheck rejected the command: $out" }
    }
    $lines = & $client -Code $Code -Port $Port 6>&1 | ForEach-Object { "$_" }
    ($lines | Where-Object { $_ -notmatch '^(Tip:|Usage:|Starting game|TCP server)' } | Select-Object -Last 1)
}

if ($Kind -eq 'auto') {
    $Kind = if ($Name -match '^(?i)bgm') { 'bgm' }
            elseif ($Name -match '^[A-Za-z]{3}_b[0-9]{3}_') { 'voice' }
            else { 'se' }
}

if ($Stop) {
    $code = switch ($Kind) {
        'voice' { '(function(){ kag.voices[0].stop(); return "voice stopped"; })()' }
        'se'    { '(function(){ kag.se[0].stop(); return "se stopped"; })()' }
        'bgm'   { '(function(){ kag.bgm.stop(); return "bgm stopped"; })()' }
    }
    Invoke-Game $code
    return
}

switch ($Kind) {
    'voice' {
        $code = "(function(){ var b = kag.voices[0]; b.stop(); b.open(`"$Name.opus`"); b.play(); return `"voice `" + b.storage + `" status=`" + b.status; })()"
        Invoke-Game $code
    }
    'se' {
        $code = "(function(){ kag.se[0].play(%[ storage: `"$Name`" ]); return `"se $Name status=`" + kag.se[0].status; })()"
        Invoke-Game $code
    }
    'bgm' {
        $previous = $null
        if ($Restore) {
            $previous = (Invoke-Game '(function(){ return kag.bgm.playingStorage; })()')
            Write-Host "was playing: $previous"
        }
        $code = "(function(){ kag.bgm.play(%[ storage: `"$Name`", loop: true ]); return `"bgm `" + kag.bgm.playingStorage; })()"
        Invoke-Game $code
        if ($Restore -and $previous) {
            Start-Sleep -Seconds $Seconds
            Invoke-Game "(function(){ kag.bgm.play(%[ storage: `"$previous`", loop: true ]); return `"restored `" + kag.bgm.playingStorage; })()"
        }
    }
}
