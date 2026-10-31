/* check-header.mjs -- 公共 API 集合快照
 *
 * 为什么需要它：runtime/include/cstackgui.h 有 328 个函数、19 个 typedef、一批 CG_ 宏，
 * 但**没有任何判据知道"声明集合本来长什么样"**。改一个控件、删一个废弃接口、
 * 或者哪天想拆分头文件，编译器只会告诉你"用到了不存在的东西"——反过来，
 * 删掉一个没人调用的公共 API，全仓照样绿。
 *
 * 快照的作用就是把"集合"本身变成判据：少一个就红，多一个也红（多一个要求你
 * 显式更新基线，避免基线悄悄腐化到没用）。
 *
 * 用法：
 *   node tools/check-header.mjs            校验（CI / 门禁用）
 *   node tools/check-header.mjs --update   有意改动 API 后更新基线
 *   node tools/check-header.mjs --verbose  打印完整清单
 */
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\//, "")), "..");
const headerPath = path.join(root, "runtime", "include", "cstackgui.h");
const basePath = path.join(root, "tools", "header-baseline.json");

const args = new Set(process.argv.slice(2));
const update = args.has("--update");
const verbose = args.has("--verbose");

/* ---------- 解析 ---------- */

/* 函数声明：行首「返回类型 [指针] cg_名字(」。
 *
 * 收尾必须用 `\b` 而不是 `\s+`：`cg_widget_t *cg_find(` 里 `*` 与 `cg_find` 之间
 * 没有空格，`[\w \*]*\s+` 匹配不上 —— 结果是**所有返回指针的函数被静默漏掉**
 * （`cg_find` 这一类共 57 个）。这就是"271 个函数"这个长期流传数字的来源：
 * 用旧口径数出来 271，换成本条正则才是真实值 328，差了 21%。
 * 注释里的 `cg_app_*` 索引写法不会被吃进来：`^\s*` 匹配不到注释行首的 `*`。 */
const RE_FN = /^\s*[A-Za-z_][\w \t\*]*?\b(cg_\w+)\s*\(/gm;

/* typedef 两种写法：
 *   typedef struct cg_app cg_app_t;              单行
 *   typedef void (*cg_event_fn)(...);            函数指针
 *   } cg_event_t;                                结构体收尾
 * 只认 cg_ 前缀，避免把标准库/内部辅助类型算进公共面。 */
const RE_TD_SINGLE = /^typedef\s+[^{]*?\b(cg_\w+)\s*;/gm;
const RE_TD_STRUCT = /^\}\s*(cg_\w+)\s*;/gm;
/* 函数指针 typedef：typedef void (*cg_event_fn)(cg_widget_t *, cg_event_t, void *);
 * 上面那条单行正则吃不到它（名字后面跟的是 `)` 和参数表，不是 `;`）——
 * 而这两个恰恰是最重要的契约（事件回调签名），漏了等于没锁。 */
const RE_TD_FNPTR = /^typedef\s+[\w \*]*\(\s*\*\s*(cg_\w+)\s*\)/gm;

/* 宏：#define CG_xxx —— CG_RGB / CG_BIND / CG_BIND_END 这些是使用者的日常词汇，
 * 删掉同样是破坏性变更。 */
const RE_MACRO = /^\s*#define\s+(CG_\w+)/gm;

const src = fs.readFileSync(headerPath, "utf8");
const grab = (re) => [...src.matchAll(re)].map((m) => m[1]);

const cur = {
  functions: [...new Set(grab(RE_FN))].sort(),
  typedefs: [...new Set([...grab(RE_TD_SINGLE), ...grab(RE_TD_STRUCT), ...grab(RE_TD_FNPTR)])].sort(),
  macros: [...new Set(grab(RE_MACRO))].sort(),
};

const summarize = (x) => `函数 ${x.functions.length} / typedef ${x.typedefs.length} / 宏 ${x.macros.length}`;

/* ---------- 更新基线 ---------- */

if (update) {
  const before = fs.existsSync(basePath) ? JSON.parse(fs.readFileSync(basePath, "utf8")) : null;
  fs.writeFileSync(basePath, JSON.stringify(cur, null, 2) + "\n", "utf8");
  console.log(`[ok] 已写入基线 ${path.relative(root, basePath)}`);
  console.log(`     ${summarize(cur)}`);
  if (before) {
    /* 更新基线这个动作最容易掩盖破坏性变更 —— 把 diff 打出来，让人看见删了什么 */
    for (const key of ["functions", "typedefs", "macros"]) {
      const old = new Set(before[key] ?? []);
      const now = new Set(cur[key]);
      const removed = [...old].filter((x) => !now.has(x));
      const added = [...now].filter((x) => !old.has(x));
      for (const x of removed) console.log(`     [删除] ${key}: ${x}`);
      for (const x of added) console.log(`     [新增] ${key}: ${x}`);
    }
  }
  process.exit(0);
}

/* ---------- 校验 ---------- */

if (!fs.existsSync(basePath)) {
  console.error(`[fail] 缺少基线 ${path.relative(root, basePath)}`);
  console.error("       首次使用请先跑：node tools/check-header.mjs --update");
  process.exit(1);
}

const base = JSON.parse(fs.readFileSync(basePath, "utf8"));
let bad = 0;

for (const key of ["functions", "typedefs", "macros"]) {
  const oldSet = new Set(base[key] ?? []);
  const newSet = new Set(cur[key] ?? []);
  const removed = [...oldSet].filter((x) => !newSet.has(x)).sort();
  const added = [...newSet].filter((x) => !oldSet.has(x)).sort();

  if (removed.length) {
    console.error(`[fail] 公共 API 删除了 ${removed.length} 个 ${key}（破坏性变更，使用者会编不过）：`);
    for (const x of removed) console.error(`         - ${x}`);
    bad += removed.length;
  }
  if (added.length) {
    console.error(`[fail] 公共 API 新增了 ${added.length} 个 ${key}（需要显式更新基线）：`);
    for (const x of added) console.error(`         + ${x}`);
    bad += added.length;
  }
}

if (bad) {
  console.error("");
  console.error("  新增是有意改动 → node tools/check-header.mjs --update（会打出 diff 供复核）");
  console.error("  删除是非预期改动 → 请找回声明；确属废弃接口，先在 docs/ 里登记再更新基线");
  process.exit(1);
}

if (verbose) {
  for (const key of ["functions", "typedefs", "macros"]) {
    console.log(`--- ${key} (${cur[key].length}) ---`);
    for (const x of cur[key]) console.log("  " + x);
  }
}

console.log(`[ok] 公共 API 集合与基线一致：${summarize(cur)}`);
