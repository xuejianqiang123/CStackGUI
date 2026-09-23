/**
 * @file    cg_win_sync.c
 * @brief   控件逻辑状态推送到原生控件，以及树/表格/标签页条目重建
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

#include <richedit.h>       /* terminal 的行级着色（EM_SETCHARFORMAT / CHARFORMAT2W） */

/* ------------------------------------------------------------ 主窗口 */


/* ------------------------------------------------------------ 状态同步 */

/* ------------------------------------------------------------ 终端（terminal）
 * core 把行攒在 items[] 里（**首字节是方向标记**），这里只负责把"还没推过的行"
 * 追加进只读 RichEdit 并按方向着色。整体重灌只在裁剪/清空之后发生
 * （term_pushed > nitems 就说明行数变少过）。
 * 用 RichEdit 而不是 ownerdraw 列表框的理由：选择复制与自动滚动是白捡的，
 * 而"把日志复制出来贴给别人"恰恰是这类控件最常被做的事。
 */
#define CG_TERM_COL_RX  RGB(0x1B, 0x6E, 0x33)   /* 收到：深绿 */
#define CG_TERM_COL_TX  RGB(0x14, 0x5C, 0xA8)   /* 发出：深蓝 */
#define CG_TERM_COL_SYS RGB(0x6B, 0x6B, 0x6B)   /* 自己记的：灰 */

/** @brief 把一行文字（带颜色）追加到 RichEdit 末尾 */
static void term_push_line(HWND h, const char *text, COLORREF col)
{
    wchar_t     *wt = cgw_wc_from_utf8(text ? text : "");
    CHARFORMAT2W cf;
    size_t       n;

    if (!wt) return;
    n = wcslen(wt);
    {
        int start = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);

        /* 1) 先把这一行插到末尾。
         *   注意 EM_SETSEL(-1,-1) 在 RichEdit 里是**全选**（不是"移到末尾"）——
         *   拿它定位插入点会把已有内容连同颜色一起重刷，所以这里按长度定位。 */
        SendMessageW(h, EM_SETSEL, (WPARAM)start, (LPARAM)start);
        {
            wchar_t *buf = (wchar_t *)malloc((n + 3) * sizeof(wchar_t));
            if (buf) {
                memcpy(buf, wt, n * sizeof(wchar_t));
                buf[n] = L'\r';             /* RichEdit 的换行约定 */
                buf[n + 1] = L'\n';
                buf[n + 2] = 0;
                SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)buf);
                free(buf);
            }
        }

        /* 2) 再选中"刚插进去的这段真实文字"上色。
         *   不靠"空选区设插入点格式"那条路：RICHEDIT50W 上它不生效（设了也还是黑字），
         *   给一段真选区上色才是稳的。 */
        {
            int end = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
            memset(&cf, 0, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR;
            cf.dwEffects = 0;
            cf.crTextColor = col;
            SendMessageW(h, EM_SETSEL, (WPARAM)start, (LPARAM)end);
            SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

            /* 3) 上完色**把选区收掉**：这里靠的是"真选区"上色，不收的话最后一行会一直
             *   显示成选中状态（观感上像"总有半行被高亮"），而且新数据一来就把用户正拖着的
             *   选区顶掉。收成末尾插入点同时也保住了自动滚动（光标在末尾）。 */
            SendMessageW(h, EM_SETSEL, (WPARAM)end, (LPARAM)end);
        }
    }
    free(wt);
}

