/* main.mjs -- Electron 主进程
 *
 * 画布本身是纯前端（React），这里负责四件浏览器做不到的事：
 *   1. 读写磁盘上的 .cgui（真正的「打开/保存」，不是下载文件）
 *   2. 把生成的 C 骨架落到 apps/<name>/ 里
 *   3. F5 一键运行：直接跑编译内核 tools\cgbuild.mjs，再把 exe 跑起来
 *      （命令行下同一件事：CStackGUI.exe --build <工程>）
 *   4. 打开系统资源管理器定位产物
 */
import { app, BrowserWindow, ipcMain, dialog, shell, Menu, clipboard } from "electron";
import path from "node:path";
import fs from "node:fs/promises";
import { existsSync, appendFileSync, readFileSync } from "node:fs";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { build } from "./cgbuild.mjs";
import { mergeEvents, callbackDiff } from "./merge-events.mjs";
import {
  skeletonLangOf,
  resolveLang,
  implFileName as implNameOf,
  entryFileName as entryNameOf,
  langWriteList,
} from "./skeleton-lang.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEV_URL = process.env.CSTACKGUI_DEV_URL || "";
const VERSION = "0.1.0";

/* 启动排查探针：设了 CSTACKGUI_LOG=<文件路径> 才写，平时零开销。
 * 打包后的 GUI exe 抓不到 stdout/stderr，白屏、找不到项目根这类问题全靠它。 */
const LOG_FILE = process.env.CSTACKGUI_LOG || "";
/* 进程内的相对时间起点：用来量"启动到出界面"到底花在哪一段
   （配合 CSTACKGUI_LOG 用；便携版解包发生在 Electron 起来之前，量不到，看差值即知）。 */
const T0 = Date.now();
function log(...args) {
  if (!LOG_FILE) return;
  try {
    const ms = String(Date.now() - T0).padStart(5, " ");
    appendFileSync(LOG_FILE, `[${new Date().toISOString()} +${ms}ms] ${args.join(" ")}\n`);
  } catch {
    /* 写日志失败绝不能影响启动 */
  }
}
log("main.mjs 模块加载完成（相对时间起点）");

let win = null;
let currentFile = "";

/* ------------------------------------------------ 外部改动热重载（.cgui 被别的程序改了）
 *
 * 场景：发布包里带的那套 AI 技能（cgui-edit）改完 .cgui 文本，或者使用者在编辑器里手改、
 * git checkout 换了版本 —— 已经开着的画布必须自己跟上，否则他会对着旧界面白改半天。
 *
 * 为什么用轮询而不是 fs.watch：编辑器（VS Code 这类）保存时常常"写临时文件 → 改名覆盖"，
 * Windows 上 fs.watch 盯着的那个句柄会跟着消失、之后**再也不报事件**（静默失效，最难查）。
 * 轮询 mtime+size 没这个坑；.cgui 就几 KB，700ms 一次 stat 的开销可以忽略。
 *
 * 为什么还要"比内容"：保存/导出之后时间戳必然变，只比时间戳会把"自己刚保存"
 * 也当成外部改动（画布会莫名跳出一句"已载入外部改动"）。
 */
const WATCH_MS = 700;
let watchTimer = null;
let watchPath = "";
let watchStamp = "";
let watchText = "";

function watchStop() {
  if (watchTimer) clearInterval(watchTimer);
  watchTimer = null;
  watchPath = "";
}

/** 记下"磁盘上现在是什么"：自己保存/导出之后调用，免得把自己写的内容当成外部改动。
 *  只把时间戳标记成未知，下次轮询读出来比一次内容 —— 一样就什么都不做。 */
function noteDiskContent(filePath, text) {
  if (filePath !== watchPath) return;
  watchText = text;
  watchStamp = "";
}

/** 开始盯一个文件。换文件时先停掉旧的：同一时刻只盯"当前这个"，
 *  否则打开 A 再打开 B，A 被人改了也会弹到 B 的画布上。 */
function watchStart(filePath, text) {
  watchStop();
  if (!filePath) return;
  watchPath = filePath;
  noteDiskContent(filePath, text);
  watchTimer = setInterval(() => {
    void pollWatch();
  }, WATCH_MS);
}

async function pollWatch() {
  if (!watchPath || !win || win.isDestroyed()) return;
  let st;
  try {
    st = await fs.stat(watchPath);
  } catch {
    return; /* 文件被删 / 被改名：当没这回事，等它回来 */
  }
  const stamp = `${st.mtimeMs}:${st.size}`;
  if (stamp === watchStamp) return;
  watchStamp = stamp;
  let text;
  try {
    text = await fs.readFile(watchPath, "utf8");
  } catch {
    return;
  }
  if (text === watchText) return; /* 只动了时间戳、内容一样（多半就是自己刚写的） */
  watchText = text;
  log("file:changed", `${watchPath}（${text.length} 字节）`);
  win.webContents.send("file:changed", { path: watchPath, text });
}

/** 命令行里带的 .cgui：CStackGUI.exe apps\hello\hello.cgui
 *  （打包后把 .cgui 关联到 CStackGUI.exe，双击界面文件也能直接打开） */
function cguiFromArgv() {
  for (const arg of process.argv.slice(1)) {
    if (!arg || arg.startsWith("-")) continue;
    if (arg.toLowerCase().endsWith(".cgui") && existsSync(arg)) return path.resolve(arg);
  }
  return "";
}

/* ------------------------------------------------------ C 项目根目录
 * 画布需要知道 CStackGUI 这个 C 项目在哪，才能把导出的骨架放进 appS\<名字>\，
 * 也才能告诉用户「接着去项目根跑 build-app.bat」。
 *
 * 打包成 CStackGUI.exe 后，程序被解到临时目录，靠「往上找 runtime\」是找不到的，
 * 所以要能把用户选过的目录记下来（存在 Electron 的 userData 里）。
 */
const configPath = () => path.join(app.getPath("userData"), "cstackgui.json");
let projectRootCache = null;

const isProjectRoot = (dir) => !!dir && existsSync(path.join(dir, "runtime", "include", "cstackgui.h"));

/** exe 自带的 resources\runtime\ 是内嵌的编译资源副本，不是项目根。
 *  从 __dirname 往上找根时必须排除，否则会把安装目录误认成项目根。 */
const isBuiltinRuntimeDir = (dir) => {
  const res = process.resourcesPath || "";
  return !!res && !!dir && path.resolve(dir) === path.resolve(res);
};

async function loadConfig() {
  try {
    return JSON.parse(await fs.readFile(configPath(), "utf8"));
  } catch {
    return {};
  }
}

async function saveConfig(patch) {
  try {
    const next = { ...(await loadConfig()), ...patch };
    await fs.mkdir(path.dirname(configPath()), { recursive: true });
    await fs.writeFile(configPath(), JSON.stringify(next, null, 2), "utf8");
  } catch (e) {
    console.error("[cstackgui] 保存配置失败:", e);
  }
}

/** 从某个目录往上找 CStackGUI 项目根 */
function findProjectRootUpwards(from) {
  let dir = from;
  for (let i = 0; i < 8; i++) {
    if (isProjectRoot(dir) && !isBuiltinRuntimeDir(dir)) return dir;
    const up = path.dirname(dir);
    if (up === dir) break;
    dir = up;
  }
  return "";
}

async function resolveProjectRoot() {
  if (isProjectRoot(projectRootCache)) return projectRootCache;

  /* 便携版（release\ 里的那个 CStackGUI.exe）会带这个环境变量：
   * 它指向 exe 所在目录。发布包里 runtime\ 就在旁边，所以直接就能认出来，
   * 用户不用手动指定项目根，导出/编译提示也都能给对。
   * 但开发机上 release\ 常嵌在真项目里（D:\CPro\CStackGUI\release），
   * 这种情况要先向上认出真根，否则一开画布就把项目根显示成 release。 */
  const portable = process.env.PORTABLE_EXECUTABLE_DIR;
  if (portable && existsSync(portable)) {
    /* 无论目录是不是已经是项目根，都先刷新内嵌资源：用户换了新 exe 时，
     * 自举出来的旧 runtime\ 会一直赖着（isProjectRoot 恒真），新外观永远编不进去。 */
    await ensureWorkspace(portable);
  }
  if (isProjectRoot(portable)) {
    const realRoot = findProjectRootUpwards(path.dirname(portable));
    projectRootCache = realRoot || portable;
    await saveConfig({ projectRoot: projectRootCache });
    return projectRootCache;
  }

  if (isProjectRoot(process.env.CSTACKGUI_HOME)) {
    projectRootCache = process.env.CSTACKGUI_HOME;
    return projectRootCache;
  }
  const cfg = await loadConfig();
  if (isProjectRoot(cfg.projectRoot)) {
    projectRootCache = cfg.projectRoot;
    return projectRootCache;
  }
  const found = findProjectRootUpwards(__dirname);
  if (found) {
    projectRootCache = found;
    await saveConfig({ projectRoot: found });
    return found;
  }
  /* 兜底：便携单文件 exe 放在任意目录 → 把 exe 旁边自举成工作区 */
  return await resolveWorkspaceRoot();
}

