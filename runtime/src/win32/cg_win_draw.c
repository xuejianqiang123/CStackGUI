/**
 * @file    cg_win_draw.c
 * @brief   自绘窗口类与窗口过程：image / 圆环 / 进度条 / 画板 / 面板
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/** 图片控件：按控件大小 StretchBlt，缺图时画占位框 */
static LRESULT CALLBACK ImageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);

        if (w && w->bitmap) {
            BITMAP bm;
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, (HGDIOBJ)w->bitmap);
            if (GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm)) {
                int prev = SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, NULL);
                StretchBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                           mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SetStretchBltMode(dc, prev);
            }
            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            COLORREF face = cgw_to_colorref(cg_win_behind_rgb(w->app, w->native));
            HBRUSH br = CreateSolidBrush(face);
            HGDIOBJ oldbr = SelectObject(dc, br);
            HPEN pen = CreatePen(PS_DASH, 1, RGB(120, 120, 128));
            HGDIOBJ oldpen = SelectObject(dc, pen);
            Rectangle(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(dc, oldpen);
            DeleteObject(pen);
            SelectObject(dc, oldbr);
            DeleteObject(br);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(90, 90, 96));
            if (w && w->src && *w->src) {
                wchar_t *wt = cgw_wc_from_utf8(w->src);
                if (wt) {
                    DrawTextW(dc, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    free(wt);
                }
            } else {
                const wchar_t *ph = L"图片";
                DrawTextW(dc, ph, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册图片控件的自绘窗口类（整进程一次） */
void cgw_register_image_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ImageProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_IMAGE_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 环状进度（自绘）
 * 原生控件里没有圆环进度，这个和 switch 一样是 owner-draw 思路的样板：
 * 自己画在窗口上，所以能吃 style 的颜色，也才能有圆角/圆弧这种原生控件画不出的东西。
 */
/**
 * @brief 画圆环中间那一行百分比文字
 * @param dc 目标 DC；w 控件；rc 客户区
 * @details 圆环本身（底色 / 轨道 / 进度弧）在 cg_skin.c 的 `cg_skin_draw_ring()` 里
 *          用 GDI+ 抗锯齿画（GDI 的粗弧锯齿非常明显）；文字要用本文件的 `cgw_font_for()`，
 *          所以留在调用方画完圆环之后补。
 * @return void 无返回值
 */
static void draw_ring_text(HDC dc, const cg_widget_t *w, const RECT *rc)
{
    int span = (w->vmax > w->vmin) ? (w->vmax - w->vmin) : 1;
    int val = w->value;
    wchar_t text[32];
    HFONT f, oldf;
    RECT tr = *rc;

    if (val < w->vmin) val = w->vmin;
    if (val > w->vmax) val = w->vmax;

    f = w->app ? cgw_font_for(w->app, w->style.font, w->style.size, w->style.bold, 0) : NULL;
    oldf = (HFONT)SelectObject(dc, f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    swprintf(text, 32, L"%d%%", (val - w->vmin) * 100 / span);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldf);
}

/** @brief 圆环进度控件窗口过程（自绘，无交互） */
static LRESULT CALLBACK RingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            cg_skin_draw_ring(w, dc, &rc);   /* GDI+ 抗锯齿：底色 + 轨道 + 进度弧 */
            draw_ring_text(dc, w, &rc);      /* 中心百分比 */
        } else {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册圆环进度的自绘窗口类（整进程一次） */
void cgw_register_ring_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RingProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_RING_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/** @brief 进度条窗口过程（自绘，无交互）。原生的 PROGRESS_CLASS
 *  不管主题开没开都不发 NM_CUSTOMDRAW，只能整个换成自己的类。 */
static LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            cg_skin_draw_progress(w, dc, &rc);
        } else {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册进度条的自绘窗口类（整进程一次） */
void cgw_register_progress_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_PROGRESS_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 画板（自绘）
 * cg_canvas_* 命令把每笔记录在 w->ops 里，WM_PAINT 按序回放。
 * 坐标是逻辑像素（跟 .cgui 同一套），绘制时按 DPI 缩放。
 */
/** @brief 画板坐标：逻辑像素 -> 物理像素（与控件布局同一套缩放） */
static int canvas_scale(const cg_widget_t *w, int logical)
{
    return cgw_scale_px(w->app, logical);
}

/**
 * @brief 画板绘制：填底色后按记录顺序回放 cg_canvas_* 的绘制命令
 * @param dc 目标 DC；w canvas 控件；rc 客户区
 * @return void 无返回值
 */
static void draw_canvas(HDC dc, cg_widget_t *w, const RECT *rc)
{
    COLORREF bgc = (w->style.bg != CG_COLOR_NONE)
                       ? cgw_to_colorref(w->style.bg)
                       : cgw_to_colorref(cg_win_behind_rgb(w->app, w->native));
    HBRUSH bg = CreateSolidBrush(bgc);
    FillRect(dc, rc, bg);
    DeleteObject(bg);

    for (int i = 0; i < w->nops; i++) {
        const cg_canvas_op_t *op = &w->ops[i];
        HPEN pen = CreatePen(PS_SOLID, op->penw > 0 ? op->penw : 1, cgw_to_colorref(op->color));
        HGDIOBJ oldpen = SelectObject(dc, pen);
        HBRUSH br = NULL;
        HGDIOBJ oldbr = NULL;

        if (op->fill) {
            br = CreateSolidBrush(cgw_to_colorref(op->color));
            oldbr = SelectObject(dc, br);
        } else {
            oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        }

        switch (op->type) {
        case CG_CANVAS_OP_LINE:
            MoveToEx(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1), NULL);
            LineTo(dc, canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_RECT:
            Rectangle(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                      canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_ELLIPSE:
            Ellipse(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                    canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_TEXT: {
            wchar_t *wt = cgw_wc_from_utf8(op->text ? op->text : "");
            HFONT f = cgw_font_for(w->app, w->style.font,
                               w->style.size > 0 ? w->style.size : 10, w->style.bold, 0);
            HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, cgw_to_colorref(op->color));
            if (wt) {
                TextOutW(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                         wt, (int)wcslen(wt));
                free(wt);
            }
            SelectObject(dc, oldf);
            break;
        }
        default:
            break;
        }
        SelectObject(dc, oldpen);
        DeleteObject(pen);
        if (br) { SelectObject(dc, oldbr); DeleteObject(br); }
        else SelectObject(dc, oldbr);
    }
}

/** @brief 画板控件窗口过程：自绘 + 左键抬起派发 click（双击由运行时合成） */
static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            draw_canvas(dc, w, &rc);
        } else {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        if (w && !g_win_in_notify) {
            g_win_in_notify = 1;
            cg_dispatch(w, CG_EVENT_CLICK);   /* 双击由运行时按系统间隔合成 */
            g_win_in_notify = 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册画板的自绘窗口类（整进程一次） */
void cgw_register_canvas_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CanvasProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_CANVAS_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 面板窗口 */

/** @brief 画整个组框（group/box）：内区底色 + 压在顶边线上的标题 + 边框
 *  @param app 所属应用；dc 客户区 DC；w 面板控件；rc 客户区矩形
 *  @details 三样都得自己画（**不能用 WS_BORDER**）：交给系统画边框时，系统重绘非客户区
 *           会把标题盖掉，看起来就是"边框线穿过文字"。标题的上下各占一半、中线压在顶边线
 *           上（原生 BS_GROUPBOX 就是这个观感），并且用组框底色把那一小段线擦开。
 *           顶边线以上那一条留空，透出"背后"的颜色（和画布 `top:-8px` 的关系一致）。
 */
static void panel_draw_group(cg_app_t *app, HDC dc, cg_widget_t *w, const RECT *rc)
{
    int inset = cgw_scale_px(app, 1);
    int has_title = (w->text && *w->text) ? 1 : 0;
    int top;
    unsigned bg = (w->style.bg != CG_COLOR_NONE) ? w->style.bg
                                                : cg_win_behind_rgb(app, w->native);
    COLORREF penc = (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                   : RGB(0x79, 0x74, 0x7E);   /* 画布 --outline-strong */

    if (inset < 1) inset = 1;
    top = has_title ? cgw_scale_px(app, 8) : inset;   /* 顶边线：有标题就让出一条给标题 */

    /* 内区底色（顶边线以上不填，透出背后） */
    {
        RECT b = *rc;
        b.top = top;
        if (b.right > b.left && b.bottom > b.top) FillRect(dc, &b, cgw_brush_for(bg));
    }

    /* 边框 */
    {
        HPEN pen = CreatePen(PS_SOLID, inset, penc);
        HGDIOBJ oldpen = SelectObject(dc, pen);
        HGDIOBJ oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc->left, top, rc->right, rc->bottom);
        SelectObject(dc, oldbr);
        SelectObject(dc, oldpen);
        DeleteObject(pen);
    }

    /* 标题：先铺一小块自己的底色擦开顶边线，再写字 */
    if (has_title) {
        HFONT f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 10, w->style.bold, 0);
        HFONT oldf = (HFONT)SelectObject(dc, f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        wchar_t *wt = cgw_wc_from_utf8(w->text);
        int pad = cgw_scale_px(app, 4);
        SIZE sz;
        sz.cx = 0;
        sz.cy = cgw_scale_px(app, 16);
        if (wt) GetTextExtentPoint32W(dc, wt, (int)wcslen(wt), &sz);
        {
            RECT tr;
            tr.left = cgw_scale_px(app, 8);
            tr.right = tr.left + sz.cx + pad * 2;
            if (tr.right > rc->right - inset) tr.right = rc->right - inset;
            tr.top = top - sz.cy / 2;
            tr.bottom = tr.top + sz.cy + 2;
            if (tr.top < 0) { tr.bottom -= tr.top; tr.top = 0; }
            FillRect(dc, &tr, cgw_brush_for(bg));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                            : GetSysColor(COLOR_WINDOWTEXT));
            if (wt) DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (wt) free(wt);
        SelectObject(dc, oldf);
    }
}

/** @brief 面板窗口过程（group/box/card/shape）：通知上浮主窗口 + card/shape 自绘 */
LRESULT CALLBACK cgw_PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_SETCURSOR:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        /* 子控件把通知发给直接父窗口，这里转发到主窗口统一处理。
         * 分组框里的树/表格/开关/链接都靠这条转发才能收到事件；
         * WM_CTLCOLOR* 也要转发，否则分组框里的标签拿不到"透明"背景。 */
        if (app && app->hwnd && (HWND)app->hwnd != hwnd) {
            return SendMessageW((HWND)app->hwnd, msg, wp, lp);
        }
        break;
    case WM_ERASEBKGND:
        /* 面板可见的部分全在 WM_PAINT 里画（内区底色 / 边框 / 标题 / 圆角形状），
         * 这里再填一遍会把"顶边线以上透出背后"的效果糊掉。 */
        return 1;
    case WM_PAINT: {
        /* card / shape 画圆角/椭圆边框，group / box 连底色带标题一起画 */
        cg_widget_t *w = app ? cgw_widget_by_hwnd(app, hwnd) : NULL;
        if (w && (cgw_is_t(w, "group") || cgw_is_t(w, "box"))) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            panel_draw_group(app, dc, w, &rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (!w || (!cgw_is_t(w, "card") && !cgw_is_t(w, "shape"))) break;
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int inset = cgw_scale_px(app, 1);
            COLORREF fillc = (w->style.bg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.bg) : 0;
            COLORREF penc = (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                           : RGB(0xB4, 0xB0, 0xBB);
            HPEN pen = CreatePen(PS_SOLID, inset ? inset : 1, penc);
            HGDIOBJ oldpen = SelectObject(dc, pen);
            HBRUSH br = NULL;
            HGDIOBJ oldbr;
            if (cgw_is_t(w, "card") || w->style.bg != CG_COLOR_NONE) {
                br = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? fillc : GetSysColor(COLOR_BTNFACE));
                oldbr = SelectObject(dc, br);
            } else {
                oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            }
            if (cgw_is_t(w, "shape") && w->format && strcmp(w->format, "ellipse") == 0) {
                Ellipse(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            } else if (cgw_is_t(w, "shape") && (!w->format || strcmp(w->format, "rect") != 0)) {
                int r = cgw_scale_px(app, (w->style.radius > 0) ? w->style.radius : 8);
                RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, r, r);
            } else if (cgw_is_t(w, "card")) {
                int r = cgw_scale_px(app, (w->style.radius > 0) ? w->style.radius : 12);
                RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, r, r);
            } else {
                Rectangle(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            }
            SelectObject(dc, oldbr);
            if (br) DeleteObject(br);
            SelectObject(dc, oldpen);
            DeleteObject(pen);

            /* 卡片顶部画标题 */
            if (cgw_is_t(w, "card") && w->text && *w->text) {
                HFONT f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 10, w->style.bold, 0);
                HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
                RECT tr = rc;
                tr.left += cgw_scale_px(app, 12);
                tr.top += cgw_scale_px(app, 6);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                                : GetSysColor(COLOR_WINDOWTEXT));
                wchar_t *wt = cgw_wc_from_utf8(w->text);
                if (wt) { DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_TOP); free(wt); }
                SelectObject(dc, oldf);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册面板窗口类（能装子控件的自绘底板，整进程一次） */
void cgw_register_panel_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = cgw_PanelProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CG_PANEL_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 分隔条（splitter）
 * 一根能拖的细条。**几何计算在 core**（cg_splitter_apply，纯 rect 运算、可脱窗单测），
 * 这里只做两件事：鼠标（捕获 + 光标 + 把位移喂进去）、外观（一条线 + 三个握点）。
 */
static int s_split_origin;   /* 按下时鼠标的屏幕坐标（竖条看 x，横条看 y） */

/** @brief 分隔条窗口过程：拖动改两侧面板尺寸，松手派发一次 release */
static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);

    (void)wp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc, r;
        HBRUSH      line, dot;
        int         cx, cy, i, vert;

        GetClientRect(hwnd, &rc);
        if (!w) {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        /* 先把整块填成"背后"的颜色：分隔条是"没有自己底色"的自绘控件 */
        cgw_clear_to_behind(w, dc, &rc);

        vert = cgw_splitter_is_vert(w);
        cx = rc.left + (rc.right - rc.left) / 2;
        cy = rc.top + (rc.bottom - rc.top) / 2;
        line = cgw_brush_for(0xCAC4D0);
        dot  = cgw_brush_for(0x79747E);

        /* 中间一条 1px 的线 + 三个握点，一眼能看出"这里能拖" */
        r = rc;
        if (vert) { r.left = cx; r.right = cx + 1; } else { r.top = cy; r.bottom = cy + 1; }
        FillRect(dc, &r, line);
        for (i = -1; i <= 1; i++) {
            RECT d;
            if (vert) {
                d.left = cx - 1; d.right = cx + 2;
                d.top = cy + i * 6 - 1; d.bottom = cy + i * 6 + 2;
            } else {
                d.top = cy - 1; d.bottom = cy + 2;
                d.left = cx + i * 6 - 1; d.right = cx + i * 6 + 2;
            }
            FillRect(dc, &d, dot);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (w && LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, cgw_splitter_is_vert(w) ? IDC_SIZEWE : IDC_SIZENS));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (!w) break;
        SetCapture(hwnd);
        {
            POINT pt;
            GetCursorPos(&pt);
            s_split_origin = cgw_splitter_is_vert(w) ? (int)pt.x : (int)pt.y;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (!w || GetCapture() != hwnd) break;
        {
            POINT pt;
            int   now, moved;
            GetCursorPos(&pt);
            now   = cgw_splitter_is_vert(w) ? (int)pt.x : (int)pt.y;
            moved = cg_splitter_apply(w, now - s_split_origin);
            /* 被最小尺寸夹住时原点不跟走，鼠标退回来还能继续拖 */
            if (moved) s_split_origin += moved;
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        /* 拖完通知一次：业务在这里把布局存进 INI 最合适 */
        if (w) cg_dispatch(w, CG_EVENT_RELEASE);
        return 0;
    case WM_CAPTURECHANGED:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册分隔条的自绘窗口类（整进程一次） */
void cgw_register_splitter_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SplitterProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_SIZEWE);
    wc.lpszClassName = CG_SPLITTER_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 折线 / 柱状图（chart）
 * 全自绘（GDI，不走 GDI+）：网格 + 折线（批量 Polyline）/ 柱 + 图例 + 轴上下的范围文字。
 * 为什么要说的三件事：
 *   1. **点数超过绘图区像素宽度时不抽点**，而是对每个像素列取该列覆盖点的 min/max 画一条
 *      竖线（LVGL 的做法）—— 既不慢，也不会刚好把那个尖峰抽掉。
 *   2. 折线用一次 **Polyline** 批量提交（比逐段 LineTo 快约一半），且数据线不做抗锯齿。
 *   3. 这里不自己定时：实时性靠业务用 cg_set_timer 定周期 cg_chart_push，画的时候只读现成数组。
 */
/** 折线路径的栈缓冲上限（点数超过它、或超过像素宽度就走按列 min/max） */
#define CG_CHART_MAX_PTS 1024

/** 系列调色板（8 个够用；.cgui 不配颜色，按系列序取） */
static const COLORREF k_chart_colors[8] = {
    RGB(0x67, 0x50, 0xA4), RGB(0x1B, 0x6E, 0x33), RGB(0xC0, 0x4A, 0x2B),
    RGB(0x14, 0x5C, 0xA8), RGB(0xB0, 0x7A, 0x00), RGB(0x7A, 0x4E, 0x8A),
    RGB(0x00, 0x6B, 0x6B), RGB(0x8C, 0x4A, 0x4A),
};

/** @brief 值 -> 绘图区里的 y 像素（超范围按边界裁剪，所以越界数据不会画出框） */
static int chart_map_y(const cg_widget_t *w, int v, int top, int bottom)
{
    int range = w->vmax - w->vmin;

    if (range <= 0) range = 1;
    if (v < w->vmin) v = w->vmin;
    if (v > w->vmax) v = w->vmax;
    return bottom - (int)((long long)(v - w->vmin) * (bottom - top) / range);
}

/** @brief 第 i 个点落在哪一列（右对齐：最新一个点在最右边） */
static int chart_column(int i, int fill, int pw)
{
    if (fill <= 1 || pw <= 1) return 0;
    return (pw - 1) - (int)((long long)(fill - 1 - i) * (pw - 1) / (fill - 1));
}

/** @brief 画一个系列（折线或柱） */
static void chart_draw_series(HDC dc, const cg_widget_t *w, int series,
                              const RECT *plot, COLORREF col, int bars)
{
    int fill = cg_chart_filled(w, series);
    int pw   = plot->right - plot->left;
    int i;

    if (fill <= 0 || pw < 2) return;

    if (bars) {
        HBRUSH br = CreateSolidBrush(col);
        int step = (pw - 1) / (fill > 1 ? fill - 1 : 1);
        int bw = step > 2 ? step - 1 : 1;
        for (i = 0; i < fill; i++) {
            int x = plot->right - (fill - 1 - i) * step;
            int y = chart_map_y(w, cg_chart_point(w, series, i), plot->top + 1, plot->bottom - 1);
            RECT r;
            r.left = x; r.right = x + bw;
            r.top = y;  r.bottom = plot->bottom;
            if (r.right > plot->right + 1) r.right = plot->right + 1;
            if (r.left < plot->left) continue;
            FillRect(dc, &r, br);
        }
        DeleteObject(br);
        return;
    }

    if (fill <= pw && fill <= CG_CHART_MAX_PTS) {
        /* 稀疏：一条折线整批画出去 */
        POINT pts[CG_CHART_MAX_PTS];
        int   n = 0;
        int   step = (pw - 1) / (fill > 1 ? fill - 1 : 1);
        HPEN  pen;
        HGDIOBJ old;

        if (step < 1) step = 1;
        for (i = 0; i < fill; i++) {
            int x = plot->right - (fill - 1 - i) * step;
            if (x < plot->left) break;              /* 左边界外的不画，免得压到边框和刻度上 */
            pts[n].x = x;
            pts[n].y = chart_map_y(w, cg_chart_point(w, series, i), plot->top + 1, plot->bottom - 1);
            n++;
        }
        if (n >= 2) {
            pen = CreatePen(PS_SOLID, 1, col);
            old = SelectObject(dc, pen);
            Polyline(dc, pts, n);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        return;
    }

    /* 密集：按像素列取 min/max 画竖线 —— 不抽点，尖峰不会被抽掉 */
    {
        HPEN    pen = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ old = SelectObject(dc, pen);
        int     c = 0;

        for (i = 0; i < fill; i++) {
            int lo, hi, y1, y2, v;

            while (i < fill && chart_column(i, fill, pw) < c) i++;
            if (i >= fill) break;
            lo = hi = cg_chart_point(w, series, i);
            while (i < fill && chart_column(i, fill, pw) == c) {
                v = cg_chart_point(w, series, i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                i++;
            }
            i--;                                    /* 外层循环还要 +1 */
            y1 = chart_map_y(w, hi, plot->top + 1, plot->bottom - 1);
            y2 = chart_map_y(w, lo, plot->top + 1, plot->bottom - 1);
            MoveToEx(dc, plot->left + c, y1, NULL);
            LineTo(dc, plot->left + c, y2 == y1 ? y1 + 1 : y2);
            c++;
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

/** @brief 画整个图表：底、网格、边框、各系列、范围文字、图例 */
static void chart_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    RECT  plot;
    int   right_pad = 44;                    /* 给图例留的地方（有系列名时） */
    int   n = cg_chart_series_count(w);
    int   bars = (w->format && strcmp(w->format, "bar") == 0);
    int   i;
    HBRUSH bg, grid;
    HPEN  frame;
    HGDIOBJ oldpen;

    if (w->style.bg != CG_COLOR_NONE) bg = cgw_brush_for(w->style.bg);
    else                              bg = cgw_brush_for(cg_win_behind_rgb(app, (void *)w->native));
    FillRect(dc, rc, bg);

    /* 绘图区：留 28px 给左边的刻度文字、8px 内边距；有名字才给右边留图例位 */
    plot.left = rc->left + 30;
    plot.top = rc->top + 8;
    plot.right = rc->right - 8;
    plot.bottom = rc->bottom - 8;
    if (w->nitems > 0) plot.right -= right_pad;
    if (plot.right - plot.left < 8 || plot.bottom - plot.top < 8) return;

    /* 网格：4 横 3 竖（浅色，别抢数据的视线） */
    grid = cgw_brush_for(0xE7E0EC);
    for (i = 1; i < 4; i++) {
        RECT r;
        r.left = plot.left; r.right = plot.right;
        r.top = plot.top + (plot.bottom - plot.top) * i / 4;
        r.bottom = r.top + 1;
        FillRect(dc, &r, grid);
    }
    for (i = 1; i < 3; i++) {
        RECT r;
        r.top = plot.top; r.bottom = plot.bottom;
        r.left = plot.left + (plot.right - plot.left) * i / 3;
        r.right = r.left + 1;
        FillRect(dc, &r, grid);
    }

    /* 各系列 */
    for (i = 0; i < n; i++) {
        chart_draw_series(dc, w, i, &plot, k_chart_colors[i % 8], bars);
    }

    /* 边框 */
    frame = CreatePen(PS_SOLID, 1, RGB(0xB4, 0xB0, 0xBB));
    oldpen = SelectObject(dc, frame);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, plot.left, plot.top, plot.right, plot.bottom);
    SelectObject(dc, oldpen);
    DeleteObject(frame);

    /* 范围文字（上边界/下边界）+ 图例 */
    {
        HFONT  f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 8, 0, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        wchar_t buf[32];
        RECT    t;
        int     oldmode = SetBkMode(dc, TRANSPARENT);

        SetTextColor(dc, RGB(0x49, 0x45, 0x4F));
        _snwprintf(buf, 31, L"%d", w->vmax);
        t.left = rc->left + 2; t.right = plot.left - 4; t.top = plot.top - 4; t.bottom = plot.top + 14;
        DrawTextW(dc, buf, -1, &t, DT_RIGHT | DT_SINGLELINE);
        _snwprintf(buf, 31, L"%d", w->vmin);
        t.top = plot.bottom - 14; t.bottom = plot.bottom + 4;
        DrawTextW(dc, buf, -1, &t, DT_RIGHT | DT_SINGLELINE);

        for (i = 0; i < n && w->nitems > 0; i++) {
            RECT lr;
            int  ly = plot.top + 2 + i * 16;
            lr.left = plot.right + 6; lr.right = plot.right + 14;
            lr.top = ly + 5;          lr.bottom = ly + 11;
            FillRect(dc, &lr, cgw_brush_for((unsigned)k_chart_colors[i % 8]));
            SetTextColor(dc, RGB(0x1D, 0x1B, 0x20));
            lr.left = plot.right + 18; lr.right = rc->right - 2;
            lr.top = ly; lr.bottom = ly + 16;
            {
                wchar_t *wn = cgw_wc_from_utf8(w->items[i] ? w->items[i] : "");
                if (wn) {
                    DrawTextW(dc, wn, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    free(wn);
                }
            }
        }
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }
}

/** @brief 图表窗口过程（自绘，纯显示；没绑任何事件） */
static LRESULT CALLBACK ChartProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);

    (void)wp;
    (void)lp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (w) {
            chart_paint(app, w, dc, &rc);
        } else {
            /* 控件还没挂上运行时的那一帧：填个系统面色就行，别去问"背后的颜色" */
            FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册图表的自绘窗口类（整进程一次） */
void cgw_register_chart_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChartProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_CHART_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}
