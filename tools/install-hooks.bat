@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  启用仓库自带的 git 钩子（pre-push 快判据）
rem
rem  为什么需要单独一步：git 不会自动使用 .githooks\，必须把
rem  core.hooksPath 指过去。这条命令改的是**本地仓库配置**，
rem  所以不放在构建脚本里自动跑 —— 由你决定什么时候启用。
rem
rem  停用：git config --unset core.hooksPath
rem  临时绕过一次：git push --no-verify
rem ============================================================
setlocal
cd /d "%~dp0.."

if not exist ".githooks\pre-push" (
    echo [fail] .githooks\pre-push missing
    exit /b 1
)

git config core.hooksPath .githooks
if errorlevel 1 (
    echo [fail] git config failed
    exit /b 1
)

echo [ok] core.hooksPath = .githooks
echo      pre-push will run: skills / routing / guard rules / roster / designer typecheck
echo      full gate (slow):  set CG_FULL_GATE=1 before push
exit /b 0