async function rememberProjectRoot(dir) {
  if (!isProjectRoot(dir)) return false;
  projectRootCache = dir;
  await saveConfig({ projectRoot: dir });
  return true;
}

/** 把 exe 内嵌的编译资源释放成一个可用的 CStackGUI 工作区（便携单文件场景）：
 *  runtime\include\cstackgui.h、runtime\lib\{libcstackgui.a,cstackgui_res.o}、
 *  build-app.bat。库文件每次都覆盖——保证跟这个 exe 的版本一致。
 *  非打包环境（resourcesPath 里没有这些文件）返回 false。 */
async function ensureWorkspace(root) {
  try {
    const res = process.resourcesPath || "";
    const srcBat = path.join(res, "build-app.bat");
    const srcH = path.join(res, "runtime", "include", "cstackgui.h");
    const srcA = path.join(res, "runtime", "lib", "libcstackgui.a");
    const srcO = path.join(res, "runtime", "lib", "cstackgui_res.o");
    if (!existsSync(srcBat) || !existsSync(srcH) || !existsSync(srcA)) {
      log("ensureWorkspace 内嵌资源缺失", `res=${res} bat=${existsSync(srcBat)} h=${existsSync(srcH)} a=${existsSync(srcA)}`);
      return false;
    }
    await fs.mkdir(path.join(root, "runtime", "include"), { recursive: true });
    await fs.mkdir(path.join(root, "runtime", "lib"), { recursive: true });
    await fs.mkdir(path.join(root, "apps"), { recursive: true });
    await fs.copyFile(srcH, path.join(root, "runtime", "include", "cstackgui.h"));
    await fs.copyFile(srcA, path.join(root, "runtime", "lib", "libcstackgui.a"));
    if (existsSync(srcO)) await fs.copyFile(srcO, path.join(root, "runtime", "lib", "cstackgui_res.o"));
    await fs.copyFile(srcBat, path.join(root, "build-app.bat"));
    /* runtime\res：带「程序配置」<name>.rc 的工程要从这里找清单和默认图标 */
    const srcRes = path.join(res, "runtime", "res");
    if (existsSync(srcRes)) await fs.cp(srcRes, path.join(root, "runtime", "res"), { recursive: true });
    return true;
  } catch {
    return false;
  }
}

/** 找不到项目根时的兜底：自举一个工作区。
 *  便携版：PORTABLE_EXECUTABLE_DIR 指向 exe 所在目录，就地自举；
 *  安装版没有这个环境变量，退回到 ~/CStackGUI，这样装完就有可用的工作区
 *  （开箱能建工程），用户之后用「定位项目根」可以随时改到别的地方。 */
async function resolveWorkspaceRoot() {
  const portable = process.env.PORTABLE_EXECUTABLE_DIR;
  const target =
    portable && existsSync(portable) ? portable : path.join(app.getPath("home"), "CStackGUI");
  const ok = await ensureWorkspace(target);
  log("resolveWorkspaceRoot", `target=${target} ok=${ok}`);
  if (ok) {
    projectRootCache = target;
    await saveConfig({ projectRoot: target });
    return target;
  }
  return "";
}

/* 窗口出现后、真正界面加载完之前显示的那一屏（内联，不依赖 vite 产物，
   所以它能立刻画出来）。配色与画布一致：品牌紫 #6750A4 + 底色 #F3EDF7。 */
const LOADING_PAGE =
  "data:text/html;charset=utf-8," +
  encodeURIComponent(
    `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>CStackGUI</title>
<style>
  html,body{margin:0;height:100%;background:#F3EDF7;
    font:14px/1.6 system-ui,"Microsoft YaHei",sans-serif;color:#49454F}
  body{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px}
  .mark{width:64px;height:64px;border-radius:50%;background:#6750A4;color:#fff;
    font:600 34px/64px system-ui;text-align:center}
</style></head>
<body><div class="mark">C</div><div>正在启动…</div></body></html>`,
  );

function createWindow() {
  log("createWindow 开始");
  win = new BrowserWindow({
    width: 1480,
    height: 940,
    minWidth: 1080,
    minHeight: 680,
    backgroundColor: "#F3EDF7",
    title: "CStackGUI",
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  if (DEV_URL) {
    /* 开发态 vite 自己有首屏，不插启动页（免得挡住热更新） */
    win.loadURL(DEV_URL);
  } else {
    /* 窗口一出现（+30ms 左右）先画一张内联的"正在启动…"，真正的界面加载完再切过去。
     *
     * 为什么需要它：便携版解包期间由打包器的启动图占着，但那张图与解包**同生共死**
     * （解包一结束就关），之后到本窗口出现之间还有约 2.7 秒 —— 那是 Electron 冷启动，
     * 我们的代码那时还没跑起来，谁都补不了。能补的是**本窗口出现之后**到界面可用之间
     * 那约 0.7 秒：以前是一块空底色，现在有字有标识。 */
    const indexFile = path.join(__dirname, "..", "dist", "index.html");
    let switched = false;
    const loadRealUi = () => {
      if (switched || !win || win.isDestroyed()) return;
      switched = true;
      win.loadFile(indexFile);
    };
    /* 两个事件都挂（各只触发一次 + switched 兜底），保证启动页画完就切，
     * 且启动页万一加载失败也不会把真正的界面卡住。 */
    win.webContents.once("did-finish-load", loadRealUi);
    win.webContents.once("did-fail-load", loadRealUi);
    win.loadURL(LOADING_PAGE);
  }

  /* 白屏/启动失败时这几个事件能直接说明是加载失败、preload 炸了，还是前端崩了 */
  win.once("ready-to-show", () => log("ready-to-show（窗口可以显示/已可见）"));

  const wc = win.webContents;
  wc.on("did-finish-load", () => log("did-finish-load（Renderer 加载完）"));
  wc.on("did-fail-load", (_e, code, desc, url) => log("did-fail-load", code, desc, url));
  wc.on("preload-error", (_e, file, err) => log("preload-error", file, String(err)));
  wc.on("render-process-gone", (_e, details) => log("render-process-gone", JSON.stringify(details)));
  /* Electron 38 起 console-message 改传 details 对象；两种签名都兜住 */
  wc.on("console-message", (_e, ...rest) => {
    const d =
      rest.length === 1 && typeof rest[0] === "object" && rest[0] !== null
        ? rest[0]
        : { level: rest[0], message: rest[1], lineNumber: rest[2], sourceId: rest[3] };
    log("console", `level=${d.level} ${d.message} (${d.sourceId}:${d.lineNumber})`);
  });
  win.on("closed", () => {
    win = null;
    watchStop(); /* 窗口没了就别再轮询磁盘 */
  });
  // 外部链接丢给系统浏览器，不在画布里开新窗口
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: "deny" };
  });
}

function send(channel, payload) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, payload);
}

/* ------------------------------------------------------------ IPC */

ipcMain.handle("app:info", async () => {
  const projectRoot = await resolveProjectRoot();
  log("app:info", `projectRoot=${projectRoot || "(none)"} file=${currentFile || "(none)"}`);
  return { version: VERSION, projectRoot, currentFile };
});

/** 渲染进程挂载后来问：命令行有没有指定要打开的界面文件 */
ipcMain.handle("app:initialFile", async () => {
  const fromArgv = cguiFromArgv();
  if (!fromArgv) return { canceled: true };
  try {
    const text = await fs.readFile(fromArgv, "utf8");
    currentFile = fromArgv;
    watchStart(fromArgv, text);
    return { canceled: false, path: fromArgv, text };
  } catch (e) {
    dialog.showErrorBox("打开失败", String(e));
    return { canceled: true };
  }
});

