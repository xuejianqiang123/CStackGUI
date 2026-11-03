/**
 * @file    check-docs.mjs
 * @brief   路径判据：产出物放哪 + README 目录地图不许指错地方
 * @details 为什么要有它：
 *
 *   **A/B/C 管产出物去向。**`README.md` §7 与 `global-rule.md` §9 都写着
 *   「PRD → `docs/PRD_<feature>.md`」，但 2026-11-01 实测：`docs/` 下**一个 PRD 都没有**，
 *   5 份 PRD/SDD 全躺在 `.codebuddy/plans/` 里、还带着 `-prd.md` / `-sdd.md` 后缀。
 *   规范与现实的差距，**没有任何命令会发现** —— 直到有人按规范去 docs/ 找，找不到。
 *
 *   所以判据必须双向：
 *     A. `docs/` 下不许再有旧命名（`*-prd.md` / `*-sdd.md` / `*-slices.md`）；
 *     B. `.codebuddy/plans/` 下不许出现 PRD/SDD/切片（不管新旧命名）—— 那里只放实现计划；
 *     C. `docs/` 下带 `PRD_/SDD_/SLICES_` 前缀的文件名必须是小写连字符风格
 *        （`PRD_multi-window.md` 对，`PRD_MultiWindow.md` / `PRD_多窗体.md` 不对）。
 *
 *   **D 管目录地图。**2026-11-03 实测 `README.md` §4.1 那棵树里 19 条指向不存在的目录：
 *   `suites\comm\` `dev\` `text\`（早并成 studio/sys）、16 个 `examples\<tool>\`
 *   （早并成 `suites\<套件>\modules\`）、外加重复的 `toolbox\` 和不存在的
 *   `tools\gen-launcher.bat`。
 *   根因不是懒，是**那棵树穷举了磁盘事实**——重复的事实必然腐化，而没有任何命令检查它。
 *
 *   对策是两条腿：
 *     1. 树本身砍成骨架（只剩顶层 + runtime 四层 + 入口脚本），穷举下沉到各自目录；
 *     2. 就是这条 D——树里写的每个路径都必须真实存在。
 *   注意"地图块找不到"也算失败：否则有人删掉块头，D 会静默变成什么都不查
 *   （那正是本项目反复踩的坑：检查跳过了，门禁照样绿）。
 *
 *   为什么不判「PRD 必须有配对 SDD」：不少 feature 只有 PRD（想法已定、还没设计），
 *   强制配对会逼人写空壳 SDD —— 那是拿形式换内容。
 *
 * 用法：node tools\check-docs.mjs
 * 退出码：0 = 路径合规；1 = 有越界、命名不合规，或地图指错地方
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const DOCS = path.join(root, "docs");
const PLANS = path.join(root, ".codebuddy", "plans");

const OLD_RE = /-(prd|sdd|slices)\.md$/i; /* 旧命名：f2-multi-window-prd.md */
const PREFIX_RE = /^(PRD|SDD|SLICES)_/; /* 规范前缀 */
const NAME_RE = /^(PRD|SDD|SLICES)_[a-z0-9][a-z0-9-]*\.md$/; /* 规范全名 */

const bad = [];
let ok = 0;

function scan(dir, handler) {
  if (!fs.existsSync(dir)) return;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (!e.isFile() || !e.name.endsWith(".md")) continue;
    handler(e.name);
  }
}

/* A + C：docs/ 下的命名 */
scan(DOCS, (name) => {
  if (OLD_RE.test(name)) {
    bad.push(
      `[fail] docs/${name}：旧命名\n` +
        `    改成 docs/PRD_<feature>.md 或 docs/SDD_<feature>.md（feature 用短横线小写）`
    );
    return;
  }
  if (PREFIX_RE.test(name) && !NAME_RE.test(name)) {
    bad.push(
      `[fail] docs/${name}：前缀对，但 feature 部分不合规\n` +
        `    规范：<PRD|SDD|SLICES>_<小写短横线>.md，例：PRD_multi-window.md`
    );
    return;
  }
  ok++;
});

/* B：plans/ 下不许有 PRD / SDD / 切片 */
scan(PLANS, (name) => {
  if (OLD_RE.test(name) || PREFIX_RE.test(name)) {
    bad.push(
      `[fail] .codebuddy/plans/${name}：PRD / SDD / 切片不属于 plans/\n` +
        `    plans/ 只放实现计划（<feature>.md + 开头一行 > 状态：active/done/superseded）\n` +
        `    PRD / SDD / 切片请放 docs/ 并加对应前缀`
    );
    return;
  }
  ok++;
});

/* D：README 目录地图里写的每个路径都必须真实存在 */
const README = path.join(root, "README.md");
const MAP_HEAD = "CStackGUI\\"; /* 地图块的块头，单独一行 */
const MAP_SKIP = new Set(["build", "release"]); /* gitignore 的产物目录，新克隆上可能还没有 */

let mapCount = 0;
const mapLines = fs.existsSync(README)
  ? fs.readFileSync(README, "utf8").split(/\r?\n/)
  : [];
const mapStart = mapLines.findIndex((l) => l.trim() === MAP_HEAD);

if (!mapLines.length) {
  bad.push("[fail] README.md 不存在：目录地图无从核对");
} else if (mapStart < 0) {
  bad.push(
    `[fail] README.md：找不到目录地图（缺少单独一行的 \`${MAP_HEAD}\`）\n` +
      `    地图是新人唯一的入口，别把块头改掉或删掉；要改就改里面的条目`
  );
} else {
  const stack = []; /* [{ indent, segs }]，按缩进还原层级，支持 src\core\ 这种嵌套 */

  for (let i = mapStart + 1; i < mapLines.length; i++) {
    const line = mapLines[i];
    if (line.trim() === "```") break;
    if (!line.trim()) continue;

    /* 名称 = 行首空白之后的第一段非空白；描述列用 2+ 空格隔开 */
    const m = line.match(/^(\s+)(\S+)(?:\s{2,}.*)?$/);
    if (!m) continue;

    const indent = m[1].length;
    const segs = m[2].replace(/\\$/, "").split(/[\\/]/).filter(Boolean);
    if (!segs.length) continue;

    while (stack.length && stack[stack.length - 1].indent >= indent) stack.pop();
    const parent = stack.length ? stack[stack.length - 1].segs : [];
    const full = parent.concat(segs);
    stack.push({ indent, segs: full });

    mapCount++;
    if (MAP_SKIP.has(full[0])) continue;

    if (!fs.existsSync(path.join(root, ...full))) {
      bad.push(
        `[fail] README.md:${i + 1} 目录地图里的 ${full.join("\\")} 不存在\n` +
          `    要么删掉这条，要么改成真实路径 —— 指错地方的地图比没有地图更贵`
      );
    }
  }
}

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 路径判据：${bad.length} 处不合规`);
  process.exit(1);
}

console.log(
  `[ok] 路径合规：docs/ 与 plans/ 共 ${ok} 份产出物命名标准，` +
    `README 目录地图 ${mapCount} 条路径全部存在`
);
