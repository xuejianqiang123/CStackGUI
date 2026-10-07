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
rem  (build\lib\libcstackgui.a). Real logic: tools\cgbuild.c
rem  run with --runtime-only. ASCII-only on purpose - see the
rem  header in build-app.bat for why.
rem
rem  You rarely need this by hand: every project build refreshes
rem  the runtime library first.
rem
rem  Requirements: MinGW-w64 only. No Node.js.
rem ============================================================
setlocal
set "KERN=%~dp0build\cgbuild.exe"
set "SRC=%~dp0tools\cgbuild.c"

if "%MINGW%"=="" set "MINGW=C:\mingw64\bin"
set "PATH=%MINGW%;%PATH%"

if not exist "%SRC%" (
    echo [fail] missing %SRC%
    exit /b 1
)
set "STALE="
if not exist "%KERN%" set "STALE=1"
if exist "%KERN%" for %%I in ("%SRC%") do for %%J in ("%KERN%") do if "%%~tI" GTR "%%~tJ" set "STALE=1"
if defined STALE (
    echo [boot] building the build kernel: %SRC%
    gcc -std=c11 -O2 -o "%KERN%" "%SRC%"
    if errorlevel 1 (
        echo.
        echo [fail] cannot build the kernel - gcc not found?
        echo            set MINGW=D:\mingw64\bin
        exit /b 1
    )
)

"%KERN%" --runtime-only
exit /b %errorlevel%
