[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

try {
    $clangFormatCommand = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($clangFormatCommand) {
        $clangFormat = $clangFormatCommand.Source
    } elseif ($env:CLANG_FORMAT) {
        if (-not (Test-Path -LiteralPath $env:CLANG_FORMAT -PathType Leaf)) {
            throw "CLANG_FORMAT does not point to a file: $env:CLANG_FORMAT"
        }
        $clangFormat = (Resolve-Path -LiteralPath $env:CLANG_FORMAT).Path
    } else {
        throw "clang-format was not found in PATH and CLANG_FORMAT is not set."
    }

    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if (-not $gitCommand) {
        throw "git was not found in PATH."
    }

    $relativeFiles = @(& $gitCommand.Source -C $repoRoot ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed with exit code $LASTEXITCODE."
    }

    $excludedRoots = @(
        "libneurosdk/",
        "KiriKiri-Test/krkr-sdk/",
        "KiriKiri-Test/ncbind/",
        "KiriKiri-Test/dear-imgui/",
        "KiriKiri-Test/MinHook/"
    )
    $sourceExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
    $generatedDirectoryPattern = "(^|/)(build|builds|generated|out|bin|obj|CMakeFiles|Debug|Release|RelWithDebInfo|MinSizeRel|_build|cmake-build-[^/]+)(/|$)"

    $files = foreach ($relativeFile in $relativeFiles) {
        $normalizedPath = $relativeFile.Replace("\", "/")
        $excluded = $false
        foreach ($excludedRoot in $excludedRoots) {
            if ($normalizedPath.StartsWith($excludedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                $excluded = $true
                break
            }
        }

        if (-not $excluded -and
            $normalizedPath -notmatch $generatedDirectoryPattern -and
            $sourceExtensions -contains [System.IO.Path]::GetExtension($normalizedPath).ToLowerInvariant()) {
            Join-Path $repoRoot $relativeFile
        }
    }

    if ($files.Count -eq 0) {
        Write-Host "No C/C++ files found."
        exit 0
    }

    $arguments = if ($Check) { @("--dry-run", "--Werror") } else { @("-i") }
    $failedFiles = @()
    foreach ($file in $files) {
        & $clangFormat @arguments -- $file
        if ($LASTEXITCODE -ne 0) {
            $failedFiles += $file
        }
    }

    if ($failedFiles.Count -gt 0) {
        throw "clang-format failed for $($failedFiles.Count) file(s):`n$($failedFiles -join "`n")"
    }

    $action = if ($Check) { "Checked" } else { "Formatted" }
    Write-Host "$action $($files.Count) file(s)."
} catch {
    Write-Error $_
    exit 1
}
