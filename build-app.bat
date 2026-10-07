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
rem  CStackGUI - thin shell. ALL build logic is in the kernel:
rem      tools\cgbuild.c  ->  build\cgbuild.exe
rem  (canvas F5, CStackGUI.exe --build and the release package all
rem   call that same exe - there is still exactly ONE implementation.)
rem
rem  ASCII-only on purpose: cmd decodes a .bat with the console code
rem  page captured when cmd started, so a UTF-8 Chinese line gets cut
rem  in half and its tail runs as a command. No non-ASCII here means
rem  nothing can be mis-decoded on a customer machine.
rem
rem  Requirements: MinGW-w64 only. No Node.js.
rem  Usage: build-app.bat [project-name-or-path] [--release]
rem ============================================================
setlocal
set "KERN=%~dp0build\cgbuild.exe"
set "SRC=%~dp0tools\cgbuild.c"

rem 客户可能只装了 MinGW 而没把它加进 PATH —— 和内核用同一个默认值。
if "%MINGW%"=="" set "MINGW=C:\mingw64\bin"
set "PATH=%MINGW%;%PATH%"

if not exist "%SRC%" (
    echo [fail] missing %SRC%
    exit /b 1
)

rem Bootstrap: the kernel is plain C, so gcc can build it on any
rem machine that has MinGW. Rebuild it when the source is newer.
set "STALE="
if not exist "%KERN%" set "STALE=1"
if exist "%KERN%" for %%I in ("%SRC%") do for %%J in ("%KERN%") do if "%%~tI" GTR "%%~tJ" set "STALE=1"
if defined STALE (
    echo [boot] building the build kernel: %SRC%
    gcc -std=c11 -O2 -o "%KERN%" "%SRC%"
    if errorlevel 1 (
        echo.
        echo [fail] cannot build the kernel - gcc not found?
        echo        Install MinGW-w64, or set MINGW to its bin dir:
        echo            set MINGW=D:\mingw64\bin
        exit /b 1
    )
)

"%KERN%" %*
exit /b %errorlevel%
