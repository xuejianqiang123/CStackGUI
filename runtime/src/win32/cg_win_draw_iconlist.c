/**
 * @file    cg_win_draw_iconlist.c
 * @brief   自绘窗口类 iconlist：图标列表（复选框 + 图标 + 两行文字 + 右侧状态，可滚动）
 * @details 行数据的唯一真相是 w->il_rows（core 侧 API 见 cg_widget_iconlist.c），本文件只管
 *          三件事：画（双缓冲整帧）、交互（点选 / 勾选 / 悬停 / 滚动 / 键盘导航）、
 *          行图标位图缓存（bmp_src 路径比对，变了才重新加载）。
 *          滚动条用 WS_VSCROLL 原生滚动条，但**内容位移必须自己做** —— DefWindowProc 只管
 *          滚动条本身的 UI，不会替你把列表滚起来（自绘控件无先例，2026-10-12 首建）。
 */
#include "cg_win_internal.h"
#include "cg_skin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 几何（逻辑像素，绘制时按 scale 放大） */

#define IL_PAD      8    /**< 行内容左边距 */
#define IL_RPAD     8    /**< 右边距（状态文字右贴到这里） */
#define IL_CHK_W    26   /**< 复选框列宽（check = 1 时占位） */
#define IL_CHK_BOX  16   /**< 勾选框边长 */
#define IL_ICON     24   /**< 图标边长 */
#define IL_ICON_GAP 10   /**< 图标与文字的间距 */
#define IL_ROW_H    44   /**< 行高（放得下主标题 + 副标题两行） */
#define IL_RADIUS    8   /**< 选中/悬停底的圆角半径 */

/** M3 语义色（0xRRGGBB），深浅两套 —— 品牌紫与皮肤其它控件同值（cg_skin.c） */
#define IL_BRAND      0x6750A4u
#define IL_SEL_LIGHT  0xEADDFFu   /* 选中容器色：浅色 */
#define IL_SEL_DARK   0x4A4458u   /* 选中容器色：深色 */
#define IL_HOT_LIGHT  0xF6F2FAu   /* 悬停：浅色 */
#define IL_HOT_DARK   0x2B2930u   /* 悬停：深色 */
#define IL_TEXT_LIGHT 0x49454Fu   /* 副标题/状态（on-surface-variant）：浅色 */
#define IL_TEXT_DARK  0xCAC4D0u   /* 副标题/状态：深色 */

/* ------------------------------------------------------------ 缩放与几何小工具 */

/**
 * @brief 控件当前的 DPI 缩放百分比（100 = 96dpi）
 * @param w 目标控件
 * @return int 百分比；没拿到 app 时按 100
 */
static int il_sc(const cg_widget_t *w)
{
    return (w && w->app && w->app->scale > 0) ? w->app->scale : 100;
}

/**
 * @brief 逻辑像素 -> 物理像素（绘制坐标一律物理，与 GetClientRect 同一口径）
 * @param w 目标控件（提供 scale）
 * @param v 逻辑像素值
 * @return int 物理像素值
 */
static int il_px(const cg_widget_t *w, int v)
{
    return v * il_sc(w) / 100;
}

/**
 * @brief 行高（物理像素）
 * @param w 目标控件
 * @return int 物理行高
 */
static int il_row_px(const cg_widget_t *w)
{
    int h = il_px(w, IL_ROW_H);
    return h > 0 ? h : IL_ROW_H;
}

/* ------------------------------------------------------------ 行图标位图缓存 */

/**
 * @brief 按需加载一行的图标位图：路径没变就复用，变了先释放再重载
 * @param w iconlist 控件
 * @param row 目标行（下标必须合法）
 * @return void 无返回值；没有图标或加载失败时 row->bmp 为 NULL
 * @note 与 image 控件的 sync_bitmap 同一口径：bmp_src 是"当前位图属于哪个路径"。
 *       失败不刷屏日志（每个坏路径只在第一次报一行）。
 */
