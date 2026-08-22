<#
.SYNOPSIS
    Copies the x86 AddressSanitizer runtime next to the game executable.
.DESCRIPTION
    An ASan-instrumented DLL loads clang_rt.asan_dynamic-i386.dll at startup and
    finds it the way Windows finds any dependency: beside the executable that
    loaded it, which here is the game, not our build output. The alternative is
    putting the compiler's Hostx64\x86 directory on PATH for the game process,
    which is easy to forget and hard to see when it is missing -- the game just
    fails to start with no message.

    Run this once after configuring the ASan preset, and again if Visual Studio
    updates its toolset.
.PARAMETER GameDirectory
    Where the game executable lives.
#>
param(
    [string]$GameDirectory = "D:\Games\ATRI-TestVersion"
)

$runtimeName = "clang_rt.asan_dynamic-i386.dll"

$roots = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC"
) | Where-Object { Test-Path $_ }

if (-not $roots) {
    Write-Error "No Visual Studio 2022 MSVC toolset directory found."
    exit 1
}

# Newest toolset first: the runtime has to match the compiler that instrumented
# the DLL, and a stale copy fails at load time rather than at build time.
$candidate = Get-ChildItem -Path $roots -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x86\$runtimeName" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if (-not $candidate) {
    Write-Error "$runtimeName not found. Install the 'C++ AddressSanitizer' component in the Visual Studio installer."
    exit 1
}

if (-not (Test-Path $GameDirectory)) {
    Write-Error "Game directory not found: $GameDirectory"
    exit 1
}

Copy-Item -Path $candidate -Destination $GameDirectory -Force
Write-Host "Copied $runtimeName"
Write-Host "  from: $candidate"
Write-Host "    to: $GameDirectory"
