/**
 * @file    cgbuild.mjs
 * @brief   编译编排：把一个界面工程编成独立 exe（本工程编译逻辑的**唯一实现**）
 * @details 这份代码住在画布 asar 里（`designer/electron/`），由 CStackGUI.exe 自带的
 *   Node 运行时执行 —— **客户不需要安装 Node.js**，也不需要旁边放任何内核文件。
 *
 *   为什么不在 exe 旁边放 cgbuild.exe / 带中文的 build-app.bat：
 *     - 客户拿到的是**单个 CStackGUI.exe**（自包含便携 exe），不该附带一堆文件；
 *     - 编译所需的 C 运行时资源（cstackgui.h / libcstackgui.a / cstackgui_res.o /
 *       runtime\res\）已由 electron-builder 的 extraResources 内嵌进 exe，
 *       从 process.resourcesPath 直接取，不必再"自举"到 exe 旁边；
 *     - bat 里的中文会被 cmd 按代码页切断（`'lery' is not recognized` 那一类）；
 *       这里是 Node，输出走 WriteConsoleW / UTF-8，**中文在任何代码页下都正确**。
 *
 *   入口（都走本文件的 build()）：
 *     - 画布 F5：主进程直接 await build()，不经 cmd / bat / 外部 exe，也不阻塞界面
 *     - CStackGUI.exe --build <工程>：无窗口 CLI
 *     - build-app.bat：ASCII 薄壳，转发到 CStackGUI.exe --build
 *
 *   客户侧唯一外部依赖：MinGW-w64（gcc / ar / windres）。
 *
 * 用法：CStackGUI.exe --build [工程名|路径] [--release]
 */
import fs from "node:fs";
import path from "node:path";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

const mtime = (p) => (fs.existsSync(p) ? fs.statSync(p).mtimeMs : -Infinity);
const isDir = (p) => fs.existsSync(p) && fs.statSync(p).isDirectory();
const listExt = (dir, ext) =>
  isDir(dir)
    ? fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith(ext)).sort().map((f) => path.join(dir, f))
    : [];
const newest = (files) => files.reduce((a, f) => Math.max(a, mtime(f)), -Infinity);

/* ---------------------------------------------------------------- 项目根 */

function findRoot() {
  const env = process.env.CSTACKGUI_HOME;
  if (env && fs.existsSync(path.join(env, "runtime"))) return path.resolve(env);
  for (let d = path.resolve(HERE, "..", ".."), i = 0; i < 6; i++, d = path.dirname(d)) {
    if (fs.existsSync(path.join(d, "runtime"))) return d;
  }
  const portable = process.env.PORTABLE_EXECUTABLE_DIR;
  if (portable && fs.existsSync(portable)) return portable;
  if (fs.existsSync(path.join(process.cwd(), "runtime"))) return process.cwd();
  return "";
}

/** 编译资源：优先 exe 内嵌的（客户机器上没有源码树），其次源码树现编的那份 */
function toolkit(root) {
  const builtin = process.resourcesPath ? path.join(process.resourcesPath, "runtime") : "";
  if (builtin && fs.existsSync(path.join(builtin, "lib", "libcstackgui.a"))) {
    return {
      inc: path.join(builtin, "include"),
      lib: path.join(builtin, "lib", "libcstackgui.a"),
      reso: path.join(builtin, "lib", "cstackgui_res.o"),
      resDir: path.join(builtin, "res"),
      src: "",
    };
  }
  return {
    inc: path.join(root, "runtime", "include"),
    lib: path.join(root, "build", "lib", "libcstackgui.a"),
    reso: path.join(root, "build", "obj", "cstackgui_res.o"),
    resDir: path.join(root, "runtime", "res"),
    src: path.join(root, "runtime", "src"),
  };
}

/* ---------------------------------------------------------------- 工具链 */

/** where.exe 在 System32；同步探测一次即可 */
function hasExe(exe) {
  return spawnSync("where", [exe], { stdio: "ignore" }).status === 0;
}

