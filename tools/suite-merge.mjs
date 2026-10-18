/* tools\suite-merge.mjs -- 把 N 个示例工程合并成一个多页签"套件"工程（.cgui 侧）
 *
 * 为什么用脚本而不是手工拼 JSON（2026-10-18 立项）：合并要在 4 个套件里重复 4 次，
 * 而且**改名规则必须单一可核对**——手工拼必然出现"这个改了那个漏了"。
 * 本脚本是合并的单一真相：输入清单（套件定义）+ 前缀表，输出合并后的 .cgui。
 *
 * 命名契约（见 .codebuddy/plans/examples-consolidation.md §五）：
 *   - 控件 id、事件名、样式名、角色名、动作名**一律全量加前缀**（两位小写缩写），
 *     不只改撞名的 —— 规则单一才可机械核对；"只改撞名的"会让 id 规则分裂。
 *   - bind.src（控件绑定源）、action 引用、颜色字段的 @角色 引用同步改。
 *
 * C 侧不需要动函数名：各工具的回调都是 static（编译单元隔离，实测 5/5 工具零个
 * 非 static 函数），只需把 CG_BIND 的字符串名改成带前缀（与本脚本产出的 events 一致）、
 * 把 k_bindings 导出成 <tool>_bindings。
 *
 * 用法：
 *   node tools\suite-merge.mjs --suite dev --out examples\suites\dev\dev.cgui
 *   （套件定义在下方 SUITES 表里；--dry 只报告不写盘）
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** 套件定义：名字 → [ { tool, prefix } ]。tool 是原工程名，prefix 是两位缩写。 */
const SUITES = {
  dev: [
    { tool: "palette", prefix: "pl" },
    { tool: "drawpad", prefix: "dp" },
    { tool: "hextool", prefix: "ht" },
    { tool: "encconv", prefix: "ec" },
    { tool: "calculator", prefix: "cl" },
  ],
  text: [
    { tool: "notepad", prefix: "np" },
    { tool: "logfilter", prefix: "lf" },
    { tool: "filecheck", prefix: "fc" },
    { tool: "dataview", prefix: "dv" },
    { tool: "htmlreport", prefix: "hr" },
    { tool: "datarec", prefix: "dr" },
  ],
};

const args = process.argv.slice(2);
const suiteName = args[args.indexOf("--suite") + 1];
const outFile = args[args.indexOf("--out") + 1];
const dry = args.includes("--dry");
const suite = SUITES[suiteName];
if (!suite || !outFile) {
  console.error(`用法: node tools\\suite-merge.mjs --suite <${Object.keys(SUITES).join("|")}> --out <路径> [--dry]`);
  process.exit(2);
}

/** 原工程路径（两种布局都认：examples\\<t>\\ 与 examples\\base\\<t>\\） */
function toolDir(tool) {
  const flat = path.join(root, "examples", tool);
  if (fs.existsSync(path.join(flat, `${tool}.cgui`))) return flat;
  const grouped = path.join(root, "examples", "base", tool);
  if (fs.existsSync(path.join(grouped, `${tool}.cgui`))) return grouped;
  throw new Error(`找不到原工程 ${tool}`);
}

/** 递归给一个节点里的所有命名引用加前缀 */
function rename(node, pre) {
  if (!node || typeof node !== "object") return;
  if (Array.isArray(node)) {
    for (const it of node) rename(it, pre);
    return;
  }
  if (typeof node.id === "string" && node.id) node.id = pre + "_" + node.id;
  if (node.events && typeof node.events === "object") {
    for (const k of Object.keys(node.events)) {
      const v = node.events[k];
      if (typeof v === "string" && v) node.events[k] = pre + "_" + v;
    }
  }
  /* 样式引用：字符串（纯引用）或 { use: 名, ...覆盖 } */
  if (typeof node.style === "string" && node.style) node.style = pre + "_" + node.style;
  else if (node.style && typeof node.style === "object" && node.style.use) {
    node.style.use = pre + "_" + node.style.use;
  }
  if (node.bind && typeof node.bind === "object" && node.bind.src) node.bind.src = pre + "_" + node.bind.src;
  if (typeof node.action === "string" && node.action) node.action = pre + "_" + node.action;
  /* 颜色字段里的 @角色引用（角色名本身也要加前缀，见下方对 roles 表的处理） */
  for (const k of ["fg", "bg", "color"]) {
    if (typeof node[k] === "string" && node[k].startsWith("@")) node[k] = "@" + pre + "_" + node[k].slice(1);
  }
  if (node.style && typeof node.style === "object") {
    for (const k of ["fg", "bg"]) {
      if (typeof node.style[k] === "string" && node.style[k].startsWith("@")) {
        node.style[k] = "@" + pre + "_" + node.style[k].slice(1);
      }
    }
  }
  rename(node.children, pre);
  /* 菜单树：menu 控件的条目挂在 **nodes** 下（不是 children），条目里的 action 引用
   * 动作键、条目 id 用于菜单点击分流 —— 都要跟着改名。漏了它就是"菜单引用了不存在的
   * 动作"（2026-10-18 text 套件首跑抓到：notepad 的 4 个菜单项指向已改名的动作表）。 */
  rename(node.nodes, pre);
}

