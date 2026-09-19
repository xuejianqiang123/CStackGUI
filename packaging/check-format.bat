@echo off
rem ============================================================
rem  Format-contract regression test (**release-package version**)
rem
rem  The seam most likely to break: the designer writes .cgui in TS,
rem  the runtime reads it in C. If their understanding of a field
rem  drifts apart, you get "designer saves fine but app won't run".
rem
rem  How it works: read a .cgui with the bundled prebuilt designer model,
rem  simulate edits, write it back, then hand it to the runtime
rem  (--selftest) to really parse. The prebuilt model lives in
rem  build\modeljs\ - only Node.js is needed, no designer sources.
rem
rem  Usage: tools\check-format.bat
rem
rem  NOTE: keep this file ASCII-only. cmd mis-parses non-ASCII comments
rem  after `chcp 65001`.
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

where node >nul 2>nul
if errorlevel 1 (
    echo [fail] Node.js is required, get it from https://nodejs.org
    exit /b 1
)
if not exist "build\modeljs\model.js" (
    echo [fail] build\modeljs\model.js missing - prebuilt designer model, package incomplete.
    exit /b 1
)

for %%a in (hello gallery) do (
    if not exist "examples\%%a\%%a.exe" (
        call build-app.bat %%a
        if errorlevel 1 exit /b 1
    )
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
