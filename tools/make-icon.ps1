# Draws the RDC Manager icon and writes res\rdcm.ico.
# Only needed if you want to change the icon; the .ico is committed.
#
#   pwsh tools\make-icon.ps1

Add-Type -AssemblyName System.Drawing

$out = Join-Path $PSScriptRoot '..\res\rdcm.ico'
$sizes = 16, 20, 24, 32, 40, 48, 64, 256

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

function New-IconBitmap([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap($n, $n, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    # blue rounded tile
    $tile = New-RoundedRect 0 0 $n $n ($n * 0.22)
    $top = [System.Drawing.Color]::FromArgb(255, 27, 127, 204)
    $bottom = [System.Drawing.Color]::FromArgb(255, 12, 78, 134)
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF(0, $n)), $top, $bottom)
    $g.FillPath($grad, $tile)

    # white monitor
    $stroke = [Math]::Max(1.3, $n * 0.07)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, $stroke)
    $pen.LineJoin = 'Round'
    $pen.StartCap = 'Round'
    $pen.EndCap = 'Round'
    $mx = $n * 0.19; $my = $n * 0.23; $mw = $n * 0.62; $mh = $n * 0.41
    $screen = New-RoundedRect $mx $my $mw $mh ($n * 0.06)
    $g.DrawPath($pen, $screen)
    $g.DrawLine($pen, [float]($n * 0.5), [float]($my + $mh), [float]($n * 0.5), [float]($n * 0.76))
    $g.DrawLine($pen, [float]($n * 0.35), [float]($n * 0.78), [float]($n * 0.65), [float]($n * 0.78))

    # the list on the screen (too small to draw below 32 px)
    if ($n -ge 32) {
        $line = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 255, 255, 255), [float]($n * 0.045))
        $line.StartCap = 'Round'
        $line.EndCap = 'Round'
        $g.DrawLine($line, [float]($n * 0.31), [float]($n * 0.355), [float]($n * 0.69), [float]($n * 0.355))
        $g.DrawLine($line, [float]($n * 0.31), [float]($n * 0.435), [float]($n * 0.60), [float]($n * 0.435))
        $g.DrawLine($line, [float]($n * 0.31), [float]($n * 0.515), [float]($n * 0.66), [float]($n * 0.515))
        $line.Dispose()
    }

    $pen.Dispose(); $grad.Dispose(); $tile.Dispose(); $screen.Dispose(); $g.Dispose()
    return $bmp
}

# One icon image: PNG from 48 px up (much smaller), classic 32-bit bitmap below that.
function Get-ImageBytes([System.Drawing.Bitmap]$bmp) {
    $n = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    if ($n -ge 48) {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$ms.ToArray()
    }
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([int]40); $bw.Write([int]$n); $bw.Write([int]($n * 2))   # header size, width, height (colour + mask)
    $bw.Write([int16]1); $bw.Write([int16]32)                          # planes, bits per pixel
    $bw.Write([int]0); $bw.Write([int]($n * $n * 4))                   # no compression, image size
    $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $n - 1; $y -ge 0; $y--) {                                # rows are stored bottom-up
        for ($x = 0; $x -lt $n; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    $maskRow = [int]([Math]::Ceiling($n / 32.0) * 4)                   # empty mask; alpha does the work
    $bw.Write((New-Object byte[] ($maskRow * $n)))
    $bw.Flush()
    return ,$ms.ToArray()
}

$images = foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    [pscustomobject]@{ Size = $s; Bytes = (Get-ImageBytes $bmp) }
    $bmp.Dispose()
}

$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter($fs)
$w.Write([int16]0); $w.Write([int16]1); $w.Write([int16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([int16]1); $w.Write([int16]32)
    $w.Write([int]$img.Bytes.Length); $w.Write([int]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) { $w.Write($img.Bytes) }
$w.Dispose(); $fs.Dispose()

"Wrote $((Resolve-Path $out).Path) ($((Get-Item $out).Length) bytes, sizes: $($sizes -join ', '))"
