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
const args = process.argv.slice(2);
const only = args.includes("--suite") ? args[args.indexOf("--suite") + 1] : null;

/** 套件骨架控件的前缀（合并脚本自己创建的 tabs 页签容器等）——它不属于任何工具模块，
 *  ⑥ 的"前缀必须对得上模块"对它豁免。 */
const SKELETON_PREFIX = "sv";

let fails = 0;
const fail = (msg) => { fails += 1; console.error(`  [fail] ${msg}`); };

/** 套件工程目录：4 个工具套件在 examples\suites\，教学六课套件在 examples\lessons\lessons\。
 *  六课并成套件后同样是"多页签 + modules\"结构，同一套契约照管（事件双向一致 / id 唯一且前缀对得上模块）。 */
const suiteDirs = [];
const suitesRoot = path.join(root, "examples", "suites");
if (fs.existsSync(suitesRoot)) {
  for (const d of fs.readdirSync(suitesRoot, { withFileTypes: true })) {
    if (d.isDirectory()) suiteDirs.push({ name: d.name, dir: path.join(suitesRoot, d.name) });
  }
}
const lessonsSuite = path.join(root, "examples", "lessons", "lessons");
if (fs.existsSync(path.join(lessonsSuite, "lessons.cgui"))) {
  suiteDirs.push({ name: "lessons", dir: lessonsSuite });
}

const suites = suiteDirs.filter((s) => !only || s.name === only);

if (!suites.length) {
  console.error(`[fail] 没找到套件${only ? ` "${only}"` : ""}`);
  process.exit(1);
}

for (const { name: s, dir } of suites) {
  const cgui = path.join(dir, `${s}.cgui`);
  if (!fs.existsSync(cgui)) { fail(`${s}: 缺 ${s}.cgui`); continue; }

  const doc = JSON.parse(fs.readFileSync(cgui, "utf8"));

  // ① 收集 .cgui 里的全部回调名（events 值）
  const used = new Set();
  const actRefs = new Set();   /* 动作引用（指向 actions 表的键，不是回调名） */
  const ids = [];              /* 控件 id：全局唯一（⑤）+ 前缀对得上模块（⑥） */
  const menuIds = [];          /* 菜单项 id（nodes[] 下）：**按菜单控件作用域**解析
                                *  （cg_menu_last_id(self) 只在被点的那个菜单内查），
                                *  跨菜单复用同名项是合法的 —— 所以只查前缀（⑥），
                                *  不参与全局唯一（⑤）。2026-10-18 lessons 合并首跑：
                                *  lesson05 的 m_page 同时在菜单栏与菜单按钮里，原工程本就如此，
                                *  门禁误报成"id 重复"，据此把两类 id 分开。 */
  (function walk(ns, inMenu) {
    for (const n of ns ?? []) {
      if (n.id) (inMenu ? menuIds : ids).push(n.id);
      if (n.events) for (const v of Object.values(n.events)) if (v) used.add(String(v));
      if (doc.app && doc.app.events) for (const v of Object.values(doc.app.events)) if (v) used.add(String(v));
      /* action 的 handler 也是回调名（要进双向核对：漏了就查不出"handler 没改前缀"
       * 这类合并事故 —— 2026-10-18 text 套件首跑前自查发现的盲区） */
      if (doc.actions) for (const v of Object.values(doc.actions)) if (v && v.handler) used.add(String(v.handler));
      /* 动作引用（控件的 action 字段、菜单条目 nodes[].action）指向动作表的键 ——
       * 单独记一份核对存在性（漏改名就是运行时"菜单引用了不存在的动作"） */
      if (typeof n.action === "string" && n.action) actRefs.add(n.action);
      walk(n.children, false);
      walk(n.nodes, true);     /* 菜单树挂在 nodes 下，里面的 id 是菜单项 id（作用域不同） */
    }
  })(doc.widgets, false);

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
  const entrySrcs = fs.readdirSync(dir).filter((x) => x.endsWith(".c"));
  for (const f of entrySrcs) {
    const src = fs.readFileSync(path.join(dir, f), "utf8");
    for (const m of src.matchAll(/CG_BIND\("([^"]+)"/g)) registered.add(m[1]);
  }
  /* 残留旧动作名的反向门禁（合并器 4b 的兜底）：C 里 strcmp(name, "save_as") / cg_hotkey_set
   * 这类**按字符串消费动作名**的地方若没改前缀，就是"点菜单没反应 / 热键白注册"的静默失效
   * —— selftest 抓不到（回调照常触发、只是分支不匹配）。动作键改名后，旧名字面量
   * 不该再出现在任何 C 源里。 */
  const allC = [...modFiles.map((f) => path.join(modDir, f)), ...entrySrcs.map((f) => path.join(dir, f))]
    .map((f) => fs.readFileSync(f, "utf8"))
    .join("\n");
  for (const key of Object.keys(doc.actions ?? {})) {
    const orig = key.split("_").slice(1).join("_");   /* 去掉两位模块前缀 = 原动作键 */
    if (!orig) continue;
    if (allC.includes(`"${orig}"`)) {
      fail(`${s}: C 源里残留旧动作名 "${orig}"（动作键已改名 ${key}，这里不改就是点菜单没反应）`);
    }
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
  // ⑥ id 前缀与模块对得上（模块前缀 = 文件名首个下划线前的段，如 modules/ht.c -> ht、
  //    modules/st_main.c -> st；套件骨架 sv_ 豁免）。
  //    取首段而非全名，是为了支持"一个工具拆成多个子模块 + 共享内部头"（st.c -> st_main.c
  //    + st_framer.c，照搬运行时 cg_win_*.c + cg_win_internal.h 的模式）：子模块文件名共享
  //    同一工具前缀 st，id 前缀仍唯一归属该工具 —— "每个 id 归属某个工具"的保证不变。
  //    统计行先打，fail 明细后打（先看总账再看哪条破了）。
  console.log(
    `[ok]   ${s}: 控件 ${ids.length}、回调 ${used.size}、绑定 ${registered.size}、模块 ${modFiles.length}`,
  );
  const prefixes = new Set(
    modFiles.map((f) => {
      const base = path.basename(f, ".c");
      return base.includes("_") ? base.slice(0, base.indexOf("_")) : base;
    }),
  );
  for (const id of [...ids, ...menuIds]) {
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
