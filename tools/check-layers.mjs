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
 *        （build-runtime.bat:62），撞名会互相覆盖且没有任何报错；
 *     6) docs/ARCHITECTURE.md 的 runtime 文件清单与真实文件互为镜像；
 *     7) **业务层（examples/、apps/）不得 include Windows 头** —— "业务代码只 include
 *        cstackgui.h"是 README 里写明的卖点（换台机器复制过去就能跑、画布「支持库」页签
 *        查得到），但它一直只是"约定"：2026-09-27 实测现状干净（examples/apps 下一个
 *        windows.h 都没有），干净就更该锁 —— 从网上抄一段带 <windows.h> 的代码进来，
 *        编译器不会说任何话；
 *     8) **运行时的工作线程只许出现在白名单文件**（cg_serial.c / cg_net.c /
 *        cg_win_gif.c，各自理由见第 8 段检查的注释）—— 读线程只把字节搬进环形
 *        缓冲、业务在时钟回调里取走，这条线一破就是"控件被别的线程碰"的隐性数据竞争，
 *        任何单测都测不出来（2026-09-27 实测只有这两个文件在 CreateThread；
 *        2026-10-17 扩入录屏编码线程，它不碰控件与 app）。
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
          `core 保持平台中立是"core+kit 逻辑与 Win32 视图解耦"这条分层卖点的基石` +
          `（2026-10-17 表述收敛：目前只有 win32 一个后端，"可跨平台"是设计属性不是已验证事实）`,
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

/* 6) 文件清单与真实文件互为镜像（docs/ARCHITECTURE.md 里有逐文件的 runtime 清单）
 *
 *    这条是被真实事故推出来的：2026-09-23 把 cg_win_draw.c 拆成四个文件后，清单里那行
 *    "cg_win_draw.c 自绘窗口类与过程"立刻就谎了 —— 读者按清单找代码会找不到。
 *    两个方向都查，且只认 `cg_*.c` / `cg_*.h` 这种运行时命名（例子里的 main.c、
 *    build-app.bat 之类不会误报）。因为"提到即算列出"，正文里的引用也算，够用且不脆。
 */
{
  const docPath = path.join(root, "docs", "ARCHITECTURE.md");
  if (fs.existsSync(docPath)) {
    const doc = fs.readFileSync(docPath, "utf8");
    /* `cg_skin.{h,c}` 这种花括号写法展开成 cg_skin.h / cg_skin.c */
    const listed = new Set();
    for (const m of doc.matchAll(/cg_[a-z0-9_]*\.\{h,c\}/g)) {
      const base = m[0].replace(/\.\{h,c\}$/, "");
      listed.add(`${base}.h`);
      listed.add(`${base}.c`);
    }
    for (const m of doc.matchAll(/cg_[a-z0-9_]+\.(c|h)\b/g)) listed.add(m[0]);

    const actual = new Set();
    for (const dir of ["runtime/src", "runtime/include"]) {
      for (const f of listFiles(path.join(root, dir), [".c", ".h"])) {
        const base = path.basename(f);
        /* 公共头（include/ 段）在文档里由专门一节说明，不以 cg_ 命名，单列进白名单 */
        if (base === "cstackgui.h") continue;
        actual.add(base);
      }
    }

    checks++;
    for (const name of actual) {
      if (!listed.has(name)) {
        problems.push(
          `docs/ARCHITECTURE.md 的文件清单里没有 ${name} ——` +
            ` 那份清单是读者找代码的入口，新文件要么补进去、要么说明它为什么不用列`,
        );
      }
    }
    checks++;
    for (const name of listed) {
      if (!actual.has(name)) {
        problems.push(
          `docs/ARCHITECTURE.md 列了 ${name}，但 runtime/ 下没有这个文件（改名/删除后清单没跟上）`,
        );
      }
    }
  }
}

