# 提取 exe 关联图标存成 png（肉眼验证图标是否打进去了）
# 用法: powershell -NoProfile -File tools\check-icon.ps1 <exe路径> <输出png路径>
param([string]$Exe, [string]$Out)
Add-Type -AssemblyName System.Drawing
$i = [System.Drawing.Icon]::ExtractAssociatedIcon((Resolve-Path $Exe))
$i.ToBitmap().Save($Out)
Write-Host "[ok] $Out"
