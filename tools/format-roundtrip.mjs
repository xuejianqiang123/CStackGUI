/* format-roundtrip.mjs -- 格式契约回归测试
 *
 * 这条缝最容易坏：画布用 TS 写 .cgui，运行时用 C 读 .cgui，
 * 两边对字段的理解一旦漂移，画布会「能存但跑不起来」。
 *
 * 测法：
 *   1. 用画布的模型代码读一个 .cgui（parseDoc）
 *   2. 改点东西（挪位置、改文字、加控件、绑事件）后重新写出来（serializeDoc）
 *   3. 把写出来的文件交给 C 运行时自检（gallery.exe --selftest）
 *
 * 用法：node tools/format-roundtrip.mjs <exe> <输入.cgui> <输出.cgui>
 * 需要先编译画布模型：npx tsc src/model.ts --outDir ../build/modeljs ...
 */
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");

const exe = process.argv[2] ? path.resolve(process.argv[2]) : path.join(root, "examples", "gallery", "gallery.exe");
const input = process.argv[3] ? path.resolve(process.argv[3]) : path.join(root, "examples", "gallery", "gallery.cgui");
const output = process.argv[4] ? path.resolve(process.argv[4]) : path.join(root, "build", "roundtrip_gallery.cgui");

const modelPath = path.join(root, "build", "modeljs", "model.js");
if (!fs.existsSync(modelPath)) {
  console.error(`[fail] 找不到 ${modelPath}，先用 tsc 把 designer/src/model.ts 编译到 build/modeljs/`);
  process.exit(2);
}

const { parseDoc, serializeDoc, findNode, makeNode, walk } = await import(
  `file://${modelPath.replace(/\\/g, "/")}`
);

/* 1. 读 */
const original = fs.readFileSync(input, "utf8");
const parsed = parseDoc(original);
if (!parsed.doc) {
  console.error(`[fail] 画布模型读不了 ${input}: ${parsed.error}`);
  process.exit(1);
}
const doc = parsed.doc;

/* 2. 改：模拟用户在画布上做的事 */

/** 文档里第一个已绑定的回调名。探针控件要复用它 —— 随便编一个名字的话，
 *  C 侧自检会把「名字没登记」当成失败，那是测试自己造出来的假故障。 */
let knownCallback = "";
walk(doc.widgets, (n) => {
  if (knownCallback) return;
  for (const name of Object.values(n.events ?? {})) {
    if (name) {
      knownCallback = String(name);
      return;
    }
  }
});

let touched = 0;
walk(doc.widgets, (n) => {
  if (n.type === "button" && !n.events?.click && knownCallback) {
    n.events = { ...(n.events ?? {}), click: knownCallback };
    touched += 1;
  }
});

const probeId = "chk_roundtrip";
if (!findNode(doc, probeId)) {
  const node = makeNode("checkbox", 24, 220);
  node.id = probeId;
  node.text = "画布新增的复选框";
  node.checked = true;
  if (knownCallback) node.events = { change: knownCallback };
  doc.widgets.push(node);
  touched += 1;
}

const title = findNode(doc, "lab_title");
if (title) {
  title.text = "由画布写回";
  title.rect.x += 6;
  touched += 1;
}

/* 有树/表格/标签页/状态栏的话，连结构化内容一起走一遍：
 * 这些字段最容易两边理解不一致，必须让 C 侧真的读一遍 */
walk(doc.widgets, (n) => {
  if (n.type === "tabs") {
    n.items = [...(n.items ?? []), "画布加的第三页"];
    n.children = n.children ?? [];
    const child = n.children[0];
    if (child) {
      child.page = 2;
      n.children.push({ ...structuredClone(child), id: `${child.id}_p2`, page: 2 });
    }
    touched += 1;
  }
  if (n.type === "tree") {
    n.nodes = [...(n.nodes ?? []), { text: "画布加的根节点", expanded: true, children: [{ text: "子" }] }];
    touched += 1;
  }
  if (n.type === "table") {
    n.columns = [...(n.columns ?? []), { title: "画布加的列", w: 90 }];
    n.rows = [...(n.rows ?? []), ["x", "y", "z"]];
    touched += 1;
  }
  if (n.type === "statusbar") {
    n.items = [...(n.items ?? []), "画布加的分段"];
    touched += 1;
  }
  if (n.type === "menu" || n.type === "menubutton") {
    /* 菜单是「嵌套 + id + 分隔线 + 勾选」的组合，最值得让 C 侧真读一遍 */
    n.nodes = [
      ...(n.nodes ?? []),
      { text: "-" },
      { text: "画布加的菜单", id: "m_roundtrip" },
      { text: "带子项", id: "m_roundtrip_sub", checked: true, children: [{ text: "子项", id: "m_sub" }] },
    ];
    touched += 1;
  }
  if (n.type === "toolbar") {
    n.items = [...(n.items ?? []), "画布加的按钮"];
    touched += 1;
  }
  if (n.type === "image" && n.src) {
    n.rect.w += 10;
    touched += 1;
  }
});

const written = serializeDoc(doc);
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, written, "utf8");
console.log(`[ok] 画布改动 ${touched} 处，写出 ${output}（${written.length} 字节）`);

/* 3. 交给 C 运行时读 */
let out = "";
let code = 0;
try {
  out = execFileSync(exe, ["--selftest", output], { encoding: "utf8" });
} catch (e) {
  out = String(e.stdout ?? "") + String(e.stderr ?? "");
  code = e.status ?? 1;
}

process.stdout.write(out);

const ok = code === 0 && /selftest OK/.test(out);
if (ok) {
  console.log(`[ok] 运行时接受了画布写出的文件（${exe} --selftest 通过）`);
  process.exit(0);
}
console.error(`[fail] 运行时拒绝或报警：exit=${code}`);
process.exit(1);
