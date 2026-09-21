@echo off
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
chcp 65001 >nul
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

echo.
echo == 3/4 组装 release\ ==
if exist release rmdir /s /q release
mkdir release\runtime\include
mkdir release\runtime\lib
mkdir release\runtime\res
copy /y runtime\include\cstackgui.h release\runtime\include\ >nul
copy /y build\lib\libcstackgui.a release\runtime\lib\ >nul
copy /y build\obj\cstackgui_res.o release\runtime\lib\ >nul
rem runtime\res：带「程序配置」<名字>.rc 的工程会从这里引用清单和默认图标 ——
rem 少了它 windres 那一步会失败。
xcopy /e /i /q /y runtime\res release\runtime\res >nul
if errorlevel 1 (
    echo [fail] 复制运行时库失败
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
copy /y packaging\README.release.md release\README.md >nul
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
copy /y tools\format-roundtrip.mjs release\tools\ >nul
copy /y tools\shot.ps1 release\tools\ >nul
rem cg_kit 冒烟测试也带上：发布包里那份预编译库要能真跑通文件/配置/编码
copy /y tools\check-kit.bat release\tools\ >nul
copy /y tools\kit-test.c release\tools\ >nul

rem AI 编程助手（CodeBuddy 等）的项目级技能：用户用 AI 写 C 回调 / 手改 .cgui 时自动加载
xcopy /e /i /q /y packaging\codebuddy release\.codebuddy >nul

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
echo == 4/4 自检：在 release\ 里编译并校验一个示例 ==
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
