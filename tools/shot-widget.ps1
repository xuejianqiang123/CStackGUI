# shot-widget.ps1 -- 截一个 C 业务 exe 里**某一个控件**的放大图（验边框 / 滚动条 / 角标这类细节）
#
# 为什么需要它：tools\shot.ps1 截整窗，缩到聊天窗口里那 1~2px 的边框线就糊了 ——
# 判断"边框缺了一条边""聚焦环变色""箭头压边框"必须放大到像素级看。本脚本跑起 exe，
# 按**客户区坐标**裁一块再放大保存，还可以先点一下（-ClickX/-ClickY）截"聚焦/选中"后的样子。
#
# 用法（在项目根；坐标是控件的客户区坐标，容器里的控件把容器偏移加上）：
#   powershell -ExecutionPolicy Bypass -File tools\shot-widget.ps1 -Exe examples\lessons\lesson01_basics\lesson01_basics.exe `
#       -QX 16 -QY 296 -QW 400 -QH 130 -Out build\edt_multi.png
#   powershell ... -ClickX 216 -ClickY 361 -Out build\edt_multi_focus.png        # 真鼠标点一下再截
#   powershell ... -ClickX 341 -ClickY 161 -Clicks 2 -Out build\spin.png         # 连点两下再截
#   powershell ... -MsgClick -QX 216 -QY 560 -QW 224 -QH 44 -Out build\rad.png  # 发控件通知再截
#
# 点击有两条路（lesson02 实测出来的坑）：
#   A. 默认 = 真鼠标注入（SetCursorPos + mouse_event）。
#      **窗口不在前台时，第一下会被"窗口激活"吃掉** —— 所以
#        · 会切换的控件（checkbox / switch / radio / rate / pagination …）**不要**用 -Clicks 2：
#          点两下 = 勾上又取消，像素回到原样，看着像"点击没生效"（假阴性）；
#        · 每次点击都会累加的目标（数值框的调节器）才用 -Clicks 2。
#      注入前会断言 SetForegroundWindow 成功；不成功会打印警告，并用 SetWindowPos(HWND_TOPMOST)
#      抬到最前兜底，点之前再用 WindowFromPoint 校验"落点窗口属于目标进程"。
#   B. -MsgClick = 给控件发**真实控件通知**：button / checkbox / radio 用 BM_CLICK；
#      edit 的调节器（msctls_updown32）发 WM_LBUTTONDOWN/UP（按坐标落在上半/下半决定上/下箭头）。
#      不依赖前台、窗口被盖住也有效、结果确定；其它控件类打印 unsupported 而不是静默不点。
#   两条路都会把"实际走的路径 + 结果"打到 stdout。
#   **点击有没有生效，必须看"点了之后应该变什么"（控件状态 / 状态栏那句话）；
#     只看"这张图看着正常"不算证据 —— 注入失败会静默产出一张和成功一样的图。**
#
# 输出：-Out 那张（放大 Zoom 倍）+ 同目录 <名字>_full.png（整窗，便于对照位置）。
# 说明：坐标必须是**窗口客户区**坐标（和 .cgui 里 rect 的 x/y 一致，但 .cgui 是相对父容器：
#       顶层控件直接用；放进 group/tabs 的控件要自己加上容器坐标）。
param(
  [string]$Exe = "",
  [string]$Cgui = "",
  [int]$QX = 0, [int]$QY = 0, [int]$QW = 0, [int]$QH = 0,
  [int]$ClickX = -1, [int]$ClickY = -1,
  [int]$Clicks = 1,
  [switch]$MsgClick,
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
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECTW r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out WRECTW r);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref WPTW p);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(WPTW p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
  private delegate bool EnumProcW(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProcW cb, IntPtr l);
  private static System.Collections.Generic.List<IntPtr> s_kids = new System.Collections.Generic.List<IntPtr>();
  /* 控件窗口类名：Button / Edit / msctls_updown32 / SysMonthCal32 …，-MsgClick 用它决定发哪种通知 */
  public static string ClassOf(IntPtr h) {
    var s = new System.Text.StringBuilder(256);
    GetClassNameW(h, s, 256);
    return s.ToString();
  }
  /* 所有后代控件 HWND（含容器里的子控件）；顺序不保证 z 序，命中判定要配合"面积最小" */
  public static IntPtr[] Children(IntPtr parent) {
    s_kids.Clear();
    EnumChildWindows(parent, delegate(IntPtr h, IntPtr l) { s_kids.Add(h); return true; }, IntPtr.Zero);
    return s_kids.ToArray();
  }
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

# ① 前台断言：SetForegroundWindow 会被系统的前台锁拒绝（返回 False），这时注入的第一下
#    会被"窗口激活"吃掉 —— 必须出声，不许静默继续。
$raised = $false
[Win32WidgetShot]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 700
$fg = [Win32WidgetShot]::GetForegroundWindow()
$fgState = if ($fg -eq $hwnd) { "self" } else { "other" }
if ($fgState -ne "self") {
  Write-Output "warn: SetForegroundWindow 没生效（前台不是本窗口）—— 注入点击的第一下会被「窗口激活」吃掉；要连点请加 -Clicks 2，或改用 -MsgClick"
  if ($ClickX -ge 0 -and -not $MsgClick) {
    # ② 兜底：抬到最前（不依赖前台权限）；点之前下面还会用 WindowFromPoint 校验落点
    $raised = [Win32WidgetShot]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0043)   # HWND_TOPMOST|NOSIZE|NOMOVE|SHOWWINDOW
    Start-Sleep -Milliseconds 400
    Write-Output "raised=topmost ok=$raised"
  }
}

