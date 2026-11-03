/* check-examples.mjs -- 控件类型覆盖闸门：每种类型都得在 examples/ 里有落点
 *
 * 为什么需要它：示例就是"控件目录"的说明书。加了新类型却没有任何示例用它 = 读者找不到用法，
 * 时间一长大家会以为这个控件不存在（`box`/`meter` 就这么被漏掉过）。
 * 类型真相在 runtime/src/core/cg_doc_kind.c 的 k_kinds 表，这里只做"对表"这件事，不猜。
 *
 * 用法（在项目根）：
 *   node tools\check-examples.mjs                扫类型表 + 扫 examples/**\*.cgui，缺落点就报错（退出码 1）
 *   node tools\check-examples.mjs --table        打印 Markdown 类型映射表（生成文档索引表用，别手抄）
 *   node tools\check-examples.mjs --lesson-table 打印"教学示例事实表"（课/目录/主题/规模/控件清单，自动生成）
 *   node tools\check-examples.mjs --ex-index    打印 examples\README.md 的自动索引（六课 / 四套件 / 独立工程）
 *   node tools\check-examples.mjs --docs         回校文档里的"课数 / 示例范围 / 篇数"声明与那张事实表
 *   node tools\check-examples.mjs --lessons      只认教学示例目录（lessonNN_*）：这是"控件分类课"的验收标准
 *   node tools\check-examples.mjs --gallery      只认 gallery（"一页看全"那句话得是真的）
 *
 * 说明：
 *   - 伪类型 `window`（窗口本身也是一个"控件"，事件写在 app.events）不算，其余全部要落点。
 *   - 只看 examples/：apps/ 是用户自己的工程，不该被这个闸门约束。
 *   - `--lesson-table` / `--docs` 的动机：课数、示例范围、每课规模这些事实散落在十来个文档里手写，
 *     加一课/改一次目录名就得靠人记着全改（2026-09-23 体检一次漏了 31 处）。现在它们由这里生成 + 回校。
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const KIND_FILE = path.join(root, "runtime", "src", "core", "cg_doc_kind.c");
const EX_DIR = path.join(root, "examples");

const args = process.argv.slice(2);
const wantTable = args.includes("--table");
const wantLessonTable = args.includes("--lesson-table");
const wantExIndex = args.includes("--ex-index");
const wantDocs = args.includes("--docs");
const onlyGallery = args.includes("--gallery");
const onlyLessons = args.includes("--lessons");
if (onlyGallery && onlyLessons) {
  console.error("[fail] --gallery 与 --lessons 只能选一个");
  process.exit(2);
}
const SCOPE = onlyGallery ? "gallery" : onlyLessons ? "lessons" : "examples";

/** 从 k_kinds 表里取全部类型名（排除伪类型 window） */
function readTypes() {
  const src = fs.readFileSync(KIND_FILE, "utf8");
  const types = [];
  const re = /^\s*\{\s*"([a-z0-9_]+)"\s*,\s*CG_K_/gm;
  let m;
  while ((m = re.exec(src)) !== null) {
    if (m[1] !== "window") types.push(m[1]);
  }
  return types;
}

/** 递归收集一个 .cgui 文档里用到的控件类型 */
function collectTypes(node, out) {
  if (!node || typeof node !== "object") return;
  if (Array.isArray(node)) {
    for (const it of node) collectTypes(it, out);
    return;
  }
  if (typeof node.type === "string" && node.type) out.add(node.type);
  for (const key of ["widgets", "children"]) collectTypes(node[key], out);
}

/** 扫描 examples/ 下全部 .cgui，返回 { 类型 -> Set(相对路径) } 与文件数 */
function scanExamples() {
  const map = new Map();
  let files = 0;
  const walk = (dir) => {
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        if (ent.name === "lib" || ent.name === "assets") continue;   // 跳过编译产物与素材
        walk(p);
        continue;
      }
      if (!ent.name.endsWith(".cgui")) continue;
      files += 1;
      const rel = path.relative(root, p).replace(/\\/g, "/");
      let doc;
      try {
        doc = JSON.parse(fs.readFileSync(p, "utf8"));
      } catch (e) {
        console.error(`[fail] ${rel} 不是合法 JSON：${e.message}`);
        process.exit(1);
      }
      const used = new Set();
      collectTypes(doc.widgets, used);
      for (const t of used) {
        if (!map.has(t)) map.set(t, new Set());
        map.get(t).add(rel);
      }
    }
  };
  walk(EX_DIR);
  return { map, files };
}

