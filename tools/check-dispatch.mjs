/**
 * @file    check-dispatch.mjs
 * @brief   把"改了就红"的运行时契约锁住：事件/字段 必须真的落到原生侧
 * @details 为什么要有这个脚本：
 *
 *   2026-09-23 的排查挖出一整类缺陷 —— **声明了却不生效**。它们的共同点是：
 *     · 类型表/文档都承诺支持（`cg_doc_kind.c` 的掩码、`CGUI-FORMAT` 的事件列），
 *     · 但原生侧从来没有那条路径（没派发、没作用到 HWND、或被提前 return 跳过）；
 *     · 而 `--selftest` 是**直接调函数指针**，对这种缺陷天生失明 —— 门禁全绿。
 *   四个实例（image 的 click、edit 的 click/dblclick、禁用态漏 native2、列表下标被 [0,100] 夹取）
 *   全靠人工"解析点 × 事件掩码 × 原生作用点"三方对照才挖出来。
 *
 *   这张表就是那次排查的沉淀：每一条 = 一个**改了就出事**的具体代码形态。
 *   它不是"风格检查"，而是**回归锁** —— 谁把某条路径删了/绕过了，这里立刻红，
 *   并且会告诉他"这条路径当年为什么存在"。
 *
 *   判定方式故意做得很土（子串 + 函数体切分），换来的是零依赖、零误报源：
 *   规则只在"确实找不到"时报警；改写了别处的等价实现时，把规则一起更新即可。
 *
 * 用法：node tools\check-dispatch.mjs
 * 退出码：0 = 全部契约在；1 = 有契约被破坏（并列出每条的来历）
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** 读文件并按 \n 归一（仓库里 .c 是 CRLF，:145 之类的 token 里不含换行，归一只是为了稳妥） */
function read(rel) {
  return fs.readFileSync(path.join(root, rel), "utf8").replace(/\r\n/g, "\n");
}

/** 递归列出 runtime\src 下所有 .c（契约按函数名定位，不按文件路径 —— 文件是会被拆的） */
function listRuntimeSources() {
  const base = path.join(root, "runtime", "src");
  const out = [];
  const walk = (dir) => {
    if (!fs.existsSync(dir)) return;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else if (e.name.endsWith(".c")) out.push(path.relative(root, p).replace(/\\/g, "/"));
    }
  };
  walk(base);
  return out;
}

/**
 * 取一个函数体：从含 `name(` 的那一行开始，到第一个**顶格 `}`** 结束。
 * 这个仓库里函数结尾的 `}` 都顶格（col 0），所以不需要真正的括号匹配 ——
 * 也就不会被字符串/注释里的花括号骗到。
 * @returns {string|null} 函数体文本；找不到返回 null
 */
function bodyOf(src, name) {
  const lines = src.split("\n");
  /* 只认**定义行**：顶格起（col 0）、类型在前、且不是以分号结尾的声明/调用。
   * 这个工程的函数定义一律写成 `ret-type name(args)` 顶格起、`{` 换行 ——
   * 少了"顶格 + 无分号"这两条，调用点（缩进的 `foo(...)`）会被误当成定义：
   * 首版就这么错过（cg_widget_set_value 一度被 6 个文件"同时定义"）。 */
  const re = new RegExp(`^[A-Za-z_][A-Za-z0-9_ \\t*]*\\b${name}\\s*\\(`);
  let i = lines.findIndex((l) => re.test(l) && !/;\s*$/.test(l));
  if (i < 0) return null;
  const start = i;
  for (; i < lines.length; i++) {
    if (/^\}\s*$/.test(lines[i])) return lines.slice(start, i + 1).join("\n");
  }
  return lines.slice(start).join("\n");
}

