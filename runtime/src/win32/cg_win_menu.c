/**
 * @file    cg_win_menu.c
 * @brief   菜单构建与弹出、工具栏按钮填充
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/** 菜单按钮：点击时在按钮下方弹出菜单，选中项直接派发（TPM_RETURNCMD 省掉消息路由） */
/**
 * @brief 菜单按钮：点击时在按钮下方弹出菜单，选中项直接派发
 * @param app 所属应用；用于前置窗口与前后的防重入状态
 * @param w 菜单 / 菜单按钮控件；提供行表
 * @param hwnd 触发按钮的窗口；菜单从它的下边缘弹出
 * @return void 无返回值（菜单为空或建表失败时什么都不做）
 * @note 用"返回选中命令"的方式取结果，省掉一整套消息路由
 */
void cgw_popup_menu_for(cg_app_t *app, cg_widget_t *w, HWND hwnd)
{
    HMENU hm;
    RECT  r;
    int   cmd;
    int   saved = g_win_in_notify;

    hm = cgw_menu_build(w, 0);
    if (!hm || w->nmenu == 0) {
        if (hm) DestroyMenu(hm);
        return;
    }
    GetWindowRect(hwnd, &r);

    /* TrackPopupMenu 自己会跑消息循环：这段时间放开防重入，
     * 否则弹出菜单期间其它控件的事件会被吞掉。 */
    g_win_in_notify = 0;
    if (app->hwnd) SetForegroundWindow((HWND)app->hwnd);
    cmd = (int)TrackPopupMenu(hm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              r.left, r.bottom, 0, (HWND)app->hwnd, NULL);
    if (app->hwnd) PostMessageW((HWND)app->hwnd, WM_NULL, 0, 0);
    g_win_in_notify = saved;

    DestroyMenu(hm);
    if (cmd) cg_dispatch_menu_cmd(app, cmd);
}

/**
 * @brief 按运行时行表建 HMENU（行表里父项下标一定小于子项，一遍扫过去就够）
 * @param w menu / menubutton 控件
 * @param as_bar 1 = 菜单栏（CreateMenu，给 SetMenu 用）；0 = 弹出菜单（CreatePopupMenu）
 * @return HMENU 菜单句柄（调用方挂载或 DestroyMenu）
 */
HMENU cgw_menu_build(const cg_widget_t *w, int as_bar)
{
    HMENU  root;
    HMENU *subs;

    /* 顶层容器：挂到窗口上当菜单栏必须用 CreateMenu；弹出菜单必须用 CreatePopupMenu。
     * 用错的话 SetMenu 会静默失败——窗口上根本看不到菜单栏（这个坑真实踩过）。 */
    root = as_bar ? CreateMenu() : CreatePopupMenu();
    if (!root || w->nmenu <= 0) return root;

    subs = (HMENU *)calloc((size_t)w->nmenu, sizeof(HMENU));
    if (!subs) return root;

    for (int i = 0; i < w->nmenu; i++) {
        const cg_menu_row_t *r = &w->menu[i];
        HMENU        parent = root;
        const char  *text = r->text;
        const char  *accel = NULL;
        int          act = (r->action && *r->action) ? cg_action_index(w->app, r->action) : -1;
        int          checked = r->checked;
        int          enabled = r->enabled;   /* 行表是真相；动作项下面会被动作的值覆盖 */
        wchar_t     *wt;
        DWORD        flags = MF_STRING;

        if (r->parent >= 0 && r->parent < w->nmenu && subs[r->parent]) parent = subs[r->parent];

        /* 动作项：**勾选与启用是运行期状态，只有后端知道怎么落到 HMENU 上**；
         * 文字不在这里取 —— 它已经在 cg_action_build() 里归一化进 r->text 了
         * （静态规则放 core，一条规则只有一份实现；这里只处理"会变的那部分"）。 */
        if (act >= 0) {
            const cg_action_row_t *a = &w->app->actions[act];
            checked = a->checked;
            enabled = a->enabled;
            if (a->accel && *a->accel) accel = a->accel;
        }

        if (r->text && strcmp(r->text, "-") == 0) {
            AppendMenuW(parent, MF_SEPARATOR, 0, NULL);
            continue;                      /* 分隔线没有文字，别走下面那条分配 wt 的路 */
        }

        if (r->popup) {
            wt = cgw_wc_from_utf8(text);
            subs[i] = CreatePopupMenu();
            if (checked) flags |= MF_CHECKED;
            AppendMenuW(parent, flags | MF_POPUP, (UINT_PTR)subs[i], wt ? wt : L"");
        } else {
            /* 快捷键显示：菜单项文字里的 `\t` 会把后半段右对齐显示 —— 于是
             * "Ctrl+S" 在菜单上就看得见，不用另找地方解释快捷键是什么。 */
            char buf[256];
            if (accel) snprintf(buf, sizeof(buf), "%s\t%s", text ? text : "", accel);
            wt = cgw_wc_from_utf8(accel ? buf : (text ? text : ""));
            if (checked)  flags |= MF_CHECKED;
            if (!enabled) flags |= MF_GRAYED;
            AppendMenuW(parent, flags, (UINT_PTR)r->cmd, wt ? wt : L"");
        }
        free(wt);
    }
    free(subs);
    return root;
}