/** @brief terminal 的行 -> 原生控件（增量追加；裁剪或清空后整体重灌） */
static void sync_terminal(HWND h, cg_widget_t *w)
{
    int i;

    if (w->style.bg != CG_COLOR_NONE) {
        SendMessageW(h, EM_SETBKGNDCOLOR, 0, (LPARAM)cgw_to_colorref(w->style.bg));
    }
    if (w->items_dirty || w->term_pushed > w->nitems || w->term_pushed < 0) {
        SendMessageW(h, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(h, L"");
        w->term_pushed = 0;
        SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    }
    if (w->term_pushed >= w->nitems) {
        SendMessageW(h, EM_SCROLLCARET, 0, 0);
        return;
    }

    /* 只读控件不让 EM_REPLACESEL 写进去，临时放开，写完立刻收回 */
    SendMessageW(h, EM_SETREADONLY, FALSE, 0);
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    for (i = w->term_pushed; i < w->nitems; i++) {
        const char *line = w->items[i] ? w->items[i] : "";
        unsigned char tag = (unsigned char)line[0];
        COLORREF col = CG_TERM_COL_SYS;

        if (tag == CG_TERM_RX)       { col = CG_TERM_COL_RX;  line++; }
        else if (tag == CG_TERM_TX)  { col = CG_TERM_COL_TX;  line++; }
        else if (tag == CG_TERM_SYS) {                        line++; }
        /* 首字节不是标记（用户手写的 .cgui 初始内容）就按 SYS 原样显示 */
        term_push_line(h, line, col);
    }
    w->term_pushed = w->nitems;
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    SendMessageW(h, EM_SETREADONLY, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);      /* 自动滚到底 */
}

/** 标签页当前显示第几页时，某个子控件该不该出现 */
static int page_visible(const cg_widget_t *w)
{
    const cg_widget_t *p = w->parent;
    if (!p || !cgw_is_t(p, "tabs")) return 1;
    return w->page == p->value;
}

/** 按 page / visible 规则应用单个控件的显隐（sync_widget_body 与切页共用） */
static void apply_visible(cg_widget_t *w)
{
    HWND h = (HWND)w->native;
    if (!h) return;
    if (w->parent && cgw_is_t(w->parent, "tabs") && !page_visible(w)) {
        ShowWindow(h, SW_HIDE);
        if (w->native2) ShowWindow((HWND)w->native2, SW_HIDE);
    } else {
        ShowWindow(h, w->visible ? SW_SHOWNA : SW_HIDE);
        if (w->native2) ShowWindow((HWND)w->native2, w->visible ? SW_SHOWNA : SW_HIDE);
    }
}

/** 切页后把 tabs 的所有后代按新的 value 重刷显隐。
 *  只 sync tabs 本身是不够的——page 显隐是每个子控件各自的事，
 *  嵌套 tabs 的内层页也按各自的 value 决定，逐个套规则即可。 */
void cgw_sync_tabs_pages(cg_widget_t *tabs)
{
    cg_app_t *app = tabs->app;
    if (!app) return;
    for (cg_widget_t *x = app->widgets; x; x = x->next) {
        cg_widget_t *p;
        int under = 0;
        for (p = x->parent; p; p = p->parent) {
            if (p == tabs) { under = 1; break; }
        }
        if (!under) continue;
        apply_visible(x);
    }
}

/** 重建树：逻辑下标 -> HTREEITEM 的映射存在 w->item_map 里 */
static void build_tree(HWND h, cg_widget_t *w)
{
    if (w->item_map) { free(w->item_map); w->item_map = NULL; }
    w->item_map_n = 0;
    if (w->ntree <= 0) return;

    w->item_map = (void **)calloc((size_t)w->ntree, sizeof(void *));
    if (!w->item_map) return;
    w->item_map_n = w->ntree;

    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (int i = 0; i < w->ntree; i++) {
        TVINSERTSTRUCTW ins;
        wchar_t *wt = cgw_wc_from_utf8(w->tree[i].text);
        HTREEITEM parent = TVI_ROOT;
        memset(&ins, 0, sizeof(ins));
        if (w->tree[i].parent >= 0 && w->tree[i].parent < i) {
            parent = (HTREEITEM)w->item_map[w->tree[i].parent];
        }
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT;
        ins.item.pszText = wt ? wt : L"";
        w->item_map[i] = (void *)SendMessageW(h, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        free(wt);
    }
    /* 展开必须等所有节点都插完再做：对一个"还没有子节点"的节点调 TVM_EXPAND 是空操作，
     * 所以原来在插入循环里顺手展开，等于 .cgui 里的 "expanded": true 永远不生效。 */
    for (int i = 0; i < w->ntree; i++) {
        if (w->tree[i].expanded && w->item_map[i]) {
            SendMessageW(h, TVM_EXPAND, TVE_EXPAND, (LPARAM)w->item_map[i]);
        }
    }
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    if (w->tree_sel >= 0 && w->tree_sel < w->item_map_n) {
        SendMessageW(h, TVM_SELECTITEM, TVGN_CARET, (LPARAM)w->item_map[w->tree_sel]);
    }
    InvalidateRect(h, NULL, TRUE);
}

/** @brief 表格行图标：把图片缩到 imagelist 的格子尺寸再放进去，返回下标，失败 -1 */
/**
 * @brief 把图片缩到 imagelist 格子尺寸后加进去（表格行图标用）
 * @param w 所属控件（解析图片相对路径用）；il 目标列表；path 图片路径；cell 格子边长
 * @return int 图片下标；失败返回 -1
 */
int cgw_imagelist_add_file(cg_widget_t *w, HIMAGELIST il, const char *path, int cell)
{
    HBITMAP src = cgw_load_image_file(w->app, path);
    HBITMAP fitted = NULL;
    int idx = -1;
    HDC sdc, ddc, sdc0;
    HGDIOBJ olds, oldd;

    if (!src || !il || cell < 4) {
        if (src) DeleteObject(src);
        return -1;
    }
    sdc0 = GetDC(NULL);
    fitted = CreateCompatibleBitmap(sdc0, cell, cell);
    ReleaseDC(NULL, sdc0);
    sdc = CreateCompatibleDC(NULL);
    ddc = CreateCompatibleDC(NULL);
    olds = SelectObject(sdc, src);
    oldd = SelectObject(ddc, fitted);
    SetStretchBltMode(ddc, HALFTONE);
    SetBrushOrgEx(ddc, 0, 0, NULL);
    {
        BITMAP bm;
        int sw = 16, sh = 16;
        if (GetObjectW(src, sizeof(bm), &bm)) { sw = bm.bmWidth; sh = bm.bmHeight; }
        StretchBlt(ddc, 0, 0, cell, cell, sdc, 0, 0, sw, sh, SRCCOPY);
    }
    SelectObject(sdc, olds);
    SelectObject(ddc, oldd);
    DeleteDC(sdc);
    DeleteDC(ddc);
    idx = ImageList_Add(il, fitted, NULL);
    DeleteObject(fitted);
    DeleteObject(src);
    return idx;
}

/** 惰性建表格的 imagelist（行图标用），建好就挂到 LVSIL_SMALL 上 */
HIMAGELIST cgw_table_imagelist(HWND h, cg_widget_t *w)
{
    int cell = cgw_scale_px(w->app, 16);
    if (cell < 12) cell = 12;
    if (!w->imglist) {
        w->imglist = ImageList_Create(cell, cell, ILC_COLOR32, 4, 4);
        SendMessageW(h, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)w->imglist);
    }
    return (HIMAGELIST)w->imglist;
}

/** 重建表格：列 + 行 */
static void build_table(HWND h, cg_widget_t *w)
{
    RECT rc;
    int  total, auto_w, fixed = 0, autos = 0;

    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    while (SendMessageW(h, LVM_DELETECOLUMN, 0, 0)) { /* 清空所有列 */ }
    SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
    SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
    if (w->check) {
        SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
    }

    GetClientRect(h, &rc);
    total = rc.right - rc.left;
    for (int i = 0; i < w->ncols; i++) {
        if (w->cols[i].width > 0) fixed += w->cols[i].width;
        else autos++;
    }
    auto_w = (autos > 0 && total > fixed) ? (total - fixed) / autos : 90;

    for (int i = 0; i < w->ncols; i++) {
        LVCOLUMNW col;
        wchar_t *wt = cgw_wc_from_utf8(w->cols[i].title);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = wt ? wt : L"";
        col.cx = (w->cols[i].width > 0) ? w->cols[i].width : auto_w;
        col.iSubItem = i;
        SendMessageW(h, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        free(wt);
    }

    for (int r = 0; r < w->nrows; r++) {
        LVITEMW it;
        char *first = (w->rows[r] && w->row_cells > 0) ? w->rows[r][0] : NULL;
        wchar_t *wfirst = cgw_wc_from_utf8(first ? first : "");
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = r;
        it.iSubItem = 0;
        it.pszText = wfirst ? wfirst : L"";
        it.lParam = (w->row_val && r < w->row_cap) ? w->row_val[r] : 0;
        SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)&it);
        free(wfirst);

        for (int c = 1; c < w->ncols && c < w->row_cells; c++) {
            LVITEMW sub;
            wchar_t *wt = cgw_wc_from_utf8(w->rows[r][c] ? w->rows[r][c] : "");
            memset(&sub, 0, sizeof(sub));
            sub.iItem = r;
            sub.iSubItem = c;
            sub.pszText = wt ? wt : L"";
            SendMessageW(h, LVM_SETITEMTEXTW, (WPARAM)r, (LPARAM)&sub);
            free(wt);
        }
    }

    /* 行图标：行表刚重建，图片下标要重新放一遍 */
    for (int r = 0; r < w->nrows && r < w->row_icon_n; r++) {
        if (!w->row_icon[r]) continue;
        {
            HIMAGELIST il = cgw_table_imagelist(h, w);
            int idx = cgw_imagelist_add_file(w, il, w->row_icon[r], cgw_scale_px(w->app, 16));
            if (idx >= 0) {
                LVITEMW it;
                memset(&it, 0, sizeof(it));
                it.mask = LVIF_IMAGE;
                it.iItem = r;
                it.iImage = idx;
                SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
            }
        }
    }

    /* 勾选状态：重建后按记录恢复 */
    if (w->check) {
        for (int r = 0; r < w->nrows && r < w->row_cap; r++) {
            if (!w->row_checked || !w->row_checked[r]) continue;
            {
                LVITEMW it;
                memset(&it, 0, sizeof(it));
                it.mask = LVIF_STATE;
                it.state = INDEXTOSTATEIMAGEMASK(2);
                it.stateMask = LVIS_STATEIMAGEMASK;
                SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)r, (LPARAM)&it);
            }
        }
    }

    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);
}