/* ---------------------------------------------------------------- 执行器 */

/**
 * 一次编译的上下文：持有输出回调与当前退出码。
 * 子进程输出按 UTF-8 解码后逐行回传（GUI 面板 / 控制台都按 UTF-8 显示，中文不会乱码）。
 */
class Ctx {
  constructor(onLine) {
    this.onLine = onLine || ((s) => process.stdout.write(`${s}\n`));
  }
  log(s) { this.onLine(s); }
  fail(s) { this.onLine(`[fail] ${s}`); return 1; }
  /** 跑一条命令：不阻塞（异步），输出逐行回传，返回退出码 */
  run(argv) {
    return new Promise((resolve) => {
      const child = spawn(argv[0], argv.slice(1), { stdio: ["ignore", "pipe", "pipe"] });
      const push = (buf) => {
        for (const line of String(buf).replace(/\r$/, "").split(/\r?\n/)) {
          if (line.length) this.onLine(line);
        }
      };
      child.stdout.on("data", push);
      child.stderr.on("data", push);
      child.on("error", (e) => { this.onLine(`[fail] ${argv[0]}：${e.message}`); resolve(1); });
      child.on("close", (code) => resolve(code ?? 1));
    });
  }
  copyIfChanged(src, dst) {
    if (!fs.existsSync(src)) return this.fail(`缺少编译产物：${src}`);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    /* 内容相同就不覆盖 —— 覆盖会把目标时间刷成"现在"，
       于是它永远比 exe 新，"没变就跳过"的判定永不生效。 */
    if (fs.existsSync(dst) && fs.readFileSync(src).equals(fs.readFileSync(dst))) return 0;
    fs.copyFileSync(src, dst);
    return 0;
  }
}

/* ---------------------------------------------------------------- 运行时库 */

async function buildRuntime(ctx, root, tk) {
  const objDir = path.join(root, "build", "obj");
  const libDir = path.join(root, "build", "lib");
  fs.mkdirSync(objDir, { recursive: true });
  fs.mkdirSync(libDir, { recursive: true });

  const layers = ["core", "win32", "skin", "kit"];
  /* 改头文件必须让所有目标文件失效：这些头声明了整个库赖以构建的结构体布局与宏 */
  const hdrT = newest([
    ...listExt(path.join(root, "runtime", "include"), ".h"),
    ...layers.flatMap((d) => listExt(path.join(tk.src, d), ".h")),
  ]);

  let changed = false;
  const objs = [];
  for (const layer of layers) {
    for (const src of listExt(path.join(tk.src, layer), ".c")) {
      const obj = path.join(objDir, `${path.basename(src, ".c")}.o`);
      objs.push(obj);
      if (mtime(obj) > mtime(src) && mtime(obj) > hdrT) continue;
      ctx.log(`  CC runtime/src/${layer}/${path.basename(src)}`);
      const rc = await ctx.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
        `-I${path.join(root, "runtime", "include")}`,
        ...layers.map((l) => `-I${path.join(tk.src, l)}`),
        "-c", src, "-o", obj]);
      if (rc) return rc;
      changed = true;
    }
  }
  if (!objs.length) return ctx.fail(`${tk.src} 下没有找到运行时源码`);

  const rcSrc = path.join(tk.resDir, "cstackgui.rc");
  const rcIco = path.join(tk.resDir, "cstackgui.ico");
  const resObj = path.join(objDir, "cstackgui_res.o");
  if (!(mtime(resObj) > mtime(rcSrc) && mtime(resObj) > mtime(rcIco))) {
    ctx.log("  RC runtime/res/cstackgui.rc");
    const rc = await ctx.run(["windres", `--include-dir=${tk.resDir}`, "-O", "coff", rcSrc, "-o", resObj]);
    if (rc) return rc;
    changed = true;
  }

  const lib = path.join(libDir, "libcstackgui.a");
  if (!fs.existsSync(lib)) changed = true;
  if (!changed) {
    ctx.log("  （运行时库已是最新，跳过编译）");
    return 0;
  }
  ctx.log("  AR build/lib/libcstackgui.a");
  /* 先删旧库再打包：`ar rcs` 只增替、不删除已消失的成员 */
  if (fs.existsSync(lib)) fs.rmSync(lib);
  const rc = await ctx.run(["ar", "rcs", lib, ...objs, resObj]);
  if (rc) return rc;
  ctx.log("[ok] build/lib/libcstackgui.a");
  return 0;
}