static void il_ensure_bmp(cg_widget_t *w, int row)
{
    cg_ilrow_t *r = &w->il_rows[row];
    const char *want = (r->icon && *r->icon) ? r->icon : "";
    const char *have = r->bmp_src ? r->bmp_src : "";

    if (strcmp(want, have) == 0) return;
    if (r->bmp) {
        DeleteObject((HGDIOBJ)r->bmp);
        r->bmp = NULL;
    }
    free(r->bmp_src);
    r->bmp_src = NULL;
    if (*want) {
        r->bmp = cgw_load_image_file(w->app, want);
        if (!r->bmp) fprintf(stderr, "[cstackgui] iconlist: 图标加载失败 %s\n", want);
    }
}

/**
 * @brief 释放一行的后端位图（core 的 clear/set_row/free 通过 cg_internal.h 调进来）
 * @param row 目标行；NULL 安全
 * @return void 无返回值
 */
void cgw_iconlist_row_free(cg_ilrow_t *row)
{
    if (!row) return;
    if (row->bmp) {
        DeleteObject((HGDIOBJ)row->bmp);
        row->bmp = NULL;
    }
    free(row->bmp_src);
    row->bmp_src = NULL;
}

/**
 * @brief 释放一个 iconlist 控件的全部行图标位图（销毁与关窗两条路径共用）
 * @param w iconlist 控件；非 iconlist 或没有行时是空操作
 * @return void 无返回值
 * @note 行**字符串**归 core 的 cg_widget_free，这里只还 GDI 句柄
 */
void cgw_iconlist_free_bmps(cg_widget_t *w)
{
    int i;
    if (!w || !w->type || strcmp(w->type, "iconlist") != 0) return;
    for (i = 0; i < w->il_n; i++) cgw_iconlist_row_free(&w->il_rows[i]);
}

/* ------------------------------------------------------------ 滚动 */

/**
 * @brief 可视区能放几行
 * @param w iconlist 控件
 * @param rc 客户区矩形（物理像素）
 * @return int 行数（至少 1，避免除零与负页）
 */
static int il_visible_rows(const cg_widget_t *w, const RECT *rc)
{
    int h = (rc->bottom - rc->top) / il_row_px(w);
    return h > 0 ? h : 1;
}

/**
 * @brief 把 il_scroll 夹进合法范围（行数变少 / 窗口变矮后不能停在越界处）
 * @param w iconlist 控件
 * @param rc 客户区矩形
 * @return void 无返回值
 */
static void il_clamp_scroll(cg_widget_t *w, const RECT *rc)
{
    int max = w->il_n - il_visible_rows(w, rc);
    if (max < 0) max = 0;
    if (w->il_scroll > max) w->il_scroll = max;
    if (w->il_scroll < 0) w->il_scroll = 0;
}

/**
 * @brief 把滚动条状态刷成"当前行数 / 可视页 / 首行"（行单位）
 * @param hwnd 控件窗口
 * @param w iconlist 控件
 * @param rc 客户区矩形
 * @return void 无返回值
 * @note 每帧画完调一次：行数、窗口尺寸、滚动位置都在这里归一，省掉三处各同步一遍
 */
