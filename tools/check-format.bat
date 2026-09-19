@echo off
rem ============================================================
rem  格式契约回归：画布写出的 .cgui 必须能被 C 运行时读进来自检通过
rem
rem  这条缝最容易坏——画布用 TS 写、运行时用 C 读，一旦字段理解漂移，
rem  就会出现「画布能存但程序跑不起来」。改 .cgui 字段或控件类型后请跑这个。
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

if not exist build\lib\libcstackgui.a (
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)
rem 产物在各自工程目录里（examples\<名字>\<名字>.exe）
for %%a in (hello gallery) do (
    if not exist examples\%%a\%%a.exe (
        call build-app.bat %%a
        if errorlevel 1 exit /b 1
    )
)

echo   TSC designer\src\model.ts -^> build\modeljs
call npx --prefix designer tsc designer\src\model.ts --outDir build\modeljs --target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict
if errorlevel 1 (
    echo [fail] 编译画布模型失败
    exit /b 1
)

rem hello 走简单控件（按钮/输入/勾选/滑块），gallery 走新控件（树/表格/标签页/图片…）
for %%a in (hello gallery) do (
    echo.
    echo == %%a ==
    node tools\format-roundtrip.mjs examples\%%a\%%a.exe examples\%%a\%%a.cgui build\roundtrip_%%a.cgui
    if errorlevel 1 exit /b 1
)

echo.
echo [ok] 格式契约检查通过
exit /b 0
