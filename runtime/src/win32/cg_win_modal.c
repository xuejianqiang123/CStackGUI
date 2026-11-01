/**
 * @file    cg_win_modal.c
 * @brief   模态对话框：独立顶层窗口 + 子树原生懒建 + 内层消息泵
 * @details 契约见 docs/CGUI-FORMAT.md 第 4 节的 dialog 行；设计见
 *          docs/SDD_multi-window.md（决策 W：app->widgets 保持一棵树
 *          不分区 —— 对话框子树的 widget 对象启动时就建好了，这里只负责"原生"的生死：
 *          open 时建、close 时收。事件表一张照用（全局按名查），id 天然全局唯一）。
 */
#include "cg_win_internal.h"

/* 对话框窗口类：与主窗口同一个窗口过程。控件派发（cgw_notify_from /
 * cg_widget_by_ctrl_id）按 HWND/ctrl_id 在 app 上查，本来就不看是哪个顶层窗口；
 * 只有窗口级事件的宿主要认 —— cgw_MainProc 开头按 HWND 判（主窗口=__window，
 * 对话框=dialog 节点自己）。 */
#define CGW_DIALOG_CLASS L"CStackGUIDialog"

/**
 * @brief 注册对话框窗口类（只注册一次）
 * @param app 应用；取它的图标当对话框图标
 * @return void 无返回值（RegisterClassExW 失败多半是重复注册，静默忽略）
 */
static void register_dialog_class(const cg_app_t *app)
{
    static int done = 0;
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = cgw_MainProc;                 /* 与主窗口同一个过程 */
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = (st && st->icon) ? st->icon : LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = CGW_DIALOG_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/**
 * @brief 给对话框建顶层窗口与整棵子树的原生（cg_dialog_open 的"建"一半）
 * @param dlg dialog 节点；成功时它的 native 被填成对话框 HWND
 * @return int 0 成功；-1 建窗失败
 * @note 子树控件的父窗口就是对话框自己：cgw_parent_hwnd_of 走 parent->native，
 *       所以只要先把 dlg->native 填上，子控件建出来就天然挂在对话框里（主窗口一个字不用改）。
 *       尺寸：.cgui 的 rect.w/h 是**客户区**（逻辑像素），按 DPI 换算后用
 *       AdjustWindowRectEx 折成含边框的外框尺寸 —— 与主窗口同一套换算。
 */
static int dialog_create_native(cg_widget_t *dlg)
{
    cg_app_t *app = dlg->app;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;   /* 固定框：只有关闭钮 */
    RECT rc;
    int ww, wh, wx, wy;
    HWND owner = (HWND)app->hwnd;
    HWND h;
    wchar_t *wt = cgw_wc_from_utf8((dlg->text && *dlg->text) ? dlg->text : "对话框");

    register_dialog_class(app);

    rc.left = 0;
    rc.top  = 0;
    rc.right  = cgw_scale_px(app, dlg->w);
    rc.bottom = cgw_scale_px(app, dlg->h);
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    ww = rc.right - rc.left;
    wh = rc.bottom - rc.top;

    /* 位置：缺省居中于 owner；.cgui 里 center:false 时按 rect.x/y 摆 ——
       与主窗口的 app.x/y 同一口径：**原始逻辑像素直接当屏幕坐标用**，
       不做 DPI 换算（先例：cg_win_window.c 的 has_pos 分支就是裸赋值） */
    if (dlg->center) {
        RECT ro;
        GetWindowRect(owner, &ro);
        wx = ro.left + ((ro.right - ro.left) - ww) / 2;
        wy = ro.top + ((ro.bottom - ro.top) - wh) / 2;
    } else {
        wx = dlg->x;
        wy = dlg->y;
    }

    h = CreateWindowExW(0, CGW_DIALOG_CLASS, wt ? wt : L"", style,
                        wx, wy, ww, wh, owner, NULL, GetModuleHandleW(NULL), app);
    free(wt);
    if (!h) {
        fprintf(stderr, "[cstackgui] 创建对话框失败: %s #%s\n", dlg->type, dlg->id ? dlg->id : "-");
        return -1;
    }
    dlg->native = (void *)h;

    /* 整窗圆角与主窗口同一观感（M3）：dwmapi 动态加载，老系统没有就跳过 */
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *dwma_t)(HWND, DWORD, LPCVOID, DWORD);
            dwma_t set_attr = (dwma_t)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (set_attr) {
                int pref = 2;   /* DWMWCP_ROUND */
                set_attr(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(int));
            }
            FreeLibrary(dwm);
        }
    }

    /* 子树建原生：父先于子（树序），控件建好自带初始同步与焦点子类化 */
    cgw_create_native_tree(dlg);
    return 0;
}

