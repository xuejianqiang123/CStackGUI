/**
 * @file    cg_win_draw_panel.c
 * @brief   自绘窗口类：面板（group/box/card/shape）与分隔条（splitter）
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 面板窗口 */

/**
 * @brief 画整个组框（group / box）：内区底色 + 压在顶边线上的标题 + 边框
 * @param app 所属应用（DPI 缩放、字体、背后取色）
 * @param dc 客户区 DC
 * @param w 面板控件（标题文本 / 样式 / 底色）
 * @param rc 客户区矩形
 * @return void 无返回值
 * @note 三样都得自己画（**不能用系统边框样式**）：交给系统画边框时，系统重绘非客户区
 *       会把标题盖掉，看起来就是"边框线穿过文字"。标题的上下各占一半、中线压在顶边线上
 *       （原生组框就是这个观感），故障时用组框底色把那一小段线擦开
 */
static void panel_draw_group(cg_app_t *app, HDC dc, cg_widget_t *w, const RECT *rc)
{
    int inset = cgw_scale_px(app, 1);
    int has_title = (w->text && *w->text) ? 1 : 0;
    int top;
    unsigned bg = (w->style.bg != CG_COLOR_NONE) ? w->style.bg
                                                : cg_win_behind_rgb(app, w->native);
    /* 标题小块的底色 = **组自己的填充色**，对齐画布（styles.css 的 .ctl-group-title 是
     * `background: inherit` + `top:-8px`：标题骑在边框线上、底色与组体一致、无上下内边距）。
     * 2026-09-24 曾试过改成"透出背后"，实测与画布不一致，已回退。
     * 用户如想要"标题完全在框内、不骑线"的另一种风格，需要画布与运行时一起改。 */
    unsigned title_bg = bg;
    COLORREF penc = (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                   : RGB(0x79, 0x74, 0x7E);   /* 画布 --outline-strong */

    if (inset < 1) inset = 1;
    top = has_title ? cgw_scale_px(app, 8) : inset;   /* 顶边线：有标题就让出一条给标题 */

    /* 双缓冲下"不画 = 透出背后"不再成立：内存位图是未初始化像素，顶边线以上那条留白
     * 不填就是一截随机垃圾 —— 表现为标题右侧一条黑横带（2026-09-25 用户截图）。
     * 先整块清成背后色，内区底色再盖掉标题行以下的部分，留白条才真正透出页面底色。 */
    cgw_clear_to_behind(w, dc, rc);

    /* 内区底色 */
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
        HFONT f = cgw_font_for(app, w->style.font, w->style.size, w->style.bold, 0);
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
            FillRect(dc, &tr, cgw_brush_for(title_bg));   /* 用组自己的填充色擦开顶边线（title_bg = bg；组没设背景色时才等于"背后"色） */
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                            : GetSysColor(COLOR_WINDOWTEXT));
            if (wt) DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (wt) free(wt);
        SelectObject(dc, oldf);
    }
}

/** @brief 双缓冲回调参数 */
typedef struct { cg_app_t *app; cg_widget_t *w; } panel_ctx_t;

/** @brief group/box 一帧适配器：画进内存位图 */
/**
 * @brief group / box 一帧适配器：画进内存位图
 * @param mem 内存位图 DC（双缓冲的离屏 DC）
 * @param rc 离屏位图矩形（左上角为 0,0）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void panel_group_body(HDC mem, const RECT *rc, void *vctx)
{
    panel_ctx_t *c = (panel_ctx_t *)vctx;
    panel_draw_group(c->app, mem, c->w, rc);
}

/** @brief 画卡片/形状：先铺底（清四角），再圆角/椭圆/矩形描边，卡片带标题 */
/**
 * @brief 画卡片 / 形状：先铺底（清四角），再按格式描边，卡片带标题
 * @param app 所属应用（DPI 缩放、字体、背后取色）
 * @param dc 目标 DC
 * @param w card / shape 控件（格式、圆角、样式）
 * @param rc 客户区矩形
 * @return void 无返回值
 */
