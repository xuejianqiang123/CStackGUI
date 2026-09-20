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
chcp 65001 >nul
setlocal enabledelayedexpansion
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

rem Sources are grouped by layer under runtime\src:
rem   core\   backend-agnostic: JSON, doc + kind table, widget model, events, lifecycle
rem   win32\  Win32 backend: window, native controls, message mapping, dialogs, tray
rem   skin\   GDI+ appearance layer (replace the whole folder to swap in a D2D kernel)
rem Each layer is on the include path so the files can include each other by bare name;
rem runtime\src itself is deliberately NOT on it -- cross-layer includes must be explicit.
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src\core -Iruntime\src\win32 -Iruntime\src\skin
set CHANGED=
set OBJS=

rem Compile every .c under core\ win32\ skin\ -- no hand-maintained file list to forget.
rem Object name = source basename (build\obj\<name>.o), so two folders must never hold
rem files with the same name. The naming convention guarantees it:
rem core = cg_*, backend = cg_win_*, appearance = cg_skin_*.
rem Incremental: only recompile a source whose .o is missing or not strictly newer
rem than it. The "YYYY/MM/DD HH:MM" timestamp compares chronologically as a string;
rem "not strictly newer" counts as stale so a same-minute edit is never missed.
for %%d in (core win32 skin) do (
    for %%f in ("runtime\src\%%d\*.c") do (
        set "SRCF=%%~ff"
        set "OBJ=build\obj\%%~nf.o"
        set "OBJS=!OBJS! build\obj\%%~nf.o"
        set "DO=1"
        if exist "!OBJ!" (
            for %%o in ("!OBJ!") do set "OT=%%~to"
            for %%c in ("!SRCF!") do set "CT=%%~tc"
            if "!OT!" GTR "!CT!" set "DO="
        )
        if defined DO (
            echo   CC runtime\src\%%d\%%~nxf
            gcc %CFLAGS% -c "!SRCF!" -o "!OBJ!"
            if errorlevel 1 goto :fail
            set "CHANGED=1"
        )
    )
)

rem Resource object (manifest + version info + icon): rebuild if its .rc / .ico moved
set "RCOBJ=build\obj\cstackgui_res.o"
set "DO=1"
if exist "!RCOBJ!" (
    for %%o in ("!RCOBJ!") do set "OT=%%~to"
    for %%c in ("runtime\res\cstackgui.rc") do set "CT=%%~tc"
    for %%c in ("runtime\res\cstackgui.ico") do set "IT=%%~tc"
    if "!OT!" GTR "!CT!" if "!OT!" GTR "!IT!" set "DO="
)
if defined DO (
    echo   RC runtime\res\cstackgui.rc
    windres --include-dir=runtime\res -O coff runtime\res\cstackgui.rc -o "!RCOBJ!"
    if errorlevel 1 goto :fail
    set "CHANGED=1"
)

rem Re-archive only when something actually changed (or the lib is missing)
if not exist build\lib\libcstackgui.a set "CHANGED=1"
if defined CHANGED (
    echo   AR build\lib\libcstackgui.a
    ar rcs build\lib\libcstackgui.a !OBJS! build\obj\cstackgui_res.o
    if errorlevel 1 goto :fail
    echo [ok] build\lib\libcstackgui.a
) else (
    echo   (runtime up to date, skipped compile^)
)

rem Refresh the clangd language-service config (mirrors the flags above).
rem Silently skipped when node is missing or not at repo root.
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0

:fail
echo [fail] runtime build failed
exit /b 1
