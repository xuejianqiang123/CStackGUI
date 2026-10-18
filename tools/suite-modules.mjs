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

  // 6) 文件头注释加一行模块说明（在首个 /* 块后插入）
  const header = `/**
 * @file    ${pre}.c
 * @brief   ${tool} —— 套件模块（原 examples\\${tool} 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\\suite-merge.mjs（.cgui 侧）+ build\\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 ${pre}_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 ${pre}_bindings、原 on_ready 变成 ${pre}_init。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

`;
  src = header + src;

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

#endif
`;
  fs.writeFileSync(path.join(dir, `${pre}.h`), h, "utf8");
  console.log(`  ${tool} -> modules/${pre}.c  (ready=${readyName || "?"})`);
}
console.log("[ok] 模块已生成");