const types = readTypes();
const { map, files } = scanExamples();

/* ================= 教学示例的"事实表"与文档口径回校 =================
 * 动机：课数、示例范围、每课规模这些事实被手写散落在十来个文档里，加一课或改一次目录名，
 * 就得靠人记着全改一遍（2026-09-23 体检一次漏了 31 处）。这里把它们变成"从目录 + .cgui + 自检"
 * 生成，文档只允许贴生成结果，并由 --docs 回校。 */

const LESSON_RE = /^lesson(\d\d)_([a-z0-9_]+)$/;
const LESSON_DOCS = [
  "README.md",
  "examples/教学使用说明.md",
  "packaging/README.release.md",
  "docs/ARCHITECTURE.md",
];
const ARTICLES_DIR = path.join(root, "docs", "articles");
const CN_NUM = { 一: 1, 二: 2, 三: 3, 四: 4, 五: 5, 六: 6, 七: 7, 八: 8, 九: 9, 十: 10 };

/**
 * 教学六课套件（2026-10-18 合并）：六课从 6 个独立工程并成一个多页签套件，每课 = 套件
 * .cgui 里的一页（sv_pages 第 N 页）+ modules\lN.c 模块。事实表改为"从套件 .cgui 按页派生"：
 * 控件数 / 类数 / 控件清单都能逐页递归算出（与原六课逐课 selftest 数字逐页一致，实测
 * 30/55/27/23/23/13），不再逐课跑 selftest（合并后只有一个 exe）。页签名（课题名）由合并器
 * 写进 sv_pages.items，这里读它、不另立名单。
 */
const LESSON_SUITE = path.join(EX_DIR, "lessons", "lessons");

/** 读教学套件 .cgui（六课合并后都在这一份里）；读不到返回 null */
function lessonSuiteDoc() {
  try { return JSON.parse(fs.readFileSync(path.join(LESSON_SUITE, "lessons.cgui"), "utf8")); }
  catch { return null; }
}

/** 套件的 tabs 页签容器（sv_pages） */
function lessonTabs(doc) {
  return ((doc && doc.widgets) || []).find((w) => w.type === "tabs") || null;
}

/** 六课身份（按页序）：课号 01..06、模块前缀（l1..l6）、主题（页签名去掉 "NN " 前缀） */
function lessonDirs() {
  const tabs = lessonTabs(lessonSuiteDoc());
  const items = (tabs && tabs.items) || [];
  return items.map((title, i) => ({
    no: String(i + 1).padStart(2, "0"),
    pre: "l" + (i + 1),
    theme: title.replace(/^[0-9]+\s*/, "").trim() || title,
  }));
}

/** 某一页的顶层控件（page == p） */
function lessonPageWidgets(doc, p) {
  const tabs = lessonTabs(doc);
  return ((tabs && tabs.children) || []).filter((w) => w.page === p);
}

/** 递归数控件（顶层 + 全部后代），与 selftest 的"控件 N 个"同口径 */
function countWidgets(n) {
  let c = 1;
  for (const k of n.children || []) c += countWidgets(k);
  return c;
}

/** 套件整体跑一次 --selftest 取总数（合并后只有一个 exe，回调总数在这儿量） */
function suiteSelftest() {
  const base = path.join(LESSON_SUITE, "lessons");
  try {
    const out = execFileSync(base + ".exe", ["--selftest", base + ".cgui"], {
      encoding: "utf8",
      timeout: 20000,
      stdio: ["ignore", "pipe", "ignore"],
    });
    const m = out.match(/控件\s*(\d+)\s*个[\s\S]*?触发回调\s*(\d+)\s*次/);
    return m ? { widgets: Number(m[1]), callbacks: Number(m[2]) } : null;
  } catch {
    return null;                       // 没编译过 / 跑不起来：规模行显示"?"，由调用方提示先 build
  }
}

