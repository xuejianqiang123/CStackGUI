@echo off
rem ============================================================
rem  编译 CStackGUI 运行时库 -> build\lib\libcstackgui.a
rem
rem  只在「改了 runtime\ 下的东西」或第一次用时才需要跑；
rem  平时改业务代码只跑 build-app.bat 就行。
rem
rem  可选环境变量：
rem    MINGW   MinGW-w64 的 bin 目录（默认 C:\mingw64\bin）
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [fail] 找不到 gcc。装个 MinGW-w64，或设置 MINGW 环境变量指向它的 bin 目录：
    echo        set MINGW=D:\your\mingw64\bin
    exit /b 1
)

if not exist build\obj mkdir build\obj
if not exist build\lib mkdir build\lib

set CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime\include -Iruntime\src
set SRC=cg_json cg_doc cg_app cg_timer cg_win32

for %%f in (%SRC%) do (
    echo   CC runtime\src\%%f.c
    gcc %CFLAGS% -c runtime\src\%%f.c -o build\obj\%%f.o
    if errorlevel 1 goto :fail
)

echo   RC runtime\res\cstackgui.rc
windres --include-dir=runtime\res -O coff runtime\res\cstackgui.rc -o build\obj\cstackgui_res.o
if errorlevel 1 goto :fail

ar rcs build\lib\libcstackgui.a build\obj\cg_json.o build\obj\cg_doc.o build\obj\cg_app.o build\obj\cg_timer.o build\obj\cg_win32.o build\obj\cstackgui_res.o
if errorlevel 1 goto :fail

echo [ok] build\lib\libcstackgui.a
exit /b 0

:fail
echo [fail] 运行时编译失败
exit /b 1
