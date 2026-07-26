param([string]$Path)

$data = [System.IO.File]::ReadAllBytes($Path)
$sig = [System.Text.Encoding]::ASCII.GetString($data[0..2])
if ($sig -ne "PSB") { Write-Error "Not PSB"; exit 1 }

$StrOffPos = [System.BitConverter]::ToUInt32($data, 0x10)
$StrDataPos = [System.BitConverter]::ToUInt32($data, 0x14)

# Read string table
function GetSize($b) {
    switch ($b) { 0x0D { 1 } 0x0E { 2 } 0x0F { 3 } 0x10 { 4 } default { throw "Unknown size: 0x$('{0:X}' -f $b)" } }
}
$pos = $StrOffPos
$cntSize = GetSize($data[$pos])
$count = [System.BitConverter]::ToUInt32($data, $pos + 1) -band ([math]::Pow(256, $cntSize) - 1)
$pos += 1 + $cntSize
$offSize = GetSize($data[$pos]); $pos++

$offsets = for ($i = 0; $i -lt $count; $i++) {
    [System.BitConverter]::ToUInt32($data, $pos + ($i * $offSize)) -band ([math]::Pow(256, $offSize) - 1)
}
$strings = for ($i = 0; $i -lt $count; $i++) {
    $sp = $StrDataPos + $offsets[$i]; $e = $sp; while ($data[$e] -ne 0) { $e++ }
    [System.Text.Encoding]::UTF8.GetString($data[$sp..($e-1)])
}

# Bytecode walk
$BCStart = [System.BitConverter]::ToUInt32($data, 0x24)
$BCLen   = [System.BitConverter]::ToUInt32($data, 0x10) - $BCStart
$BCEnd = $BCStart + $BCLen

$strOps = @()       # direct string refs (opcodes 0x15-0x18)
$seenIds = @{}      # dedup
$arrayPairs = @()   # (typeCode, paramStrId) pairs from 1+2-byte arrays
$pendingTypes = $null  # buffer for 1-byte type array

$i = $BCStart
while ($i -lt $BCEnd) {
    $cmd = $data[$i]
    switch ($cmd) {
        { $_ -ge 0x15 -and $_ -le 0x18 } { # String refs
            $len = $_ - 0x14
            $id = [System.BitConverter]::ToUInt32($data, $i + 1) -band ([math]::Pow(256, $len) - 1)
            if ($id -lt $strings.Length -and -not $seenIds.ContainsKey($id)) { 
                $strOps += ,@($id, $cmd)
                $seenIds[$id] = $true
            }
            $i += 1 + $len
        }
        { $_ -ge 0x04 -and $_ -le 0x0C } { $i += 1 + ($_ - 0x04) } # Numbers
        0x1D { $i += 1 } # void
        0x1E { $i += 5 } # 4-byte embedded
        0x1F { $i += 9 } # 8-byte embedded
        { $_ -ge 0x00 -and $_ -le 0x03 } { $i += 1 } # Constants
        { $_ -ge 0x0D -and $_ -le 0x14 } { # Arrays (records)
            $i++
            $cLen = $_ - 0xC
            $c = [System.BitConverter]::ToUInt32($data, $i) -band ([math]::Pow(256, $cLen) - 1)
            $i += $cLen
            $eLenByte = $data[$i]; $i++
            $eLen = $eLenByte - 0xC
            
            if ($eLen -eq 1) {
                # 1-byte = tag type codes
                $pendingTypes = for ($r = 0; $r -lt $c; $r++) { $data[$i + $r] }
            } elseif ($eLen -eq 2 -and $pendingTypes -ne $null) {
                # 2-byte = string ID params — pair with pending types
                for ($r = 0; $r -lt $c -and $r -lt $pendingTypes.Count; $r++) {
                    $typeCode = $pendingTypes[$r]
                    $strId = [System.BitConverter]::ToUInt16($data, $i + $r * 2)
                    $str = if ($strId -lt $strings.Length) { $strings[$strId] } else { "?" }
                    $seenIds[$strId] = $true  # mark as seen
                    $arrayPairs += ,@($typeCode, $strId, $str)
                }
                $pendingTypes = $null
            } else {
                $pendingTypes = $null
            }
            
            $i += $eLen * $c
        }
        0x20 { $i++ }
        0x21 { $i++ }
        { $_ -ge 0x19 -and $_ -le 0x1C } { $i += 1 + ($_ - 0x18) } # Embedded refs
        default { $i++ }
    }
}