/* ------------------------------------------------------------------ *
 * 契约表
 *
 *   scope: "func"  →  { file, func, must: [...] }   只在函数体里找
 *   scope: "file"  →  { file, must: [...] }         在整个文件里找
 *   must 里的每个 token 都必须出现；缺任何一个即失败。
 *   order（可选）   →  [a, b] 表示 a 必须先于 b 出现。用于"必须在 X 之前"这类契约
 *                     （典型：加速键必须排在 IsDialogMessage 之前，反了就是焦点在输入框里
 *                      Ctrl+S 按不出来 —— 而不报错、不崩）。
 *   why：这条路径**当年为什么存在**（谁删了就来这里看）
 * ------------------------------------------------------------------ */
const RULES = [
  {
    scope: "func", file: "runtime/src/win32/cg_win_draw.c", func: "ImageProc",
    must: ["WM_LBUTTONUP", "CG_EVENT_CLICK"],
    why: "image 的 click/dblclick 是类型表承诺过的，但 IMAGE_CLASS 自己不发任何通知 —— "
       + "必须在 WM_LBUTTONUP 里补派发一次 click（dblclick 再由 cg_dispatch 合成）。"
       + "2026-09-23 之前这里是空的：给图片绑 click 永远不触发。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_event.c", func: "cgw_FocusProc",
    must: ['cgw_is_t(w, "edit")', "CG_EVENT_CLICK", "* 100 / sc"],
    why: "edit 声明了 dblclick，但 EDIT 既不发 BN_CLICKED、也不产生 WM_LBUTTONDBLCLK"
       + "（该类没有 CS_DBLCLKS）→ 双击合成路径（挂在 CLICK 上）永不触发。所以子类要对 edit "
       + "的左键抬起补一次 click。同一函数还负责把控件级鼠标/滚轮坐标按 DPI 折回**逻辑像素**。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_sync.c", func: "sync_widget_body",
    must: ["EnableWindow(w->native2"],
    why: "enabled:false 必须连第二个原生窗口（数值框的调节钮 UPDOWN、datetime 的第二个 DTP）"
       + "一起禁用。只禁主 HWND 时，禁用态下点箭头仍能改值，而那条路径（UDN_DELTAPOS）"
       + "直接改 value、不走 cg_dispatch，连回调都不发。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_sync.c", func: "browser_recreate",
    must: ["apply_visible(", "EnableWindow("],
    why: "browser 换地址会整个重建 HWND，而建窗只带 WS_VISIBLE —— 所以重建后必须把"
       + "「该不该显示 / 该不该可点」重落一遍。调用方建完就 return，走不到 sync 末尾那两句。",
  },
  {
    scope: "func", file: "runtime/src/core/cg_widget.c", func: "cg_widget_set_value",
    must: ["w->nitems - 1"],
    why: "列表框/下拉框/标签页的 value 是「第几项」，范围 0..项数-1，不是默认的 [0,100]。"
       + "不区分的话，超过 100 项的列表通过公共 API 永远选不中后段。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_util.c", func: "cgw_font_for",
    must: ["underline ? 1 : 0, st->dpi"],
    why: "字体缓存的 key 必须带 dpi：height 是按 dpi 算的，跨屏 DPI 变了若命中旧缓存，"
       + "字号就停在旧比例。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_main.c", func: "fill_mouse",
    must: ["* 100 / sc"],
    why: "事件坐标对业务是**逻辑像素**（与 .cgui、控件坐标同一套）：消息给的是物理像素，"
       + "不折回的话，缩放屏上按坐标画图/算布局会差 1.x 倍。",
  },
  {
    scope: "func", file: "runtime/src/skin/cg_skin.c", func: "cg_skin_draw_progress",
    must: ["w->style.fg"],
    why: "进度条的填充色必须尊重 style.fg（与 progressring / meter / led 同一口径）。"
       + "以前两处（GDI+ 与 GDI 回退）都写死成 M3 主色，于是画布上给进度条设「文字色」"
       + "看不出任何变化 —— 典型的「画布能设、运行时不消费」。",
  },
  {
    scope: "file", file: "runtime/src/win32/cg_win_window.c",
    must: ["GetDpiForSystem", "cgw_scale_px(app, app->doc.width)"],
    why: "主窗口客户区尺寸必须和控件一样过 DPI 缩放：漏了的话 125%/150% 下窗口还是 1× 大小、"
       + "控件按 1.x 摆放，右侧与下侧整片被裁。",
  },
  {
    scope: "file", file: "runtime/src/win32/cg_win_main.c",
    must: ["app->window->ev_x = (int)(short)LOWORD(lp) * 100 / sc;"],
    why: "size 事件的宽高也要折回逻辑像素 —— 这样业务在 size 回调里读到的 w×h 可以原样回喂"
       + "cg_win_set_size()，与 .cgui 是同一套单位。",
  },
  {
    scope: "file", file: "runtime/src/win32/cg_win_sync.c",
    must: ['"statusbar")) build_status'],
    why: "状态栏分段是按**当前客户区宽**算出来的绝对像素，不能只在 items_dirty 时重建 ——"
       + "否则窗口一变宽，前几段的分隔线停在旧位置（只有最后一段 -1 会自己撑开）。",
  },
  {
    scope: "file", file: "make-release.bat",
    must: ["vite build", "release\\runtime\\lib\\libcstackgui.a"],
    why: "发版必须先构建画布前端（electron-builder 不会替你跑 vite build，dist 又被 .gitignore "
       + "忽略，漏了就白屏或静默发旧 UI），并且断言运行时库真的进了包（包里缺它，用户编不了任何东西）。",
  },
  {
    scope: "file", file: "tools/check-kit.bat",
    must: ["runtime\\lib\\libcstackgui.a"],
    why: "发布包的库在 runtime\\lib\\（包里**没有** build\\lib\\ 也没有 build-runtime.bat）——"
       + "只认源码树布局的话，发布包自带的自检结构上永远过不去。",
  },
  {
    // 2026-10-07：编译逻辑从 packaging\build-app.bat 搬到内核 tools\cgbuild.mjs
    // （发布包那份现在只是转发到 CStackGUI.exe --build 的 ASCII 薄壳），
    // 所以这条契约跟着指向内核 —— 它才是两份包真正共用的那一份。
    scope: "file", file: "tools/cgbuild.mjs",
    must: ["-luuid", "-lole32", "-loleaut32"],
    why: "发布版和源码版的链接库要一致：browser（WebBrowser 控件 = OLE/COM）需要 "
       + "-luuid -lole32 -loleaut32，少了它们在发布包里编带 browser 的工程会 undefined reference。",
  },
  {
    scope: "func", file: "runtime/src/core/cg_event.c", func: "cg_dispatch",
    must: ["CG_EVENT_CLICK && w->action", "cg_action_invoke(w->app, w->action)"],
    why: "引用了动作（.cgui 的 action）的控件，click 必须在**派发中枢**分流到动作。"
       + "放在这里，每一条产生 click 的路径（BN_CLICKED、自绘控件在 WM_LBUTTONUP 里的补派发、"
       + "菜单项的命令映射）都自动走同一条路；改放到某个后端调用点上，就会有控件"
       + "「画布上设了 action，点它却还是走自己那份 click」。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_window.c", func: "win_run",
    must: ["TranslateAcceleratorW((HWND)app->hwnd, st->accel, &msg))", "IsDialogMessageW"],
    order: ["TranslateAcceleratorW", "IsDialogMessageW"],
    why: "动作的快捷键靠**消息循环里的加速键表**生效（TranslateAccelerator），"
       + "而且**必须排在 IsDialogMessage 之前**：要处理的正是那些会被焦点控件吃掉、"
       + "或被 Tab/回车导航截走的按键。顺序反了的表现是「焦点在输入框里 Ctrl+S 没反应」——"
       + "不报错、不崩，只能靠人发现（窗口级 keydown 事件做不到这件事：EDIT 会吃掉按键）。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_main.c", func: "cgw_MainProc",
    must: ["WM_SYSCOMMAND", "cg_action_invoke_cmd"],
    why: "带 ALT 的加速键由 TranslateAccelerator 以 **WM_SYSCOMMAND** 发回来（不是 WM_COMMAND）。"
       + "少处理这一路，Alt 组合的快捷键永远不生效，而它会被归因成「快捷键坏了」。",
  },
  {
    scope: "func", file: "runtime/src/win32/cg_win_menu.c", func: "cg_win_action_sync",
    must: ["EnableMenuItem", "CheckMenuItem"],
    why: "动作的启用/勾选必须**真落到 HMENU 上**（EnableMenuItem / CheckMenuItem）。"
       + "只改内存里的 enabled 字段、不落到菜单，就是本项目最典型的那类缺陷："
       + "「按钮灰了、菜单项还是亮的」，而代码看起来完全正确。",
  },
];

