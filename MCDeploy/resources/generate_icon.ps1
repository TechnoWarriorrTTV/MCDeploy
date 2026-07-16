# Generates mcdeploy.ico from scratch using GDI+.
# The icon is a green rounded square with a wrench glyph, matching the
# marketing site favicon and the app's brand color.
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$outFile = Join-Path $scriptDir 'mcdeploy.ico'

# Multiple sizes packed into one .ico so Windows picks the crispest one for each context.
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$pngBlobs = @()

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = 'AntiAlias'
    $g.CompositingQuality = 'HighQuality'
    $g.PixelOffsetMode   = 'HighQuality'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)

    $r = $size * 0.16
    $rect = New-Object System.Drawing.RectangleF 0, 0, $size, $size

    # Rounded rectangle background gradient (deep green -> neon green)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc((New-Object System.Drawing.RectangleF 0, 0, ($r*2), ($r*2)), 180, 90)
    $path.AddArc((New-Object System.Drawing.RectangleF ($size - $r*2), 0, ($r*2), ($r*2)), 270, 90)
    $path.AddArc((New-Object System.Drawing.RectangleF ($size - $r*2), ($size - $r*2), ($r*2), ($r*2)), 0, 90)
    $path.AddArc((New-Object System.Drawing.RectangleF 0, ($size - $r*2), ($r*2), ($r*2)), 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, [System.Drawing.Color]::FromArgb(30, 189, 86), [System.Drawing.Color]::FromArgb(92, 255, 150), [System.Drawing.Drawing2D.LinearGradientMode]::ForwardDiagonal)
    $g.FillPath($brush, $path)
    $brush.Dispose()

    # Wrench glyph — draw simplified two-diagonal-strokes shape in dark.
    # Using GraphicsPath to describe a wrench-like shape.
    $wrenchColor = [System.Drawing.Color]::FromArgb(255, 12, 15, 12)
    $pen = New-Object System.Drawing.Pen $wrenchColor, ($size * 0.11)
    $pen.StartCap = 'Round'
    $pen.EndCap   = 'Round'
    $pen.LineJoin = 'Round'

    # Diagonal wrench body
    $p1x = $size * 0.32; $p1y = $size * 0.68
    $p2x = $size * 0.68; $p2y = $size * 0.32
    $g.DrawLine($pen, [float]$p1x, [float]$p1y, [float]$p2x, [float]$p2y)

    # Wrench "head" — a small filled circle at the top-right of the diagonal
    $headSize = $size * 0.22
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $wrenchColor), [float]($p2x - $headSize/2), [float]($p2y - $headSize/2), [float]$headSize, [float]$headSize)

    # Wrench "grip" — filled circle at bottom-left
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $wrenchColor), [float]($p1x - $headSize/2), [float]($p1y - $headSize/2), [float]$headSize, [float]$headSize)

    $pen.Dispose()

    # Save as PNG bytes
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBytes = $ms.ToArray()
    $ms.Dispose()
    $g.Dispose()
    $bmp.Dispose()

    $pngBlobs += ,@{ Size = $size; Bytes = $pngBytes }
}

# Now build the .ico container: ICONDIR + entries + payload
$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $stream

# ICONDIR: reserved(2)=0, type(2)=1, count(2)=N
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$pngBlobs.Count)

$dataOffset = 6 + (16 * $pngBlobs.Count)
foreach ($blob in $pngBlobs) {
    $w = if ($blob.Size -ge 256) { 0 } else { $blob.Size }
    $h = if ($blob.Size -ge 256) { 0 } else { $blob.Size }
    $writer.Write([byte]$w)                              # width
    $writer.Write([byte]$h)                              # height
    $writer.Write([byte]0)                                # color count (0 = truecolor)
    $writer.Write([byte]0)                                # reserved
    $writer.Write([uint16]1)                              # planes
    $writer.Write([uint16]32)                             # bits per pixel
    $writer.Write([uint32]$blob.Bytes.Length)             # bytes in resource
    $writer.Write([uint32]$dataOffset)                    # offset
    $dataOffset += $blob.Bytes.Length
}
foreach ($blob in $pngBlobs) {
    $writer.Write($blob.Bytes)
}

$writer.Flush()
[System.IO.File]::WriteAllBytes($outFile, $stream.ToArray())
$writer.Close()

Write-Host "Wrote $outFile ($([System.IO.FileInfo]::new($outFile).Length) bytes, $($pngBlobs.Count) sizes)"
