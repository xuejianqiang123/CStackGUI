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
rem  CStackGUI release package - thin shell.
rem  ALL build logic lives in the kernel tools\cgbuild.mjs; it is
rem  executed by the canvas itself:   CStackGUI.exe --build ...
rem  (the source repo's build-app.bat calls the very same kernel,
rem   so there is still exactly ONE implementation.)
rem
rem  ASCII-only on purpose - see the header comment in the repo's
rem  build-app.bat for why (cmd + UTF-8 Chinese = lines cut in half).
rem
rem  Usage: build-app.bat [project-name-or-path] [--release]
rem  Needs: MinGW-w64 (gcc/ar/windres). Node.js is NOT required --
rem         CStackGUI.exe carries its own.
rem ============================================================
setlocal
if not exist "%~dp0CStackGUI.exe" (
    echo [fail] CStackGUI.exe not found next to this script.
    echo        Keep the release package intact, or re-download it.
    exit /b 1
)
"%~dp0CStackGUI.exe" --build %*
exit /b %errorlevel%
