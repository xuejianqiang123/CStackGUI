# shot-widget.ps1 -- 截一个 C 业务 exe 里**某一个控件**的放大图（验边框 / 滚动条 / 角标这类细节）
#
# 为什么需要它：tools\shot.ps1 截整窗，缩到聊天窗口里那 1~2px 的边框线就糊了 ——
# 判断"边框缺了一条边""聚焦环变色""箭头压边框"必须放大到像素级看。本脚本跑起 exe，
# 按**客户区坐标**裁一块再放大保存，还可以先点一下（-ClickX/-ClickY）截"聚焦/选中"后的样子。
#
# 用法（在项目根；坐标是控件的客户区坐标，容器里的控件把容器偏移加上）：
#   powershell -ExecutionPolicy Bypass -File tools\shot-widget.ps1 -Exe examples\lesson01_basics\lesson01_basics.exe `
#       -QX 16 -QY 296 -QW 400 -QH 130 -Out build\edt_multi.png
#   powershell ... -ClickX 216 -ClickY 361 -Out build\edt_multi_focus.png   # 先点一下再截
#
# ⚠ 点击的坑（lesson02/lesson03 实测）：
#   1) 窗口不在前台时，**第一次 down/up 会被"激活窗口"吃掉** → 单击常有假阴性（会误判成框架 bug）。
#      所以 -Clicks 默认设成 2。但：
#   2) **-Clicks 2 只适合"累加型"目标**（数值框箭头：两下 +5+5）。**会切换的控件**
#      （checkbox / switch / radio / rate / pagination）点两下 = 回去又回来、像素回到原样，
#      对它们请：先对一个**空白坐标**空点一下激活窗口，再 -Clicks 1 点目标。
#   3) **一定要看"点了之后应该变什么"**（状态栏文字 / 选中态 / 数值）—— 失败截图与成功截图
#      长得一模一样，只有"该变的东西变了"才算证据。要表达时序（先点 A 再点 B）本工具做不到，
#      改用 EnumChildWindows 找控件 HWND 发真实控件通知（BM_CLICK 等）。
#
# 输出：-Out 那张（放大 Zoom 倍）+ 同目录 <名字>_full.png（整窗，便于对照位置）。
# 说明：坐标必须是**窗口客户区**坐标（和 .cgui 里 rect 的 x/y 一致，但 .cgui 是相对父容器：
#       顶层控件直接用；放进 group/tabs 的控件要自己加上容器坐标）。
param(
  [string]$Exe = "",
  [string]$Cgui = "",
  [int]$QX = 0, [int]$QY = 0, [int]$QW = 0, [int]$QH = 0,
  [int]$ClickX = -1, [int]$ClickY = -1,
  [int]$Clicks = 2,
  [int]$Pad = 6,
  [int]$Zoom = 3,
  [int]$WaitSec = 3,
  [string]$Out = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRECTW { public int L, T, R, B; }
public struct WPTW { public int X, Y; }
public class Win32WidgetShot {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECTW r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out WRECTW r);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref WPTW p);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
}
'@

$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { Write-Output "[fail] 需要 -Exe <业务 exe>"; exit 1 }
if (-not $Cgui) { $Cgui = (Join-Path (Split-Path -Parent $Exe) ((Split-Path -Leaf $Exe) -replace '\.exe$', '.cgui')) }
if (-not $Out) { $Out = Join-Path $root "build\widget-shot.png" }
if ($QW -le 0 -or $QH -le 0) { Write-Output "[fail] 需要 -QW/-QH（控件宽高，客户区坐标）"; exit 1 }
if (-not [System.IO.Path]::IsPathRooted($Exe)) { $Exe = Join-Path $root $Exe }
if (-not [System.IO.Path]::IsPathRooted($Cgui)) { $Cgui = Join-Path $root $Cgui }
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }

$proc = Start-Process -FilePath $Exe -ArgumentList $Cgui -PassThru
Start-Sleep -Seconds $WaitSec
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 20; $i++) {
  $proc.Refresh()
  if ($proc.HasExited) { Write-Output "HAS_EXITED code=$($proc.ExitCode)"; exit 1 }
  if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
  Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "[fail] 5s 内没等到窗口"; Stop-Process -Id $proc.Id -Force; exit 1 }