# Add unreferenced strings at the end
for ($s = 0; $s -lt $strings.Length; $s++) {
    if (-not $seenIds.ContainsKey($s)) { $strOps += ,@($s, -1) }
}

# Analyze type code frequencies
$typeFreq = @{}
foreach ($pair in $arrayPairs) { $t = $pair[0]; if (-not $typeFreq.ContainsKey($t)) { $typeFreq[$t] = 0 }; $typeFreq[$t]++ }

$sortedTypes = $typeFreq.GetEnumerator() | Sort-Object Value -Descending

Write-Host "=== Tag type codes (1-byte arrays) ===" -ForegroundColor Cyan
Write-Host "Type | Freq   | Sample params"
Write-Host "-----+--------+----------------------"
foreach ($entry in $sortedTypes) {
    $samples = @()
    $sampleCount = 0
    foreach ($pair in $arrayPairs) {
        if ($pair[0] -eq $entry.Key -and $sampleCount -lt 3) {
            $s = $pair[2]
            if ($s.Length -gt 40) { $s = $s.Substring(0, 37) + "..." }
            $samples += "[$($pair[1])]'$s'"
            $sampleCount++
        }
    }
    $sampleStr = if ($samples.Count -gt 0) { $samples -join ", " } else { "(no params)" }
    Write-Host ("0x{0:X2} ({0,3}) | {1,5} | {2}" -f $entry.Key, $entry.Value, $sampleStr)
}

Write-Host ""
Write-Host "=== Strings in bytecode order (direct refs + array params) ===" -ForegroundColor Cyan
Write-Host "ID  | String"
Write-Host "----+---------------------------------------------------------------"
for ($i = 0; $i -lt $strOps.Length; $i++) {
    $id = $strOps[$i][0]
    $str = $strings[$id]
    if ($str.Length -gt 80) { $str = $str.Substring(0, 77) + "..." }
    Write-Host ("{0,4} | {1}" -f $i, $str)
}

Write-Host ""
Write-Host "=== Lookup by ScnEditorGUI IDs ===" -ForegroundColor Cyan
foreach ($lookup in @("夏生", "キャサリン", "女の声", "水菜萌", "誰かの声", "打ち上げ中継", "朝のラジオ", "Natsuki", "Catherine", "Minamo", "Newsreader", "Someone's Voice", "Woman's Voice")) {
    for ($i = 0; $i -lt $strOps.Length; $i++) {
        if ($strings[$strOps[$i][0]] -eq $lookup) {
            Write-Host ("ScnID {0,4} = '{1}'" -f $i, $lookup)
            break
        }
    }
}

Write-Host ""
Write-Host "=== Voice tag lookup ===" -ForegroundColor Cyan
foreach ($prefix in @("ATR", "CAT", "MIN", "RYU", "RIR", "XYA", "YAS", "XXA", "XXB", "MAN", "WOM")) {
    for ($i = 0; $i -lt $strOps.Length; $i++) {
        $str = $strings[$strOps[$i][0]]
        if ($str -match "^${prefix}_b101_") {
            Write-Host ("ScnID {0,4} = '{1}'" -f $i, $str)
        }
    }
}

Write-Host ""
Write-Host "=== (type, strId) pairs sample (first 200) ===" -ForegroundColor Cyan
Write-Host "Idx | Type | StrID | String"
Write-Host "----+------+-------+-------------------------"
for ($p = 0; $p -lt 200 -and $p -lt $arrayPairs.Count; $p++) {
    $s = $arrayPairs[$p][2]
    if ($s.Length -gt 60) { $s = $s.Substring(0, 57) + "..." }
    Write-Host ("{0,3} | 0x{1:X2} | {2,5} | {3}" -f $p, $arrayPairs[$p][0], $arrayPairs[$p][1], $s)
}

Write-Host ""
Write-Host "=== Stats ===" -ForegroundColor Cyan
Write-Host "Total strings: $count"
Write-Host "Array pairs (type+param): $($arrayPairs.Count)"
Write-Host "Unique type codes: $($typeFreq.Count)"