/* 7) 业务层（examples/、apps/）不得 include Windows 头
 *
 *    "业务代码从头到尾只 include cstackgui.h"是 README §1.2 写明的卖点：
 *    界面之外的能力（文件/配置/编码/剪贴板/注册表/串口/网口）都由 cg_kit 提供，
 *    画布「支持库」页签也是照 cstackgui.h 生成的。一旦业务代码自己 include <windows.h>：
 *      ① 它绑死在 Win32 上（这个工程的分层卖点当场作废）；
 *      ② 画布「支持库」里查不到那些函数，用户只能去翻 MSDN；
 *      ③ `tools/check-support.mjs` 的一致性也失去了意义。
 *
 *    为什么现在才加：2026-09-27 实测现状是干净的（examples/apps 下的 .c/.h 一个
 *    windows.h 都没有），而"干净"正是锁它的最好时机 —— 抄一段网上的 Win32 片段进来，
 *    编译器不会说任何话。
 *
 *    范围：只扫 .c / .h。.rc（gallery.rc、cstackgui.rc）**必须** include <windows.h>
 *    才写得出 VS_VERSION_INFO，天然豁免。
 */
{
  const WIN_ONLY_HEADERS = [
    "windows.h", "windowsx.h", "winuser.h", "wingdi.h", "winbase.h", "windef.h",
    "commctrl.h", "commdlg.h", "shellapi.h", "shlobj.h", "gdiplus.h", "uxtheme.h",
    "dwmapi.h", "tchar.h", "richedit.h", "winsock2.h", "ws2tcpip.h",
    "atlbase.h", "atlwin.h", "mmsystem.h",
  ];
  for (const dir of ["examples", "apps"]) {
    for (const f of listFiles(path.join(root, dir), [".c", ".h"])) {
      for (const inc of includes(f)) {
        checks++;
        if (!WIN_ONLY_HEADERS.includes(inc.name.toLowerCase())) continue;
        problems.push(
          `${rel(f)}:${inc.n} — 业务代码不能 include <${inc.name}>；` +
            `业务只 include cstackgui.h（界面之外的能力在 cg_kit 里）——` +
            `确实需要原生能力就把它加到 runtime/src/kit 并暴露成 cg_* 函数，` +
            `那样画布「支持库」页签也查得到`,
        );
      }
    }
  }
}

/* 8) 运行时的工作线程只许出现在白名单里的文件
 *
 *    ARCHITECTURE.md §8 把这条写成了硬约束："运行时的工作线程只出现在这三个文件里"。
 *    它存在的原因是**控件不是线程安全的**：读线程只把字节搬进环形缓冲，业务必须在
 *    cg_set_timer 的时钟回调里取走。在别处悄悄开一条线程去碰控件，是那种"跑一万次
 *    才崩一次"的数据竞争，单测与 --selftest 都照不到 —— 只有这条静态断言拦得住。
 *
 *    白名单三个文件与各自理由（扩白名单必须在这里写清"它为什么不碰控件"）：
 *      cg_serial.c  串口读线程：只搬字节进 64KB 环形缓冲
 *      cg_net.c     TCP 读线程：同上（UDP 不开线程）
 *      cg_win_gif.c 录屏编码线程（2026-10-17 扩）：量化 + LZW + 写盘一帧几十毫秒，
 *                   塞在 UI 线程 50ms 心跳里必掉帧还拖死输入响应（架构评审整改）。
 *                   它只碰两块抓帧缓冲、编码器与文件；抓帧（要读 app 的 DPI）仍在
 *                   UI 线程，交接走 cs 保护的乒乓缓冲 —— 控件与 app 都不跨线程。
 */
{
  const THREAD_ALLOWED = ["cg_serial.c", "cg_net.c", "cg_win_gif.c"];
  const THREAD_API = /\b(CreateThread|_beginthreadex|_beginthread|CreateRemoteThread)\s*\(/;
  for (const f of listFiles(SRC, [".c"])) {
    const base = path.basename(f);
    if (THREAD_ALLOWED.includes(base)) continue;
    const lines = fs.readFileSync(f, "utf8").split(/\r?\n/);
    lines.forEach((line, i) => {
      if (!THREAD_API.test(line)) return;
      checks++;
      problems.push(
        `${rel(f)}:${i + 1} — 运行时的工作线程只允许出现在 ${THREAD_ALLOWED.join(" / ")}；` +
          `控件不是线程安全的，读线程只能把字节搬进环形缓冲、业务在时钟回调里取走` +
          `（见 docs/COMM.md）。要在这里开线程，先想清楚谁在别的线程里碰控件`,
      );
    });
  }
}

if (problems.length > 0) {
  console.log(`[fail] 分层契约被破坏（${problems.length} 处 / 共检查 ${checks} 项）\n`);
  for (const p of problems) console.log(`  - ${p}`);
  process.exit(1);
}
console.log(
  `[ok] 分层契约成立：共检查 ${checks} 项（core 平台中立 / win32 只走 cg_win_internal.h / ` +
    `skin 不反向依赖 / kit 不依赖界面 / ${byName.size} 个 .c 基名唯一 / ` +
    `业务层不 include Windows 头 / 工作线程只在 cg_serial.c·cg_net.c·cg_win_gif.c）`,
);
