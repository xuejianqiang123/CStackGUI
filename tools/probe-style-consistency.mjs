/* probe-style-consistency.mjs -- 样式/角色在画布模型层的契约探针（T3.0 + A6）
 *
 * 三组判据：
 *   1. resolveStyle 的合并口径（契约 §2.2.1）：命名打底、内联覆盖、失效 use 当没写 ——
 *      这里按**契约文字**独立再实现一遍对拍（不是读 style.ts 抄答案），两实现漂移就红。
 *   2. @角色引用要在 parse/serialize 双向存活（A6）：parse.ts 的 colorOf 原先会**静默丢**
 *      非 hex 颜色——修之前这两条是红的（保存一次引用就没了）。
 *   3. roles{} 表本身要在画布往返中无损（G1 落地前画布根本没有这个字段——整表静默消失）。
 *
 * 依赖：build\modeljs（tsc 命令见 probe-merge-e2e.mjs 头部）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它守的是**永久契约**：
 * resolveStyle 的合并口径、@角色引用与 roles{} 的双向往返。红过一次的历史：
 * parse.ts 的 colorOf 静默丢 @（保存一次引用就没了）+ roles{} 整表消失，2026-10-07 修复。
 */
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseDoc, serializeDoc, resolveStyle } from "../build/modeljs/model.js";

let fails = 0;
const ok = (name, cond, extra = "") => {
  if (!cond) fails += 1;
  console.log(`${cond ? "PASS" : "FAIL"} ${name}${extra ? `：${extra}` : ""}`);
};

const here = path.dirname(fileURLToPath(import.meta.url));
const doc1 = {
  cgui: 1,
  app: { title: "t", width: 300, height: 200, bg: "@surface" },
  roles: {
    accent: { light: "#6750A4", dark: "#D0BCFF" },
    danger: { light: "#B3261E" },
  },
  styles: {
    body: { font: "Consolas", size: 9, fg: "#00FF00" },
    themed: { fg: "@accent", bg: "@danger" },
  },
  widgets: [
    { id: "w_inline", type: "label", style: { size: 11 } },
    { id: "w_named", type: "label", style: "body" },
    { id: "w_mix", type: "label", style: { use: "body", size: 20 } },
    { id: "w_missing", type: "label", style: { use: "nope", size: 8 } },
    { id: "w_ref", type: "label", style: { fg: "@accent" } },
    { id: "w_refmix", type: "label", style: { use: "themed", fg: "@danger" } },
    { id: "w_nostyle", type: "label" },
  ],
};

const text1 = JSON.stringify(doc1, null, 2);

/* ================= 1. 合并口径（契约文字的独立实现） ================= */
{
  const parsed = parseDoc(text1);
  ok("文档能解析", !!parsed.doc, parsed.error ?? "");
  if (parsed.doc) {
    /* 独立实现：按契约"命名打底、内联覆盖（写了才算）"再算一遍 */
    const contractResolve = (doc, node) => {
      const s = node.style;
      const use = typeof s === "string" ? s : s && typeof s === "object" && s.use ? s.use : "";
      const named = use && doc.styles && Object.hasOwn(doc.styles, use) ? doc.styles[use] : {};
      const inline = s && typeof s === "object" ? s : {};
      const eff = {};
      for (const k of ["font", "size", "bold", "fg", "bg", "radius", "align"]) {
        /* 契约：内联优先 —— 控件自己写了的（值或 @引用）盖过命名样式 */
        const v = inline[k] !== undefined ? inline[k] : named[k];
        if (v !== undefined) eff[k] = v;
      }
      return eff;
    };

    const w = (id) => parsed.doc.widgets.find((n) => n.id === id);
    for (const id of ["w_inline", "w_named", "w_mix", "w_missing", "w_ref", "w_refmix"]) {
      const node = w(id);
      const mine = resolveStyle(parsed.doc, node.style).effective;
      const theirs = contractResolve(parsed.doc, node);
      const keys = [...new Set([...Object.keys(mine), ...Object.keys(theirs)])];
      const diff = keys.filter((k) => JSON.stringify(mine[k]) !== JSON.stringify(theirs[k]));
      ok(`resolveStyle 与契约实现对拍：${id}`, diff.length === 0,
        diff.length ? `漂移字段 ${diff.join(",")}（${JSON.stringify(mine)} vs ${JSON.stringify(theirs)}）` : "");
    }

    ok("纯引用控件的 fg 来自命名样式", w("w_named").style.fg === undefined &&
      resolveStyle(parsed.doc, w("w_named").style).effective.fg === "#00FF00");
    ok("失效 use 当没写（不炸）", resolveStyle(parsed.doc, w("w_missing").style).effective.size === 8);
  }
}

/* ================= 2. parse→serialize 往返：@引用与 roles{} 双向存活（A6） ================= */
{
  const p1 = parseDoc(text1);
  ok("第二轮 parse 与第一轮一致（w_ref 存在）", !!p1.doc?.widgets.find((n) => n.id === "w_ref"));
  ok("@引用 parse 后原样保留（fg=@accent）", p1.doc?.widgets.find((n) => n.id === "w_ref")?.style?.fg === "@accent",
    JSON.stringify(p1.doc?.widgets.find((n) => n.id === "w_ref")?.style ?? null));
  ok("styles{} 里的 @引用也保留", p1.doc?.styles?.themed?.fg === "@accent");
  ok("roles{} 表 parse 后在", p1.doc?.roles?.accent?.dark === "#D0BCFF" && p1.doc?.roles?.danger?.light === "#B3261E");
  ok("app.bg 的 @引用保留", p1.doc?.app?.bg === "@surface");

  if (p1.doc) {
    const written = serializeDoc(p1.doc);
    const p2 = parseDoc(written);
    ok("往返一次后 @引用仍在", p2.doc?.widgets.find((n) => n.id === "w_ref")?.style.fg === "@accent");
    ok("往返一次后 roles{} 仍在", p2.doc?.roles?.accent?.light === "#6750A4");
    ok("往返一次后 styles 的 @引用仍在", p2.doc?.styles?.themed?.bg === "@danger");
    ok("往返一次后 app.bg 的 @引用仍在", p2.doc?.app?.bg === "@surface");
  }
}

console.log(fails === 0 ? "\nALL PASS" : `\n${fails} FAILED`);
process.exitCode = fails === 0 ? 0 : 1;
