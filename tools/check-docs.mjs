/**
 * @file    check-docs.mjs
 * @brief   产出物路径判据：PRD / SDD / 切片只认 docs/ 下的标准命名，plans/ 只放实现计划
 * @details 为什么要有它：
 *
 *   `README.md` §7 与 `global-rule.md` §9 都写着「PRD → `docs/PRD_<feature>.md`」，
 *   但 2026-11-01 实测：`docs/` 下**一个 PRD 都没有**，5 份 PRD/SDD 全躺在 `.codebuddy/plans/`
 *   里、还带着 `-prd.md` / `-sdd.md` 后缀。规范与现实的差距，**没有任何命令会发现** ——
 *   直到有人按规范去 docs/ 找，找不到。
 *
 *   所以判据必须双向：
 *     A. `docs/` 下不许再有旧命名（`*-prd.md` / `*-sdd.md` / `*-slices.md`）；
 *     B. `.codebuddy/plans/` 下不许出现 PRD/SDD/切片（不管新旧命名）—— 那里只放实现计划；
 *     C. `docs/` 下带 `PRD_/SDD_/SLICES_` 前缀的文件名必须是小写连字符风格
 *        （`PRD_multi-window.md` 对，`PRD_MultiWindow.md` / `PRD_多窗体.md` 不对）。
 *
 *   为什么不判「PRD 必须有配对 SDD」：不少 feature 只有 PRD（想法已定、还没设计），
 *   强制配对会逼人写空壳 SDD —— 那是拿形式换内容。
 *
 * 用法：node tools\check-docs.mjs
 * 退出码：0 = 路径合规；1 = 有越界或命名不合规
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

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 产出物路径：${bad.length} 处越界或命名不合规`);
  process.exit(1);
}

console.log(
  `[ok] 产出物路径合规：docs/ 与 plans/ 共 ${ok} 份，PRD/SDD/切片 都在 docs/ 且命名标准`
);
