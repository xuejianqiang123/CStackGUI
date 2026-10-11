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
/* ready 自动开对话框：探针（真机 A5）驱动不了菜单点击，本环境也注入不了真实输入 ——
 * 无人操作取证靠这个。 */
doc.app.events = { ready: "on_ready" };

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

/* 片段：自己带 roles / styles，dialog 里一个 label 同时用 @角色 与命名样式。
 * 文字全 ASCII：探针（C 驱动 GetWindowTextW 比对）跨进程取文字，中文有编码坑。
 * A3 的判据就挂在这个 label 上：改片段文件（不重编）→ 重开程序 → 文字必须变。 */
fs.mkdirSync(path.join(dir, "frag"), { recursive: true });
fs.writeFileSync(
  path.join(dir, "frag", "frag.cgui"),
  JSON.stringify(
    {
      cgui: 1,
      app: { title: "fragment (not a project)" },
      roles: { fragAccent: { light: "#6750A4", dark: "#D0BCFF" } },
      styles: { fragStyle: { size: 11, bold: true } },
      widgets: [
        {
          id: "dlg_port",
          type: "dialog",
          rect: { x: 40, y: 40, w: 300, h: 180 },
          text: "PortSetup (from fragment)",
          children: [
            {
              id: "frag_lab",
              type: "label",
              rect: { x: 16, y: 20, w: 260, h: 20 },
              text: "included label v1",
              style: { fg: "@fragAccent", use: "fragStyle" },
            },
            {
              id: "frag_ok",
              type: "button",
              rect: { x: 100, y: 120, w: 88, h: 30 },
              text: "OK",
              events: { click: "on_dlg_ok" },
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

/* 片段里控件的回调（on_dlg_ok）不在宿主文档里 —— generateC 看不见它：
 * 绑定表（main.c）与回调声明（*_ui_events.h）都得手工补。这正是 PRD §7.2 说清的
 * 那条：「引用一段界面，也要把它的回调一起带上」—— 漏了的话 selftest 会报"未登记"。 */
const mainPath = path.join(dir, "main.c");
fs.writeFileSync(
  mainPath,
  fs.readFileSync(mainPath, "utf8").replace(
    "CG_BIND_END",
    'CG_BIND("on_dlg_ok", on_dlg_ok),\n    CG_BIND_END',
  ),
  "utf8",
);
const hdrPath = path.join(dir, "inctest_ui_events.h");
const hdr = fs.readFileSync(hdrPath, "utf8");
const hdrAt = hdr.lastIndexOf("#endif");
fs.writeFileSync(
  hdrPath,
  hdr.slice(0, hdrAt) +
    "void on_dlg_ok(cg_widget_t *self, cg_event_t ev, void *user);\n" +
    hdr.slice(hdrAt),
  "utf8",
);

/* 真实回调盖掉空桩：ready 开片段对话框（无人操作取证），OK 关它 */
const impl = `/**
 * @file    inctest_ui_events.c
 * @brief   include 验证工程的回调：ready 自动开片段对话框（探针驱动 / 断言用）
 */
#include "inctest_ui_events.h"

/**
 * @brief 窗口就绪：非模态打开**引用来的**对话框（F3 A5 的取证动作）
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *dlg;
    (void)ev;
    (void)user;

    dlg = cg_find(app, "dlg_port");
    if (dlg) cg_dialog_open_nonmodal(dlg);
}

/**
 * @brief 片段对话框里 "OK" 的点击：带结果 1 关闭
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_dlg_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}
`;
fs.writeFileSync(path.join(dir, "inctest_ui_events.c"), impl, "utf8");
console.log("[ok] 写出", dir);
execFileSync("node", [path.join(root, "designer", "electron", "cgbuild.mjs"), dir], { stdio: "inherit" });
