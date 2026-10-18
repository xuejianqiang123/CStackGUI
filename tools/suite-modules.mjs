/* tools\suite-modules.mjs -- 把 N 个示例 main.c 转成套件模块（C 侧的机械变换）
 *
 * 与 tools\suite-merge.mjs 配对：那个管 .cgui 侧（id/事件加前缀 + 拼 tabs），
 * 本工具管 C 侧（去 main()、绑定表导出、CG_BIND 字符串加前缀、on_ready -> <pre>_init，
 * 并生成含完整 Doxygen 的 <pre>.h）。**合并靠脚本不靠手工拼**：同一套规则要在
 * 4 个套件里重复，手工必然"这个改了那个漏了"。
 *
 * 前提（2026-10-18 实测 5/5 工具成立，后续套件动手前先复核）：各工具的回调**全是 static**、
 * 零个顶层全局变量 —— 合并进同一次链接，函数名不必改（编译单元隔离），只有
 * CG_BIND 的字符串名与导出符号要动。
 *
 * 用法：改下方 SUITE 表（tool=原工程名、pre=两位缩写前缀）后跑
 *       node tools\suite-modules.mjs <套件名>
 *
 * **2026-10-18 P4 之后原工程已删**：本工具对既有 4 个套件已无输入可跑（同 suite-merge.mjs
 * 头部的说明）。保留是为了记录 C 侧变换规则 —— 去 main、绑定表导出、CG_BIND 与动作名
 * 字符串加前缀、on_ready 变 init、窗口事件直连转发、main 体分类搬运。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** 各套件的模块清单（前缀两位小写，与 suite-merge.mjs 的 SUITES 表必须一致） */
const SUITES = {
  dev: [
    { tool: "palette", pre: "pl" },
    { tool: "drawpad", pre: "dp" },
    { tool: "hextool", pre: "ht" },
    { tool: "encconv", pre: "ec" },
    { tool: "calculator", pre: "cl" },
  ],
  text: [
    { tool: "notepad", pre: "np" },
    { tool: "logfilter", pre: "lf" },
    { tool: "filecheck", pre: "fc" },
    { tool: "dataview", pre: "dv" },
    { tool: "htmlreport", pre: "hr" },
    { tool: "datarec", pre: "dr" },
  ],
  /* comm：serialterm / modbus 的 --demo 是**自我识别的旗标**（各模块只比较自己的旗标，
   * 别人的旗标自然忽略），所以套件 main 可以把 argc/argv 原样转发给每个模块的
   * <pre>_parse_cli —— `comm.exe --demo` 两个模块同时进演示模式，语义正确。
   * 反例是 argv[1] 开文件类（dataview/encconv/htmlreport）：一个 exe 分不清该给谁，
   * 那类语句不搬，由转换器明确报"随入口丢弃"。 */
  comm: [
    { tool: "serialterm", pre: "st", cli: true },
    { tool: "netterm", pre: "nt" },
    { tool: "darkterm", pre: "dt" },
    { tool: "modbus", pre: "mb", cli: true },
    { tool: "framedec", pre: "fd" },
  ],
  sys: [
    { tool: "shottool", pre: "sh" },
    { tool: "traytool", pre: "tr" },
    { tool: "syspanel", pre: "sp" },
    { tool: "envsnap", pre: "en" },
    { tool: "settings", pre: "se" },
  ],
};

const suiteName = process.argv[2];
const SUITE = SUITES[suiteName];
if (!SUITE) {
  console.error(`用法: node tools\\suite-modules.mjs <${Object.keys(SUITES).join("|")}>`);
  process.exit(2);
}

/** 两种布局都认（原工程可能平铺 examples\<tool>\\ 或分组 examples\\base\\<tool>\\） */
function srcOf(tool) {
  for (const d of [path.join(root, "examples", tool), path.join(root, "examples", "base", tool)]) {
    if (fs.existsSync(path.join(d, "main.c"))) return path.join(d, "main.c");
  }
  throw new Error(`找不到 ${tool}/main.c`);
}

/** 原工程的 .cgui 路径（与 srcOf 同目录） */
function cguiFile2(tool) {
  return path.join(path.dirname(srcOf(tool)), `${tool}.cgui`);
}

