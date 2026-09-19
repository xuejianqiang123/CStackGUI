@echo off
rem ============================================================
rem  Format-contract regression: a .cgui written by the designer
rem  must be read back by the C runtime with selftest passing.
rem
rem  The seam most likely to break: the designer writes .cgui in TS,
rem  the runtime reads it in C. After changing .cgui fields or widget
rem  kinds, run this.
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001` (the designer's F5 run uses it).
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

if not exist build\lib\libcstackgui.a (
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)
rem Artifacts live in each project dir (examples\<name>\<name>.exe)
if not exist examples\gallery\gallery.exe (
    call build-app.bat gallery
    if errorlevel 1 exit /b 1
)

echo   TSC designer\src\model.ts -^> build\modeljs
call npx --prefix designer tsc designer\src\model.ts --outDir build\modeljs --target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict
if errorlevel 1 (
    echo [fail] compiling the designer model failed
    exit /b 1
)

rem gallery is the merged showcase: page 0 basic widgets,
rem page 1 tree/table/tabs/image..., page 2 timer/canvas/dialogs/new widgets
echo   roundtrip gallery
node tools\format-roundtrip.mjs examples\gallery\gallery.exe examples\gallery\gallery.cgui build\roundtrip_gallery.cgui
if errorlevel 1 exit /b 1

echo.
echo [ok] format contract check passed
exit /b 0
