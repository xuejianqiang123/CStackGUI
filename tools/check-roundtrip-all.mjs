/* check-roundtrip-all.mjs -- 全部示例的格式契约回归（format-roundtrip 的批量驱动）
 *
 * 为什么从 5 个样本扩到全部（2026-10-18）：examples/ 是"用户最可能照抄的东西"
 * （教学说明原话），而格式门禁此前只盯 gallery / serialterm / lesson01 / lesson06 /
 * netterm 五个 —— 近 30 个工程里另外二十几个的 .cgui 字段理解漂移**没有任何门禁知道**。
 * 新控件 / 新字段先在某个示例里用上，那正是最该被守住的地方。
 *
 * 测法不变（逻辑不复制）：每个样本 spawn 一次 tools/format-roundtrip.mjs，
 * 走完整的 画布读 -> 模拟用户改动 -> 写出 -> C 运行时 --selftest -> 恒等回写核对。
 * 那个脚本仍是唯一真相，这里只做样本收集、并发驱动与汇总。
 *
 * 并发安全性（4 路）：各样本写各自的 build/roundtrip_<组>_<工程>.cgui；
 * --selftest 只往 stdout 与按 exe 名分的日志文件（%LOCALAPPDATA%\<exe名>\app.log）写，
 * 互不冲突（cg_doc_dump 的目的地是 stdout，不是文件 —— 2026-10-18 核实过）。
 *
 * 用法：node tools/check-roundtrip-all.mjs（挂在 check-format.bat 里）
 * 退出码：0 = 全部通过（或没有可跑样本且明确说明）；1 = 有样本被破坏或缺 exe
 */
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");

/* 1. 收样本：examples/ 下有两种布局，都要认——
 *    平铺工程  examples\<工程>\<工程>.cgui（calculator / gallery / serialterm… 17 个）
 *    分组工程  examples\<组>\<工程>\<工程>.cgui（base\lesson01_basics… 12 个）
 *    判据是"目录里有没有与自己同名的 .cgui"：有就是工程，没有就是组（递归一层）。
 *    共享片段目录（如 base\common\）里的 .cgui 不与目录同名，天然不会被收进来
 *    —— 它们不是独立工程，没有 exe，进了也是假故障。 */
const exDir = path.join(root, "examples");
const jobs = [];
for (const e of fs.readdirSync(exDir, { withFileTypes: true })) {
  if (!e.isDirectory()) continue;
  const dir = path.join(exDir, e.name);
  const own = path.join(dir, `${e.name}.cgui`);
  if (fs.existsSync(own)) {
    jobs.push({ name: e.name, cgui: own, exe: path.join(dir, `${e.name}.exe`) });
    continue;
  }
  for (const p of fs.readdirSync(dir, { withFileTypes: true })) {
    if (!p.isDirectory()) continue;
    const cgui = path.join(dir, p.name, `${p.name}.cgui`);
    if (fs.existsSync(cgui)) {
      jobs.push({ name: `${e.name}/${p.name}`, cgui, exe: path.join(dir, p.name, `${p.name}.exe`) });
    }
  }
}
if (!jobs.length) {
  console.log("[skip] examples/ 下没有找到任何 .cgui 样本");
  process.exit(0);
}

/* 2. 缺 exe 的明确失败（不静默跳过 —— 没有 exe 就没有 C 侧那一半验证，
 *    假绿灯比没有门禁更糟）。同时提一句新旧：exe 比运行时库旧时结果仍可用
 *    （新字段会被旧 exe 拒，那是真阳性），但要知道自己测的是哪一版。 */
const libFile = path.join(root, "build", "lib", "libcstackgui.a");
const libTime = fs.existsSync(libFile) ? fs.statSync(libFile).mtimeMs : 0;
const runnable = [];
const missing = [];
for (const j of jobs) {
  if (!fs.existsSync(j.exe)) {
    missing.push(j.name);
    continue;
  }
  j.stale = libTime > 0 && fs.statSync(j.exe).mtimeMs < libTime;
  runnable.push(j);
}
if (!runnable.length) {
  console.error(`[fail] ${jobs.length} 个示例全都没有 exe —— 先跑 build-all.bat`);
  process.exit(1);
}

/* 3. 并发驱动（4 路）：够了 —— 再多也压不满双核编译机，且每路是一次完整往返。 */
const POOL = 4;
const results = new Array(runnable.length);
let next = 0;
async function worker() {
  for (;;) {
    const i = next++;
    if (i >= runnable.length) return;
    const j = runnable[i];
    const out = path.join(root, "build", `roundtrip_${j.name.replace("/", "_")}.cgui`);
    const r = await new Promise((resolve) => {
      const ch = spawn(
        process.execPath,
        [path.join("tools", "format-roundtrip.mjs"), j.exe, j.cgui, out],
        { cwd: root, stdio: ["ignore", "pipe", "pipe"] },
      );
      let so = "";
      let se = "";
      ch.stdout.on("data", (d) => (so += d));
      ch.stderr.on("data", (d) => (se += d));
      ch.on("close", (code) => resolve({ code, so, se }));
    });
    results[i] = { ...j, ...r };
  }
}
await Promise.all(Array.from({ length: Math.min(POOL, runnable.length) }, worker));

/* 4. 汇总：一行一样本；失败的把它的 [fail] 行打出来（定位到具体字段） */
let fails = 0;
const stale = [];
for (const r of results) {
  if (r.stale) stale.push(r.name);
  if (r.code === 0) {
    console.log(`  [ok]   ${r.name}`);
    continue;
  }
  fails += 1;
  console.log(`  [fail] ${r.name}（exit ${r.code}）`);
  for (const line of `${r.so}\n${r.se}`.split(/\r?\n/)) {
    if (line.includes("[fail]")) console.log(`         ${line.trim()}`);
  }
}
if (stale.length) {
  console.log(`  [note] ${stale.length} 个 exe 比运行时库旧（${stale.slice(0, 6).join("、")}${stale.length > 6 ? " 等" : ""}）—— 跑一遍 build-all.bat 让 C 侧那一半也测到最新`);
}
if (missing.length) {
  console.error(`[fail] ${missing.length} 个示例没有 exe：${missing.join("、")} —— 先跑 build-all.bat`);
  process.exit(1);
}
console.log(`  示例 roundtrip：${results.length - fails}/${results.length} 通过`);
if (fails) {
  console.error(`[fail] ${fails} 个示例的 .cgui 往返被破坏（上面有具体字段）`);
  process.exit(1);
}
console.log("[ok] 全部示例的 .cgui 往返成立（画布能存、C 侧能读、恒等回写不丢）");
