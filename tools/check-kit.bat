@echo off
rem ============================================================
rem  Smoke test for the cg_kit API (files / paths / INI / text+encoding /
rem  clipboard / external process / registry).
rem
rem  cg_kit is a pile of Win32 wrapper code (BOM, CRLF, where to insert an
rem  INI key, encoding round-trips) that the designer and the gallery never
rem  touch -- one wrong line breaks it silently. Run this after editing
rem  anything under runtime\src\kit\.
rem
rem  It compiles tools\kit-test.c against the freshly built library, then runs
rem  it inside build\kittest\ so its scratch files never touch the repo.
rem
rem  Usage: tools\check-kit.bat
rem
rem  NOTE: keep this file ASCII-only (cmd mis-parses non-ASCII after chcp).
rem ============================================================
chcp 65001 >nul
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    exit /b 1
)
if not exist build\lib\libcstackgui.a (
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

if not exist build\kittest mkdir build\kittest

echo   CC tools\kit-test.c
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include tools\kit-test.c -o build\kittest\kit-test.exe build\lib\libcstackgui.a -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
if errorlevel 1 (
    echo [fail] compiling tools\kit-test.c failed
    exit /b 1
)

pushd build\kittest
kit-test.exe
set "KITRC=%errorlevel%"
popd
if not "%KITRC%"=="0" (
    echo [fail] cg_kit smoke test failed
    exit /b 1
)

echo.
echo [ok] cg_kit smoke test passed
exit /b 0
