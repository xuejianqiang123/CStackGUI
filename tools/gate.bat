@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  门禁单一入口：一条命令跑完该跑的，不用按改动域挑
rem
rem  为什么要有它：
rem    sop 的 §3 是「哪条命令什么时候必跑」的唯一权威说明，但它是给人看的表格 ——
rem    每次开工都要人判断"这次该跑哪几条"，判断错了就是 sop 自己写的那种
rem    「跑了个假门禁，而没有任何命令会因此变红」。
rem    本脚本只做转发，不新增判据、不改任何通过标准；判据仍在各 check-*.mjs 里。
rem
rem  为什么不做"按 git diff 自动选域"：
rem    选域规则本身会成为新的腐化点 —— 规则写错时它会静默少跑，
rem    而那恰恰是这套门禁存在的理由。全量实测约 114s（增量全绿），零判断更划算。
rem
rem  用法（项目根）：
rem    tools\gate.bat              全量：build-all -> run-tests -> check-format
rem    tools\gate.bat --fast       只改文档/配置：跳过编译，run-tests -> check-format
rem    tools\gate.bat --skills     改了 .codebuddy\skills\：skills/routing/roster/guard
rem    tools\gate.bat --canvas     改了 designer\：tsc --noEmit -> vite build
rem    tools\gate.bat --doctor     体检：钩子接线 / 工具链（只报告，不阻断）
rem ============================================================
rem 必须开延迟展开：--doctor 里在同一个 if 括号块内先 `for /f ... set HP` 再判 %HP%，
rem 而整块在解析阶段就把 %HP% 展开成空 —— 表现为"明明接好了却说未接线"（2026-11-01 实测踩到）。
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "MODE=%~1"
if "%MODE%"=="" set "MODE=--full"

if /i "%MODE%"=="--full" (
    echo [gate] 1/3 编译 + 逐示例自检  ^(build-all.bat^)
    call build-all.bat
    if errorlevel 1 exit /b 1
    echo [gate] 2/3 C 层单测          ^(run-tests.bat^)
    call tools\run-tests.bat
    if errorlevel 1 exit /b 1
    echo [gate] 3/3 跨端契约          ^(check-format.bat^)
    call tools\check-format.bat
    if errorlevel 1 exit /b 1
    echo.
    echo [ok] gate passed ^(full^)
    exit /b 0
)

if /i "%MODE%"=="--fast" (
    echo [gate] 1/2 C 层单测          ^(run-tests.bat^)
    call tools\run-tests.bat
    if errorlevel 1 exit /b 1
    echo [gate] 2/2 跨端契约          ^(check-format.bat^)
    call tools\check-format.bat
    if errorlevel 1 exit /b 1
    echo.
    echo [ok] gate passed ^(fast^)
    exit /b 0
)

if /i "%MODE%"=="--skills" (
    echo [gate] 1/5 技能契约
    node tools\check-skills.mjs
    if errorlevel 1 exit /b 1
    echo [gate] 2/5 路由用例
    node tools\check-routing.mjs
    if errorlevel 1 exit /b 1
    echo [gate] 3/5 hook 守卫规则
    node tools\hooks\test-guard.cjs
    if errorlevel 1 exit /b 1
    echo [gate] 4/5 verify-on-stop 判据
    node tools\hooks\test-verify.cjs
    if errorlevel 1 exit /b 1
    where python >nul 2>nul
    if not errorlevel 1 (
        echo [gate] 5/5 八团编制  ^(python 可用^)
        python tools\check_roster.py
        if errorlevel 1 exit /b 1
    ) else (
        echo [gate] [skip] 没装 python，跳过编制校验
    )
    echo.
    echo [ok] gate passed ^(skills^)
    exit /b 0
)

if /i "%MODE%"=="--canvas" (
    if not exist designer\package.json (
        echo [fail] designer\package.json 不存在，画布源码不在这个工作区
        exit /b 1
    )
    echo [gate] 1/2 全量类型检查  ^(tsc --noEmit^)
    call npm run typecheck --prefix designer
    if errorlevel 1 exit /b 1
    echo [gate] 2/2 打包  ^(vite build^)
    call npm run build --prefix designer
    if errorlevel 1 exit /b 1
    echo.
    echo [ok] gate passed ^(canvas^)
    exit /b 0
)

if /i "%MODE%"=="--doctor" (
    echo [doctor] 只做体检，不跑门禁，不阻断
    echo.
    set "HP="
    for /f "delims=" %%h in ('git config --get core.hooksPath 2^>nul') do set "HP=%%h"
    if "!HP!"==".githooks" (
        echo   [ok]   pre-push 已接线  ^(core.hooksPath = .githooks^)
    ) else (
        echo   [warn] pre-push 未接线  —— 跑一次 tools\install-hooks.bat
    )
    where node >nul 2>nul
    if errorlevel 1 (echo   [warn] node 不在 PATH) else (echo   [ok]   node 可用)
    where python >nul 2>nul
    if errorlevel 1 (echo   [warn] python 不在 PATH，编制校验会被跳过) else (echo   [ok]   python 可用)
    if exist designer\package.json (echo   [ok]   designer\package.json 在) else (echo   [warn] designer 源码不在，画布门禁跑不了)
    echo.
    echo [ok] doctor done
    exit /b 0
)

echo [fail] 不认识的参数：%MODE%
echo        用法：tools\gate.bat [--full^|--fast^|--skills^|--canvas^|--doctor]
exit /b 2
