param([string]$In, [string]$Out, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Z = 4)
Add-Type -AssemblyName System.Drawing
$s = [System.Drawing.Image]::FromFile($In)
$b = New-Object System.Drawing.Bitmap ($W * $Z), ($H * $Z)
$g = [System.Drawing.Graphics]::FromImage($b)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$d = New-Object System.Drawing.Rectangle 0, 0, ($W * $Z), ($H * $Z)
$r = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
$g.DrawImage($s, $d, $r, [System.Drawing.GraphicsUnit]::Pixel)
$b.Save($Out)
