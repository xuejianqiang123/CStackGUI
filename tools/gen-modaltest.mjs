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
doc.app.width = 520;
doc.app.height = 340;

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
dlgOther.rect = { x: 40, y: 40, w: 340, h: 230 };
/* 窗口级事件（T4 收尾）：非模态对话框此前**收不到** move/size/activate
 * （宿主解析只认主窗口与模态对话框）。回调把宿主标题打进日志 —— "Other" 就证明
 * 宿主是这个对话框，而不是主窗口（"modal test"）。 */
dlgOther.events = { size: "on_dlg_size", move: "on_dlg_move" };
/* 对话框里的 tabs（T4 可选项，此前从未验证）：两页各放一个**文字不同**的按钮，
 * 探针靠"页 1 的显示 / 页 2 的隐藏"来证明切页真的换了页体 —— 光看页签高亮不算数
 * （TCM_SETCURSEL 只换高亮，页体显隐是我们自己的 cgw_sync_tabs_pages 做的）。 */
const tabsOther = makeNode("tabs", 12, 12);
tabsOther.id = "tab_other";
tabsOther.items = ["Page1", "Page2"];
tabsOther.value = 0;
tabsOther.rect = { x: 12, y: 12, w: 300, h: 150 };
tabsOther.events = { select: "on_tabs_select" };
tabsOther.children = [
  (() => {
    const b = makeNode("button", 24, 44);
    b.id = "btn_p1";
    b.text = "OnP1";
    b.page = 0;
    return b;
  })(),
  (() => {
    const b = makeNode("button", 24, 44);
    b.id = "btn_p2";
    b.text = "OnP2";
    b.page = 1;
    return b;
  })(),
];
dlgOther.children = [
  tabsOther,
  (() => {
    const b = makeNode("button", 130, 178);
    b.id = "btn_other_ok";
    b.text = "OK";
    b.events = { click: "on_other_ok" };
    return b;
  })(),
];

/* **对照**用的 tabs：摆在主窗口里。同一套驱动方式，主窗口能切、对话框不能切，
 * 才说明是对话框的问题；两个都切不动，那就是探针的驱动方式不对（不是产品问题）。 */
const tabsMain = makeNode("tabs", 16, 170);
tabsMain.id = "tab_main";
tabsMain.items = ["Page1", "Page2"];
tabsMain.value = 0;
tabsMain.rect = { x: 16, y: 170, w: 300, h: 140 };
tabsMain.children = [
  (() => {
    const b = makeNode("button", 24, 44);
    b.id = "btn_m1";
    b.text = "OnM1";
    b.page = 0;
    return b;
  })(),
  (() => {
    const b = makeNode("button", 24, 44);
    b.id = "btn_m2";
    b.text = "OnM2";
    b.page = 1;
    return b;
  })(),
];

const btnTabs = makeNode("button", 264, 128);
btnTabs.id = "btn_open_tabs";
btnTabs.text = "OpenTabs";
btnTabs.events = { click: "on_open_tabs" };

/* 切页的两个驱动口。为什么不在探针里直接点页签：
 *   ① 真实鼠标输入在本环境注入不了（SendInput 点按钮都没反应，实测 dialog=0）；
 *   ② TCM_SETCURSEL 按 MSDN **不发** TCN_SELCHANGE，只换页签高亮，我们收不到。
 * 所以改在**进程内**照真实点击的顺序模拟：原生先换选中项，再把 TCN_SELCHANGE 发给父窗口 ——
 * 走的正是点击会走的那条路（对话框过程 → cgw_notify_common → 查控件 → 换页体 → 派发 select）。 */
const btnSelMain = makeNode("button", 380, 170);
btnSelMain.id = "btn_sel_main";
btnSelMain.text = "SelMain";
btnSelMain.events = { click: "on_sel_main" };