static void il_scroll_sync(HWND hwnd, cg_widget_t *w, const RECT *rc)
{
    SCROLLINFO si;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (w->il_n > 0) ? w->il_n - 1 : 0;
    si.nPage  = (UINT)il_visible_rows(w, rc);
    si.nPos   = w->il_scroll;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/**
 * @brief 滚动到让某行可见（键盘导航与程序改选中时用）
 * @param w iconlist 控件
 * @param row 目标行
 * @param rc 客户区矩形
 * @return void 无返回值
 */
static void il_ensure_visible(cg_widget_t *w, int row, const RECT *rc)
{
    int vis = il_visible_rows(w, rc);
    if (row < w->il_scroll) w->il_scroll = row;
    if (row > w->il_scroll + vis - 1) w->il_scroll = row - vis + 1;
    il_clamp_scroll(w, rc);
}

/* ------------------------------------------------------------ 绘制 */

/**
 * @brief 画一个行内复选框（M3 圆角方框；勾选 = 品牌紫填充 + 白勾）
 * @param w iconlist 控件
 * @param dc 目标 DC
 * @param box 方框矩形（物理像素）
 * @param on 勾选态
 * @return void 无返回值
 */
static void il_draw_checkbox(cg_widget_t *w, HDC dc, const RECT *box, int on)
{
    unsigned edge = cg_skin_neutral(w->app, 0x79747Eu, 0x938F99u);
    int      rad  = il_px(w, 4);

    if (on) {
        cg_skin_fill_round_rect(dc, box, rad, IL_BRAND, CG_COLOR_NONE, 0);
        /* 白勾：两段折线（GDI 直线；方形很小，锯齿看不出来） */
        {
            int    bw = box->right - box->left;
            int    bh = box->bottom - box->top;
            HPEN   pen = CreatePen(PS_SOLID, il_px(w, 2), RGB(0xFF, 0xFF, 0xFF));
            HGDIOBJ oldp = SelectObject(dc, pen);
            int     x0 = box->left + bw * 22 / 100;
            int     y0 = box->top + bh * 52 / 100;
            int     x1 = box->left + bw * 43 / 100;
            int     y1 = box->top + bh * 72 / 100;
            int     x2 = box->left + bw * 78 / 100;
            int     y2 = box->top + bh * 30 / 100;
            MoveToEx(dc, x0, y0, NULL);
            LineTo(dc, x1, y1);
            LineTo(dc, x2, y2);
            SelectObject(dc, oldp);
            DeleteObject(pen);
        }
    } else {
        cg_skin_fill_round_rect(dc, box, rad, CG_COLOR_NONE, edge, (float)il_px(w, 1));
    }
}

/**
 * @brief 画一个行图标（等比缩放居中放进方框；没图标/加载失败画占位框）
 * @param w iconlist 控件
 * @param dc 目标 DC
 * @param row 行下标
 * @param box 图标方框（物理像素）
 * @return void 无返回值
 */
static void il_draw_icon(cg_widget_t *w, HDC dc, int row, const RECT *box)
{
    cg_ilrow_t *r = &w->il_rows[row];
    int         bw = box->right - box->left;
    int         bh = box->bottom - box->top;

    il_ensure_bmp(w, row);
    if (r->bmp) {
        BITMAP  bm;
        HDC     mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, (HGDIOBJ)r->bmp);
        if (GetObjectW((HGDIOBJ)r->bmp, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            /* 等比缩放（fit）后居中：非方形图标不拉变形 */
            double sx = (double)bw / bm.bmWidth;
            double sy = (double)bh / bm.bmHeight;
            double s  = sx < sy ? sx : sy;
            int    dw = (int)(bm.bmWidth * s);
            int    dh = (int)(bm.bmHeight * s);
            int    dx = box->left + (bw - dw) / 2;
            int    dy = box->top + (bh - dh) / 2;
            int    prev = SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, dx, dy, dw, dh, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SetStretchBltMode(dc, prev);
        }
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        /* 占位框：虚线圆角小方块，与 image 控件的缺图口径一致（不崩、看得出"这儿该有图"） */
        RECT  inset = *box;
        HBRUSH br   = CreateSolidBrush(cgw_to_colorref(cg_skin_neutral(w->app, 0xE3E3E8u, 0x2A2A2Eu)));
        HPEN   pen  = CreatePen(PS_DASH, 1, RGB(150, 150, 156));
        HGDIOBJ oldb = SelectObject(dc, br);
        HGDIOBJ oldp = SelectObject(dc, pen);
        inset.left++; inset.top++; inset.right--; inset.bottom--;
        RoundRect(dc, inset.left, inset.top, inset.right, inset.bottom,
                  inset.left + il_px(w, 4), inset.top + il_px(w, 4));
        SelectObject(dc, oldp);
        SelectObject(dc, oldb);
        DeleteObject(pen);
        DeleteObject(br);
    }
}

/**
 * @brief 画一行（底 / 焦点框 / 复选框 / 图标 / 主副标题 / 状态 / 分隔线）
 * @param w iconlist 控件
 * @param dc 目标 DC（内存位图）
 * @param rc 客户区（物理，算宽度用）
 * @param i 行下标
 * @param y 行顶（物理）
 * @param focused 控件当前有没有键盘焦点
 * @return void 无返回值
 */
static void il_draw_row(cg_widget_t *w, HDC dc, const RECT *rc, int i, int y, int focused)
{
    cg_ilrow_t *r = &w->il_rows[i];
    int         rh   = il_row_px(w);
    int         sc   = il_sc(w);
    int         x0   = il_px(w, IL_PAD);
    int         x1   = rc->right - il_px(w, IL_RPAD);
    int         textx, iconx;
    int         selected = (w->value == i);
    HFONT       f_title, f_small;
    HGDIOBJ     oldf;
    int         oldmode;
    RECT        cell;
    unsigned    sub_col;
    wchar_t    *ws;
    int         status_w = 0;

    /* 选中 / 悬停底（M3 容器圆角；选中优先于悬停） */
    if (selected || w->il_hover == i) {
        RECT     bg = { x0, y + 1, x1, y + rh - 1 };
        unsigned fill = selected
            ? cg_skin_neutral(w->app, IL_SEL_LIGHT, IL_SEL_DARK)
            : cg_skin_neutral(w->app, IL_HOT_LIGHT, IL_HOT_DARK);
        cg_skin_fill_round_rect(dc, &bg, il_px(w, IL_RADIUS), fill, CG_COLOR_NONE, 0);
    }

    /* 焦点框：选中行 + 键盘焦点才画（与 listbox 的焦点虚线同一语义） */
    if (selected && focused) {
        RECT fb = { x0, y + 2, x1, y + rh - 2 };
        DrawFocusRect(dc, &fb);
    }

    iconx = x0 + (w->check ? il_px(w, IL_CHK_W) : 0);
    textx = iconx + il_px(w, IL_ICON + IL_ICON_GAP);

    /* 复选框列（check = 1 才有，语义同 table 的同名字段） */
    if (w->check) {
        RECT box;
        int  bs = il_px(w, IL_CHK_BOX);
        box.left   = x0 + (il_px(w, IL_CHK_W) - bs) / 2;
        box.right  = box.left + bs;
        box.top    = y + (rh - bs) / 2;
        box.bottom = box.top + bs;
        il_draw_checkbox(w, dc, &box, r->checked);
    }

    /* 图标 */
    {
        RECT box;
        box.left   = iconx;
        box.right  = iconx + il_px(w, IL_ICON);
        box.top    = y + (rh - il_px(w, IL_ICON)) / 2;
        box.bottom = box.top + il_px(w, IL_ICON);
        il_draw_icon(w, dc, i, &box);
    }

    /* 状态文字先量宽：文本区要给它让位（右对齐，DT_CALCRECT 量） */
    if (r->status && *r->status) {
        RECT sr = { 0, 0, 0, 0 };
        f_small = cgw_font_for(w->app, w->style.font,
                               w->style.size > 2 ? w->style.size - 2 : w->style.size, 0, 0);
        oldf = SelectObject(dc, f_small ? f_small : GetStockObject(DEFAULT_GUI_FONT));
        ws = cgw_wc_from_utf8(r->status);
        if (ws) {
            DrawTextW(dc, ws, -1, &sr, DT_SINGLELINE | DT_CALCRECT);
            free(ws);
        }
        status_w = sr.right + il_px(w, 8);
        SelectObject(dc, oldf);
    }

    /* 字体与文字颜色：主标题用本控件字号（没声明 = 雅黑 9pt，由 cgw_font_for 兜），
       副标题/状态小一号、走中性灰（深浅两套）。 */
    f_title = cgw_font_for(w->app, w->style.font, w->style.size, w->style.bold, 0);
    f_small = cgw_font_for(w->app, w->style.font,
                           w->style.size > 2 ? w->style.size - 2 : w->style.size, 0, 0);
    oldf = SelectObject(dc, f_title ? f_title : (HGDIOBJ)GetStockObject(DEFAULT_GUI_FONT));
    oldmode = SetBkMode(dc, TRANSPARENT);
    {
        unsigned tfg = cgw_theme_text(w->app);
        COLORREF main_col = (w->style.fg != CG_COLOR_NONE)
                            ? cgw_to_colorref(w->style.fg)
                            : (tfg != CG_COLOR_NONE ? cgw_to_colorref(tfg) : RGB(0x1D, 0x1B, 0x20));
        sub_col = cg_skin_neutral(w->app, IL_TEXT_LIGHT, IL_TEXT_DARK);
        SetTextColor(dc, main_col);
    }

    cell.left  = textx;
    cell.right = x1 - status_w;
    if (r->sub && *r->sub) {
        /* 两行：主标题占上 60%，副标题占下 —— 各自单行省略号 */
        cell.top = y + il_px(w, 4);
        cell.bottom = y + il_px(w, 26);
        ws = cgw_wc_from_utf8(r->title ? r->title : "");
        if (ws) {
            DrawTextW(dc, ws, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            free(ws);
        }
        SelectObject(dc, f_small ? f_small : (HGDIOBJ)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, cgw_to_colorref(sub_col));
        cell.top = y + il_px(w, 24);
        cell.bottom = y + rh - il_px(w, 3);
        ws = cgw_wc_from_utf8(r->sub);
        if (ws) {
            DrawTextW(dc, ws, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            free(ws);
        }
    } else {
        /* 没副标题：主标题整行垂直居中 */
        cell.top = y;
        cell.bottom = y + rh;
        ws = cgw_wc_from_utf8(r->title ? r->title : "");
        if (ws) {
            DrawTextW(dc, ws, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            free(ws);
        }
    }

    /* 状态文字（右对齐，与主标题同色系的小字） */
    if (r->status && *r->status) {
        RECT sr;
        SelectObject(dc, f_small ? f_small : (HGDIOBJ)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, cgw_to_colorref(sub_col));
        sr.left = cell.right + il_px(w, 4);
        sr.right = x1;
        sr.top = y;
        sr.bottom = y + (r->sub && *r->sub ? il_px(w, 26) : rh);
        ws = cgw_wc_from_utf8(r->status);
        if (ws) {
            DrawTextW(dc, ws, -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
            free(ws);
        }
    }

    SetBkMode(dc, oldmode);
    SelectObject(dc, oldf);
    (void)sc;

    /* 行间分隔线（最后一行不画；中性灰，与截图那种密集列表同款） */
    if (i + 1 < w->il_n) {
        RECT ln = { x0, y + rh - 1, x1, y + rh };
        HBRUSH br = CreateSolidBrush(cgw_to_colorref(cg_skin_neutral(w->app, 0xE3E3E8u, 0x2A2A2Eu)));
        FillRect(dc, &ln, br);
        DeleteObject(br);
    }
}

/** 双缓冲绘制回调的上下文（WM_PAINT 里填好传进来） */
typedef struct {
    cg_app_t    *app;
    HWND         hwnd;
    cg_widget_t *w;
} il_ctx_t;

/**
 * @brief iconlist 一帧：清底 -> 逐行画 -> 同步滚动条
 * @param mem 内存位图 DC（双缓冲的离屏 DC）
 * @param rc 离屏位图矩形（左上角为 0,0）
 * @param vctx 上下文（il_ctx_t*）
 * @return void 无返回值
 * @note 底必须画满 rc：内存位图是未初始化像素，留白就是垃圾上屏
 *       （本工程双缓冲的铁律，见 .codebuddy/memory 的双缓冲画满规矩）。
 */
static void iconlist_body(HDC mem, const RECT *rc, void *vctx)
{
    il_ctx_t   *c = (il_ctx_t *)vctx;
    cg_widget_t *w = c->w;
    int          rh = il_row_px(w);
    int          vis = il_visible_rows(w, rc);
    int          focused = (GetFocus() == c->hwnd);
    int          i, y;

    /* 底：显式背景色优先，否则与背后（窗口/卡面）连成一片 */
    if (w->style.bg != CG_COLOR_NONE)
        FillRect(mem, rc, cgw_brush_for(w->style.bg));
    else
        cgw_clear_to_behind(w, mem, rc);

    il_clamp_scroll(w, rc);
    for (i = w->il_scroll; i < w->il_n && i < w->il_scroll + vis; i++) {
        y = (i - w->il_scroll) * rh;
        il_draw_row(w, mem, rc, i, y, focused);
    }

    il_scroll_sync(c->hwnd, w, rc);
}

/* ------------------------------------------------------------ 窗口过程 */

/**
 * @brief 按鼠标客户区坐标算落在第几行（负值/越界 = 没落在任何行上）
 * @param w iconlist 控件
 * @param y 客户区纵坐标（物理像素）
 * @return int 行下标；没命中返回 -1
 */
static int il_hit_row(const cg_widget_t *w, int y)
{
    int row = w->il_scroll + y / il_row_px(w);
    return (row >= 0 && row < w->il_n) ? row : -1;
}

/**
 * @brief 派发一个事件（统一进出 g_win_in_notify，防回调重入；坐标折逻辑像素）
 * @param w 控件
 * @param ev 事件类型
 * @return void 无返回值
 */
static void il_emit(cg_widget_t *w, cg_event_t ev)
{
    if (!w || !cg_kind_supports_event(w->type, ev)) return;
    g_win_in_notify = 1;
    cg_dispatch(w, ev);
    g_win_in_notify = 0;
}

/**
 * @brief iconlist 的自绘窗口过程：绘制 + 点选/勾选/悬停/滚动/键盘导航
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数
 * @param lp 附加参数
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 * @note 焦点事件（gotfocus/lostfocus）由 cgw_FocusProc 统一派发，这里**不重复发** ——
 *       只在 WM_SETFOCUS/WM_KILLFOCUS 上补一次重绘（焦点框跟着亮灭）。
 */
static LRESULT CALLBACK IconListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    cg_widget_t *w   = app ? cgw_widget_by_hwnd(app, hwnd) : NULL;
    int         sc, y;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        il_ctx_t    ctx;
        GetClientRect(hwnd, &rc);
        if (w) {
            ctx.app = app;
            ctx.hwnd = hwnd;
            ctx.w = w;
            cgw_paint_buffered(dc, &rc, iconlist_body, &ctx);
        } else {
            FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));   /* 控件还没挂上的兜底帧 */
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        /* gotfocus/lostfocus 已由 FocusProc 派发，这里只让焦点框亮/灭 */
        if (w) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        if (!w || g_win_in_notify) break;
        sc = il_sc(w);
        y  = (int)(short)HIWORD(lp) * 100 / sc;
        {
            int row = il_hit_row(w, y);
            if (row < 0) break;
            SetFocus(hwnd);
            if (w->check) {
                int chkx = (int)(short)LOWORD(lp) * 100 / sc;
                if (chkx < IL_PAD + IL_CHK_W) {
                    /* 点在复选框列：只切换勾选，不改选中（与 table 点勾选格同口径） */
                    w->il_rows[row].checked = !w->il_rows[row].checked;
                    w->items_dirty = 1;
                    cg_widget_sync(w);
                    il_emit(w, CG_EVENT_CHECK);
                    break;
                }
            }
            if (w->value != row) {
                w->value = row;      /* 直接写：set_value 的夹取在 API 侧，这里行号必然合法 */
                w->items_dirty = 1;
                cg_widget_sync(w);
                il_emit(w, CG_EVENT_SELECT);
            } else {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_LBUTTONUP:
        /* 抬起补 click；双击由 cg_dispatch 按系统双击间隔合成（core/cg_event.c） */
        if (w && !g_win_in_notify) il_emit(w, CG_EVENT_CLICK);
        break;

    case WM_MOUSEMOVE:
        if (w && !g_win_in_notify) {
            TRACKMOUSEEVENT tme;
            sc = il_sc(w);
            y  = (int)(short)HIWORD(lp) * 100 / sc;
            {
                int row = il_hit_row(w, y);
                if (row != w->il_hover) {
                    w->il_hover = row;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;

    case WM_MOUSELEAVE:
        if (w && w->il_hover != -1) {
            w->il_hover = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case WM_MOUSEWHEEL:
        if (w) {
            RECT rc;
            int  delta = (int)(short)HIWORD(wp);
            int  step  = delta / WHEEL_DELTA;
            if (step == 0) step = (delta > 0) ? 1 : -1;   /* 高精度鼠标的小步长也滚 */
            GetClientRect(hwnd, &rc);
            w->il_scroll += step * 3;                     /* 一次滚 3 行，与 listbox 手感接近 */
            il_clamp_scroll(w, &rc);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;   /* 不交给 DefWindowProc：它会把滚轮转给焦点窗口，滚两遍 */

    case WM_VSCROLL: {
        /* 原生滚动条的 UI 归系统，**内容位移归我们** —— 自绘控件没有"替你滚"的默认行为 */
        if (w) {
            RECT rc;
            int  max, pos = w->il_scroll;
            int  vis;
            GetClientRect(hwnd, &rc);
            vis = il_visible_rows(w, &rc);
            max = w->il_n - vis;
            if (max < 0) max = 0;
            switch (LOWORD(wp)) {
            case SB_LINEUP:        pos--; break;
            case SB_LINEDOWN:      pos++; break;
            case SB_PAGEUP:        pos -= vis; break;
            case SB_PAGEDOWN:      pos += vis; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = (int)HIWORD(wp); break;
            case SB_TOP:           pos = 0; break;
            case SB_BOTTOM:        pos = max; break;
            default: break;
            }
            if (pos > max) pos = max;
            if (pos < 0)   pos = 0;
            if (pos != w->il_scroll) {
                w->il_scroll = pos;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            il_scroll_sync(hwnd, w, &rc);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (w && !g_win_in_notify) {
            RECT rc;
            int  moved = 0;
            GetClientRect(hwnd, &rc);
            switch ((int)wp) {
            case VK_UP:
            case VK_DOWN: {
                int nv = w->value + ((int)wp == VK_DOWN ? 1 : -1);
                if (w->il_n <= 0) break;
                if (nv < 0) nv = 0;
                if (nv > w->il_n - 1) nv = w->il_n - 1;
                if (nv != w->value) {
                    w->value = nv;
                    il_ensure_visible(w, nv, &rc);
                    w->items_dirty = 1;
                    cg_widget_sync(w);
                    il_emit(w, CG_EVENT_SELECT);
                    moved = 1;
                }
                break;
            }
            case VK_HOME:
            case VK_END: {
                int nv = ((int)wp == VK_END) ? w->il_n - 1 : 0;
                if (w->il_n <= 0 || nv != w->value) {
                    if (w->il_n > 0) {
                        w->value = nv;
                        il_ensure_visible(w, nv, &rc);
                        w->items_dirty = 1;
                        cg_widget_sync(w);
                        il_emit(w, CG_EVENT_SELECT);
                        moved = 1;
                    }
                }
                break;
            }
            case VK_SPACE:
                /* 空格切当前行勾选（有复选框列才有意义） */
                if (w->check && w->value >= 0 && w->value < w->il_n) {
                    w->il_rows[w->value].checked = !w->il_rows[w->value].checked;
                    w->items_dirty = 1;
                    cg_widget_sync(w);
                    il_emit(w, CG_EVENT_CHECK);
                    moved = 1;
                }
                break;
            case VK_RETURN:
                il_emit(w, CG_EVENT_CLICK);
                break;
            default:
                break;
            }
            if (moved) InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/**
 * @brief 注册 iconlist 的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
void cgw_register_iconlist_class(void)
{
    static int done = 0;
    if (done) return;
    cgw_register_paint_class(CG_ICONLIST_CLASS, IconListProc, IDC_ARROW, NULL);
    done = 1;
}
