Add-Type -AssemblyName System.Drawing
$out = Join-Path (Get-Location) "res\drawable\ic_launcher.png"
$S = 192
$bmp = New-Object System.Drawing.Bitmap $S, $S
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$path = New-RoundedRect 7 7 ($S - 14) ($S - 14) 44
$bgBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 17, 24, 39))
$g.FillPath($bgBrush, $path)
$border = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 59, 130, 246), 5)
$g.DrawPath($border, $path)

$g.FillEllipse([System.Drawing.Brushes]::White, 44, 56, 44, 78)
$g.FillEllipse([System.Drawing.Brushes]::White, 104, 56, 44, 78)

$g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("icon written: {0}  ({1} bytes)" -f $out, (Get-Item $out).Length)