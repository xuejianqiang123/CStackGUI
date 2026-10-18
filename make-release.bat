@echo off
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem ============================================================
rem  打一个可以直接发给用户的发布包 -> release\
rem
rem  用法（在项目根执行）：
rem    make-release.bat
rem
rem  做四件事：
rem    1) 全量构建运行时库和示例（保证示例的 exe 是最新的）
rem    2) 打包画布（designer -> designer\release\CStackGUI.exe）
rem    3) 组装 release\：画布 + 示例 + 预编译的运行时库 + 编译脚本 + 使用说明
rem    4) 自检：从 release\ 里真的编译并运行一次示例，确认发布包能用
rem
rem  release\ 是纯产物，可以随时删掉重打。
rem ============================================================
setlocal
cd /d "%~dp0"

echo == 1/4 构建运行时与示例 ==
call build-all.bat --release
if errorlevel 1 exit /b 1

echo.
echo == 2/4 打包画布（electron-builder）==
pushd designer
rem 国内网络直连 GitHub 下载二进制（electron zip / winCodeSign 等）经常超时，
rem 走 npmmirror 镜像；缓存已齐全时这些变量不起作用。
set ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/
set ELECTRON_BUILDER_BINARIES_MIRROR=https://npmmirror.com/mirrors/electron-builder-binaries/
rem 前端必须先构建：electron-builder **不会**替你跑 vite build，而 designer\dist\ 被
rem .gitignore 忽略、干净检出上根本不存在 —— 漏这一步打出来的画布是白屏；本地恰好留着旧
rem dist 时更糟：静默把旧 UI 打进去。正主配方见 designer\package.json 的 "dist" 脚本。
echo   VITE build designer
call npx vite build
if errorlevel 1 (
    popd
    echo [fail] 画布前端构建失败（vite build）
    exit /b 1
)
rem --win 一次出齐 portable + nsis 两个产物，图标/版本保持一致
call npx electron-builder --win
if errorlevel 1 (
    popd
    echo [fail] 画布打包失败
    exit /b 1
)
popd
if not exist "designer\release\CStackGUI.exe" (
    echo [fail] 没找到 designer\release\CStackGUI.exe
    exit /b 1
)
rem 打包好的画布要有前端资源：只跑 electron-builder 不跑 vite build 时，
rem designer\dist\ 可能不存在（它被 .gitignore 忽略），产出的 exe 会白屏。
if not exist "designer\dist\index.html" (
    echo [fail] designer\dist\index.html 不存在 —— 画布前端没构建，打出来会是白屏
    exit /b 1
)

echo.
echo == 3/4 组装 release\ ==
if exist release rmdir /s /q release
if exist release (
    echo [fail] 旧 release\ 删不掉（exe 或文件被占用？关掉再试）
    exit /b 1
)
mkdir release\runtime\include
mkdir release\runtime\lib
mkdir release\runtime\res
rem 空目录也要建：画布导出 C 骨架默认就落这里，README 的目录清单里也列了它
mkdir release\apps
copy /y runtime\include\cstackgui.h release\runtime\include\ >nul
if errorlevel 1 (
    echo [fail] 复制 runtime\include\cstackgui.h 失败
    exit /b 1
)
copy /y build\lib\libcstackgui.a release\runtime\lib\ >nul
if errorlevel 1 (
    echo [fail] 复制 libcstackgui.a 失败（先跑 build-all.bat）
    exit /b 1
)
copy /y build\obj\cstackgui_res.o release\runtime\lib\ >nul
if errorlevel 1 (
    echo [fail] 复制 cstackgui_res.o 失败（先跑 build-all.bat）
    exit /b 1
)
rem runtime\res：带「程序配置」<名字>.rc 的工程会从这里引用清单和默认图标 ——
rem 少了它 windres 那一步会失败。
xcopy /e /i /q /y runtime\res release\runtime\res >nul
if errorlevel 1 (
    echo [fail] 复制 runtime\res 失败（缺清单/默认图标会让带 .rc 的工程编译失败）
    exit /b 1
)
xcopy /e /i /q /y examples release\examples >nul
if errorlevel 1 (
    echo [fail] 复制 examples 失败
    exit /b 1
)
rem 示例目录里那份 lib 是用系统路径链接的，发布包里统一用 runtime\lib，删掉避免混淆
for /d %%a in (release\examples\*) do rmdir /s /q "%%a\lib" 2>nul
copy /y packaging\build-app.bat release\build-app.bat >nul
if errorlevel 1 (
    echo [fail] 复制 build-app.bat 失败
    exit /b 1
)
copy /y packaging\README.release.md release\README.md >nul
if errorlevel 1 (
    echo [fail] 复制 README.md 失败
    exit /b 1
)
echo == 3.5 画布安装包（便携每次启动要解包，装一次能常驻，日常用快很多）==
copy /y designer\release\CStackGUI.exe release\CStackGUI.exe >nul
if errorlevel 1 (
    echo [fail] 复制画布失败
    exit /b 1
)
copy /y designer\release\CStackGUI-Setup.exe release\CStackGUI-Setup.exe >nul
if errorlevel 1 (
    echo [fail] 复制画布安装包失败（先跑一次 designer 打包）
    exit /b 1
)

