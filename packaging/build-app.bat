@echo off
rem ============================================================
rem  Build a UI project into an exe (**release-package version**)
rem
rem  Difference from the source-repo build-app.bat: this one never
rem  compiles runtime sources, it links the prebuilt runtime\lib\libcstackgui.a.
rem  The whole package is self-contained: no CStackGUI sources needed.
rem
rem  Usage:
rem    build-app.bat gallery            build examples\gallery\
rem    build-app.bat myapp              build apps\myapp\ (your project)
rem    build-app.bat myapp --release    no console
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001` (the designer's F5 run uses it).
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
if "%APP%"=="" set APP=gallery
set MODE=%~2

rem %APP% is either a bare project name (apps\<name> / examples\<name>, old
rem convention) or a path relative to the package root (any folder the user
rem put their project in, e.g. "main" or "work\demo").
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

rem exe name = project folder name (basename of %APP%)
for %%I in ("%APP%") do set NAME=%%~nxI
if not defined NAME (
    echo [fail] cannot derive exe name from "%APP%"
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

rem comdlg32 is used by the common dialogs (open/save file, choose color)
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

rem The resource object (manifest + version info) has no code symbols, so the
rem linker never pulls it out of the static lib -- link it directly so the exe
rem gets the v6 common controls manifest (modern tabs/progress).
rem A project-level <name>.rc (designer "program config" dialog) replaces the
rem generic one: it carries this app's product name / version / author / icon.
rem It is UTF-8 (Chinese inside) so --codepage=65001; -I runtime\res finds the
rem manifest + default icon it references.
if not exist "%APPDIR%\lib" mkdir "%APPDIR%\lib"
set RESOBJ=runtime\lib\cstackgui_res.o
if exist "%APPDIR%\%NAME%.rc" (
    echo   RC %APPDIR%\%NAME%.rc
    windres --codepage=65001 -I "%APPDIR%" -I "runtime\res" -O coff "%APPDIR%\%NAME%.rc" -o "%APPDIR%\lib\%NAME%_res.o"
    if errorlevel 1 (
        echo [fail] compiling %APPDIR%\%NAME%.rc failed
        exit /b 1
    )
    set RESOBJ=%APPDIR%\lib\%NAME%_res.o
)

set OUT=%APPDIR%\%NAME%.exe

rem Incremental: compile each <project>\*.c to lib\<name>.o, only when the .o is
rem missing or not strictly newer than the source / project headers / cstackgui.h.
rem Deleted sources just drop out of the link list. (The runtime lib is always
rem re-linked, so it deliberately does NOT gate the app compile.)
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
    gcc %CFLAGS% -mwindows -o "%OUT%" !OBJS! "%RESOBJ%" "runtime\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, with console^)
    gcc %CFLAGS% -o "%OUT%" !OBJS! "%RESOBJ%" "runtime\lib\libcstackgui.a" %LDFLAGS%
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
