/* gen-inctest.mjs -- 生成「include 展开」的验证工程（F3 P1 的真机/自检取证）
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是 F3 多文件组装的**永久验证夹具**：
 * 重跑一次就能复核「include 展开 + 三张表合并 + id 冲突报错」的行为有没有被改坏
 * （用法见 f3-window-ref-prd.md；反例三条：pick 没命中 / 真 id 冲突 / 循环引用）。
 *
 * host：一个按钮 + 一个标签；include[] 引用 frag/frag.cgui（pick=dlg_port）。
 * frag：一个 dialog（含一个 label，style = { fg: "@fragAccent", use: "fragStyle" }）——
 *       片段自己带 roles 与 styles，专门验证三张表合并后 @引用与命名样式都能解析。
 * 预期（--selftest）：控件数 = host 的 2 + 片段的 1 + 1（dialog 里的 label），
 * 且不出现「引用了不存在的角色名 / 样式名」的 WARN。
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { generateC } from "../build/modeljs/codegen.js";
import { emptyDoc, makeNode } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = path.join(root, "build", "inctest");

const doc = emptyDoc();
doc.app.title = "include test";
doc.app.width = 420;
doc.app.height = 200;

const btn = makeNode("button", 16, 16);
btn.id = "btn_open";
btn.text = "打开对话框";
btn.rect.w = 140;
btn.rect.h = 34;
doc.widgets.push(btn);

const lab = makeNode("label", 170, 24);
lab.id = "lab_hint";
lab.text = "宿主自己的标签";
lab.rect.w = 220;
lab.rect.h = 20;
doc.widgets.push(lab);

const files = generateC(doc, "inctest");
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });
for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.content, "utf8");

/* 往生成的 .cgui 里插 include[]（模拟"用户手写了 include"的输入形态） */
const cguiPath = path.join(dir, "inctest.cgui");
const j = JSON.parse(fs.readFileSync(cguiPath, "utf8"));
j.include = [{ path: "frag/frag.cgui", pick: "dlg_port" }];
fs.writeFileSync(cguiPath, JSON.stringify(j, null, 2), "utf8");

/* 片段：自己带 roles / styles，dialog 里一个 label 同时用 @角色 与命名样式 */
fs.mkdirSync(path.join(dir, "frag"), { recursive: true });
fs.writeFileSync(
  path.join(dir, "frag", "frag.cgui"),
  JSON.stringify(
    {
      cgui: 1,
      app: { title: "片段（不是工程）" },
      roles: { fragAccent: { light: "#6750A4", dark: "#D0BCFF" } },
      styles: { fragStyle: { size: 11, bold: true } },
      widgets: [
        {
          id: "dlg_port",
          type: "dialog",
          rect: { x: 40, y: 40, w: 300, h: 180 },
          text: "端口设置（来自片段）",
          children: [
            {
              id: "frag_lab",
              type: "label",
              rect: { x: 16, y: 20, w: 260, h: 20 },
              text: "我是被引用进来的",
              style: { fg: "@fragAccent", use: "fragStyle" },
            },
          ],
        },
      ],
    },
    null,
    2,
  ),
  "utf8",
);
console.log("[ok] 写出", dir);
execFileSync("node", [path.join(root, "designer", "electron", "cgbuild.mjs"), dir], { stdio: "inherit" });
