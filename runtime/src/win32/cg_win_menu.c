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

    hm = cgw_menu_build(w);
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
 * @return HMENU 菜单句柄（调用方挂载或 DestroyMenu）
 */
HMENU cgw_menu_build(const cg_widget_t *w)
{
    HMENU  root;
    HMENU *subs;

    root = CreatePopupMenu();
    if (!root || w->nmenu <= 0) return root;

    subs = (HMENU *)calloc((size_t)w->nmenu, sizeof(HMENU));
    if (!subs) return root;

    for (int i = 0; i < w->nmenu; i++) {
        const cg_menu_row_t *r = &w->menu[i];
        HMENU   parent = root;
        wchar_t *wt = cgw_wc_from_utf8(r->text);
        DWORD   flags = MF_STRING;

        if (r->parent >= 0 && r->parent < w->nmenu && subs[r->parent]) parent = subs[r->parent];

        if (r->text && strcmp(r->text, "-") == 0) {
            AppendMenuW(parent, MF_SEPARATOR, 0, NULL);
        } else if (r->popup) {
            subs[i] = CreatePopupMenu();
            if (r->checked) flags |= MF_CHECKED;
            AppendMenuW(parent, flags | MF_POPUP, (UINT_PTR)subs[i], wt ? wt : L"");
        } else {
            if (r->checked) flags |= MF_CHECKED;
            AppendMenuW(parent, flags, (UINT_PTR)r->cmd, wt ? wt : L"");
        }
        free(wt);
    }
    free(subs);
    return root;
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
