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
setlocal
cd /d "%~dp0"

echo == 1/4 构建运行时与示例 ==
call build-all.bat --release
if errorlevel 1 exit /b 1

echo.
echo == 2/4 打包画布（electron-builder）==
pushd designer
call npx electron-builder --win portable
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
copy /y runtime\include\cstackgui.h release\runtime\include\ >nul
copy /y build\lib\libcstackgui.a release\runtime\lib\ >nul
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
copy /y designer\release\CStackGUI.exe release\CStackGUI.exe >nul
if errorlevel 1 (
    echo [fail] 复制画布失败
    exit /b 1
)

echo.
echo == 4/4 自检：在 release\ 里编译并校验一个示例 ==
pushd release
call build-app.bat hello
if errorlevel 1 (
    popd
    echo [fail] 发布包里的编译脚本用不了
    exit /b 1
)
examples\hello\hello.exe --selftest examples\hello\hello.cgui
if errorlevel 1 (
    popd
    echo [fail] 发布包里编译出来的示例自检没过
    exit /b 1
)
popd

echo.
echo [ok] release\ 已生成，可以直接打包发给用户
dir /b release
