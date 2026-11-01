@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  格式契约回归：画布写出来的 .cgui，运行时必须能读回来且自检通过。
rem
rem  最容易出问题的接缝：画布用 TS 写 .cgui，运行时用 C 读它。
rem  改过 .cgui 字段或控件类型之后，就跑一遍这个。
rem ============================================================
setlocal
if "%MINGW%"=="" set MINGW=C:\mingw64\bin
set PATH=%MINGW%;%PATH%
cd /d "%~dp0.."

if not exist build\lib\libcstackgui.a (
    call build-runtime.bat
    if errorlevel 1 exit /b 1
)
rem 编译产物就在各工程目录里（examples\<名字>\<名字>.exe）
if not exist examples\gallery\gallery.exe (
    call build-app.bat gallery
    if errorlevel 1 exit /b 1
)

echo   TSC designer\src\model.ts -^> build\modeljs
call npx --prefix designer tsc designer\src\model.ts --outDir build\modeljs --target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict
if errorlevel 1 (
    echo [fail] compiling the designer model failed
    exit /b 1
)

rem 全部示例的格式契约回归（2026-10-18 从 5 个样本扩到全部）：examples/ 是"用户最可能
rem 照抄的东西"，而此前只有 gallery / serialterm / lesson01 / lesson06 / netterm 进门禁 ——
rem 另外二十几个工程的 .cgui 字段理解漂移，没有任何门禁知道。原 5 个样本的入选理由仍然
rem 有效（gallery=合并后的陈列室 / serialterm=串口字段 / lesson01=输入框全功能字段 /
rem lesson06=app.* 窗口级字段 / netterm=网口字段），只是不再值得逐个手写。
rem 现在一个脚本全覆盖：每个样本走完整的 画布读 -> 模拟用户改动 -> 写出 -> C 侧 --selftest
rem -> 恒等回写核对；4 路并发（各写各的 build\roundtrip_<名>.cgui；--selftest 只写 stdout
rem 与按 exe 名分的日志文件，互不冲突）。样本收集在 node 里做 —— 不写成 for 块：带中文的
rem 括号块在代码页解析下会漂移（见 README 的 bat 约定）。
rem 缺 exe 的样本明确失败（先跑 build-all.bat），不静默跳过：没有 C 侧那一半验证的绿灯
rem 比没有门禁更糟。首跑就抓到两个探针假故障（无宿主 action / 给动作按钮盲加 click），
rem 均已修并在 format-roundtrip.mjs 里注明缘由。
echo   roundtrip: all examples (parse/mutate/serialize/C selftest)
node tools\check-roundtrip-all.mjs
if errorlevel 1 exit /b 1

rem 公共 API 集合快照：cstackgui.h 有 328 个函数 / 19 个 typedef / 33 个 CG_ 宏，
rem 但此前没有任何判据知道"集合本来长什么样" —— 删掉一个没人调用的公共 API，全仓照样绿。
rem 快照把"集合"本身变成判据：少一个红（破坏性），多一个也红（要求显式更新基线）。
rem 顺带修掉一个长期的口径错误：旧统计正则漏掉所有返回指针的函数，报 271，真实 328。
echo   public API set vs baseline (functions / typedefs / macros)
node tools\check-header.mjs
if errorlevel 1 exit /b 1

rem 画布「支持库」页签是手写的 C API 索引，没人校验就会和 cstackgui.h 跑偏。
echo   support library vs cstackgui.h
node tools\check-support.mjs
if errorlevel 1 exit /b 1

rem bat 编码契约：带中文的 bat 必须是 UTF-8 无 BOM + CRLF + ASCII 前导，否则双击时 cmd 会把注释当命令跑。
echo   bat encoding (UTF-8 no BOM + CRLF + ASCII prelude)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode check
if errorlevel 1 exit /b 1

rem 文档口径契约：文档里手写的"课数 / 示例范围 / 文章篇数 / 控件类型总数"必须与 examples\、
rem docs\articles\ 与 k_kinds 的现状一致，并且教学说明里那张"六课事实表"必须是生成结果
rem （2026-09-23 体检发现这类手写事实一次漏改 31 处）。
echo   docs vs examples (lesson count / range / article count / kind count)
node tools\check-examples.mjs --docs
if errorlevel 1 exit /b 1

rem 控件落点契约：42 种类型每种都得在 examples\ 里至少有一个 .cgui 用到
rem （否则读者找不到用法 —— box/meter 就这么被漏过）。这条以前只在手工命令里，2026-09-23 接进来。
echo   every kind has an example
node tools\check-examples.mjs
if errorlevel 1 exit /b 1

