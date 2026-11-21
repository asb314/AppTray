<#
.SYNOPSIS
	Draws AppTray's icon (a blue rounded square with four app tiles) and writes a multi-size
	.ico with PNG frames.
.EXAMPLE
	pwsh -File tools\New-AppIcon.ps1
#>
param(
	[string]$Output = (Join-Path $PSScriptRoot '..\src\AppTray\res\AppTray.ico')
)

Add-Type -AssemblyName System.Drawing

function New-RoundedRectanglePath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
	$path = New-Object System.Drawing.Drawing2D.GraphicsPath
	$d = [Math]::Min($r * 2, [Math]::Min($w, $h))
	if ($d -le 0) { $path.AddRectangle((New-Object System.Drawing.RectangleF $x, $y, $w, $h)); return $path }
	$path.AddArc($x, $y, $d, $d, 180, 90)
	$path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
	$path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
	$path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
	$path.CloseFigure()
	return $path
}

function New-IconFrame([int]$size) {
	$bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bitmap)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
	$g.Clear([System.Drawing.Color]::Transparent)

	# Background: rounded square with a vertical blue gradient
	$inset = [Math]::Max(0.5, $size * 0.03)
	$side = $size - 2 * $inset
	$background = New-RoundedRectanglePath $inset $inset $side $side ($size * 0.22)
	$top = [System.Drawing.Color]::FromArgb(255, 59, 130, 246)
	$bottom = [System.Drawing.Color]::FromArgb(255, 29, 78, 216)
	$rect = New-Object System.Drawing.RectangleF $inset, $inset, $side, $side
	$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $top, $bottom, 90
	$g.FillPath($brush, $background)

	# Four tiles: three white, one amber (the "favorite")
	$gap = [Math]::Max(1.0, $size * 0.08)
	$margin = $size * 0.22
	$tile = ($size - 2 * $margin - $gap) / 2
	$radius = [Math]::Max(0.0, $tile * 0.22)
	$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
	$amber = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 251, 191, 36))
	for ($row = 0; $row -lt 2; $row++) {
		for ($column = 0; $column -lt 2; $column++) {
			$x = $margin + $column * ($tile + $gap)
			$y = $margin + $row * ($tile + $gap)
			$fill = if ($row -eq 0 -and $column -eq 1) { $amber } else { $white }
			$g.FillPath($fill, (New-RoundedRectanglePath $x $y $tile $tile $radius))
		}
	}
	$g.Dispose()

	$stream = New-Object System.IO.MemoryStream
	$bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
	$bitmap.Dispose()
	return , $stream.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$frames = foreach ($s in $sizes) { , (New-IconFrame $s) }

# ICO: header, one directory entry per frame, then the PNG data
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
	$s = $sizes[$i]; $data = $frames[$i]
	$dimension = if ($s -ge 256) { 0 } else { $s }
	$writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
	$writer.Write([byte]0); $writer.Write([byte]0)
	$writer.Write([UInt16]1); $writer.Write([UInt16]32)
	$writer.Write([UInt32]$data.Length); $writer.Write([UInt32]$offset)
	$offset += $data.Length
}
foreach ($data in $frames) { $writer.Write($data) }
$writer.Flush()
$full = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force (Split-Path $full) | Out-Null
[System.IO.File]::WriteAllBytes($full, $out.ToArray())
"Icon written: $full ($($sizes -join ', ') px)"
