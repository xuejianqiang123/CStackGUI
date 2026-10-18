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

for (const { tool, pre } of SUITE) {
  let src = fs.readFileSync(srcOf(tool), "utf8");

  // 1) 找 on_ready 名字（各工具可能不同）：desc.on_ready = NAME;
  const readyName = (src.match(/desc\.on_ready\s*=\s*(\w+)/) || [])[1];

  // 2) 去掉 int main(...)（从 `int main` 到文件尾；main 是最后一个函数）
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

  // 5) on_ready -> <pre>_init（保持签名）
  if (readyName && readyName !== `${pre}_init`) {
    src = src.replace(
      new RegExp(`static\\s+void\\s+${readyName}\\s*\\(`, "g"),
      `void ${pre}_init(`,
    );
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
  src = header + src + wrappers;

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
  .join("")}
#endif
`;
  fs.writeFileSync(path.join(dir, `${pre}.h`), h, "utf8");
  console.log(
    `  ${tool} -> modules/${pre}.c  (ready=${readyName || "?"} 窗口事件=${appEvents.map(([e]) => e).join("/") || "-"})`,
  );
}
console.log("[ok] 模块已生成");
