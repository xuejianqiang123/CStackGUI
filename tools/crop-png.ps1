# crop-png.ps1 -- crop a region out of a PNG (for looking at audit screenshots in detail)
#
# Pure ASCII on purpose: PowerShell 5.1 reads .ps1 using the ANSI code page, so Chinese
# text in a script shows up as "quote not closed" style parse errors.
#
# Repo note: lives under tools\ on purpose (build\ is not committed). Why it exists:
# judging dark-mode colors from a full-window screenshot is unreliable -- on 2026-10-09 I
# first read "the table follows dark" from a small image and the crop proved the opposite.
# **Small-image eyeballing is not evidence; crop and look.**
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\crop-png.ps1 <src> <x> <y> <w> <h> <dst>
#   Example (dark audit page, the toolbar strip):
#     powershell -NoProfile -ExecutionPolicy Bypass -File tools\crop-png.ps1 `
#       build\darkaudit\main-dark.png 0 60 340 50 build\tbar-dark.png
param([string]$Src, [int]$X, [int]$Y, [int]$W, [int]$H, [string]$Dst)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path $Src).Path)
Write-Output ("src size: " + $img.Width + "x" + $img.Height)
if ($X + $W -gt $img.Width)  { $W = $img.Width - $X }
if ($Y + $H -gt $img.Height) { $H = $img.Height - $Y }
$rect = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
$crop = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($crop)
$g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, $W, $H), $rect, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$crop.Save((Join-Path (Get-Location) $Dst), [System.Drawing.Imaging.ImageFormat]::Png)
$crop.Dispose()
$img.Dispose()
Write-Output ("wrote " + $Dst + " (" + $W + "x" + $H + ")")
