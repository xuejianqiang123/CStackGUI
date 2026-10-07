/* gen-modaltest.mjs -- 生成 T1.2 真机验证工程 build\modaltest\
 *
 * 走 generateC 出 6 个文件，再用真实回调逻辑盖掉 _ui_events.c 的空桩。驱动方是
 * tools\probe-modal.c（外部 C 驱动）：
 *   phase1 点 OK —— 期望 result=1，顺带读回 edt_port 的值（A3/A4）
 *   phase2 发 WM_CLOSE —— 期望 result=0（"直接关掉"那条路）
 *   phase3 点 OpenNM —— 非模态（T4）：立即返回、owner 保持可用、OK 关闭
 *   全程结束后主窗口必须还活着（A5）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是 probe-modal.c 的**永久夹具**：
 * 夹具没了，那 27 项真机断言（模态 A1~A5 + 非模态 A6）就只剩文档里的一段散文。
 * 复跑：node tools\gen-modaltest.mjs && node designer\electron\cgbuild.mjs build\modaltest
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { generateC } from "../build/modeljs/codegen.js";
import { emptyDoc, makeNode } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const NAME = "modaltest";
const dir = path.join(root, "build", NAME);

const doc = emptyDoc();
doc.app.title = "modal test";
doc.app.width = 420;
doc.app.height = 200;

const lbl = makeNode("label", 16, 16);
lbl.id = "lbl_hint";
lbl.text = "click Open; the probe drives the rest";

const btn = makeNode("button", 16, 48);
btn.id = "btn_open";
btn.text = "Open";
btn.events = { click: "on_open" };

const btnNm = makeNode("button", 140, 48);
btnNm.id = "btn_open_nm";
btnNm.text = "OpenNM";
btnNm.events = { click: "on_open_nm" };

/* 对话框：独立顶层窗口。按钮文字用 ASCII —— 探针（.ps1 按 ANSI 读）要按文字找它们 */
const dlg = makeNode("dialog", 0, 0);
dlg.id = "dlg_test";
dlg.text = "Port Setup";
dlg.rect = { x: 0, y: 0, w: 330, h: 170 };
dlg.children = [
  (() => { const e = makeNode("edit", 104, 18); e.id = "edt_port"; e.text = "COM3"; return e; })(),
  (() => { const b = makeNode("button", 120, 120); b.id = "btn_ok"; b.text = "OK"; b.events = { click: "on_ok" }; return b; })(),
  (() => { const b = makeNode("button", 216, 120); b.id = "btn_cancel"; b.text = "Cancel"; b.events = { click: "on_cancel" }; return b; })(),
];

doc.widgets = [lbl, btn, btnNm, dlg];

const files = generateC(doc, NAME);
fs.mkdirSync(dir, { recursive: true });
for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.content, "utf8");

/* 真实回调逻辑盖掉空桩（生成器契约：业务写这里） */
const impl = `/**
 * @file    ${NAME}_ui_events.c
 * @brief   多窗体 T1.2 的真机验证回调（探针 build\\\\probe-modal.ps1 驱动，结果写 result.log）
 */
#include "${NAME}_ui_events.h"

#include <stdio.h>

static FILE *lg = NULL;

/**
 * @brief "Open" 的点击：开两遍对话框，把结果与读回的值写进 result.log
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_open(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *port;
    int r1, r2;
    (void)ev;
    (void)user;

    lg = fopen("result.log", "w");
    if (!lg) return;

    /* 第一遍：探针给 "OK" 发 BM_CLICK —— 期望 result=1（A3 的 OK 路径） */
    r1 = cg_dialog_open(cg_find(app, "dlg_test"));
    port = cg_find(app, "edt_port");
    fprintf(lg, "phase1 result=%d port=%s\\n", r1, port ? cg_widget_text(port) : "(none)");
    fflush(lg);

    /* 第二遍：探针发 WM_CLOSE —— 期望 result=0（"直接关掉"那条路） */
    r2 = cg_dialog_open(cg_find(app, "dlg_test"));
    fprintf(lg, "phase2 result=%d\\n", r2);

    fprintf(lg, "alive=%d\\n", cg_app_has_window(app));   /* A5：主窗口必须还活着 */
    fclose(lg);
    lg = NULL;
}

/**
 * @brief "OK" 的点击：确定关闭
 */
void on_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}

/**
 * @brief "Cancel" 的点击：取消关闭
 */
void on_cancel(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 0);
}

/**
 * @brief "OpenNM" 的点击：开**非模态**对话框（T4）——立即返回、主窗口不禁用。
 *        结果追加进 result.log（此时 on_open 已返回、日志已关闭）。
 */
void on_open_nm(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "nm result=%d\\n", cg_dialog_open_nonmodal(cg_find(app, "dlg_test")));
    fclose(f);
}
`;
fs.writeFileSync(path.join(dir, `${NAME}_ui_events.c`), impl, "utf8");
console.log("written:", dir);
