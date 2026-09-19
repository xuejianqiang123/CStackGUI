@echo off
rem ============================================================
rem  Build the CStackGUI runtime library -> build\lib\libcstackgui.a
rem
rem  Only needed after editing runtime\ or on first use; normal app
rem  builds go through build-app.bat (which refreshes it anyway).
rem
rem  Optional env var:
rem    MINGW   MinGW-w64 bin dir (default C:\mingw64\bin)
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001` (the designer's F5 run uses it).
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    echo        to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)

if not exist build\obj mkdir build\obj
if not exist build\lib mkdir build\lib

set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src
set SRC=cg_json cg_doc cg_app cg_timer cg_win32

for %%f in (%SRC%) do (
    echo   CC runtime\src\%%f.c
    gcc %CFLAGS% -c runtime\src\%%f.c -o build\obj\%%f.o
    if errorlevel 1 goto :fail
)

echo   RC runtime\res\cstackgui.rc
windres --include-dir=runtime\res -O coff runtime\res\cstackgui.rc -o build\obj\cstackgui_res.o
if errorlevel 1 goto :fail

ar rcs build\lib\libcstackgui.a build\obj\cg_json.o build\obj\cg_doc.o build\obj\cg_app.o build\obj\cg_timer.o build\obj\cg_win32.o build\obj\cstackgui_res.o
if errorlevel 1 goto :fail

echo [ok] build\lib\libcstackgui.a

rem Refresh the clangd language-service config (mirrors the flags above).
rem Silently skipped when node is missing or not at repo root.
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0

:fail
echo [fail] runtime build failed
exit /b 1
