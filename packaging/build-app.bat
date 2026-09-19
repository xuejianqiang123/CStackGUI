@echo off
rem ============================================================
rem  Build a UI project into an exe (**release-package version**)
rem
rem  Difference from the source-repo build-app.bat: this one never
rem  compiles runtime sources, it links the prebuilt runtime\lib\libcstackgui.a.
rem  The whole package is self-contained: no CStackGUI sources needed.
rem
rem  Usage:
rem    build-app.bat hello              build examples\hello\
rem    build-app.bat myapp              build apps\myapp\ (your project)
rem    build-app.bat myapp --release    no console
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001` (the designer's F5 run uses it).
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
if "%APP%"=="" set APP=hello
set MODE=%~2

set APPDIR=
if exist "apps\%APP%\"     set APPDIR=apps\%APP%
if not defined APPDIR if exist "examples\%APP%\" set APPDIR=examples\%APP%
if not defined APPDIR (
    echo [fail] project "%APP%" not found
    echo.
    echo        your projects - default export dir of the designer:
    dir /b /ad apps 2>nul
    echo        bundled examples:
    dir /b /ad examples 2>nul
    exit /b 1
)

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64 from winlibs.com or MSYS2, or set
    echo        the MINGW env var to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)

if not exist "runtime\lib\libcstackgui.a" (
    echo [fail] runtime\lib\libcstackgui.a missing, package incomplete. Get it again.
    exit /b 1
)

set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

set OUT=%APPDIR%\%APP%.exe
if "%MODE%"=="--release" (
    echo   LD %OUT% ^(release, no console^)
    gcc %CFLAGS% -mwindows -o "%OUT%" "%APPDIR%\*.c" "runtime\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, with console^)
    gcc %CFLAGS% -o "%OUT%" "%APPDIR%\*.c" "runtime\lib\libcstackgui.a" %LDFLAGS%
)
if errorlevel 1 (
    echo [fail] build of %APP% failed
    exit /b 1
)

echo [ok] %OUT%
echo.
echo     run it (double-click works):   %OUT%
echo     self-check without window:     %OUT% --selftest

rem Refresh the clangd language-service config (compile_commands.json, generated
rem by tools\gen-cc.mjs; used by the clangd plugin in CodeBuddy/VSCode).
rem Silently skipped when node is missing; does not affect the build.
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0
