/* gen-darkaudit.mjs -- 生成「深色审计」工程：把 SDD T1 对照表里的每个部位摆进一页
 *
 * 用途：T1（内置皮肤深色化）前后各截一张图，逐部位核对——深色下不再"发亮的浅灰"，
 *       浅色下一像素不变。后来也成了角色（G1）@引用深浅两态的取证页。
 *       2026-10-09 起也管**对话框**：深色审计原先只验主窗口，对话框是独立 HWND，
 *       深色下到底长什么样没人看过 —— 这里在 ready 里非模态打开它并存一张 PNG。
 *       生成物在 build\darkaudit\（cgbuild 可随时重编）。
 * 用法：node tools\gen-darkaudit.mjs           深色那张（dlg-dark.png）
 *       node tools\gen-darkaudit.mjs --light   浅色那张（dlg-light.png）
 * 依赖：build\modeljs（tsc 命令见 probe-merge-e2e.mjs 头部）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是深色皮肤的**永久审计页**：
 * 深浅两态各一张截图，逐控件核对（禁用按钮 / meter / 无底色 edit / @引用取色 / 对话框）。
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { generateC } from "../build/modeljs/codegen.js";
import { emptyDoc, makeNode } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = path.join(root, "build", "darkaudit");

const lightMode = process.argv.includes("--light");   /* 浅色对照那张 */
const shotName = lightMode ? "dlg-light.png" : "dlg-dark.png";
const mainShot = lightMode ? "main-light.png" : "main-dark.png";

const doc = emptyDoc();
doc.app.title = "dark audit";
doc.app.dark = !lightMode;
doc.app.width = 1200;
doc.app.height = 900;
/* 对话框要在**无人操作**的情况下出现，才能自动截图取证：ready 里开（见下面的事件实现）。 */
doc.app.events = { ready: "on_ready" };

const add = (n) => doc.widgets.push(n);
const label = (id, text, x, y, w) => {
  const n = makeNode("label", x, y);
  n.id = id;
  n.text = text;
  n.rect.w = w;
  n.rect.h = 18;
  add(n);
};

/* 按钮两态：普通（品牌紫 tonal，深色下保持）+ 禁用（T1 修：禁用灰在深底上发亮） */
const b1 = makeNode("button", 20, 40);
b1.id = "btn_normal";
b1.text = "普通按钮";
b1.rect.w = 120;
b1.rect.h = 34;
add(b1);
const b2 = makeNode("button", 20, 86);
b2.id = "btn_disabled";
b2.text = "禁用按钮";
b2.rect.w = 120;
b2.rect.h = 34;
b2.enabled = false;
add(b2);
label("lab_btn", "↑ 按钮普通/禁用", 150, 96, 140);

/* edit（无 bg：T1 修——客户区白底 + 边框带 COLOR_WINDOW） */
const e1 = makeNode("edit", 20, 136);
e1.id = "ed_plain";
e1.text = "bg-less edit";
e1.rect.w = 220;
e1.rect.h = 30;
add(e1);
label("lab_ed", "↑ 无背景色输入框", 250, 142, 150);

/* 复选 + 开关 + 进度 + 滑块（槽/框 T1 核对；选中紫保持） */
const c1 = makeNode("checkbox", 20, 184);
c1.id = "chk";
c1.text = "复选框";
c1.rect.w = 130;
c1.rect.h = 24;
c1.checked = true;
add(c1);
const sw = makeNode("switch", 170, 182);
sw.id = "sw";
sw.rect.w = 56;
sw.rect.h = 28;
sw.checked = true;
add(sw);
const sw2 = makeNode("switch", 240, 182);
sw2.id = "sw_off";
sw2.rect.w = 56;
sw2.rect.h = 28;
add(sw2);
label("lab_sw", "↑ 开/关开关", 310, 188, 120);
const pg = makeNode("progress", 20, 226);
pg.id = "pg";
pg.rect.w = 260;
pg.rect.h = 14;
pg.value = 62;
add(pg);
const sl = makeNode("slider", 20, 258);
sl.id = "sl";
sl.rect.w = 260;
sl.rect.h = 32;
sl.value = 40;
add(sl);

