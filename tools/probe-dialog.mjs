/* T1 的红基线 + 「画布不许静默丢节点」的回归探针。
 *
 * 两件事：
 *  1. **基线**：今天拿一份带 `dialog` 节点的 .cgui，两侧各是什么反应（T1 完成后这两条应当翻转）。
 *  2. **质量判据（会红）**：画布遇到不认识的控件类型，**不许静默丢** ——
 *     要么报错拒绝整份文件，要么把节点原样留着。
 *     2026-10-07 修之前它是"读进来了、节点被静默丢了"：`label` 不是容器，
 *     `children` 连整棵子树带事件一起消失，用户打开再保存就是**永久删除**。
 *
 * 依赖：先把模型层编到 build/modeljs（见 check-format.bat 里那条 tsc）。
 *
 * 入库位置说明：fixture 与本探针都在 tools\ 下（build\ 整个不入库），因为
 *   - fixture 是 T1 的验收输入（SDD §6 引用它）；
 *   - 「画布不许静默丢」那几条是**永久回归测试**；「运行时基线」两条等 T1.3 翻转后再升级成门禁。
 */
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { parseDoc } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const fixture = path.join(root, "tools", "fixtures", "dialog.cgui");
const text = fs.readFileSync(fixture, "utf8");

let fails = 0;
const ok = (name, cond, extra = "") => {
  if (!cond) fails += 1;
  console.log(`${cond ? "PASS" : "FAIL"} ${name}${extra ? `：${extra}` : ""}`);
};
const doc1 = (widgets) =>
  JSON.stringify({ cgui: 1, app: { title: "t", width: 300, height: 200 }, widgets });

console.log(`fixture: ${path.relative(root, fixture)}\n`);

/* ================= 画布侧 ================= */
const parsed = parseDoc(text);
console.log(`画布 parseDoc：${parsed.doc ? "读进来了" : `报错 -> ${parsed.error}`}`);

/* 质量判据：不许"读进来了但节点没了"这种静默丢 */
{
  const dlg = parsed.doc?.widgets.find((n) => n.type === "dialog");
  const kept = Boolean(dlg && dlg.children?.length === 4);
  ok(
    "[画布] 不认识的类型不许**静默丢**（要么报错拒绝，要么原样留着）",
    !parsed.doc || kept,
    parsed.doc ? (kept ? "原样留着" : `读进来了但节点被丢了（剩：${parsed.doc.widgets.map((n) => n.type).join(",")}）`) : "报错拒绝",
  );
}

/* 宽容那部分别一起修没了：别名仍要能升级、正常类型照旧 */
{
  const a = parseDoc(doc1([{ id: "e1", type: "multiedit", rect: { x: 0, y: 0, w: 100, h: 30 } }]));
  ok("[画布] 老别名 multiedit 仍升级成 edit", a.doc?.widgets[0]?.type === "edit", a.error ?? a.doc?.widgets[0]?.type);
  const b = parseDoc(doc1([{ id: "b1", type: "button", rect: { x: 0, y: 0, w: 100, h: 30 } }]));
  ok("[画布] 正常类型照旧读得进来", b.doc?.widgets[0]?.type === "button", b.error ?? "");
  const c = parseDoc(doc1([{ id: "h1", type: "button", rect: { x: 0, y: 0, w: 100, h: 30 } },
                           { id: "x1", type: "nonsense", rect: { x: 0, y: 0, w: 10, h: 10 } }]));
  ok("[画布] 未知类型**整份**拒绝（不是只跳过一个）", !c.doc && /nonsense/.test(c.error ?? ""), c.error ?? "");
}

/* ================= 运行时侧（基线） ================= */
const cands = [
  path.join(root, "apps", "main", "main.exe"),
  path.join(root, "examples", "gallery", "gallery.exe"),
];
const exe = cands.find((p) => fs.existsSync(p));
if (!exe) {
  console.log("SKIP 运行时侧：找不到可用的 exe");
} else {
  const r = spawnSync(exe, ["--selftest", fixture], { encoding: "utf8" });
  const out = `${r.stdout || ""}${r.stderr || ""}`.trim();
  console.log(`\n运行时 ${path.relative(root, exe)} --selftest：exit=${r.status}`);
  console.log(out.split("\n").slice(0, 6).map((l) => `  ${l}`).join("\n"));
  /* T1.1 起：运行时能解析 dialog（作为容器进树，children 归它）。
   * exit 仍可能是 5 —— fixture 的回调没登记在 main.exe 的 BIND 表里，那是 BIND 侧的事，
   * T1.2 会用带绑定的真工程验 open/close。这里判的是**解析**：
   * 不许再出现 unknown widget type，且树里要能看到 dialog 和它的孩子。 */
  const noUnknown = !/unknown widget type/.test(out);
  const inTree = /dialog #dlg_settings/.test(out) && /edt_port/.test(out);
  ok(
    "[运行时] dialog 能解析进树（T1.1）",
    noUnknown && inTree,
    noUnknown ? (inTree ? "树里可见 dialog 与它的子控件" : "解析过了但树里没看到 dialog 子树") : "仍在报 unknown widget type",
  );
}

console.log(fails === 0 ? "\nPASS（画布不静默丢；运行时已能解析 dialog —— open/close 是 T1.2）" : `\n${fails} FAILED`);
process.exitCode = fails === 0 ? 0 : 1;
