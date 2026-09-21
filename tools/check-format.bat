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

echo.
echo [ok] format contract check passed
exit /b 0
