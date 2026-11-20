# Renders the Open Mastering Studio app icon (level bars meeting a limiter ceiling).
#   pwsh -File Tools\make_icon.ps1            -> Resources\icon_big.png (1024 px) and icon_small.png (simplified, for 16/32 px)
param([string]$OutDir = (Join-Path $PSScriptRoot "..\Resources"))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null

function RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}
function TopRoundedBar([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddLine($x + $w, $y + $h, $x, $y + $h); $p.CloseFigure(); return $p
}
function C([string]$hex, [int]$a = 255) { $c = [System.Drawing.ColorTranslator]::FromHtml($hex); [System.Drawing.Color]::FromArgb($a, $c.R, $c.G, $c.B) }

function Render([string]$file, [float[]]$heights, [float]$barW, [float]$gap, [float]$ceilThick, [bool]$detail) {
    $S = 1024
    $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.Clear([System.Drawing.Color]::Transparent)

    # Plate
    $inset = 36; $plate = RoundedRect $inset $inset ($S - 2 * $inset) ($S - 2 * $inset) 210
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, $inset)), ([System.Drawing.PointF]::new(0, $S - $inset)), (C "#2a2f3b"), (C "#101217")
    $g.FillPath($bg, $plate)
    $rim = New-Object System.Drawing.Pen ((C "#4a5263")), 10
    $g.DrawPath($rim, $plate)

    # Bars
    $n = $heights.Count
    $total = $n * $barW + ($n - 1) * $gap
    $x0 = ($S - $total) / 2; $base = 812; $ceilY = 300
    $barBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, $ceilY)), ([System.Drawing.PointF]::new(0, $base)), (C "#8fe3ff"), (C "#1e88c8")
    for ($i = 0; $i -lt $n; $i++) {
        $h = [Math]::Min($heights[$i], $base - $ceilY - $ceilThick / 2 - 14)
        $x = $x0 + $i * ($barW + $gap)
        $g.FillPath($barBrush, (TopRoundedBar $x ($base - $h) $barW $h ([Math]::Min(22, $barW / 4))))
    }
    if ($detail) {
        # baseline
        $g.FillRectangle((New-Object System.Drawing.SolidBrush (C "#4fc3f7" 90)), [float]($x0 - 20), [float]($base + 18), [float]($total + 40), [float]10)
    }

    # Limiter ceiling with glow
    $lx = $x0 - 40; $lw = $total + 80
    foreach ($k in 3, 2, 1) {
        $glow = New-Object System.Drawing.SolidBrush (C "#ffb74d" (22 * (4 - $k)))
        $g.FillPath($glow, (RoundedRect $lx ($ceilY - $ceilThick / 2 - 8 * $k) $lw ($ceilThick + 16 * $k) (($ceilThick + 16 * $k) / 2)))
    }
    $g.FillPath((New-Object System.Drawing.SolidBrush (C "#ffb74d")), (RoundedRect $lx ($ceilY - $ceilThick / 2) $lw $ceilThick ($ceilThick / 2)))

    $g.Dispose()
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "wrote $file"
}

# Full icon: five bars, the middle ones pressed against the ceiling
Render (Join-Path $OutDir "icon_big.png") @(250, 420, 600, 600, 360) 104 42 30 $true
# Small sizes: three thick bars and a heavier line so it still reads at 16-32 px
Render (Join-Path $OutDir "icon_small.png") @(330, 600, 260) 190 70 64 $false