/* 圆环 / 仪表（T1 修：meter 指针帽深底不可见）/ 状态灯（灭点） */
const rg = makeNode("progressring", 320, 40);
rg.id = "rg";
rg.rect.w = 110;
rg.rect.h = 110;
rg.value = 40;
add(rg);
const mt = makeNode("meter", 440, 40);
mt.id = "mt";
mt.rect.w = 120;
mt.rect.h = 100;
mt.value = 70;
add(mt);
const ld = makeNode("led", 330, 165);
ld.id = "ld_off";
ld.rect.w = 20;
ld.rect.h = 20;
add(ld);
label("lab_ld", "↑ 灭态灯", 358, 168, 90);

/* 无底色标签：文字色跟主题（既有行为，回归核对） */
label("lab_plain", "无底色标签：文字色跟随主题（既有行为）", 20, 310, 380);

/* ---------------- G1/T2：色彩角色引用（深浅各取对应槽，A4） ---------------- */
doc.roles = {
  accent: { light: "#6750A4", dark: "#D0BCFF" },
  danger: { light: "#B3261E", dark: "#F2B8B5" },
};
const la = makeNode("label", 20, 344);
la.id = "lab_accent";
la.text = "fg=@accent 的标签（浅紫 / 深紫各按模式）";
la.rect.w = 380;
la.rect.h = 18;
la.style = { fg: "@accent" };
add(la);
const ldg = makeNode("label", 20, 368);
ldg.id = "lab_danger";
ldg.text = "fg=@danger 的标签（红 / 浅红各按模式）";
ldg.rect.w = 380;
ldg.rect.h = 18;
ldg.style = { fg: "@danger" };
add(ldg);
const br = makeNode("button", 420, 340);
br.id = "btn_role";
br.text = "bg=@accent";
br.rect.w = 130;
br.rect.h = 34;
br.style = { bg: "@accent" };
add(br);

/* ---------------- 主窗口也放一个 tabs（对照：页体在深浅两态下该是什么色） ----------------
 * 对话框那张图里页体是**亮白**的；要看这是"对话框特有"还是"tabs 页体普遍问题"，
 * 就得让主窗口有个同款 tabs 同一次跑出来比。 */
const mTabs = makeNode("tabs", 20, 404);
mTabs.id = "tabs_main";
mTabs.items = ["页一", "页二"];
mTabs.rect = { x: 20, y: 404, w: 300, h: 120 };
const mTabsLab = makeNode("label", 16, 20);
mTabsLab.id = "tabs_lab";
mTabsLab.text = "主窗口页体里的无底色标签";
mTabsLab.rect.w = 240;
mTabsLab.rect.h = 18;
mTabs.children = [mTabsLab];
add(mTabs);

/* ---------------- 对话框：深色下的**独立 HWND**（此前无人核对） ----------------
 * 摆的和主窗口审计同一批部位：无底色标签 / @accent 引用 / 禁用按钮 / 无底色 edit / 复选框，
 * 外加两页 tabs —— 一次性看清"第二个窗口"在深色下跟不跟得上。 */
const dlg = makeNode("dialog", 0, 0);
dlg.id = "dlg_audit";
dlg.text = "对话框（深/浅对照）";
dlg.rect = { x: 40, y: 40, w: 330, h: 250 };