rem 程序配置链路：app.info -> <工程>.rc -> windres -> exe 版本信息/图标。
rem 这条以前**没有任何验证**（仓库里一个示例都不带 .rc，那套 windres 参数从没被真跑过）。
rem 现在拿 gallery 当标本（它的 .rc 是手写样板，形状同画布生成器）。
rem 期望值用字符码拼：既避开 .bat 里写中文的编码坑，又能真的抓到"UTF-8 被当 GBK 解"这类退化
rem —— 真出现那种退化，ProductName 的字符数会变、字符串也对不上。
rem 0x63A7 0x4EF6 0x9648 0x5217 0x5BA4 = 控件陈列室
rem 注意：echo 里别写 "->" —— cmd 会把那个 ">" 当成重定向，凭空建出一个叫 exe 的文件
rem （2026-09-23 加这一步时踩过，仓库根目录多出一个 59 字节的 "exe"）。
rem 运行时契约表：把"**声明了却不生效**"那一类修复锁住（事件/字段 是否真的落到原生侧）。
rem 13 条规则，每条的 why 都写着"这条路径当年为什么存在"；谁把某条删了/绕过了，这里立刻红。
rem （写这行时注意：echo 里别出现 ">"，会被 cmd 当重定向 —— 见下面 app config 那段的注释。）
echo   runtime dispatch contract: events and fields really reach the native side
node tools\check-dispatch.mjs
if errorlevel 1 exit /b 1

rem 分层契约：core 平台中立 / win32 只走 cg_win_internal.h / skin 不反向依赖 /
rem kit 不依赖界面 / 四层 .c 基名唯一（撞名会让 build-runtime 的 .o 互相覆盖）。
rem 结构审查实测这五条当前**全部成立** —— 越干净越值得锁，因为一次图省事的 include
rem 就能悄悄毁掉"core+kit 与 Win32 视图解耦"，而编译器不会报警（2026-10-17 表述收敛：
rem 目前只有 win32 一个后端，可移植是设计属性、不是已验证事实）。
echo   layer contract (core / win32 / skin / kit)
node tools\check-layers.mjs
if errorlevel 1 exit /b 1

rem 控件类型表双向同步：运行时 k_kinds 与画布 KINDS 是同一事实的两份手写，
rem 此前只有注释约定（2026-10-17 架构评审把它列为"唯一无锁的跨端契约"）。
rem 类型名或容器位任何一边改了忘了另一边，这里立刻红。
echo   kind table sync (k_kinds vs designer KINDS)
node tools\check-kinds.mjs
if errorlevel 1 exit /b 1

rem 画布主进程（designer\electron\*.mjs）此前没有任何语法门禁：tsc 只看 .ts、
rem vite build 只打渲染进程。2026-10-07 实测 —— 一句块注释里写错一个字符对，
rem 注释被提前闭合，画布重新打包后开机即崩，而当时所有门禁都是绿的。
echo   electron main process syntax
node tools\check-electron.mjs
if errorlevel 1 exit /b 1

rem 画布全量类型检查：本脚本前面只 tsc 了 model.ts，而 vite build（发布链）只转译
rem 不查类型 —— App.tsx / components/ 的类型错误能一路发版（2026-10-17 二轮评审
rem "designer 是最大盲区"整改；与 check-electron 同构：tsc 看不见的口径必须单独锁）。
rem designer 源码不在（发布包环境）时跳过，与 check-support 同口径。
echo   designer type check (tsc --noEmit, whole src)
if exist designer\package.json (
  call npm run typecheck --prefix designer
  if errorlevel 1 exit /b 1
)

rem 画布文档层契约（store/ops，headless）：命令可序列化重放 / 不被消费污染 /
rem id 跨调用唯一 / 撤销栈语义。画布此前 0 自动验证，这是文档操作层的第一块回归判据
rem （2026-10-18 路线图 G1/G2；首跑就抓到 applyCmd 引用污染命令的真实缺陷）。
echo   canvas store/ops contract (headless, journal discipline)
node tools\check-canvas-ops.mjs
if errorlevel 1 exit /b 1

rem 画布行为冒烟（路线图 G2）：起真 Electron 窗口，在渲染层里驱动真实事件
rem （点调色板 / 敲键盘 / 切页签 / 拖预演滑杆），把 UI 接线层的行为钉住。
rem store/命令/键盘/页签/预演五批改动此前只有 tsc 与 vite build 把关 ——
rem 类型全对、行为全错照样发版（2026-10-18 二轮会诊点名的最大盲区）。
rem 本步先 vite build 再起窗，保证测的是当前源码；designer 源码或 electron
rem 不在时脚本自己跳过（发布包环境无画布源码，与 check-support 同口径）。
echo   canvas e2e smoke (real window, real events)
node tools\check-canvas-e2e.mjs
if errorlevel 1 exit /b 1

