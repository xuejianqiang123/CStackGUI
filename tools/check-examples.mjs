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

/** 教学示例目录名（examples/lessonNN_xxx，按课号排序） */
function lessonDirs() {
  return fs.readdirSync(EX_DIR, { withFileTypes: true })
    .filter((e) => e.isDirectory() && LESSON_RE.test(e.name))
    .map((e) => e.name)
    .sort();
}

/** 真跑一次 --selftest 取规模数字（规模真相在运行时，不是我们数 JSON 的结果） */
function selftestNumbers(dir) {
  const base = path.join(EX_DIR, dir, dir);
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
    const cgui = path.join(EX_DIR, dir, dir + ".cgui");
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
