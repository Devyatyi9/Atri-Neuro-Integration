param([string]$Path, [switch]$Raw)

$data = [System.IO.File]::ReadAllBytes($Path)

$sig = [System.Text.Encoding]::ASCII.GetString($data[0..2])
if ($sig -ne "PSB") {
    if ($sig -eq "mdf") {
        $decompLen = [System.BitConverter]::ToUInt32($data, 4)
        $input = New-Object System.IO.MemoryStream($data, 8, $data.Length - 8)
        $output = New-Object System.IO.MemoryStream
        $zlib = New-Object System.IO.Compression.DeflateStream($input, [System.IO.Compression.CompressionMode]::Decompress)
        $zlib.CopyTo($output)
        $data = $output.ToArray()
        $zlib.Close(); $input.Close(); $output.Close()
    } else {
        Write-Error "Not a PSB file (sig: $sig)"; exit 1
    }
}

$StrOffPos = [System.BitConverter]::ToUInt32($data, 0x10)
$StrDataPos = [System.BitConverter]::ToUInt32($data, 0x14)

# Read string table
$pos = $StrOffPos
function GetSize($b) {
    switch ($b) {
        0x0D { return 1 }
        0x0E { return 2 }
        0x0F { return 3 }
        0x10 { return 4 }
        default { throw "Unknown size: 0x$('{0:X}' -f $b)" }
    }
}
$cntSize = GetSize($data[$pos])
$count = [System.BitConverter]::ToUInt32($data, $pos + 1) -band ([math]::Pow(256, $cntSize) - 1)
$pos += 1 + $cntSize
$offSize = GetSize($data[$pos]); $pos++

$offsets = for ($i = 0; $i -lt $count; $i++) {
    [System.BitConverter]::ToUInt32($data, $pos + ($i * $offSize)) -band ([math]::Pow(256, $offSize) - 1)
}
$pos += $offSize * $count

$strings = for ($i = 0; $i -lt $count; $i++) {
    $strPos = $StrDataPos + $offsets[$i]
    $end = $strPos; while ($data[$end] -ne 0) { $end++ }
    [System.Text.Encoding]::UTF8.GetString($data[$strPos..($end-1)])
}

Write-Host "Total: $count strings" -ForegroundColor Cyan
$i = 0
$strings | ForEach-Object { Write-Host ("[{0,4}] {1}" -f $i, $_); $i++ }
