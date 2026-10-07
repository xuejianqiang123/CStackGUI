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

/* 错误路径的两个驱动口（T4 收尾）：
 *   OpenNM2   —— 已经开着一个非模态时再开一次（守卫必须拒绝，且不许顶掉原来那个）
 *   CloseIdle —— 什么都没开时调 cg_dialog_close（不许崩，之后还得能正常开） */
const btnNm2 = makeNode("button", 264, 48);
btnNm2.id = "btn_open_nm2";
btnNm2.text = "OpenNM2";
btnNm2.events = { click: "on_open_nm2" };

const btnIdle = makeNode("button", 16, 88);
btnIdle.id = "btn_close_idle";
btnIdle.text = "CloseIdle";
btnIdle.events = { click: "on_close_idle" };

/* **第二个**对话框节点：用它才能验证"同一时间只支持一个"的**全局**守卫 ——
 * 对同一个节点重复 open 会被更早的 `dlg->native 已存在` 分支拦下，测不到那条守卫。
 * 红测时（故意放行进来的那次）这里必须打开第二个窗口，断言才会红。 */
const btnOther = makeNode("button", 264, 88);
btnOther.id = "btn_open_other";
btnOther.text = "OpenOther";
btnOther.events = { click: "on_open_other" };

const dlgOther = makeNode("dialog", 0, 0);
dlgOther.id = "dlg_other";
dlgOther.text = "Other";
dlgOther.rect = { x: 40, y: 40, w: 260, h: 140 };
dlgOther.children = [
  (() => {
    const b = makeNode("button", 90, 80);
    b.id = "btn_other_ok";
    b.text = "OK";
    b.events = { click: "on_other_ok" };
    return b;
  })(),
];

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

doc.widgets = [lbl, btn, btnNm, btnNm2, btnIdle, btnOther, dlg, dlgOther];

const files = generateC(doc, NAME);
fs.mkdirSync(dir, { recursive: true });
for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.content, "utf8");

/* 真实回调逻辑盖掉空桩（生成器契约：业务写这里） */
const impl = `/**
 * @file    ${NAME}_ui_events.c
 * @brief   多窗体的真机验证回调（探针 tools\\\\probe-modal.c 驱动，结果写 result.log）
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

/**
 * @brief "OpenNM2" 的点击：**已经开着一个**时再开一次（同一时间只支持一个）。
 *        期望守卫返回 0，且原来那个对话框不受影响。
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_open_nm2(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "nm2 result=%d\\n", cg_dialog_open_nonmodal(cg_find(app, "dlg_test")));
    fclose(f);
}

/**
 * @brief "CloseIdle" 的点击：**什么都没开着**的时候关一次 —— 错误路径，不许崩，
 *        也不许把状态写坏（随后还得能正常开）。
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_close_idle(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    FILE *f;
    (void)ev;
    (void)user;

    cg_dialog_close(cg_find(app, "dlg_test"), 0);
    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "idle_close done\\n");   /* 能走到这儿就说明没崩 */
    fclose(f);
}

/**
 * @brief "OpenOther" 的点击：已经开着 dlg_test（非模态）时，去开**另一个**对话框节点。
 *        期望被"同一时间只支持一个"的全局守卫拒绝（返回 0，且不许冒出第二个窗口）。
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_open_other(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "other result=%d\\n", cg_dialog_open_nonmodal(cg_find(app, "dlg_other")));
    fclose(f);
}

/**
 * @brief 第二个对话框里 "OK" 的点击：确定关闭
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_other_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}
`;
fs.writeFileSync(path.join(dir, `${NAME}_ui_events.c`), impl, "utf8");
console.log("written:", dir);
