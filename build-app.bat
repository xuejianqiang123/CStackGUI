@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----

echo ============================================================
echo  把界面工程编译成独立 exe（源码仓库内版本）
echo  用法（在项目根执行）：
echo    build-app.bat gallery            调试版（带控制台，printf 可见）
echo    build-app.bat myapp --release    发布版（-mwindows，无控制台）
echo    build-app.bat                    不带参数 = 取 apps\ 下第一个工程，没有则取 examples\ 下的
echo ============================================================


setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

set APP=%~1
echo 画布（F5 运行）用环境变量 CG_APP_DIR 传工程目录，而不是 argv ——
echo spawn 出来的 cmd 转义会把路径搞坏；手动执行时仍然走 %1。
if "%APP%"=="" if not "%CG_APP_DIR%"=="" set "APP=%CG_APP_DIR%"
echo 不带参数：取 apps\ 下第一个工程，没有则取 examples\ 下第一个。
echo （以前默认是 hello 那个示例，后来两个示例合并成 examples\gallery 时被删了。）
if "%APP%"=="" for /d %%a in (apps\*) do if not defined APP set "APP=%%~nxa"
if "%APP%"=="" for /d %%a in (examples\*) do if not defined APP set "APP=%%~nxa"
set MODE=%~2

echo %APP% 既可以是裸工程名（apps\^<名字^> / examples\^<名字^>），也可以是相对本根的路径 ——
echo 画布传进来的就是 "apps\gallery" 这种。
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

echo 先刷一遍运行时库：改过 runtime\ 之后跳过这一步，链接时会报一堆
echo 莫名其妙的 "undefined reference to cg_xxx"。
echo （build-all.bat 已经刷过一次，会设 CSTACKGUI_SKIP_RUNTIME=1 跳过。）
if not "%CSTACKGUI_SKIP_RUNTIME%"=="1" (
    echo [info] refreshing runtime library
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

echo 往工程目录里留一份运行时库的拷贝：这样整个目录被挪走之后还能自己重编。
if not exist "%APPDIR%\lib" mkdir "%APPDIR%\lib"
rem **只在内容真的不同时才拷贝**。无脑 copy /y 会把目标文件的时间刷成"现在"，
rem 于是它永远比 exe 新，下面那句"没变就跳过链接"就永远不生效 —— 更糟的是它靠
rem 分钟级时间戳比较，同分钟时会被误判成"库没变"。内容相同就保留旧时间戳。
fc /b "build\lib\libcstackgui.a" "%APPDIR%\lib\libcstackgui.a" >nul 2>nul
if errorlevel 1 (
    copy /y build\lib\libcstackgui.a "%APPDIR%\lib\libcstackgui.a" >nul
    if errorlevel 1 (
        echo [fail] cannot copy runtime lib to %APPDIR%\lib
        exit /b 1
    )
    set "CHANGED=1"
)
echo 资源对象（清单 + 版本信息）里没有代码符号，链接器不会主动从静态库里取它 ——
echo 必须显式链接，否则 exe 拿不到 v6 通用控件（页签/进度条会退回老外观）。
copy /y build\obj\cstackgui_res.o "%APPDIR%\lib\cstackgui_res.o" >nul
if errorlevel 1 (
    echo [fail] cannot copy resource object to %APPDIR%\lib
    exit /b 1
)

echo gdiplus 只用来加载图片（png/jpg/bmp/gif），Windows 自带
echo comdlg32 供通用对话框（打开/保存文件、选颜色）使用
set LDFLAGS=-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32 -luuid -lole32 -loleaut32 -lws2_32
echo 回调签名是固定的，没用到的 self/ev/user 参数不应该报警告
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include

echo exe 名 = 工程目录名（%APP% 的最后一段）
for %%I in ("%APP%") do set NAME=%%~nxI
set OUT=%APPDIR%\%NAME%.exe

echo 工程自带的 ^<名字^>.rc（画布「程序配置」对话框生成的）会顶掉运行时那份通用版本：
echo 里面是本程序的产品名 / 版本 / 作者 / 图标。它是 UTF-8（含中文字符串），
echo 所以加 --codepage=65001；-I runtime\res 让它能引用到 v6 通用控件清单。
set RESOBJ=%APPDIR%\lib\cstackgui_res.o
if exist "%APPDIR%\%NAME%.rc" (
    set RESOBJ=%APPDIR%\lib\%NAME%_res.o
    rem .rc 和它引用的运行时资源脚本都没变就不重跑 windres —— 和 build-runtime.bat 对自己
    rem 那份 .rc 的口径一致（那边有新旧判断、这边原先没有，等于每次都白跑一遍）。
    set "RC_DO=1"
    if exist "!RESOBJ!" (
        for %%o in ("!RESOBJ!") do set "OT=%%~to"
        for %%c in ("%APPDIR%\%NAME%.rc") do set "CT=%%~tc"
        for %%c in ("runtime\res\cstackgui.rc") do if "%%~tc" GTR "!CT!" set "CT=%%~tc"
        if "!OT!" GTR "!CT!" set "RC_DO="
    )
    if defined RC_DO (
        echo   RC %APPDIR%\%NAME%.rc
        windres --codepage=65001 -I "%APPDIR%" -I "runtime\res" -O coff "%APPDIR%\%NAME%.rc" -o "!RESOBJ!"
        if errorlevel 1 (
            echo [fail] compiling %APPDIR%\%NAME%.rc failed
            exit /b 1
        )
        set "CHANGED=1"
    )
)

echo 增量编译：把 ^<工程^>\*.c 逐个编成 lib\^<名字^>.o，只在该 .o 不存在、或者不比源文件
echo 新时才编。工程内的头文件和公开头 cstackgui.h 变新同样会触发重编，避免悄悄链上
echo 旧代码。删掉的源文件自然从链接列表里消失。（运行时库每次都会重新链接，所以
echo 故意不拿它判定工程要不要重编 —— 否则每次重链之后工程都会判成过期。）
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
        set "CHANGED=1"
    )
    set "OBJS=!OBJS! "!OBJ!""
)

