@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  一键编译 CStackGUI 画布，并把 CStackGUI.exe 同步到 release\
rem
rem  用法（在项目根双击或执行）：
rem    build-cstackgui.bat
rem
rem  做两件事：
rem    1) 编译并打包画布（designer -> designer\release\CStackGUI.exe）
rem    2) 把刚编出来的 CStackGUI.exe 拷到项目根的 release\CStackGUI.exe
rem
rem  只更新画布这一个 exe；运行时库和示例由 build-all.bat / make-release.bat 负责。
rem ============================================================
setlocal
cd /d "%~dp0"

echo == 1/2 编译并打包画布（vite build + electron-builder）==
pushd designer
rem 国内网络直连 GitHub 下载二进制（electron zip / winCodeSign 等）经常超时，
rem 走 npmmirror 镜像；缓存已齐全时这些变量不起作用。
set ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/
set ELECTRON_BUILDER_BINARIES_MIRROR=https://npmmirror.com/mirrors/electron-builder-binaries/
call npm run dist
if errorlevel 1 (
    popd
    echo [fail] 画布编译/打包失败
    exit /b 1
)
popd
if not exist "designer\release\CStackGUI.exe" (
    echo [fail] 没找到 designer\release\CStackGUI.exe
    exit /b 1
)

echo.
echo == 2/2 同步 CStackGUI.exe 到 release\ ==
if not exist release mkdir release
copy /y designer\release\CStackGUI.exe release\CStackGUI.exe >nul
if errorlevel 1 (
    echo [fail] 复制 CStackGUI.exe 到 release\ 失败
    exit /b 1
)

echo.
echo [ok] CStackGUI.exe 已更新到 release\
exit /b 0