rem 开发工具：clangd 配置生成器 + 格式契约回归 + 截图
if not exist release\tools mkdir release\tools
copy /y packaging\check-format.bat release\tools\check-format.bat >nul
copy /y tools\gen-cc.mjs release\tools\ >nul
rem 编译不需要任何内核文件：实现在 CStackGUI.exe 内部（cgbuild.mjs 随 asar 打包），
rem 编译所需的 cstackgui.h / libcstackgui.a / cstackgui_res.o / runtime\res 也由
rem electron-builder 的 extraResources 内嵌进 exe。客户拿走单个 CStackGUI.exe 就能编
rem 自己的工程（只另需 MinGW-w64）。release\ 里其余内容都是给开发者的源码与示例。
copy /y tools\format-roundtrip.mjs release\tools\ >nul
copy /y tools\shot.ps1 release\tools\ >nul
rem cg_kit 冒烟测试也带上：发布包里那份预编译库要能真跑通文件/配置/编码
copy /y tools\check-kit.bat release\tools\ >nul
copy /y tools\kit-test.c release\tools\ >nul
rem 六个工具逐个点名：少一个用户就拿不到对应能力，且不报错
for %%f in (check-format.bat gen-cc.mjs format-roundtrip.mjs shot.ps1 check-kit.bat kit-test.c) do if not exist "release\tools\%%f" (
    echo [fail] release\tools\%%f 不在包里
    exit /b 1
)

rem AI 编程助手（CodeBuddy 等）的项目级技能：用户用 AI 写 C 回调 / 手改 .cgui 时自动加载
xcopy /e /i /q /y packaging\codebuddy release\.codebuddy >nul
if errorlevel 1 (
    echo [fail] 复制 .codebuddy 技能失败
    exit /b 1
)

rem 运行时库必须真的进了包：发布包自带的 build-app.bat 第一件事就是找它，
rem 缺了的话用户在包里编不了任何东西（2026-09-23 审计发现 release\runtime\lib 曾经是空的）。
if not exist "release\runtime\lib\libcstackgui.a" (
    echo [fail] release\runtime\lib\libcstackgui.a 不在包里 —— 发布包不完整
    exit /b 1
)

rem 预编译画布模型（发布版 check-format 用；发布机上没有 designer 源码）
echo -- 预编译画布模型到 release\build\modeljs --
call npx --prefix designer tsc designer\src\model.ts --outDir release\build\modeljs --target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict
if errorlevel 1 (
    echo [fail] 预编译画布模型失败
    exit /b 1
)

rem 生成 clangd 的语言服务配置（相对路径，机器无关；用户编译后也会自动刷新）
echo -- 生成 release\compile_commands.json --
pushd release
node tools\gen-cc.mjs
popd
if errorlevel 1 (
    echo [fail] 生成 compile_commands.json 失败
    exit /b 1
)

echo.
echo == 4/4 自检 ==
rem 第一关先看**画布本体**：打包后的资源布局（asar 里的 dist、内嵌 runtime、GUI 子系统）
rem 与源码树不同，下面 release\ 目录的那几项自检盖不到它 —— 打包出一个点不开的画布，
rem 那几项一样全绿（2026-10-18 真跑补的关卡：起打包画布跑它自带的 43 项行为探针）。
echo   画布本体：打包画布的行为探针
node tools\canvas-package-smoke.mjs
if errorlevel 1 (
    echo [fail] 打包画布的行为自检没过
    exit /b 1
)

echo   发布目录：在 release\ 里编译并校验示例
pushd release
call build-app.bat gallery
if errorlevel 1 (
    popd
    echo [fail] 发布包里的编译脚本用不了
    exit /b 1
)
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui
if errorlevel 1 (
    popd
    echo [fail] 发布包里编译出来的示例自检没过
    exit /b 1
)
rem 记事本示例用到 cg_kit（文件/配置/编码），顺带验证发布包的运行时库
call build-app.bat notepad
if errorlevel 1 (
    popd
    echo [fail] 发布包里编译 cg_kit 示例失败
    exit /b 1
)
examples\notepad\notepad.exe --selftest examples\notepad\notepad.cgui
if errorlevel 1 (
    popd
    echo [fail] 发布包里记事本示例自检没过
    exit /b 1
)
call tools\check-format.bat
if errorlevel 1 (
    popd
    echo [fail] 发布包里的格式契约检查没过
    exit /b 1
)
call tools\check-kit.bat
if errorlevel 1 (
    popd
    echo [fail] 发布包里的 cg_kit 冒烟测试没过
    exit /b 1
)
popd

echo.
echo [ok] release\ 已生成，可以直接打包发给用户
dir /b release
