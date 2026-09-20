/* main.mjs -- Electron 主进程
 *
 * 画布本身是纯前端（React），这里负责四件浏览器做不到的事：
 *   1. 读写磁盘上的 .cgui（真正的「打开/保存」，不是下载文件）
 *   2. 把生成的 C 骨架落到 apps/<name>/ 里
 *   3. F5 一键运行：调项目根的 build-app.bat 编译，再把 exe 跑起来
 *   4. 打开系统资源管理器定位产物
 */
import { app, BrowserWindow, ipcMain, dialog, shell, Menu } from "electron";
import path from "node:path";
import fs from "node:fs/promises";
import { existsSync } from "node:fs";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEV_URL = process.env.CSTACKGUI_DEV_URL || "";
const VERSION = "0.1.0";

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
    if (isProjectRoot(dir)) return dir;
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
  }
  return found;
}

async function rememberProjectRoot(dir) {
  if (!isProjectRoot(dir)) return false;
  projectRootCache = dir;
  await saveConfig({ projectRoot: dir });
  return true;
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

ipcMain.handle("app:info", async () => ({
  version: VERSION,
  projectRoot: await resolveProjectRoot(),
  currentFile,
}));

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
  const root = (await rememberProjectRoot(chosen)) ? chosen : findProjectRootUpwards(chosen);
  if (!root) {
    dialog.showErrorBox(
      "这里不是 CStackGUI 项目根",
      `在 "${chosen}" 里没找到 runtime\\include\\cstackgui.h。\n请选择包含 runtime\\ 和 apps\\ 的那一层目录。`,
    );
    return { canceled: true };
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
  const root = fileRoot || (await resolveProjectRoot());
  if (!root) {
    return fail("还没找到 CStackGUI 项目根。点工具条上的「定位项目根」，\n选含 runtime\\、apps\\ 的那一层目录。");
  }

  /* build-app.bat 的参数是「工程目录名」，不是界面文件名：
   * main.cgui 放在 examples\test\ 里时，要编译的是 examples\test，
   * 按「main」找工程会错编 apps\main（同名文件不同目录的工程会互相串）。 */
  const name = safeName(path.basename(path.dirname(path.resolve(filePath))));
  if (!name) {
    return fail(
      `工程目录名要只用英文/数字/下划线（当前目录名无法用作编译参数）。\n` +
        `当前文件在：${appDir}`,
    );
  }
  const under = [path.join(root, "apps"), path.join(root, "examples")]
    .some((base) => appDir.toLowerCase().startsWith(base.toLowerCase() + path.sep));
  if (!under) {
    return fail(
      `工程要放在项目根的 apps\\ 或 examples\\ 下才能编译。\n` +
      `当前文件在：${appDir}\n` +
      `用「导出 C 骨架」把工程放进 ${path.join(root, "apps")} 即可。`,
    );
  }
  if (!existsSync(path.join(appDir, "main.c"))) {
    return fail(`${appDir} 里没有 main.c。\n先点「导出 C 骨架」生成 C 代码，再运行。`);
  }

  runBusy = true;
  send("run:output", { kind: "line", text: `$ build-app.bat ${name}` });

  /* chcp 65001：让批处理里的中文输出走 UTF-8，渲染进程不用再转码 */
  let exitCode = -1;
  await new Promise((resolve) => {
    const child = spawn("cmd.exe", ["/d", "/s", "/c", `chcp 65001 >nul & build-app.bat ${name}`], { cwd: root });
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
  const root = fileRoot || (await resolveProjectRoot());
  if (!root) {
    return fail("还没找到 CStackGUI 项目根。点工具条上的「定位项目根」，\n选含 runtime\\、apps\\ 的那一层目录。");
  }
  const name = safeName(path.basename(path.dirname(path.resolve(filePath))));
  if (!name) {
    return fail(`工程目录名要只用英文/数字/下划线（当前目录名无法用作编译参数）。\n当前文件在：${appDir}`);
  }
  const under = [path.join(root, "apps"), path.join(root, "examples")]
    .some((base) => appDir.toLowerCase().startsWith(base.toLowerCase() + path.sep));
  if (!under) {
    return fail(
      `工程要放在项目根的 apps\\ 或 examples\\ 下才能编译。\n` +
      `当前文件在：${appDir}\n` +
      `用「导出 C 骨架」把工程放进 ${path.join(root, "apps")} 即可。`,
    );
  }
  if (!existsSync(path.join(appDir, "main.c"))) {
    return fail(`${appDir} 里没有 main.c。\n先点「导出 C 骨架」生成 C 代码，再发布。`);
  }

  runBusy = true;
  send("run:output", { kind: "line", text: `$ build-app.bat ${name} --release` });

  /* 1) release 编译（-mwindows：无控制台窗口，适合分发） */
  let exitCode = -1;
  await new Promise((resolve) => {
    const child = spawn("cmd.exe", ["/d", "/s", "/c", `chcp 65001 >nul & build-app.bat ${name} --release`], { cwd: root });
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
    await fs.copyFile(filePath, path.join(pubDir, path.basename(filePath)));
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

app.whenReady().then(() => {
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