function refreshClangd() {
  /* 没装 node 时静默跳过，不影响编译结果 */
  const gen = path.join(findRoot(), "tools", "gen-cc.mjs");
  if (!fs.existsSync(gen)) return;
  try {
    spawnSync(process.execPath, [gen], { stdio: "ignore", env: { ...process.env, ELECTRON_RUN_AS_NODE: "1" } });
  } catch { /* noop */ }
}

/* ---------------------------------------------------------------- 一个工程 */

async function buildApp(ctx, root, tk, appDir, release) {
  const name = path.basename(appDir);
  const out = path.join(appDir, `${name}.exe`);
  const appLib = path.join(appDir, "lib");
  fs.mkdirSync(appLib, { recursive: true });

  if (tk.src && isDir(tk.src)) {
    if (process.env.CSTACKGUI_SKIP_RUNTIME !== "1") {
      ctx.log("[info] 正在刷新运行时库……");
      const rc = await buildRuntime(ctx, root, tk);
      if (rc) return rc;
    }
  } else if (process.env.CSTACKGUI_SKIP_RUNTIME !== "1") {
    ctx.log("  （使用 exe 内嵌的运行时库，跳过编译）");
  }
  if (!fs.existsSync(tk.lib)) return ctx.fail(`找不到运行时库：${tk.lib}`);

  /* 往工程目录里留一份副本：整个目录被挪走之后还能自己重编。
     资源对象没有代码符号，必须显式链接，否则 exe 拿不到 v6 通用控件。 */
  ctx.copyIfChanged(tk.lib, path.join(appLib, "libcstackgui.a"));
  ctx.copyIfChanged(tk.reso, path.join(appLib, "cstackgui_res.o"));

  /* 工程自带的 <名字>.rc 会顶掉通用版本；它是 UTF-8，所以加 --codepage=65001。 */
  let appRes = path.join(appLib, "cstackgui_res.o");
  const rcPath = path.join(appDir, `${name}.rc`);
  if (fs.existsSync(rcPath)) {
    appRes = path.join(appLib, `${name}_res.o`);
    if (!(mtime(appRes) > mtime(rcPath) && mtime(appRes) > mtime(path.join(tk.resDir, "cstackgui.rc")))) {
      ctx.log(`  RC ${name}.rc`);
      const rc = await ctx.run(["windres", "--codepage=65001", "-I", appDir, "-I", tk.resDir,
        "-O", "coff", rcPath, "-o", appRes]);
      if (rc) return rc;
    }
  }

  /* 增量编译：头变新同样触发重编，避免悄悄链上旧代码 */
  const hdrT = newest([...listExt(appDir, ".h"), path.join(tk.inc, "cstackgui.h")]);
  const objs = [];
  for (const src of listExt(appDir, ".c")) {
    const obj = path.join(appLib, `${path.basename(src, ".c")}.o`);
    objs.push(obj);
    if (mtime(obj) > mtime(src) && mtime(obj) > hdrT) continue;
    ctx.log(`  CC ${path.basename(src)}`);
    const rc = await ctx.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
      `-I${tk.inc}`, "-c", src, "-o", obj]);
    if (rc) return rc;
  }
  if (!objs.length) return ctx.fail(`${appDir} 里没有 .c 源文件`);

  /* 链接的代价远低于一个不匹配的 exe，所以总是链接 */
  ctx.log(`  LD ${name}.exe${release ? "（发布版，无控制台）" : "（调试版，带控制台）"}`);
  const rc = await ctx.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter", `-I${tk.inc}`,
    ...(release ? ["-mwindows"] : []),
    "-o", out, ...objs, appRes, path.join(appLib, "libcstackgui.a"),
    "-lcomctl32", "-luser32", "-lgdi32", "-lshell32", "-lgdiplus", "-lcomdlg32",
    "-luuid", "-lole32", "-loleaut32", "-lws2_32", "-lstdc++"]);
  if (rc) return rc;

  ctx.log(`[ok] ${out}`);
  ctx.log("");
  ctx.log(`    直接运行（双击也行）：   ${out}`);
  ctx.log(`    不开窗自检：             ${out} --selftest`);
  ctx.log(`    重新编译本工程：         CStackGUI.exe --build ${name}`);
  ctx.log("    在画布里编译：           打开本工程的 .cgui，按 F5");
  refreshClangd();
  return 0;
}

