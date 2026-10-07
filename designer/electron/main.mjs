/* main.mjs -- Electron 主进程
 *
 * 画布本身是纯前端（React），这里负责四件浏览器做不到的事：
 *   1. 读写磁盘上的 .cgui（真正的「打开/保存」，不是下载文件）
 *   2. 把生成的 C 骨架落到 apps/<name>/ 里
 *   3. F5 一键运行：直接跑编译内核 tools\cgbuild.mjs，再把 exe 跑起来
 *      （命令行下同一件事：CStackGUI.exe --build <工程>）
 *   4. 打开系统资源管理器定位产物
 */
import { app, BrowserWindow, ipcMain, dialog, shell, Menu } from "electron";
import path from "node:path";
import fs from "node:fs/promises";
import { existsSync, appendFileSync, readFileSync } from "node:fs";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEV_URL = process.env.CSTACKGUI_DEV_URL || "";
const VERSION = "0.1.0";

/* 启动排查探针：设了 CSTACKGUI_LOG=<文件路径> 才写，平时零开销。
 * 打包后的 GUI exe 抓不到 stdout/stderr，白屏、找不到项目根这类问题全靠它。 */
const LOG_FILE = process.env.CSTACKGUI_LOG || "";
function log(...args) {
  if (!LOG_FILE) return;
  try {
    appendFileSync(LOG_FILE, `[${new Date().toISOString()}] ${args.join(" ")}\n`);
  } catch {
    /* 写日志失败绝不能影响启动 */
  }
}

let win = null;
let currentFile = "";

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

function createWindow() {
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
    win.loadURL(DEV_URL);
  } else {
    win.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }

  /* 白屏/启动失败时这几个事件能直接说明是加载失败、preload 炸了，还是前端崩了 */
  const wc = win.webContents;
  wc.on("did-finish-load", () => log("did-finish-load"));
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
    return { canceled: false, path: target };
  } catch (e) {
    dialog.showErrorBox("保存失败", String(e));
    return { canceled: true };
  }
});

ipcMain.handle("cgui:exportC", async (_e, { baseName, files, filePath }) => {
  /* 和 run:buildAndLaunch 同一个道理：从当前文件位置向上找根优先，
   * 避免便携版把 release\ 认成项目根、把骨架导到发布包里去。 */
  const root =
    (filePath ? findProjectRootUpwards(path.dirname(path.resolve(filePath))) : "") || (await resolveProjectRoot());
  const appsDir = root ? path.join(root, "apps", baseName) : "";
  const res = await dialog.showOpenDialog(win, {
    title: root
      ? `导出 C 骨架到 apps/${baseName}（也可以选别的目录）`
      : `导出 C 骨架：请选 CStackGUI 的 apps 目录（找不到项目根，导完还要手动编译）`,
    defaultPath: appsDir || root || undefined,
    properties: ["openDirectory", "createDirectory"],
  });
  if (res.canceled || !res.filePaths.length) return { canceled: true };

  const chosen = res.filePaths[0];
  // 用户可能选了 apps 这一层，或者选了自己想放的目录：两种都让结果落在 <chosen>/<baseName>
  const dir = path.basename(chosen).toLowerCase() === baseName.toLowerCase() ? chosen : path.join(chosen, baseName);

  const written = [];
  const skipped = [];
  try {
    await fs.mkdir(dir, { recursive: true });
    for (const f of files) {
      const target = path.join(dir, f.name);
      // 业务代码是用户的资产，已存在就不覆盖（main.c 用户也常加全局变量/逻辑）
      const isUserCode = f.name.endsWith("_ui_events.c") || f.name === "main.c";
      if (isUserCode && existsSync(target)) {
        skipped.push(f.name);
        continue;
      }
      await fs.writeFile(target, f.content, "utf8");
      written.push(f.name);
    }
  } catch (e) {
    dialog.showErrorBox("导出失败", String(e));
    return { canceled: true };
  }

  /* 顺手认一下项目根：用户选在 <root>\apps\xxx 里的话，这里就能自动识别并记住，
   * 下次导出就不用再找目录了，界面上也能给出准确的编译命令。 */
  if (!isProjectRoot(projectRootCache)) {
    const detected = findProjectRootUpwards(dir);
    if (detected) await rememberProjectRoot(detected);
  }

  return {
    canceled: false,
    dir,
    written,
    skipped,
    projectRoot: isProjectRoot(projectRootCache) ? projectRootCache : "",
    appName: baseName,
  };
});

ipcMain.handle("app:reveal", (_e, target) => {
  if (target && existsSync(target)) shell.showItemInFolder(target);
});

