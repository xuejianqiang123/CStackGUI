/**
 * @file    check-plans.mjs
 * @brief   计划生命周期判据：新计划必须自报 status，历史计划走基线豁免
 * @details 为什么要有它：
 *
 *   `sop` §6 说"换会话续跑先读 `.codebuddy/plans/<feature>.md`"。但目录里有 40 多份计划，
 *   其中相当一部分早就做完了 —— 读到一份 2026-10 的历史计划，会把已完成的工作当待办重做，
 *   而**没有任何东西会因此变红**。2026-11-01 实测：42 份里只有 17 份（40%）有进度段。
 *
 *   为什么不给 42 份历史文件回填 status：
 *     "这份到底做完了没"只能靠人读，机械回填等于造假；而按名读取时误读概率本来就不高。
 *   所以采用**基线快照**（与 `check-header.mjs` 的公共 API 基线同款）：
 *     - `tools/fixtures/plans-baseline.json` 记下判据生效那天已存在的文件，一律豁免；
 *     - 此后**新加的**计划必须在开头声明 status，否则红。
 *   这样判据上线当天不会红 42 条，也不会因为"历史文件没改"而永远失效。
 *
 *   判据只有一条，且不猜内容：开头 12 行内出现 `> 状态：active|done|superseded`。
 *   不校验"active 的是不是真的在做" —— 那是读文档的人的判断，不是脚本能替的。
 *
 * 用法：node tools\check-plans.mjs
 * 退出码：0 = 合规；1 = 有新计划没自报 status
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PLANS = path.join(root, ".codebuddy", "plans");
const BASE = path.join(root, "tools", "fixtures", "plans-baseline.json");

/* `> 状态：active` / `> 状态: `done`` / 带 ｜ 分隔的整行都认 */
const STATUS_RE = /^>\s*状态[：:]\s*`?(active|done|superseded)/m;

if (!fs.existsSync(PLANS)) {
  console.log("[ok] 没有 .codebuddy/plans/，跳过");
  process.exit(0);
}

let known = new Set();
if (fs.existsSync(BASE)) {
  try {
    known = new Set(JSON.parse(fs.readFileSync(BASE, "utf8")).files || []);
  } catch (e) {
    console.log(`[fail] tools/fixtures/plans-baseline.json 解析失败：${e.message}`);
    process.exit(1);
  }
} else {
  console.log(`[warn] 缺基线 tools/fixtures/plans-baseline.json —— 本轮把所有计划都当历史豁免`);
}

const bad = [];
let fresh = 0;
let legacy = 0;

for (const e of fs.readdirSync(PLANS, { withFileTypes: true })) {
  if (!e.isFile() || !e.name.endsWith(".md")) continue;
  if (known.has(e.name)) {
    legacy++;
    continue;
  }
  fresh++;
  const head = fs
    .readFileSync(path.join(PLANS, e.name), "utf8")
    .split(/\r?\n/)
    .slice(0, 12)
    .join("\n");
  if (!STATUS_RE.test(head)) {
    bad.push(
      `[fail] plans/${e.name}：新计划没自报生命周期\n` +
        `    在文件开头 12 行内加一行（三选一）：\n` +
        `      > 状态：active      ← 正在做\n` +
        `      > 状态：done        ← 已收敛，可归档\n` +
        `      > 状态：superseded  ← 被后来的计划取代`
    );
  }
}

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 计划生命周期：${bad.length} 份新计划缺 status（历史豁免 ${legacy} 份）`);
  process.exit(1);
}

console.log(
  `[ok] 计划生命周期：新增 ${fresh} 份均自报 status，历史 ${legacy} 份走基线豁免`
);
