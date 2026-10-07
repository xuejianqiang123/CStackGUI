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
rem ============================================================
rem  CStackGUI - thin shell.  ALL build logic lives INSIDE
rem  CStackGUI.exe (cgbuild.mjs, packed in its asar). This file
rem  only forwards:
rem      CStackGUI.exe --build [project-name-or-path] [--release]
rem
rem  Customer (release package): the exe sits next to this file,
rem  so we call it - no Node.js needed, no kernel file needed.
rem  Developer (source repo): no exe yet, so we run the very same
rem  cgbuild.mjs with this machine's node (the repo already needs
rem  node for tools\check-*.mjs).
rem
rem  *** ASCII ONLY - NO EXCEPTIONS. ***
rem  cmd decodes a .bat with the console code page captured when
rem  cmd itself started. UTF-8 Chinese is then read at drifting
rem  byte offsets, so a LATER line gets cut in half and its tail
rem  runs as a command - that is how "CStackGUI.exe" showed up as
rem  'StackGUI.exe' is not recognized. With no non-ASCII byte in
rem  this file nothing can be mis-decoded. Chinese build messages
rem  are emitted by the exe / node, where they survive any code
rem  page (WriteConsoleW / UTF-8).
rem ============================================================
setlocal
set "EXE=%~dp0CStackGUI.exe"
if exist "%EXE%" (
    "%EXE%" --build %*
    exit /b %errorlevel%
)

where node >nul 2>nul
if errorlevel 1 (
    echo [fail] neither CStackGUI.exe nor node.exe is available.
    echo        Put this .bat next to CStackGUI.exe, or install Node.js.
    exit /b 1
)
node "%~dp0designer\electron\cgbuild.mjs" %*
exit /b %errorlevel%
