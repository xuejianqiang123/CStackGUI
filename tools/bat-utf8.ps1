<#
  bat-utf8.ps1 - keep every .bat of this repo runnable with Chinese text in BOTH
  a 936 console (double-click) and a 65001 one (IDE terminal).

  Why the prelude: cmd decodes a batch file with the console code page captured
  when the cmd process started. A UTF-8 .bat that contains Chinese therefore has
  its lines cut in the middle when the console is 936, and the comment tails are
  executed as commands ("... is not recognized"). Switching the code page INSIDE
  the same file does not help either (measured in a real console: it breaks in
  both 936 and 65001). The fix is to make the console 65001 FIRST and re-enter
  the very same file through a fresh cmd - that process decodes the whole file as
  UTF-8 from byte 0 and never switches code page again:

      @echo off
      rem ---- ASCII prelude (keep this part pure ASCII) ----
      for /f "tokens=2 delims=:" %c in ('chcp') do set "CG_CP=%c"
      if not "%CG_CP: =%"=="65001" (
          chcp 65001 >nul
          cmd /c "%~f0" %*
          exit /b %errorlevel%
      )
      rem ---- UTF-8 content starts here: Chinese is fine below ----

  Rules enforced for every .bat:
    1. UTF-8 encoded, no BOM   (a BOM makes cmd report the first line as a bad command)
    2. CRLF line ends
    3. the prelude above is present, and it comes BEFORE the first non-ASCII byte
    4. no other "chcp" / "cls" line outside the prelude
    5. no "goto" / "call :label" in a file that contains non-ASCII: cmd looks up a
       label by scanning the file with BYTE offsets, and 3-byte UTF-8 Chinese makes
       that scan drift (symptom: a stray "*" error at the end, or a surprise
       "Press any key"). Put the subroutine in its own .bat and call that instead -
       that is why tools\gen-launcher.bat exists.

  Usage (from anywhere):
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode show -Path build-app.bat

  -Mode check : report violations, exit 1 if any (wired into tools\check-format.bat)
  -Mode fix   : strip BOM / convert GBK -> UTF-8 / normalize CRLF / insert the prelude
  -Mode show  : print one file's text (auto-detect UTF-8 or GBK) to stdout
#>
[CmdletBinding()]
param(
    [ValidateSet('check', 'fix', 'show')][string]$Mode = 'check',
    [string[]]$Path
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$bom = [byte[]](0xEF, 0xBB, 0xBF)
$utf8Strict = New-Object Text.UTF8Encoding($false, $true)
$utf8Bom = New-Object Text.UTF8Encoding($false)
$gbk = [Text.Encoding]::GetEncoding(936)

$prelude = @(
    'rem ---- ASCII prelude: keep everything above the next marker ASCII-only ----'
    'rem cmd decodes a .bat with the console code page captured when it started, so a'
    'rem UTF-8 file must be (re)entered by a process whose console is already 65001;'
    'rem otherwise the Chinese lines below get cut in the middle and run as commands.'
    'for /f "tokens=2 delims=:" %%c in (''chcp'') do set "CG_CP=%%c"'
    'if not "%CG_CP: =%"=="65001" ('
    '    chcp 65001 >nul'
    '    cmd /c "%~f0" %*'
    '    exit /b %errorlevel%'
    ')'
    'rem ---- UTF-8 content starts here: Chinese is fine below ----'
)
$marker = 'cmd /c "%~f0" %*'

function Get-BatFiles {
    $root = Split-Path -Parent $PSScriptRoot
    Get-ChildItem -Path $root -Filter *.bat -Recurse -File | Where-Object {
        $_.FullName -notmatch '\\(node_modules|build|release|\.git|\.codebuddy|\.vscode)\\|\\designer\\release\\'
    } | Sort-Object FullName
}

function Get-Text($file) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $hadBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    try {
        $text = $utf8Strict.GetString($bytes)
        $encoding = 'utf-8'
    } catch {
        $text = $gbk.GetString($bytes)
        $encoding = 'gbk'
    }
    [pscustomobject]@{ Text = $text; HadBom = $hadBom; Encoding = $encoding }
}

