/* check-examples.mjs -- 控件类型覆盖闸门：每种类型都得在 examples/ 里有落点
 *
 * 为什么需要它：示例就是"控件目录"的说明书。加了新类型却没有任何示例用它 = 读者找不到用法，
 * 时间一长大家会以为这个控件不存在（`box`/`meter` 就这么被漏掉过）。
 * 类型真相在 runtime/src/core/cg_doc_kind.c 的 k_kinds 表，这里只做"对表"这件事，不猜。
 *
 * 用法（在项目根）：
 *   node tools\check-examples.mjs            扫类型表 + 扫 examples/**\*.cgui，缺落点就报错（退出码 1）
 *   node tools\check-examples.mjs --table    打印 Markdown 映射表（生成文档索引表用，别手抄）
 *   node tools\check-examples.mjs --lessons  只认五课（lesson01~05）：这是"控件分类课"的验收标准
 *   node tools\check-examples.mjs --gallery  只认 gallery（"一页看全"那句话得是真的）
 *
 * 说明：
 *   - 伪类型 `window`（窗口本身也是一个"控件"，事件写在 app.events）不算，其余全部要落点。
 *   - 只看 examples/：apps/ 是用户自己的工程，不该被这个闸门约束。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const KIND_FILE = path.join(root, "runtime", "src", "core", "cg_doc_kind.c");
const EX_DIR = path.join(root, "examples");

const args = process.argv.slice(2);
const wantTable = args.includes("--table");
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
