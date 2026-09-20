@echo off
rem ============================================================
rem  Build a UI project into a standalone exe (source-repo version)
rem
rem  Usage (run at the repo root):
rem    build-app.bat gallery            debug build (console, printf visible)
rem    build-app.bat myapp --release    release build (-mwindows, no console)
rem    build-app.bat                    no args = first project under apps\, else examples\
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001` (the designer's F5 run uses it), turning rem
rem  lines into bogus commands. Chinese docs live in README.md.
rem ============================================================
chcp 65001 >nul
setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
rem The designer passes the project directory via CG_APP_DIR (env, not argv:
rem cmd quoting of paths inside spawn gets mangled). Manual use stays %1.
if "%APP%"=="" if not "%CG_APP_DIR%"=="" set "APP=%CG_APP_DIR%"
rem No argument: build the first project under apps\, else the first example.
rem (This used to default to "hello", an example that was deleted when the two
rem  examples were merged into examples\gallery.)
if "%APP%"=="" for /d %%a in (apps\*) do if not defined APP set "APP=%%~nxa"
if "%APP%"=="" for /d %%a in (examples\*) do if not defined APP set "APP=%%~nxa"
set MODE=%~2

rem %APP% is a bare project name (apps\<name> / examples\<name>) or a path
rem relative to this root -- the designer passes e.g. "apps\gallery".
set APPDIR=
if exist "apps\%APP%\"     set APPDIR=apps\%APP%
if not defined APPDIR if exist "examples\%APP%\" set APPDIR=examples\%APP%
if not defined APPDIR if exist "%APP%\" set APPDIR=%APP%
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
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    echo        to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)

rem Always refresh the runtime lib first: after editing runtime\, skipping
rem this gives confusing "undefined reference to cg_xxx" link errors.
rem (build-all.bat refreshes once and sets CSTACKGUI_SKIP_RUNTIME=1)
if not "%CSTACKGUI_SKIP_RUNTIME%"=="1" (
    echo [info] refreshing runtime library
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

rem Keep a copy of the runtime lib in the project dir, so the folder can
rem be moved away and still rebuild.
if not exist "%APPDIR%\lib" mkdir "%APPDIR%\lib"
copy /y build\lib\libcstackgui.a "%APPDIR%\lib\libcstackgui.a" >nul
if errorlevel 1 (
    echo [fail] cannot copy runtime lib to %APPDIR%\lib
    exit /b 1
)
rem The resource object (manifest + version info) has no code symbols, so the
rem linker never pulls it out of the static lib -- it must be linked directly,
rem otherwise the exe gets no v6 common controls (classic ugly tabs/progress).
copy /y build\obj\cstackgui_res.o "%APPDIR%\lib\cstackgui_res.o" >nul
if errorlevel 1 (
    echo [fail] cannot copy resource object to %APPDIR%\lib
    exit /b 1
)

rem gdiplus is only used to load images (png/jpg/bmp/gif), shipped with Windows
rem comdlg32 is used by the common dialogs (open/save file, choose color)
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
rem Callbacks have fixed signatures; unused self/ev/user args must not warn
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

rem exe name = project folder name (basename of %APP%)
for %%I in ("%APP%") do set NAME=%%~nxI
set OUT=%APPDIR%\%NAME%.exe

rem A project-level <name>.rc (written by the designer's "program config" dialog)
rem replaces the generic runtime one: it carries this app's product name / version /
rem author / icon. It is UTF-8 (Chinese strings inside), hence --codepage=65001;
rem -I runtime\res lets it pull the v6 common-controls manifest.
set RESOBJ=%APPDIR%\lib\cstackgui_res.o
if exist "%APPDIR%\%NAME%.rc" (
    echo   RC %APPDIR%\%NAME%.rc
    windres --codepage=65001 -I "%APPDIR%" -I "runtime\res" -O coff "%APPDIR%\%NAME%.rc" -o "%APPDIR%\lib\%NAME%_res.o"
    if errorlevel 1 (
        echo [fail] compiling %APPDIR%\%NAME%.rc failed
        exit /b 1
    )
    set RESOBJ=%APPDIR%\lib\%NAME%_res.o
)

rem Incremental: compile each <project>\*.c to lib\<name>.o, but only when the .o is
rem missing or not strictly newer than the source. A newer project header or the public
rem cstackgui.h also forces a recompile, so nothing silently links old code. Deleted
rem sources just drop out of the link list. (The runtime lib is always re-linked, so it
rem deliberately does NOT gate the app compile -- otherwise every relink would be stale.)
set "HDRT=0"
for %%h in ("%APPDIR%\*.h") do if "%%~th" GTR "!HDRT!" set "HDRT=%%~th"
for %%x in ("runtime\include\cstackgui.h") do if "%%~tx" GTR "!HDRT!" set "HDRT=%%~tx"

set "OBJS="
for %%f in ("%APPDIR%\*.c") do (
    set "SRCF=%%~ff"
    set "OBJ=%APPDIR%\lib\%%~nf.o"
    set "DO=1"
    if exist "!OBJ!" (
        for %%o in ("!OBJ!") do set "OT=%%~to"
        for %%c in ("!SRCF!") do set "CT=%%~tc"
        if "!OT!" GTR "!CT!" if "!OT!" GTR "!HDRT!" set "DO="
    )
    if defined DO (
        echo   CC !SRCF!
        gcc %CFLAGS% -c "!SRCF!" -o "!OBJ!"
        if errorlevel 1 (
            echo [fail] compiling !SRCF! failed
            exit /b 1
        )
    )
    set "OBJS=!OBJS! "!OBJ!""
)

if "%MODE%"=="--release" (
    echo   LD %OUT% ^(release, no console^)
    gcc %CFLAGS% -mwindows -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, with console^)
    gcc %CFLAGS% -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
)
if errorlevel 1 (
    echo [fail] build of %APP% failed
    exit /b 1
)

rem Drop a launcher into the project dir so double-click rebuilds it
call :make_launcher %APP%

echo [ok] %OUT%
echo.
echo     run it (double-click works):   %OUT%
echo     self-check without window:     %OUT% --selftest
echo     rebuild this project:          %APPDIR%\build-app.bat

rem Refresh the clangd language-service config (tools\gen-cc.mjs mirrors the
rem flags used here). Silently skipped when node is missing or not at repo root.
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0

:make_launcher
rem %1 = project name (the folder name at generation time)
set L=%APPDIR%\build-app.bat
> "%L%" echo @echo off
>>"%L%" echo rem Build entry for this project (generated by the repo-root build-app.bat).
>>"%L%" echo rem The real build logic lives in the repo-root build-app.bat, so the two never drift.
>>"%L%" echo setlocal
>>"%L%" echo if "%%CSTACKGUI_HOME%%"=="" set CSTACKGUI_HOME=%%~dp0..\..
>>"%L%" echo if not exist "%%CSTACKGUI_HOME%%\build-app.bat" ^(
>>"%L%" echo     echo [fail] project root not found: %%CSTACKGUI_HOME%%
>>"%L%" echo     echo        set CSTACKGUI_HOME to the CStackGUI root dir (the one with runtime\ and apps\)
>>"%L%" echo     exit /b 1
>>"%L%" echo ^)
>>"%L%" echo rem The project name is THIS folder's name, so renaming the folder is fine.
>>"%L%" echo for %%%%I in ("%%~dp0.") do set CG_APP=%%%%~nI
>>"%L%" echo call "%%CSTACKGUI_HOME%%\build-app.bat" %%CG_APP%% %%*
>>"%L%" echo exit /b %%errorlevel%%
exit /b 0