/** 重建标签页标签 + 按当前页决定子控件显隐 */
static void build_tabs(HWND h, cg_widget_t *w)
{
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, TCM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < w->nitems; i++) {
        TCITEMW tci;
        wchar_t *wt = cgw_wc_from_utf8(w->items[i]);
        memset(&tci, 0, sizeof(tci));
        tci.mask = TCIF_TEXT;
        tci.pszText = wt ? wt : L"";
        SendMessageW(h, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&tci);
        free(wt);
    }
    if (w->value >= w->nitems) w->value = 0;
    SendMessageW(h, TCM_SETCURSEL, (WPARAM)w->value, 0);
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);

    for (size_t i = 0; i < w->nchildren; i++) {
        cgw_sync_widget(w->children[i]);
    }
}

/** 状态栏：按段数均分，最后一段吃掉余量 */
static void build_status(HWND h, cg_widget_t *w)
{
    RECT rc;
    int  parts[32];
    int  n = w->nitems > 0 ? w->nitems : 1;
    int  total;

    if (n > 32) n = 32;
    GetClientRect(h, &rc);
    total = rc.right - rc.left;
    for (int i = 0; i < n; i++) {
        parts[i] = (i == n - 1) ? -1 : total * (i + 1) / n;
    }
    SendMessageW(h, SB_SETPARTS, (WPARAM)n, (LPARAM)parts);
    for (int i = 0; i < n; i++) {
        const char *text = (i < w->nitems) ? w->items[i] : "";
        wchar_t *wt = cgw_wc_from_utf8(text ? text : "");
        SendMessageW(h, SB_SETTEXTW, (WPARAM)i, (LPARAM)(wt ? wt : L""));
        free(wt);
    }
}