function Get-Problems($file) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $info = Get-Text $file
    $text = $info.Text
    $problems = @()

    if ($info.HadBom) { $problems += 'has a UTF-8 BOM - keep these files BOM-less: cmd takes the BOM as part of the first command line on a 936 console, and on 65001 it skips the 3 bytes but keeps counting them, so long files drift (measured: 18 broken lines)' }
    if ($info.Encoding -eq 'gbk') { $problems += 'is not valid UTF-8 (GBK?) - this repo keeps .bat files in UTF-8' }

    $lf = 0; $crlf = 0
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -eq 0x0A) { $lf++; if ($i -gt 0 -and $bytes[$i - 1] -eq 0x0D) { $crlf++ } }
    }
    if ($lf -ne $crlf) { $problems += "has $($lf - $crlf) lone LF line end(s) - batch blocks/labels need CRLF" }

    if (-not $text.Contains($marker)) {
        $problems += 'has no prelude: add the ASCII block that switches the code page and re-enters the file'
    } else {
        $markerAt = $text.IndexOf($marker)
        $firstNonAscii = -1
        $start = 0
        if ($info.HadBom) { $start = 1 }   # the BOM itself is expected, ignore it
        for ($i = $start; $i -lt $text.Length; $i++) {
            if ([int][char]$text[$i] -gt 127) { $firstNonAscii = $i; break }
        }
        if ($firstNonAscii -ge 0 -and $firstNonAscii -lt $markerAt) {
            $problems += 'has non-ASCII text above the prelude (must stay ASCII until the code page is 65001)'
        }
        if ($firstNonAscii -ge 0 -and $text -match '(?m)(^|\|\||&&|&)\s*(goto\s|call\s*:)') {
            $problems += 'has goto / call :label while containing non-ASCII - cmd scans labels by byte offset and UTF-8 Chinese makes it drift; move the subroutine to its own .bat'
        }
        $after = $text.Substring($markerAt)
        if ($after -match '(?m)^\s*(chcp|cls)\b') {
            $problems += 'still has a "chcp"/"cls" line after the prelude - move it into the prelude or drop it'
        }
    }
    return $problems
}

function Repair-File($file) {
    $info = Get-Text $file
    $text = $info.Text
    if ($info.HadBom) { $text = $text.Substring(1) }
    $text = [regex]::Replace($text, "(?<!`r)`n", "`r`n")

    if (-not $text.Contains($marker)) {
        $lines = $text -split "`r`n"
        # drop every chcp line, plus a cls line that only existed to hide the BOM error
        $kept = @()
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\s*chcp\b') {
                if ($i + 1 -lt $lines.Count -and $lines[$i + 1] -match '^\s*cls\b') { $i++ }
                continue
            }
            $kept += $lines[$i]
        }
        $lines = $kept
        $head = 0
        while ($head -lt $lines.Count -and $lines[$head].Trim() -eq '') { $head++ }
        if ($head -lt $lines.Count -and $lines[$head] -match '^@echo off') { $at = $head + 1 }
        else { $lines = , '@echo off' + $lines; $at = 1 }
        $out = @($lines[0..($at - 1)]) + $prelude + @($lines[$at..($lines.Count - 1)])
        $text = ($out -join "`r`n")
    }

    [IO.File]::WriteAllText($file, $text, $utf8Bom)
}

switch ($Mode) {
    'check' {
        $bad = 0
        $files = if ($Path) { $Path | ForEach-Object { Get-Item $_ } } else { Get-BatFiles }
        foreach ($f in $files) {
            $problems = Get-Problems $f.FullName
            if ($problems.Count -gt 0) {
                $bad++
                '[FAIL] ' + (Resolve-Path -Relative $f.FullName)
                foreach ($p in $problems) { '       ' + $p }
            }
        }
        if ($bad -eq 0) {
            'bat-utf8: {0} file(s) checked - UTF-8 BOM-less + CRLF + ASCII prelude' -f $files.Count
            exit 0
        }
        'bat-utf8: {0} file(s) need fixing - run: powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix' -f $bad
        exit 1
    }
    'fix' {
        $files = if ($Path) { $Path | ForEach-Object { Get-Item $_ } } else { Get-BatFiles }
        $fixed = 0
        foreach ($f in $files) {
            if ((Get-Problems $f.FullName).Count -eq 0) { continue }
            Repair-File $f.FullName
            $fixed++
            '  fixed: ' + (Resolve-Path -Relative $f.FullName)
        }
        'bat-utf8: {0} file(s) fixed, {1} already fine' -f $fixed, ($files.Count - $fixed)
        exit 0
    }
    'show' {
        foreach ($p in $Path) { (Get-Text (Get-Item $p).FullName).Text }
        exit 0
    }
}