/** 让用户手动指定 C 项目根（打包后的 exe 靠这个找对地方） */
ipcMain.handle("app:locateProject", async () => {
  const res = await dialog.showOpenDialog(win, {
    title: "选择 CStackGUI 项目根目录（含 runtime、apps、designer 的那一层）",
    properties: ["openDirectory"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true };

  const chosen = res.filePaths[0];
  // 用户可能选中了子目录，往上找一层也行
  let root = (await rememberProjectRoot(chosen)) ? chosen : findProjectRootUpwards(chosen);
  if (!root) {
    /* 便携单文件场景：用户选的目录不是项目根也没关系——就地自举一个工作区
     * （释放内嵌的 runtime 库和编译脚本），以后这个目录就是他的项目根。 */
    if (await ensureWorkspace(chosen)) {
      root = chosen;
    } else {
      dialog.showErrorBox(
        "这里不是 CStackGUI 项目根",
        `在 "${chosen}" 里没找到 runtime\\include\\cstackgui.h。\n请选择包含 runtime\\ 和 apps\\ 的那一层目录。`,
      );
      return { canceled: true };
    }
  }
  await rememberProjectRoot(root);
  return { canceled: false, projectRoot: root };
});

ipcMain.handle("cgui:open", async () => {
  const res = await dialog.showOpenDialog(win, {
    title: "打开界面文件",
    filters: [{ name: "CStackGUI 界面", extensions: ["cgui"] }],
    properties: ["openFile"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true };
  const filePath = res.filePaths[0];
  try {
    const text = await fs.readFile(filePath, "utf8");
    currentFile = filePath;
    watchStart(filePath, text);
    return { canceled: false, path: filePath, text };
  } catch (e) {
    dialog.showErrorBox("打开失败", String(e));
    return { canceled: true };
  }
});

ipcMain.handle("cgui:save", async (_e, { filePath, text, suggestName }) => {
  let target = filePath || "";
  if (!target) {
    const res = await dialog.showSaveDialog(win, {
      title: "保存界面文件",
      defaultPath: suggestName ? `${suggestName}.cgui` : "untitled.cgui",
      filters: [{ name: "CStackGUI 界面", extensions: ["cgui"] }],
    });
    if (res.canceled || !res.filePath) return { canceled: true };
    target = res.filePath;
  }
  try {
    await fs.writeFile(target, text, "utf8");
    currentFile = target;
    watchStart(target, text); /* 换成"另存为"的新路径时，监视器也跟着换 */
    return { canceled: false, path: target };
  } catch (e) {
    dialog.showErrorBox("保存失败", String(e));
    return { canceled: true };
  }
});

/* ------------------------------------------------------------ 导出落点
 * 「导出」分两趟：先 exportPlan 问清楚"导到哪 + 这个工程现在用的是哪种语言"，
 * 才轮到真正的 exportC 写盘。中间要插一次用户选择（首次导出选 C 还是 C++），
 * 而"导到哪"的目录框只能弹一次 —— 第二趟带上 targetDir，就不至于让用户选两遍。 */

/** codegen 会把工程名里的非法字符换成 '_' —— 主进程推文件名时用同一套口径才对得上 */
const safePrefix = (s) => (s || "").replace(/[^0-9a-zA-Z_]/g, "_").replace(/^([0-9])/, "_$1");

/** 这个目录里现在用的是哪种语言（"" = 首次导出）。判据与取舍见 skeleton-lang.mjs。 */
const langOnDisk = (dir, prefix) => skeletonLangOf(prefix, (p) => existsSync(path.join(dir, p)));

/** 导出目录：能自己认出来就别弹框（详见调用处的注释）；targetDir 优先（第二趟带来的） */
async function resolveExportDir({ baseName, filePath, targetDir }) {
  const root =
    (filePath ? findProjectRootUpwards(path.dirname(path.resolve(filePath))) : "") || (await resolveProjectRoot());
  const appsDir = root ? path.join(root, "apps", baseName) : "";

  if (targetDir) return { canceled: false, root, dir: targetDir };

  /* 目标目录能自己认出来就别弹框 —— 工程已经在那儿了（.cgui 就躺在工程目录里、
   * 旁边有 main.c / main.cpp），每次导出/发布都问一遍"导到哪"是纯打扰；
   * 「发布」尤其受不了：它本来就只是想把最新界面重新编进 exe，不该中断在选目录上。
   * 只有在真的认不出来时（新文档、目录里没有任何工程痕迹）才问。 */
  const here = filePath ? path.dirname(path.resolve(filePath)) : "";
  if (here && (existsSync(path.join(here, "main.c")) || existsSync(path.join(here, "main.cpp")))) {
    return { canceled: false, root, dir: here };   // 原地就是工程目录
  }
  if (here && path.basename(here).toLowerCase() === baseName.toLowerCase()) {
    return { canceled: false, root, dir: here };
  }

  const res = await dialog.showOpenDialog(win, {
    title: root
      ? `导出骨架到 apps/${baseName}（也可以选别的目录）`
      : `导出骨架：请选 CStackGUI 的 apps 目录（找不到项目根，导完还要手动编译）`,
    defaultPath: appsDir || root || undefined,
    properties: ["openDirectory", "createDirectory"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true, root, dir: "" };
  const chosen = res.filePaths[0];
  // 用户可能选了 apps 这一层，或者选了自己想放的目录：两种都让结果落在 <chosen>/<baseName>
  return {
    canceled: false,
    root,
    dir: path.basename(chosen).toLowerCase() === baseName.toLowerCase() ? chosen : path.join(chosen, baseName),
  };
}

ipcMain.handle("cgui:exportPlan", async (_e, { baseName, filePath }) => {
  const plan = await resolveExportDir({ baseName, filePath, targetDir: "" });
  if (plan.canceled) return { canceled: true };
  const prefix = safePrefix(baseName);
  return { canceled: false, dir: plan.dir, lang: langOnDisk(plan.dir, prefix) };
});

ipcMain.handle("cgui:exportC", async (_e, { baseName, files, filePath, lang, targetDir }) => {
  const plan = await resolveExportDir({ baseName, filePath, targetDir });
  if (plan.canceled) return { canceled: true };
  const dir = plan.dir;
  const root = plan.root;

  /* 文件名要与 codegen 对齐：优先按 `_ui_events.h` 的前缀推，推不出来再按工程名换算。 */
  const headerName = files.find((f) => f.name.endsWith("_ui_events.h"))?.name ?? "";
  const prefix = headerName ? headerName.replace(/_ui_events\.h$/i, "") : safePrefix(baseName);

  /* 语言只在**首次导出**时由用户选（`cgui:exportPlan` 就是为问这一句而存在的往返）。
     目录里已经躺着一份骨架时，一律以磁盘现状为准 —— 往 C 工程里再落一份 `main.cpp`
     （或反过来）等于同一个工程两套入口：链接直接重复定义，而用户的业务代码可能写在另一份里。
     传入的 lang 与现状不一致时**只记一行日志**，写什么仍由现状决定。 */
  const existing = langOnDisk(dir, prefix);
  if (existing && lang && lang !== existing) {
    log("exportC", `[warn] 工程已是 ${existing} 骨架，忽略传入的 ${lang}（换语言请另建工程）`);
  }
  const effLang = resolveLang({ requested: lang, existing });
  const implFile = implNameOf(prefix, effLang);
  const mainFile = entryNameOf(effLang);

  /* 这个目录是不是"手写工程"（main.c / main.cpp 不是画布产物）？
   * 判据只能是模板首行的机器标记：示例的 main.c 同样含 desc.auto_locate_cgui /
   * k_bindings 这些"看着像模板"的东西，靠内容猜会误判（踩过：darkterm 的回调被清空）。
   * 手写工程**只同步界面文件 .cgui**（那是画布自己的产物），代码一律不碰 ——
   * 往里塞 main.c / _ui_events.* / .rc / build.bat 不但污染目录，还会被一起编进 exe。
   * 与"单文件发布"无关：界面是编译期作为 PE 资源编进 exe 的（见 cgbuild.mjs）。 */
  const mainPath = path.join(dir, mainFile);
  const normEol = (s) => s.replace(/\r\n/g, "\n");
  const oldMain = existsSync(mainPath) ? await fs.readFile(mainPath, "utf8") : "";
  const handwritten = oldMain ? !/generated-by-cstackgui/.test(oldMain) : false;

  /* 事件头是每次导出都重写的纯生成物 → 它 = "画布上次导出时绑定了哪些回调"的权威快照。
     导出前先读下来：老 .h 声明过、新 .h 里没有的名字，就是"画布上已不再绑定、而回调实现里还留着"的回调。 */
  const headerPath = headerName ? path.join(dir, headerName) : "";
  const cPath = existsSync(path.join(dir, implFile)) ? path.join(dir, implFile) : "";
  const oldHeader = headerPath && existsSync(headerPath) ? await fs.readFile(headerPath, "utf8") : "";
  const newHeader = headerName ? files.find((f) => f.name === headerName)?.content ?? "" : "";

  /* 入口文件 **永不覆盖**（见下），但"工程里那份已经和当前样板不同步"这件事得让用户知道 ——
     否则画布改了样板，老工程永远拿不到，而且一点提示都没有。只比对、只提醒，绝不自动写。
     换行符先归一：在编辑器里存过一手的入口文件会被转成 CRLF，那不算"用户改过"。 */
  const newMain = files.find((f) => f.name === mainFile)?.content ?? "";
  const staleMainC = Boolean(oldMain) && !handwritten && normEol(oldMain) !== normEol(newMain);

  /* 语言专属文件里不属于当前语言的那些（另一套 `<base>_ui_events.*` / `main.*`）
     一个字节都不写 —— 判据收在 skeleton-lang.mjs，tools\check-skeleton.mjs 锁着它的行为。 */
  const langSkip = new Set(langWriteList(prefix, effLang, files.map((f) => f.name)).skip);

  const written = [];
  const skipped = [];
  /** 这次为新绑的事件补上的空函数（渲染端会把它念给用户听） */
  const addedCallbacks = [];
  try {
    await fs.mkdir(dir, { recursive: true });
    for (const f of files) {
      const target = path.join(dir, f.name);
      /* 另一种语言的源文件：整个跳过。它是"这个目录对应哪门语言"的最后一道闸 ——
       * 渲染端偶尔会先按默认语言生成一版 files 再送过来，光靠"那边已经商量好"不够保险。 */
      if (langSkip.has(f.name)) {
        skipped.push(f.name);
        continue;
      }
      if (handwritten && !f.name.endsWith(".cgui")) {
        skipped.push(f.name);
        continue;
      }
      /* 入口文件已是用户的资产：**已存在就不覆盖**。仓库里的示例是手写程序
       * （业务与回调就在 main.c 里），覆盖它等于把回调清空（2026-10-07 踩过：darkterm 按钮全部失效）。
       * 单文件发布也不依赖它 —— 界面是编译期作为 PE 资源编进 exe 的。 */
      if (f.name === mainFile && existsSync(target)) {
        skipped.push(f.name);
        continue;
      }
      /* <工程>_ui_events.{c,cpp} 不再整份跳过，改成"**只补不改**"：
       * 缺的回调补桩 —— 不补的话 .h 里已经声明、入口的 BIND 表也引用了它，
       * 链接会直接 undefined reference；已有的原样留着，永不删用户的代码。
       * 合并结果与老文件完全一致时**连文件都不写**：mtime 不动，才不会触发无谓的重新编译。 */
      if (f.name === implFile && existsSync(target)) {
        const merged = mergeEvents(await fs.readFile(target, "utf8"), f.content);
        if (merged.mode === "same") {
          skipped.push(f.name);
          continue;
        }
        await fs.writeFile(target, merged.content, "utf8");
        written.push(f.name);
        addedCallbacks.push(...merged.added);
        if (merged.added.length) log("exportC", `补桩 ${merged.added.length} 个：${merged.added.join(", ")}`);
        /* 想补却切不出整块 = 内部不变量被破坏（当前调用路径不可能发生，它拿到的永远是生成器产物）。
           这时文件没动、上面已把它记进 skipped（用户在导出结果里能看到"跳过了这个文件"），
           这里补一条日志把名字留下，否则真出了事只能看到"跳过一个文件"而无从下手。 */
        if (merged.failed.length) log("exportC", `[warn] 补桩失败（整块切不出来）：${merged.failed.join(", ")}`);
        continue;
      }
      await fs.writeFile(target, f.content, "utf8");
      written.push(f.name);
    }
  } catch (e) {
    dialog.showErrorBox("导出失败", String(e));
    return { canceled: true };
  }

  /* 导出会写一份 .cgui 到工程目录，画布侧随后也把"当前文件"指到它 ——
     监视器跟着换过去，顺手把主进程的 currentFile 也补上（原先导出后它还是旧值，
     `app:info` 报出去的是个过期路径）。 */
  const cguiFile = files.find((f) => f.name.endsWith(".cgui"));
  if (cguiFile) {
    const cguiPath = path.join(dir, cguiFile.name);
    currentFile = cguiPath;
    watchStart(cguiPath, cguiFile.content);
  }

  /* 顺手认一下项目根：用户选在 <root>\apps\xxx 里的话，这里就能自动识别并记住，
   * 下次导出就不用再找目录了，界面上也能给出准确的编译命令。 */
  if (!isProjectRoot(projectRootCache)) {
    const detected = findProjectRootUpwards(dir);
    if (detected) await rememberProjectRoot(detected);
  }

  /* 画布上**已不再绑定**、而 .c 里还留着的回调：**永不自动删**（那些函数里可能有逻辑），
     只提醒一句让用户自己决定。判据是"老 .h 声明过、新 .h 里没有了"——
     拿 .c 里的函数名去比会把手写的辅助函数也算进来（它们从来不是回调）。 */
  const unusedCallbacks = cPath && existsSync(cPath) ? callbackDiff(oldHeader, newHeader) : [];
  if (unusedCallbacks.length) log("exportC", `已不再绑定但保留的实现：${unusedCallbacks.join(", ")}`);

  return {
    canceled: false,
    dir,
    written,
    skipped,
    projectRoot: isProjectRoot(projectRootCache) ? projectRootCache : "",
    appName: baseName,
    /* 渲染端拿它决定那句提示该说什么文件名（`${base}_ui_events.c` 还是 `.cpp`） */
    lang: effLang,
    addedCallbacks,
    unusedCallbacks,
    staleMainC,
  };
});

ipcMain.handle("app:reveal", (_e, target) => {
  if (target && existsSync(target)) shell.showItemInFolder(target);
});

/* 打开回调所在的源文件（画布「事件」页签里那个「代码」按钮）。
 *
 * 新手闭环最容易断在这里：绑完事件、导出骨架之后要"切到编辑器里找到那个空函数"。
 * 这里从 .cgui 路径推出工程目录 → 找 <名字>_ui_events.{c,cpp}（哪个在就开哪个）→
 * 搜索函数名所在行 → 优先用 VS Code 的 `code -g 文件:行号` 直接跳过去，
 * 没装 VS Code 就用默认程序打开。 */
ipcMain.handle("cgui:openCallback", async (_e, { filePath, callback }) => {
  const target = filePath || currentFile;
  if (!target) {
    return { ok: false, message: "先「导出 C 骨架」，画布才知道源文件在哪" };
  }
  const dir = path.dirname(target);
  const base = path.basename(target).replace(/\.cgui$/i, "");
  const implName = langOnDisk(dir, base) === "cpp" ? `${base}_ui_events.cpp` : `${base}_ui_events.c`;
  const cfile = path.join(dir, implName);
  if (!existsSync(cfile)) {
    return { ok: false, message: `还没生成 ${implName}，先点工具条的「导出 C 骨架」` };
  }

  let line = 1;
  if (callback) {
    try {
      const lines = readFileSync(cfile, "utf8").split(/\r?\n/);
      const idx = lines.findIndex((l) => l.includes(`${callback}(`));
      if (idx >= 0) line = idx + 1;
    } catch {
      /* 读不到就只打开文件，不跳行 */
    }
  }

  /* 有 VS Code 就跳行；没有（spawn 失败或退出码非 0）就退回默认程序打开整个文件 */
  const jumped = await new Promise((resolve) => {
    try {
      const p = spawn("code", ["-g", `${cfile}:${line}`], { shell: true, stdio: "ignore" });
      p.on("error", () => resolve(false));
      p.on("exit", (code) => resolve(code === 0));
    } catch {
      resolve(false);
    }
  });
  if (!jumped) {
    const err = await shell.openPath(cfile);
    if (err) return { ok: false, message: `打不开 ${cfile}：${err}` };
    return { ok: true, message: `已用默认程序打开 ${implName}` };
  }
  return { ok: true, message: `已打开 ${implName}${line > 1 ? ` 第 ${line} 行` : ""}` };
});

/** 选窗口图标（.ico）。尽量给出相对 .cgui 目录的路径（工程整体搬走也不失效）；
 *  工程目录之外的图标就拷一份进 <工程>\assets\，这样 .rc 编译和发布包都找得到它。 */
ipcMain.handle("app:pickIcon", async () => {
  const res = await dialog.showOpenDialog(win, {
    title: "选择窗口图标（.ico）",
    filters: [{ name: "图标文件", extensions: ["ico"] }],
    properties: ["openFile"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true };
  const picked = res.filePaths[0];
  if (!currentFile) return { canceled: false, path: picked };

  const dir = path.dirname(path.resolve(currentFile));
  const relRaw = path.relative(dir, picked);
  if (relRaw && !relRaw.startsWith("..") && !path.isAbsolute(relRaw)) {
    return { canceled: false, path: picked, rel: relRaw.split(path.sep).join("\\") };
  }
  try {
    const assets = path.join(dir, "assets");
    await fs.mkdir(assets, { recursive: true });
    const dest = path.join(assets, path.basename(picked));
    await fs.copyFile(picked, dest);
    return { canceled: false, path: dest, rel: `assets\\${path.basename(picked)}` };
  } catch {
    return { canceled: false, path: picked };
  }
});

/** 选控件背景图（按钮/图片控件用，png/jpg/bmp/gif）。相对路径与 assets 拷贝逻辑同 pickIcon：
 *  工程内的图直接给相对路径；工程外的拷一份进 <工程>\assets\，工程整体搬走也不失效。 */
/** 读一张图给画布预览（`button`/`iconbutton`/`image` 的 src、窗口图标）。
 *
 *  路径按**当前 .cgui 所在目录**解析：这与运行时 `cgw_resolve_asset()`（`cg_win_util.c:241`）
 *  的第二顺位、以及 `pickImage` 写 `rel` 的基准（下面那个 handler）三处一致。
 *  绝对路径原样读。CWD 只作兜底 —— 这工程里相对路径的契约就是"相对 .cgui"。
 *
 *  返回 dataURL 而不是文件路径：dev 模式的页面是 http://localhost:5273，读不了 file:// 的图
 *  （生产是 file:// 页面，两种模式都得能跑，所以走 IPC 而不是直接给 <img src>）。
 */
const READ_IMAGE_MAX = 4 * 1024 * 1024; /* 4 MB 上限：base64 还要再涨 1/3，别拿一张大图把画布拖死 */
const IMAGE_MIME = {
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".bmp": "image/bmp",
  ".gif": "image/gif",
  ".ico": "image/x-icon",
};

ipcMain.handle("cgui:readImage", async (_e, { src }) => {
  const raw = String(src || "").trim();
  if (!raw) return { ok: false };
  const base = currentFile ? path.dirname(path.resolve(currentFile)) : "";
  const candidates = path.isAbsolute(raw) ? [raw] : [base ? path.join(base, raw) : "", path.resolve(raw)].filter(Boolean);
  for (const p of candidates) {
    try {
      const st = await fs.stat(p);
      if (!st.isFile()) continue;
      /* 太大：连读都不读（base64 之后还要再涨 1/3，别拿一张大图把画布拖死）。
         画布那边把 tooBig 与"读不到"合并成同一条提示（少一个状态、少一串分支），
         所以这里返回的 tooBig 目前只用于日志排查，界面上看不出区别。 */
      if (st.size > READ_IMAGE_MAX) {
        log("readImage", `太大不预览（${st.size} 字节）：${p}`);
        return { ok: false, tooBig: true, path: p };
      }
      const buf = await fs.readFile(p);
      const mime = IMAGE_MIME[path.extname(p).toLowerCase()] || "application/octet-stream";
      return { ok: true, path: p, dataUrl: `data:${mime};base64,${buf.toString("base64")}` };
    } catch {
      /* 这个候选不存在：试下一个 */
    }
  }
  return { ok: false };
});

ipcMain.handle("cgui:pickImage", async () => {
  const res = await dialog.showOpenDialog(win, {
    title: "选择图片（png / jpg / bmp / gif）",
    filters: [{ name: "图片文件", extensions: ["png", "jpg", "jpeg", "bmp", "gif"] }],
    properties: ["openFile"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true };
  const picked = res.filePaths[0];
  if (!currentFile) return { canceled: false, path: picked };

  const dir = path.dirname(path.resolve(currentFile));
  const relRaw = path.relative(dir, picked);
  if (relRaw && !relRaw.startsWith("..") && !path.isAbsolute(relRaw)) {
    return { canceled: false, path: picked, rel: relRaw.split(path.sep).join("\\") };
  }
  try {
    const assets = path.join(dir, "assets");
    await fs.mkdir(assets, { recursive: true });
    const dest = path.join(assets, path.basename(picked));
    await fs.copyFile(picked, dest);
    return { canceled: false, path: dest, rel: `assets\\${path.basename(picked)}` };
  } catch {
    return { canceled: false, path: picked };
  }
});

/* ------------------------------------------------------------ 一键运行
 * F5：编译当前工程并把 exe 跑起来。
 * 前提（渲染进程负责先保存）：
 *   1. .cgui 已经在磁盘上（有路径）
 *   2. 工程在项目根的 apps\ 或 examples\ 下（build-app.bat 只认这两处）
 *   3. 工程目录里有 main.c 或 main.cpp（导出过骨架；C++ 工程是后者）
 */
let runBusy = false;

/** 工程名只允许安全字符：它要拼进 cmd 命令行 */
const safeName = (s) => (s || "").replace(/[^0-9a-zA-Z_\-]/g, "");

/** MinGW-w64 下载页（没有编译环境时引导用户去这里拿） */
const MINGW_URL = "https://pan.quark.cn/s/f7f5f4791017";

/** 编译环境检测：MINGW 环境变量 / 默认安装位置 / PATH，三处任一找到 gcc 即可。
 *  build-app.bat 就是按这个顺序找编译器的，这里保持同一套规则。 */
function hasMinGW() {
  const mingwDir = process.env.MINGW;
  if (mingwDir && existsSync(path.join(mingwDir, "gcc.exe"))) return Promise.resolve(true);
  if (existsSync("C:\\mingw64\\bin\\gcc.exe")) return Promise.resolve(true);
  return new Promise((resolve) => {
    const child = spawn("where.exe", ["gcc"], { stdio: "ignore" });
    child.on("exit", (code) => resolve(code === 0));
    child.on("error", () => resolve(false));
  });
}

/** 没有 MinGW-w64：弹窗引导下载（主按钮直接打开网盘链接） */
async function failNoMinGW() {
  const res = await dialog.showMessageBox(win, {
    type: "warning",
    title: "缺少 MinGW-w64",
    message: "没有检测到 MinGW-w64 编译环境（gcc）",
    detail:
      "CStackGUI 编译 C 程序需要 MinGW-w64。\n\n" +
      "点「打开下载页」获取：\n" + MINGW_URL + "\n\n" +
      "装好后把 <mingw64>\\bin 加入 PATH（或设置 MINGW 环境变量），再重启画布。",
    buttons: ["打开下载页", "取消"],
    defaultId: 0,
  });
  if (res.response === 0) await shell.openExternal(MINGW_URL);
  return { ok: false, message: `缺少 MinGW-w64 编译环境，请从这里下载：${MINGW_URL}` };
}

ipcMain.handle("run:buildAndLaunch", async (_e, { filePath }) => {
  const fail = (message) => {
    dialog.showErrorBox("跑不起来", message);
    return { ok: false, message };
  };

  if (runBusy) return fail("上一次编译还没结束，等一下再点。");
  if (!filePath || !existsSync(filePath)) {
    return fail("先把 .cgui 保存到磁盘（Ctrl+S）再运行。");
  }
  if (!(await hasMinGW())) return failNoMinGW();

  /* 便携版画布常被放在 release\ 里编辑"真项目"的 .cgui。
   * 文件本身就躺在某个项目根的 apps\ 或 examples\ 下时，以文件位置向上找到的根为准，
   * PORTABLE_EXECUTABLE_DIR / 配置里的根只作兜底 —— 否则会错把 release\ 当项目根，
   * 报"工程要放在 apps\ 下"（实际上就在项目里，只是认错了根）。 */
  const appDir = path.dirname(path.resolve(filePath));
  const fileRoot = findProjectRootUpwards(appDir);
  if (fileRoot && fileRoot !== projectRootCache) await rememberProjectRoot(fileRoot);
  let root = fileRoot || (await resolveProjectRoot());
  if (!root) {
    return fail("还没找到 CStackGUI 项目根。点工具条上的「定位项目根」，\n选含 runtime\\、apps\\ 的那一层目录。");
  }

  /* 工程定位走环境变量 CG_APP_DIR（绝对路径），不进 cmd 命令行——
   * 路径里的引号经 node→cmd 转义会变成 \"...\"，%~1 剥不干净。
   * 根必须是带 runtime 的那份：相对根不可达（空/越界/在别的盘）时，
   * 就地把工程父目录自举成工作区（便携单文件最常见）。 */
  const rel = path.relative(root, appDir);
  if (!rel || rel.startsWith("..") || path.isAbsolute(rel)) {
    const candidate = path.dirname(appDir);
    const usable =
      candidate !== root &&
      (existsSync(path.join(candidate, "build-app.bat")) || (await ensureWorkspace(candidate)));
    if (!usable) {
      return fail(`工程目录不在项目根 ${root} 里，无法编译。\n当前文件在：${appDir}`);
    }
    root = candidate;
  }
  const name = path.basename(appDir);
  /* C 工程是 main.c、C++ 工程是 main.cpp —— 两个都没有才是"还没导出过骨架" */
  if (!existsSync(path.join(appDir, "main.c")) && !existsSync(path.join(appDir, "main.cpp"))) {
    return fail(`${appDir} 里没有 main.c / main.cpp。\n先点「导出 C 骨架」生成代码，再运行。`);
  }


  runBusy = true;
  send("run:output", { kind: "line", text: `$ cgbuild ${name}` });

  /* 编译就在本进程里做（cgbuild.mjs，住在 asar 内）。
   * 以前这里 spawn 的是 `cmd /d /s /c "chcp 65001 >nul & build-app.bat"`：
   * chcp 与批处理挤在同一个 cmd 进程里，脚本自带的重入逻辑不生效，
   * 在部分机器上中文行会被从多字节字符中间切断、后半段被当成命令执行。
   * 现在：不经 cmd / bat / 外部内核；客户不需要 Node（用的是画布自带的运行时），
   * 也不需要旁边放任何文件 —— 编译资源是内嵌进 exe 的。
   * build() 是异步的，所以编译期间界面不卡。 */
  const exitCode = await build({
    root,
    project: appDir,
    onLine: (text) => send("run:output", { kind: "line", text }),
  });
  runBusy = false;

  if (exitCode !== 0) {
    send("run:output", { kind: "fail", code: exitCode });
    return fail(`编译失败（退出码 ${exitCode}），输出在画布下方的运行面板里。`);
  }

  const exePath = path.join(appDir, `${name}.exe`);
  /* 编译退出码为 0 不代表产物一定在（比如工程目录里没有入口源文件时链接的
   * 是别的目录）。spawn 一个不存在的 exe 会抛未捕获异常弹窗，先拦下来。 */
  if (!existsSync(exePath)) {
    const msg = `编译没有生成 ${exePath}。\n请确认工程目录里有 main.c / main.cpp（点「导出 C 骨架」生成），目录里没有多余的旧产物。`;
    send("run:output", { kind: "fail", code: exitCode });
    return fail(msg);
  }
  /* detached：画布退出后程序照跑 */
  spawn(exePath, [], { detached: true, stdio: "ignore", cwd: appDir }).unref();
  send("run:output", { kind: "done", text: exePath });
  return { ok: true, message: `已启动 ${name}.exe`, exePath };
});

ipcMain.handle("app:setTitle", (_e, title) => {
  if (win && !win.isDestroyed()) win.setTitle(title ? `CStackGUI — ${title}` : "CStackGUI");
});

/* 发布：把当前工程编译成 release 版并打成自包含 zip，交给其他用户即可双击运行。
 * 产物：<项目根>\publish\<工程名>.zip（内含 exe / .cgui / assets / README.txt）。 */
ipcMain.handle("publish:project", async (_e, { filePath }) => {
  const fail = (message) => {
    dialog.showErrorBox("发布失败", message);
    return { ok: false, message };
  };

  if (runBusy) return fail("上一次编译还没结束，等一下再点。");
  if (!filePath || !existsSync(filePath)) {
    return fail("先把 .cgui 保存到磁盘（Ctrl+S）再发布。");
  }
  if (!(await hasMinGW())) return failNoMinGW();

  /* 与运行按钮同一套前置检查（找根 / 工程名 / 目录位置 / main.c） */
  const appDir = path.dirname(path.resolve(filePath));
  const fileRoot = findProjectRootUpwards(appDir);
  if (fileRoot && fileRoot !== projectRootCache) await rememberProjectRoot(fileRoot);
  let root = fileRoot || (await resolveProjectRoot());
  if (!root) {
    return fail("还没找到 CStackGUI 项目根。点工具条上的「定位项目根」，\n选含 runtime\\、apps\\ 的那一层目录。");
  }
  /* 与运行入口同一套规则：工程可以是根下任意子目录（便携工作区不强制 apps\examples） */
  /* 与运行入口同一套规则：CG_APP_DIR 环境变量传工程目录，根不可达就自举 */
  const rel = path.relative(root, appDir);
  if (!rel || rel.startsWith("..") || path.isAbsolute(rel)) {
    const candidate = path.dirname(appDir);
    const usable =
      candidate !== root &&
      (existsSync(path.join(candidate, "build-app.bat")) || (await ensureWorkspace(candidate)));
    if (!usable) {
      return fail(`工程目录不在项目根 ${root} 里，无法编译。\n当前文件在：${appDir}`);
    }
    root = candidate;
  }
  const name = path.basename(appDir);
  if (!existsSync(path.join(appDir, "main.c")) && !existsSync(path.join(appDir, "main.cpp"))) {
    return fail(`${appDir} 里没有 main.c / main.cpp。\n先点「导出 C 骨架」生成代码，再发布。`);
  }

  runBusy = true;
  send("run:output", { kind: "line", text: `$ 发布编译 ${name}（release，编译在 CStackGUI 内部完成）` });

  /* 1) release 编译（-mwindows：无控制台窗口，适合分发）。
   *    走和 F5 同一条内部编译（cgbuild.mjs），不经 cmd / bat。 */
  const exitCode = await build({
    root,
    project: appDir,
    release: true,
    onLine: (text) => send("run:output", { kind: "line", text }),
  });
  runBusy = false;

  if (exitCode !== 0) {
    send("run:output", { kind: "fail", code: exitCode });
    return fail(`发布编译失败（退出码 ${exitCode}），输出在画布下方的运行面板里。`);
  }
  const exePath = path.join(appDir, `${name}.exe`);
  if (!existsSync(exePath)) {
    return fail(`编译没有生成 ${exePath}，请确认工程目录里有 main.c / main.cpp。`);
  }

  /* 2) 发布产物就是**工程目录里那个 exe**（也就是 .cgui 旁边）：
   *    界面在编译时已作为 PE 资源编进 exe（见 cgbuild.mjs），所以不需要再组装
   *    publish\ 目录、也不需要打 zip —— 拿走这一个文件就能跑。
   *    不区分"画布工程 / 手写工程"：资源是编译期加的，跟工程代码无关。 */
  const assetsSrc = path.join(appDir, "assets");
  let message =
    `已发布独立 exe：${exePath}\n` +
    `界面已编进 exe，双击即可运行（不需要 .cgui、不需要任何其他文件）。`;
  if (existsSync(assetsSrc)) {
    message +=
      `\n注意：这个工程用了外挂图片（assets\\），图片是运行时按路径读的，` +
      `要把 assets\\ 和 exe 一起给对方。`;
  }
  message += `\n想改界面：把改好的同名 ${name}.cgui 放到 exe 旁边即可生效，不用重新编译。`;

  const target = exePath;
  send("run:output", { kind: "done", text: target });
  shell.showItemInFolder(target);
  return { ok: true, message, target };
});

/** 关闭前询问未保存的改动 */
ipcMain.handle("app:confirmDiscard", async (_e, message) => {
  const res = await dialog.showMessageBox(win, {
    type: "question",
    buttons: ["丢弃改动", "取消"],
    defaultId: 1,
    cancelId: 1,
    message: message || "当前改动还没保存，确定要丢弃吗？",
  });
  return res.response === 0;
});

/* ------------------------------------------------------------ 生命周期 */

/** 复制文本到系统剪贴板（file:// 下渲染进程的 navigator.clipboard 不保证可用，走主进程最稳） */
ipcMain.handle("app:copyText", (_e, text) => {
  clipboard.writeText(String(text ?? ""));
  return true;
});

Menu.setApplicationMenu(
  Menu.buildFromTemplate([
    {
      label: "文件",
      submenu: [
        { label: "新建", accelerator: "CmdOrCtrl+N", click: () => send("menu:new") },
        { label: "打开 .cgui", accelerator: "CmdOrCtrl+O", click: () => send("menu:open") },
        { label: "保存", accelerator: "CmdOrCtrl+S", click: () => send("menu:save") },
        { label: "另存为", accelerator: "CmdOrCtrl+Shift+S", click: () => send("menu:saveAs") },
        { type: "separator" },
        { label: "导出 C 骨架", accelerator: "CmdOrCtrl+E", click: () => send("menu:exportC") },
        { label: "程序配置", click: () => send("menu:programConfig") },
        { label: "动作", click: () => send("menu:actions") },
        { label: "样式", click: () => send("menu:styles") },
        { type: "separator" },
        { label: "运行（保存并编译启动）", accelerator: "F5", click: () => send("menu:run") },
        { type: "separator" },
        { role: "quit", label: "退出" },
      ],
    },
    {
      label: "编辑",
      submenu: [
        { label: "撤销", accelerator: "CmdOrCtrl+Z", click: () => send("menu:undo") },
        { label: "重做", accelerator: "CmdOrCtrl+Shift+Z", click: () => send("menu:redo") },
        { type: "separator" },
        { label: "复制控件", accelerator: "CmdOrCtrl+D", click: () => send("menu:duplicate") },
        { label: "删除控件", accelerator: "Delete", click: () => send("menu:delete") },
      ],
    },
    {
      label: "视图",
      submenu: [
        { label: "放大", accelerator: "CmdOrCtrl+=", click: () => send("menu:zoomIn") },
        { label: "缩小", accelerator: "CmdOrCtrl+-", click: () => send("menu:zoomOut") },
        { label: "适应窗口", accelerator: "CmdOrCtrl+0", click: () => send("menu:zoomFit") },
        { type: "separator" },
        { role: "toggleDevTools", label: "开发者工具" },
      ],
    },
    {
      label: "帮助",
      submenu: [
        {
          label: "QQ 交流群 151796494（点击复制群号）",
          click: () => send("menu:qqGroup"),
        },
      ],
    },
  ]),
);

/* ------------------------------------------------ 无窗口 CLI：--build
 * CStackGUI.exe --build [工程名|路径] [--release]
 * 发布包里的 build-app.bat 只是转发到这一条。编译逻辑仍然只有一份，
 * 就在 tools\cgbuild.mjs；这里不建窗口、输出直连 stdout、按编译退出码退出，
 * 所以命令行 / bat / CI 拿到的行为与画布 F5 完全一致。 */
const buildCliArgs = (() => {
  const i = process.argv.indexOf("--build");
  return i < 0 ? null : process.argv.slice(i + 1);
})();

/* ------------------------------------------------ 画布行为冒烟：--smoke
 * CStackGUI.exe --smoke（或 electron . --smoke）
 * 起真窗口、加载真渲染层，然后在渲染层里跑一段**自包含**探针（点调色板 / 敲键盘 /
 * 切页签 / 拖预演滑杆），断言打成 [smoke] 行、按退出码汇报。
 *
 * 为什么值得（2026-10-18 路线图 G2）：画布的 UI 接线层此前只有 tsc 与 vite build 把关
 * —— 类型全对、行为全错照样发版。store/命令/键盘/页签/预演五批改动零运行时验证，
 * 这是画布自动化验证从"模型层"（check-canvas-ops，纯逻辑）推到"行为层"的第一块。
 *
 * 探针只碰 DOM 与真实事件（PointerEvent / KeyboardEvent / input），不 import 任何
 * 内部实现 —— 是行为测试，不是白盒测试。选择器全部用有稳定语义的钩子：
 *   .app-window（画布根）/ .widget[data-node-id]（控件）/ .palette-item（调色板）/
 *   .tab（页签）/ .toolbar .sim-slider（预演滑杆）。
 * 入口是 tools\check-canvas-e2e.mjs（先 vite build 再起窗，挂 check-format）。 */
const smokeMode = process.argv.includes("--smoke");

const SMOKE_PROBE = async () => {
  const out = [];
  const t = (name, cond) => out.push({ name, ok: !!cond });
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const $ = (s) => document.querySelector(s);
  const $$ = (s) => [...document.querySelectorAll(s)];
  const widgetCount = () => $$(".app-window .widget[data-node-id]").length;

  /* 真实指针序列（pointerdown + 同位 pointerup）：调色板是 onPointerUp 触发 onAdd 的，
   * 合成 .click() 到不了它。setPointerCapture 对合成指针会抛异常 —— 按 DOM 规范，
   * 监听器里的异常只上报 window.onerror、不冒泡出 dispatchEvent，onAdd 照常触发。 */
  const tap = (el) => {
    const r = el.getBoundingClientRect();
    const o = {
      bubbles: true, cancelable: true, composed: true,
      clientX: r.left + r.width / 2, clientY: r.top + r.height / 2,
      pointerId: 1, isPrimary: true, button: 0, buttons: 1,
    };
    el.dispatchEvent(new PointerEvent("pointerdown", o));
    el.dispatchEvent(new PointerEvent("pointerup", { ...o, buttons: 0 }));
  };
  /* range 滑杆必须走原型上的原生 setter：React 在自己的 value setter 里记了"当前值"，
   * 直接 el.value = x 会被它当成"没变"而吞掉 onChange（react-testing 的经典坑）。 */
  const setRange = (el, v) => {
    const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, "value").set;
    setter.call(el, String(v));
    el.dispatchEvent(new Event("input", { bubbles: true }));
  };
  /* React 受控输入的统一助手：必须走**该元素自己的原型**上的原生 setter ——
   * input 的 value 在 HTMLInputElement.prototype、textarea 在 HTMLTextAreaElement.prototype，
   * 拿错原型的 setter 会抛 Illegal invocation（2026-10-18 实测踩到）。
   * 直接 el.value = x 也会被 React 的 value tracker 当成"没变"而吞掉 onChange。 */
  const setVal = (el, v) => {
    const proto =
      el instanceof window.HTMLTextAreaElement ? window.HTMLTextAreaElement.prototype : window.HTMLInputElement.prototype;
    const setter = Object.getOwnPropertyDescriptor(proto, "value").set;
    setter.call(el, String(v));
    el.dispatchEvent(new Event("input", { bubbles: true }));
  };

  /* 1. 启动基线：画布渲染出来、空文档 0 个控件 */
  t("启动后画布渲染（.app-window 在）", !!$(".app-window"));
  t("空文档画布 0 个控件", widgetCount() === 0);

  /* 2. 点调色板加控件：store.edit → 命令 → DOM（G1 全链路 + useSyncExternalStore） */
  const paletteBtn = (label) =>
    $$(".palette-item").find((b) => b.querySelector(".palette-label")?.textContent.trim() === label);
  const btn = paletteBtn("按钮");
  t("调色板有「按钮」项", !!btn);
  if (btn) {
    tap(btn);
    await sleep(150);
    t("点调色板后画布多出一个控件", widgetCount() === 1);
    t("新控件自动选中（.selected）", $$(".app-window .widget.selected").length === 1);

    /* 3. Ctrl+Z 一步撤销回 0（快照栈 + 键盘接线） */
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
    await sleep(150);
    t("Ctrl+Z 撤销后控件消失", widgetCount() === 0);
  }

  /* 4. 预演开关与滑杆（G3'-UI）：按钮切出两条滑杆，拖动真的改变画布窗口尺寸 */
  const previewBtn = $$(".toolbar button").find((b) => b.textContent.includes("预演"));
  t("工具栏有「预演」按钮", !!previewBtn);
  if (previewBtn) {
    t("预演关时没有滑杆", $$(".toolbar .sim-slider").length === 0);
    previewBtn.click();
    await sleep(150);
    const sliders = $$(".toolbar .sim-slider");
    t("开预演出两条滑杆（宽/高）", sliders.length === 2);
    if (sliders.length === 2) {
      const before = $(".app-window").style.width;
      setRange(sliders[0], 150);
      await sleep(150);
      t("拖宽滑杆后画布窗口变宽", $(".app-window").style.width !== before);
      t("预演读数显示模拟尺寸", /×/.test($(".toolbar .sim-readout")?.textContent ?? ""));
    }
    previewBtn.click(); /* 关回去，免得影响后面的断言 */
    await sleep(100);
  }

  /* 5. 支持库页签 + 选中关键词预置（G4'） */
  if (btn) {
    tap(btn);
    await sleep(150);
    const tab = $$(".tab").find((b) => b.textContent.includes("支持库"));
    t("有「支持库」页签", !!tab);
    if (tab) {
      tab.click();
      await sleep(200);
      const kw = $(".sup-search input");
      t("选中按钮后支持库预置了关键词", !!kw && kw.value.length > 0);
    }
  }

  /* 6. 点表导入对话框（G4）：粘 2 好 1 坏 -> live 预览报数 -> 导入 -> 一步撤销。
   *    这是 G4 唯一没有运行时验证的部分（模型层有 44 项 headless 契约，对话框接线没有）。 */
  {
    const openBtn = $$(".toolbar button").find((b) => b.textContent.includes("导入点表"));
    t("工具栏有「导入点表」按钮", !!openBtn);
    if (openBtn) {
      const base = widgetCount();   /* 相对基线：不依赖前面几段留下的控件数 */
      openBtn.click();
      await sleep(200);
      t("点它弹出导入对话框", !!$(".modal-card"));
      const ta = $(".modal-card textarea");
      t("对话框有粘贴框", !!ta);
      if (ta) {
        /* 第 2 行只有一个字段：命中"至少要有两列"那条契约 */
        setVal(ta, ["启动,button", "坏行", "标签,label"].join("\n"));
        await sleep(200);
        t("live 预览：将导入 2 个（坏行不计入）", /将导入 2 个控件/.test($(".modal-head")?.textContent ?? ""));
        t("坏行逐行报错（问题清单出现）", !!$(".modal-card .act-note.warn"));
        t("坏行原因带行号（第 2 行）", /第 2 行/.test($(".modal-card .act-note.warn")?.textContent ?? ""));
        const go = $$(".modal-foot button").find((b) => b.textContent.includes("导入"));
        t("导入按钮就绪且报数（2 个）", !!go && !go.disabled && /导入 2 个/.test(go.textContent));
        if (go) {
          go.click();
          await sleep(250);
          t("导入后对话框自己关掉", !$(".modal-card"));
          t("导入后画布多出 2 个控件", widgetCount() === base + 2);
          window.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
          await sleep(200);
          t("一步撤销回到基线（批量导入是一步）", widgetCount() === base);
        }
      }
    }
  }

  /* 7. 拖拽移动（画布最核心的编辑交互）：pointerdown 设拖拽状态 -> move 过阈值压一次
     历史 + 直写 DOM 预览 -> pointerup 把整组坐标一次提交。缩放默认 1，屏幕位移 = 文档位移；
     +40/+20 都是 4 的倍数，吸附不会改动结果。 */
  {
    const w = $(".app-window .widget[data-node-id]");
    t("画布上有一个控件可拖", !!w);
    if (w) {
      const before = { x: parseFloat(w.style.left), y: parseFloat(w.style.top) };
      const r = w.getBoundingClientRect();
      const cx = r.left + r.width / 2;
      const cy = r.top + r.height / 2;
      const down = {
        bubbles: true, cancelable: true, composed: true,
        clientX: cx, clientY: cy, pointerId: 1, isPrimary: true, button: 0, buttons: 1,
      };
      w.dispatchEvent(new PointerEvent("pointerdown", down));
      await sleep(120); /* 等 React setDrag + effect 把 window 监听挂上 */
      window.dispatchEvent(new PointerEvent("pointermove", { ...down, clientX: cx + 40, clientY: cy + 20 }));
      await sleep(120);
      window.dispatchEvent(new PointerEvent("pointerup", { ...down, clientX: cx + 40, clientY: cy + 20, buttons: 0 }));
      await sleep(200);
      const after = { x: parseFloat(w.style.left), y: parseFloat(w.style.top) };
      t("拖拽后控件位移 +40/+20（pointerup 提交进 doc）", after.x === before.x + 40 && after.y === before.y + 20);
      /* 关键一步：拖动中间是**直写 DOM 的预览**，松手才提交进 doc。切换一次无关状态
         （网格开关）强制 React 从 doc 重渲 —— 位置还在，才证明真的提交了，而不是只活
         在预览里。没有这一步，"松手漏提交"的变异能骗过前两条断言（2026-10-18 补）。 */
      $$(".toolbar button").find((b) => b.textContent.includes("网格"))?.click();
      await sleep(150);
      t("重渲后位置不变（提交进了 doc，不是只活在预览里）", parseFloat(w.style.left) === before.x + 40 && parseFloat(w.style.top) === before.y + 20);
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
      await sleep(200);
      t(
        "一步撤销回到拖动前位置（历史压在真正开始拖的那一下）",
        parseFloat(w.style.left) === before.x && parseFloat(w.style.top) === before.y,
      );
    }
  }

  /* 8. 属性编辑链：Inspector 输入框 -> store.edit -> WidgetView 重渲（+ 一步撤销）。
     第 5 段把页签切到了「支持库」，先切回「属性」。 */
  {
    const w = $(".app-window .widget[data-node-id]");
    const propsTab = $$(".tab").find((b) => b.textContent.includes("属性"));
    propsTab?.click();
    await sleep(200);
    const textRow = $$(".insp-row").find((r) => r.querySelector(".insp-label")?.textContent.trim() === "文字");
    const input = textRow?.querySelector("input");
    t("属性面板有「文字」输入框", !!input);
    if (w && input) {
      const oldText = input.value;
      setVal(input, "冒烟测试钮");
      await sleep(150);
      t("改文字立刻反映到画布控件上", w.textContent.includes("冒烟测试钮"));
      t("画布上不再是旧文字", oldText === "" || !w.textContent.includes(oldText));
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
      await sleep(200);
      t("属性编辑一步撤销：文字回到原值", w.textContent.includes(oldText));
    }
  }

  /* 9. 拖放打开 .cgui（文件打开路径的行为层首覆盖）：合成一个带 File 的 drop 事件。
     冒烟此前始终以空文档启动，replaceDoc 这条路径（打开文件 / 载入外部改动）
     零运行时验证。合成可行性：处理器用的是纯浏览器 API
     （dataTransfer.files[0] + file.text()），不碰 electron 桥。 */
  {
    /* 最小合法 .cgui（schema 与 examples/ 下真实文件一致：cgui/app/widgets）。
       故意用与空文档不同的尺寸（640x480）和两个具名控件 —— 断言全部由这份内容决定，
       不引用任何示例文件（示例改了不会误红）。 */
    const cgui = JSON.stringify({
      cgui: 1,
      app: { title: "冒烟拖放", width: 640, height: 480, bg: "#FFFFFF" },
      widgets: [
        { id: "smk_btn", type: "button", rect: { x: 24, y: 24, w: 140, h: 40 }, text: "拖进来的按钮" },
        { id: "smk_lbl", type: "label", rect: { x: 24, y: 80, w: 200, h: 24 }, text: "拖进来的标签" },
      ],
    });
    const dt = new DataTransfer();
    dt.items.add(new File([cgui], "smoke-drop.cgui", { type: "application/json" }));
    window.dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: dt }));
    await sleep(300);
    t("拖放 .cgui 后画布换成文件里的界面（2 个控件）", $$(".app-window .widget[data-node-id]").length === 2);
    const win = $(".app-window");
    t("窗口尺寸用文件里的 640x480", win?.style.width === "640px" && win?.style.height === "480px");
    t("文件里的控件id与文字渲染出来了", !!$(".app-window .widget[data-node-id='smk_btn']") && document.body.textContent.includes("拖进来的按钮"));
    /* replaceDoc 清历史：打开文件后第一次编辑仍可一步撤销（新历史的起点）——
       这条同时钉住"换文档 = 历史归零"的 store 语义在 UI 侧也成立。 */
    const btn2 = $$(".palette-item").find((b) => b.querySelector(".palette-label")?.textContent.trim() === "按钮");
    if (btn2) {
      tap(btn2);
      await sleep(150);
      t("打开文件后能继续加控件", $$(".app-window .widget[data-node-id]").length === 3);
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true, cancelable: true }));
      await sleep(200);
      t("replaceDoc 后历史干净：加一个撤一个", $$(".app-window .widget[data-node-id]").length === 2);
    }
  }

  return out;
};

async function runCanvasSmoke() {
  const done = (code) => app.exit(code);
  const wc = win?.webContents;
  if (!wc) { console.log("[smoke] FAIL 窗口没创建"); done(1); return; }
  /* 等真 UI（prod 先画启动页再切 dist/index.html；最多 30s） */
  const deadline = Date.now() + 30000;
  for (;;) {
    if (!win || win.isDestroyed()) { console.log("[smoke] FAIL 窗口在探针前被关闭"); done(1); return; }
    let ready = false;
    try { ready = await wc.executeJavaScript(`!!document.querySelector(".app-window")`); } catch { /* 页面切换中，继续等 */ }
    if (ready) break;
    if (Date.now() > deadline) { console.log("[smoke] FAIL 真 UI 30 秒内没就绪"); done(1); return; }
    await new Promise((r) => setTimeout(r, 200));
  }
  let rows;
  try {
    /* 用字符串 IIFE 而不是直接把函数交给 executeJavaScript：本环境（Electron 38 +
     * 软件 GL）对函数参数走结构化克隆会抛 "An object could not be cloned"（2026-10-18
     * 实测）。toString + 求值等价且不依赖参数克隆；探针返回的仍是纯数据数组。 */
    rows = await wc.executeJavaScript(`(${SMOKE_PROBE.toString()})()`);
  } catch (e) {
    console.log("[smoke] FAIL 探针在渲染层抛异常：" + String(e));
    done(1);
    return;
  }
  let pass = 0;
  for (const r of rows ?? []) {
    if (r.ok) { pass += 1; console.log(`[smoke] ok   ${r.name}`); }
    else console.log(`[smoke] FAIL ${r.name}`);
  }
  console.log(`[smoke] ${pass}/${(rows ?? []).length} 通过`);
  done((rows ?? []).length > 0 && pass === (rows ?? []).length ? 0 : 1);
}

app.whenReady().then(async () => {
  log("app.whenReady 触发（Chromium 已就绪）");
  if (buildCliArgs) {
    /* 编译发生在 exe 内部（cgbuild.mjs 在 asar 里），用的是 exe 自带的运行时，
     * 所以命令行这一条同样不需要客户装 Node，也不需要旁边有内核文件。 */
    const root = path.dirname(process.execPath);
    const code = await build({
      root,
      project: buildCliArgs.find((a) => !a.startsWith("--")),
      release: buildCliArgs.includes("--release"),
      runtimeOnly: buildCliArgs.includes("--runtime-only"),
      onLine: (s) => process.stdout.write(`${s}\n`),
    });
    app.exit(code);
    return;
  }
  createWindow();
  if (smokeMode) void runCanvasSmoke();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
