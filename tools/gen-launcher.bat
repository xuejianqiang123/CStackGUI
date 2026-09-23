@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  生成"工程级启动脚本"（<工程>\build-app.bat），由 build-app.bat 调用。
rem
rem  为什么要单独一个文件：本文件里**不能用 goto / call :label**。cmd 找标签是按
rem  字节偏移扫描的，UTF-8 的中文是 3 字节/字，扫描会越扫越偏 —— 表现为脚本结尾
rem  多出一行 "*" 报错、或者莫名停在 "Press any key"（tools\bat-utf8.ps1 会拦）。
rem
rem  用法：call tools\gen-launcher.bat "<工程目录（绝对或相对路径都行）>"
rem  幂等：生成物里带版本标记，标记一致就跳过，避免重写正在运行的启动脚本。
rem ============================================================
set "APPDIR=%~1"
if "%APPDIR%"=="" (
    echo [fail] gen-launcher.bat needs the project dir as %%1
    exit /b 1
)
set L=%APPDIR%\build-app.bat
if exist "%L%" findstr /c:"CG_LAUNCHER_V 1" "%L%" >nul && exit /b 0
> "%L%" echo @echo off
>>"%L%" echo rem CG_LAUNCHER_V 1
>>"%L%" echo rem ---- ASCII prelude: keep everything above the next marker ASCII-only ----
>>"%L%" echo rem cmd decodes a .bat with the console code page captured when it started, so a
>>"%L%" echo rem UTF-8 file must be (re)entered by a process whose console is already 65001;
>>"%L%" echo rem otherwise the Chinese lines below get cut in the middle and run as commands.
>>"%L%" echo for /f "tokens=2 delims=:" %%%%c in ('chcp') do set "CG_CP=%%%%c"
>>"%L%" echo if not "%%CG_CP: =%%"=="65001" ^(
>>"%L%" echo     chcp 65001 ^>nul
>>"%L%" echo     cmd /c "%%~f0" %%*
>>"%L%" echo     exit /b %%errorlevel%%
>>"%L%" echo ^)
>>"%L%" echo rem ---- UTF-8 content starts here: Chinese is fine below ----
>>"%L%" echo echo 本工程的编译入口（由项目根目录的 build-app.bat 生成）。
>>"%L%" echo echo 真正的编译逻辑在项目根的 build-app.bat 里，两处不会各自跑偏。
>>"%L%" echo setlocal
>>"%L%" echo if "%%CSTACKGUI_HOME%%"=="" set CSTACKGUI_HOME=%%~dp0..\..
>>"%L%" echo if not exist "%%CSTACKGUI_HOME%%\build-app.bat" ^(
>>"%L%" echo     echo [fail] project root not found: %%CSTACKGUI_HOME%%
>>"%L%" echo     echo        set CSTACKGUI_HOME to the CStackGUI root dir, the one holding runtime\ and apps\
>>"%L%" echo     exit /b 1
>>"%L%" echo ^)
>>"%L%" echo echo 工程名就是本目录的名字，所以随便改目录名都没关系。
>>"%L%" echo for %%%%I in ("%%~dp0.") do set CG_APP=%%%%~nI
>>"%L%" echo call "%%CSTACKGUI_HOME%%\build-app.bat" "%%CG_APP%%" %%*
>>"%L%" echo echo 失败时别让窗口一闪而过：双击运行时它会直接关掉，
>>"%L%" echo echo 错误信息还没看清就没了。
>>"%L%" echo set CG_RC=%%errorlevel%%
>>"%L%" echo if not "%%CG_RC%%"=="0" pause
>>"%L%" echo exit /b %%CG_RC%%
exit /b 0
