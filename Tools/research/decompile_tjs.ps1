param(
    [string]$InputDir = "D:\Games\ATRI-TestVersion\new-extract-data",
    [string]$OutputDir = "D:\Games\ATRI-TestVersion\data-tjs-decompiled",
    [string]$DecompilerPy = "F:\Users\User\Projects\tjs2-decompiler\tjs2_decompiler.py",
    [string]$Python = "python",
    [string]$Encoding = "utf-16le-bom",
    [switch]$Force
)

if (!(Test-Path $DecompilerPy)) {
    Write-Error "Decompiler not found: $DecompilerPy"
    exit 1
}

$files = Get-ChildItem -Recurse -Filter "*.tjs" $InputDir
$total = $files.Count
$ok = 0
$skipped = 0
$errors = @()

Write-Host "=== TJS2 Batch Decompiler ==="
Write-Host "Input:  $InputDir"
Write-Host "Output: $OutputDir"
Write-Host "Files:  $total"
Write-Host ""

foreach ($file in $files) {
    # Check magic: skip non-binary (text) .tjs files
    $magic = $null
    try {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        if ($bytes.Length -ge 8) {
            $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 8)
        }
    } catch {
        $errors += "$($file.FullName): read error"
        continue
    }

    if ($magic -ne "TJS2100`0") {
        Write-Host "SKIP text: $($file.FullName)"
        $skipped++
        continue
    }

    # Build output path preserving directory structure
    $relPath = $file.FullName.Substring($InputDir.Length).TrimStart('\')
    $outPath = Join-Path $OutputDir $relPath
    $outDir = Split-Path $outPath -Parent

    if (!(Test-Path $outDir)) {
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    }

    if (!$Force -and (Test-Path $outPath)) {
        Write-Host "SKIP exists: ${relPath}"
        $skipped++
        continue
    }

    Write-Host "DECOMPILE: ${relPath}"

    & $Python $DecompilerPy $($file.FullName) -o $outPath 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $errors += "${relPath}: exit $LASTEXITCODE"
        Write-Host "  ERROR: exit $LASTEXITCODE"
    } else {
        $ok++
    }
}

Write-Host ""
Write-Host "=== Done ==="
Write-Host "Decompiled: $ok"
Write-Host "Skipped:    $skipped (text or already exists)"
if ($errors.Count -gt 0) {
    Write-Host "Errors:     $($errors.Count)"
    Write-Host ""
    Write-Host "Errors:"
    $errors | ForEach-Object { Write-Host "  $_" }
}
