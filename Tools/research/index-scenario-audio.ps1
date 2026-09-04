param(
    [string]$SceneRoot = 'D:\Games\ATRI-TestVersion\ks-scn-decompiled',
    [string]$InventoryPath = "$PSScriptRoot\..\..\docs-local\archive\inventory\dataxp3-structure.txt",
    [string]$OutputDirectory = "$PSScriptRoot\..\..\docs-local\research\audio"
)

$ErrorActionPreference = 'Stop'

function Find-AudioReferences {
    param(
        [object]$Value,
        [string]$Scene,
        [string]$Label,
        [string]$TitleJapanese,
        [string]$TitleEnglish,
        [string]$TitleChineseSimplified,
        [string]$TitleChineseTraditional,
        [int]$Line,
        [string]$Path,
        # The class of the sound layer this value sits inside, taken from the
        # scenario rather than guessed from the file name. Names alone cannot
        # decide it: ed_voonly is present under both sound/ and bgm/.
        [string]$LayerClass,
        [System.Collections.Generic.List[object]]$Results
    )

    if ($null -eq $Value) {
        return
    }

    if ($Value -is [System.Collections.IDictionary] -or $Value -is [pscustomobject]) {
        $properties = if ($Value -is [System.Collections.IDictionary]) {
            $Value.GetEnumerator() | ForEach-Object { [pscustomobject]@{ Name = $_.Key; Value = $_.Value } }
        } else {
            $Value.PSObject.Properties
        }

        foreach ($property in $properties) {
            $nextPath = if ($Path) { "$Path.$($property.Name)" } else { $property.Name }
            # Every replay.filename counts, whatever it is called. Selecting by
            # name (^(se|bgm)[0-9]) used to drop ed_voonly, ed_short1,
            # ed_short_voonly, op_short and top, so every coverage figure
            # measured against this table was understated.
            if ($nextPath -like '*replay.filename' -and $property.Value -is [string] -and $property.Value) {
                $Results.Add([pscustomobject]@{
                    Type = switch -Regex ($LayerClass) {
                        '^(?i)bgm$'          { 'BGM'; break }
                        '^(?i)(loop)?se[0-9]*$' { 'SE'; break }
                        # No class in scope: fall back to the old name rule, so a
                        # shape this does not know about still lands somewhere.
                        default { if ($property.Value -match '^(?i)se') { 'SE' } else { 'BGM' } }
                    }
                    LayerClass = $LayerClass
                    Name = $property.Value.ToLowerInvariant()
                    Scene = $Scene
                    Label = $Label
                    TitleJapanese = $TitleJapanese
                    TitleEnglish = $TitleEnglish
                    TitleChineseSimplified = $TitleChineseSimplified
                    TitleChineseTraditional = $TitleChineseTraditional
                    Line = $Line
                    Path = $nextPath
                })
            }
            Find-AudioReferences -Value $property.Value -Scene $Scene -Label $Label -TitleJapanese $TitleJapanese -TitleEnglish $TitleEnglish -TitleChineseSimplified $TitleChineseSimplified -TitleChineseTraditional $TitleChineseTraditional -Line $Line -Path $nextPath -LayerClass $LayerClass -Results $Results
        }
        return
    }

    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        # A layer is written as ["lse", "loopse", { ... }] -- name, class, body.
        # The class is what says whether a filename below is music or an effect,
        # so it is carried down from here.
        $items = @($Value)
        $classHere = $LayerClass
        if ($items.Count -ge 3 -and $items[0] -is [string] -and $items[1] -is [string]) {
            $classHere = $items[1]
        }
        $index = 0
        foreach ($item in $items) {
            Find-AudioReferences -Value $item -Scene $Scene -Label $Label -TitleJapanese $TitleJapanese -TitleEnglish $TitleEnglish -TitleChineseSimplified $TitleChineseSimplified -TitleChineseTraditional $TitleChineseTraditional -Line $Line -Path "$Path[$index]" -LayerClass $classHere -Results $Results
            $index++
        }
    }
}

$inventoryNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
# Every .ogg in the listing, not just the ones named like se/bgm. The old
# pattern hid thirteen assets -- ed, ed_inst, ed_short1, ed_short2, the _voonly
# and _inst variants, jingle1, jingle2, op_short -- so anything referring to
# them looked like a reference to a file that does not exist.
Get-Content -LiteralPath $InventoryPath | ForEach-Object {
    # Tree-drawing characters and indentation first, then the leaf. Names can
    # contain spaces and brackets, so only the decoration is stripped.
    $leaf = ($_ -replace '^[\s│├└──|+\\\-]+', '').Trim()
    if ($leaf -notmatch '(?i)\.ogg$') { return }
    [void]$inventoryNames.Add(($leaf -replace '(?i)\.ogg$', '').ToLowerInvariant())
}

$references = [System.Collections.Generic.List[object]]::new()
Get-ChildItem -LiteralPath $SceneRoot -Recurse -Filter '*.ks.json' -File | ForEach-Object {
    $document = Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json -Depth 100
    $sceneName = $_.BaseName -replace '\.ks$', ''
    foreach ($scenarioScene in $document.scenes) {
        $title = @($scenarioScene.title)
        for ($line = 0; $line -lt $scenarioScene.lines.Count; $line++) {
            Find-AudioReferences -Value $scenarioScene.lines[$line] -Scene $sceneName -Label $scenarioScene.label -TitleJapanese $title[0] -TitleEnglish $title[1] -TitleChineseSimplified $title[2] -TitleChineseTraditional $title[3] -Line $line -Path 'lines' -Results $references
        }
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$references | Sort-Object Type, Name, Scene, Label, Line, Path |
    Export-Csv -NoTypeInformation -Encoding utf8BOM -LiteralPath (Join-Path $OutputDirectory 'scenario-audio-occurrences.csv')

$usage = $references | Group-Object Type, Name | ForEach-Object {
    $first = $_.Group[0]
    [pscustomobject]@{
        Type = $first.Type
        Name = $first.Name
        Asset = "$($first.Name).ogg"
        InInventory = $inventoryNames.Contains($first.Name)
        Occurrences = $_.Count
        Scenes = ($_.Group.Scene | Sort-Object -Unique) -join ', '
        SceneTitlesEnglish = ($_.Group | Group-Object Scene | Sort-Object Name | ForEach-Object {
                "$($_.Name) ($($_.Group[0].TitleEnglish))"
            }) -join ', '
    }
}
$usage | Sort-Object Type, Name |
    Export-Csv -NoTypeInformation -Encoding utf8BOM -LiteralPath (Join-Path $OutputDirectory 'scenario-audio-usage.csv')

$se = @($usage | Where-Object Type -eq 'SE')
$bgm = @($usage | Where-Object Type -eq 'BGM')
$missing = @($usage | Where-Object { -not $_.InInventory })
$markdown = @(
    '# Scenario Audio Index',
    '',
    "Generated by `Tools/research/index-scenario-audio.ps1` from `$SceneRoot`.",
    '',
    "- Scene files: $(@($references.Scene | Sort-Object -Unique).Count)",
    "- Audio occurrences: $($references.Count)",
    "- Unique SE: $($se.Count)",
    "- Unique BGM: $($bgm.Count)",
    "- Inventory matches: $($usage.Count - $missing.Count)/$($usage.Count)",
    '',
    '## Unmatched References',
    '',
    '> This whole file is generated and every run overwrites it. What these',
    '> unmatched names turned out to be is written up in `asset-defects.md`',
    '> beside it, which is not generated.',
    ''
)
if ($missing.Count) {
    $markdown += $missing | Sort-Object Type, Name | ForEach-Object { "- ``$($_.Type)`` ``$($_.Name)`` in $($_.Scenes)" }
} else {
    $markdown += '- None.'
}
$markdown += @(
    '',
    '## Files',
    '',
    '- `scenario-audio-usage.csv`: one row per audio name with all scenes using it, including English scene titles.',
    '- `scenario-audio-occurrences.csv`: every reference with scene, localized title fields, KAG label, source line index, and JSON path.',
    '',
    'The inventory is a filename list, not a semantic sound catalog. A match verifies that an asset exists; it does not identify its audible content.'
)
$markdown | Set-Content -Encoding utf8BOM -LiteralPath (Join-Path $OutputDirectory 'scenario-audio-index.md')
