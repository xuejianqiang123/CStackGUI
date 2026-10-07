/**
 * @file    cgbuild.mjs
 * @brief   编译内核：把一个界面工程编成独立 exe（本工程编译逻辑的**唯一实现**）
 * @details 为什么要有这个脚本：
 *
 *   编译逻辑原先住在 `build-app.bat` + `build-runtime.bat` 两个带中文的 UTF-8 批处理里，
 *   它们要靠 `chcp 65001 + cmd /c "%~f0"` 重入才能被正确解析。画布 F5 走的是
 *   `cmd /d /s /c "chcp 65001 >nul & build-app.bat"`（`designer/electron/main.mjs`），
 *   chcp 与脚本挤在同一个 cmd 进程里，重入逻辑不生效 —— 在部分机器上表现为
 *   **行被从多字节字符中间切断**，后半段被当成命令（`'lery' is not recognized` 这类）。
 *   本机复现不出来，但失败形态与本仓库已记录的坑一致（cmd 按字节偏移扫描，
 *   UTF-8 中文 3 字节/字会漂移）。
 *
 *   Node 一律按 UTF-8 处理源码与输出，没有代码页问题。把逻辑搬到这里之后，
 *   bat 只剩 ASCII 薄壳，那一整类问题消失。
 *
 *   四个入口全部转发到本文件，实现只有一份：
 *     - 画布 F5：fork process.execPath（Electron 自带的 node）跑本文件
 *     - build-app.bat          -> node tools\cgbuild.mjs %*
 *     - build-runtime.bat      -> node tools\cgbuild.mjs --runtime-only
 *     - 发布包 CStackGUI.exe --build <工程>（Electron 无窗口 CLI 分支，内部同样跑本文件）
 *
 * 用法：node tools\cgbuild.mjs [<工程名|路径>] [--release] [--runtime-only]
 * 环境变量：
 *   MINGW                    MinGW-w64 的 bin 目录（默认 C:\mingw64\bin）
 *   CSTACKGUI_HOME           项目根（含 runtime\ 的那一层）；默认是本脚本的上级目录
 *   CG_APP_DIR               工程目录（画布原先靠它传参；本脚本也接受命令行参数）
 *   CSTACKGUI_SKIP_RUNTIME=1 跳过刷新运行时库（build-all.bat 已刷过一次时用）
 * 退出码：0 成功；非 0 失败（不建窗口、不暂停，命令行/CI 直接用）
 */
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = process.env.CSTACKGUI_HOME
  ? path.resolve(process.env.CSTACKGUI_HOME)
  : path.resolve(HERE, "..");

/** 与 build-runtime.bat / build-app.bat 保持一致的公共参数（tools\gen-cc.mjs 里也有一份） */
const CFLAGS_APP =
  "-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime/include";
const CFLAGS_RT =
  `${CFLAGS_APP} -Iruntime/src/core -Iruntime/src/win32 -Iruntime/src/skin -Iruntime/src/kit`;
const LDFLAGS =
  "-lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32 -luuid -lole32 -loleaut32 -lws2_32";
const RT_LAYERS = ["core", "win32", "skin", "kit"];

const log = (s) => process.stdout.write(`${s}\n`);
const fail = (s) => {
  process.stderr.write(`[fail] ${s}\n`);
  process.exit(1);
};
const mtime = (p) => (fs.existsSync(p) ? fs.statSync(p).mtimeMs : -Infinity);
const newest = (files) => files.reduce((a, f) => Math.max(a, mtime(f)), -Infinity);
const isDir = (p) => fs.existsSync(p) && fs.statSync(p).isDirectory();
const listExt = (dir, ext) =>
  isDir(dir)
    ? fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith(ext)).sort()
        .map((f) => path.join(dir, f))
    : [];

/** 内容相同的两个文件就不覆盖 —— 覆盖会把目标时间刷成"现在"，
 *  于是它永远比 exe 新，"没变就跳过"的判定就永不生效（bat 里用 fc /b 就是这个目的）。 */
function copyIfChanged(src, dst) {
  if (!fs.existsSync(src)) fail(`missing generated file: ${src}`);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  if (fs.existsSync(dst) && fs.readFileSync(src).equals(fs.readFileSync(dst))) return false;
  fs.copyFileSync(src, dst);
  return true;
}

/** 往 PATH 前面塞 MinGW 的 bin，并确认工具链齐了 */
function setupToolchain() {
  const mingw = process.env.MINGW || "C:\\mingw64\\bin";
  process.env.PATH = `${mingw}${path.delimiter}${process.env.PATH || ""}`;
  const has = (exe) => {
    const r = spawnSync("where", [exe], { stdio: "ignore", shell: false });
    return r.status === 0;
  };
  if (!has("gcc") || !has("windres") || !has("ar")) {
    fail(
      `gcc not found. Install MinGW-w64, or set the MINGW env var to its bin dir, e.g.:  set MINGW=D:\\mingw64\\bin\n` +
        `        (looked in: ${mingw} and the rest of PATH)`,
    );
  }
}

