/**
 * @file    cg_win_draw_panel.c
 * @brief   自绘窗口类：面板（group/box/card/shape）与分隔条（splitter）
 */
#include "cg_win_internal.h"

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
