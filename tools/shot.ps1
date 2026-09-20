# shot.ps1 -- 跑一个界面并截图，调 GUI 外观用
# 用法（在项目根或发布包根）：
#   powershell -ExecutionPolicy Bypass -File tools\shot.ps1                          # 默认截 gallery
#   powershell -ExecutionPolicy Bypass -File tools\shot.ps1 apps\main\main.exe      # 截自己的工程
#   powershell -ExecutionPolicy Bypass -File tools\shot.ps1 x.exe ui.cgui out.png
#
# 截法：优先 PrintWindow（让目标窗口把自己画进内存 DC），别的窗口压在它上面也能截对；
#       老系统 PrintWindow 失败时才退回 CopyFromScreen（那种情况才要求窗口在前台）。
param(
  [string]$Exe = "",
  [string]$Cgui = "",
  [string]$Out = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRECT { public int L, T, R, B; }
public class Win32Cap {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe)  { $Exe  = Join-Path $root "examples\gallery\gallery.exe" }
if (-not $Cgui) { $Cgui = (Join-Path (Split-Path -Parent $Exe) ((Split-Path -Leaf $Exe) -replace '\.exe$', '.cgui')) }
if (-not $Out)  { $Out  = Join-Path $root "build\shot.png" }

$p = Start-Process -FilePath $Exe -ArgumentList $Cgui -PassThru
Start-Sleep -Seconds 4
$p.Refresh()
if ($p.HasExited) { Write-Output "HAS_EXITED code=$($p.ExitCode)"; exit 1 }
Write-Output "ALIVE title=[$($p.MainWindowTitle)]"

# 尽量把窗口提到前台（只为观感/回调正常；截屏本身不依赖它）
[Win32Cap]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800

$r = New-Object WRECT
[Win32Cap]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L
$h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)

# PrintWindow：窗口自己画自己，不怕被别的窗口遮住（SetForegroundWindow 常被系统拒绝）。
# 先试 PW_RENDERFULLCONTENT(2)：拿合成后的画面，和屏幕一致；
# flag 0 走 WM_PRINT，某些控件（STATIC 等）会画出不一样的底色，只作兜底。
$ok = $false
foreach ($flag in @(2, 0)) {
  $hdc = $g.GetHdc()
  try { $ok = [Win32Cap]::PrintWindow($p.MainWindowHandle, $hdc, $flag) } finally { $g.ReleaseHdc($hdc) }
  if ($ok) { break }
}
if (-not $ok) {
  # 回退：屏幕拷贝（要求窗口的确在前台）
  Write-Output "PrintWindow failed, falling back to CopyFromScreen"
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
}
$g.Dispose()

$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$bmp.Save($Out)
$bmp.Dispose()
Write-Output "saved $Out ${w}x${h}"
Stop-Process -Id $p.Id -Force