Write-Output "ALIVE title=[$($proc.MainWindowTitle)]"
[Win32WidgetShot]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 700

$rcWin = New-Object WRECTW
$rcCli = New-Object WRECTW
[Win32WidgetShot]::GetWindowRect($hwnd, [ref]$rcWin) | Out-Null
[Win32WidgetShot]::GetClientRect($hwnd, [ref]$rcCli) | Out-Null
$ptOrg = New-Object WPTW
[Win32WidgetShot]::ClientToScreen($hwnd, [ref]$ptOrg) | Out-Null
Write-Output ("window={0},{1} client={2}x{3} clientOrigin={4},{5}" -f $rcWin.L, $rcWin.T, $rcCli.R, $rcCli.B, $ptOrg.X, $ptOrg.Y)

if ($ClickX -ge 0) {
  [Win32WidgetShot]::SetCursorPos(($ptOrg.X + $ClickX), ($ptOrg.Y + $ClickY)) | Out-Null
  for ($n = 0; $n -lt [Math]::Max(1, $Clicks); $n++) {
    Start-Sleep -Milliseconds 200
    [Win32WidgetShot]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
    [Win32WidgetShot]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
  }
  Start-Sleep -Milliseconds 700
  Write-Output ("clicked client {0},{1} x{2}" -f $ClickX, $ClickY, [Math]::Max(1, $Clicks))
}

$winW = $rcWin.R - $rcWin.L
$winH = $rcWin.B - $rcWin.T
$full = New-Object System.Drawing.Bitmap -ArgumentList $winW, $winH
$gfx = [System.Drawing.Graphics]::FromImage($full)
$ok = $false
# PrintWindow：窗口自己画自己，不怕别的窗口压在上面。flag 2 = 合成画面（和屏幕一致），0 = WM_PRINT 兜底
foreach ($flag in @(2, 0)) {
  $hdc = $gfx.GetHdc()
  try { $ok = [Win32WidgetShot]::PrintWindow($hwnd, $hdc, $flag) } finally { $gfx.ReleaseHdc($hdc) }
  if ($ok) { break }
}
$gfx.Dispose()
Write-Output "printwindow=$ok"

# 客户区原点相对窗口原点的偏移 -> 把客户区坐标换算成截图坐标
$offX = $ptOrg.X - $rcWin.L
$offY = $ptOrg.Y - $rcWin.T
$cutX = $QX - $Pad + $offX
$cutY = $QY - $Pad + $offY
$cutW = $QW + 2 * $Pad
$cutH = $QH + 2 * $Pad
if ($cutX -lt 0) { $cutW += $cutX; $cutX = 0 }
if ($cutY -lt 0) { $cutH += $cutY; $cutY = 0 }
Write-Output ("crop: x={0} y={1} w={2} h={3}" -f $cutX, $cutY, $cutW, $cutH)
$rectSrc = New-Object System.Drawing.Rectangle -ArgumentList ([int]$cutX), ([int]$cutY), ([int]$cutW), ([int]$cutH)
$zoomW = [int]($cutW * $Zoom)
$zoomH = [int]($cutH * $Zoom)
$big = New-Object System.Drawing.Bitmap -ArgumentList $zoomW, $zoomH
$gfx2 = [System.Drawing.Graphics]::FromImage($big)
$gfx2.InterpolationMode = 'NearestNeighbor'   # 放大不许平滑：要看清每个像素
$gfx2.PixelOffsetMode = 'Half'
$rectDst = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $zoomW, $zoomH
$gfx2.DrawImage($full, $rectDst, $rectSrc, 'Pixel')
$gfx2.Dispose()

$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$big.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$big.Dispose()
$full.Save(($Out -replace '\.png$', '_full.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$full.Dispose()
Write-Output ("saved {0} zoom={1}" -f $Out, $Zoom)
Stop-Process -Id $proc.Id -Force
