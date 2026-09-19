@echo off
rem ============================================================
rem  编译一个界面工程 -> exe（**发布包里的版本**）
rem
rem  和开发仓库里那个 build-app.bat 的区别：这个不编译运行时源码，
rem  直接用 runtime\lib\libcstackgui.a 这份预编译库。所以整个发布包
rem  自包含，不需要 CStackGUI 源码也能改界面、写业务、出 exe。
rem
rem  用法：
rem    build-app.bat hello              编译 examples\hello\
rem    build-app.bat myapp              编译 apps\myapp\（你自己导出的工程）
rem    build-app.bat myapp --release    无控制台版本
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
if "%APP%"=="" set APP=hello
set MODE=%~2

set APPDIR=
if exist "apps\%APP%\"     set APPDIR=apps\%APP%
if not defined APPDIR if exist "examples\%APP%\" set APPDIR=examples\%APP%
if not defined APPDIR (
    echo [fail] 找不到工程 "%APP%"
    echo.
    echo        你自己的工程（画布导出的默认位置）：
    dir /b /ad apps 2>nul
    echo        自带的示例：
    dir /b /ad examples 2>nul
    exit /b 1
)

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] 找不到 gcc。装个 MinGW-w64（http://winlibs.com 或 MSYS2），
    echo        或设置环境变量 MINGW 指向它的 bin 目录，例如：
    echo        set MINGW=D:\mingw64\bin
    exit /b 1
)

if not exist "runtime\lib\libcstackgui.a" (
    echo [fail] 缺少 runtime\lib\libcstackgui.a，发布包不完整，请重新获取。
    exit /b 1
)

set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

set OUT=%APPDIR%\%APP%.exe
if "%MODE%"=="--release" (
    echo   LD %OUT% ^(release, 无控制台^)
    gcc %CFLAGS% -mwindows -o "%OUT%" "%APPDIR%\*.c" "runtime\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, 带控制台^)
    gcc %CFLAGS% -o "%OUT%" "%APPDIR%\*.c" "runtime\lib\libcstackgui.a" %LDFLAGS%
)
if errorlevel 1 (
    echo [fail] %APP% 编译失败
    exit /b 1
)

echo [ok] %OUT%
echo.
echo     直接跑（双击也行）:   %OUT%
echo     只校验不开窗:         %OUT% --selftest
exit /b 0