const btnSelDlg = makeNode("button", 380, 208);
btnSelDlg.id = "btn_sel_dlg";
btnSelDlg.text = "SelDlg";
btnSelDlg.events = { click: "on_sel_dlg" };

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

doc.widgets = [
  lbl, btn, btnNm, btnNm2, btnIdle, btnOther, btnTabs, btnSelMain, btnSelDlg,
  tabsMain, dlg, dlgOther,
];

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
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <commctrl.h>

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

/**
 * @brief "OpenTabs" 的点击：打开**带 tabs 的**那个对话框（非模态，便于探针随后驱动）
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_open_tabs(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "tabs result=%d\\n", cg_dialog_open_nonmodal(cg_find(app, "dlg_other")));
    fclose(f);
}

/**
 * @brief 对话框里 tabs 的 select：证明 TCN_SELCHANGE **走到了我们手里**
 *        （页体显隐是另一回事，由探针按窗口可见性判）
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_tabs_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "tabsel=%d\\n", cg_tabs_selected(self));
    fclose(f);
}

/**
 * @brief 按「类名 + 标题」找顶层窗口（EnumWindows 实现）
 * @param cls 类名
 * @param title 窗口标题
 * @return HWND 命中的窗口；没有返回 NULL
 * @note 不用 FindWindowW：真机实测它在**本进程内**按类名查自己刚注册的窗口会返回 NULL
 *       （而 EnumWindows 能枚举到同一个窗口、外部进程的 FindWindowW 也能找到）——
 *       原因未查明，但结论明确：拿不准的查找方式不用于验证，换已被证明可靠的那条。
 */
struct find_win_ctx {
    const wchar_t *cls;
    const wchar_t *title;
    HWND hit;
};

static BOOL CALLBACK find_win_proc(HWND h, LPARAM lp)
{
    struct find_win_ctx *c = (struct find_win_ctx *)(intptr_t)lp;
    wchar_t cls[64], txt[64];
    GetClassNameW(h, cls, 64);
    GetWindowTextW(h, txt, 64);
    if (!c->hit && wcscmp(cls, c->cls) == 0 && wcscmp(txt, c->title) == 0) c->hit = h;
    return TRUE;
}

static HWND find_toplevel(const wchar_t *cls, const wchar_t *title)
{
    struct find_win_ctx c;
    c.cls = cls;
    c.title = title;
    c.hit = NULL;
    EnumWindows(find_win_proc, (LPARAM)(intptr_t)&c);
    return c.hit;
}

/**
 * @brief 照**真实点击**的顺序制造一次切页：原生先换选中项，再把 TCN_SELCHANGE 发给父窗口
 * @param win_class 目标顶层窗口的类名（CStackGUIMain / CStackGUIDialog）
 * @param win_title 目标顶层窗口的标题
 * @return void 无返回值
 * @note 探针没法直接点页签（本环境注入不了真实输入；TCM_SETCURSEL 又按 MSDN 不发通知），
 *       所以在进程内补齐"控件本来会做的那件事"：消息与真实点击完全同构，
 *       父窗口（主窗口或对话框）收到的就是同一条 WM_NOTIFY。
 *       只用 Win32（EnumWindows / FindWindowEx）拿句柄 —— cg_widget_t 是不透明类型，
 *       业务代码不许戳内部（摸 native 会编译失败，这是契约在保护我们）。
 */
static void fake_selchange(const wchar_t *win_class, const wchar_t *win_title)
{
    HWND win = find_toplevel(win_class, win_title);
    HWND tab, parent;
    NMHDR nm;

    if (!win) return;
    tab = FindWindowExW(win, NULL, L"SysTabControl32", NULL);
    if (!tab) return;
    SendMessageW(tab, TCM_SETCURSEL, 1, 0);    /* 原生换到第 1 页（这一步不发通知） */
    memset(&nm, 0, sizeof(nm));
    nm.hwndFrom = tab;
    nm.idFrom = (UINT_PTR)GetDlgCtrlID(tab);
    nm.code = TCN_SELCHANGE;
    parent = GetParent(tab);
    SendMessageW(parent, WM_NOTIFY, 0, (LPARAM)&nm);
}

