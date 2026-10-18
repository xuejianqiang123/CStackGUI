/* check-canvas-e2e.mjs -- 画布行为冒烟门禁（路线图 G2，2026-10-18）
 *
 * 为什么存在：画布的 UI 接线层此前只有 tsc 与 vite build 把关 —— 类型全对、行为全错
 * 照样发版。本脚本起**真 Electron 窗口**，在渲染层里驱动真实事件（点调色板 / 敲键盘 /
 * 切页签 / 拖预演滑杆），把 store/命令/键盘/页签/预演五批改动的行为钉住：
 *   1. 启动渲染（.app-window 出现、空文档 0 控件）        —— G1 store 接回
 *   2. 点调色板 → 多一个控件 + 自动选中                  —— G1 命令 + useSyncExternalStore
 *   3. Ctrl+Z → 控件消失                                —— 快照栈 + 键盘接线
 *   4. 预演按钮 → 双滑杆 → 拖宽滑杆画布窗口真的变宽       —— G3'-UI 接线
 *   5. 支持库页签 → 选中控件预置搜索关键词                —— G4' 接线
 *
 * 探针本体在 designer/electron/main.mjs 的 --smoke 分支（自包含、只碰 DOM 与真实
 * 事件，是行为测试不是白盒测试）；本脚本负责门禁三件事：
 *   - 每次先 vite build（3s）：dist 必须是当前源码的产物，否则冒烟测的是旧代码；
 *   - 起窗、收 [smoke] 行、按退出码汇报；
 *   - designer / node_modules 不在时跳过（与 check-support 同口径：发布包环境无画布源码）。
 *
 * 用法：node tools\check-canvas-e2e.mjs；退出码 0 = 全过，1 = 有失败或起不来窗。
 */
import { execSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");
const designer = path.join(root, "designer");

if (!fs.existsSync(path.join(designer, "package.json"))) {
  console.log("[skip] canvas e2e smoke (no designer sources)");
  process.exit(0);
}
const electronExe = path.join(designer, "node_modules", "electron", "dist", "electron.exe");
if (!fs.existsSync(electronExe)) {
  console.log("[skip] canvas e2e smoke (electron not installed; run npm install in designer/)");
  process.exit(0);
}

/* 1. 重建 dist：冒烟必须测当前源码（tsc/vite 的门禁哲学是"每次现编"，这里对齐） */
try {
  execSync("npx vite build", { cwd: designer, stdio: "inherit", shell: true });
} catch {
  console.error("[fail] vite build 失败（冒烟的前置）");
  process.exit(1);
}

/* 2. 起真窗跑探针。electron.exe 直接起（绕开 npx 解析）；--smoke 分支会自己按
 *    退出码退出。给它 120s：冷启动 ~3s + 启动页切换 + 探针 ~2s，留足余量。 */
let out = "";
try {
  out = execSync(`"${electronExe}" . --smoke`, {
    cwd: designer,
    encoding: "utf8",
    timeout: 120000,
    shell: true,
  });
} catch (e) {
  out = `${String(e.stdout ?? "")}\n${String(e.stderr ?? "")}`;
  if (!out.includes("[smoke]")) {
    console.error("[fail] 画布冒烟没跑起来（electron 起不来 / 超时 / 探针没输出）");
    console.error(out.split("\n").slice(-20).join("\n"));
    process.exit(1);
  }
}

const rows = out
  .split(/\r?\n/)
  .filter((l) => l.includes("[smoke]"))
  .map((l) => l.trim());
let fails = 0;
for (const l of rows) {
  console.log(`  ${l}`);
  if (l.includes("[smoke] FAIL")) fails += 1;
}
const summary = rows.find((l) => l.includes("通过"));
if (fails > 0 || !summary) {
  console.error(`[fail] 画布行为冒烟：${fails} 项失败（上面有 [smoke] FAIL 行）`);
  process.exit(1);
}
console.log(`[ok] 画布行为冒烟：${summary.replace("[smoke] ", "")}`);