$rcWin = New-Object WRECTW
$rcCli = New-Object WRECTW
[Win32WidgetShot]::GetWindowRect($hwnd, [ref]$rcWin) | Out-Null
[Win32WidgetShot]::GetClientRect($hwnd, [ref]$rcCli) | Out-Null
$ptOrg = New-Object WPTW
[Win32WidgetShot]::ClientToScreen($hwnd, [ref]$ptOrg) | Out-Null
Write-Output ("window={0},{1} client={2}x{3} clientOrigin={4},{5}" -f $rcWin.L, $rcWin.T, $rcCli.R, $rcCli.B, $ptOrg.X, $ptOrg.Y)

$nClick = [Math]::Max(1, $Clicks)
if ($ClickX -lt 0) {
  Write-Output "click=none"
} elseif ($MsgClick) {
  # ③ -MsgClick：取"包含该点、面积最小"的子控件（最贴身的那个），按它的窗口类发真实通知。
  #    不能取"最后一个枚举到的"：EnumChildWindows 不保证压在上面的先出来 ——
  #    edit 的调节器与它自己的 Edit 重叠时，Edit 面积更大但可能后枚举，会选错。
  $sx = $ptOrg.X + $ClickX
  $sy = $ptOrg.Y + $ClickY
  $hit = [IntPtr]::Zero
  $hitL = 0; $hitT = 0; $hitW = 0; $hitH = 0
  $best = [int]::MaxValue
  foreach ($k in [Win32WidgetShot]::Children($hwnd)) {
    $r = New-Object WRECTW
    [Win32WidgetShot]::GetWindowRect($k, [ref]$r) | Out-Null
    $aw = $r.R - $r.L
    $ah = $r.B - $r.T
    if ($aw -le 0 -or $ah -le 0) { continue }
    if ($sx -lt $r.L -or $sx -ge $r.R -or $sy -lt $r.T -or $sy -ge $r.B) { continue }
    $area = $aw * $ah
    if ($area -le $best) { $best = $area; $hit = $k; $hitL = $r.L; $hitT = $r.T; $hitW = $aw; $hitH = $ah }
  }
  $cls = ""
  if ($hit -ne [IntPtr]::Zero) { $cls = [Win32WidgetShot]::ClassOf($hit) }
  if ($cls -eq "Button") {
    # button / checkbox / radio / switch 都是 Button 类；BM_CLICK = 一次完整点击（会发 BN_CLICKED）
    for ($i = 0; $i -lt $nClick; $i++) { [Win32WidgetShot]::SendMessage($hit, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    Write-Output ("click=msgclick class=Button hwnd={0} x{1}" -f $hit, $nClick)
  } elseif ($cls -eq "msctls_updown32") {
    # edit 的调节器：上/下半各一个箭头；lParam 是控件内相对坐标
    $rx = $sx - $hitL
    $ry = $sy - $hitT
    $arrow = if ($ry -lt ($hitH / 2)) { "up" } else { "down" }
    $lp = New-Object IntPtr -ArgumentList ([int64](($ry -shl 16) -bor ($rx -band 0xFFFF)))
    for ($i = 0; $i -lt $nClick; $i++) {
      [Win32WidgetShot]::SendMessage($hit, 0x0201, [IntPtr]::Zero, $lp) | Out-Null   # WM_LBUTTONDOWN
      Start-Sleep -Milliseconds 120
      [Win32WidgetShot]::SendMessage($hit, 0x0202, [IntPtr]::Zero, $lp) | Out-Null   # WM_LBUTTONUP
    }
    Write-Output ("click=msgclick class=msctls_updown32 hwnd={0} x{1} arrow={2} rel={3},{4}" -f $hit, $nClick, $arrow, $rx, $ry)
  } else {
    Write-Output ("click=msgclick unsupported class=[{0}] —— 这一下没点（图还会照截）；可改用 -ClickX/-ClickY 注入" -f $cls)
  }
  Start-Sleep -Milliseconds 700
} else {
  # 真鼠标注入：点之前先确认"落点上的窗口真属于目标进程"，否则大概率点在别的窗口上（静默失败）
  $tx = $ptOrg.X + $ClickX
  $ty = $ptOrg.Y + $ClickY
  $pt = New-Object WPTW
  $pt.X = $tx
  $pt.Y = $ty
  $atPid = 0
  [Win32WidgetShot]::GetWindowThreadProcessId([Win32WidgetShot]::WindowFromPoint($pt), [ref]$atPid) | Out-Null
  $owner = if ($atPid -eq $proc.Id) { "self" } else { "other" }
  if ($owner -ne "self") {
    Write-Output ("warn: 落点 {0},{1} 上的窗口不属于目标进程（被别的窗口盖住？）—— 这一下大概率点不到，别把它当证据" -f $tx, $ty)
  }
  [Win32WidgetShot]::SetCursorPos($tx, $ty) | Out-Null
  for ($i = 0; $i -lt $nClick; $i++) {
    Start-Sleep -Milliseconds 200
    [Win32WidgetShot]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
    [Win32WidgetShot]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
  }
  Start-Sleep -Milliseconds 700
  $fgNow = if ([Win32WidgetShot]::GetForegroundWindow() -eq $hwnd) { "self" } else { "other" }
  Write-Output ("click=inject client={0},{1} screen={2},{3} x{4} fg_before={5} fg_after={6} raised={7} point_owner={8}" -f $ClickX, $ClickY, $tx, $ty, $nClick, $fgState, $fgNow, $raised, $owner)
}
if ($ClickX -ge 0) { Write-Output "note: 上面 click= 那行说的是「实际走的哪条路」；点击有没有生效请看控件/状态栏的变化，别只看图" }

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
