@echo off
rem ============================================================
rem  把界面工程编译成独立 exe（源码仓库内版本）
rem
rem  用法（在项目根执行）：
rem    build-app.bat gallery            调试版（带控制台，printf 可见）
rem    build-app.bat myapp --release    发布版（-mwindows，无控制台）
rem    build-app.bat                    不带参数 = 取 apps\ 下第一个工程，没有则取 examples\ 下的
rem
rem  注意：本文件是 GBK（cp936）+ CRLF 行尾。
rem  所以 cmd 按"启动这个 cmd 进程时的控制台代码页"解析 bat，UTF-8 的中文会被按 2 字节/字解码，
rem  行定位错位、注释后半句被当命令执行；改完用 tools\bat-gbk.ps1 -Mode fix 归位（check-format 会查）。
rem  中文使用说明见 README.md。
rem ============================================================
chcp 65001 >nul
setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
rem 画布（F5 运行）用环境变量 CG_APP_DIR 传工程目录，而不是 argv ——
rem spawn 出来的 cmd 转义会把路径搞坏；手动执行时仍然走 %1。
if "%APP%"=="" if not "%CG_APP_DIR%"=="" set "APP=%CG_APP_DIR%"
rem 不带参数：取 apps\ 下第一个工程，没有则取 examples\ 下第一个。
rem （以前默认是 hello 那个示例，后来两个示例合并成 examples\gallery 时被删了。）
if "%APP%"=="" for /d %%a in (apps\*) do if not defined APP set "APP=%%~nxa"
if "%APP%"=="" for /d %%a in (examples\*) do if not defined APP set "APP=%%~nxa"
set MODE=%~2

rem %APP% 既可以是裸工程名（apps\<名字> / examples\<名字>），也可以是相对本根的路径 ——
rem 画布传进来的就是 "apps\gallery" 这种。
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

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    echo        to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)

rem 先刷一遍运行时库：改过 runtime\ 之后跳过这一步，链接时会报一堆
rem 莫名其妙的 "undefined reference to cg_xxx"。
rem （build-all.bat 已经刷过一次，会设 CSTACKGUI_SKIP_RUNTIME=1 跳过。）
if not "%CSTACKGUI_SKIP_RUNTIME%"=="1" (
    echo [info] refreshing runtime library
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

rem 往工程目录里留一份运行时库的拷贝：这样整个目录被挪走之后还能自己重编。
if not exist "%APPDIR%\lib" mkdir "%APPDIR%\lib"
copy /y build\lib\libcstackgui.a "%APPDIR%\lib\libcstackgui.a" >nul
if errorlevel 1 (
    echo [fail] cannot copy runtime lib to %APPDIR%\lib
    exit /b 1
)
rem 资源对象（清单 + 版本信息）里没有代码符号，链接器不会主动从静态库里取它 ——
rem 必须显式链接，否则 exe 拿不到 v6 通用控件（页签/进度条会退回老外观）。
copy /y build\obj\cstackgui_res.o "%APPDIR%\lib\cstackgui_res.o" >nul
if errorlevel 1 (
    echo [fail] cannot copy resource object to %APPDIR%\lib
    exit /b 1
)

rem gdiplus 只用来加载图片（png/jpg/bmp/gif），Windows 自带
rem comdlg32 供通用对话框（打开/保存文件、选颜色）使用
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
rem 回调签名是固定的，没用到的 self/ev/user 参数不应该报警告
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

rem exe 名 = 工程目录名（%APP% 的最后一段）
for %%I in ("%APP%") do set NAME=%%~nxI
set OUT=%APPDIR%\%NAME%.exe

rem 工程自带的 <名字>.rc（画布「程序配置」对话框生成的）会顶掉运行时那份通用版本：
rem 里面是本程序的产品名 / 版本 / 作者 / 图标。它是 UTF-8（含中文字符串），
rem 所以加 --codepage=65001；-I runtime\res 让它能引用到 v6 通用控件清单。
set RESOBJ=%APPDIR%\lib\cstackgui_res.o
if exist "%APPDIR%\%NAME%.rc" (
    echo   RC %APPDIR%\%NAME%.rc
    windres --codepage=65001 -I "%APPDIR%" -I "runtime\res" -O coff "%APPDIR%\%NAME%.rc" -o "%APPDIR%\lib\%NAME%_res.o"
    if errorlevel 1 (
        echo [fail] compiling %APPDIR%\%NAME%.rc failed
        exit /b 1
    )
    set RESOBJ=%APPDIR%\lib\%NAME%_res.o
)

rem 增量编译：把 <工程>\*.c 逐个编成 lib\<名字>.o，只在该 .o 不存在、或者不比源文件
rem 新时才编。工程内的头文件和公开头 cstackgui.h 变新同样会触发重编，避免悄悄链上
rem 旧代码。删掉的源文件自然从链接列表里消失。（运行时库每次都会重新链接，所以
rem 故意不拿它判定工程要不要重编 —— 否则每次重链之后工程都会判成过期。）
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
    gcc %CFLAGS% -mwindows -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
) else (
    echo   LD %OUT% ^(debug, with console^)
    gcc %CFLAGS% -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS%
)
if errorlevel 1 (
    echo [fail] build of %APP% failed
    exit /b 1
)

