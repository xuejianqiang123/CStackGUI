/**
 * @file    cg_win_notify.c
 * @brief   原生通知映射：WM_COMMAND / WM_NOTIFY 归一成 CStackGUI 事件
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 事件映射 */


/** 把编辑框里的当前文字读回控件模型 */
void cgw_pull_edit_text(cg_widget_t *w, HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(hwnd, buf, len + 1);
    /* EDIT 里的换行是 \r\n，模型和 .cgui 统一用 \n：去掉 \r 再入模型，
     * 否则存回去的文字会带上 \r（画布上看不出来，但字符串比较/写文件都会多一个字节）。 */
    {
        wchar_t *d = buf;
        const wchar_t *s;
        for (s = buf; *s; s++) {
            if (*s != L'\r') *d++ = *s;
        }
        *d = 0;
    }
    {
        int need = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
        char *u8 = (char *)malloc((size_t)(need > 0 ? need : 1));
        if (u8) {
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, u8, need, NULL, NULL);
            cg_widget_set_text(w, u8);
            free(u8);
        }
    }
    free(buf);
}

/** 真正的映射：原生通知 -> CStackGUI 事件。cmd_id 是 WM_COMMAND 的 LOWORD。 */
void cgw_notify_body(cg_app_t *app, cg_widget_t *w, HWND hwnd, int code, int cmd_id)
{
    /* 菜单按钮：不派发 click，改成弹菜单（菜单项被选中时再派发 click） */
    if (!strcmp(w->type, "menubutton")) {
        if (code == BN_CLICKED) cgw_popup_menu_for(app, w, hwnd);
        return;
    }
    /* 颜色选择：点色块弹系统选色对话框，选中后写回 value（0xRRGGBB）并报 change。
     * 注意 value 存的是颜色，不能走 cg_widget_set_value —— 那里会按 min/max 截断。 */
    if (!strcmp(w->type, "colorpicker")) {
        if (code != BN_CLICKED) return;
        {
            static COLORREF custom[16];
            CHOOSECOLORW cc;
            unsigned cur = (w->value > 0) ? (unsigned)w->value : 0x6750A4;
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = app->hwnd ? (HWND)app->hwnd : NULL;
            cc.rgbResult = cgw_to_colorref(cur);
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (!ChooseColorW(&cc)) return;
            w->value = (int)(((unsigned)GetRValue(cc.rgbResult) << 16) |
                             ((unsigned)GetGValue(cc.rgbResult) << 8) |
                             (unsigned)GetBValue(cc.rgbResult));
            if (w->node) w->node->value = w->value;
            if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
            cg_dispatch(w, CG_EVENT_CHANGE);
        }
        return;
    }
    /* tag：能点的圆角小片，当按钮用 */
    if (!strcmp(w->type, "tag")) {
        if (code == BN_CLICKED) cg_dispatch(w, CG_EVENT_CLICK);
        return;
    }
    /* rate / pagination 的点击在 cgw_FocusProc 的 WM_LBUTTONUP 里处理
     * （要知道 x 坐标才能判断点的是哪颗星 / 左右翻页），这里忽略 */
    if (!strcmp(w->type, "rate") || !strcmp(w->type, "pagination")) {
        return;
    }
    /* 工具栏：按钮点击走 WM_COMMAND，命令 id 反查是第几个按钮 */
    if (!strcmp(w->type, "toolbar")) {
        if (code == BN_CLICKED && cmd_id) {
            for (int i = 0; i < w->ntool; i++) {
                if (w->tool_ids[i] != cmd_id) continue;
                w->value = i;
                if (w->node) w->node->value = i;
                cg_dispatch(w, CG_EVENT_CLICK);
                break;
            }
        }
        return;
    }
    if (!strcmp(w->type, "switch")) {
        /* switch 是 BS_OWNERDRAW 按钮：点击时 BUTTON 不会 toggle check 状态
         * （BM_SETCHECK/BM_GETCHECK 只对 checkbox/radio 有定义），读 BM_GETCHECK
         * 只会拿到恒定旧值，表现为"关掉后就再也打不开"。所以自己取反。 */
        cg_widget_set_checked(w, !w->checked);
        cg_dispatch(w, CG_EVENT_CHANGE);
        return;
    }
    if (!strcmp(w->type, "listbox")) {
        if (code == LBN_SELCHANGE) {
            LRESULT sel = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                cg_widget_set_value(w, (int)sel);
                cg_dispatch(w, CG_EVENT_SELECT);
            }
        } else if (code == LBN_DBLCLK) {
            cg_dispatch(w, CG_EVENT_DBLCLICK);
        }
        return;
    }
    if (!strcmp(w->type, "combo")) {
        /* 下拉框（CBS_DROPDOWNLIST）的选择变化走 WM_COMMAND 的 CBN_SELCHANGE。
         * value 直接写、不走 cg_widget_set_value：那个会按 min/max 夹到区间里，
         * 而这里存的是条目下标，条目数可以超过 100（夹了就永远选不中后面那些）。 */
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                w->value = (int)sel;
                if (w->node) w->node->value = (int)sel;
                cg_dispatch(w, CG_EVENT_SELECT);
            }
        }
        return;
    }
    /* 复选/单选：owner-draw 不会自动 toggle，这里手动翻状态并派发 change。
     * 单选还要把同组（同 parent）其它 radio 取消选中。 */
    if (!strcmp(w->type, "checkbox") || !strcmp(w->type, "radio")) {
        if (code == BN_CLICKED) {
            if (!strcmp(w->type, "radio")) {
                for (cg_widget_t *r = app->widgets; r; r = r->next) {
                    if (r != w && !strcmp(r->type, "radio") && r->parent == w->parent) {
                        cg_widget_set_checked(r, 0);
                        if (r->native) InvalidateRect((HWND)r->native, NULL, TRUE);
                    }
                }
            }
            cg_widget_set_checked(w, !w->checked);
            cg_dispatch(w, CG_EVENT_CHANGE);
            InvalidateRect((HWND)w->native, NULL, TRUE);
        }
        return;
    }
    /* 这三种都是"点一下 = click"，双击由运行时按系统间隔合成（原生控件报的
     * STN_CLICKED / BN_CLICKED 都是 0，走同一条路更一致） */
    if (!strcmp(w->type, "button") || !strcmp(w->type, "iconbutton") ||
        !strcmp(w->type, "link")) {
        if (code == BN_CLICKED || code == STN_CLICKED) cg_dispatch(w, CG_EVENT_CLICK);
        return;
    }
    if (cgw_is_editlike(w)) {
        if (code == EN_CHANGE) {
            if (strcmp(w->type, "spinner") == 0) {
                /* 微调框要让 value 跟上手输的数字。
                 * 顺序是关键：先把文本算成 int 落进 w->value / updown 内部位置，
                 * 最后再 cgw_pull_edit_text —— pull 会经 cg_widget_set_text 触发一次 sync，
                 * sync 按 w->value 重写 buddy 文本；若 value 还是旧值（如 1），
                 * 用户刚敲进去的 13 会被刷回 1（"明明 13 了还显示 1"）。 */
                int  len = GetWindowTextLengthW(hwnd);
                int  v = w->value;
                HWND up = (HWND)w->native2;
                wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
                if (buf) {
                    GetWindowTextW(hwnd, buf, len + 1);
                    v = (int)wcstol(buf, NULL, 10);
                    free(buf);
                }
                if (v < w->vmin) v = w->vmin;
                if (v > w->vmax) v = w->vmax;
                w->value = v;
                if (w->node) w->node->value = v;
                if (up) {
                    BOOL err = FALSE;
                    int cur = (int)SendMessageW(up, UDM_GETPOS32, 0, (LPARAM)&err);
                    if (err || cur != v)
                        SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)v);
                }
                /* value 已就位：pull 触发的 sync 用新 value 比对文本，一致就不重写、
                 * 光标保持在点击处；超界输入会被 clamp 成 vmin/vmax 显示。 */
                cgw_pull_edit_text(w, hwnd);
            } else {
                cgw_pull_edit_text(w, hwnd);
            }
            cg_dispatch(w, CG_EVENT_CHANGE);
        } else if (code == EN_KILLFOCUS) {
            /* 数值/日期输入方式：离开输入框时把文本收干净（越界夹回、非法清空）。
             * 走的是"程序改文本"，所以不派发 change —— 业务要拿最终值就读 cg_widget_text。 */
            cgw_edit_validate(w, hwnd);
        }
        return;
    }
    (void)app;
}

