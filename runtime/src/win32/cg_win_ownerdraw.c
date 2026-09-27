/**
 * @file    cg_win_ownerdraw.c
 * @brief   owner-draw 项绘制：图标按钮、色块、徽章、标签片、评分、分页、WM_DRAWITEM 分发
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/** 图标按钮：把 src 图片按比例居中画在按钮里（没有图就画个占位框） */
static void draw_icon_button(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT   rc = dis->rcItem;
    HDC    dc = dis->hDC;
    HBRUSH bg;
    int    box_w = rc.right - rc.left;
    int    box_h = rc.bottom - rc.top;

    bg = CreateSolidBrush((w->style.bg != CG_COLOR_NONE)
                              ? cgw_to_colorref(w->style.bg)
                              : cgw_to_colorref(cg_win_behind_rgb(w->app, w->native)));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    if (w->bitmap) {
        BITMAP bm;
        HDC    mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, (HGDIOBJ)w->bitmap);
        if (GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            int maxw = box_w - 8 > 1 ? box_w - 8 : box_w;
            int maxh = box_h - 8 > 1 ? box_h - 8 : box_h;
            int dw = bm.bmWidth;
            int dh = bm.bmHeight;
            int x, y;

            /* 只缩不放：小图标保持原尺寸，大图标等比缩到装得下 */
            if (dw > maxw || dh > maxh) {
                int sx = (long long)maxw * 100 / dw;
                int sy = (long long)maxh * 100 / dh;
                int s = sx < sy ? sx : sy;
                dw = bm.bmWidth * s / 100;
                dh = bm.bmHeight * s / 100;
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;
            }
            x = rc.left + (box_w - dw) / 2;
            y = rc.top + (box_h - dh) / 2;

            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, x, y, dw, dh, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        }
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        RECT inner = rc;

        InflateRect(&inner, -4, -4);
        FrameRect(dc, &inner, (HBRUSH)GetStockObject(GRAY_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 120, 128));
        DrawTextW(dc, L"图标", -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ------------------------------------------------------ 自绘小控件
 * colorpicker / badge / tag / rate / pagination 都是 BS_OWNERDRAW 按钮，
 * 画什么由各自的状态决定，和 switch 一个路数。
 */

/** @brief 取控件前景色；style.fg 未设置时用 fallback */
static unsigned widget_fg_or(const cg_widget_t *w, unsigned fallback)
{
    return (w->style.fg != CG_COLOR_NONE) ? w->style.fg : fallback;
}

/** 颜色选择：整个按钮就是一块当前颜色，边上勾一圈边框 */
static void draw_color_swatch(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    unsigned rgb = (w->value > 0) ? (unsigned)w->value : 0x6750A4;
    HBRUSH br = CreateSolidBrush(cgw_to_colorref(rgb));
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);
    FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
}

/** 徽章：圆角小圆片 + 文字，底色用 style.bg（没设用品牌红） */
static void draw_badge(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;

    cgw_clear_to_behind(w, dis->hDC, &rc);   /* 先清掉 owner-draw 默认灰底 */
    /* 走皮肤的抗锯齿原语：这里原来是 GDI `RoundRect`，没有抗锯齿 —— 圆角是"毛边"的
     * （2026-09-24 用户报"徽章和标签片锯齿状、没有丝滑 iOS 风格"）。半径 = 高度即胶囊形。 */
    cg_skin_fill_round_rect(dis->hDC, &rc, rc.bottom - rc.top,
                            (w->style.bg != CG_COLOR_NONE) ? w->style.bg : 0xB3261Eu,
                            CG_COLOR_NONE, 0.0f);
    {
        wchar_t *wt = cgw_wc_from_utf8(w->text ? w->text : "");
        if (wt) {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, cgw_to_colorref(widget_fg_or(w, 0xFFFFFF)));
            DrawTextW(dis->hDC, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            free(wt);
        }
    }
}

/** 标签片：圆角底 + 同色系边框 + 文字，像网页里的 tag */
static void draw_tag(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    RECT rr = { rc.left, rc.top, rc.right - 1, rc.bottom - 1 };

    cgw_clear_to_behind(w, dis->hDC, &rc);   /* 先清掉 owner-draw 默认灰底 */
    /* 同徽章：原来的 GDI `RoundRect` 没有抗锯齿（边框那圈锯齿最明显）。
     * 内缩 1px 让 1px 描边完整落在控件里（GDI+ 的笔是以路径为中心画的）。 */
    cg_skin_fill_round_rect(dis->hDC, &rr, rr.bottom - rr.top,
                            (w->style.bg != CG_COLOR_NONE) ? w->style.bg : 0xE8DEF8u,
                            widget_fg_or(w, 0x6750A4), 1.0f);
    {
        wchar_t *wt = cgw_wc_from_utf8(w->text ? w->text : "");
        if (wt) {
            SetBkMode(dis->hDC, TRANSPARENT);
            /* 默认字色按**标签片自己的默认浅底**（0xE8DEF8）挑：深色下这个浅紫填充不跟随主题，
             * 所以字保持深色才对（与按钮那条"画在 M3 浅紫填充上的字不变浅"同一个道理）。
             * 唯一的坑：控件自己写了**深**底却没写 fg，就会深底深字 —— 本工程的约定是
             * "写了 bg 就按自己的来、不做自动对比色"，所以这种情况请连 fg 一起写。
             * （2026-09-27 定论：这处**不需要**改成跟随主题。） */
            SetTextColor(dis->hDC, cgw_to_colorref(widget_fg_or(w, 0x1D1B20)));
            DrawTextW(dis->hDC, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            free(wt);
        }
    }
}

/** 评分：★☆ 按值画，样式字体没给就用默认大小 */
static void draw_rate(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    int stars = w->vmax - w->vmin;
    int val = w->value - w->vmin;
    wchar_t buf[32];
    int filled, empty;

    if (stars < 1) stars = 1;
    if (stars > 10) stars = 10;
    if (val < 0) val = 0;
    if (val > stars) val = stars;

    filled = val;
    empty = stars - filled;

    /* 先清底：设了背景色用它，否则用"背后"的颜色（owner-draw 默认是系统面色） */
    FillRect(dis->hDC, &rc, (w->style.bg != CG_COLOR_NONE)
                                ? cgw_brush_for(w->style.bg)
                                : cgw_brush_for(cg_win_behind_rgb(w->app, w->native)));
    {
        /* ★☆ 要用符号字体：正文默认字体（雅黑之类）没有这两个字形，GDI 又不做字体回退，
         * 画出来就是一排方块（浏览器会自动回退，所以画布上是好的、运行时是方块）。
         * 这里固定用 Segoe UI Symbol（系统自带），保留用户设的字号。 */
        HFONT f = cgw_font_for(w->app, "Segoe UI Symbol", w->style.size > 0 ? w->style.size : 12, 0, 0);
        HFONT oldf = (HFONT)SelectObject(dis->hDC, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dis->hDC, TRANSPARENT);
        if (filled > 0) {
            RECT fr = rc;
            /* 注意 swprintf 里 %s 是"多字节串"、宽串必须写 %ls（写错会把宽字面量当 char* 读，出方块） */
            swprintf(buf, 32, L"%.*ls", filled, L"★★★★★★★★★★");
            SetTextColor(dis->hDC, cgw_to_colorref(widget_fg_or(w, 0xF2B219)));
            DrawTextW(dis->hDC, buf, filled, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (empty > 0) {
            RECT er = rc;
            /* 空星接在实星后面：先量实星的宽度 */
            SIZE sz = {0, 0};
            wchar_t one = L'★';
            GetTextExtentPoint32W(dis->hDC, &one, 1, &sz);
            er.left += sz.cx * filled;
            swprintf(buf, 32, L"%.*ls", empty, L"☆☆☆☆☆☆☆☆☆☆");
            SetTextColor(dis->hDC, RGB(0xC4, 0xC7, 0xCC));
            DrawTextW(dis->hDC, buf, empty, &er, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dis->hDC, oldf);
    }
}

/** 分页：<  页码  >，点两侧翻页（点击判定在 cgw_FocusProc） */
static void draw_pagination(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    wchar_t mid[32];
    swprintf(mid, 32, L"%d / %d", w->value - w->vmin + 1, w->vmax - w->vmin + 1);

    FillRect(dis->hDC, &rc, cgw_brush_for(cg_win_behind_rgb(w->app, w->native)));
    {
        RECT l = rc, r = rc;
        wchar_t *lt = cgw_wc_from_utf8(w->value <= w->vmin ? "" : "<");
        wchar_t *rt = cgw_wc_from_utf8(w->value >= w->vmax ? "" : ">");
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
        l.right = rc.left + (rc.right - rc.left) / 3;
        r.left = rc.right - (rc.right - rc.left) / 3;
        if (lt) { DrawTextW(dis->hDC, lt, -1, &l, DT_CENTER | DT_VCENTER | DT_SINGLELINE); free(lt); }
        if (rt) { DrawTextW(dis->hDC, rt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); free(rt); }
        DrawTextW(dis->hDC, mid, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/** WM_DRAWITEM：自绘控件统一分发（绘制本体在 cg_skin.c / 各 draw_* 函数） */
static void cgw_draw_own_item_impl(cg_app_t *app, const DRAWITEMSTRUCT *dis);  /* 在下面 */

/** @brief owner-draw 统一分发（双缓冲）：整帧画进内存位图后一次贴屏
 *  @details 各皮肤是"填底 → 画形状 → 画文字"的多步直绘，每步都直接落在屏幕
 *           DC 上，中间态可见 —— 复选框/开关一重绘，文字先消失一帧再回来，
 *           悬停/点击的高频重绘下就是肉眼闪烁（2026-09-25 用户报"复选框的
 *           文字也会闪烁"）。与圆环/标签同一治法：内存位图画好一次 BitBlt。 */
void cgw_draw_own_item(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    int wpx, hpx;
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ ob;
    DRAWITEMSTRUCT d2;

    if (!dis || !dis->hDC) return;
    wpx = dis->rcItem.right - dis->rcItem.left;
    hpx = dis->rcItem.bottom - dis->rcItem.top;
    if (wpx <= 0 || hpx <= 0) return;

    mem = CreateCompatibleDC(dis->hDC);
    bmp = CreateCompatibleBitmap(dis->hDC, wpx, hpx);
    if (!mem || !bmp) {                    /* 资源紧张：退回直绘，丑但别不画 */
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        cgw_draw_own_item_impl(app, dis);
        return;
    }
    ob = SelectObject(mem, bmp);
    d2 = *dis;
    d2.hDC = mem;
    d2.rcItem.left = 0;    d2.rcItem.top = 0;
    d2.rcItem.right = wpx; d2.rcItem.bottom = hpx;
    cgw_draw_own_item_impl(app, &d2);
    BitBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, wpx, hpx, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void cgw_draw_own_item_impl(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    cg_widget_t *w = cgw_widget_by_hwnd(app, dis->hwndItem);

    if (!w) return;
    if (cgw_is_t(w, "button")) { cg_skin_draw_button(app, dis); return; }  /* M3 圆角自绘 */
    if (cgw_is_t(w, "checkbox") || cgw_is_t(w, "radio")) { cg_skin_draw_check(app, dis); return; }  /* M3 勾选/单选自绘 */
    if (cgw_is_t(w, "switch")) { cg_skin_draw_switch(w, dis->hDC, &dis->rcItem); return; }  /* M3 开关自绘 */
    if (cgw_is_t(w, "iconbutton")) {
        draw_icon_button(w, dis);
        return;
    }
    if (cgw_is_t(w, "colorpicker")) { draw_color_swatch(w, dis); return; }
    if (cgw_is_t(w, "badge"))       { draw_badge(w, dis); return; }
    if (cgw_is_t(w, "tag"))         { draw_tag(w, dis); return; }
    if (cgw_is_t(w, "rate"))        { draw_rate(w, dis); return; }
    if (cgw_is_t(w, "pagination"))  { draw_pagination(w, dis); return; }
}

/**
 * @brief WM_COMMAND 入口：反查控件后走 cgw_notify_body（带防重入保护）
 * @param app 所属应用；hwnd 发通知的控件窗口；code 通知码；cmd_id 命令 ID
 * @return void 无返回值
 */
void cgw_notify_from(cg_app_t *app, HWND hwnd, int code, int cmd_id)
{
    /* 先按窗口句柄找：数值框有两个 HWND，按 ctrl id 会漏掉箭头 */
    cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
    if (!w) w = cg_widget_by_ctrl_id(app, GetDlgCtrlID(hwnd));

    if (!w || g_win_in_notify) return;
    g_win_in_notify = 1;
    cgw_notify_body(app, w, hwnd, code, cmd_id);
    g_win_in_notify = 0;
}
