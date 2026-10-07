# probe-hotreload.ps1 -- end-to-end check for ".cgui external change hot reload"
#
# Pure ASCII on purpose: PowerShell 5.1 reads .ps1 using the ANSI code page,
# Chinese text in a script shows up as "quote not closed" style parse errors.
#
# Repo note: lives under tools\ on purpose (build\ is not committed). This is the
# permanent regression for the hot-reload feature (F1): external edit -> canvas
# reloads, self-save must NOT be reported as an external change, bad JSON ignored.
# Re-run: powershell -NoProfile -ExecutionPolicy Bypass -File tools\probe-hotreload.ps1
#
# Steps:
#   1. copy examples\darkterm\darkterm.cgui -> build\hotreload.cgui
#   2. start the canvas (electron) pointing at that file, CSTACKGUI_LOG -> build\hotreload.log
#   3. screenshot -> build\hotreload-before.png
#   4. change the app title in that file from OUTSIDE the canvas
#   5. wait, screenshot -> build\hotreload-after.png   (title on canvas must be the NEW one)
#   6. kill the canvas, print the log tail
param(
  [string]$OutDir = "build"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRC2 { public int L, T, R, B; }
public class Win32Shot2 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRC2 r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "designer\node_modules\electron\dist\electron.exe"
$app  = Join-Path $root "designer"
$src  = Join-Path $root "examples\darkterm\darkterm.cgui"
$dst  = Join-Path $root "$OutDir\hotreload.cgui"
$log  = Join-Path $root "$OutDir\hotreload.log"
if (-not (Test-Path $exe)) { Write-Output "[fail] no electron at $exe"; exit 1 }

$enc = New-Object System.Text.UTF8Encoding $false
$txt = [System.IO.File]::ReadAllText($src, [System.Text.Encoding]::UTF8)
[System.IO.File]::WriteAllText($dst, $txt, $enc)
if (Test-Path $log) { Remove-Item $log -Force }

$env:CSTACKGUI_LOG = $log
$p = Start-Process -FilePath $exe -ArgumentList @("`"$app`"", "`"$dst`"") -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
  Start-Sleep -Milliseconds 500
  $p.Refresh()
  if ($p.HasExited) { Write-Output "HAS_EXITED code=$($p.ExitCode)"; exit 1 }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $h = $p.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { Write-Output "[fail] no window after 30s"; Stop-Process -Id $p.Id -Force; exit 1 }
Write-Output "ALIVE title=[$($p.MainWindowTitle)]"
Start-Sleep -Seconds 5

function Shot([string]$path) {
  [Win32Shot2]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 500
  $r = New-Object WRC2
  [Win32Shot2]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L
  $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $ok = $false
  foreach ($flag in @(2, 0)) {
    $hdc = $g.GetHdc()
    try { $ok = [Win32Shot2]::PrintWindow($h, $hdc, $flag) } finally { $g.ReleaseHdc($hdc) }
    if ($ok) { break }
  }
  $g.Dispose()
  $bmp.Save($path)
  $bmp.Dispose()
  Write-Output "shot $path printwindow=$ok"
}

Shot (Join-Path $root "$OutDir\hotreload-before.png")

# ---- change the .cgui from OUTSIDE the canvas (only the first "title" = app.title) ----
$re = New-Object System.Text.RegularExpressions.Regex '"title"\s*:\s*"[^"]*"'
$now = [System.IO.File]::ReadAllText($dst, [System.Text.Encoding]::UTF8)
$new = $re.Replace($now, '"title": "HOT RELOAD OK"', 1)
[System.IO.File]::WriteAllText($dst, $new, $enc)
Write-Output "outside edit done: $($now.Length) -> $($new.Length) bytes"

Start-Sleep -Milliseconds 3000
Shot (Join-Path $root "$OutDir\hotreload-after.png")

# ---- A3: saving from INSIDE the canvas must not look like an outside change ----
# (if it did, every Ctrl+S would reload the doc and wipe the undo stack)
Add-Type -AssemblyName System.Windows.Forms
[Win32Shot2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
[System.Windows.Forms.SendKeys]::SendWait("^s")
Start-Sleep -Milliseconds 2500
Shot (Join-Path $root "$OutDir\hotreload-after-save.png")
Write-Output "file:changed lines in log = $((Select-String -Path $log -Pattern 'file:changed' -SimpleMatch).Count) (must be 1: only the outside edit)"

# ---- A4: an outside write that is not valid .cgui must be ignored, not crash ----
[System.IO.File]::WriteAllText($dst, "{ this is not json at all", $enc)
Start-Sleep -Milliseconds 2500
Shot (Join-Path $root "$OutDir\hotreload-after-badjson.png")

Stop-Process -Id $p.Id -Force
Start-Sleep -Milliseconds 400
Write-Output "--- CSTACKGUI_LOG tail ---"
if (Test-Path $log) { Get-Content $log -Tail 10 } else { Write-Output "(no log)" }