static void panel_draw_card_shape(cg_app_t *app, HDC dc, cg_widget_t *w, const RECT *rc)
{
    unsigned face = GetSysColor(COLOR_BTNFACE);
    unsigned facergb = ((face & 0xFFu) << 16) | (face & 0xFF00u) | ((face >> 16) & 0xFFu);

    /* 先整块清成"背后色"再画圆角：圆角弧外露出的四角才是页面底色，圆角才真正成立。
     * 原来直接把整个矩形填成卡片底色，四个圆角外糊着一圈直角色块（2026-09-25 用户报"卡片丑"）。 */
    cgw_clear_to_behind(w, dc, rc);

    {
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
            Ellipse(dc, rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1);
        } else if (cgw_is_t(w, "shape") && (!w->format || strcmp(w->format, "rect") != 0)) {
            /* 抗锯齿圆角：GDI 的 RoundRect 没有抗锯齿、圆角会"发毛"（用户报的是徽章/标签片，
             * 面板圆角是同一个毛病）。形状框没设背景色时**只有描边**，所以填充传 CG_COLOR_NONE。 */
            int r = cgw_scale_px(app, (w->style.radius > 0) ? w->style.radius : 8);
            RECT rr = { rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1 };
            cg_skin_fill_round_rect(dc, &rr, r,
                                    (w->style.bg != CG_COLOR_NONE) ? w->style.bg : CG_COLOR_NONE,
                                    (w->style.fg != CG_COLOR_NONE) ? w->style.fg : 0xB4B0BBu,
                                    (float)(inset ? inset : 1));
        } else if (cgw_is_t(w, "card")) {
            int r = cgw_scale_px(app, (w->style.radius > 0) ? w->style.radius : 12);
            RECT rr = { rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1 };
            cg_skin_fill_round_rect(dc, &rr, r,
                                    (w->style.bg != CG_COLOR_NONE) ? w->style.bg : facergb,
                                    (w->style.fg != CG_COLOR_NONE) ? w->style.fg : 0xB4B0BBu,
                                    (float)(inset ? inset : 1));
        } else {
            Rectangle(dc, rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1);
        }
        SelectObject(dc, oldbr);
        if (br) DeleteObject(br);
        SelectObject(dc, oldpen);
        DeleteObject(pen);

        /* 卡片顶部画标题 */
        if (cgw_is_t(w, "card") && w->text && *w->text) {
            HFONT f = cgw_font_for(app, w->style.font, w->style.size, w->style.bold, 0);
            HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
            RECT tr = *rc;
            tr.left += cgw_scale_px(app, 12);
            tr.top += cgw_scale_px(app, 6);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                            : GetSysColor(COLOR_WINDOWTEXT));
            wchar_t *wt = cgw_wc_from_utf8(w->text);
            if (wt) { DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_TOP); free(wt); }
            SelectObject(dc, oldf);
        }
    }
}

/** @brief card/shape 一帧适配器：画进内存位图 */
/**
 * @brief card / shape 一帧适配器：画进内存位图
 * @param mem 内存位图 DC（双缓冲的离屏 DC）
 * @param rc 离屏位图矩形（左上角为 0,0）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void panel_card_body(HDC mem, const RECT *rc, void *vctx)
{
    panel_ctx_t *c = (panel_ctx_t *)vctx;
    panel_draw_card_shape(c->app, mem, c->w, rc);
}

/** @brief 面板窗口过程（group/box/card/shape）：通知上浮主窗口 + card/shape 自绘 */
/**
 * @brief 面板窗口过程（group / box / card / shape）：通知上浮主窗口 + card / shape 自绘
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，转发给主窗口时原样传递）
 * @param lp 附加参数（通知消息时是通知头，其余消息原样转发）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
LRESULT CALLBACK cgw_PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        /* 同 cg_win_event.c 的 cgw_FocusProc：只转发"来自我们某个控件"的通知。
         * 无条件转发会劫走控件内部子窗口（ComboLBox / 内嵌 MonthCal / 内嵌 Up-Down）
         * 发给控件自己的通知，表现为下拉选了不生效、日历点了不落值、加减钮没反应。 */
        if (app && app->hwnd && (HWND)app->hwnd != hwnd) {
            HWND from = (msg == WM_NOTIFY && lp) ? ((const NMHDR *)lp)->hwndFrom : (HWND)lp;
            if (cgw_widget_by_hwnd(app, from)) {
                return SendMessageW((HWND)app->hwnd, msg, wp, lp);
            }
        }
        break;
    case WM_HSCROLL:
    case WM_VSCROLL:
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
        /* card / shape 画圆角/椭圆边框，group / box 连底色带标题一起画。
         * 这几种都分多笔直绘（铺底 / 边框 / 标题），窗口缩放这类高频重绘下会闪，
         * 故统一走双缓冲（cgw_paint_buffered）一次贴上屏。 */
        cg_widget_t *w = app ? cgw_widget_by_hwnd(app, hwnd) : NULL;
        if (w && (cgw_is_t(w, "group") || cgw_is_t(w, "box") || cgw_is_t(w, "card") || cgw_is_t(w, "shape"))) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            panel_ctx_t ctx;
            GetClientRect(hwnd, &rc);
            ctx.app = app; ctx.w = w;
            cgw_paint_buffered(dc, &rc,
                               (cgw_is_t(w, "group") || cgw_is_t(w, "box")) ? panel_group_body : panel_card_body,
                               &ctx);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册面板窗口类（能装子控件的自绘底板，整进程一次） */