rem 套件工程的静态契约（examples 合并方案 P0 起）：套件是 N 个原工程合并成的多页签工程，
rem 合并期最容易在这里漏 —— .cgui 的回调名在模块里没登记（运行时才报错）、反向死绑定、
rem 套件内 id 撞名、id 前缀与模块不符。自检能兜住一部分，但要跑起来才知道；
rem 这里静态就拦住，且 suites\ 不存在时脚本自己跳过（合并未开始的仓库）。
echo   suite contract (static: events/bindings/id-prefix)
node tools\check-suite.mjs
if errorlevel 1 exit /b 1

rem 骨架语言判据（designer\electron\skeleton-lang.mjs）：首次导出问 C/C++ 这条规则的
rem "不破坏既有骨架"承诺由它兜 —— 判据写坏、另一套源文件的写保护被绕过，这里立刻红。
echo   skeleton language guard (C or C++, never both)
node tools\check-skeleton.mjs
if errorlevel 1 exit /b 1

echo   app config: exe version info (examples\gallery\gallery.rc)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$exe='examples\gallery\gallery.exe'; if (-not (Test-Path $exe)) { Write-Host '[fail] gallery.exe missing - run build-all first'; exit 1 }; $v=(Get-Item $exe).VersionInfo; $want='CStackGUI ' + [char]0x63A7 + [char]0x4EF6 + [char]0x9648 + [char]0x5217 + [char]0x5BA4; if ($v.FileVersion -ne '0.1.0.0' -or $v.OriginalFilename -ne 'gallery.exe' -or $v.ProductName -ne $want) { Write-Host ('[fail] gallery.exe version info mismatch -> FileVersion=' + $v.FileVersion + ' ProductName=' + $v.ProductName + ' OriginalFilename=' + $v.OriginalFilename); exit 1 }; Write-Host ('  gallery.exe: ' + $v.ProductName + ' ' + $v.FileVersion)"
if errorlevel 1 exit /b 1

rem 技能契约：技能目录 / INDEX.md（总数与分类）/ .codebuddy 的 README.md 目录树 / 各 SKILL.md 的
rem frontmatter / settings.json 的 scopes，五处说的必须是同一件事。
rem （2026-09-27 清理 13 个技能时，靠人工同步了 6 个文件 40 处引用；这类"同一事实两份"的事
rem  按本仓库自己的规矩必须机器兜 —— INDEX 维护清单里那条"记得同步 README 与映射表"从此由它兜住。）
echo   skills: dirs vs INDEX.md vs README tree vs frontmatter vs scopes
node tools\check-skills.mjs
if errorlevel 1 exit /b 1

rem  路由用例（学自上游 teams.toml 的"单一事实源"）：
rem  触发词必须在 description 里（否则模型永远选不中）、不许第二个技能同时命中、每个技能必须被点名。
echo   routing: trigger words vs descriptions, no ambiguity
node tools\check-routing.mjs
if errorlevel 1 exit /b 1

rem  hook 守卫的回归用例：guard-command.cjs 判错的两种方式都很贵 —— 漏判（危险命令直接执行）
rem  与误伤（正常命令被拦，使用者只能关掉整条规则）。2026-10-31 实测的误伤是 `| Format-Table`
rem  被 `/^(format|...)\b/` 判成"格式化磁盘"；这里把那类写法固定成放行用例。
echo   hook guard rules (deny cases + false-positive regressions)
node tools\hooks\test-guard.cjs
if errorlevel 1 exit /b 1

rem  收工验证的判据回归：三态结果（绿/红/判不出来）与"只报回归"。
rem  这条判据写错的后果是**什么都不发生**（hook 静默放行），只能靠用例发现。
echo   stop-hook gate judging (tri-state / regression-only)
node tools\hooks\test-verify.cjs
if errorlevel 1 exit /b 1

rem  八团编制：teams.toml ↔ 磁盘目录 ↔ INDEX 的 ROSTER。没装 Python 时跳过（不阻断构建）。
where python >nul 2>nul
if not errorlevel 1 (
  echo   roster: teams.toml vs skill dirs vs INDEX ROSTER
  python tools\check_roster.py
  if errorlevel 1 exit /b 1
) else (
  echo   [skip] python not found - roster check skipped
)

echo.
echo [ok] format contract check passed
exit /b 0
