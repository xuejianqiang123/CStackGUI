@echo off
rem ============================================================
rem  从源码编译一个界面工程 -> 自包含的一个目录
rem
rem  用法（在项目根执行）：
rem    build-app.bat hello              调试版（带控制台，printf 可见）
rem    build-app.bat myapp --release    发布版（-mwindows，无控制台）
rem    build-app.bat                    不带参数 = 编译 hello
rem
rem  参数是**目录名**，按 apps\ -> examples\ 的顺序找：
rem    apps\<名字>\      你自己的工程（画布导出的默认位置）
rem    examples\<名字>\  仓库自带的示例
rem
rem  产物直接落在那个工程目录里：
rem
rem    <工程目录>\
rem      <名字>.exe           <- 双击就能跑（界面文件就在旁边）
rem      lib\libcstackgui.a   <- 运行时库副本，这个目录离开项目也能看
rem      build-app.bat        <- 编译入口（双击即可重编译本工程）
rem      <名字>.cgui / main.c / assets\...
rem
rem  中间产物（各 .o、运行时库）仍然放在 build\ 下。
rem  发布给用户的话用 make-release.bat，那条路不需要源码。
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
    echo        仓库自带的示例：
    dir /b /ad examples 2>nul
    exit /b 1
)

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] 找不到 gcc。装个 MinGW-w64，或设置 MINGW 环境变量指向它的 bin 目录。
    exit /b 1
)

rem 每次都先刷新运行时库：改了 runtime\ 下的东西之后，不刷新会出现
rem 「undefined reference to cg_xxx」这种看着莫名其妙的链接错误。
rem 只有几个文件，几秒钟的事，换来「改了就能编」的确定性。
rem （build-all.bat 会先统一刷一次，然后设 CSTACKGUI_SKIP_RUNTIME=1 让这里跳过）
if not "%CSTACKGUI_SKIP_RUNTIME%"=="1" (
    echo [info] 刷新运行时库
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

rem 运行时库在工程目录里放一份：这样整个工程目录拷走也能跑、也能重编译链接
if not exist "%APPDIR%\lib" mkdir "%APPDIR%\lib"
copy /y build\lib\libcstackgui.a "%APPDIR%\lib\libcstackgui.a" >nul
if errorlevel 1 (
    echo [fail] 复制运行时库到 %APPDIR%\lib 失败
    exit /b 1
)

rem gdiplus 只用来读图片（png/jpg/bmp/gif），系统自带
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus
rem 回调签名是固定的，用不到的 self/ev/user 参数不该报 warning
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

set OUT=%APPDIR%\%APP%.exe
if "%MODE%"=="--release" (
    echo   LD %OUT% ^(release, 无控制台^)
    gcc %CFLAGS% -mwindows -o "%OUT%" "%APPDIR%\*.c" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, 带控制台^)
    gcc %CFLAGS% -o "%OUT%" "%APPDIR%\*.c" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
)
if errorlevel 1 (
    echo [fail] %APP% 编译失败
    exit /b 1
)

rem 每个工程目录里放一个自己的编译入口，双击就能重编译这一个
call :make_launcher %APP%

echo [ok] %OUT%
echo.
echo     直接跑（双击也行）:   %OUT%
echo     只校验不开窗:         %OUT% --selftest
echo     重编译本工程:         %APPDIR%\build-app.bat
exit /b 0

:make_launcher
rem %1 = 工程名
set L=%APPDIR%\build-app.bat
> "%L%" echo @echo off
>>"%L%" echo rem 本工程的编译入口（由项目根的 build-app.bat 生成，可删，删了下次编译会重建）
>>"%L%" echo rem 双击或在命令行直接跑都行：真正的编译逻辑在项目根的 build-app.bat 里，避免两份脚本各改各的。
>>"%L%" echo setlocal
>>"%L%" echo if "%%CSTACKGUI_HOME%%"=="" set CSTACKGUI_HOME=%%~dp0..\..
>>"%L%" echo if not exist "%%CSTACKGUI_HOME%%\build-app.bat" ^(
>>"%L%" echo     echo [fail] 找不到项目根：%%CSTACKGUI_HOME%%
>>"%L%" echo     echo        请把 CSTACKGUI_HOME 指到 CStackGUI 项目根目录（含 runtime\、examples\ 的那一层）
>>"%L%" echo     exit /b 1
>>"%L%" echo ^)
>>"%L%" echo call "%%CSTACKGUI_HOME%%\build-app.bat" %1 %%*
>>"%L%" echo exit /b %%errorlevel%%
exit /b 0
