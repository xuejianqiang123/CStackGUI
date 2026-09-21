@echo off
rem ============================================================
rem  一条命令把该编的都编了：运行时库 + examples\ 下所有示例
rem
rem  用法（在项目根执行）：
rem    build-all.bat              调试版
rem    build-all.bat --release    发布版（无控制台）
rem
rem  你自己的工程（apps\ 下）不会被自动编译 —— 那些按需单独编：
rem    build-app.bat <名字>
rem ============================================================
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo == 运行时库 ==
call build-runtime.bat
if errorlevel 1 exit /b 1

rem 运行时只编一次，后面的 build-app 就不要再各刷一遍了
set CSTACKGUI_SKIP_RUNTIME=1
set FAIL=0

rem 逐个编译示例，编完立刻跑一次 --selftest：编译过只能说明语法没问题，
rem 自检过才说明 .cgui 里的事件名都能对上 C 函数。
for /d %%a in (examples\*) do (
    echo.
    echo == 示例 %%a ==
    call build-app.bat %%~nxa %~1
    if errorlevel 1 (
        set FAIL=1
    ) else (
        echo   SELFTEST %%~nxa
        "%%a\%%~nxa.exe" --selftest "%%a\%%~nxa.cgui"
        if errorlevel 1 set FAIL=1
    )
)

echo.
if "%FAIL%"=="1" (
    echo [fail] 有示例没编过，看上面的输出
    exit /b 1
)
echo [ok] 全部构建完成
exit /b 0