/**
 * @brief 收掉模态对话框子树的原生（销毁 + 释放后端资源 + 清句柄）
 * @param app 应用
 * @param dlg dialog 节点（它自己的 native 正在销毁中，这里只收子树）
 * @return void 无返回值
 * @note 由 cgw_MainProc 的 WM_DESTROY（非主窗口分支）调用。此刻子窗口还活着，
 *       逐个收干净才不留野指针；不收的话 win_shutdown 会对着一堆已销毁的句柄
 *       再 DestroyWindow 一遍（它按 app->widgets 统一释放，对不上动态生死的子树）。
 */
void cgw_dialog_teardown(cg_app_t *app, cg_widget_t *dlg)
{
    size_t i;
    (void)app;
    for (i = 0; dlg->children && i < dlg->nchildren; i++) {
        cgw_destroy_native_tree(dlg->children[i]);
    }
    dlg->native = NULL;   /* 对话框自己的 HWND 正在销毁中，置空别再碰 */
}

/**
 * @brief 递归找 native == hwnd 的 dialog 节点（cgw_dialog_by_hwnd 的实现）
 * @param w 当前节点（首次调用传 app->window，即树的根伪控件）
 * @param hwnd 要找的顶层窗口
 * @return cg_widget_t* 命中的 dialog 节点；没找到返回 NULL
 */
static cg_widget_t *dialog_by_hwnd_walk(cg_widget_t *w, HWND hwnd)
{
    size_t i;
    if (!w) return NULL;
    if (w->type && strcmp(w->type, "dialog") == 0 && (HWND)w->native == hwnd) return w;
    for (i = 0; w->children && i < w->nchildren; i++) {
        cg_widget_t *hit = dialog_by_hwnd_walk(w->children[i], hwnd);
        if (hit) return hit;
    }
    return NULL;
}

/**
 * @brief 按 HWND 找打开着的 dialog 节点（非模态对话框没有 modal_dlg 快路径可用）
 * @param app 应用
 * @param hwnd 目标顶层窗口
 * @return cg_widget_t* 命中的 dialog 节点；没有返回 NULL
 * @note 全树 O(n)，只在 WM_CLOSE / WM_DESTROY 这类低频消息里调，别放进逐消息的路径。
 */
cg_widget_t *cgw_dialog_by_hwnd(cg_app_t *app, HWND hwnd)
{
    if (!app || !hwnd) return NULL;
    return dialog_by_hwnd_walk(app->window, hwnd);
}

/**
 * @brief 找一个还开着（native 非空）的 dialog 节点 —— "同一时间只支持一个"的守卫用
 * @param w 当前节点（首次调用传 app->window）
 * @return cg_widget_t* 第一个还开着的 dialog 节点；没有返回 NULL
 */
static cg_widget_t *any_open_dialog_walk(cg_widget_t *w)
{
    size_t i;
    if (!w) return NULL;
    if (w->type && strcmp(w->type, "dialog") == 0 && w->native) return w;
    for (i = 0; w->children && i < w->nchildren; i++) {
        cg_widget_t *hit = any_open_dialog_walk(w->children[i]);
        if (hit) return hit;
    }
    return NULL;
}

/**
 * @brief 现在有没有任何对话框开着（模态或非模态）——"同一时间只支持一个"的守卫
 * @param app 应用
 * @return int 1 = 有；0 = 没有
 */
static int any_dialog_open(cg_app_t *app)
{
    if (app->modal_dlg) return 1;
    return any_open_dialog_walk(app->window) != NULL;
}

/**
 * @brief 打开模态对话框（公共 API，语义见 cstackgui.h）
 * @param dlg dialog 节点
 * @return int 1 = 回调里 cg_dialog_close(self, 1) 过；0 = 取消 / Esc / 没开成
 */
