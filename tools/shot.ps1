Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct WRECT { public int L, T, R, B; }
public class Win32Cap {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECT r);
}
'@

$exe = 'd:\CPro\CStackGUI\examples\gallery\gallery.exe'
$cgui = 'd:\CPro\CStackGUI\examples\gallery\gallery.cgui'
$p = Start-Process -FilePath $exe -ArgumentList $cgui -PassThru
Start-Sleep -Seconds 4
$p.Refresh()
if ($p.HasExited) { Write-Output "HAS_EXITED code=$($p.ExitCode)"; exit 1 }
Write-Output "ALIVE title=[$($p.MainWindowTitle)]"
[Win32Cap]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object WRECT
[Win32Cap]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L
$h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('d:\CPro\CStackGUI\build\shot_new.png')
$bmp.Dispose()
Write-Output "saved ${w}x${h}"
Stop-Process -Id $p.Id -Force