rem 往工程目录里放一个启动脚本，双击就能重编本工程
call :make_launcher %APP%

echo [ok] %OUT%
echo.
echo     run it (double-click works):   %OUT%
echo     self-check without window:     %OUT% --selftest
echo     rebuild this project:          %APPDIR%\build-app.bat

rem 刷新 clangd 语言服务配置（tools\gen-cc.mjs 里的编译参数和这里保持一致）。
rem 没装 node 或者不在项目根时会静默跳过。
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0

:make_launcher
rem 参数 %1 = 工程名（生成时所在目录的名字）
rem 生成的启动脚本里踩过两个坑，每个都够调半天：
rem  - echo 的文本里不能出现半角括号：if 块里某行 echo 带上右括号，
rem    会让整个 if 块提前收尾，后面的 exit /b 1 就变成无条件执行 ——
rem    双击启动脚本时看起来就像什么都没发生。
rem  - 生成物里的中文必须是 GBK（cp936）：cmd 解析 bat 用的代码页是"启动这个 cmd 进程时"
rem    的控制台代码页，UTF-8 的中文会被按 2 字节/字解码 → 行定位错位 → 注释的后半句被当
rem    命令执行（双击时满屏 "... is not recognized ..."）。所以下面写文件前先切到 936，
rem    写完再切回 65001（本脚本自己的输出还是 UTF-8）。tools\bat-gbk.ps1 负责检查。
rem 生成物必须是 GBK：写文件期间把控制台代码页切到 936。
chcp 936 >nul
set L=%APPDIR%\build-app.bat
> "%L%" echo @echo off
>>"%L%" echo rem 本工程的编译入口（由项目根目录的 build-app.bat 生成）。
>>"%L%" echo rem 真正的编译逻辑在项目根的 build-app.bat 里，两处不会各自跑偏。
>>"%L%" echo setlocal
>>"%L%" echo if "%%CSTACKGUI_HOME%%"=="" set CSTACKGUI_HOME=%%~dp0..\..
>>"%L%" echo if not exist "%%CSTACKGUI_HOME%%\build-app.bat" ^(
>>"%L%" echo     echo [fail] project root not found: %%CSTACKGUI_HOME%%
>>"%L%" echo     echo        set CSTACKGUI_HOME to the CStackGUI root dir, the one holding runtime\ and apps\
>>"%L%" echo     exit /b 1
>>"%L%" echo ^)
>>"%L%" echo rem 工程名就是本目录的名字，所以随便改目录名都没关系。
>>"%L%" echo for %%%%I in ("%%~dp0.") do set CG_APP=%%%%~nI
>>"%L%" echo call "%%CSTACKGUI_HOME%%\build-app.bat" %%CG_APP%% %%*
>>"%L%" echo rem 失败时别让窗口一闪而过：双击运行时它会直接关掉，
>>"%L%" echo rem 错误信息还没看清就没了。
>>"%L%" echo set CG_RC=%%errorlevel%%
>>"%L%" echo if not "%%CG_RC%%"=="0" pause
>>"%L%" echo exit /b %%CG_RC%%
rem 生成物写完，代码页切回来（本脚本后面的输出仍按 UTF-8 走）。
chcp 65001 >nul
exit /b 0
