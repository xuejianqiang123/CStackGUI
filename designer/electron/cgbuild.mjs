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
import { createHash } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

const mtime = (p) => (fs.existsSync(p) ? fs.statSync(p).mtimeMs : -Infinity);
const isDir = (p) => fs.existsSync(p) && fs.statSync(p).isDirectory();
const listExt = (dir, ext) =>
  isDir(dir)
    ? fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith(ext)).sort().map((f) => path.join(dir, f))
    : [];

/** 递归收集工程源码（模块化工程的 modules\ 子目录要进来）。
 *  排除 lib\ —— 那是 cgbuild 自己放中间产物（.o/.d）的目录，绝不能递归进去。
 *  平铺工程（没有子目录）扫出来与 listExt 一字不差，行为不变。 */
function listSrcDeep(dir, ext, out = []) {
  if (!isDir(dir)) return out;
  for (const name of fs.readdirSync(dir).sort()) {
    if (name === "lib" || name === "node_modules") continue;
    const full = path.join(dir, name);
    if (isDir(full)) listSrcDeep(full, ext, out);
    else if (name.toLowerCase().endsWith(ext)) out.push(full);
  }
  return out;
}

/* ---------------------------------------------------------------- include 展开（F3）
 * 语义与 runtime 的 cg_doc_expand_includes 同一套（pick / 三张表合并主文档优先 /
 * id 冲突报错 / 循环与重复引用跳过）；改这里请同步改那边。差异仅两点：
 *   - 构建期**没有**"跳过"：循环/重复引用直接构建失败（能报错的时机就别放过）；
 *   - 展开完成后删掉 include 字段（嵌进资源的就是自包含版，运行时不会再走 include 分支）。 */

function cguiCollectIds(nodes, set) {
  for (const n of nodes ?? []) {
    if (n.id) set.add(n.id);
    cguiCollectIds(n.children, set);
  }
}

function cguiExpandIncludes(text, mainDir) {
  const visited = new Set();
  const walk = (jsonText, dir, fileLabel) => {
    let doc;
    try {
      doc = JSON.parse(jsonText);
    } catch (e) {
      return { error: `${fileLabel} 不是合法 JSON：${e.message}` };
    }
    /* 先递归展开它自己的 include（并入它自己），再挑节点进主文档 */
    if (Array.isArray(doc.include)) {
      for (const inc of doc.include) {
        if (!inc || typeof inc.path !== "string" || !inc.path.trim())
          continue; /* 与 runtime 的宽容口径一致：path 非空字符串才收 */
        const file = path.resolve(dir, inc.path);
        const norm = path.normalize(file).toLowerCase();
        if (visited.has(norm)) continue; /* 循环 / 重复：跳过 */
        visited.add(norm);
        if (!fs.existsSync(file)) return { error: `include 引用的文件打不开：${file}（来自 ${fileLabel}）` };
        const r = walk(fs.readFileSync(file, "utf8"), path.dirname(file), file);
        if (r.error) return r;
        const sub = r.doc;
        /* pick：按 id 只取一个顶层节点；没命中 = 报错（不静默丢） */
        let nodes = sub.widgets ?? [];
        if (inc.pick) {
          nodes = nodes.filter((w) => w.id === inc.pick);
          if (!nodes.length) return { error: `include：${file} 里没有 id 为「${inc.pick}」的顶层节点` };
        }
        /* id 冲突：报错（主文档已有同名 id 的控件树里查一遍） */
        const hostIds = new Set();
        cguiCollectIds(doc.widgets, hostIds);
        for (const n of nodes) {
          const ids = new Set();
          cguiCollectIds([n], ids);
          for (const id of ids)
            if (hostIds.has(id))
              return { error: `include 冲突：id「${id}」（来自 ${file}）在主文档里已经存在。id 是事件绑定与 cg_find 的钥匙，不能静默改名` };
        }
        /* 三张表合并：同名 = 主文档优先 */
        for (const tbl of ["styles", "roles", "actions"]) {
          for (const [k, v] of Object.entries(sub[tbl] ?? {})) {
            if (doc[tbl] && Object.hasOwn(doc[tbl], k)) continue;
            (doc[tbl] ??= {})[k] = v;
          }
        }
        doc.widgets = [...(doc.widgets ?? []), ...nodes];
      }
    }
    delete doc.include; /* 嵌进资源的自包含版：运行时不再走 include 分支 */
    return { doc };
  };

  const first = walk(text, mainDir, path.join(mainDir, "<主 .cgui>"));
  if (first.error) return first;
  return { text: JSON.stringify(first.doc, null, 2) };
}

/* 一个工程可以纯 C、纯 C++，也可以混着来：后缀决定用 gcc 还是 g++ 编这一个文件 */
const CXX_EXTS = [".cpp", ".cc", ".cxx"];
const isCxx = (src) => CXX_EXTS.includes(path.extname(src).toLowerCase());