function run(argv, opts = {}) {
  const r = spawnSync(argv[0], argv.slice(1), { stdio: "inherit", ...opts });
  if (r.error) fail(`${argv[0]}: ${r.error.message}`);
  if (r.status !== 0) process.exit(r.status ?? 1);
}

/* ------------------------------------------------------------------ *
 * 运行时库（原 build-runtime.bat）
 * ------------------------------------------------------------------ */
function buildRuntime() {
  const objDir = path.join(ROOT, "build", "obj");
  const libDir = path.join(ROOT, "build", "lib");
  fs.mkdirSync(objDir, { recursive: true });
  fs.mkdirSync(libDir, { recursive: true });

  // 改头文件必须让所有目标文件失效：这些头声明了整个库赖以构建的结构体布局与宏，
  // 留着旧 .o 会悄悄链到旧定义上。
  const hdrT = newest([
    ...listExt(path.join(ROOT, "runtime", "include"), ".h"),
    ...RT_LAYERS.flatMap((d) => listExt(path.join(ROOT, "runtime", "src", d), ".h")),
  ]);

  let changed = false;
  const objs = [];
  for (const layer of RT_LAYERS) {
    const dir = path.join(ROOT, "runtime", "src", layer);
    for (const src of listExt(dir, ".c")) {
      const obj = path.join(objDir, `${path.basename(src, ".c")}.o`);
      objs.push(obj);
      if (mtime(obj) > mtime(src) && mtime(obj) > hdrT) continue;
      log(`  CC runtime/src/${layer}/${path.basename(src)}`);
      run(["gcc", ...CFLAGS_RT.split(" "), "-c", src, "-o", obj]);
      changed = true;
    }
  }
  if (!objs.length) fail(`no runtime sources found under ${path.join(ROOT, "runtime", "src")}`);

  const rcSrc = path.join(ROOT, "runtime", "res", "cstackgui.rc");
  const rcIco = path.join(ROOT, "runtime", "res", "cstackgui.ico");
  const resObj = path.join(objDir, "cstackgui_res.o");
  if (!(mtime(resObj) > mtime(rcSrc) && mtime(resObj) > mtime(rcIco))) {
    log("  RC runtime/res/cstackgui.rc");
    run(["windres", "--include-dir=runtime/res", "-O", "coff", rcSrc, "-o", resObj]);
    changed = true;
  }

  const lib = path.join(libDir, "libcstackgui.a");
  if (!fs.existsSync(lib)) changed = true;
  if (!changed) {
    log("  (runtime up to date, skipped compile)");
  } else {
    log("  AR build/lib/libcstackgui.a");
    // 先删旧库再打包：`ar rcs` 只增替、**不会删除**已消失的成员 —— 删掉/改名某个 .c
    // 之后重编，旧 .o 仍留在库里，可能被链进最终 exe（静默出错）。
    if (fs.existsSync(lib)) fs.rmSync(lib);
    run(["ar", "rcs", lib, ...objs, resObj]);
    log(`[ok] ${path.relative(ROOT, lib)}`);
  }
  refreshClangd(ROOT);
  return { lib, resObj };
}

/* ------------------------------------------------------------------ *
 * 一个工程（原 build-app.bat）
 * ------------------------------------------------------------------ */
function resolveApp(arg) {
  let app = arg || process.env.CG_APP_DIR || "";
  if (!app) {
    for (const base of ["apps", "examples"]) {
      const dir = path.join(ROOT, base);
      const first = isDir(dir) ? fs.readdirSync(dir).find((f) => isDir(path.join(dir, f))) : null;
      if (first) {
        app = first;
        break;
      }
    }
  }
  if (!app) fail("no project given and none found under apps\\ or examples\\");
  // 既可以是裸工程名（apps\<名字> / examples\<名字>），也可以是相对本根的路径。
  for (const cand of [path.join(ROOT, "apps", app), path.join(ROOT, "examples", app), path.resolve(ROOT, app)]) {
    if (isDir(cand)) return cand;
  }
  fail(`project "${app}" not found (looked in apps\\, examples\\, and as a path under ${ROOT})`);
}

