@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  格式契约回归测试（**发布包版本**）
rem
rem  最容易出问题的接缝：画布用 TS 写 .cgui，运行时用 C 读它。
rem  两边对同一个字段的理解一旦跑偏，就会出现「画布存得好好的，程序跑不起来」。
rem
rem  做法：用随包预编译的画布模型读一个 .cgui，模拟改动后写回，再交给
rem  运行时（--selftest）真解析一遍。预编译模型放在 build\modeljs\ ——
rem  只需要 Node.js，不需要画布源码。
rem
rem  用法：tools\check-format.bat
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

for %%a in (gallery) do (
    if not exist "examples\%%a\%%a.exe" (
        call build-app.bat %%a
        if errorlevel 1 exit /b 1
    )
)

rem gallery 覆盖面最广（基础控件 + 树/表格/页签/图片……）
for %%a in (gallery) do (
    echo.
    echo == %%a ==
    node tools\format-roundtrip.mjs examples\%%a\%%a.exe examples\%%a\%%a.cgui build\roundtrip_%%a.cgui
    if errorlevel 1 exit /b 1
)

echo.
echo [ok] format contract check passed
exit /b 0
