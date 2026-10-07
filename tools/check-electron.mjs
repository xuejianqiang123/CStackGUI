/**
 * @file    check-electron.mjs
 * @brief   画布主进程（designer\electron\*.mjs）的语法门禁
 * @details 为什么需要它：
 *
 *   `npx tsc --noEmit` 只看 `.ts`（`electron\*.mjs` 不在 tsconfig 的 include 里），
 *   `npx vite build` 只打包渲染进程 —— 所以**主进程的 .mjs 此前没有任何语法检查**。
 *
 *   2026-10-07 实测后果：一句块注释里写了 `_ui_events.*` 紧跟 `/`（想表达"那个目录"），
 *   注释被那个字符对提前闭合，后面半句变成代码 —— 画布重新打包后**开机即崩**
 *   （`Uncaught SyntaxError: Unexpected token`），而当时
 *   build-all / run-tests / check-format / tsc / vite **全都是绿的**。
 *
 *   本脚本对每个 .mjs 跑一次 `node --check`：只做语法检查，不执行、不加载 electron。
 * 用法：node tools\check-electron.mjs [目录]   （默认 designer\electron；带目录是为了能拿
 *       一个故意写坏的文件来验反例 —— 自建门禁必须正反例各跑一次）
 * 退出码：0 = 全部通过；1 = 有文件语法错误
 */
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = process.argv[2] ? path.resolve(process.argv[2]) : path.join(root, "designer", "electron");
const files = fs.existsSync(dir)
  ? fs.readdirSync(dir).filter((f) => f.endsWith(".mjs")).sort()
  : [];

if (!files.length) {
  console.log("[fail] designer\\electron 下找不到 .mjs —— 目录搬了？本检查要跟着改。");
  process.exit(1);
}

const bad = [];
for (const f of files) {
  const r = spawnSync(process.execPath, ["--check", path.join(dir, f)], { encoding: "utf8" });
  if (r.status !== 0) {
    const why = `${r.stderr || ""}`.trim().split(/\r?\n/).slice(0, 4).join("\n");
    bad.push(`[fail] designer/electron/${f}\n${why}`);
  }
}

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 画布主进程语法不通过：${bad.length}/${files.length}`);
  process.exit(1);
}
console.log(`[ok] electron 主进程语法成立：${files.length} 个 .mjs（tsc 与 vite 都看不到这些文件）`);
