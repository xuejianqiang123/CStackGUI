@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  cg_kit API 冒烟测试（文件 / 路径 / INI / 文本+编码 /
rem  剪贴板 / 外部进程 / 注册表）。
rem
rem  cg_kit 是一堆 Win32 包装代码（BOM、CRLF、INI 键该插在哪、编码往返），
rem  画布和 gallery 都碰不到它 —— 一行写错就是静默出错。
rem  改过 runtime\src\kit\ 下的任何文件后，跑一遍这个。
rem
rem  它拿刚编出来的库编译 tools\kit-test.c，然后在 build\kittest\ 里运行，
rem  产生的临时文件不会落到仓库里。
rem
rem  用法：tools\check-kit.bat
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] gcc not found. Install MinGW-w64, or set the MINGW env var
    exit /b 1
)
if not exist build\lib\libcstackgui.a (
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)

if not exist build\kittest mkdir build\kittest

echo   CC tools\kit-test.c
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include tools\kit-test.c -o build\kittest\kit-test.exe build\lib\libcstackgui.a -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32 -luuid -lole32 -loleaut32
if errorlevel 1 (
    echo [fail] compiling tools\kit-test.c failed
    exit /b 1
)

pushd build\kittest
kit-test.exe
set "KITRC=%errorlevel%"
popd
if not "%KITRC%"=="0" (
    echo [fail] cg_kit smoke test failed
    exit /b 1
)

echo.
echo [ok] cg_kit smoke test passed
exit /b 0