rem 只有"真的有东西变了"才重新链接：工程 .c 重编过 / 资源重编过 / exe 还不存在 /
rem 运行时库或资源对象比 exe 新。**极性取保守**：exe 必须**严格新于**这些才算不用重链
rem —— 时间戳只到分钟，相等时宁可重链一次，也不能悄悄链上旧库。
set "LD_DO="
rem **先恢复成"总是链接"**：跳过链接曾被怀疑造成 exe 与运行时库不匹配（用户报控件行为
rem 异常），在查清之前以正确性优先 —— 链接的代价远低于一个不匹配的 exe。
rem 下面的判定保留着（含保守极性），查清后再决定是否重新启用。
set "LD_DO=1"
if defined CHANGED set "LD_DO=1"
rem 极性取保守：exe 必须**严格新于**库和资源对象才允许跳过 —— 时间戳只到分钟，
rem 相等时宁可多链一次，也不能链上旧库。
if exist "%OUT%" (
    for %%o in ("%OUT%") do set "OT=%%~to"
    for %%l in ("%APPDIR%\lib\libcstackgui.a") do if not "!OT!" GTR "%%~tl" set "LD_DO=1"
    for %%l in ("%APPDIR%\lib\cstackgui_res.o") do if not "!OT!" GTR "%%~tl" set "LD_DO=1"
)
if defined LD_DO (
    if "%MODE%"=="--release" (
        echo   LD %OUT% ^(release, no console^)
        gcc %CFLAGS% -mwindows -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS% -lstdc++
    ) else (
        echo   LD %OUT% ^(debug, with console^)
        gcc %CFLAGS% -o "%OUT%" !OBJS! "%RESOBJ%" "%APPDIR%\lib\libcstackgui.a" %LDFLAGS% -lstdc++
    )
    if errorlevel 1 (
        echo [fail] build of %APP% failed
        exit /b 1
    )
) else (
    echo   -- %OUT% 没变，跳过链接
)

echo 往工程目录里放一个启动脚本，双击就能重编本工程
call "%~dp0tools\gen-launcher.bat" "%APPDIR%"


echo [ok] %OUT%
echo.
echo     run it (double-click works):   %OUT%
echo     self-check without window:     %OUT% --selftest
echo     rebuild this project:          %APPDIR%\build-app.bat

echo 刷新 clangd 语言服务配置（tools\gen-cc.mjs 里的编译参数和这里保持一致）。
echo 没装 node 或者不在项目根时会静默跳过。
rem 只在真的重编/重链过时才刷 clangd 配置 —— 起一次 node 有成本，没变化时不必付。
if defined LD_DO (
    where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
)
exit /b 0