/* 打开回调所在的 C 文件（画布「事件」页签里那个「代码」按钮）。
 *
 * 新手闭环最容易断在这里：绑完事件、导出骨架之后要"切到编辑器里找到那个空函数"。
 * 这里从 .cgui 路径推出工程目录 → 找 <名字>_ui_events.c → 搜索函数名所在行 →
 * 优先用 VS Code 的 `code -g 文件:行号` 直接跳过去，没装 VS Code 就用默认程序打开。 */
ipcMain.handle("cgui:openCallback", async (_e, { filePath, callback }) => {
  const target = filePath || currentFile;
  if (!target) {
    return { ok: false, message: "先「导出 C 骨架」，画布才知道 C 文件在哪" };
  }
  const dir = path.dirname(target);
  const base = path.basename(target).replace(/\.cgui$/i, "");
  const cfile = path.join(dir, `${base}_ui_events.c`);
  if (!existsSync(cfile)) {
    return { ok: false, message: `还没生成 ${base}_ui_events.c，先点工具条的「导出 C 骨架」` };
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
    return { ok: true, message: `已用默认程序打开 ${base}_ui_events.c` };
  }
  return { ok: true, message: `已打开 ${base}_ui_events.c${line > 1 ? ` 第 ${line} 行` : ""}` };
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
 *   3. 工程目录里有 main.c（导出过 C 骨架）
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
  if (!existsSync(path.join(appDir, "main.c"))) {
    return fail(`${appDir} 里没有 main.c。\n先点「导出 C 骨架」生成 C 代码，再运行。`);
  }
  /* 编译内核必须在这个根里 —— 便携版指错了根时，给一句能照着做的话，
   * 而不是让 spawn 失败后只显示一个退出码。 */
  if (!existsSync(path.join(root, "tools", "cgbuild.mjs"))) {
    return fail(`项目根 ${root} 里没有编译内核 tools\\cgbuild.mjs。\n用工具条的「定位项目根」选含 runtime\\、tools\\ 的那一层目录。`);
  }

  runBusy = true;
  send("run:output", { kind: "line", text: `$ cgbuild ${name}` });

  /* 直接用画布自带的 Node 跑编译内核 tools\cgbuild.mjs。
   * 以前这里 spawn 的是 `cmd /d /s /c "chcp 65001 >nul & build-app.bat"`：
   * chcp 与批处理挤在同一个 cmd 进程里，脚本自带的重入逻辑不生效，
   * 在部分机器上中文行会被从多字节字符中间切断、后半段被当成命令执行。
   * 走 Node 就没有代码页这一层，工程目录直接走命令行参数（不再用 CG_APP_DIR）。
   * ELECTRON_RUN_AS_NODE=1：process.execPath 是 Electron 本体，不加这个会被
   * 当成"用 electron 打开一个应用"，而不是"用 node 跑一个脚本"。 */
  const kernel = path.join(root, "tools", "cgbuild.mjs");
  let exitCode = -1;
  await new Promise((resolve) => {
    const child = spawn(process.execPath, [kernel, appDir], {
      cwd: root,
      env: { ...process.env, CSTACKGUI_HOME: root, ELECTRON_RUN_AS_NODE: "1" },
    });
    const push = (buf) => {
      for (const line of String(buf).split(/\r?\n/)) {
        if (line.trim()) send("run:output", { kind: "line", text: line });
      }
    };
    child.stdout.on("data", push);
    child.stderr.on("data", push);
    child.on("exit", (code) => { exitCode = code ?? -1; resolve(); });
    child.on("error", (err) => {
      send("run:output", { kind: "line", text: `[run] ${err}` });
      resolve();
    });
  });
  runBusy = false;

  if (exitCode !== 0) {
    send("run:output", { kind: "fail", code: exitCode });
    return fail(`编译失败（退出码 ${exitCode}），输出在画布下方的运行面板里。`);
  }

  const exePath = path.join(appDir, `${name}.exe`);
  /* 编译退出码为 0 不代表产物一定在（比如工程目录里没有 main.c 时链接的
   * 是别的目录）。spawn 一个不存在的 exe 会抛未捕获异常弹窗，先拦下来。 */
  if (!existsSync(exePath)) {
    const msg = `编译没有生成 ${exePath}。\n请确认工程目录里有 main.c（点「导出 C 骨架」生成），目录里没有多余的旧产物。`;
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
  if (!existsSync(path.join(appDir, "main.c"))) {
    return fail(`${appDir} 里没有 main.c。\n先点「导出 C 骨架」生成 C 代码，再发布。`);
  }

  runBusy = true;
  send("run:output", { kind: "line", text: `$ build-app.bat ${name} --release` });

  /* 1) release 编译（-mwindows：无控制台窗口，适合分发） */
  let exitCode = -1;
  await new Promise((resolve) => {
    const child = spawn("cmd.exe", ["/d", "/s", "/c", "chcp 65001 >nul & build-app.bat --release"], {
      cwd: root,
      env: { ...process.env, CG_APP_DIR: appDir },
    });
    const push = (buf) => {
      for (const line of String(buf).split(/\r?\n/)) {
        if (line.trim()) send("run:output", { kind: "line", text: line });
      }
    };
    child.stdout.on("data", push);
    child.stderr.on("data", push);
    child.on("exit", (code) => { exitCode = code ?? -1; resolve(); });
    child.on("error", (err) => {
      send("run:output", { kind: "line", text: `[publish] ${err}` });
      resolve();
    });
  });
  runBusy = false;

  if (exitCode !== 0) {
    send("run:output", { kind: "fail", code: exitCode });
    return fail(`发布编译失败（退出码 ${exitCode}），输出在画布下方的运行面板里。`);
  }
  const exePath = path.join(appDir, `${name}.exe`);
  if (!existsSync(exePath)) {
    return fail(`编译没有生成 ${exePath}，请确认工程目录里有 main.c。`);
  }

  /* 2) 组装自包含发布目录：exe + .cgui + assets + 使用说明 */
  const pubRoot = path.join(root, "publish");
  const pubDir = path.join(pubRoot, name);
  const readme = [
    `${name} —— 由 CStackGUI 制作`,
    "",
    "使用方法：解压后双击 " + name + ".exe 即可运行（无需安装任何运行库）。",
    name + ".cgui 是界面文件，必须和 exe 放在同一目录（解压即已同目录，别单独挪走）。",
    "assets\\ 里是程序用到的图片等资源（如有）。",
    "",
    "想自己改界面：去 CStackGUI 项目（https://github.com/ 上找作者要）用画布打开 .cgui。",
  ].join("\r\n");

  try {
    await fs.rm(pubDir, { recursive: true, force: true });
    await fs.mkdir(pubDir, { recursive: true });
    await fs.copyFile(exePath, path.join(pubDir, `${name}.exe`));
    /* .cgui 必须按**工程名**落一份：运行时默认找的就是 `<exe 名>.cgui`（找不到才去
     * ..\apps\ 与 ..\examples\ 里翻），README 也是这么写给用户的。
     * 早先这里只拷 basename —— 用户「另存为」改过名之后，zip 里那份 .cgui 与 exe 名对不上，
     * 双击 exe 三条查找路全落空，直接报"找不到界面文件"。
     * 原名再补一份：万一工程名与文件名不同，两种都能命中（多一份几百字节，换的是"绝不扑空"）。 */
    await fs.copyFile(filePath, path.join(pubDir, `${name}.cgui`));
    if (path.basename(filePath) !== `${name}.cgui`) {
      await fs.copyFile(filePath, path.join(pubDir, path.basename(filePath)));
    }
    const assetsDir = path.join(appDir, "assets");
    if (existsSync(assetsDir)) await fs.cp(assetsDir, path.join(pubDir, "assets"), { recursive: true });
    await fs.writeFile(path.join(pubDir, "README.txt"), readme, "utf8");
  } catch (err) {
    return fail(`组装发布目录失败：${err}`);
  }

  /* 3) 打 zip（Windows 10+ 自带 bsdtar，-a 按扩展名选 zip） */
  const zipPath = path.join(pubRoot, `${name}.zip`);
  send("run:output", { kind: "line", text: `$ tar -a -c -f ${zipPath}` });
  let zipOk = true;
  await new Promise((resolve) => {
    const child = spawn("tar.exe", ["-a", "-c", "-f", zipPath, "-C", pubRoot, name]);
    child.on("exit", (code) => { zipOk = code === 0; resolve(); });
    child.on("error", () => { zipOk = false; resolve(); });
  });

  const target = zipOk ? zipPath : pubDir;
  send("run:output", { kind: "done", text: target });
  shell.showItemInFolder(target);
  return { ok: true, message: zipOk ? `发布包已生成：${zipPath}` : `发布目录已生成：${pubDir}`, target };
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

app.whenReady().then(() => {
  if (buildCliArgs) {
    /* 打包后 exe 就在发布包根（release\CStackGUI.exe），内核被 make-release.bat
     * 拷到同级的 tools\ 下。开发态（electron .）的 execPath 指向 electron.exe，
     * 那种情况直接 node tools\cgbuild.mjs。 */
    const root = path.dirname(process.execPath);
    const kernel = path.join(root, "tools", "cgbuild.mjs");
    if (!existsSync(kernel)) {
      process.stdout.write(`[fail] build kernel not found: ${kernel}\n`);
      app.exit(1);
      return;
    }
    const r = spawnSync(process.execPath, [kernel, ...buildCliArgs], {
      stdio: "inherit",
      cwd: root,
      env: { ...process.env, CSTACKGUI_HOME: root, ELECTRON_RUN_AS_NODE: "1" },
    });
    app.exit(r.status ?? 1);
    return;
  }
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