/** 图片 / 图标按钮 / 按钮（背景图）：按需加载，换图时释放旧位图 */
static void sync_bitmap(cg_widget_t *w)
{
    const char *want = w->src ? w->src : "";
    const char *have = w->bitmap_src ? w->bitmap_src : "";

    if (strcmp(want, have) != 0) {
        if (w->bitmap) {
            DeleteObject((HGDIOBJ)w->bitmap);
            w->bitmap = NULL;
        }
        free(w->bitmap_src);
        w->bitmap_src = w->src ? _strdup(w->src) : NULL;
        w->image_failed = 0;
        if (w->src && *w->src) {
            w->bitmap = cgw_load_image_file(w->app, w->src);
            if (!w->bitmap) {
                w->image_failed = 1;
                fprintf(stderr, "[cstackgui] 读不到图片: %s（显示为占位框）\n", w->src);
            }
        }
        if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
    }
}

/** browser 只在创建时认一次地址，换地址就整个重建（省掉一套 COM 调用） */
static void browser_recreate(cg_widget_t *w)
{
    cg_app_t *app = w->app;
    wchar_t *wurl;
    HWND h;

    if (w->native) {
        DestroyWindow((HWND)w->native);
        w->native = NULL;
    }
    if (!g_win_atl_ok || !app) return;

    wurl = cgw_wc_from_utf8(w->src && *w->src ? w->src : "about:blank");
    h = CreateWindowExW(0, L"AtlAxWin", wurl ? wurl : L"about:blank",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        cgw_scale_px(app, w->x), cgw_scale_px(app, w->y),
                        cgw_scale_px(app, w->w), cgw_scale_px(app, w->h),
                        cgw_parent_hwnd_of(app, w), (HMENU)(INT_PTR)w->ctrl_id,
                        GetModuleHandleW(NULL), NULL);
    free(wurl);
    w->native = h;
}