/* ---------------------------------------------------------------- main() 体分析
 * 剥 main() 不能一剥了之（P2 首跑前的通用扫描抓到：17 个工具的 main() 里有非 desc 语句）：
 *   - darkterm 在 main 里做工具级初值（s_biz.autoscroll = 1）——丢了就与 .cgui 的
 *     初始勾选态不一致（P0/P1 都没暴露：它们的工具 main 里只有启动日志）；
 *   - serialterm/modbus 在 main 里解析 --demo 旗标 ——丢了就没了"无硬件自测"入口；
 *   - dataview/encconv/htmlreport 用 argv[1] 开文件 —— 一个套件 exe 分不清该给谁，
 *     那类语句**不搬**，但必须**报出来**（静默丢 = 假绿）。
 * 做法：把 main 体按大括号平衡切成顶层语句块，分类后各归各位。 */
function mainBodyOf(src) {
  const mi = src.search(/^int main\s*\(/m);
  if (mi < 0) return "";
  const tail = src.slice(mi);
  const open = tail.indexOf("{");
  let depth = 0;
  for (let i = open; i < tail.length; i++) {
    if (tail[i] === "{") depth++;
    else if (tail[i] === "}" && --depth === 0) return tail.slice(open + 1, i);
  }
  return "";
}

/** 把 main 体切成顶层语句块（大括号 + **圆括号**都平衡才算顶层；
 *  for (i = 1; i < argc; i++) 的分号在圆括号里，只算大括号会把 for 头切成三块 ——
 *  2026-10-18 comm 首跑抓到。字符串/字符字面量里的括号与引号不算。 */
function topChunks(body) {
  const out = [];
  let cur = "";
  let brace = 0;
  let paren = 0;
  let quote = "";
  let esc = false;
  for (const c of body) {
    if (quote) {
      cur += c;
      if (esc) esc = false;
      else if (c === "\\") esc = true;
      else if (c === quote) quote = "";
      continue;
    }
    if (c === '"' || c === "'") { quote = c; cur += c; continue; }
    if (c === "{") brace++;
    else if (c === "}") brace--;
    else if (c === "(") paren++;
    else if (c === ")") paren--;
    cur += c;
    if (brace === 0 && paren === 0 && (c === ";" || c === "}")) { out.push(cur); cur = ""; }
  }
  if (cur.trim()) out.push(cur);
  return out.map((s) => s.trim()).filter(Boolean);
}

/** 语句块分类：入口(丢) / 旗标式CLI(可搬) / 启动日志(丢) / 局部声明(按引用搬) / 状态初值(搬) */
function classify(c) {
  if (/--selftest|cg_app_run|cg_app_selftest/.test(c)) return "entry";
  if (/desc\.|memset\s*\(\s*&desc|cg_app_desc_t/.test(c)) return "entry";
  if (/argc|argv|getenv/.test(c)) return "cli";
  if (/^\s*(cg_log|printf|puts)\s*\(/.test(c)) return "log";
  if (/^(?:const\s+)?(?:unsigned\s+)?(?:struct\s+\w+\s+)?[A-Za-z_][\w\s\*]*[\w\]\[]\s*;$/.test(c)) return "decl";
  return "state";
}

/** 取局部声明块里的标识符（`int i;` -> i） */
function declNameOf(c) {
  const m = c.match(/^(?:const\s+)?(?:unsigned\s+)?[A-Za-z_][\w\s\*]*?([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;$/);
  return m ? m[1] : null;
}

for (const { tool, pre, cli } of SUITE) {
  let src = fs.readFileSync(srcOf(tool), "utf8");

  // 1) 找 on_ready 名字（各工具可能不同）：desc.on_ready = NAME;
  const readyName = (src.match(/desc\.on_ready\s*=\s*(\w+)/) || [])[1];

  // 2) main() 体分析（见上方说明）：切块 -> 分类 -> 各归各位
  const mainBody = mainBodyOf(src);
  const chunks = topChunks(mainBody).map((c) => ({ text: c, kind: classify(c) }));
  const stateChunks = chunks.filter((c) => c.kind === "state").map((c) => c.text);
  const cliChunks = chunks.filter((c) => c.kind === "cli").map((c) => c.text);
  const droppedCli = chunks.filter((c) => c.kind === "cli").map((c) => c.text);
  const nLogs = chunks.filter((c) => c.kind === "log").length;
  /* 局部声明按引用搬：只搬被对应用途（状态初值 / 搬走的 CLI）真正用到的声明，
   * 状态与 CLI 各算各的（状态块用不到 i 就别把 int i 搬进 init）。 */
  const declsFor = (txt) =>
    chunks
      .filter((c) => c.kind === "decl")
      .map((c) => ({ text: c.text, name: declNameOf(c.text) }))
      .filter((d) => d.name && new RegExp(`\\b${d.name}\\b`).test(txt))
      .map((d) => d.text);
  const stateDecls = declsFor(stateChunks.join("\n"));
  const cliDecls = declsFor(cliChunks.join("\n"));
  /* 状态初值搬进 init；没有 on_ready 的工具（darkterm）由转换器生成一个 init */
  const statePrelude =
    stateChunks.length || !readyName
      ? `    /* 原 main() 里的工具级初值（转换器搬来：丢了会与 .cgui 的初始状态不一致） */\n` +
        stateDecls.map((d) => `    ${d}\n`).join("") +
        stateChunks.map((s) => `    ${s}`).join("\n") + "\n"
      : "";
  /* 旗标式 CLI：只有 cli:true 的工具才搬（套件 main 把 argv 原样转发，各模块只认自己的旗标） */
  const cliFn =
    cli && cliChunks.length
      ? `/**
 * @brief 命令行旗标的套件转发入口（套件 main 把 argc/argv 原样转发；不认识的旗标被本模块的比较忽略）
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return void 无返回值
 */
void ${pre}_parse_cli(int argc, char **argv)
{
${cliDecls.map((d) => `    ${d}\n`).join("")}${cliChunks.map((s) => `    ${s}`).join("\n")}
    (void)argc;
    (void)argv;
}
`
      : "";

  // 去掉 int main(...)（从 `int main` 到文件尾；main 是最后一个函数）
  const mainIdx = src.search(/^int main\s*\(/m);
  if (mainIdx > 0) src = src.slice(0, mainIdx).replace(/\s+$/, "") + "\n";

  // 3) k_bindings -> <pre>_bindings（去 static，保留 const）
  src = src.replace(
    /static\s+const\s+cg_binding_t\s+k_bindings\[\]/g,
    `const cg_binding_t ${pre}_bindings[]`,
  );

  // 4) CG_BIND("on_xxx" -> CG_BIND("<pre>_on_xxx"（仅当还没带前缀）
  src = src.replace(/CG_BIND\("([^"]+)"/g, (m, name) =>
    name.startsWith(`${pre}_`) ? m : `CG_BIND("${pre}_${name}"`,
  );

  /* 4b) **C 代码里按字符串引用动作名**的地方（strcmp(name, "shot")、
   * cg_hotkey_set(app, hk, "shot")）：动作键加了前缀而 C 串不改，就是
   * "点菜单没反应 / 全局热键白注册"的**静默失效** —— selftest 抓不到（回调照常触发，
   * 只是分支不匹配）。2026-10-18 P3 动手前扫描全仓抓到 10 处（notepad/shottool/traytool），
   * 其中 notepad 那处意味着 P1 的"另存为"已经在套件里悄悄退化成"保存"。
   * 只在**动作名消费模式**（strcmp / cg_hotkey_set / cg_action）的行上改，
   * 改动逐行报出来供复核；check-suite 另有一道"残留旧名"的反向门禁兜底。 */
  const actionKeys = fs.existsSync(cguiFile2(tool))
    ? Object.keys(JSON.parse(fs.readFileSync(cguiFile2(tool), "utf8")).actions ?? {})
    : [];
  for (const k of actionKeys) {
    const lit = `"${k}"`;
    src = src
      .split("\n")
      .map((line) => {
        if (!line.includes(lit) || !/(strcmp|cg_hotkey_set|cg_action)/.test(line)) return line;
        console.log(`      [动作名] ${line.trim().slice(0, 76)}`);
        return line.split(lit).join(`"${pre}_${k}"`);
      })
      .join("\n");
  }

  // 5) on_ready -> <pre>_init（保持签名）；原 main() 里的工具级初值注入 init 开头
  if (readyName && readyName !== `${pre}_init`) {
    src = src.replace(
      new RegExp(`static\\s+void\\s+${readyName}\\s*\\(`, "g"),
      `void ${pre}_init(`,
    );
  }
  if (statePrelude) {
    if (readyName) {
      const m = src.match(new RegExp(`void\\s+${pre}_init\\s*\\([^)]*\\)\\s*\\{`));
      if (m) src = src.replace(m[0], `${m[0]}\n${statePrelude}`);
      else console.error(`  [warn] ${tool}: 找不到 ${pre}_init 的函数体开头，状态初值未注入`);
    } else {
      /* 没有 on_ready 的工具（darkterm）：由转换器生成 init，别让 main() 的初值随入口消失 */
      src += `
/**
 * @brief ${tool} 套件模块初始化（原工程没有 on_ready；初值原在 main() 里，由转换器搬来）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void ${pre}_init(cg_app_t *app, void *user)
{
    (void)app;
    (void)user;
${statePrelude}
}
`;
    }
  }

  /* 5b) 窗口事件改直连转发（P1 首次遇到：notepad 的 actions 之后第二个"多工具撞一个槽"）。
   *
   * 事实：一个窗口只能接一个回调名（运行时把 app.events.close 存进 root->ev[close]
   * 一个槽）。合并后 N 个工具都想要 close —— "首工具胜出"会静默吞掉其余工具的窗口
   * 职责（notepad 的未保存拦截 / filecheck 的写窗口尺寸 / lf·hr 的收尾记账全没了）。
   * 正解是 main 的分发器 suite_<event> 逐个调进来，本模块的 handler 不再"按名派发"：
   *   - 绑定表里那条 CG_BIND 删掉（留着就是死绑定，check-suite 会红）；
   *   - 追加直连转发函数 <pre>_app_<event>，由 main 直接调。
   */
  const cguiFile = path.join(path.dirname(srcOf(tool)), `${tool}.cgui`);
  const appEvents = fs.existsSync(cguiFile)
    ? Object.entries(JSON.parse(fs.readFileSync(cguiFile, "utf8")).app?.events ?? {})
    : [];
  for (const [, handler] of appEvents) {
    /* 注意匹配**已加前缀**的名字：本步跑在"CG_BIND 字符串加前缀"之后，按原名匹配会落空
     * （2026-10-18 首跑抓到：np_on_close 没删掉，成死绑定）。(?:前缀_)? 两者都认。 */
    src = src.replace(new RegExp(`\\s*CG_BIND\\("(?:${pre}_)?${handler}"[^\\n]*\\n`, "g"), "\n");
  }
  const wrappers = appEvents
    .map(([ev, handler]) => `
/**
 * @brief 窗口事件 ${ev} 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev ${ev}
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void ${pre}_app_${ev}(cg_widget_t *self, cg_event_t ev, void *user)
{
    ${handler}(self, ev, user);
}
`)
    .join("");

  // 6) 文件头注释加一行模块说明（在首个 /* 块后插入）
  const header = `/**
 * @file    ${pre}.c
 * @brief   ${tool} —— 套件模块（原 examples\\${tool} 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\\suite-merge.mjs（.cgui 侧）+ build\\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 ${pre}_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 ${pre}_bindings、原 on_ready 变成 ${pre}_init、
 *          窗口事件改成直连转发 ${pre}_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

`;
  src = header + src + wrappers + cliFn;

  const dir = path.join(root, "examples", "suites", suiteName, "modules");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, `${pre}.c`), src, "utf8");

  // 7) 头文件（注释必须过 check-comments：@brief / @param / @return 齐全）
  const h = `/**
 * @file    ${pre}.h
 * @brief   ${tool} 套件模块接口
 */
#ifndef SUITE_${pre.toUpperCase()}_H
#define SUITE_${pre.toUpperCase()}_H

#include "cstackgui.h"

/** ${tool} 的事件绑定表（表尾 CG_BIND_END）；事件名带 ${pre}_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t ${pre}_bindings[];

/**
 * @brief ${tool} 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void ${pre}_init(cg_app_t *app, void *user);
${appEvents
  .map(
    ([ev]) => `
/**
 * @brief 窗口事件 ${ev} 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev ${ev}
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void ${pre}_app_${ev}(cg_widget_t *self, cg_event_t ev, void *user);
`,
  )
  .join("")}${cliFn
  ? `
/**
 * @brief 命令行旗标的套件转发入口（套件 main 转发 argc/argv；不认识的旗标被本模块的比较忽略）
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return void 无返回值
 */
void ${pre}_parse_cli(int argc, char **argv);
`
  : ""}
#endif
`;
  fs.writeFileSync(path.join(dir, `${pre}.h`), h, "utf8");
  console.log(
    `  ${tool} -> modules/${pre}.c  (ready=${readyName || "?"} 窗口事件=${appEvents.map(([e]) => e).join("/") || "-"} 初值=${stateChunks.length} CLI=${cliFn ? `${cliChunks.length}块` : "不搬"})`,
  );
  /* 丢弃要报出来（静默丢 = 假绿）：旗标式 CLI 没被选中搬走的、以及启动日志的条数 */
  for (const t of droppedCli) {
    if (!cli) console.log(`      [丢弃] 旗标式 CLI 未搬（未选 cli:true）：${t.split("\n")[0].trim().slice(0, 76)}`);
  }
  if (nLogs) console.log(`      [丢弃] 启动日志 ${nLogs} 条随入口丢弃（无功能损失）`);
}
console.log("[ok] 模块已生成");
