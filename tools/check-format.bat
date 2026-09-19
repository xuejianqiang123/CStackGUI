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
for %%a in (hello gallery) do (
    if not exist examples\%%a\%%a.exe (
        call build-app.bat %%a
        if errorlevel 1 exit /b 1
    )
)

echo   TSC designer\src\model.ts -^> build\modeljs
call npx --prefix designer tsc designer\src\model.ts --outDir build\modeljs --target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict
if errorlevel 1 (
    echo [fail] compiling the designer model failed
    exit /b 1
)

rem hello covers basic widgets (button/input/check/slider),
rem gallery covers newer ones (tree/table/tabs/image...)
for %%a in (hello gallery) do (
    echo.
    echo == %%a ==
    node tools\format-roundtrip.mjs examples\%%a\%%a.exe examples\%%a\%%a.cgui build\roundtrip_%%a.cgui
    if errorlevel 1 exit /b 1
)

echo.
echo [ok] format contract check passed
exit /b 0