/**
 * @brief 定时器回调：真正执行模拟切页
 * @param w 窗口（NULL，线程定时器）
 * @param m 消息
 * @param id 定时器 id：1 = 主窗口的 tabs，2 = 对话框里的 tabs
 * @param t 时间（未使用）
 * @return void 无返回值
 * @note **为什么经由定时器**：按钮回调运行在事件派发栈里，防重入锁（g_win_in_notify）
 *       是挂着的 —— 这时发的嵌套 WM_NOTIFY 会被 cgw_notify_common 开头静默丢掉
 *       （第一版就在这里栽了：两处切页都没反应）。SetTimer(NULL,…) 的回调由消息泵在
 *       **空闲时**调，锁是放开的 —— 时序与真实点击一致（真实点击也不在任何派发栈里）。
 *       "泵里放行嵌套事件"的先例：模态泵与菜单泵都是先把锁存起来清零再进。
 */
static VOID CALLBACK sel_timer_proc(HWND w, UINT m, UINT_PTR id, DWORD t)
{
    (void)w;
    (void)m;
    (void)t;
    KillTimer(NULL, id);
    if (id == 1) fake_selchange(L"CStackGUIMain", L"modal test");
    else fake_selchange(L"CStackGUIDialog", L"Other");
}

/**
 * @brief "SelMain" 的点击：主窗口里那个 tabs 切到第 1 页（基线对照）
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note 主窗口基线走**公共 API**：cg_widget_set_value 对 tabs 会连页体一起切
 *       （cg_win_sync.c 的 sync 分支：TCM_SETCURSEL + cgw_sync_tabs_pages）。
 *       不走 fake_selchange 的原因：EnumWindows 在本进程内找主窗口的时机不稳定
 *       （对话框那次能找到、主窗口这次找不到，原因未查明）——基线只验证
 *       "主窗口里的页体切换是好的"，API 路径足够；通知管线由对话框那条路验证。
 */
void on_sel_main(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    cg_widget_set_value(cg_find(app, "tab_main"), 1);
}

/**
 * @brief "SelDlg" 的点击：**对话框里**那个 tabs 切到第 1 页
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_sel_dlg(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    SetTimer(NULL, 2, 30, sel_timer_proc);
}

/**
 * @brief 对话框的 size 事件（窗口级）：把**宿主**与新的客户区尺寸写进日志
 * @param self 事件宿主（应当是对话框节点，不是主窗口）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note 宿主标题就是判据：打 "Other" 说明派到了对话框上；打 "modal test" 说明派错窗口。
 *       size 事件里 ev_x / ev_y 是**新的客户区宽高**（逻辑像素，与 .cgui 同一套单位）。
 */
void on_dlg_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "dlgsize host=%s w=%d h=%d\\n",
            self ? (cg_widget_text(self) ? cg_widget_text(self) : "-") : "-",
            cg_event_x(self), cg_event_y(self));
    fclose(f);
}

/**
 * @brief 对话框的 move 事件（窗口级）：把宿主与新的屏幕坐标写进日志
 * @param self 事件宿主（应当是对话框节点）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
void on_dlg_move(cg_widget_t *self, cg_event_t ev, void *user)
{
    FILE *f;
    (void)ev;
    (void)user;

    f = fopen("result.log", "a");
    if (!f) return;
    fprintf(f, "dlgmove host=%s x=%d y=%d\\n",
            self ? (cg_widget_text(self) ? cg_widget_text(self) : "-") : "-",
            cg_event_x(self), cg_event_y(self));
    fclose(f);
}
`;
fs.writeFileSync(path.join(dir, `${NAME}_ui_events.c`), impl, "utf8");
console.log("written:", dir);