const dTabs = makeNode("tabs", 12, 12);
dTabs.id = "dlg_tabs";
dTabs.items = ["页一", "页二"];
dTabs.rect = { x: 12, y: 12, w: 300, h: 160 };
const mk = (type, id, x, y, w, h, page) => {
  const n = makeNode(type, x, y);
  n.id = id;
  n.rect.w = w;
  n.rect.h = h;
  n.page = page;
  return n;
};
const dLab = mk("label", "dlg_lab", 16, 24, 260, 18, 0);
dLab.text = "对话框里的无底色标签";
const dLabRole = mk("label", "dlg_lab_role", 16, 56, 260, 18, 0);
dLabRole.text = "fg=@accent 的标签";
dLabRole.style = { fg: "@accent" };
const dBtnDis = mk("button", "dlg_btn_disabled", 16, 92, 120, 34, 0);
dBtnDis.text = "禁用按钮";
dBtnDis.enabled = false;
const dEd = mk("edit", "dlg_ed", 16, 24, 200, 30, 1);
dEd.text = "bg-less edit";
const dChk = mk("checkbox", "dlg_chk", 16, 70, 130, 24, 1);
dChk.text = "复选框";
dChk.checked = true;
dTabs.children = [dLab, dLabRole, dBtnDis, dEd, dChk];

const dOk = mk("button", "dlg_ok", 120, 190, 88, 30, 0);
dOk.text = "确定";
dOk.events = { click: "on_dlg_ok" };
dlg.children = [dTabs, dOk];
add(dlg);

/* ---------------- 补审计（2026-10-09）：此前审计页**从未摆过**的类型 ----------------
 * tabs 页体那次的教训是"文档说不跟随的那半其实能改"，而它之所以一直没被发现，就是因为
 * 审计页里没有 tabs。所以这里把剩下的系统/自绘类型都摆上：combo / listbox / tree /
 * table / richtext / terminal / chart / 带调节器的数值框 —— 让深浅两态各出一张图来说话。 */
const cmb = makeNode("combo", 600, 40);
cmb.id = "cmb";
cmb.items = ["选项一", "选项二", "选项三"];
cmb.rect.w = 170;
cmb.rect.h = 30;
add(cmb);

const lb = makeNode("listbox", 600, 80);
lb.id = "lb";
lb.items = ["条目一", "条目二", "条目三", "条目四"];
lb.rect.w = 170;
lb.rect.h = 120;
add(lb);

const tr = makeNode("tree", 600, 210);
tr.id = "tr";
tr.nodes = [{ text: "根节点", expanded: true, children: [{ text: "子节点一" }, { text: "子节点二" }] }];
tr.rect.w = 170;
tr.rect.h = 150;
add(tr);

const tb = makeNode("table", 600, 375);
tb.id = "tb";
tb.columns = [{ title: "列一", w: 120 }, { title: "列二", w: 140 }];
tb.rows = [["甲", "100"], ["乙", "200"], ["丙", "300"]];
tb.rect.w = 280;
tb.rect.h = 150;
add(tb);

const rt = makeNode("richtext", 20, 540);
rt.id = "rt";
rt.text = "多行富文本：深色下底与字都应该跟着变";
rt.rect.w = 280;
rt.rect.h = 110;
add(rt);

const term = makeNode("terminal", 320, 540);
term.id = "term";
term.text = "terminal: 也应深底浅字";
term.rect.w = 260;
term.rect.h = 110;
add(term);

const ch = makeNode("chart", 600, 540);
ch.id = "ch";
ch.items = ["周一", "周二", "周三"];
ch.rect.w = 280;
ch.rect.h = 110;
add(ch);

const sp = makeNode("edit", 20, 670);
sp.id = "sp_num";
sp.text = "42";
sp.spin = 1;
sp.rect.w = 180;
sp.rect.h = 30;
add(sp);

/* 覆盖表里剩下的系统控件也摆上：菜单栏 / 工具栏 / 状态栏 / 月历 / 日期时间。
 * 目标是让审计页**覆盖覆盖表里的每一个类型** —— 以后（比如换了 Win10、或系统更新后）
 * 重跑一次 `gen-darkaudit` 就能一次性复核所有"跟随/不跟随"的判断，不用再靠记忆。 */
