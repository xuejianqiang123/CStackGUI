@echo off
rem ---- ASCII prelude: keep everything above the next marker ASCII-only ----
rem cmd decodes a .bat with the console code page captured when it started, so a
rem UTF-8 file must be (re)entered by a process whose console is already 65001;
rem otherwise the Chinese lines below get cut in the middle and run as commands.
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
cd /d "%~dp0designer"
if not exist node_modules (
  echo [CStackGUI] 首次运行，安装前端依赖（npm install）...
  call npm install
)
echo [CStackGUI] 启动画布开发模式（vite 5273 + electron，支持热更新）...
call npm run dev
