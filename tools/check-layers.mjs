/**
 * @file    check-layers.mjs
 * @brief   分层契约：四层（core / win32 / skin / kit）的 include 方向与文件基名唯一性
 * @details 为什么要有这个脚本：
 *
 *   这套工程的分层**一直是"约定"而不是"机制"** —— 头文件里写着"子模块之间只允许通过本头
 *   互相调用"，但没有东西在拦。2026-09-23 的结构审查实测：四条不变量当前**全部成立**，
 *   而且成立得很干净（core 的 .c 一律不 include windows.h，win32 的 15 个文件对项目头的
 *   include 有且只有一个 cg_win_internal.h）。越干净越值得锁 —— 一次图省事的 include
 *   就能把"core 可跨平台 / 后端可替换"这两个卖点悄悄毁掉，而且**编译器不会报警**。
 *
 *   本脚本只做静态断言，不修任何东西：
 *     1) core 不得 include win32/skin 的头，也不得 include <windows.h>（保持平台中立）；
 *     2) win32 的 .c 允许的项目头只有 cg_win_internal.h（不许 include 兄弟文件的头）；
 *     3) skin 不得 include cg_win_internal.h（它比 win32 低一层）；
 *     4) kit 不得 include win32/skin 的头；
 *     5) 四层的 .c 文件基名不得重复 —— build-runtime.bat 用"文件基名"做 .o
 *        （build-runtime.bat:62），撞名会互相覆盖且没有任何报错。
 *
 * 用法：node tools\check-layers.mjs
 * 退出码：0 = 全部成立；1 = 有越界（逐条给出文件:行号与它为什么重要）
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SRC = path.join(root, "runtime", "src");

/** 递归列出某目录下的文件（按扩展名过滤） */
function listFiles(dir, exts) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) return listFiles(p, exts);
    return exts.some((x) => e.name.endsWith(x)) ? [p] : [];
  });
}

/** 取一个 C 文件里的所有 include（行号 + 原文 + 是否项目头） */
function includes(file) {
  return fs
    .readFileSync(file, "utf8")
    .split(/\r?\n/)
    .map((line, i) => ({ n: i + 1, line: line.trim() }))
    .filter((x) => /^#\s*include/.test(x.line))
    .map((x) => {
      const q = /^#\s*include\s+"([^"]+)"/.exec(x.line);
      const a = /^#\s*include\s+<([^>]+)>/.exec(x.line);
      return { n: x.n, name: (q ? q[1] : a ? a[1] : ""), quoted: !!q, raw: x.line };
    });
}

const rel = (p) => path.relative(root, p).replace(/\\/g, "/");
const problems = [];
let checks = 0;

const WIN_HEADERS = ["cg_win_internal.h"];
const WIN32_ONLY = ["cg_skin.h", "cg_win_internal.h"];

/* 1) core：平台中立 —— 不碰 win32/skin 的头，也不 include windows.h */
for (const f of listFiles(path.join(SRC, "core"), [".c", ".h"])) {
  for (const inc of includes(f)) {
    checks++;
    if (WIN_HEADERS.includes(inc.name) || inc.name === "cg_skin.h") {
      problems.push(
        `${rel(f)}:${inc.n} — core 不能 include "${inc.name}"（反向依赖）；` +
          `core 必须平台中立，win32/skin 只能通过 cg_internal.h 里声明的"端口"被 core 调用`,
      );
    }
    if (inc.name === "windows.h") {
      problems.push(
        `${rel(f)}:${inc.n} — core 不能 include <windows.h>；` +
          `core 是跨平台层，"能在 Linux 上编译 core"是这个工程的分层卖点之一`,
      );
    }
  }
}

/* 2) win32：对项目头的 include 只允许 cg_win_internal.h */
for (const f of listFiles(path.join(SRC, "win32"), [".c"])) {
  for (const inc of includes(f)) {
    checks++;
    if (!inc.quoted) continue;
    if (!WIN32_ONLY.includes(inc.name)) {
      problems.push(
        `${rel(f)}:${inc.n} — win32 只能 include "cg_win_internal.h"（当前是 "${inc.name}"）；` +
          `兄弟模块之间必须走共享契约头，否则改一处会牵动一片`,
      );
    }
  }
}

/* 3) skin：不得 include win32 的头 */
for (const f of listFiles(path.join(SRC, "skin"), [".c", ".h"])) {
  for (const inc of includes(f)) {
    checks++;
    if (WIN_HEADERS.includes(inc.name)) {
      problems.push(
        `${rel(f)}:${inc.n} — skin 不能 include "${inc.name}"；` +
          `skin 比 win32 低一层（外观内核可整目录替换），反向 include 会让替换变成不可能`,
      );
    }
  }
}

/* 4) kit：不得 include win32/skin 的头 */
for (const f of listFiles(path.join(SRC, "kit"), [".c", ".h"])) {
  for (const inc of includes(f)) {
    checks++;
    if (WIN_HEADERS.includes(inc.name) || inc.name === "cg_skin.h") {
      problems.push(
        `${rel(f)}:${inc.n} — kit 不能 include "${inc.name}"；` +
          `kit 是独立工具层（文件/网络/串口），不该依赖界面后端`,
      );
    }
  }
}

/* 5) 四层的 .c 基名唯一（build-runtime.bat 用基名做 .o） */
const byName = new Map();
for (const dir of ["core", "win32", "skin", "kit"]) {
  for (const f of listFiles(path.join(SRC, dir), [".c"])) {
    const base = path.basename(f);
    if (!byName.has(base)) byName.set(base, []);
    byName.get(base).push(rel(f));
  }
  checks++;
}
for (const [base, files] of byName) {
  if (files.length > 1) {
    problems.push(
      `重名 .c：${base} —— ${files.join(" / ")}；` +
        `build-runtime.bat:62 用文件基名做 .o，同名会互相覆盖且没有任何报错`,
    );
  }
}

if (problems.length > 0) {
  console.log(`[fail] 分层契约被破坏（${problems.length} 处 / 共检查 ${checks} 项）\n`);
  for (const p of problems) console.log(`  - ${p}`);
  process.exit(1);
}
console.log(
  `[ok] 分层契约成立：共检查 ${checks} 项（core 平台中立 / win32 只走 cg_win_internal.h / ` +
    `skin 不反向依赖 / kit 不依赖界面 / ${byName.size} 个 .c 基名唯一）`,
);
