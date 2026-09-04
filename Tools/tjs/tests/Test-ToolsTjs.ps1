[CmdletBinding()]
param(
    # tjsbox.exe. Defaults to the copy kept in Tools, then PATH.
    [string]$Sandbox
)

# Runs the scripts in Tools/tjs against stub objects, outside the game.
#
# tjscheck says a script parses. This says it behaves: that the sweep collector
# reads the right layers, that its wrapper calls through, and that it survives
# the states that actually took the game down -- a saved handler gone, a sampler
# cleared while a stale wrapper is still installed. Those are one-line checks
# here and a lost afternoon in the game.
#
# A script under test is wrapped in a function on purpose: ours end with
# `return`, and at top level that would end the whole script before any check
# could run.

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$toolsTjs = Split-Path -Parent $here
$toolsDir = Split-Path -Parent $toolsTjs

if (-not $Sandbox) {
    $candidate = Join-Path $toolsDir "tjsbox.exe"
    if (Test-Path -LiteralPath $candidate) {
        $Sandbox = $candidate
    } else {
        $command = Get-Command tjsbox -ErrorAction SilentlyContinue
        if ($command) { $Sandbox = $command.Source }
    }
}

if (-not $Sandbox -or -not (Test-Path -LiteralPath $Sandbox)) {
    Write-Warning "tjsbox.exe not found. Build it from the krkrz-tjscheck repository (vcproj/tjsbox.vcxproj) and copy it to Tools\, or pass -Sandbox. Skipping."
    exit 0
}
$Sandbox = (Resolve-Path -LiteralPath $Sandbox).Path

$stubs = Get-Content -LiteralPath (Join-Path $here "stubs.tjs") -Raw
$checks = Get-ChildItem -LiteralPath $here -Filter "*.checks.tjs" | Sort-Object Name

if ($checks.Count -eq 0) {
    Write-Warning "No *.checks.tjs files found."
    exit 0
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$failures = 0

foreach ($check in $checks) {
    $name = $check.Name -replace '\.checks\.tjs$', ''
    $target = Join-Path $toolsTjs "$name.tjs"
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "FAIL $name : no script at $target"
        $failures++
        continue
    }

    $script = Get-Content -LiteralPath $target -Raw
    $body = $stubs + "`n(function(){`n" + $script + "`n})();`n" +
            (Get-Content -LiteralPath $check.FullName -Raw)

    $temp = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), ".tjs")
    [System.IO.File]::WriteAllText($temp, $body, $utf8NoBom)
    try {
        $output = & $Sandbox $temp 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    } finally {
        Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue
    }

    if ($code -ne 0) {
        Write-Host "FAIL $name"
        # Line numbers refer to the composed file, so say how it was composed.
        Write-Host "     $($output -join "`n     ")"
        Write-Host "     (line numbers count from the composed file: stubs, then the script, then the checks)"
        $failures++
    } else {
        Write-Host "ok   $name  $($output -join ' ')"
    }
}

if ($failures -gt 0) {
    Write-Host ""
    Write-Host "$failures script(s) failed."
    exit 1
}

Write-Host ""
Write-Host "All Tools/tjs checks passed."
exit 0
