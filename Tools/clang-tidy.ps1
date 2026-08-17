[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$compileCommandsDir = Join-Path $repoRoot "build\clangd"
$compileCommandsPath = Join-Path $compileCommandsDir "compile_commands.json"

try {
    $clangTidyCommand = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($clangTidyCommand) {
        $clangTidy = $clangTidyCommand.Source
    } elseif ($env:CLANG_TIDY) {
        if (-not (Test-Path -LiteralPath $env:CLANG_TIDY -PathType Leaf)) {
            throw "CLANG_TIDY does not point to a file: $env:CLANG_TIDY"
        }
        $clangTidy = (Resolve-Path -LiteralPath $env:CLANG_TIDY).Path
    } else {
        throw "clang-tidy was not found in PATH and CLANG_TIDY is not set."
    }

    if (-not (Test-Path -LiteralPath $compileCommandsPath -PathType Leaf)) {
        throw "Compilation database not found: $compileCommandsPath. Run the Configure: clangd Database task first."
    }

    $compileCommands = Get-Content -LiteralPath $compileCommandsPath -Raw | ConvertFrom-Json
    $sourcePattern = "(?i)^(src|prototype)/.*\.(c|cc|cpp|cxx)$"
    $files = @($compileCommands.file | Where-Object {
        $relativePath = [System.IO.Path]::GetRelativePath($repoRoot, $_).Replace("\", "/")
        $relativePath -match $sourcePattern
    } | Sort-Object -Unique)

    if ($files.Count -eq 0) {
        throw "No first-party translation units were found in $compileCommandsPath."
    }

    $failedFiles = @()
    foreach ($file in $files) {
        Write-Host "Checking $file"
        & $clangTidy $file "-p=$compileCommandsDir" "--quiet"
        if ($LASTEXITCODE -ne 0) {
            $failedFiles += $file
        }
    }

    if ($failedFiles.Count -gt 0) {
        throw "clang-tidy failed for $($failedFiles.Count) file(s):`n$($failedFiles -join "`n")"
    }

    Write-Host "Checked $($files.Count) first-party translation unit(s)."
} catch {
    Write-Error $_
    exit 1
}
