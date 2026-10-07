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
rem  CStackGUI - thin shell. Builds ONLY the runtime library
rem  (build\lib\libcstackgui.a). Real logic: tools\cgbuild.mjs
rem  with --runtime-only. Kept ASCII-only on purpose - see the
rem  comment header in build-app.bat for why.
rem
rem  You rarely need this by hand: every project build refreshes
rem  the runtime library first.
rem
rem  Needs: MinGW-w64 (gcc/ar/windres) and Node.js 18+
rem ============================================================
setlocal
where node >nul 2>nul
if errorlevel 1 (
    echo [fail] node not found - the build kernel tools\cgbuild.mjs needs Node.js 18+.
    echo        To build by hand:  node "%~dp0tools\cgbuild.mjs" --runtime-only
    exit /b 1
)
node "%~dp0tools\cgbuild.mjs" --runtime-only
exit /b %errorlevel%
