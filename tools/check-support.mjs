/* check-support.mjs -- 「支持库」表与公共头文件的一致性检查
 *
 * 背景：画布「支持库」页签的数据是手写的（designer/src/supportlib.ts），
 * 而真相在 runtime/include/cstackgui.h。改了一边忘了另一边，新手就会照着
 * 不存在的函数写代码 —— 而且要到编译时才发现。这个脚本把两边对起来：
 *
 *   1. 失败项：表里的 cg_* 函数在头文件里找不到 / CG_* 常量没有 #define
 *      （= 表里写了不存在的东西，点了「复制」粘过去编译不过）
 *   2. 提示项：头文件里的公共 API 没进表（= 新手在画布里查不到它）
 *
 * 用法：node tools/check-support.mjs   （在项目根，或由 tools\check-format.bat 调起）
 *
 * 若 designer 源码不在（如发布包），本脚本直接跳过，不算失败。
 */
import { execSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");
const headerPath = path.join(root, "runtime", "include", "cstackgui.h");
const srcPath = path.join(root, "designer", "src", "supportlib.ts");

if (!fs.existsSync(srcPath) || !fs.existsSync(headerPath)) {
  console.log("[skip] support-lib check (no designer sources / runtime header)");
  process.exit(0);
}

/* 1. 把 TS 数据表编译成可 import 的 js（和 check-format 编译 model.ts 同一套路） */
const outDir = path.join(root, "build", "supportjs");
try {
  /* 走 shell：Windows 上 npx 是 .cmd，node 不允许无 shell 直接 spawn */
  execSync(
    "npx --prefix designer tsc designer/src/supportlib.ts --outDir build/supportjs " +
      "--target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict",
    { cwd: root, stdio: "pipe", shell: true },
  );
} catch (e) {
  console.error("[fail] 编译 designer/src/supportlib.ts 失败");
  console.error(String(e.stdout ?? "") + String(e.stderr ?? ""));
  process.exit(1);
}

const { SUPPORT_LIBS, allCommands } = await import(
  `file://${path.join(outDir, "supportlib.js").replace(/\\/g, "/")}`
);

/* 2. 从公共头文件里抠出真实存在的函数名与宏名 */
const header = fs.readFileSync(headerPath, "utf8");
const headerFns = new Set();
for (const m of header.matchAll(/\b(cg_[a-z0-9_]+)\s*\(/g)) headerFns.add(m[1]);
const headerMacros = new Set();
for (const m of header.matchAll(/#define\s+(CG_[A-Z0-9_]+)/g)) headerMacros.add(m[1]);
/** 表里允许出现的「不是函数」的条目：枚举类型名（帮助卡片里当说明用） */
const TYPE_ENTRIES = new Set(["cg_event_t"]);

const cmds = allCommands();
let bad = 0;

/* 3. 表里的每一条都必须在头文件里真的存在 */
for (const c of cmds) {
  if (TYPE_ENTRIES.has(c.fn)) continue;
  const ok = c.fn.startsWith("CG_") ? headerMacros.has(c.fn) : headerFns.has(c.fn);
  if (!ok) {
    console.error(`[fail] 支持库里的 ${c.fn}（${c.name}）在 cstackgui.h 里不存在`);
    bad += 1;
  }
}

/* 4. 反方向：头文件里有、表里漏了的（只提示，不判失败） */
const listed = new Set(cmds.map((c) => c.fn));
const missingFns = [...headerFns].filter((f) => !listed.has(f)).sort();
const missingMacros = [...headerMacros].filter((m) => !listed.has(m) && !m.startsWith("CG_VERSION_")).sort();

console.log(
  `  支持库 ${cmds.length} 条：${SUPPORT_LIBS.map((l) => `${l.label}(${l.groups.length} 类)`).join(" / ")}`,
);
if (missingFns.length) console.log(`  提示：未收录的公共函数 ${missingFns.length} 个 —— ${missingFns.join(", ")}`);
if (missingMacros.length) console.log(`  提示：未收录的公共宏 ${missingMacros.length} 个 —— ${missingMacros.join(", ")}`);

if (bad) {
  console.error(`[fail] 支持库表与 cstackgui.h 不一致（${bad} 条）`);
  process.exit(1);
}
console.log("[ok] 支持库表与 cstackgui.h 一致");