/* ---------------------------------------------------------------- 读入并合并 */
let widgets = [];
const styles = {};
const roles = {};
const actions = {};
const report = [];
let maxW = 800;
let maxH = 600;

suite.forEach(({ tool, prefix }, page) => {
  const dir = toolDir(tool);
  const doc = JSON.parse(fs.readFileSync(path.join(dir, `${tool}.cgui`), "utf8"));
  const app = doc.app || {};

  /* app.events（窗口级事件）：**不按工具前缀改名，而是统一指向 main 的分发器** `suite_<事件>`。
   * 一个窗口只能接一个回调名（运行时把 app.events.close 存进 root->ev[close] 一个槽），
   * 若按工具前缀改名再"首工具胜出"，其余工具的窗口职责就被静默吞掉（notepad 的未保存
   * 拦截、filecheck 的写窗口尺寸、lf/hr 的收尾记账全没了 —— check-suite 的死绑定会红）。
   * 正解：main 里一个 suite_<事件> 分发器逐个调各模块的 <pre>_app_<event> 直连转发。 */
  const appEvents = {};
  if (app.events) for (const k of Object.keys(app.events)) appEvents[k] = `suite_${k}`;

  rename(doc.widgets, prefix);
  for (const w of doc.widgets || []) {
    w.page = page;          /* 搬进 tabs 的第 N 页 */
    widgets.push(w);
  }
  if (doc.styles) for (const [k, v] of Object.entries(doc.styles)) styles[`${prefix}_${k}`] = v;
  if (doc.roles) for (const [k, v] of Object.entries(doc.roles)) roles[`${prefix}_${k}`] = v;
  /* actions 表：键加前缀（避免撞名），**handler 也要加前缀** —— 它是回调名，
   * C 侧的 CG_BIND 已经加了前缀，不改就对不上（动作出错，自检 exit 4）。
   * dev 套件 0 个 action 所以没暴露；text 套件的 notepad 有 4 个（save_as 与 save
   * 共用一个 handler，双双改名后仍共用）。浅拷贝，别改到原对象。 */
  if (doc.actions) {
    for (const [k, v] of Object.entries(doc.actions)) {
      const out = { ...v };
      if (typeof out.handler === "string" && out.handler && !out.handler.startsWith(`${prefix}_`)) {
        out.handler = `${prefix}_${out.handler}`;
      }
      actions[`${prefix}_${k}`] = out;
    }
  }

  maxW = Math.max(maxW, app.width || 800);
  maxH = Math.max(maxH, app.height || 600);
  report.push({
    tool, prefix, page,
    widgets: (doc.widgets || []).length,
    events: Object.keys(appEvents).length,
    styles: doc.styles ? Object.keys(doc.styles).length : 0,
    roles: doc.roles ? Object.keys(doc.roles).length : 0,
    actions: doc.actions ? Object.keys(doc.actions).length : 0,
    appEvents,
  });
});

/* 窗口事件：多工具都有时要小心——合并后只留一份，同名事件用第一个工具的
   （各工具的窗口事件语义不同：计算器可能没有，画板有 close。这里只合并非空的）。 */
const mergedAppEvents = {};
for (const r of report) for (const [k, v] of Object.entries(r.appEvents)) if (!(k in mergedAppEvents)) mergedAppEvents[k] = v;

const merged = {
  cgui: 1,
  app: {
    title: `${suiteName} 套件`,
    width: maxW,
    height: maxH,
    bg: "#F3EDF7",
    info: {
      product: `CStackGUI ${suiteName} 套件`,
      desc: `原 ${suite.map((t) => t.tool).join(" / ")} 合并（每工具一页签）`,
      comments: "由 tools\\suite-merge.mjs 合并生成",
      version: "1.0.0.0",
      author: "CStackGUI",
    },
  },
  widgets: [
    {
      id: "sv_pages",
      type: "tabs",
      rect: { x: 6, y: 6, w: maxW - 12, h: maxH - 12 },
      items: suite.map((t) => t.tool),
      children: widgets,
    },
  ],
};
if (Object.keys(mergedAppEvents).length) merged.app.events = mergedAppEvents;
if (Object.keys(styles).length) merged.styles = styles;
if (Object.keys(roles).length) merged.roles = roles;
if (Object.keys(actions).length) merged.actions = actions;

/* ---------------------------------------------------------------- 报告 */
let tot = 0;
for (const r of report) {
  tot += r.widgets;
  console.log(
    `  ${r.tool.padEnd(11)} page=${r.page} 前缀=${r.prefix} 控件=${String(r.widgets).padStart(3)} ` +
      `事件=${r.events} 样式=${r.styles} 角色=${r.roles} 动作=${r.actions}`,
  );
}
console.log(`  合计控件 ${tot}，合并后窗口 ${maxW}x${maxH}`);

/* ---------------------------------------------------------------- 写盘 */
if (dry) {
  console.log("[dry] 未写盘");
  process.exit(0);
}
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, JSON.stringify(merged, null, 2) + "\n", "utf8");
console.log(`[ok] 写出 ${path.relative(root, outFile)}（${fs.statSync(outFile).size} 字节）`);