int cg_dialog_open(cg_widget_t *dlg)
{
    cg_app_t *app;
    HWND owner, dh;
    MSG msg;
    int saved_notify;

    if (!dlg || !dlg->app) return 0;
    app = dlg->app;
    owner = (HWND)app->hwnd;
    if (!owner || !app->natives) return 0;      /* 窗口还没建（回调时序不对） */
    if (any_dialog_open(app)) {
        fprintf(stderr, "[cstackgui] 已有一个对话框开着（模态或非模态），本次 open 忽略（同一时间只支持一个）\n");
        return 0;
    }
    if (dlg->native) {
        fprintf(stderr, "[cstackgui] 对话框 %s 已经开着\n", dlg->id ? dlg->id : "-");
        return 0;
    }
    if (dialog_create_native(dlg) != 0) return 0;

    app->modal_dlg = dlg;
    app->modal_result = 0;

    /* 模态：禁用 owner；恢复也在这里做（泵退出后），不靠 WM_ENABLE 消息 */
    EnableWindow(owner, FALSE);
    ShowWindow((HWND)dlg->native, SW_SHOW);

    /* **嵌套泵期间必须放开防重入锁**：对话框的整个生命周期都跑在"打开它的那个回调"
     * 的栈里，锁还挂着的话，对话框里每个控件通知都会被 cgw_notify_from 直接丢掉
     * （点 OK 什么都不发生）—— 这是标准用法（按钮回调里开对话框），不是边角。
     * 先例：tray_callback 的 TrackPopupMenu、cg_win_menu 的菜单泵，都是存起来→清零→恢复。 */
    saved_notify = g_win_in_notify;
    g_win_in_notify = 0;

    dh = (HWND)dlg->native;
    while (app->modal_dlg && !app->quit) {
        int r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) {
            /* WM_QUIT 被内层泵吃掉就丢了：原样还给外层（标准模态泵的做法） */
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        /* Esc = 取消（对话框惯例）：在 IsDialogMessage 之前拦 ——
         * 主窗口的 escClose 是 app 级开关，对话框不该看它脸色 */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            cg_dialog_close(dlg, 0);
            continue;
        }
        /* Tab / 方向键导航交给 IsDialogMessage；其余照常派发 */
        if (!IsDialogMessageW(dh, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    g_win_in_notify = saved_notify;
    EnableWindow(owner, TRUE);
    return app->modal_result;
}

/**
 * @brief 关闭当前打开的模态对话框（公共 API，语义见 cstackgui.h）
 * @param self 触发事件的控件；对话框自己或它里面的控件都行
 * @param result 交给 cg_dialog_open 返回值的结果（1 = 确定，0 = 取消）
 * @return void 无返回值
 */
void cg_dialog_close(cg_widget_t *self, int result)
{
    cg_app_t *app = self ? self->app : NULL;
    cg_widget_t *dlg;
    cg_widget_t *w;

    if (!app) return;
    dlg = app->modal_dlg;
    if (!dlg) {
        /* 非模态对话框也走这里：self 沿父链核对到哪个 dialog，就关哪个（result 无返回值语义，忽略） */
        for (w = self; w && !(w->type && strcmp(w->type, "dialog") == 0); w = w->parent) {
        }
        if (w && w->native) {
            PostMessageW((HWND)w->native, WM_CLOSE, 0, 0);
            return;
        }
        return;
    }
    /* self 必须是对话框自己或它的子控件：沿父链核对，别的控件调不动它 */
    for (w = self; w && w != dlg; w = w->parent) {
    }
    if (w != dlg) {
        fprintf(stderr, "[cstackgui] cg_dialog_close 的 self 不是当前对话框里的控件\n");
        return;
    }
    app->modal_result = result ? 1 : 0;
    /* 走 WM_CLOSE：close/closequery 回调照常触发（cg_cancel_close 还能拦下不关），
     * 销毁发生在泵里而不是回调栈中间 —— 回调返回后控件还活着 */
    PostMessageW((HWND)dlg->native, WM_CLOSE, 0, 0);
}

/**
 * @brief 打开一个非模态对话框（公共 API，语义见 cstackgui.h）
 * @param dlg dialog 节点
 * @return int 1 = 已打开（立即返回，不等关闭）；0 = 没开成
 * @note 与模态共用 dialog_create_native（独立 HWND + 子树懒建）；区别是**不禁用主窗口、
 *       不进内层泵** —— 对话框活在主泵里，生命周期到 WM_DESTROY 为止。
 *       非模态没有返回值语义（cg_dialog_close 的 result 对它无意义）。
 */
int cg_dialog_open_nonmodal(cg_widget_t *dlg)
{
    cg_app_t *app;

    if (!dlg || !dlg->app) return 0;
    app = dlg->app;
    if (!app->hwnd || !app->natives) return 0;  /* 窗口还没建（回调时序不对） */
    if (any_dialog_open(app)) {
        fprintf(stderr, "[cstackgui] 已有一个对话框开着（模态或非模态），本次 open 忽略（同一时间只支持一个）\n");
        return 0;
    }
    if (dlg->native) {
        fprintf(stderr, "[cstackgui] 对话框 %s 已经开着\n", dlg->id ? dlg->id : "-");
        return 0;
    }
    if (dialog_create_native(dlg) != 0) return 0;

    /* 非模态：不禁用主窗口、不进泵 —— ShowWindow 完立即返回，对话框活在主消息循环里 */
    ShowWindow((HWND)dlg->native, SW_SHOW);
    return 1;
}
