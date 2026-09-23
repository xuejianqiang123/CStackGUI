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

rem gallery 是合并后的陈列室：第 0 页基础控件，第 1 页树/表格/页签/图片……，
rem 第 2 页定时器/画布/对话框/新控件
echo   roundtrip gallery
node tools\format-roundtrip.mjs examples\gallery\gallery.exe examples\gallery\gallery.cgui build\roundtrip_gallery.cgui
if errorlevel 1 exit /b 1

rem 示例工程也要进这条回归：示例的 .cgui 是用户最可能照抄的东西，新控件/新 API 往往先在示例里用上，
rem 只守 gallery 会让示例里的字段悄悄丢（2026-09-21 就是靠这条发现"回归只覆盖 gallery"这个缺口）。
rem 注意这里**故意不写成 for (...) do (...) 块**：带中文的括号块在代码页解析下会漂移（见 README 的 bat 约定）。
echo   roundtrip serialterm
node tools\format-roundtrip.mjs examples\serialterm\serialterm.exe examples\serialterm\serialterm.cgui build\roundtrip_serialterm.cgui
if errorlevel 1 exit /b 1

rem lesson01 是输入框全功能例程：multiline/scroll/maxlen/conv/input/border/spin 这些字段
rem 用户最先从这个文件抄，所以要进这条回归，别让新字段在示例里悄悄丢。
echo   roundtrip lesson01
node tools\format-roundtrip.mjs examples\lesson01_basics\lesson01_basics.exe examples\lesson01_basics\lesson01_basics.cgui build\roundtrip_lesson01.cgui
if errorlevel 1 exit /b 1

rem 第六课的 .cgui 带一整套 app.* 窗口配置（center/border/topmost/escClose/tray/events/info），
rem 拿它当"窗口级字段"的回归样本：字段语义一变，这条往返就会红。
echo   roundtrip lesson06
node tools\format-roundtrip.mjs examples\lesson06_window\lesson06_window.exe examples\lesson06_window\lesson06_window.cgui build\roundtrip_lesson06.cgui
if errorlevel 1 exit /b 1

echo   roundtrip netterm
node tools\format-roundtrip.mjs examples\netterm\netterm.exe examples\netterm\netterm.cgui build\roundtrip_netterm.cgui
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
echo   app config -> exe version info (examples\gallery\gallery.rc)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$exe='examples\gallery\gallery.exe'; if (-not (Test-Path $exe)) { Write-Host '[fail] gallery.exe missing - run build-all first'; exit 1 }; $v=(Get-Item $exe).VersionInfo; $want='CStackGUI ' + [char]0x63A7 + [char]0x4EF6 + [char]0x9648 + [char]0x5217 + [char]0x5BA4; if ($v.FileVersion -ne '0.1.0.0' -or $v.OriginalFilename -ne 'gallery.exe' -or $v.ProductName -ne $want) { Write-Host ('[fail] gallery.exe version info mismatch -> FileVersion=' + $v.FileVersion + ' ProductName=' + $v.ProductName + ' OriginalFilename=' + $v.OriginalFilename); exit 1 }; Write-Host ('  gallery.exe: ' + $v.ProductName + ' ' + $v.FileVersion)"
if errorlevel 1 exit /b 1

echo.
echo [ok] format contract check passed
exit /b 0
