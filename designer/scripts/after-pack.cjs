/**
 * electron-builder afterPack 钩子：把 CStackGUI.exe 这个名字让给原生启动器
 *
 * 为什么（2026-10-07）：便携版每次启动都要自解压。打包器那张启动图**到解包结束就撤**
 * （electron-builder 的 portable.nsi 把 BgImage::Destroy 写在 ExecWait 之前），之后
 * Electron 还要约 2.7 秒才加载到 main.mjs —— 那 2.7 秒里没有任何属于本程序的进程在跑，
 * 屏幕上什么都没有，应用代码也补不了。
 *
 * 做法：真程序改名 CStackGUI-app.exe，把 CStackGUI.exe 这个名字让给一个 20KB 的原生
 * 启动器（packaging/splash/splash.c）。它能在解包结束那一刻就画出启动图，盖到真窗口
 * 出现为止。解包器执行的就是 `$INSTDIR\CStackGUI.exe`（见 app-builder-lib 的
 * templates/nsis/common.nsh: `!define APP_EXECUTABLE_FILENAME "${PRODUCT_FILENAME}.exe"`），
 * 所以这么做**不需要改 electron-builder 的任何模板**，升级它也不会失效。
 *
 * 与 packaging/splash/splash.c 的约定（改一处必须改另一处）：
 *   - 真程序叫 CStackGUI-app.exe，与启动器同目录
 *   - 启动器旁边要有 splash.bmp（没有就退化成品牌底色）
 */

const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "..", "..");
const LAUNCHER_SRC = path.join(ROOT, "packaging", "splash", "splash.c");
const LAUNCHER_EXE = path.join(ROOT, "packaging", "splash", "splash.exe");
const SPLASH_BMP = path.join(ROOT, "designer", "splash.bmp");

/** electron-builder 便携版模板：解包器的启动图窗口由它控制 */
const PORTABLE_NSI = path.join(
  __dirname, "..", "node_modules", "app-builder-lib", "templates", "nsis", "portable.nsi",
);

/**
 * @brief 让解包器在解包期间保留启动图窗口（改 electron-builder 的便携版模板）
 *
 * 模板的 Section 第一件事是 `HideWindow` —— 把画着启动图的那个窗口在解包一开始就藏掉，
 * 而解包本身要 2～4 秒（写 283MB）。2026-10-07 实测：屏幕上 1.76s～4.2s 全空，用户看到
 * 的就是"启动图提前关了，窗口半天不出来"。去掉这一行，启动图就盖满整个解包期，
 * 后面再交给我们那个原生启动器，最后交回画布窗口 —— 全程无缝。
 *
 * 为什么要在这儿改：electron-builder 只给了 nsis 目标的 `script` 口子，
 * `PortableOptions` 没有（它继承的 CommonNsisOptions 里没有 script）。
 * 所以每次打包都就地重打一次补丁 —— 重装 node_modules 后下次构建自动恢复，不用手工修。
 * @return void 模板结构对不上时抛异常，宁可打包失败也不要悄悄产出一个会露白的包
 */
function patchPortableSplash() {
  if (!fs.existsSync(PORTABLE_NSI)) {
    throw new Error(`[afterPack] 找不到 ${PORTABLE_NSI}，无法保证解包期不露白`);
  }
  const src = fs.readFileSync(PORTABLE_NSI, "utf8");
  if (!src.includes("HideWindow")) {
    return; // 已经打过（本次或上次构建打的）
  }
  const re = /^([ \t]*)HideWindow([ \t]*)\r?$/m;
  if (!re.test(src)) {
    throw new Error(
      `[afterPack] ${PORTABLE_NSI} 里有 HideWindow 但位置/写法和预期不符` +
        `（electron-builder 升级过？）—— 解包期会露白 2～4 秒，先对一下模板再打包`,
    );
  }
  const eol = src.includes("\r\n") ? "\r\n" : "\n";
  const patched = src.replace(
    re,
    [
      "$1; CStackGUI 补丁：这里原本是 HideWindow，会在解包一开始就把画着启动图的窗口藏掉。",
      "$1; 解包要 2～4 秒，藏掉就是满屏空白 —— 改成保留窗口，由 designer/scripts/after-pack.cjs 每次构建自动打。",
      "$1; 别手工删这段：删掉 HideWindow 之后这个注释块就是补丁生效的标记。",
    ].join(eol),
  );
  fs.writeFileSync(PORTABLE_NSI, patched, "utf8");
  console.log("[afterPack] 已让 portable.nsi 在解包期保留启动图窗口（消除 2～4 秒空白）");
}

/** 入口名（= electron-builder 的 PRODUCT_FILENAME + .exe，也是解包器执行的那个） */
const ENTRY_NAME = "CStackGUI.exe";
/** 改名后的真程序：Electron 主体，上百 MB */
const REAL_NAME = "CStackGUI-app.exe";
/** 真程序至少这么大；启动器只有 20KB —— 用体积区分"改名跑过没有"，比删了重来安全 */
const REAL_MIN_BYTES = 1024 * 1024;

/**
 * @brief 编译原生启动器（改了 splash.c 必须重编，否则静默打进旧启动器）
 * @return void gcc 不在 PATH 时抛异常，由调用方决定是否放过
 */
function buildLauncher() {
  execFileSync(
    "gcc",
    ["-std=c11", "-O2", "-s", "-mwindows", "-Wall", "-Wextra",
     "-o", LAUNCHER_EXE, LAUNCHER_SRC, "-lgdi32", "-luser32"],
    { stdio: "inherit" },
  );
}

exports.default = async function afterPack(context) {
  if (context.electronPlatformName !== "win32") {
    return;
  }
  const appDir = context.appOutDir;
  const entry = path.join(appDir, ENTRY_NAME);
  const real = path.join(appDir, REAL_NAME);

  /* 解包器那张启动图会被模板藏掉 —— 先把它救回来（便携版空白段的真正来源） */
  patchPortableSplash();

  try {
    buildLauncher();
  } catch (err) {
    /* 没有 gcc 也不能把包打坏：仓库里若已有编好的启动器就照用它 */
    if (!fs.existsSync(LAUNCHER_EXE)) {
      throw new Error(
        `编译 ${LAUNCHER_SRC} 失败，且仓库里没有现成的 splash.exe。\n` +
          `需要 MinGW-w64 的 gcc 在 PATH 里（本工程编译运行时库本来就要它）。\n` +
          `底层错误：${err.message}`,
      );
    }
    console.warn(`[afterPack] 找不到 gcc，改用已有的 splash.exe：${err.message}`);
  }

  if (!fs.existsSync(entry)) {
    throw new Error(`[afterPack] ${entry} 不存在 —— 打包目录结构和预期不符`);
  }

  /* 幂等：Electron 主体有上百 MB，启动器只有 20KB，体积一比就知道改名跑过没有 */
  if (fs.statSync(entry).size >= REAL_MIN_BYTES) {
    fs.rmSync(real, { force: true });
    fs.renameSync(entry, real);
  }

  fs.copyFileSync(LAUNCHER_EXE, entry);
  fs.copyFileSync(SPLASH_BMP, path.join(appDir, "splash.bmp"));
  console.log(
    `[afterPack] ${ENTRY_NAME} = 启动器（${fs.statSync(entry).size} B），` +
      `真程序 = ${REAL_NAME}（${fs.statSync(real).size} B）`,
  );
};
