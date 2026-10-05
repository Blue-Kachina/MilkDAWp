# resources/make-icon.ps1 -- regenerates resources/icon.png (6.2-6.4).
#
# The app icon is v1's wordmark ("MilkDAWp Logo Transparent.png", 408x135, from
# the v1 repository's resources/images) centred on a dark rounded tile with a
# soft accent glow. JUCE turns icon.png into the .ico/.icns the shells and
# installers use; Linux packages use the PNG directly.
#
#   pwsh resources/make-icon.ps1 -Logo "<v1 repo>/resources/images/MilkDAWp Logo Transparent.png"
#
# A square or vector mark would read better at 16-32 px than the wordmark does;
# replace icon.png with one whenever it exists.
param(
  [Parameter(Mandatory = $true)][string]$Logo,
  [string]$Out = (Join-Path $PSScriptRoot "icon.png"),
  [int]$Size = 1024
)

Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Logo))

# Bounds of the visible logo (its soft shadow included).
$x0 = $src.Width; $x1 = 0; $y0 = $src.Height; $y1 = 0
for ($y = 0; $y -lt $src.Height; $y++) {
  for ($x = 0; $x -lt $src.Width; $x++) {
    if ($src.GetPixel($x, $y).A -gt 30) {
      if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x }
      if ($y -lt $y0) { $y0 = $y }; if ($y -gt $y1) { $y1 = $y }
    }
  }
}
$crop = [System.Drawing.RectangleF]::new($x0, $y0, $x1 - $x0 + 1, $y1 - $y0 + 1)

$bmp = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
$g.Clear([System.Drawing.Color]::Transparent)

# Rounded tile with a margin, like other desktop icons.
$m = [int]($Size * 0.08); $r = [int]($Size * 0.2); $w = $Size - 2 * $m
$tile = New-Object System.Drawing.Drawing2D.GraphicsPath
$tile.AddArc($m, $m, $r, $r, 180, 90); $tile.AddArc($m + $w - $r, $m, $r, $r, 270, 90)
$tile.AddArc($m + $w - $r, $m + $w - $r, $r, $r, 0, 90); $tile.AddArc($m, $m + $w - $r, $r, $r, 90, 90)
$tile.CloseFigure()
$tileBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.Point]::new(0, $m),
  [System.Drawing.Point]::new(0, $m + $w), [System.Drawing.Color]::FromArgb(255, 32, 36, 48),
  [System.Drawing.Color]::FromArgb(255, 12, 13, 18))
$g.FillPath($tileBrush, $tile)

# Accent glow (the drawer's accent, #6cc4ff).
$glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$gw = [int]($w * 0.8); $glowPath.AddEllipse([int](($Size - $gw) / 2), [int](($Size - $gw) / 2), $gw, $gw)
$glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $glowPath
$glow.CenterColor = [System.Drawing.Color]::FromArgb(110, 108, 196, 255)
$glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 108, 196, 255))
$g.SetClip($tile); $g.FillPath($glow, $glowPath); $g.ResetClip()

# The wordmark, as wide as fits.
$box = $w * 0.72
$scale = [Math]::Min($box / $crop.Width, $box / $crop.Height)
$dw = $crop.Width * $scale; $dh = $crop.Height * $scale
$g.DrawImage($src, [System.Drawing.RectangleF]::new(($Size - $dw) / 2, ($Size - $dh) / 2, $dw, $dh), $crop,
  [System.Drawing.GraphicsUnit]::Pixel)

$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)

# A 256 px copy for the Linux icon theme (hicolor/256x256), downscaled here so
# packaging needs no image tools.
$small = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gs = [System.Drawing.Graphics]::FromImage($small)
$gs.InterpolationMode = 'HighQualityBicubic'; $gs.PixelOffsetMode = 'HighQuality'
$gs.DrawImage($bmp, 0, 0, 256, 256)
$gs.Dispose()
$smallOut = [System.IO.Path]::ChangeExtension($Out, $null).TrimEnd('.') + "-256.png"
$small.Save($smallOut, [System.Drawing.Imaging.ImageFormat]::Png)
$small.Dispose(); $bmp.Dispose(); $src.Dispose()
Write-Host "Wrote $Out ($Size x $Size) and $smallOut (256 x 256)"