/** 一页的事实：课号、模块、主题、规模（控件/类/回调）、用到的控件类型。
 *  控件数按页递归数（与 selftest 口径一致，实测复现原六课逐课数字）；
 *  回调数取该页模块 CG_BIND 数（窗口事件转直连转发不进绑定表，故不含窗口事件）。 */
function lessonFacts() {
  const doc = lessonSuiteDoc() || {};
  return lessonDirs().map((L, p) => {
    const pageWidgets = lessonPageWidgets(doc, p);
    const used = new Set();
    for (const w of pageWidgets) collectTypes(w, used);
    const widgets = pageWidgets.reduce((a, w) => a + countWidgets(w), 0);
    const modSrc = (() => {
      try { return fs.readFileSync(path.join(LESSON_SUITE, "modules", `${L.pre}.c`), "utf8"); }
      catch { return ""; }
    })();
    const callbacks = (modSrc.match(/CG_BIND\("/g) || []).length;
    return {
      no: L.no,
      pre: L.pre,
      module: `examples\\lessons\\lessons\\modules\\${L.pre}.c`,
      theme: L.theme,
      kinds: used.size,
      widgets,
      callbacks,
      types: [...used].sort(),
    };
  });
}

const MARK_BEGIN = "<!-- lessons-scale:begin";
const MARK_END = "<!-- lessons-scale:end -->";

/** 生成那张"事实表"（带标记，供文档贴用与 --docs 回校） */
function lessonsScaleBlock() {
  const out = [];
  out.push(MARK_BEGIN + "（由 node tools\\check-examples.mjs --lesson-table 生成，别手改）-->");
  out.push("| 页 | 模块 | 主题 | 规模 | 用到的控件 |");
  out.push("| --- | --- | --- | --- | --- |");
  for (const r of lessonFacts()) {
    const typesCell = r.types.map((t) => "`" + t + "`").join(" ");
    out.push(`| ${r.no} | \`${r.module}\` | ${r.theme} | ${r.kinds} 类 · ${r.widgets} 个控件 · ${r.callbacks} 个回调 | ${typesCell} |`);
  }
  out.push(MARK_END);
  return out.join("\n");
}

/* ================= examples\README.md 的自动索引（2026-10-18 示例合并 P4 之后） =================
 * 合并后 examples/ 从 30 个平铺工程变成「六课 + 若干套件 + 独立工程」三类，新布局的门面
 * 就是这张索引。它与事实表同一套哲学：**由目录生成，文档只允许贴生成结果，--docs 回校** ——
 * 手写索引必然在下一次合并/加工具时过期（"目录改了索引没改"就是又一个漂移的事实）。
 * 页签名直接取自套件 .cgui 的 tabs.items（合并器写进去的就是工具名），不另立一份名单。 */
function exIndexBlock() {
  const out = [];
  out.push("<!-- ex-index:begin（由 node tools\\check-examples.mjs --ex-index 生成，别手改）-->");
  out.push("");
  out.push("## 教学六课（`examples\\lessons\\lessons\\` · 一个多页签套件）");
  out.push("");
  out.push("**顺序即内容**（前一课的概念是后一课的地基），六课并进一个多页签套件，每课一页签、一个模块：");
  out.push("");
  out.push("| 页 | 模块 | 主题 |");
  out.push("| --- | --- | --- |");
  for (const r of lessonFacts()) {
    out.push(`| ${r.no} | \`${r.module}\` | ${r.theme} |`);
  }
  out.push("");
  out.push("编译 `build-app.bat lessons\\lessons`，运行 `examples\\lessons\\lessons\\lessons.exe` —— 六课在六个页签里。");
  out.push("");
  out.push("## 合并套件（`examples\\suites\\`）");
  out.push("");
  /* 数字不写死：套件数 = examples\suites\ 的目录数，工具数 = 各套件 .cgui 的 tabs.items 之和。
   * 2026-11-03 之前这里是硬编码的「21 个工具并成 4 个」，合并成 studio/sys 之后那句话就再没
   * 人改过 —— 文档里最像真话的一句假话。数字从目录生成，才不会在下次合并时又漂。 */
  const suitesDir = path.join(EX_DIR, "suites");
  const suiteRows = [];
  let toolCount = 0;
  if (fs.existsSync(suitesDir)) {
    for (const e of fs.readdirSync(suitesDir, { withFileTypes: true })) {
      if (!e.isDirectory()) continue;
      const cgui = path.join(suitesDir, e.name, `${e.name}.cgui`);
      let doc = {};
      try { doc = JSON.parse(fs.readFileSync(cgui, "utf8")); } catch { doc = {}; }
      const tabs = (doc.widgets ?? []).find((w) => w.type === "tabs");
      const items = tabs?.items ?? [];
      toolCount += items.length;
      const pages = items.join(" / ") || "-";
      const docsDir = path.join(suitesDir, e.name, "docs");
      const nDocs = fs.existsSync(docsDir) ? fs.readdirSync(docsDir).filter((f) => f.endsWith(".md")).length : 0;
      suiteRows.push(
        `| \`${e.name}\` | ${pages} | \`build-app.bat suites\\${e.name}\` | ` +
          `\`examples\\suites\\${e.name}\\${e.name}.exe\` | \`suites\\${e.name}\\docs\\*.md\`（${nDocs} 份） |`,
      );
    }
  }
  out.push(`${toolCount} 个工具并成 ${suiteRows.length} 个**多页签工程**：main 只做入口（拼绑定表 / 窗口事件分发器），`);
  out.push("每个工具的完整逻辑在自己的模块里（照抄价值零损失）。编译一个套件 = 编出全部页签。");
  out.push("");
  out.push("| 套件 | 页签（工具） | 编译 | 运行 / 自检 | 逐工具文档 |");
  out.push("| --- | --- | --- | --- | --- |");
  out.push(...suiteRows);
  out.push("");
  out.push("## 独立工程（一个工程就是它自己，不并套件）");
  out.push("");
  out.push("| 工程 | 目录 | 演示 |");
  out.push("| --- | --- | --- |");
  for (const e of fs.readdirSync(EX_DIR, { withFileTypes: true })) {
    if (!e.isDirectory() || ["lessons", "suites"].includes(e.name)) continue;
    const cgui = path.join(EX_DIR, e.name, `${e.name}.cgui`);
    if (!fs.existsSync(cgui)) continue;
    let doc = {};
    try { doc = JSON.parse(fs.readFileSync(cgui, "utf8")); } catch { doc = {}; }
    const title = (doc.app && doc.app.title) || e.name;
    out.push(`| \`${e.name}\` | \`examples\\${e.name}\` | ${title} |`);
  }
  out.push("<!-- ex-index:end -->");
  return out.join("\n");
}

if (wantExIndex) {
  console.log(exIndexBlock());
  process.exit(0);
}

if (wantLessonTable) {
  console.log(lessonsScaleBlock());
  process.exit(0);
}

if (wantDocs) {
  const dirs = lessonDirs();
  const n = dirs.length;
  const articles = fs.existsSync(ARTICLES_DIR)
    ? fs.readdirSync(ARTICLES_DIR).filter((f) => f.endsWith(".md") && f !== "README.md")
    : [];
  const problems = [];
  let claims = 0;

  // ① 事实表的标记块必须与生成结果逐字一致（防手改、防漏改）
  const lessonDoc = path.join(root, "examples", "教学使用说明.md");
  if (fs.existsSync(lessonDoc)) {
    const txt = fs.readFileSync(lessonDoc, "utf8");
    const m = txt.match(/<!-- lessons-scale:begin[\s\S]*?<!-- lessons-scale:end -->/);
    if (!m) {
      problems.push("examples/教学使用说明.md — 找不到事实表标记块（lessons-scale:begin … end）");
    } else {
      claims += 1;
      if (m[0].replace(/\r\n/g, "\n") !== lessonsScaleBlock().replace(/\r\n/g, "\n")) {
        problems.push("examples/教学使用说明.md — 事实表与生成结果不一致（跑 --lesson-table 重贴；若规模列是 ? 先 build-all）");
      }
    }
  }

  // ①.5 examples/README.md 的索引块必须与生成结果逐字一致（合并后新布局的门面，
  //      页签名 / 文档份数 / 编译命令都由目录生成 —— 手改或过期就红）。
  {
    const idxDoc = path.join(EX_DIR, "README.md");
    if (fs.existsSync(idxDoc)) {
      const txt = fs.readFileSync(idxDoc, "utf8");
      const m = txt.match(/<!-- ex-index:begin[\s\S]*?<!-- ex-index:end -->/);
      if (!m) {
        problems.push("examples/README.md — 找不到索引标记块（ex-index:begin … end；跑 --ex-index 生成）");
      } else {
        claims += 1;
        if (m[0].replace(/\r\n/g, "\n") !== exIndexBlock().replace(/\r\n/g, "\n")) {
          problems.push("examples/README.md — 索引与现状不一致（跑 --ex-index 重贴）");
        }
      }
    }
  }

  // ② 课数声明必须等于实际课数。
  //    只认"明确在说总数"的措辞 —— 中文没有词边界，若放宽成"任意数字+课"，
  //    "这一课""每一课""第三课"全会误报（2026-09-23 首版实测：36 条里 15 条是误报）。
  const NUM = "(?<n>[0-9]+|[一二三四五六七八九十]+)";
  const LESSON_CLAIM_RES = [
    new RegExp(`共\\s*${NUM}\\s*课`, "g"),                 // 共 6 课
    new RegExp(`${NUM}\\s*课(教学示例|一览|里|中|范围内|各讲)`, "g"),   // 六课教学示例 / 六课一览 / 六课里…
    new RegExp(`学完(这|那)?${NUM}\\s*课`, "g"),            // 学完这六课
    new RegExp(`${NUM}\\s*课\\s*\\+`, "g"),                 // 六课 + 陈列室 / 六课 + 导读 + 实战
  ];
  for (const rel of LESSON_DOCS) {
    const p = path.join(root, rel);
    if (!fs.existsSync(p)) continue;
    fs.readFileSync(p, "utf8").split(/\r?\n/).forEach((line, i) => {
      for (const re of LESSON_CLAIM_RES) {
        for (const m of line.matchAll(re)) {
          const raw = m.groups && m.groups.n;              // 用具名分组取数：按序号取会被别的捕获组串位
          if (!raw) continue;
          claims += 1;
          const got = CN_NUM[raw] ?? Number(raw);
          if (got !== n) problems.push(`${rel}:${i + 1} — 写的是"${m[0].trim()}"，实际是 ${n} 课`);
        }
      }
    });
  }

  // ③ "lessonNN_a ~ lessonNN_b" 的首尾必须是第一课与最后一课
  for (const rel of LESSON_DOCS) {
    const p = path.join(root, rel);
    if (!fs.existsSync(p)) continue;
    fs.readFileSync(p, "utf8").split(/\r?\n/).forEach((line, i) => {
      for (const m of line.matchAll(/(lesson\d\d_[a-z0-9_]+)\s*~\s*(lesson\d\d_[a-z0-9_]+)/g)) {
        claims += 1;
        if (m[1] !== dirs[0] || m[2] !== dirs[n - 1]) {
          problems.push(`${rel}:${i + 1} — 范围写的是 ${m[1]} ~ ${m[2]}，实际是 ${dirs[0]} ~ ${dirs[n - 1]}`);
        }
      }
    });
  }

  // ④ CSDN 系列文章数："共 N 篇" 必须等于 docs/articles/*.md（不含 README）的张数
  if (fs.existsSync(ARTICLES_DIR)) {
    for (const f of fs.readdirSync(ARTICLES_DIR).filter((x) => x.endsWith(".md"))) {
      const p = path.join(ARTICLES_DIR, f);
      fs.readFileSync(p, "utf8").split(/\r?\n/).forEach((line, i) => {
        for (const m of line.matchAll(/共\s*([0-9]+|[一二三四五六七八九十])\s*篇/g)) {
          claims += 1;
          const got = CN_NUM[m[1]] ?? Number(m[1]);
          if (got !== articles.length) {
            problems.push(`docs/articles/${f}:${i + 1} — 写的是"共 ${m[1]} 篇"，实际有 ${articles.length} 篇`);
          }
        }
      });
    }
  }

  // ④.5 控件类型总数：文档里"N 种控件 / N 类控件 / 控件 N 种 / N 行类型表"必须等于 k_kinds 里的实名类型数
  //      （2026-09-23 排查发现 ARCHITECTURE 里同时写着 44 行类型表 / 补齐到 39 种 / 第 40·41·43·44 种，
  //       而代码是 42 —— 这类"总数"是同一类会漂移的手写事实，所以纳入口径门禁）
  const KIND_DOCS = [
    "README.md",
    "examples/教学使用说明.md",
    "packaging/README.release.md",
    "docs/ARCHITECTURE.md",
    "docs/CGUI-FORMAT.md",
    "docs/M3-SKIN.md",
  ];
  const kindCount = types.length;
  const KIND_CLAIM_RES = [
    new RegExp(`(?<n>[0-9]+)\\s*种控件`, "g"),
    // 刻意不收 "N 类控件"：那个措辞常被用来讲"某个示例覆盖了多少种"（如 gallery 覆盖 33 类），
    // 不是总数声明 —— 收进来会误报（2026-09-23 首版实测）。
    new RegExp(`控件\\s*(?<n>[0-9]+)\\s*种`, "g"),
    new RegExp(`(?<n>[0-9]+)\\s*行类型表`, "g"),
  ];
  for (const rel of KIND_DOCS) {
    const p = path.join(root, rel);
    if (!fs.existsSync(p)) continue;
    fs.readFileSync(p, "utf8").split(/\r?\n/).forEach((line, i) => {
      for (const re of KIND_CLAIM_RES) {
        for (const m of line.matchAll(re)) {
          claims += 1;
          const got = Number(m.groups.n);
          if (got !== kindCount) {
            problems.push(`${rel}:${i + 1} — 写的是"${m[0].trim()}"，实际是 ${kindCount} 种（k_kinds 实名类型）`);
          }
        }
      }
    });
  }

  // ④.6 逐类型事件表：CGUI-FORMAT 第 4 节"支持的 events"列必须覆盖 cg_doc_kind.c 的掩码。
  //      2026-09-23 排查的教训是"声明了却不生效"这一整类缺陷（edit 声明 dblclick 却从不派发、
  //      image 声明 click 却从不派发）—— 掩码本身对不对没法机器判定，但"**掩码与文档脱节**"
  //      可以：掩码里有、文档没写 = 错（用户发现不了这个能力）；文档写了、掩码里没有 = 警告
  //      （多半是过时的空头承诺，如撤销前的 monthcal dblclick）。
  {
    const kindSrc = fs.readFileSync(path.join(root, "runtime/src/core/cg_doc_kind.c"), "utf8");
    const evSrc = fs.readFileSync(path.join(root, "runtime/src/core/cg_event.c"), "utf8");

    // CG_EVENT_XXX -> "xxx"（以运行时的事件名表为唯一来源）
    const evName = new Map();
    for (const m of evSrc.matchAll(/\[(CG_EVENT_[A-Z]+)\]\s*=\s*"([a-z]+)"/g)) evName.set(m[1], m[2]);

    // 逐类型掩码：行首 `{ "type", CG_K_XXX,` 开始，累积到含 "}," 的那一行结束（掩码会跨行）
    const masks = new Map();
    let cur = null;
    for (const line of kindSrc.split(/\r?\n/)) {
      if (!cur) {
        const m = /^\s*\{\s*"([a-z]+)"\s*,\s*CG_K_/.exec(line);
        if (m) cur = { type: m[1], text: line };
        else continue;
      } else {
        cur.text += " " + line;
      }
      if (/\},?\s*$/.test(line) && /CG_K_/.test(cur.text)) {
        const evs = [...cur.text.matchAll(/CG_EV_([A-Z]+)/g)]
          .map((x) => evName.get("CG_EVENT_" + x[1]))
          .filter(Boolean);
        masks.set(cur.type, new Set(evs));
        cur = null;
      }
    }

    // 文档侧：CGUI-FORMAT 第 4 节的类型表，第 6 列 = 支持的 events
    const fmtRel = "docs/CGUI-FORMAT.md";
    const fmtLines = fs.readFileSync(path.join(root, fmtRel), "utf8").split(/\r?\n/);
    let inTable = false;
    fmtLines.forEach((line, i) => {
      if (/^##\s*4\./.test(line)) { inTable = true; return; }
      if (inTable && /^##\s*5\./.test(line)) { inTable = false; return; }
      if (!inTable || !line.startsWith("|")) return;
      const cells = line.split("|");
      if (cells.length < 7) return;
      const type = (cells[1] || "").trim().replace(/`/g, "");
      const mask = masks.get(type);
      if (!mask) return; // 非类型行（表头/分隔行）或文档里多出来的行，跳过
      const cell = cells[6] || "";
      const mentioned = new Set([...cell.matchAll(/`([a-z]+)`/g)].map((m) => m[1]).filter((n) => evName.has("CG_EVENT_" + n.toUpperCase()) || [...evName.values()].includes(n)));
      claims += 1;
      for (const ev of mask) {
        if (!mentioned.has(ev)) {
          problems.push(`${fmtRel}:${i + 1} — \`${type}\` 的掩码里有 \`${ev}\`，但这一行没写（用户会以为没这个事件）`);
        }
      }
      for (const ev of mentioned) {
        if (!mask.has(ev)) {
          console.log(`        提示：${fmtRel}:${i + 1} — \`${type}\` 这一行写了 \`${ev}\`，但掩码里没有（过时的空头承诺？）`);
        }
      }
    });

    // ④.7 画布的事件表（designer/src/model/events.ts 的 KIND_EVENTS）必须与运行时掩码逐条一致。
    //      画布是用户**唯一**能绑事件的地方，所以这条是"声明了却不生效"缺陷的高发边界：
    //      · 画布列了、运行时不派发 → 用户在画布上绑好、运行时永远不触发（静默失效）；
    //        月历的 dblclick 就是这么来的（类型表声明过、而 MCN_SELECT 从不发 NM_DBLCLK）。
    //      · 运行时支持、画布不列 → 用户以为框架没这个能力，只能手写 .cgui（edit 的 click 一度如此）。
    //      与 ④.6 互补：④.6 管"文档 vs 代码"，这里管"画布 vs 代码"。
    {
      const evTs = fs.readFileSync(path.join(root, "designer/src/model/events.ts"), "utf8");
      const knDecl = /export const KIND_EVENTS[\s\S]*?\n\};/.exec(evTs);
      if (!knDecl) {
        problems.push("designer/src/model/events.ts — 找不到 KIND_EVENTS 声明（改名了？这条门禁要跟着改）");
      } else {
        const knSrc = knDecl[0]
          .replace(/\/\*[\s\S]*?\*\//g, "")
          .replace(/\/\/[^\n]*/g, "");
        const unknownInCanvas = [];
        let compared = 0;
        for (const m of knSrc.matchAll(/([a-z_]+)\s*:\s*\[([\s\S]*?)\]/g)) {
          const type = m[1];
          const mask = masks.get(type);
          if (!mask) { unknownInCanvas.push(type); continue; }
          const listed = new Set([...m[2].matchAll(/"([a-z]+)"/g)].map((x) => x[1]));
          compared += 1;
          for (const ev of listed) {
            if (!mask.has(ev)) {
              problems.push(
                `designer/src/model/events.ts — 画布给 \`${type}\` 列了 \`${ev}\`，但运行时掩码里没有：` +
                `用户绑了它、运行时永远不派发（静默失效）`,
              );
            }
          }
          for (const ev of mask) {
            if (!listed.has(ev)) {
              /* 2026-10-17 二轮评审硬化：反向缺口从"提示"升级为红 —— 与 check-support
               * 同一契约同一强度（那边同日已硬化）。运行时声明了事件、画布不列，
               * 用户在属性面板看不到它，等于半个静默失效。硬化前实测反向缺口为 0。 */
              problems.push(
                `designer/src/model/events.ts — 运行时给 \`${type}\` 声明了 \`${ev}\`，画布没列（用户会以为没这个事件）`,
              );
            }
          }
        }
        claims += compared;
        if (unknownInCanvas.length) {
          console.log(`        提示：画布 KIND_EVENTS 里有运行时未知的类型（旧写法别名？这些不参与比对）：${unknownInCanvas.join(", ")}`);
        }
      }
    }
  }

  // ⑤ 每篇自述的"本篇是第 N 篇"必须等于它在系列里的序号（文件名前缀 + 1：00 导读 = 第 1 篇）
  if (fs.existsSync(ARTICLES_DIR)) {
    for (const f of articles) {
      const idx = Number(f.slice(0, 2)) + 1;
      if (!Number.isFinite(idx)) continue;
      fs.readFileSync(path.join(ARTICLES_DIR, f), "utf8").split(/\r?\n/).forEach((line, i) => {
        // 只认"自述"两种写法：本篇是第 N 篇 / 这是系列的第 N 篇。
        // "见第 1 篇""后面第 4 篇"是交叉引用，不是自述 —— 按任意"第 N 篇"匹配会误报。
        for (const m of line.matchAll(/本篇是第\s*(?<n1>[0-9]+)\s*篇|这是系列的第\s*(?<n2>[0-9]+)\s*篇/g)) {
          claims += 1;
          const got = Number(m.groups.n1 ?? m.groups.n2);
          if (got !== idx) {
            problems.push(`docs/articles/${f}:${i + 1} — 自述"第 ${got} 篇"，按文件名应是第 ${idx} 篇`);
          }
        }
      });
    }
  }

  if (problems.length > 0) {
    console.error(`[fail] 文档口径与现状不一致（${problems.length} 处 / 共检查 ${claims} 条声明）：`);
    for (const p of problems) console.error("        - " + p);
    process.exit(1);
  }
  console.log(`[ok] 文档口径与现状一致：${n} 课、${articles.length} 篇文章（共检查 ${claims} 条声明）`);
  process.exit(0);
}

/** 六课刻意不教的控件（在别处有落点，不计入 --lessons 的缺失）：
 *  serial → suites/studio 的串口终端；iconlist / dialog → gallery 陈列室。
 *  合并前 --lessons 的正则已失效（匹配不到 examples/lessons/ 下任何文件），
 *  这三种"缺落点"从未被真正检查出；2026-10-18 修作用域后才暴露 —— 它们本就不在六课范围内。 */
const LESSONS_EXEMPT = new Set(["serial", "iconlist", "dialog"]);

/** 某个类型在本次检查范围内的落点（按 --gallery / --lessons 过滤）
 *  2026-10-18 合并：六课并成一个套件，落点从每个 lessonNN 目录下的 .cgui 变成
 *  "examples/lessons/lessons/lessons.cgui"（六课都在这一份里）。 */
function hits(t) {
  const all = [...(map.get(t) ?? [])];
  if (onlyGallery) return all.filter((f) => f.startsWith("examples/gallery/"));
  if (onlyLessons) return all.filter((f) => f.startsWith("examples/lessons/"));
  return all;
}

const missing = types.filter((t) => {
  if (onlyLessons && LESSONS_EXEMPT.has(t)) return false;   // 六课刻意不教的控件不计缺失
  return hits(t).length === 0;
});

if (wantTable) {
  console.log("| 类型 | 出现处 |");
  console.log("| --- | --- |");
  for (const t of types) {
    const h = hits(t);
    const where = h.length === 0 ? "**（没有落点）**" : h.map((f) => path.basename(f, ".cgui")).join(" / ");
    console.log(`| \`${t}\` | ${where} |`);
  }
  console.log("");
  console.log(`（类型 ${types.length} 种，扫描 .cgui ${files} 个）`);
  process.exit(missing.length === 0 ? 0 : 1);
}

if (missing.length > 0) {
  console.error(`[fail] ${missing.length} 种控件在「${SCOPE}」范围内没有任何 .cgui 用到（读者找不到用法）：`);
  for (const t of missing) console.error(`        - ${t}`);
  console.error(`        扫描 .cgui ${files} 个`);
  process.exit(1);
}
console.log(`[ok] ${types.length} 种控件在「${SCOPE}」范围里都有落点（.cgui ${files} 个）`);