/** WM_NOTIFY：标签页 / 树 / 表格 / 微调 的富通知都从这条路进来 */
LRESULT cgw_notify_common(cg_app_t *app, const NMHDR *hdr)
{
    cg_widget_t *w = cgw_widget_by_hwnd(app, hdr->hwndFrom);

    if (!w) return 0;
    /* 自绘滑块：NM_CUSTOMDRAW 是绘制查询，要在 g_win_in_notify 之前返回，否则会卡住重入 */
    if (hdr->code == NM_CUSTOMDRAW && !strcmp(w->type, "slider"))
        return cg_skin_draw_slider_notify(app, hdr);
    if (g_win_in_notify) return 0;
    g_win_in_notify = 1;

    switch (hdr->code) {
    case TCN_SELCHANGE: {
        LRESULT sel = SendMessageW((HWND)w->native, TCM_GETCURSEL, 0, 0);
        if (sel >= 0) {
            w->value = (int)sel;
            if (w->node) w->node->value = (int)sel;
            cgw_sync_widget(w);          /* tabs 自己（含 TCM_SETCURSEL 同步） */
            cgw_sync_tabs_pages(w);      /* 后代按 page 重刷显隐——只 sync tabs 本身是不够的 */
            cg_dispatch(w, CG_EVENT_SELECT);
        }
        break;
    }
    case TVN_SELCHANGEDW: {
        const NMTREEVIEWW *ntv = (const NMTREEVIEWW *)hdr;
        for (int i = 0; i < w->item_map_n; i++) {
            if ((HTREEITEM)w->item_map[i] == ntv->itemNew.hItem) {
                w->tree_sel = i;
                cg_dispatch(w, CG_EVENT_SELECT);
                break;
            }
        }
        break;
    }
    /* 注：树控件没有 TVN_DBLCLK 这个码，双击统一由 NM_DBLCLK 派发；
     * TVN_FIRST-1 是 TVN_SELCHANGINGA（ANSI 选中前），别在这里误用。 */
    case LVN_ITEMCHANGED: {
        const NMLISTVIEW *nlv = (const NMLISTVIEW *)hdr;
        /* 勾选列：状态位图（高 4 位）变了才是勾选切换，别和选中混在一起 */
        if ((nlv->uChanged & LVIF_STATE) && w->check &&
            (nlv->uOldState & LVIS_STATEIMAGEMASK) != (nlv->uNewState & LVIS_STATEIMAGEMASK)) {
            int on = ((nlv->uNewState >> 12) & 0xF) > 1;
            if (nlv->iItem >= 0 && nlv->iItem < w->row_cap && w->row_checked) {
                w->row_checked[nlv->iItem] = on ? 1 : 0;
            }
            cg_dispatch(w, CG_EVENT_CHECK);
        }
        if ((nlv->uChanged & LVIF_STATE) && (nlv->uNewState & LVIS_SELECTED)) {
            w->table_sel = nlv->iItem;
            cg_dispatch(w, CG_EVENT_SELECT);
        }
        break;
    }
    case NM_DBLCLK:
        cg_dispatch(w, CG_EVENT_DBLCLICK);
        break;
    case TVN_ITEMEXPANDEDW: {               /* TVN_ITEMEXPANDEDW = TVN_FIRST-55（Unicode） */
        const NMTREEVIEWW *ntv = (const NMTREEVIEWW *)hdr;
        for (int i = 0; i < w->item_map_n; i++) {
            if ((HTREEITEM)w->item_map[i] != ntv->itemNew.hItem) continue;
            w->tree_sel = i;                /* 回调里用 cg_tree_selected 能问到是哪个节点 */
            cg_dispatch(w, (ntv->action & TVE_COLLAPSE) ? CG_EVENT_COLLAPSE : CG_EVENT_EXPAND);
            break;
        }
        break;
    }
    case MCN_SELECT: {                      /* 月历：用户点选了某天 */
        SYSTEMTIME st;
        if (SendMessageW((HWND)w->native, MCM_GETCURSEL, 0, (LPARAM)&st)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            cg_widget_set_text(w, buf);
        }
        cg_dispatch(w, CG_EVENT_SELECT);
        break;
    }
    case DTN_DATETIMECHANGE: {
        const NMDATETIMECHANGE *dt = (const NMDATETIMECHANGE *)hdr;
        if (dt->dwFlags == GDT_VALID) {
            cgw_datetime_pull_text(w, (HWND)w->native, (HWND)w->native2);
        }
        cg_dispatch(w, CG_EVENT_CHANGE);
        break;
    }
    case UDN_DELTAPOS: {
        const NMUPDOWN *nud = (const NMUPDOWN *)hdr;
        HWND buddy = (HWND)w->native;

        /* 输入框上的调节器（.cgui 的 spin）：只派发「调节钮被按下」，delta 给 ±1。
         * 自动方式顺手把文本改成夹好的整数 —— 那是程序改文本，按本工程的规矩不派发
         * change，业务在 spin 回调里读 cg_widget_text 就是最终值。 */
        if (!strcmp(w->type, "edit")) {
            int v = nud->iPos + nud->iDelta;
            if (v < w->vmin) v = w->vmin;
            if (v > w->vmax) v = w->vmax;
            w->ev_delta = nud->iDelta;
            w->value = v;
            if (w->native2) SendMessageW((HWND)w->native2, UDM_SETPOS32, 0, (LPARAM)v);
            if (w->spin == 1 && buddy) {          /* 自动：把 ±1 后的值写进文本 */
                wchar_t wbuf[32];
                swprintf(wbuf, 32, L"%d", v);
                if (!cgw_hwnd_text_is(buddy, wbuf)) SetWindowTextW(buddy, wbuf);
                SendMessageW(buddy, EM_SETSEL, (WPARAM)(wcslen(wbuf)), (LPARAM)(wcslen(wbuf)));
            }
            cg_dispatch(w, CG_EVENT_SPIN);
            g_win_in_notify = 0;
            return 1;
        }

        int v = nud->iPos + nud->iDelta;
        if (v < w->vmin) v = w->vmin;
        if (v > w->vmax) v = w->vmax;
        w->value = v;
        if (w->node) w->node->value = v;
        /* 手动把新值写进 buddy 并把光标放到文本末尾，然后返回 1 拦住 updown 自动更新：
         * 若带 UDS_SETBUDDYINT，updown 自己 SetWindowText 会把光标/选区清回 0
         * —— 点完箭头光标总停在前面的元凶。文本自此由我们接管。 */
        {
            wchar_t wbuf[32];
            swprintf(wbuf, 32, L"%d", v);
            if (!cgw_hwnd_text_is(buddy, wbuf)) SetWindowTextW(buddy, wbuf);
            SendMessageW(buddy, EM_SETSEL, (WPARAM)(wcslen(wbuf)), (LPARAM)(wcslen(wbuf)));
        }
        if (w->native2)
            SendMessageW((HWND)w->native2, UDM_SETPOS32, 0, (LPARAM)v);
        cg_dispatch(w, CG_EVENT_CHANGE);
        g_win_in_notify = 0;
        return 1;    /* 已自管内部位置与 buddy 文本，别让 updown 再算一遍 */
    }
    default:
        break;
    }
    g_win_in_notify = 0;
    return 0;
}
