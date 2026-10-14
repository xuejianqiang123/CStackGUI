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
 * 教学示例目录名（按课号排序）
 * 教训目录现在收在 examples/base/ 下（重组后不再平铺在 examples/ 顶层），
 * 所以两层都要扫 —— 只扫顶层会得到 0 课，门禁里所有"六课"的声明就全红了。
 */
function lessonDirs() {
  const names = new Set();
  for (const sub of ["", "base"]) {
    const dir = sub ? path.join(EX_DIR, sub) : EX_DIR;
    if (!fs.existsSync(dir)) continue;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (e.isDirectory() && LESSON_RE.test(e.name)) names.add(e.name);
    }
  }
  return [...names].sort();
}

/** 某一课的实际目录：优先 examples/<名>，其次 examples/base/<名> */
function lessonHome(dir) {
  const direct = path.join(EX_DIR, dir);
  return fs.existsSync(path.join(direct, dir + ".cgui")) ? direct : path.join(EX_DIR, "base", dir);
}

/** 真跑一次 --selftest 取规模数字（规模真相在运行时，不是我们数 JSON 的结果） */
function selftestNumbers(dir) {
  const base = path.join(lessonHome(dir), dir);
  try {
    const out = execFileSync(base + ".exe", ["--selftest", base + ".cgui"], {
      encoding: "utf8",
      timeout: 20000,
      stdio: ["ignore", "pipe", "ignore"],
    });
    const m = out.match(/控件\s*(\d+)\s*个[\s\S]*?触发回调\s*(\d+)\s*次/);
    return m ? { widgets: Number(m[1]), callbacks: Number(m[2]) } : null;
  } catch {
    return null;                       // 没编译过 / 跑不起来：规模列显示"?"，由调用方提示先 build-all
  }
}

/** 一节课的事实：课号、目录、主题（取 app.title 里 "·" 之后那段）、规模、用到的控件类型 */
function lessonFacts() {
  const rows = [];
  for (const dir of lessonDirs()) {
    const cgui = path.join(lessonHome(dir), dir + ".cgui");
    let doc = {};
    try { doc = JSON.parse(fs.readFileSync(cgui, "utf8")); } catch { doc = {}; }
    const title = (doc.app && doc.app.title) || "";
    const theme = title.includes("·") ? title.split("·").slice(1).join("·").trim() : title.trim();
    const used = new Set();
    collectTypes(doc.widgets, used);
    const st = selftestNumbers(dir);
    rows.push({
      no: dir.slice(6, 8),
      dir,
      theme,
      kinds: used.size,
      widgets: st ? st.widgets : "?",
      callbacks: st ? st.callbacks : "?",
      types: [...used].sort(),
    });
  }
  return rows;
}

const MARK_BEGIN = "<!-- lessons-scale:begin";
const MARK_END = "<!-- lessons-scale:end -->";

/** 生成那张"事实表"（带标记，供文档贴用与 --docs 回校） */
function lessonsScaleBlock() {
  const out = [];
  out.push(MARK_BEGIN + "（由 node tools\\check-examples.mjs --lesson-table 生成，别手改）-->");
  out.push("| 课 | 目录 | 主题 | 规模 | 用到的控件 |");
  out.push("| --- | --- | --- | --- | --- |");
  for (const r of lessonFacts()) {
    const typesCell = r.types.map((t) => "`" + t + "`").join(" ");
    out.push(`| 入门 ${r.no} | \`examples\\${r.dir}\` | ${r.theme} | ${r.kinds} 类 · ${r.widgets} 个控件 · ${r.callbacks} 个回调 | ${typesCell} |`);
  }
  out.push(MARK_END);
  return out.join("\n");
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
              console.log(`        提示：designer/src/model/events.ts — \`${type}\` 运行时掩码有 \`${ev}\`，画布没列（用户会以为没这个事件）`);
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

/** 某个类型在本次检查范围内的落点（按 --gallery / --lessons 过滤） */
function hits(t) {
  const all = [...(map.get(t) ?? [])];
  if (onlyGallery) return all.filter((f) => f.startsWith("examples/gallery/"));
  if (onlyLessons) return all.filter((f) => /^examples\/lesson\d\d_/.test(f));
  return all;
}

const missing = types.filter((t) => hits(t).length === 0);

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
