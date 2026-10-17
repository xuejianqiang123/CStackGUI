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

/* 4. 反方向：头文件里有、表里漏了的 —— 存量走白名单，**新增漏收录直接红**。
 *
 * 为什么从"只提示"升级（2026-10-17 架构评审"反向仅提示"整改）：提示没人看 = 没有锁。
 * 新加一个公共 API 却忘了进支持库表，画布里就查不到它 —— 那正是本脚本存在的理由。
 *
 * 二轮评审升级（2026-10-17 同日）：白名单从"只有名字"改为 名字 -> 理由 ——
 * 报错一直要求"写明理由"，此前却无处可写；零理由字段的白名单是最容易腐化的形态。
 * 理由分两类（均已核对支持库正文）：「讲解在别处」= 该族的语义写在别的条目描述里
 * （如 CG_BLE_ATTR_* 出现在 BLE 特征条目的说明中）；「存量未收录」= 历史欠账，
 * 新增同族条目时应一并补上。
 * 退出策略：stale 自检连续两次红（白名单回升）= 收录策略已死，改为从 cstackgui.h
 * 生成索引、白名单整体作废；负向清单只应单调收缩。 */
const UNLISTED_OK_FNS = new Map([
  ["cg_dialog_close", "存量未收录：对话框族（2026-10-17 盘点）"],
  ["cg_dialog_open", "存量未收录：对话框族（2026-10-17 盘点）"],
  ["cg_dialog_open_nonmodal", "存量未收录：对话框族（2026-10-17 盘点）"],
  ["cg_input_box", "存量未收录：对话框族（2026-10-17 盘点）"],
  ["cg_edit_scroll_end", "存量未收录：编辑框辅助（2026-10-17 盘点）"],
  ["cg_table_begin_update", "存量未收录：表格批处理族（2026-10-17 盘点）"],
  ["cg_table_end_update", "存量未收录：表格批处理族（2026-10-17 盘点）"],
  ["cg_table_last_col", "存量未收录：表格批处理族（2026-10-17 盘点）"],
  ["cg_theme_is_dark", "存量未收录：主题族（2026-10-17 盘点）"],
  ["cg_theme_set_dark", "存量未收录：主题族（2026-10-17 盘点）"],
  ["cg_widget_focus", "存量未收录：焦点控制（2026-10-17 盘点）"],
]);
const UNLISTED_OK_MACROS = new Map([
  ["CG_BIND_END", "讲解在别处：CG_BIND 系列条目的示例即用（2026-10-17 核对）"],
  ["CG_BLE_ATTR_INDICATE", "讲解在别处：BLE 特征条目描述（supportlib.ts:1048）"],
  ["CG_BLE_ATTR_NOTIFY", "讲解在别处：BLE 特征条目描述（supportlib.ts:1048）"],
  ["CG_BLE_ATTR_READ", "讲解在别处：BLE 特征条目描述（supportlib.ts:1048）"],
  ["CG_BLE_ATTR_WRITE", "讲解在别处：BLE 特征条目描述（supportlib.ts:1048）"],
  ["CG_BLE_ATTR_WRITE_NR", "讲解在别处：BLE 特征条目描述（supportlib.ts:1048）"],
  ["CG_FRAME_DELIM", "讲解在别处：帧定界条目描述（supportlib.ts:1081）"],
  ["CG_FRAME_FIXED", "讲解在别处：帧定界条目描述（supportlib.ts:1081）"],
  ["CG_FRAME_LENFIELD", "讲解在别处：帧定界条目描述（supportlib.ts:1081）"],
  ["CG_FRAME_QUEUE", "讲解在别处：帧定界条目描述（supportlib.ts:1081）"],
  ["CG_FRAME_SILENCE", "讲解在别处：帧定界条目描述（supportlib.ts:1081）"],
  ["CG_LOG_ERROR", "讲解在别处：日志条目示例即用"],
  ["CG_LOG_INFO", "讲解在别处：日志条目示例即用"],
  ["CG_LOG_WARN", "讲解在别处：日志条目示例即用"],
  ["CG_MODBUS_MAX_VALUES", "存量未收录：Modbus 常量族（2026-10-17 盘点）"],
  ["CG_MODBUS_RTU", "存量未收录：Modbus 常量族（2026-10-17 盘点）"],
  ["CG_MODBUS_TCP", "存量未收录：Modbus 常量族（2026-10-17 盘点）"],
  ["CG_SUM_ADD16", "存量未收录：校验和常量族（2026-10-17 盘点）"],
  ["CG_SUM_ADD8", "存量未收录：校验和常量族（2026-10-17 盘点）"],
  ["CG_SUM_CRC16_MODBUS", "存量未收录：校验和常量族（2026-10-17 盘点）"],
  ["CG_SUM_NONE", "存量未收录：校验和常量族（2026-10-17 盘点）"],
  ["CG_SUM_XOR8", "存量未收录：校验和常量族（2026-10-17 盘点）"],
  ["CG_TERM_RX", "讲解在别处：terminal 条目示例即用（supportlib.ts:518 等 6 处）"],
  ["CG_TERM_SYS", "讲解在别处：terminal 条目示例即用"],
  ["CG_TERM_TX", "讲解在别处：terminal 条目示例即用"],
  ["CG_WIFI_SSID_MAX", "存量未收录：WiFi 常量（2026-10-17 盘点）"],
]);

