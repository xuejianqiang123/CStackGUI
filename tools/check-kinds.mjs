/* check-kinds.mjs -- 控件类型表双向同步契约（k_kinds <-> KINDS）
 *
 * 背景：运行时的类型表（runtime/src/core/cg_doc_kind.c 的 k_kinds）与画布的登记表
 * （designer/src/model/kinds.ts 的 KINDS）是"同一事实的两份手写"。此前只有注释约定
 * （kinds.ts:26 "顺序与分组要和 k_kinds 保持一致"）没有机器断言 —— 2026-10-17 架构
 * 评审把它列为"唯一无锁的跨端契约"：
 *   - 画布有、运行时没有的类型：拖出来保存后运行时解析直接失败，报错离根因很远；
 *   - 运行时有、画布没有的类型：调色板拖不出来、属性面板没编辑器，而且没有任何门禁会红。
 * 本脚本锁两条：① 类型名集合双向一致；② 共有类型的"容器位"一致
 * （k_kinds 第 3 列 0/1 ↔ KINDS 的 container: true/false —— 运行时按它决定要不要建子树，
 * 画布按它决定要不要渲染容器编辑器，两边不一致 = 画得出、装不进）。
 *
 * 事件面不在这里管：KIND_EVENTS <-> CG_EV_* 掩码由 tools/check-examples.mjs 已锁。
 * 默认尺寸（w/h）与分组（group）**故意不比**：画布调色板默认尺寸与运行时默认尺寸
 * 是两个口径（实测 switch 56x32 vs 160x32），分组更是纯画布概念。
 *
 * 用法：node tools/check-kinds.mjs
 * 退出码：0 = 两表一致；1 = 有漂移（逐条给出方向与差集）
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");
const kindCPath = path.join(root, "runtime", "src", "core", "cg_doc_kind.c");
const kindsTsPath = path.join(root, "designer", "src", "model", "kinds.ts");

if (!fs.existsSync(kindsTsPath) || !fs.existsSync(kindCPath)) {
  console.log("[skip] kinds sync check (missing cg_doc_kind.c / kinds.ts)");
  process.exit(0);
}

/* 1. 运行时 k_kinds：表体 = `k_kinds[] = {` 起到第一个顶格 `};` 止。
 *    每行形如 { "button", CG_K_BUTTON, 0, 120, 40, "按钮", ... } ——
 *    取类型名与第 3 列（容器位 0/1）。注释行不会命中（必须以 { 开头）。 */
const kindC = fs.readFileSync(kindCPath, "utf8");
const tblAt = kindC.indexOf("k_kinds[] = {");
if (tblAt < 0) {
  console.error("[fail] cg_doc_kind.c 里找不到 k_kinds[] 定义（改名了？这条门禁要跟着改）");
  process.exit(1);
}
const tblEnd = kindC.indexOf("\n};", tblAt);
const tblBody = kindC.slice(tblAt, tblEnd < 0 ? kindC.length : tblEnd);
const runtimeRows = [...tblBody.matchAll(/^\s*\{\s*"([a-z0-9_]+)"\s*,\s*CG_K_[A-Z0-9_]+\s*,\s*([01])/gm)].map((m) => ({
  name: m[1],
  container: m[2] === "1",
}));

/* 2. 画布 KINDS：每条形如 { type: "button", label: ..., container: false, ... } */
const kindsTs = fs.readFileSync(kindsTsPath, "utf8");
const designerRows = [...kindsTs.matchAll(/\{\s*type:\s*"([a-z0-9_]+)"[^{}]*?container:\s*(true|false)/g)].map((m) => ({
  name: m[1],
  container: m[2] === "true",
}));

/* 3. 解析 sanity：一张表空了（正则没匹配上）却"全绿"是最恶性的假通过 */
const problems = [];
if (runtimeRows.length < 20) {
  problems.push(`cg_doc_kind.c 只解析出 ${runtimeRows.length} 行（应 >= 20）—— 表格式变了，修本脚本的正则`);
}
if (designerRows.length < 20) {
  problems.push(`kinds.ts 只解析出 ${designerRows.length} 行（应 >= 20）—— 表格式变了，修本脚本的正则`);
}

/* 4. 双向差集。
 *    有意不在调色板的伪类型走白名单：window = 窗口自身（.cgui 的 app 节点），
 *    画布上不作为控件存在，所以 KINDS 里没有它 —— 这是设计不是漂移。
 *    白名单本身也受检：哪天画布真收录了它，这里会红着提醒把白名单删干净。 */
const PALETTE_EXCLUDED = new Set(["window"]);

const rt = new Map(runtimeRows.map((r) => [r.name, r]));
const ds = new Map(designerRows.map((r) => [r.name, r]));
for (const name of rt.keys()) {
  if (PALETTE_EXCLUDED.has(name)) {
    if (ds.has(name)) problems.push(`白名单过期：window 已被 KINDS 收录，请从 PALETTE_EXCLUDED 删除`);
    continue;
  }
  if (!ds.has(name)) problems.push(`运行时有、画布没有：k_kinds 的 "${name}" 在 kinds.ts 的 KINDS 里缺一行`);
}
for (const name of ds.keys()) {
  if (!rt.has(name)) problems.push(`画布有、运行时没有：KINDS 的 "${name}" 在 k_kinds 里缺一行（运行时会解析失败）`);
}

/* 5. 容器位一致 */
for (const [name, r] of rt) {
  const d = ds.get(name);
  if (d && d.container !== r.container) {
    problems.push(
      `容器位不一致："${name}" 运行时 k_kinds=${r.container ? 1 : 0}、画布 KINDS container=${d.container} —— ` +
        `两边必须同真同假，否则画得出、装不进`,
    );
  }
}

if (problems.length) {
  console.error(`[fail] 控件类型表漂移（${problems.length} 处；运行时 ${rt.size} 种 / 画布 ${ds.size} 种）`);
  for (const p of problems) console.error(`  - ${p}`);
  process.exit(1);
}
console.log(`[ok] 控件类型表双向一致：${rt.size} 种（含容器位），k_kinds <-> KINDS`);
