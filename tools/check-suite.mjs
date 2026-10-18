/* tools\check-suite.mjs -- 套件工程的静态契约（合并后的新风险，见 examples-consolidation.md §六）
 *
 * 套件是"N 个原工程合并成一个多页签工程"，合并期最容易在这里漏：
 *   ① .cgui 的 events 值（回调名）在 C 侧没有 CG_BIND 登记 —— 运行时才报"未登记回调"，
 *      而自检要跑起来才知道；这里静态就拦住。
 *   ② 反向死绑定：模块里登记了 CG_BIND 但 .cgui 没有用到 —— 白占命名空间，
 *      日后无人知道该不该删。
 *   ③ 控件 id 在套件内撞名 —— 合并后互相覆盖（原各工程独立，无此约束）。
 *   ④ id 前缀与模块不符 —— 规则单一才可机械核对（examples-consolidation.md §五）。
 *
 * 用法：node tools\check-suite.mjs            扫 examples\suites\ 下全部套件
 *       node tools\check-suite.mjs --suite dev    只查 dev
 * 退出码 0 = 全部契约成立；1 = 有违反（打印具体条目）。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const suitesDir = path.join(root, "examples", "suites");
const args = process.argv.slice(2);
const only = args.includes("--suite") ? args[args.indexOf("--suite") + 1] : null;

if (!fs.existsSync(suitesDir)) {
  console.log("[skip] examples\\suites\\ 不存在（套件合并尚未开始）");
  process.exit(0);
}

/** 套件骨架控件的前缀（合并脚本自己创建的 tabs 页签容器等）——它不属于任何工具模块，
 *  ⑥ 的"前缀必须对得上模块"对它豁免。 */
const SKELETON_PREFIX = "sv";

let fails = 0;
const fail = (msg) => { fails += 1; console.error(`  [fail] ${msg}`); };

const suites = fs
  .readdirSync(suitesDir, { withFileTypes: true })
  .filter((d) => d.isDirectory() && (!only || d.name === only))
  .map((d) => d.name);

if (!suites.length) {
  console.error(`[fail] 没找到套件${only ? ` "${only}"` : ""}`);
  process.exit(1);
}

for (const s of suites) {
  const dir = path.join(suitesDir, s);
  const cgui = path.join(dir, `${s}.cgui`);
  if (!fs.existsSync(cgui)) { fail(`${s}: 缺 ${s}.cgui`); continue; }

  const doc = JSON.parse(fs.readFileSync(cgui, "utf8"));

  // ① 收集 .cgui 里的全部回调名（events 值）
  const used = new Set();
  const actRefs = new Set();   /* 动作引用（指向 actions 表的键，不是回调名） */
  const ids = [];
  (function walk(ns) {
    for (const n of ns ?? []) {
      if (n.id) ids.push(n.id);
      if (n.events) for (const v of Object.values(n.events)) if (v) used.add(String(v));
      if (doc.app && doc.app.events) for (const v of Object.values(doc.app.events)) if (v) used.add(String(v));
      /* action 的 handler 也是回调名（要进双向核对：漏了就查不出"handler 没改前缀"
       * 这类合并事故 —— 2026-10-18 text 套件首跑前自查发现的盲区） */
      if (doc.actions) for (const v of Object.values(doc.actions)) if (v && v.handler) used.add(String(v.handler));
      /* 动作引用（控件的 action 字段、菜单条目 nodes[].action）指向动作表的键 ——
       * 单独记一份核对存在性（漏改名就是运行时"菜单引用了不存在的动作"） */
      if (typeof n.action === "string" && n.action) actRefs.add(n.action);
      walk(n.children);
      walk(n.nodes);
    }
  })(doc.widgets);

  // ② 收集 modules\ 下全部 CG_BIND 登记名
  const modDir = path.join(dir, "modules");
  const registered = new Set();
  const modFiles = fs.existsSync(modDir) ? fs.readdirSync(modDir).filter((f) => f.endsWith(".c")) : [];
  for (const f of modFiles) {
    const src = fs.readFileSync(path.join(modDir, f), "utf8");
    for (const m of src.matchAll(/CG_BIND\("([^"]+)"/g)) registered.add(m[1]);
  }
  /* 入口 main.c 也有绑定表：窗口事件的 suite_<事件> 分发器在这里注册（模块的窗口 handler
   * 走直连转发不进绑定表，分发器才按名被 .cgui 的 app.events 找到）。不收进来的话
   * "suite_close 未登记"会被误报。 */
  for (const f of fs.readdirSync(dir).filter((x) => x.endsWith(".c"))) {
    const src = fs.readFileSync(path.join(dir, f), "utf8");
    for (const m of src.matchAll(/CG_BIND\("([^"]+)"/g)) registered.add(m[1]);
  }

  // ③ .cgui 用到的回调必须都有登记
  for (const name of used) {
    if (!registered.has(name)) fail(`${s}: .cgui 用的回调 "${name}" 在 modules\\ 里没有 CG_BIND 登记`);
  }
  // ④ 反向：登记了但没人用 = 死绑定
  for (const name of registered) {
    if (!used.has(name)) fail(`${s}: 模块登记了 "${name}" 但 .cgui 没用到（死绑定）`);
  }
  /* 动作引用必须在动作表里（菜单/控件引用已改名的动作键时漏一个就是运行时报错） */
  for (const ref of actRefs) {
    if (!Object.hasOwn(doc.actions ?? {}, ref)) fail(`${s}: 引用了不存在的动作 "${ref}"（actions 表里没有）`);
  }
  // ⑤ 套件内 id 唯一
  const seen = new Set();
  for (const id of ids) {
    if (seen.has(id)) fail(`${s}: 控件 id "${id}" 在套件内重复（合并后会互相覆盖）`);
    seen.add(id);
  }
  // ⑥ id 前缀与模块对得上（模块文件名 = 前缀，如 modules/ht.c -> id 以 ht_ 开头；
  //    套件骨架 sv_ 豁免）。统计行先打，fail 明细后打（先看总账再看哪条破了）。
  console.log(
    `[ok]   ${s}: 控件 ${ids.length}、回调 ${used.size}、绑定 ${registered.size}、模块 ${modFiles.length}`,
  );
  const prefixes = new Set(modFiles.map((f) => path.basename(f, ".c")));
  for (const id of ids) {
    const pre = id.includes("_") ? id.slice(0, id.indexOf("_")) : "";
    if (pre && pre !== SKELETON_PREFIX && !prefixes.has(pre)) {
      fail(`${s}: 控件 id "${id}" 的前缀 "${pre}" 不对应任何模块（${[...prefixes].join("/")}）`);
    }
  }
}

if (fails) {
  console.error(`[fail] 套件契约被破坏：${fails} 处`);
  process.exit(1);
}
console.log(`[ok] 套件契约成立：${suites.length} 个（事件双向一致 / id 唯一且前缀对得上模块）`);
