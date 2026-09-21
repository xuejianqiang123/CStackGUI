<#
  bat-gbk.ps1 - keep every .bat of this repo in the code page of a Chinese
  Windows console (GBK / cp936).

  Why: cmd.exe decodes a batch file with the console code page captured when
  the cmd process started. A UTF-8 .bat that contains Chinese is therefore
  decoded 2-bytes-per-character instead of 3, cmd loses its place in the file
  and starts executing the tail of the comment lines ("... is not recognized
  as an internal or external command"). This shows up when the file is
  double-clicked (console = 936) and NOT when it is run from a shell that
  already sits on code page 65001, which is why it is easy to test green by
  accident. Files that are pure ASCII are safe in any code page.

  Rules enforced (all .bat except node_modules / build / release):
    - GBK (cp936) encoded, no BOM, CRLF line ends
    - not "valid UTF-8 that contains CJK" (= a UTF-8 file that needs fixing)

  Usage (from anywhere):
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-gbk.ps1
    powershell ... -File tools\bat-gbk.ps1 -Mode fix
    powershell ... -File tools\bat-gbk.ps1 -Mode show -Path build-app.bat
    powershell ... -File tools\bat-gbk.ps1 -Mode utf8 -Path examples\notepad\build-app.bat

  -Mode check : report violations, exit 1 if any (used by tools\check-format.bat)
  -Mode fix   : convert the violating files from UTF-8 to GBK in place
  -Mode show  : print one file's text (decoded from GBK) to stdout
  -Mode utf8  : convert one file GBK -> UTF-8 so an editor can touch it;
                run "-Mode fix" afterwards to put it back

  Editing workflow: convert to UTF-8 with -Mode utf8, edit, run -Mode fix.
#>
[CmdletBinding()]
param(
    [ValidateSet('check', 'fix', 'show', 'utf8')][string]$Mode = 'check',
    [string[]]$Path
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$gbk = [Text.Encoding]::GetEncoding(936)
$utf8Strict = New-Object Text.UTF8Encoding($false, $true)

function Get-BatFiles {
    $root = Split-Path -Parent $PSScriptRoot
    Get-ChildItem -Path $root -Filter *.bat -Recurse -File | Where-Object {
        $_.FullName -notmatch '\\(node_modules|build|release|\.git|\.codebuddy|\.vscode)\\|\\designer\\release\\'
    } | Sort-Object FullName
}

function Get-Violations([string]$file) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $text = $gbk.GetString($bytes)
    $problems = @()

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $problems += 'has a UTF-8 BOM (cmd then reports the first line as a bad command)'
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $problems += 'is UTF-16 (cmd cannot run UTF-16 batch files)'
    }

    $cjk = [regex]::Matches($text, '[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]').Count
    if ($cjk -gt 0) {
        try {
            $asUtf8 = $utf8Strict.GetString($bytes)
            $cjkUtf8 = [regex]::Matches($asUtf8, '[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]').Count
            if ($cjkUtf8 -ge 2) {
                $problems += "is UTF-8 with Chinese text ($cjkUtf8 CJK chars) - cmd will mis-parse it on a 936 console"
            }
        } catch { }
    }

    # line ends are counted on the raw bytes: decoding UTF-8 bytes as GBK can
    # swallow a CR as the "trail byte" of a bogus pair, which fakes lone LFs.
    $lf = 0
    $crlf = 0
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -eq 0x0A) {
            $lf++
            if ($i -gt 0 -and $bytes[$i - 1] -eq 0x0D) { $crlf++ }
        }
    }
    if ($lf -ne $crlf) {
        $problems += "has $($lf - $crlf) lone LF line end(s) - batch blocks/labels need CRLF"
    }
    return $problems
}

function Convert-File($file, $from, $to, $what) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $text = $from.GetString($bytes)
    [IO.File]::WriteAllText($file, $text, $to)
    '  {0} -> {1}  {2}' -f $what, $to.WebName, (Resolve-Path -Relative $file)
}

switch ($Mode) {
    'check' {
        $bad = 0
        $files = if ($Path) { $Path | ForEach-Object { Get-Item $_ } } else { Get-BatFiles }
        foreach ($f in $files) {
            $problems = Get-Violations $f.FullName
            if ($problems.Count -gt 0) {
                $bad++
                '[FAIL] ' + (Resolve-Path -Relative $f.FullName)
                foreach ($p in $problems) { '       ' + $p }
            }
        }
        if ($bad -eq 0) {
            'bat-gbk: {0} file(s) checked, all GBK/cp936 + CRLF, no BOM' -f $files.Count
            exit 0
        }
        'bat-gbk: {0} file(s) need fixing - run: powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-gbk.ps1 -Mode fix' -f $bad
        exit 1
    }
    'fix' {
        $files = if ($Path) { $Path | ForEach-Object { Get-Item $_ } } else { Get-BatFiles }
        $fixed = 0
        foreach ($f in $files) {
            $problems = Get-Violations $f.FullName
            if ($problems.Count -eq 0) { continue }
            $bytes = [IO.File]::ReadAllBytes($f.FullName)
            $text = [Text.Encoding]::UTF8.GetString($bytes)
            if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
                $text = $text.Substring(1)
            }
            $text = [regex]::Replace($text, "(?<!`r)`n", "`r`n")
            [IO.File]::WriteAllText($f.FullName, $text, $gbk)
            $fixed++
            '  converted to GBK: ' + (Resolve-Path -Relative $f.FullName)
        }
        'bat-gbk: {0} file(s) converted, {1} already fine' -f $fixed, ($files.Count - $fixed)
        exit 0
    }
    'show' {
        foreach ($p in $Path) { [IO.File]::ReadAllText((Get-Item $p).FullName, $gbk) }
        exit 0
    }
    'utf8' {
        $utf8NoBom = New-Object Text.UTF8Encoding($false)
        foreach ($p in $Path) { Convert-File (Get-Item $p).FullName $gbk $utf8NoBom 'utf8' }
        exit 0
    }
}
