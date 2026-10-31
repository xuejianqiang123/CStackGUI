# shot-tabs.ps1 -- 逐页签截图（PostMessage 点 tab，不需要真鼠标）
# 用法：powershell -Bypass -File tools\shot-tabs.ps1 <exe> <cgui> <outdir>
param([string]$Exe, [string]$Cgui, [string]$OutDir = "build")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRECT2 { public int L, T, R, B; }
public class Cap2 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECT2 r);
  [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe)  { $Exe  = Join-Path $root "examples\toolbox\toolbox.exe" }
if (-not $Cgui) { $Cgui = Join-Path $root "examples\toolbox\toolbox.cgui" }

# 页签在 tab 客户区内的点击 x（按条目中心估算）与输出名
$tabs = @(
  @{ n=1; x=79;  name="p1-opts"   },  # 常用设置
  @{ n=2; x=141; name="p2-mytools"},  # 我的工具
  @{ n=3; x=202; name="p3-dev"    },  # 系统工具
  @{ n=4; x=265; name="p4-clean"  },  # 清理优化
  @{ n=5; x=330; name="p5-privacy"},  # 隐私设置
  @{ n=6; x=396; name="p6-apps"   },  # 应用管理
  @{ n=7; x=461; name="p7-rmenu"  }   # 右键增强
)
foreach ($t in $tabs) {
  $p = Start-Process -FilePath $Exe -ArgumentList $Cgui -PassThru
  Start-Sleep -Seconds 2
  $p.Refresh()
  if ($p.HasExited) { Write-Output "[$($t.name)] HAS_EXITED"; continue }
  $main = $p.MainWindowHandle
  $tab  = [Cap2]::FindWindowEx($main, [IntPtr]::Zero, "SysTabControl32", $null)
  if ($tab -eq [IntPtr]::Zero) { Write-Output "[$($t.name)] tab not found"; Stop-Process -Id $p.Id -Force; continue }
  $lp = [IntPtr](($t.x) -bor (20 -shl 16))   # (x, y=20) 页签头行
  [Cap2]::PostMessage($tab, 0x0201, [IntPtr]1, $lp) | Out-Null   # WM_LBUTTONDOWN
  Start-Sleep -Milliseconds 120
  [Cap2]::PostMessage($tab, 0x0202, [IntPtr]::Zero, $lp) | Out-Null # WM_LBUTTONUP
  Start-Sleep -Milliseconds 700
  $r = New-Object WRECT2
  [Cap2]::GetWindowRect($main, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [Cap2]::PrintWindow($main, $hdc, 2)
  $g.ReleaseHdc($hdc) | Out-Null
  $g.Dispose()
  $out = Join-Path $root "$OutDir\shot-$($t.name).png"
  $bmp.Save($out); $bmp.Dispose()
  Write-Output "[$($t.name)] saved ($ok)"
  Stop-Process -Id $p.Id -Force
  Start-Sleep -Milliseconds 300
}
