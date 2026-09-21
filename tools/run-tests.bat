@echo off
rem ============================================================
rem  运行时的 C 层单元测试。
rem
rem  把 tests\ 下的 .c 全部编一遍（框架 + 各套件，不用手工维护清单），
rem  链接 build\lib\libcstackgui.a 后运行。各套件启动时自己注册
rem  （见 tests\cg_test.h），所以加一个测试文件就够了 —— 这里不用改。
rem
rem  用法（在项目根执行）：
rem    tools\run-tests.bat                  单元测试 + cg_kit 冒烟测试
rem    tools\run-tests.bat --list           只列套件，不跑
rem    tools\run-tests.bat --grep=json      只跑名字里含 "json" 的套件
rem    tools\run-tests.bat --memcheck       给 malloc/free 挂钩，报告泄漏的块
rem    tools\run-tests.bat --no-kit         跳过 tools\check-kit.bat
rem    tools\run-tests.bat --full           再跑一遍 tools\check-format.bat
rem
rem  退出码：0 = 全部通过。
rem
rem  注意：本文件是 GBK（cp936）+ CRLF 行尾。
rem  所以 cmd 按"启动这个 cmd 进程时的控制台代码页"解析 bat，UTF-8 的中文会被按 2 字节/字解码，
rem  行定位错位、注释后半句被当命令执行；改完用 tools\bat-gbk.ps1 -Mode fix 归位（check-format 会查）。
rem ============================================================
chcp 65001 >nul
setlocal enabledelayedexpansion
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    echo        to its bin dir, e.g.:  set MINGW=D:\mingw64\bin
    exit /b 1
)
rem 每次都刷一遍库 —— build-runtime.bat 只会重编过期的文件。刚改完 runtime\
rem 却拿旧 .a 来测，会得出「旧代码通过」的假结论，比没有测试还糟。
call build-runtime.bat
if errorlevel 1 exit /b 1

rem ---- 把本脚本自己的开关和测试程序认识的开关分开 ----
set "MEMCHECK="
set "RUNKIT=1"
set "RUNFORMAT="
set "EXEOPT="
for %%a in (%*) do (
    if /i "%%~a"=="--memcheck" (set "MEMCHECK=1"
    ) else if /i "%%~a"=="--no-kit" (set "RUNKIT="
    ) else if /i "%%~a"=="--full" (set "RUNFORMAT=1"
    ) else set "EXEOPT=!EXEOPT! %%a"
)

rem 包含路径：公开头文件、与后端无关那层（白盒测试直接读 cg_json.h / cg_doc.h），
rem 以及 tests\ 自己（为了 cg_test.h）。
set "CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src\core -Itests"
set "DEFS="
set "LINKFLAGS="
if defined MEMCHECK (
    rem -g + -O1：让插桩在调试时既不失真又好读。
    rem --memcheck 依赖 tests\cg_test_memcheck.c 提供的 ld --wrap 挂钩。
    set "CFLAGS=-std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src\core -Itests"
    set "DEFS=-DCG_TEST_MEMCHECK"
    set "LINKFLAGS=-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=_strdup"
)

set "SRC="
for /r tests %%f in (*.c) do set SRC=!SRC! "%%f"
if not exist build\tests mkdir build\tests

echo   CC tests\ -> build\tests\cgu-tests.exe
gcc %CFLAGS% %DEFS% !SRC! -o build\tests\cgu-tests.exe build\lib\libcstackgui.a %LINKFLAGS% -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32
if errorlevel 1 (
    echo [fail] compiling the test program failed
    exit /b 1
)

rem 在 build\tests 里运行，临时文件（dump 输出、临时配置）就不会落到仓库里。
pushd build\tests
cgu-tests.exe %EXEOPT%
set "RC=%errorlevel%"
popd
if not "%RC%"=="0" (
    echo.
    echo [fail] unit tests failed
    exit /b 1
)

if defined RUNKIT (
    echo.
    echo ==== cg_kit smoke test ====
    call tools\check-kit.bat
    if errorlevel 1 exit /b 1
)
if defined RUNFORMAT (
    echo.
    echo ==== format contract / support library ====
    call tools\check-format.bat
    if errorlevel 1 exit /b 1
)

echo.
echo [ok] all checks passed
exit /b 0
