# shot-canvas.ps1 -- 截画布（CStackGUI.exe / electron）窗口，调画布 UI 观感用
#
# 为什么需要它：tools\shot.ps1 是给 C 业务 exe 用的（Start-Process + PrintWindow），
# 画布是 Electron，改了面板/样式之后同样要「放大截图肉眼看一眼」，所以单独留一个。
#
# 用法（在项目根）：
#   powershell -ExecutionPolicy Bypass -File tools\shot-canvas.ps1
#   powershell -ExecutionPolicy Bypass -File tools\shot-canvas.ps1 -Out build\my.png
#
# 说明：
#   - 默认跑源码里的 electron（designer\node_modules\electron\dist\electron.exe），
#     加载 designer\dist\index.html —— 所以截之前先 `cd designer && npx vite build`。
#   - 它截的是**画布启动后的默认界面**。要截需要点才能看到的页签（如「支持库」），
#     先把 App.tsx 里 `useState<Tab>("props")` 临时改成那个页签，构建、截图，改回来再构建。
#   - 截法同 shot.ps1：PrintWindow flag 先 2（合成画面）再 0（WM_PRINT）兜底。
param(
  [string]$Out = "build\canvas-shot.png"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRC { public int L, T, R, B; }
public class Win32ShotCanvas {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRC r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "designer\node_modules\electron\dist\electron.exe"
$appDir = Join-Path $root "designer"
$dist = Join-Path $appDir "dist\index.html"
if (-not (Test-Path $exe)) { Write-Output "[fail] no electron at $exe (run npm install in designer\)"; exit 1 }
if (-not (Test-Path $dist)) { Write-Output "[fail] no $dist (run: cd designer && npx vite build)"; exit 1 }

$p = Start-Process -FilePath $exe -ArgumentList $appDir -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
  Start-Sleep -Milliseconds 500
  $p.Refresh()
  if ($p.HasExited) { Write-Output "HAS_EXITED code=$($p.ExitCode)"; exit 1 }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $h = $p.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { Write-Output "[fail] no window after 30s"; Stop-Process -Id $p.Id -Force; exit 1 }
Start-Sleep -Seconds 3   # 等前端渲染完（Electron 冷启动比 C exe 慢）
Write-Output "ALIVE title=[$($p.MainWindowTitle)]"

[Win32ShotCanvas]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object WRC
[Win32ShotCanvas]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L
$ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$ok = $false
foreach ($flag in @(2, 0)) {
  $hdc = $g.GetHdc()
  try { $ok = [Win32ShotCanvas]::PrintWindow($h, $hdc, $flag) } finally { $g.ReleaseHdc($hdc) }
  if ($ok) { break }
}
$g.Dispose()
if (-not $ok) { Write-Output "PrintWindow failed (画面可能全黑，重试一次)" }
$outPath = Join-Path $root $Out
$dir = Split-Path -Parent $outPath
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$bmp.Save($outPath)
$bmp.Dispose()
Write-Output "saved $outPath ${w}x${ht} printwindow=$ok"
Stop-Process -Id $p.Id -Force
