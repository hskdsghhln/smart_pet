$ErrorActionPreference = "Stop"

# Extract real font bitmaps from OLED_Data.c and emit tools/oled_data.js
# Run from the project root:  powershell -ExecutionPolicy Bypass -File tools\extract_oled_data.ps1

$root = (Get-Location).Path
$src = Join-Path $root "HardWare\OLED_Data.c"
$outDir = Join-Path $root "tools"
$out = Join-Path $outDir "oled_data.js"

if (-not (Test-Path $src)) { throw "OLED_Data.c not found: $src" }
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

$txt = [System.IO.File]::ReadAllText($src, [System.Text.Encoding]::UTF8)

function Get-Block([string]$decl) {
    $i = $txt.IndexOf($decl)
    if ($i -lt 0) { throw "declaration not found: $decl" }
    $j = $txt.IndexOf("};", $i)
    if ($j -lt 0) { throw "array terminator not found for: $decl" }
    return $txt.Substring($i, $j - $i)
}

function Get-Bytes([string]$block) {
    $m = [regex]::Matches($block, '0x([0-9A-Fa-f]{2})')
    $list = New-Object System.Collections.Generic.List[int]
    foreach ($x in $m) { $list.Add([Convert]::ToInt32($x.Groups[1].Value, 16)) }
    return $list
}

$b8 = Get-Bytes (Get-Block "OLED_F8x16[][16]")
$b6 = Get-Bytes (Get-Block "OLED_F6x8[][6]")

Write-Host ("F8x16 bytes : " + $b8.Count + "  (expect 1520 = 95 * 16)")
Write-Host ("F6x8  bytes : " + $b6.Count + "  (expect 570  = 95 * 6)")

# Chinese glyph table: entry = "char", then 32 hex bytes
$block = Get-Block "OLED_CF16x16[] ="
$ms = [regex]::Matches($block, '"([^"]*)"\s*,\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){32})')

$entries = New-Object System.Collections.Generic.List[string]
$count = 0
foreach ($m in $ms) {
    $s = $m.Groups[1].Value
    if ($s.Length -eq 0) { continue }   # sentinel entry, used as list terminator in firmware

    $hex = [regex]::Matches($m.Groups[2].Value, '0x([0-9A-Fa-f]{2})')
    $vals = New-Object System.Collections.Generic.List[string]
    foreach ($x in $hex) { $vals.Add([Convert]::ToInt32($x.Groups[1].Value, 16)) }

    $cp = [int][char]$s[0]
    $entries.Add('"' + $cp + '":[' + ($vals -join ',') + ']')
    $count++
}
Write-Host ("CJK glyphs  : " + $count)

$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("window.OLED_F8x16=[")
[void]$sb.Append(($b8 -join ','))
[void]$sb.Append("];`n")
[void]$sb.Append("window.OLED_F6x8=[")
[void]$sb.Append(($b6 -join ','))
[void]$sb.Append("];`n")
[void]$sb.Append("window.GLYPHS={")
[void]$sb.Append(($entries -join ','))
[void]$sb.Append("};`n")

[System.IO.File]::WriteAllText($out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ("written: " + $out + "  (" + (Get-Item $out).Length + " bytes)")