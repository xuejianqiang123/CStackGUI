/* tools\canvas-package-smoke.mjs -- 打包画布的行为自检（发布链的"画布本体"那一关）
 *
 * 为什么单独一关（2026-10-18 补）：make-release.bat 的自检覆盖的是**发布目录**
 * （release\ 里编示例、跑格式门禁与 kit 冒烟），但**打包后的画布本体从没被跑过** ——
 * 两者的资源布局不同（asar 里的 dist/index.html、内嵌 runtime、GUI 子系统），
 * 打包出一个点不开的画布，前面的自检一样全绿。这一关起打包画布、跑它自带的
 * 43 项行为探针（designer/electron/main.mjs 的 --smoke 分支，探针就编在包里）。
 *
 * 两个只有真跑才发现的坑（写进这里，别再踩）：
 *   1. 打包应用是 GUI 子系统：console.log 不进任何控制台，探针明细只能靠
 *      CSTACKGUI_SMOKE_OUT 文件通道拿（main.mjs 的 --smoke 分支支持该变量）。
 *   2. 跑的必须是 win-unpacked\CStackGUI-app.exe（afterPack 把真程序改名了），
 *      不是 CStackGUI.exe —— 后者是 20KB 的启动器。
 *
 * 前置：designer\release\win-unpacked\ 已经打好（make-release.bat 第 2 步 / npm run dist）。
 * 用法：node tools\canvas-package-smoke.mjs
 * 退出码：0 = 探针全过；1 = 打包应用没起来 / 探针有失败（明细会打出来）。
 */
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const appExe = path.join(root, "designer", "release", "win-unpacked", "CStackGUI-app.exe");

if (!fs.existsSync(appExe)) {
  console.error(`[pkg-smoke] FAIL 找不到 ${appExe} —— 先打包（make-release.bat 第 2 步 / npm run dist --prefix designer）`);
  process.exit(1);
}

const outFile = path.join(root, "build", `pkg_smoke_${Date.now()}.txt`);
const r = spawnSync(appExe, ["--smoke"], {
  cwd: root, encoding: "utf8",
  env: { ...process.env, CSTACKGUI_SMOKE_OUT: outFile },
});
const code = r.status ?? 1;

if (!fs.existsSync(outFile)) {
  console.error(`[pkg-smoke] FAIL 探针明细文件没生成（退出码 ${code}）—— 打包应用可能没起来`);
  process.exit(1);
}
const detail = fs.readFileSync(outFile, "utf8").split(/\r?\n/);
/* 摘要行形如 "[smoke] 43/43 通过"：不硬编码项数（探针会长，写死数字每次加断言都要回来改） */
const sum = (detail.filter((l) => l.includes("/")).at(-1) ?? "").match(/\[smoke\] (\d+)\/(\d+)/);
if (code !== 0 || !sum || sum[1] !== sum[2]) {
  for (const l of detail) {
    if (l.includes("FAIL")) console.error(`  ${l}`);
  }
  console.error(`[pkg-smoke] FAIL 打包画布的行为探针没全过（明细：${outFile}）`);
  process.exit(1);
}
console.log(`[pkg-smoke] ok   打包画布 ${sum[1]}/${sum[2]} 行为探针全过`);
process.exit(0);