function buildApp(appDir, release) {
  const name = path.basename(appDir);
  const out = path.join(appDir, `${name}.exe`);
  const appLib = path.join(appDir, "lib");
  fs.mkdirSync(appLib, { recursive: true });

  // 发布包里没有 runtime\src：它不编运行时源码，只链接预编译好的 runtime\lib。
  // 那种情况直接用预编译产物，也不刷运行时库。
  const hasRuntimeSrc = isDir(path.join(ROOT, "runtime", "src"));
  const lib = hasRuntimeSrc
    ? path.join(ROOT, "build", "lib", "libcstackgui.a")
    : path.join(ROOT, "runtime", "lib", "libcstackgui.a");
  const resObj = hasRuntimeSrc
    ? path.join(ROOT, "build", "obj", "cstackgui_res.o")
    : path.join(ROOT, "runtime", "lib", "cstackgui_res.o");
  if (hasRuntimeSrc && process.env.CSTACKGUI_SKIP_RUNTIME !== "1") {
    log("[info] refreshing runtime library");
    buildRuntime();
  }

  // 往工程目录里留一份运行时库的拷贝：这样整个目录被挪走之后还能自己重编。
  copyIfChanged(lib, path.join(appLib, "libcstackgui.a"));
  // 资源对象（清单 + 版本信息）里没有代码符号，链接器不会主动从静态库里取它 ——
  // 必须显式链接，否则 exe 拿不到 v6 通用控件（页签/进度条会退回老外观）。
  copyIfChanged(resObj, path.join(appLib, "cstackgui_res.o"));

  // 工程自带的 <名字>.rc（画布「程序配置」对话框生成的）会顶掉运行时那份通用版本。
  // 它是 UTF-8（含中文字符串），所以加 --codepage=65001；-I runtime\res 让它能引用
  // 到 v6 通用控件清单。
  let appRes = path.join(appLib, "cstackgui_res.o");
  const rcPath = path.join(appDir, `${name}.rc`);
  if (fs.existsSync(rcPath)) {
    appRes = path.join(appLib, `${name}_res.o`);
    const rtRc = path.join(ROOT, "runtime", "res", "cstackgui.rc");
    if (!(mtime(appRes) > mtime(rcPath) && mtime(appRes) > mtime(rtRc))) {
      log(`  RC ${path.relative(ROOT, rcPath)}`);
      run([
        "windres", "--codepage=65001", "-I", appDir, "-I", path.join(ROOT, "runtime", "res"),
        "-O", "coff", rcPath, "-o", appRes,
      ]);
    }
  }

  // 增量编译：把 <工程>\*.c 逐个编成 lib\<名字>.o。工程内的头文件和公开头 cstackgui.h
  // 变新同样会触发重编，避免悄悄链上旧代码。删掉的源文件自然从链接列表里消失。
  const hdrT = newest([
    ...listExt(appDir, ".h"),
    path.join(ROOT, "runtime", "include", "cstackgui.h"),
  ]);
  const objs = [];
  for (const src of listExt(appDir, ".c")) {
    const obj = path.join(appLib, `${path.basename(src, ".c")}.o`);
    objs.push(obj);
    if (mtime(obj) > mtime(src) && mtime(obj) > hdrT) continue;
    log(`  CC ${path.relative(ROOT, src)}`);
    run(["gcc", ...CFLAGS_APP.split(" "), "-c", src, "-o", obj]);
  }
  if (!objs.length) fail(`no .c sources found in ${appDir}`);

  // 链接的代价远低于一个不匹配的 exe，所以这里总是链接（build-app.bat 里也是这样，
  // 保守极性：宁可多链一次，也不能悄悄链上旧库）。
  log(`  LD ${path.relative(ROOT, out)}${release ? " (release, no console)" : " (debug, with console)"}`);
  run([
    "gcc", ...CFLAGS_APP.split(" "),
    ...(release ? ["-mwindows"] : []),
    "-o", out, ...objs, appRes, path.join(appLib, "libcstackgui.a"),
    ...LDFLAGS.split(" "), "-lstdc++",
  ]);

  log(`[ok] ${path.relative(ROOT, out)}`);
  log("");
  log(`    run it (double-click works):   ${out}`);
  log(`    self-check without window:     ${out} --selftest`);
  log(`    rebuild this project:          build-app.bat ${name}`);
  log("    在画布里编译：                 打开本工程的 .cgui，按 F5（画布就是调上面这一条）");
  refreshClangd(ROOT);
}

/** 刷新 clangd 语言服务配置（tools\gen-cc.mjs 里的编译参数和这里保持一致） */
function refreshClangd() {
  if (!fs.existsSync(path.join(ROOT, "tools", "gen-cc.mjs"))) return;
  const r = spawnSync(process.execPath, [path.join(ROOT, "tools", "gen-cc.mjs")], { stdio: "ignore" });
  if (r.error) return; // 静默跳过：没装 node 或者不在项目根时不影响编译结果
}

/* ------------------------------------------------------------------ */
function main() {
  const argv = process.argv.slice(2);
  const release = argv.includes("--release");
  const runtimeOnly = argv.includes("--runtime-only");
  const arg = argv.find((a) => !a.startsWith("--"));

  if (!fs.existsSync(path.join(ROOT, "runtime"))) {
    fail(`project root not found: ${ROOT} (set CSTACKGUI_HOME to the dir holding runtime\\ and apps\\)`);
  }
  process.chdir(ROOT);
  setupToolchain();

  if (runtimeOnly) {
    buildRuntime();
    return;
  }
  buildApp(resolveApp(arg), release);
}

main();
