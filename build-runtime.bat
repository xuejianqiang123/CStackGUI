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
rem  (build\lib\libcstackgui.a). Same logic, two routes:
rem      CStackGUI.exe --build --runtime-only          (customer, no Node)
rem      node designer\electron\cgbuild.mjs --runtime-only   (developer)
rem
rem  *** ASCII ONLY - NO EXCEPTIONS *** - see build-app.bat for
rem  why (cmd + UTF-8 Chinese = a later line gets cut in half and
rem  its tail runs as a command).
rem
rem  You rarely need this by hand: every project build refreshes
rem  the runtime library first.
rem ============================================================
setlocal
set "EXE=%~dp0CStackGUI.exe"
if exist "%EXE%" (
    "%EXE%" --build --runtime-only
    exit /b %errorlevel%
)

where node >nul 2>nul
if errorlevel 1 (
    echo [fail] neither CStackGUI.exe nor node.exe is available.
    exit /b 1
)
node "%~dp0designer\electron\cgbuild.mjs" --runtime-only
exit /b %errorlevel%
