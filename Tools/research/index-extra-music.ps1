param(
    [string]$SoundListPath = 'D:\Games\ATRI-TestVersion\extract_dataxp3\main\soundlist.csv',
    [string]$OutputDirectory = "$PSScriptRoot\..\..\docs-local\research\audio"
)

$ErrorActionPreference = 'Stop'

$titles = @{}
foreach ($line in Get-Content -LiteralPath $SoundListPath -Encoding utf8) {
    if ($line -match '^bgm(\d+),(.+)$') {
        $titles[('BGM{0:d2}' -f [int]$matches[1])] = $matches[2]
    }
}

$durations = @(116149, 123807, 113914, 119142, 129672, 144910, 110585, 141000, 120584, 118199, 131052, 113705, 173793)
$tracks = for ($index = 0; $index -lt $durations.Count; $index++) {
    $storage = 'BGM{0:d2}' -f ($index + 1)
    [pscustomobject]@{
        ExtraItem = "sitem$index"
        Storage = $storage
        JapaneseTitle = $titles[$storage]
        DurationMs = $durations[$index]
        RuntimeVerified = $true
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$tracks | Export-Csv -NoTypeInformation -Encoding utf8BOM -LiteralPath (Join-Path $OutputDirectory 'extra-music-track-map.csv')

$markdown = @(
    '# Extra Music Track Map',
    '',
    'Runtime-verified 2026-08-19 through the native `cgmode` `sitemN` handlers. Japanese titles come from `main/soundlist.csv`; the Extra UI uses localized image labels, whose text is not available in the KAG data.',
    '',
    '| Extra item | Storage | Japanese title | Duration (ms) |',
    '| --- | --- | --- | --- |'
)
$markdown += $tracks | ForEach-Object { "| $($_.ExtraItem) | $($_.Storage) | $($_.JapaneseTitle) | $($_.DurationMs) |" }
$markdown | Set-Content -Encoding utf8BOM -LiteralPath (Join-Path $OutputDirectory 'extra-music-track-map.md')
