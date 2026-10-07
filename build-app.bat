@echo off
rem ---- ASCII prelude: keep everything above the next marker ASCII-only ----
rem cmd decodes a .bat with the console code page captured when it started, so a
rem UTF-8 file must be (re)entered by a process whose console is already 65001;
rem otherwise the Chinese lines below get cut in the middle and run as commands.
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  CStackGUI - thin shell. ALL build logic lives in the kernel:
rem      tools\cgbuild.mjs          <- the ONE implementation
rem  (canvas F5 and the release package call the same kernel.)
rem
rem  Why this file is ASCII-only: cmd decodes a .bat with the console
rem  code page captured when cmd itself started. A UTF-8 Chinese line
rem  then gets cut in the middle and its tail is executed as a command
rem  ("'...' is not recognized as an internal or external command").
rem  The kernel is Node, which is UTF-8 everywhere - so all Chinese
rem  output lives there, and this file cannot be mis-decoded.
rem
rem  Usage: build-app.bat [project-name-or-path] [--release]
rem  Needs: MinGW-w64 (gcc/ar/windres) and Node.js 18+
rem ============================================================
setlocal
where node >nul 2>nul
if errorlevel 1 (
    echo [fail] node not found - the build kernel tools\cgbuild.mjs needs Node.js 18+.
    echo        Install Node.js, then re-run. To build without this shell:
    echo            node "%~dp0tools\cgbuild.mjs" %*
    exit /b 1
)
node "%~dp0tools\cgbuild.mjs" %*
exit /b %errorlevel%
