@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
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
rem
rem examples\ 下有两种目录：**工程目录**（自带 main.c / main.cpp）和**容器目录**
rem （examples\lessons\ 教学六课、examples\suites\ 的 studio 与 sys 套件 —— 它们自己没有源文件，
rem 工程收在各自的子目录里）。老写法一律按"工程目录"处理，于是每跑必红在容器这一行、
rem 它下面的示例还一个都没编 —— 现在：有源文件就编它自己，没有就往下编一层（2026-10-14 修；
rem 2026-10-18 示例合并 P4 后容器是 lessons\ 与 suites\；2026-10-19 comm/dev/text 并成 studio\）。
for /d %%a in (examples\*) do (
    if exist "%%a\main.c"   call :build_one "%%a" %~1
    if exist "%%a\main.cpp" call :build_one "%%a" %~1
    if not exist "%%a\main.c" if not exist "%%a\main.cpp" (
        for /d %%b in (%%a\*) do (
            if exist "%%b\main.c"   call :build_one "%%b" %~1
            if exist "%%b\main.cpp" call :build_one "%%b" %~1
        )
    )
)

echo.
if "%FAIL%"=="1" (
    echo [fail] 有示例没编过，看上面的输出
    exit /b 1
)
echo [ok] 全部构建完成
exit /b 0

rem 编一个示例工程并立刻自检；%~1 = 工程目录（相对 examples\ 的路径），%~2 = 附加参数
:build_one
echo.
echo == 示例 %~1 ==
call build-app.bat %~1 %~2
if errorlevel 1 (
    set FAIL=1
) else (
    echo   SELFTEST %~nx1
    "%~1\%~nx1.exe" --selftest "%~1\%~nx1.cgui"
    if errorlevel 1 set FAIL=1
)
exit /b 0
