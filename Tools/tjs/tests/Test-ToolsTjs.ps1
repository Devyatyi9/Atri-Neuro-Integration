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

# A checked script is looked for in Tools/tjs first, then among the scripts
# deployed into the game. Those are the ones that run unattended on every poll,
# and they had no behaviour coverage at all -- which is where the empty bgm
# field sat unnoticed for as long as it did.
$repoRoot = Split-Path -Parent $toolsDir
$deployed = Join-Path $repoRoot "integration-assets\neuro-assets"

function Resolve-Script([string]$Name) {
    $candidate = Join-Path $toolsTjs "$Name.tjs"
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    if (Test-Path -LiteralPath $deployed) {
        $found = Get-ChildItem -LiteralPath $deployed -Recurse -Filter "$Name.tjs" |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

foreach ($check in $checks) {
    $name = $check.Name -replace '\.checks\.tjs$', ''
    $target = Resolve-Script $name
    if (-not $target) {
        Write-Host "FAIL $name : no script named $name.tjs in Tools\tjs or integration-assets\neuro-assets"
        $failures++
        continue
    }

    # A restore script is only meaningful after its capture script has run, so a
    # <name>.setup file may name the scripts to run first, one per line. Naming
    # them beats copying them: the setup is then the real script, not a stale
    # imitation of it.
    $setup = ""
    $setupList = Join-Path $here "$name.setup"
    if (Test-Path -LiteralPath $setupList) {
        foreach ($line in Get-Content -LiteralPath $setupList) {
            $line = $line.Trim()
            if (-not $line -or $line.StartsWith("#")) { continue }
            $prior = Resolve-Script $line
            if (-not $prior) {
                throw "$name.setup names a script that does not exist: $line.tjs"
            }
            $setup += "(function(){`n" + (Get-Content -LiteralPath $prior -Raw) + "`n})();`n"
        }
    }

    $script = Get-Content -LiteralPath $target -Raw
    # The script's own value is kept: several of these report 1 or 0 to say
    # whether they did anything, and that answer is worth checking.
    $body = $stubs + "`n" + $setup +
            "global.__scriptResult = (function(){`n" + $script + "`n})();`n" +
            (Get-Content -LiteralPath $check.FullName -Raw)

    $temp = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), ".tjs")
    [System.IO.File]::WriteAllText($temp, $body, $utf8NoBom)
    try {
        # --advise here rather than on the file alone: the advisor runs the
        # script before reporting, and most of these do not run without the
        # stubs. It never changes the exit code, so its findings are read out
        # of the output.
        $output = & $Sandbox --advise $temp 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    } finally {
        Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue
    }

    $advice = @($output | Where-Object { $_ -like "advice: *" -and $_ -ne "advice: nothing to report" })
    # The advisor names the composed file, which is a temporary path. Naming the
    # script under test instead would be wrong as often as right: the setup
    # scripts are compiled into the same file, so the code it found may belong
    # to one of those. The run is named, the file is not.
    $advice = @($advice | ForEach-Object { $_ -replace [regex]::Escape($temp), "the composed file" })

    if ($code -ne 0) {
        Write-Host "FAIL $name"
        # Line numbers refer to the composed file, so say how it was composed.
        Write-Host "     $($output -join "`n     ")"
        Write-Host "     (line numbers count from the composed file: stubs, then the script, then the checks)"
        $failures++
    } elseif ($advice.Count -gt 0) {
        Write-Host "FAIL $name  (advice)"
        Write-Host "     $($advice -join "`n     ")"
        $failures++
    } else {
        $said = @($output | Where-Object { $_ -notlike "advice: *" })
        Write-Host "ok   $name  $($said -join ' ')"
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