/**
 * @brief 注册面板窗口类（能装子控件的自绘底板，整进程一次）
 * @return void 无返回值
 */
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
static int s_split_dragged;  /* 这次拖动真的动过分隔条吗（松手要不要报 release） */

/** @brief 分隔条一帧适配器：画进内存位图（清背后 + 线 + 握点） */
/**
 * @brief 分隔条一帧适配器：画进内存位图（清背后 + 线 + 握点）
 * @param mem 内存位图 DC（双缓冲的离屏 DC）
 * @param rc 离屏位图矩形（左上角为 0,0）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void splitter_body(HDC mem, const RECT *rc, void *vctx)
{
    panel_ctx_t *c = (panel_ctx_t *)vctx;
    cg_widget_t *w = c->w;

    /* 先把整块填成"背后"的颜色：分隔条是"没有自己底色"的自绘控件 */
    cgw_clear_to_behind(w, mem, rc);

    {
        int vert = cgw_splitter_is_vert(w);
        int cx = rc->left + (rc->right - rc->left) / 2;
        int cy = rc->top + (rc->bottom - rc->top) / 2;
        HBRUSH line = cgw_brush_for(0xCAC4D0);
        HBRUSH dot  = cgw_brush_for(0x79747E);
        RECT r = *rc;
        int i;
        if (vert) { r.left = cx; r.right = cx + 1; } else { r.top = cy; r.bottom = cy + 1; }
        FillRect(mem, &r, line);
        for (i = -1; i <= 1; i++) {
            RECT d;
            if (vert) {
                d.left = cx - 1; d.right = cx + 2;
                d.top = cy + i * 6 - 1; d.bottom = cy + i * 6 + 2;
            } else {
                d.top = cy - 1; d.bottom = cy + 2;
                d.left = cx + i * 6 - 1; d.right = cx + i * 6 + 2;
            }
            FillRect(mem, &d, dot);
        }
    }
}

/** @brief 分隔条窗口过程：拖动改两侧面板尺寸，松手派发一次 release */
/**
 * @brief 分隔条窗口过程：拖动改两侧面板尺寸，松手派发一次 release
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（光标消息时低字是命中码，其余消息未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
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
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (!w) {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        /* 双缓冲：拖动分隔条时每帧重画（线 + 握点），多笔直绘会闪 */
        { panel_ctx_t ctx = { app, w }; cgw_paint_buffered(dc, &rc, splitter_body, &ctx); }
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
        s_split_dragged = 0;
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
            int   now, moved, sc;
            GetCursorPos(&pt);
            now   = cgw_splitter_is_vert(w) ? (int)pt.x : (int)pt.y;
            /* GetCursorPos 给的是物理像素，cg_splitter_apply 吃的是逻辑像素
             * （和 .cgui / rect 同一口径）：高 DPI 下不折算会拖快 scale 倍 */
            sc    = (w->app && w->app->scale > 0) ? w->app->scale : 100;
            moved = cg_splitter_apply(w, (now - s_split_origin) * 100 / sc);
            /* 被最小尺寸夹住时原点不跟走，鼠标退回来还能继续拖 */
            if (moved) {
                s_split_origin += moved;
                s_split_dragged = 1;
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        /* 拖完通知一次：业务在这里把布局存进 INI 最合适。
         * 单击不动（没真的拖过）不算"拖完"，不发 —— 否则业务会把没变过的布局当
         * "用户拖过了"存下来。 */
        if (w && s_split_dragged) {
            s_split_dragged = 0;
            cg_dispatch(w, CG_EVENT_RELEASE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册分隔条的自绘窗口类（整进程一次） */
/**
 * @brief 注册分隔条的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
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
