@echo off
rem 本工程的编译入口（由项目根目录的 build-app.bat 生成）。
rem 真正的编译逻辑在项目根的 build-app.bat 里，两处不会各自跑偏。
setlocal
if "%CSTACKGUI_HOME%"=="" set CSTACKGUI_HOME=%~dp0..\..
if not exist "%CSTACKGUI_HOME%\build-app.bat" (
    echo [fail] project root not found: %CSTACKGUI_HOME%
    echo        set CSTACKGUI_HOME to the CStackGUI root dir, the one holding runtime\ and apps\
    exit /b 1
)
rem 工程名就是本目录的名字，所以随便改目录名都没关系。
for %%I in ("%~dp0.") do set CG_APP=%%~nI
call "%CSTACKGUI_HOME%\build-app.bat" %CG_APP% %*
rem 失败时别让窗口一闪而过：双击运行时它会直接关掉，
rem 错误信息还没看清就没了。
set CG_RC=%errorlevel%
if not "%CG_RC%"=="0" pause
exit /b %CG_RC%