/* ---------------------------------------------------------------- */

/**
 * 编译主入口（异步，不阻塞界面）。
 * @param {object}  opts
 * @param {string}  [opts.root]     项目根；缺省自动找
 * @param {string}  [opts.project]  工程名或路径；缺省取 apps\ / examples\ 第一个
 * @param {boolean} [opts.release]  发布版（无控制台）
 * @param {(line:string)=>void} [opts.onLine] 逐行输出回调（GUI 面板接这个）
 * @returns {Promise<number>} 退出码
 */
export async function build(opts = {}) {
  const root = opts.root || findRoot();
  const ctx = new Ctx(opts.onLine);
  if (!root) return ctx.fail("找不到 CStackGUI 项目根（含 runtime\\ 的那一层）。可设置 CSTACKGUI_HOME。");
  process.chdir(root);

  const mingw = process.env.MINGW || "C:\\mingw64\\bin";
  process.env.PATH = `${mingw}${path.delimiter}${process.env.PATH || ""}`;
  if (!hasExe("gcc") || !hasExe("windres") || !hasExe("ar")) {
    return ctx.fail(
      "没找到 gcc / ar / windres。请安装 MinGW-w64，或用 MINGW 指向它的 bin 目录，\n" +
        `        例如：  set MINGW=D:\\mingw64\\bin\n        已查找：${mingw} 以及 PATH 里的其余路径`,
    );
  }

  const tk = toolkit(root);
  if (opts.runtimeOnly) {
    if (!tk.src || !isDir(tk.src)) return ctx.fail("当前用的是 exe 内嵌的运行时库（没有 runtime\\src），无法单独编译它");
    return buildRuntime(ctx, root, tk);
  }
  let appDir = opts.project || process.env.CG_APP_DIR || "";
  if (!appDir) {
    for (const base of ["apps", "examples"]) {
      const dir = path.join(root, base);
      const first = isDir(dir) ? fs.readdirSync(dir).find((f) => isDir(path.join(dir, f))) : null;
      if (first) { appDir = first; break; }
    }
  }
  if (!appDir) return ctx.fail("没有指定工程，apps\\ 和 examples\\ 下也没找到任何工程");
  const cand = [path.join(root, "apps", appDir), path.join(root, "examples", appDir), path.resolve(root, appDir)]
    .find(isDir);
  if (!cand) return ctx.fail(`找不到工程 "${appDir}"（已在 apps\\、examples\\ 和作为路径三种方式下找过）`);

  return buildApp(ctx, root, tk, cand, !!opts.release);
}

/* 命令行直连（开发态调试用） */
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  build({
    release: process.argv.includes("--release"),
    runtimeOnly: process.argv.includes("--runtime-only"),
    project: process.argv.slice(2).find((a) => !a.startsWith("--")),
  }).then((code) => process.exit(code));
}
