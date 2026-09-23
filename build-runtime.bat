@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  编译 CStackGUI 运行时库 -> build\lib\libcstackgui.a
rem
rem  只有改过 runtime\ 或者第一次用时才需要单独跑；平时的工程编译
rem  都走 build-app.bat（它自己会先刷一遍）。
rem
rem  可选环境变量：
rem    MINGW   MinGW-w64 的 bin 目录（默认 C:\mingw64\bin）
rem ============================================================
setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    echo        to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)

if not exist build\obj mkdir build\obj
if not exist build\lib mkdir build\lib

rem 源码在 runtime\src 下按层分组：
rem   core\   与后端无关：JSON、文档 + 类型表、控件模型、事件、生命周期
rem   win32\  Win32 后端：窗口、原生控件、消息映射、对话框、托盘
rem   skin\   GDI+ 外观层（整个目录换掉就能换成 D2D 内核）
rem   kit\    非界面的平台服务：文件、路径、INI、文本/编码、注册表
rem 每一层都在 include 路径上，层内文件可以直接用裸文件名互相包含；
rem runtime\src 本身故意不在路径里 —— 跨层包含必须写清楚是哪一层。
set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src\core -Iruntime\src\win32 -Iruntime\src\skin -Iruntime\src\kit
set CHANGED=
set OBJS=

rem 把 core\ win32\ skin\ kit\ 下的 .c 全部编一遍 —— 不用手工维护文件清单，也就不会漏。
rem 目标文件名 = 源文件基础名（build\obj\<名字>.o），所以两个目录绝不能有同名文件，
rem 命名约定保证了这一点：
rem core = cg_*、后端 = cg_win_*、外观 = cg_skin_*、平台服务 = cg_kit_*。
rem 增量编译：只重编 .o 不存在、或者不比源文件新的那些。
rem 时间戳是 "YYYY/MM/DD HH:MM" 格式，按字符串比较正好等于按时间比较；
rem 「不比源文件新」也算过期，这样同一分钟内改的代码不会被漏掉。
rem 改头文件必须让所有目标文件失效：这些头文件声明了整个库赖以构建的结构体
rem 布局和宏，留着旧 .o 会悄悄链到旧定义上。所以取 runtime\include 加四层里
rem 最新的头文件时间，比它旧的目标文件全部重编。
set "HDRT=0"
for %%h in (runtime\include\*.h) do if "%%~th" GTR "!HDRT!" set "HDRT=%%~th"
for %%d in (core win32 skin kit) do (
    for %%h in ("runtime\src\%%d\*.h") do if "%%~th" GTR "!HDRT!" set "HDRT=%%~th"
)
for %%d in (core win32 skin kit) do (
    for %%f in ("runtime\src\%%d\*.c") do (
        set "SRCF=%%~ff"
        set "OBJ=build\obj\%%~nf.o"
        set "OBJS=!OBJS! build\obj\%%~nf.o"
        set "DO=1"
        if exist "!OBJ!" (
            for %%o in ("!OBJ!") do set "OT=%%~to"
            for %%c in ("!SRCF!") do set "CT=%%~tc"
            if "!OT!" GTR "!CT!" if "!OT!" GTR "!HDRT!" set "DO="
        )
        if defined DO (
            echo   CC runtime\src\%%d\%%~nxf
            gcc %CFLAGS% -c "!SRCF!" -o "!OBJ!"
            if errorlevel 1 ( echo [fail] runtime build failed & exit /b 1 )
            set "CHANGED=1"
        )
    )
)


rem 资源对象（清单 + 版本信息 + 图标）：它的 .rc / .ico 有变动就重编
set "RCOBJ=build\obj\cstackgui_res.o"
set "DO=1"
if exist "!RCOBJ!" (
    for %%o in ("!RCOBJ!") do set "OT=%%~to"
    for %%c in ("runtime\res\cstackgui.rc") do set "CT=%%~tc"
    for %%c in ("runtime\res\cstackgui.ico") do set "IT=%%~tc"
    if "!OT!" GTR "!CT!" if "!OT!" GTR "!IT!" set "DO="
)
if defined DO (
    echo   RC runtime\res\cstackgui.rc
    windres --include-dir=runtime\res -O coff runtime\res\cstackgui.rc -o "!RCOBJ!"
    if errorlevel 1 ( echo [fail] runtime build failed & exit /b 1 )
    set "CHANGED=1"
)

rem 只有真的有东西变了（或者库还不存在）才重新打包
if not exist build\lib\libcstackgui.a set "CHANGED=1"
if defined CHANGED (
    echo   AR build\lib\libcstackgui.a
    rem 先删旧库再打包：`ar rcs` 只增替、**不会删除**已消失的成员 —— 删掉/改名某个 .c 之后
    rem 重编，旧 .o 仍留在库里，可能被链进最终 exe（静默出错）。
    if exist build\lib\libcstackgui.a del /q build\lib\libcstackgui.a
    ar rcs build\lib\libcstackgui.a !OBJS! build\obj\cstackgui_res.o
    if errorlevel 1 ( echo [fail] runtime build failed & exit /b 1 )
    echo [ok] build\lib\libcstackgui.a
) else (
    echo   (runtime up to date, skipped compile^)
)

rem 刷新 clangd 语言服务配置（用的参数和上面一致）。
rem 没装 node 或者不在项目根时会静默跳过。
where node >nul 2>nul && if exist tools\gen-cc.mjs node tools\gen-cc.mjs >nul 2>nul
exit /b 0