const mn = makeNode("menu", 0, 0);
mn.id = "mn";
mn.nodes = [{ text: "文件", children: [{ text: "新建", id: "m_new" }] }, { text: "帮助", children: [{ text: "关于", id: "m_about" }] }];
add(mn);

const tbar = makeNode("toolbar", 320, 670);
tbar.id = "tbar";
tbar.items = ["新建", "打开", "保存"];
tbar.rect.w = 260;
tbar.rect.h = 40;
add(tbar);

const mc = makeNode("monthcal", 910, 40);
mc.id = "mc";
mc.rect.w = 230;
mc.rect.h = 180;
add(mc);

const dp = makeNode("datepicker", 910, 240);
dp.id = "dp";
dp.text = "2026-10-09";
dp.rect.w = 200;
dp.rect.h = 30;
add(dp);

const sb = makeNode("statusbar", 0, 856);
sb.id = "sb";
sb.items = ["状态栏：深色下跟不跟？", "第二段"];
sb.rect.w = 1200;
sb.rect.h = 24;
add(sb);

/* 时钟：只为了"等窗口画好再截图"（ready 里立刻截会得到一张空白窗）。
 * 用框架自己的 timer 控件而不是 SetTimer —— 后者的回调在 ready 里设下去实测**不触发**
 * （原因未查明；用框架的时钟既确定又顺手）。 */
const tm = makeNode("timer", 0, 0);
tm.id = "tm_shot";
tm.period = 700;
tm.events = { tick: "on_shot" };   /* timer 的事件名是 tick（不是 clock） */
add(tm);

/* ---------------- 生成工程 ---------------- */
const files = generateC(doc, "darkaudit");
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });
for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.content, "utf8");

/* 真实回调盖掉空桩：ready 里开对话框并存 PNG —— 审计要"无人操作"也能取证，
 * 本环境注入不了真实输入（SendInput 点按钮都没反应），只能靠这个。 */
const impl = `/**
 * @file    darkaudit_ui_events.c
 * @brief   深色审计页的回调：ready 时非模态打开对话框并存一张 PNG
 */
#include "darkaudit_ui_events.h"

#include <stdio.h>
#include <windows.h>

static int g_shot_done = 0;

/**
 * @brief 窗口就绪：非模态打开对话框并把它截成 PNG
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note 审计页要"无人操作"也能取证：本环境注入不了真实输入，点不了按钮开对话框，
 *       所以放在 ready 里开 —— 对话框一建好就画，紧跟着截图是有内容的（实测）。
 *       主窗口那张不能在这儿截：此刻它还没绘制，截出来是纯白空窗（也实测过），
 *       留给下面 on_shot 的时钟回调。
 */
void on_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *dlg;
    (void)ev;
    (void)user;

    dlg = cg_find(app, "dlg_audit");
    if (!dlg) return;
    if (cg_dialog_open_nonmodal(dlg)) cg_widget_screenshot(dlg, "${shotName}");
}

/**
 * @brief 时钟回调：等窗口画好之后截主窗口（只做一次）
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note 用框架的 timer 控件而不是 SetTimer(NULL,…)：后者在 ready 里设下去实测**不触发**
 *       （同环境同写法，modaltest 在点击回调里设却正常——原因未查明，不深挖；
 *       审计页用框架时钟既确定又顺手）。
 */
void on_shot(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;

    if (g_shot_done) return;
    g_shot_done = 1;
    cg_win_screenshot(app, "${mainShot}");   /* 主窗口（含对照用的 tabs） */
}

/**
 * @brief 对话框里"确定"的点击：带结果 1 关闭
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
fs.writeFileSync(path.join(dir, "darkaudit_ui_events.c"), impl, "utf8");
console.log(`[ok] 写出 ${files.length} 个文件到 ${dir}`);
execFileSync("node", [path.join(root, "designer", "electron", "cgbuild.mjs"), dir], { stdio: "inherit" });