/* ------------------------------------------------------------------ */

let bad = 0;
let located = 0;   /* 按函数名定位成功的条数（顺带证明「文件可拆、契约不散」） */
const sources = new Map();
function srcOf(rel) {
  if (!sources.has(rel)) {
    if (!fs.existsSync(path.join(root, rel))) return null;
    sources.set(rel, read(rel));
  }
  return sources.get(rel);
}

for (const r of RULES) {
  /* func 类契约**按函数名**在整个 runtime/src 下定位：文件是可以被拆的 —— 2026-09-23 把
   * cg_win_draw.c 拆成四个文件时就踩到"规则里的路径写死"这个坑。规则里的 file 字段
   * 只作"它当初住在哪"的备注，定位不依赖它。 */
  let where = null;
  let at = r.file;

  if (r.scope === "func") {
    const hits = listRuntimeSources().filter((f) => bodyOf(read(f), r.func) !== null);
    if (hits.length === 0) {
      console.log(`[fail] 在 runtime/src 下找不到函数 ${r.func}()（契约原来记在 ${r.file}）`);
      console.log(`       它承载着：${r.why}`);
      bad++;
      continue;
    }
    if (hits.length > 1) {
      console.log(`[fail] 函数 ${r.func}() 在多个文件里都有定义：${hits.join(" / ")}`);
      console.log("       静态函数不该重名 —— 重名会让契约指向不确定的实现，请先合并或改名。");
      bad++;
      continue;
    }
    where = bodyOf(read(hits[0]), r.func);
    at = `${hits[0]} 的 ${r.func}()`;
    located++;
  } else {
    const src = srcOf(r.file);
    if (src === null) {
      console.log(`[fail] ${r.file} 不存在（契约：${r.why.split("。")[0]}）`);
      bad++;
      continue;
    }
    where = src;
  }

  for (const token of r.must) {
    if (!where.includes(token)) {
      console.log(`[fail] ${at} 里找不到「${token}」`);
      console.log(`       这条路径为什么必须在：${r.why}`);
      bad++;
    }
  }

  /* 顺序契约：a 必须先于 b 出现。两个 token 都在才判 —— 缺一个的话上面那条已经报了，
   * 这里不重复刷屏。 */
  if (r.order && r.order.length >= 2) {
    const pos = r.order.map((t) => where.indexOf(t));
    if (pos.every((p) => p >= 0)) {
      for (let i = 1; i < pos.length; i++) {
        if (pos[i - 1] < pos[i]) continue;
        console.log(`[fail] ${at} 里「${r.order[i - 1]}」必须出现在「${r.order[i]}」之前（现在是反的）`);
        console.log(`       这条路径为什么必须在：${r.why}`);
        bad++;
      }
    }
  }
}

if (bad > 0) {
  console.log(`\n[fail] ${bad} 条运行时契约被破坏（共 ${RULES.length} 条）`);
  console.log("       这些不是风格问题：每一条都对应一类「声明了却不生效」的真实缺陷。");
  process.exit(1);
}
console.log(
  `[ok] ${RULES.length} 条"改了就红"的运行时契约都还在（事件/字段 -> 原生作用点；` +
    `其中 ${located} 条按**函数名**定位，与文件路径无关）`,
);