/** @brief 原生控件当前文本是否已经是 wt
 *  @return int 1 = 一致（调用方就别再 SetWindowText 了）
 *  @details EDIT 收到 WM_SETTEXT 会把选区/光标清回 0，所以"内容没变还写一遍"这种
 *           冗余同步必须避免：`EN_CHANGE` 里 pull 文本再 sync 就会踩到它。
 */
int cgw_hwnd_text_is(HWND h, const wchar_t *wt)
{
    int n;
    wchar_t *cur;
    int same;

    if (!wt) return 1;
    n = GetWindowTextLengthW(h);
    if (n != (int)wcslen(wt)) return 0;
    cur = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!cur) return 0;
    GetWindowTextW(h, cur, n + 1);
    same = (wcscmp(cur, wt) == 0);
    free(cur);
    return same;
}

/** @brief 把控件逻辑状态同步到原生控件（位置/文字/字体/条目/值/显隐），不做防重入 */
static void sync_widget_body(cg_widget_t *w)
{
    HWND h = (HWND)w->native;
    cg_app_t *app = w->app;

    if (!h || !app) return;

    if (w->is_window) {
        if (w->text) {
            wchar_t *wt = cgw_wc_from_utf8(w->text);
            if (wt) { SetWindowTextW(h, wt); free(wt); }
        }
        return;
    }

    /* browser：src 变了就整个重建；位置也由重建时铺好 */
    if (cgw_is_t(w, "browser")) {
        const char *want = w->src ? w->src : "";
        if (strcmp(want, w->bitmap_src ? w->bitmap_src : "") != 0) {
            free(w->bitmap_src);
            w->bitmap_src = _strdup(want);
            browser_recreate(w);
            return;
        }
    }

    /* 位置尺寸（数值框不用在这里让位给箭头：箭头由 cg_skin_edit_fit_spin 摆在
     * 客户区内侧，是浮在输入框上面的独立窗口，输入框本身仍是整块尺寸） */
    if (cgw_is_t(w, "datepicker") && cgw_is_datetime(w) && w->native2) {
        /* 日期在左半边，时间在右半边 */
        int x = cgw_scale_px(app, w->x), y = cgw_scale_px(app, w->y);
        int width = cgw_scale_px(app, w->w), height = cgw_scale_px(app, w->h);
        int half = width / 2;
        SetWindowPos(h, NULL, x, y, half - 2, height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos((HWND)w->native2, NULL, x + half + 2, y, width - half - 2, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(h, NULL, cgw_scale_px(app, w->x), cgw_scale_px(app, w->y),
                     cgw_scale_px(app, w->w), cgw_scale_px(app, w->h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* 文本（switch / tabs / tree / table / statusbar / image / toolbar / datepicker / monthcal 自己管内容） */
    if (!strcmp(w->type, "label") || !strcmp(w->type, "button") ||
        !strcmp(w->type, "menubutton") || !strcmp(w->type, "link") ||
        !strcmp(w->type, "checkbox") || !strcmp(w->type, "radio") ||
        !strcmp(w->type, "group") || !strcmp(w->type, "box") ||
        cgw_is_editlike(w)) {
        /* 数值框（edit + 调节器）：显示文本 = value 的十进制。value 是真相，
         * .cgui 的 text 只是初始快照（程序要改就调 cg_widget_set_value）。 */
        if (cg_widget_is_numedit(w)) {
            wchar_t wbuf[32];
            swprintf(wbuf, 32, L"%d", w->value);
            if (!cgw_hwnd_text_is(h, wbuf)) SetWindowTextW(h, wbuf);
        } else {
            /* 多行输入框：逻辑换行 \n 要先展开成 EDIT 认的 \r\n，否则整段挤成一行 */
            wchar_t *wt = cgw_wc_from_utf8_crlf(w->text);
            if (wt) {
                /* 内容已经一样就不要再 SetWindowText —— EDIT 收到 WM_SETTEXT 会把选区/光标
                 * 清回 0。而 EN_CHANGE 里会 pull 一次输入框文本再 sync（见 cgw_pull_edit_text），
                 * 于是每敲一个字光标都回 0、下一个字插到最前面（输入 1234 得到 4321）。 */
                if (!cgw_hwnd_text_is(h, wt)) SetWindowTextW(h, wt);
                free(wt);
            }
        }
    }

    /* 字体（链接默认带下划线，像链接该有的样子）。同样的字体也别重设：
     * WM_SETFONT 会让控件重排（输入框子类里还会重设边距），同样会动到光标。 */
    {
        int underline = cgw_is_t(w, "link") ? 1 : 0;
        /* 终端默认等宽：HEX 与时间戳列对不齐就没法看（.cgui 里设了 style.font 就用它） */
        const char *fam = (!w->style.font && cgw_is_t(w, "terminal")) ? "Consolas" : w->style.font;
        HFONT f = cgw_font_for(app, fam, w->style.size, w->style.bold, underline);
        HFONT use = f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if ((HFONT)SendMessageW(h, WM_GETFONT, 0, 0) != use) {
            SendMessageW(h, WM_SETFONT, (WPARAM)use, TRUE);
        }
    }

    /* 条目内容 */
    if (w->items_dirty) {
        if (!strcmp(w->type, "listbox")) {
            SendMessageW(h, WM_SETREDRAW, FALSE, 0);
            SendMessageW(h, LB_RESETCONTENT, 0, 0);
            for (int i = 0; i < w->nitems; i++) {
                wchar_t *wt = cgw_wc_from_utf8(w->items[i]);
                if (wt) {
                    SendMessageW(h, LB_ADDSTRING, 0, (LPARAM)wt);
                    free(wt);
                }
            }
            SendMessageW(h, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(h, NULL, TRUE);
            if (w->scroll_bottom && w->nitems > 0) {
                SendMessageW(h, LB_SETTOPINDEX, (WPARAM)(w->nitems - 1), 0);
            }
        } else if (!strcmp(w->type, "combo")) {
            SendMessageW(h, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i < w->nitems; i++) {
                wchar_t *wt = cgw_wc_from_utf8(w->items[i]);
                if (wt) {
                    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)wt);
                    free(wt);
                }
            }
            /* 选中项跟 .cgui 的 value 走（越界退回第 0 项）：每次都跳回第一项
             * 会让"程序设了初始选择"失效。 */
            SendMessageW(h, CB_SETCURSEL,
                         (WPARAM)((w->value > 0 && w->value < w->nitems) ? w->value : 0), 0);
        } else if (!strcmp(w->type, "tabs")) {
            build_tabs(h, w);
        } else if (!strcmp(w->type, "tree")) {
            build_tree(h, w);
        } else if (!strcmp(w->type, "table")) {
            build_table(h, w);
        } else if (!strcmp(w->type, "statusbar")) {
            build_status(h, w);
        } else if (!strcmp(w->type, "toolbar")) {
            cgw_build_toolbar(h, w);
        }
        w->items_dirty = 0;
        w->scroll_bottom = 0;
    }

    /* 终端：按行**增量追加**，所以不能挂在上面那个 if (items_dirty) 里 ——
     * 它每次只是多几行（items_dirty 恒为 0），整体重灌只发生在裁剪/清空之后
     * （sync_terminal 里靠 term_pushed > nitems 判断）。 */
    if (!strcmp(w->type, "terminal")) sync_terminal(h, w);

    /* 值 */
    if (!strcmp(w->type, "progress")) {
        /* 自绘类直接读 w->value 画，只需触发重绘 */
        InvalidateRect(h, NULL, TRUE);
    } else if (!strcmp(w->type, "slider")) {
        int page = (w->vmax - w->vmin) / 10;
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(w->vmin, w->vmax));
        /* 在通道上点一下 = 走一个 page step。**必须显式设**：不设它 page step 就是 0，
         * 表现为"点轨道毫无反应、只能拖拇指"（lesson03 真机点击测出来的）。
         * 取量程的 1/10，最少 1（量程是 0~1 时也能动一格）。 */
        if (page < 1) page = 1;
        SendMessageW(h, TBM_SETPAGESIZE, 0, page);
        SendMessageW(h, TBM_SETPOS, TRUE, w->value);
    } else if (!strcmp(w->type, "checkbox") || !strcmp(w->type, "radio")) {
        /* 自绘勾选/单选按 w->checked 画（cg_skin_draw_check），这里只需触发重绘；
         * BM_SETCHECK 对 BS_OWNERDRAW 按钮没有视觉效果，是无效调用。 */
        InvalidateRect(h, NULL, TRUE);
    } else if (cg_widget_is_numedit(w)) {
        /* 数值框：把范围与当前位置推给上下箭头（文本已由上面那段按 value 写好） */
        HWND up = (HWND)w->native2;
        if (up) {
            SendMessageW(up, UDM_SETRANGE32, (WPARAM)w->vmin, (LPARAM)w->vmax);
            SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)w->value);
        }
    } else if (!strcmp(w->type, "image") || !strcmp(w->type, "iconbutton") ||
               !strcmp(w->type, "button")) {
        sync_bitmap(w);
    } else if (!strcmp(w->type, "monthcal")) {
        /* text 写日期就定位到那天，没写就用今天（.cgui 里通常留空） */
        SYSTEMTIME st;
        SYSTEMTIME dummy;
        if (w->text && *w->text) {
            cgw_datetime_from_text(w, &st, &dummy);
            SendMessageW(h, MCM_SETCURSEL, 0, (LPARAM)&st);
        }
    } else if (!strcmp(w->type, "datepicker")) {
        SYSTEMTIME d, t;
        cgw_datetime_from_text(w, &d, &t);
        /* 显示格式跟 .cgui 里 text 的写法对齐，免得控件显示成系统区域格式 */
        SendMessageW(h, DTM_SETFORMATW, 0, (LPARAM)(cgw_is_time_only(w) ? L"HH':'mm" : L"yyyy-MM-dd"));
        SendMessageW(h, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&d);
        if (w->native2) {
            SendMessageW((HWND)w->native2, DTM_SETFORMATW, 0, (LPARAM)L"HH':'mm");
            SendMessageW((HWND)w->native2, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&t);
        }
    } else if (!strcmp(w->type, "tabs")) {
        SendMessageW(h, TCM_SETCURSEL, (WPARAM)w->value, 0);
    } else if (!strcmp(w->type, "listbox") && !w->items_dirty) {
        SendMessageW(h, LB_SETCURSEL, (WPARAM)w->value, 0);
    } else if (!strcmp(w->type, "combo") && !w->items_dirty) {
        /* 代码里 cg_widget_set_value(combo, i) 也要能改当前选中项 */
        SendMessageW(h, CB_SETCURSEL,
                     (WPARAM)((w->value > 0 && w->value < w->nitems) ? w->value : 0), 0);
    } else if (!strcmp(w->type, "table") && w->table_sel >= 0) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_STATE;
        it.state = LVIS_SELECTED | LVIS_FOCUSED;
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)w->table_sel, (LPARAM)&it);
    }

    EnableWindow(h, w->enabled ? TRUE : FALSE);

    /* 第二个原生窗口（数值框的调节器 UPDOWN、datetime 的第二个 DTP）也要一起禁用。
     * 只禁主控件的话：enabled:false 的数值框仍能点箭头改值 —— 而那条路径（UDN_DELTAPOS）
     * 是直接改 value、不走 cg_dispatch，所以连回调都不会发（2026-09-23 排查发现）。 */
    if (w->native2) EnableWindow(w->native2, w->enabled ? TRUE : FALSE);

    /* 标签页的子控件要按当前页显隐（数值框 / datetime 的 native2 同进同退） */
    apply_visible(w);
    InvalidateRect(h, NULL, TRUE);
}

/** 外面统一包一层防重入：同步会触发原生通知（EN_CHANGE 等），
 *  不能让它们再跑回回调里，否则「回调改控件」会自己套自己。 */
void cgw_sync_widget(cg_widget_t *w)
{
    int saved = g_win_in_notify;
    g_win_in_notify = 1;
    sync_widget_body(w);
    g_win_in_notify = saved;
}
