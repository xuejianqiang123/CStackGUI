@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  把界面工程编译成 exe（**发布包版本**）
rem
rem  与源码仓库里那份 build-app.bat 的区别：这里不编译运行时源码，
rem  只链接预编译好的 runtime\lib\libcstackgui.a。整个包是自包含的，
rem  不需要任何 CStackGUI 源码。
rem
rem  用法：
rem    build-app.bat gallery            编 examples\gallery\
rem    build-app.bat myapp              编 apps\myapp\（你自己的工程）
rem    build-app.bat myapp --release    无控制台
rem ============================================================
setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
rem 画布（F5 运行）用环境变量 CG_APP_DIR 传工程目录，而不是 argv ——
rem spawn 出来的 cmd 转义会把路径搞坏；手动执行时仍然走 %1。
if "%APP%"=="" if not "%CG_APP_DIR%"=="" set "APP=%CG_APP_DIR%"
if "%APP%"=="" set APP=gallery
set MODE=%~2

rem %APP% 既可以是裸工程名（apps\<名字> / examples\<名字>，老约定），也可以是
rem 相对发布包根的路径（用户把工程放在任意目录里，比如 "main" 或 "work\demo"）。
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

rem exe 名 = 工程目录名（%APP% 的最后一段）
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

rem comdlg32 供通用对话框（打开/保存文件、选颜色）使用
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

rem 资源对象（清单 + 版本信息）里没有代码符号，链接器不会主动从静态库里取它 ——
rem 显式链接，exe 才能拿到 v6 通用控件清单（页签/进度条才是新外观）。
rem 工程自带的 <名字>.rc（画布「程序配置」对话框生成的）会顶掉通用那份：
rem 里面是本程序的产品名 / 版本 / 作者 / 图标。它是 UTF-8（含中文），
rem 所以加 --codepage=65001；-I runtime\res 让它找到清单和默认图标。
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

rem 增量编译：把 <工程>\*.c 逐个编成 lib\<名字>.o，只在该 .o 不存在、或者不比
rem 源文件 / 工程头文件 / cstackgui.h 新时才编。删掉的源文件自然从链接列表里
rem 消失。（运行时库每次都会重新链接，所以故意不拿它判定工程要不要重编。）
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

rem 刷新 clangd 语言服务配置（compile_commands.json，由 tools\gen-cc.mjs 生成，
rem 供 CodeBuddy/VSCode 里的 clangd 插件使用）。没装 node 时静默跳过，不影响编译。
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0
