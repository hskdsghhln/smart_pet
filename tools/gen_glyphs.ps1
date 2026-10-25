Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$ErrorActionPreference = "Stop"

$cps = @(0x6674,0x5929,0x6C14,0x591A,0x4E91,0x9634,0x5C0F,0x4E2D,0x5927,0x96E8,0x9635,0x96F7,0x96EA,0x96FE,0x6E29,0x5EA6,0x6E7F)
$chars = @()
foreach ($c in $cps) { $chars += [string][char]$c }

$fontName = "SimSun"
$fontSize = 14
$flags = [System.Windows.Forms.TextFormatFlags]::NoPadding -bor `
         [System.Windows.Forms.TextFormatFlags]::HorizontalCenter -bor `
         [System.Windows.Forms.TextFormatFlags]::VerticalCenter -bor `
         [System.Windows.Forms.TextFormatFlags]::SingleLine

$allBytes = @()
$allBits = @()

foreach ($ch in $chars) {
  $bmp = New-Object System.Drawing.Bitmap(16,16)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $font = New-Object System.Drawing.Font($fontName, [single]$fontSize, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $rect = New-Object System.Drawing.Rectangle(0,0,16,16)
  [System.Windows.Forms.TextRenderer]::DrawText($g, $ch, $font, $rect, [System.Drawing.Color]::White, [System.Drawing.Color]::Black, $flags)
  $g.Dispose()

  $bits = New-Object 'int[,]' 16,16
  for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
      if ($bmp.GetPixel($x,$y).R -gt 100) { $bits[$x,$y] = 1 }
    }
  }
  $bmp.Dispose()

  $data = New-Object byte[] 32
  for ($page = 0; $page -lt 2; $page++) {
    for ($col = 0; $col -lt 16; $col++) {
      $v = 0
      for ($r = 0; $r -lt 8; $r++) {
        if ($bits[$col, ($page * 8 + $r)] -eq 1) { $v = $v -bor (1 -shl $r) }
      }
      $data[$page * 16 + $col] = [byte]$v
    }
  }
  $allBytes += ,$data
  $allBits += ,$bits

  $ink = 0
  foreach ($b in $data) { for ($i = 0; $i -lt 8; $i++) { if ((($b -shr $i) -band 1) -eq 1) { $ink++ } } }
  Write-Host ("  {0}  ink={1}" -f $ch, $ink)
}

$sb = New-Object System.Text.StringBuilder
for ($k = 0; $k -lt $chars.Count; $k++) {
  $b = $allBytes[$k]
  [void]$sb.Append("`t`"")
  [void]$sb.Append($chars[$k])
  [void]$sb.Append("`",`r`n")
  for ($line = 0; $line -lt 2; $line++) {
    [void]$sb.Append("`t")
    $parts = @()
    for ($i = 0; $i -lt 16; $i++) { $parts += ("0x{0:X2}" -f $b[$line * 16 + $i]) }
    [void]$sb.Append(($parts -join ","))
    [void]$sb.Append(",`r`n")
  }
  [void]$sb.Append("`r`n")
}
$outC = Join-Path (Get-Location).Path "_glyphs_body.txt"
[System.IO.File]::WriteAllText($outC, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

$cell = 16 * 6
$gap = 6
$pw = $chars.Count * ($cell + $gap) + $gap
$ph = $cell + $gap * 2
$prev = New-Object System.Drawing.Bitmap($pw, $ph)
$pg = [System.Drawing.Graphics]::FromImage($prev)
$pg.Clear([System.Drawing.Color]::FromArgb(255, 12, 16, 22))
for ($k = 0; $k -lt $chars.Count; $k++) {
  $bits = $allBits[$k]
  $ox = $gap + $k * ($cell + $gap)
  for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
      if ($bits[$x,$y] -eq 1) {
        $pg.FillRectangle([System.Drawing.Brushes]::White, $ox + $x * 6, $gap + $y * 6, 6, 6)
      }
    }
  }
}
$pg.Dispose()
$outP = Join-Path (Get-Location).Path "_glyphs_preview.png"
$prev.Save($outP, [System.Drawing.Imaging.ImageFormat]::Png)
$prev.Dispose()
Write-Host ("wrote " + $outC)
Write-Host ("wrote " + $outP)