/**
 * @brief 动作的启用 / 勾选变了：同步到**菜单项**（控件那一半走控件的启用接口）
 * @param app 所属应用；用它的窗口菜单栏与动作表
 * @param name 动作名；据此找到挂在菜单栏上的对应项
 * @return void 无返回值
 * @note 只处理窗口菜单栏那一份：菜单按钮的弹出菜单是**每次点击时现场重建**的，
 *       建的时候自然读到最新状态，不需要同步
 */
void cg_win_action_sync(cg_app_t *app, const char *name)
{
    const cg_action_row_t *a;
    HMENU  hm;
    int    act;

    if (!app || !name || !app->hwnd) return;
    act = cg_action_index(app, name);
    if (act < 0) return;
    a  = &app->actions[act];
    hm = GetMenu((HWND)app->hwnd);
    if (!hm) return;

    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        for (int i = 0; i < w->nmenu; i++) {
            const cg_menu_row_t *r = &w->menu[i];
            if (!r->action || strcmp(r->action, name) != 0 || r->cmd == 0) continue;
            EnableMenuItem(hm, (UINT)r->cmd,
                           MF_BYCOMMAND | (a->enabled ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(hm, (UINT)r->cmd,
                          MF_BYCOMMAND | (a->checked ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    /* 菜单栏不会因为 EnableMenuItem 自己重画，得推它一把 */
    DrawMenuBar((HWND)app->hwnd);
}

/**
 * @brief 窗口菜单栏是哪个控件：**第一个**类型为 menu 的
 * @param app 所属应用；在它的控件链表里找
 * @return cg_widget_t * 窗口菜单栏控件；没有则 NULL（归 app，勿释放）
 * @note 与建窗时那段扫描同一口径 —— 多出来的 menu 在建窗时就报过"被忽略"，
 *       所以这里的"第一个"与当时的"那一个"永远是同一个
 */
static cg_widget_t *bar_menu_of(cg_app_t *app)
{
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (cgw_is_t(w, "menu")) return w;
    }
    return NULL;
}

/** @brief 按客户区尺寸重算窗口大小（补上菜单栏占掉的高度） */
/**
 * @brief 按客户区尺寸重算窗口大小（补上菜单栏占掉的高度）
 * @param app 所属应用；提供文档逻辑尺寸与主窗口
 * @return void 无返回值（app 或主窗口为空时直接返回）
 */
void cgw_menu_fit_client(cg_app_t *app)
{
    HWND h;
    RECT cr, wr;
    int  fw, fh;

    if (!app || !app->hwnd) return;
    h = (HWND)app->hwnd;
    GetClientRect(h, &cr);
    GetWindowRect(h, &wr);
    fw = (wr.right - wr.left) - (cr.right - cr.left);
    fh = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    SetWindowPos(h, NULL, 0, 0,
                 cgw_scale_px(app, app->doc.width) + fw,
                 cgw_scale_px(app, app->doc.height) + fh,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/**
 * @brief 菜单行表变了之后重建窗口菜单栏
 * @param w menu 控件；不是窗口菜单栏时直接返回
 * @return void 无返回值
 * @note 行表是**唯一的真相**，原生菜单句柄只是一次投影 —— 所以"改完重建"就够了，
 *       不需要维护两套状态（这也是整套运行时菜单 API 能做得这么小的原因）；
 *       菜单按钮的弹出菜单不归这里管，它每次点击现场重建
 */
void cg_win_menu_rebuild(cg_widget_t *w)
{
    cg_app_t *app;
    HWND      h;
    HMENU     hm, old;

    if (!w || !w->app || !w->app->hwnd) return;
    app = w->app;
    if (bar_menu_of(app) != w) return;          /* 不是窗口菜单栏：没有需要重建的东西 */
    h = (HWND)app->hwnd;

    hm = cgw_menu_build(w, 1);
    if (!hm) return;

    old = GetMenu(h);
    if (w->nmenu > 0) {
        SetMenu(h, hm);
    } else {
        /* 项被清空了：把菜单栏整个撤掉 —— 与建窗时"没有可点的项就不挂菜单"同一口径。
         * SetMenu 不会替你销毁旧表，所以两条路都得自己 DestroyMenu。 */
        DestroyMenu(hm);
        SetMenu(h, NULL);
    }
    if (old) DestroyMenu(old);
    cgw_menu_fit_client(app);   /* 菜单栏高度可能变了，客户区尺寸要重算 */
    DrawMenuBar(h);
}

/** 工具栏按钮：没有图标，用「文字池 + BTNS_SHOWTEXT」做纯文字按钮 */
/**
 * @brief 工具栏按钮：没有图标，用「文字池 + 显示文字」做纯文字按钮
 * @param h 工具栏原生窗口；先清空再按行表重建按钮
 * @param w toolbar 控件；提供按钮文本并回填按钮 id
 * @return void 无返回值（无按钮或分配失败时只留下空工具栏）
 */
void cgw_build_toolbar(HWND h, cg_widget_t *w)
{
    wchar_t  *pool;
    TBBUTTON *btns;
    int       total, base;
    size_t    off;

    while (SendMessageW(h, TB_BUTTONCOUNT, 0, 0) > 0) {
        SendMessageW(h, TB_DELETEBUTTON, 0, 0);
    }
    SendMessageW(h, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    free(w->tool_ids);
    w->tool_ids = NULL;
    w->ntool = 0;
    if (w->nitems <= 0) return;

    /* 文字池：一条接一条的 UTF-16 字符串，最后再补一个 0 结尾 */
    total = 1;
    for (int i = 0; i < w->nitems; i++) total += (int)strlen(w->items[i] ? w->items[i] : "") + 1;
    pool = (wchar_t *)calloc((size_t)total, sizeof(wchar_t));
    if (!pool) return;
    off = 0;
    for (int i = 0; i < w->nitems; i++) {
        wchar_t *one = cgw_wc_from_utf8(w->items[i] ? w->items[i] : "");
        size_t len = one ? wcslen(one) : 0;
        if (off + len >= (size_t)total) { free(one); break; }
        memcpy(pool + off, one, len * sizeof(wchar_t));
        off += len + 1;
        free(one);
    }
    base = (int)SendMessageW(h, TB_ADDSTRINGW, 0, (LPARAM)pool);
    free(pool);
    if (base < 0) return;

    w->tool_ids = (int *)calloc((size_t)w->nitems, sizeof(int));
    btns = (TBBUTTON *)calloc((size_t)w->nitems, sizeof(TBBUTTON));
    if (!w->tool_ids || !btns) {
        free(btns);
        return;
    }
    for (int i = 0; i < w->nitems; i++) {
        w->tool_ids[i] = cg_alloc_ctrl_id(w->app);   /* 按钮点击走 WM_COMMAND */
        btns[i].iBitmap = I_IMAGENONE;
        btns[i].idCommand = w->tool_ids[i];
        btns[i].fsState = TBSTATE_ENABLED;
        btns[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        btns[i].iString = base + i;
    }
    w->ntool = w->nitems;
    SendMessageW(h, TB_ADDBUTTONSW, (WPARAM)w->nitems, (LPARAM)btns);
    free(btns);
    SendMessageW(h, TB_AUTOSIZE, 0, 0);
}