/* ---------------------------------------------------------------- 项目根 */

function findRoot() {
  const env = process.env.CSTACKGUI_HOME;
  if (env && fs.existsSync(path.join(env, "runtime"))) return path.resolve(env);
  /* 打包环境的坑（2026-10-18 真跑 make-release 自检抓到，此前 --build 在发布包里从来没通过）：
   * exe 内嵌的 resources\runtime\ 就在"往上找 runtime\"的路径上 —— 不排除它，就会把
   * **包自己的资源目录**当成项目根，接着去那里找 examples\ 自然找不到工程
   * （报「找不到工程 gallery」，而 gallery 明明就在 cwd 的 examples\ 里）。
   * 与 main.mjs 的 isBuiltinRuntimeDir 同一口径：根目录不许是 process.resourcesPath。 */
  const builtinRes = process.resourcesPath ? path.resolve(process.resourcesPath) : "";
  for (let d = path.resolve(HERE, "..", ".."), i = 0; i < 6; i++, d = path.dirname(d)) {
    if (builtinRes && d === builtinRes) continue;
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

/* ---------------------------------------------------------------- 增量与并发 */

/**
 * -MMD 精确增量：obj 在、.d 在、且 .d 列出的每个依赖都不比 obj 新 —— 才跳过编译。
 * 取代原先"任何一个头比 obj 新就全部重编"的 hdrT 规则（2026-10-17 架构评审
 * "改公共头 = 半全量"整改）：内部头改动只波及真正 include 它的文件；
 * 配合 runPool 并发，全量重编也快 4~6 倍。
 * @param obj 目标文件路径
 * @param dep gcc -MMD 生成的依赖文件（与 obj 同名 .d）
 * @return boolean true = 可以跳过这次编译
 */
function upToDate(obj, dep) {
  if (!fs.existsSync(obj) || !fs.existsSync(dep)) return false;
  const ot = mtime(obj);
  let text;
  try {
    text = fs.readFileSync(dep, "utf8");
  } catch {
    return false;
  }
  /* depfile 形如 "obj.o: src.c hdr1.h \\\n  hdr2.h"：先拼回续行，再按空白切，
     丢掉以 ":" 结尾的目标项，剩下的都是依赖路径（本工程路径不含空格） */
  const tokens = text.replace(/\\\r?\n/g, " ").split(/\s+/).filter((t) => t && !t.endsWith(":"));
  for (const d of tokens) {
    if (!fs.existsSync(d)) return false;
    if (mtime(d) > ot) return false;
  }
  return true;
}

/** 算一份运行时源码的内容指纹（SHA-1）
 *  @param src .c 源文件路径
 *  @return string 十六进制摘要
 */
const srcSig = (src) => createHash("sha1").update(fs.readFileSync(src)).digest("hex");

/** 判断这份源码自上次成功编译后有没有改过
 *  @param sig 与 .o 同目录的 .sig 指纹文件
 *  @param src .c 源文件路径
 *  @return boolean true = 内容一致、可跳过编译
 */
function sigMatch(sig, src) {
  if (!fs.existsSync(sig)) return false;
  try {
    return fs.readFileSync(sig, "utf8").trim() === srcSig(src);
  } catch {
    return false;
  }
}

/** 编译成功后记下这次的源码指纹
 *  @param sig 与 .o 同目录的 .sig 指纹文件
 *  @param src .c 源文件路径
 *  @return void 无返回值
 */
function sigWrite(sig, src) {
  fs.writeFileSync(sig, srcSig(src));
}

/**
 * 并发跑一组编译任务（限流），全部完成后返回首个失败码（0 = 全部成功）
 * @param tasks 每项是返回退出码的 async 函数
 * @param limit 最大并发路数
 * @return number 退出码
 */
async function runPool(tasks, limit) {
  let i = 0;
  let firstErr = 0;
  const workers = Array.from({ length: Math.min(limit, tasks.length) }, async () => {
    while (i < tasks.length) {
      const k = i++;
      const rc = await tasks[k]();
      if (rc && !firstErr) firstErr = rc;
    }
  });
  await Promise.all(workers);
  return firstErr;
}

/* ---------------------------------------------------------------- 运行时库 */

async function buildRuntime(ctx, root, tk) {
  const objDir = path.join(root, "build", "obj");
  const libDir = path.join(root, "build", "lib");
  fs.mkdirSync(objDir, { recursive: true });
  fs.mkdirSync(libDir, { recursive: true });

  const layers = ["core", "win32", "skin", "kit"];

  const objs = [];
  const tasks = [];
  for (const layer of layers) {
    for (const src of listExt(path.join(tk.src, layer), ".c")) {
      const base = path.basename(src, ".c");
      const obj = path.join(objDir, `${base}.o`);
      const dep = path.join(objDir, `${base}.d`);
      const sig = path.join(objDir, `${base}.sig`);
      objs.push(obj);
      /* -MMD 精确增量（见 upToDate）：内部头改动只波及真正引用它的文件。
       * 再加一层源码内容指纹：mtime 会被 git / 打包工具还原成比 .o 更早的时间，
       * 只比时间戳就判"已是最新"，源码里的修复永远进不了 .a / exe
       * （2026-11-08 输入框边框修复在已编译产物里没生效就是这个）。 */
      if (upToDate(obj, dep) && sigMatch(sig, src)) continue;
      tasks.push(async () => {
        ctx.log(`  CC runtime/src/${layer}/${path.basename(src)}`);
        const rc = await ctx.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
          `-I${path.join(root, "runtime", "include")}`,
          ...layers.map((l) => `-I${path.join(tk.src, l)}`),
          "-MMD", "-MF", dep,
          "-c", src, "-o", obj]);
        if (!rc) sigWrite(sig, src);
        return rc;
      });
    }
  }
  if (!objs.length) return ctx.fail(`${tk.src} 下没有找到运行时源码`);
  if (tasks.length) {
    ctx.log(`  （需重编 ${tasks.length} 个文件，并行 6 路）`);
    const rc = await runPool(tasks, 6);
    if (rc) return rc;
  }

  const rcSrc = path.join(tk.resDir, "cstackgui.rc");
  const rcIco = path.join(tk.resDir, "cstackgui.ico");
  const resObj = path.join(objDir, "cstackgui_res.o");
  if (!(mtime(resObj) > mtime(rcSrc) && mtime(resObj) > mtime(rcIco))) {
    ctx.log("  RC runtime/res/cstackgui.rc");
    const rc = await ctx.run(["windres", `--include-dir=${tk.resDir}`, "-O", "coff", rcSrc, "-o", resObj]);
    if (rc) return rc;
  }

  /* 库新鲜度用**状态判据**（.a 是否比全部 .o 与资源对象都新），不用"本轮编过文件"
     这种轮次判据 —— 构建被打断在 AR 之前时，轮次判据会永久跳过 AR，让 .a 悄悄落后
     于 .o，下一轮报"已是最新"（2026-10-17 二轮评审"假绿灯"整改；app 侧"总是链接"
     本就是这个哲学，这里对齐）。 */
  const lib = path.join(libDir, "libcstackgui.a");
  const newestObj = [...objs, resObj].reduce((a, f) => Math.max(a, mtime(f)), -Infinity);
  if (fs.existsSync(lib) && mtime(lib) >= newestObj) {
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

  /* 单文件发布：把界面文件作为 PE 资源一起编进 exe（运行时在所有文件位置都找不到
     .cgui 时才读它）。用资源而不是"生成一个几百 KB 的 C 数组"：
       - 不用改工程代码 —— 手写工程（examples/*）也能直接单文件发布；
       - 不生成中间源码，不污染工程目录。
     旁边放一份 .cgui 仍然优先，所以"改界面不必重新编译"照旧成立。

     include[]（F3）：工程 .cgui 写了 include 时，**嵌进资源的必须是展开后的完整版** ——
     运行时从资源加载拿不到目录，解析不了相对路径。展开语义与 runtime 的
     cg_doc_expand_includes 同一套（pick / 三张表合并主文档优先 / id 冲突报错 /
     循环与重复引用跳过）；改动请两处同步。工程 .cgui 本身不动（保留 include），
     开发态"改片段重开即见"照旧。 */
  const cguiPath = path.join(appDir, `${name}.cgui`);
  let cguiRes = null;
  if (fs.existsSync(cguiPath) && fs.statSync(cguiPath).size > 0) {
    const objDir = path.join(root, "build", "obj");
    const rcFile = path.join(objDir, `${name}_cgui_res.rc`);
    const objFile = path.join(objDir, `${name}_cgui_res.o`);
    let embedPath = cguiPath;

    /* --- include 展开（仅当写了 include；展开版写到 obj 目录，工程文件不动） --- */
    const raw = fs.readFileSync(cguiPath, "utf8");
    if (/"include"\s*:/.test(raw)) {
      const exp = cguiExpandIncludes(raw, appDir);
      if (exp.error) {
        ctx.log(`  [fail] include 展开：${exp.error}`);
        return 1;
      }
      fs.writeFileSync(path.join(objDir, `${name}_cgui_expanded.cgui`), exp.text, "utf8");
      embedPath = path.join(objDir, `${name}_cgui_expanded.cgui`);
      ctx.log("  include 已展开后编进资源（单文件发布自包含）");
    }

    fs.mkdirSync(objDir, { recursive: true });
    /* 资源内容只取决于 .rc 与被嵌入文件的字节。光比 mtime 会漏：git / 打包工具把
       .cgui 还原成旧时间戳时 obj 反而比它新，判「已是最新」后改过的界面永远进不了 exe
       （与运行时 .c 用 SHA-1 指纹防漏是同一个成因）。指纹把 .rc 路径和界面字节一起算。 */
    fs.writeFileSync(rcFile, `CGUI RCDATA "${embedPath.replace(/\\/g, "/")}"\r\n`, "utf8");
    const cguiSigFile = path.join(objDir, `${name}_cgui_res.sig`);
    const cguiSig = createHash("sha1")
      .update(fs.readFileSync(rcFile))
      .update(fs.readFileSync(embedPath))
      .digest("hex");
    let cguiSigOK = false;
    if (fs.existsSync(cguiSigFile)) {
      try {
        cguiSigOK = fs.readFileSync(cguiSigFile, "utf8").trim() === cguiSig;
      } catch {
        cguiSigOK = false;
      }
    }
    if (!(mtime(objFile) > mtime(embedPath)) || !cguiSigOK) {
      ctx.log("  RC 把界面编进 exe（单文件发布）");
      const rc = await ctx.run(["windres", "--codepage=65001", "-O", "coff", rcFile, "-o", objFile]);
      if (rc) return rc;
      fs.writeFileSync(cguiSigFile, cguiSig);
    }
    cguiRes = objFile;
  }

  /* 增量编译：与运行时同一套 -MMD 精确增量（upToDate）——
     工程自己的 .h 或公共头改动只波及真正引用它的源文件 */
  /* 递归收集（listSrcDeep）：模块化工程把各工具放 modules\ 子目录；
     平铺工程扫出来与原来一字不差。lib\ 是本工具的产物目录，被排除在外。 */
  const sources = [...listSrcDeep(appDir, ".c"), ...CXX_EXTS.flatMap((e) => listSrcDeep(appDir, e))];
  const objs = [];
  for (const src of sources) {
    const base = path.basename(src, path.extname(src));
    const obj = path.join(appLib, `${base}.o`);
    const dep = path.join(appLib, `${base}.d`);
    objs.push(obj);
    if (upToDate(obj, dep)) continue;
    ctx.log(`  CC ${path.basename(src)}`);
    /* C++ 源文件交给 g++：gcc 按 C 编 .cpp 会因为后缀不认而失败 */
    const rc = await ctx.run([isCxx(src) ? "g++" : "gcc", isCxx(src) ? "-std=c++17" : "-std=c11",
      "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
      `-I${tk.inc}`, "-MMD", "-MF", dep, "-c", src, "-o", obj]);
    if (rc) return rc;
  }
  if (!objs.length) return ctx.fail(`${appDir} 里没有源文件（.c / .cpp / .cc / .cxx）`);

  /* 链接的代价远低于一个不匹配的 exe，所以总是链接。
     工程里有 C++ 就用 g++ 链接：C++ 异常 / 静态对象的初始化要它带进来的一堆东西，
     少一样就会在"链接过了、一运行就崩"这种最难查的地方炸。 */
  const hasCxx = sources.some(isCxx);
  if (hasCxx && !hasExe("g++")) {
    return ctx.fail("工程里有 C++ 源文件，但没找到 g++。MinGW-w64 装完整就有它（gcc 旁边那个 g++.exe）");
  }
  ctx.log(`  LD ${name}.exe${release ? "（发布版，无控制台）" : "（调试版，带控制台）"}${hasCxx ? "（C++ 工程，用 g++ 链接）" : ""}`);
  /* C++ 工程静态收进 libstdc++ / libgcc：客户机器上没有 MinGW，
     动态链接的 exe 一运行就是 0xC0000135（找不到 libstdc++-6.dll）。 */
  const rc = await ctx.run([hasCxx ? "g++" : "gcc", hasCxx ? "-std=c++17" : "-std=c11",
    /* -static：把 libstdc++ / libgcc / libwinpthread 全静态收进 exe。
       不做这件事，exe 在没装 MinGW 的机器上要么 0xC0000135（找不到 libstdc++-6.dll），
       要么更糟 —— 从 PATH 里捡到一个位数不对的同名 dll，报 0xC000007B。 */
    ...(hasCxx ? ["-static"] : []),
    "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter", `-I${tk.inc}`,
    ...(release ? ["-mwindows"] : []),
    "-o", out, ...objs, appRes, ...(cguiRes ? [cguiRes] : []), path.join(appLib, "libcstackgui.a"),
    "-lcomctl32", "-luser32", "-lgdi32", "-lshell32", "-lgdiplus", "-lcomdlg32",
    "-luuid", "-lole32", "-loleaut32", "-lws2_32", ...(hasCxx ? [] : ["-lstdc++"])]);
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
