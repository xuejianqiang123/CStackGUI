/**
 * @file    cg_win_menu.c
 * @brief   菜单构建与弹出、工具栏按钮填充
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/** 菜单按钮：点击时在按钮下方弹出菜单，选中项直接派发（TPM_RETURNCMD 省掉消息路由） */
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
        int          enabled = 1;
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
 * @brief 动作的启用 / 勾选变了：同步到**菜单项**（控件那一半走 cg_widget_set_enabled）
 * @details 只处理窗口菜单栏那一份：menubutton 的弹出菜单是**每次点击时现场重建**的
 *          （cgw_popup_menu_for），建的时候自然会读到最新状态，不需要同步。
 *          不必判断某个 cmd 到底挂在哪个菜单里 —— EnableMenuItem 对"这个 HMENU 里
 *          没有的命令 id"返回 -1，是空操作，比先查一遍更省事也更不容易漏。
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

/** 工具栏按钮：没有图标，用「文字池 + BTNS_SHOWTEXT」做纯文字按钮 */
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