const listed = new Set(cmds.map((c) => c.fn));
const missingFns = [...headerFns].filter((f) => !listed.has(f) && !UNLISTED_OK_FNS.has(f)).sort();
const missingMacros = [...headerMacros]
  .filter((m) => !listed.has(m) && !m.startsWith("CG_VERSION_") && !UNLISTED_OK_MACROS.has(m))
  .sort();
const staleFns = [...UNLISTED_OK_FNS.keys()].filter((f) => listed.has(f)).sort();
const staleMacros = [...UNLISTED_OK_MACROS.keys()].filter((m) => listed.has(m)).sort();
/* TYPE_ENTRIES（"不是函数"的说明性条目）此前没有 stale 自检 —— 二轮评审补上，
 * 白名单类清单一律要能自己瘦身。 */
const staleTypes = [...TYPE_ENTRIES].filter((t) => !listed.has(t)).sort();
bad += missingFns.length + missingMacros.length + staleFns.length + staleMacros.length + staleTypes.length;

console.log(
  `  支持库 ${cmds.length} 条：${SUPPORT_LIBS.map((l) => `${l.label}(${l.groups.length} 类)`).join(" / ")}`,
);
if (missingFns.length)
  console.error(`[fail] 新增公共函数未收录进支持库表：${missingFns.join(", ")} —— 补 c(...) 条目，或加进 UNLISTED_OK_FNS 并写明理由`);
if (missingMacros.length)
  console.error(`[fail] 新增公共宏未收录进支持库表：${missingMacros.join(", ")} —— 补条目，或加进 UNLISTED_OK_MACROS 并写明理由`);
if (staleFns.length || staleMacros.length)
  console.error(`[fail] 白名单已过期（这些条目其实已收录进表）：${[...staleFns, ...staleMacros].join(", ")} —— 从 UNLISTED_OK_* 删除`);
if (staleTypes.length)
  console.error(`[fail] TYPE_ENTRIES 已过期（表里已没有这些条目）：${staleTypes.join(", ")} —— 从 TYPE_ENTRIES 删除`);

if (bad) {
  console.error(`[fail] 支持库表与 cstackgui.h 不一致（${bad} 条）`);
  process.exit(1);
}

/* 5. 教学说明里手写的「支持库（N 条）」必须等于这里数出来的真值。
 *
 * 为什么放这里：条数的真相就在本脚本刚算出的 cmds.length（编译后的数据表 = 画布上
 * 真实列出的条数），在这里比一次是零成本；放进「文档口径」门禁（check-examples.mjs --docs）
 * 则只能拿正则去数源码，多行 c( ... ) 会数漏（2026-10-11 实测：正则 282、真值 292）。
 *
 * 为什么值得兜：这个数字以前靠人肉 +1 维护，早就漂了 —— 2026-10-11 实测文档写着 202、
 * 真值 292（差 90 条），而当时全部门禁都是绿的。手写的"同一事实两份"没人兜就会停在
 * 上一次手改的那一版（本仓库 2026-09-23 体检同款教训：一次漏改 31 处）。 */
let docBad = 0;
const docRel = "examples/教学使用说明.md";
const docPath = path.join(root, docRel);
if (fs.existsSync(docPath)) {
  const docText = fs.readFileSync(docPath, "utf8");
  let claimed = 0;
  for (const m of docText.matchAll(/「\*\*支持库\*\*」页签（([0-9]+) 条/g)) {
    claimed += 1;
    if (Number(m[1]) !== cmds.length) {
      console.error(
        `[fail] ${docRel} 写着「支持库 ${m[1]} 条」，表里实际 ${cmds.length} 条 —— 改成 ${cmds.length}`,
      );
      docBad += 1;
    }
  }
  if (claimed === 0) {
    console.error(
      `[fail] ${docRel} 里找不到「**支持库**」页签的条数声明（措辞改了？这条门禁要跟着改）`,
    );
    docBad += 1;
  }
} else {
  console.log(`  [skip] 文档条数比对（没有 ${docRel}）`);
}

if (docBad) {
  console.error(`[fail] 文档里的支持库条数与数据表不一致（${docBad} 处）`);
  process.exit(1);
}
console.log("[ok] 支持库表与 cstackgui.h 一致、文档条数一致");
