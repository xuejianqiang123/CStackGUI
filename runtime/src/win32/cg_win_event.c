/**
 * @file    cg_win_event.c
 * @brief   焦点子类、窗口级事件、悬停提示与时钟联动
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* 防重入开关：定义提前到这里，因为下面几个辅助函数要用到它。
 * 含义见「事件映射」那一节。 */
int g_win_in_notify = 0;

/* ------------------------------------------------------------ 全局焦点子类
 * 标准控件不会主动把焦点变化告诉父窗口（EDIT 有 EN_SETFOCUS，但按钮/滑块/树
 * 什么都没有）。给每个控件套一层 SetWindowSubclass，WM_SETFOCUS / WM_KILLFOCUS
 * 在这里统一转成 gotfocus / lostfocus 事件。
 * 顺手干两件"必须知道鼠标坐标"的活：评分点第几颗星、分页点左半边还是右半边。
 */
cg_app_t *g_win_app = NULL;

/** @brief 鼠标消息的按键组合 -> 1=Ctrl 2=Shift 4=Alt（与 cg_win_main.c 的窗口级口径一致） */
static int widget_mods_from_mouse(WPARAM wp)
{
    int m = 0;
    if (wp & MK_CONTROL) m |= 1;
    if (wp & MK_SHIFT) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/** @brief 键盘消息的按键组合 -> 1=Ctrl 2=Shift 4=Alt */
static int widget_mods_from_key(void)
{
    int m = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= 1;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/**
 * @brief 评分/分页的点击判定：按 x 落点换算成星数或翻页方向并派发 change
 * @param w rate / pagination 控件；hwnd 控件窗口；x 点击横坐标（客户区）
 * @return void 无返回值
 */
static void rate_or_pag_click(cg_widget_t *w, HWND hwnd, int x)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    if (width <= 0) return;

    if (cgw_is_t(w, "rate")) {
        int stars = w->vmax - w->vmin;
        if (stars < 1) stars = 1;
        int v = w->vmin + (x * stars + width / 2) / width;   /* 落在第几颗星 */
        if (v < w->vmin) v = w->vmin;
        if (v > w->vmax) v = w->vmax;
        if (v != w->value) {
            g_win_in_notify = 1;
            cg_widget_set_value(w, v);
            cg_dispatch(w, CG_EVENT_CHANGE);
            g_win_in_notify = 0;
        }
        return;
    }
    /* pagination：左 1/3 上一页，右 1/3 下一页，中间是页码显示 */
    int v = w->value;
    if (x < width / 3)      v = w->value - 1;
    else if (x > width * 2 / 3) v = w->value + 1;
    else return;
    if (v < w->vmin || v > w->vmax) return;   /* 越界的一下当没点着 */
    g_win_in_notify = 1;
    cg_widget_set_value(w, v);
    cg_dispatch(w, CG_EVENT_CHANGE);
    g_win_in_notify = 0;
}

/**
 * @brief 全控件统一子类：焦点变化转事件、通知消息上浮主窗口、评分分页按点判定
 * @return LRESULT 交回原窗口过程
 */
LRESULT CALLBACK cgw_FocusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;

    /* 子控件的通知永远先到直接父窗口。放进 tabs / 其它系统控件的容器里后，
     * 父窗口不再是主窗口，这些通知必须转发上去，否则 ownerdraw 按钮
     * （colorpicker/rate/badge/...）画不出来、点击也没反应。
     * cgw_PanelProc 对 group/box 做的是同一件事；这里覆盖剩下所有控件。 */
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        if (g_win_app && g_win_app->hwnd && (HWND)g_win_app->hwnd != hwnd) {
            return SendMessageW((HWND)g_win_app->hwnd, msg, wp, lp);
        }
        break;
    case WM_HSCROLL:
    case WM_VSCROLL:
        /* 只转发滑块 / 数值框（带调节器的 edit）的滚动通知（它们要送回主窗口换算成值）。
         * listbox / tree / table 的滚动条消息是发给控件自己、由控件自己处理的，
         * 无条件转发会把这些消息劫走——表现为滚动条拖了没反应。 */
        if (g_win_app && g_win_app->hwnd && (HWND)g_win_app->hwnd != hwnd) {
            cg_widget_t *sw = cgw_widget_by_hwnd(g_win_app, (HWND)lp);
            if (sw && (cgw_is_t(sw, "slider") || cg_widget_is_numedit(sw))) {
                return SendMessageW((HWND)g_win_app->hwnd, msg, wp, lp);
            }
        }
        break;
    default:
        break;
    }

    if (msg == WM_ERASEBKGND && g_win_app) {
        /* 静态类控件（标签 / 链接 / 分割线）自己不管底色：尤其分割线，系统的 SS_ETCHEDHORZ
         * 连 WM_CTLCOLORSTATIC 都不发，它那块像素还是创建时留下的（窗口底色），
         * 放进带底色的分组框就是一块窗口色。这里统一按规则擦一遍。 */
        cg_widget_t *ew = cgw_widget_by_hwnd(g_win_app, hwnd);
        if (ew && (cgw_is_t(ew, "label") || cgw_is_t(ew, "link") || cgw_is_t(ew, "separator"))) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, (ew->style.bg != CG_COLOR_NONE)
                                      ? cgw_brush_for(ew->style.bg)
                                      : cgw_brush_for(cg_win_behind_rgb(g_win_app, hwnd)));
            return 1;
        }
    }

    if (msg == WM_PAINT) {
        /* 标签页的页体是系统按主题画的；设了「背景色」就把页体矩形（不含标签行）重填一遍。
         * 子控件是独立窗口、且标签页有 WS_CLIPCHILDREN，所以这一填不会盖到子控件上。 */
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        cg_widget_t *tw = g_win_app ? cgw_widget_by_hwnd(g_win_app, hwnd) : NULL;
        if (tw && cgw_is_t(tw, "tabs") && tw->style.bg != CG_COLOR_NONE) {
            HDC dc = GetDC(hwnd);
            if (dc) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SendMessageW(hwnd, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
                FillRect(dc, &rc, cgw_brush_for(tw->style.bg));
                ReleaseDC(hwnd, dc);
            }
        }
        return r;
    }

    if (!g_win_in_notify && g_win_app) {
        cg_widget_t *w = cgw_widget_by_hwnd(g_win_app, hwnd);
        if (w) {
            if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
                cg_event_t ev = (msg == WM_SETFOCUS) ? CG_EVENT_GOTFOCUS : CG_EVENT_LOSTFOCUS;
                if (cg_kind_supports_event(w->type, ev)) {
                    g_win_in_notify = 1;
                    cg_dispatch(w, ev);
                    g_win_in_notify = 0;
                }
            }
            if (msg == WM_LBUTTONUP &&
                (cgw_is_t(w, "rate") || cgw_is_t(w, "pagination"))) {
                rate_or_pag_click(w, hwnd, (short)LOWORD(lp));
            }
            /* 输入框没有原生 click 通知（EDIT 不发 BN_CLICKED，也没有 WM_LBUTTONDBLCLK），
             * 但 `edit` 的类型事件表声明了 click/dblclick —— 在这里把左键抬起补成一次 click，
             * 双击再由 cg_dispatch 按系统双击间隔合成（core/cg_event.c）。
             * 只给 edit 补：其它类型各有原生通知或合成来源，补了会重复派发。 */
            if (msg == WM_LBUTTONUP && cgw_is_t(w, "edit")) {
                g_win_in_notify = 1;
                cg_dispatch(w, CG_EVENT_CLICK);
                g_win_in_notify = 0;
            }
            /* 控件级鼠标/键盘/滚轮事件：k_kinds 声明了才派发（目前按钮全套支持）。
             * 坐标/键码等参数与窗口级事件同一套 cg_event_x/y/keys/key/delta 读取，
             * 存在"派发目标控件"上（窗口事件存在窗口伪控件上，口径一致）。 */
            cg_event_t wev;
            int have_wev = 1;
            switch (msg) {
            case WM_LBUTTONDOWN:  wev = CG_EVENT_MOUSEDOWN; break;
            case WM_LBUTTONUP:    wev = CG_EVENT_MOUSEUP;   break;
            case WM_RBUTTONDOWN:  wev = CG_EVENT_RDOWN;     break;
            case WM_RBUTTONUP:    wev = CG_EVENT_RUP;       break;
            case WM_MOUSEMOVE:    wev = CG_EVENT_MOUSEMOVE; break;
            case WM_MOUSEWHEEL:   wev = CG_EVENT_WHEEL;     break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:   wev = CG_EVENT_KEYDOWN;   break;
            case WM_KEYUP:
            case WM_SYSKEYUP:     wev = CG_EVENT_KEYUP;     break;
            case WM_CHAR:         wev = CG_EVENT_CHAR;      break;
            default:              have_wev = 0;             break;
            }
            if (have_wev && cg_kind_supports_event(w->type, wev)) {
                /* 数值/日期输入方式：非法字符直接吞掉 —— 它压根没进输入框，
                 * 所以也不派发 char 事件（报了反而像"输入成功了"） */
                if (wev == CG_EVENT_CHAR && cgw_edit_char_reject(w, hwnd, (wchar_t)wp))
                    return 0;

                /* 控件级事件的坐标也是**逻辑像素**（与窗口级事件、.cgui 同一套口径）：
                 * 消息给的是物理像素，这里按 DPI 折回，业务拿它算控件内位置才准。 */
                int sc = (w->app && w->app->scale > 0) ? w->app->scale : 100;

                if (wev == CG_EVENT_WHEEL) {
                    /* 滚轮消息的坐标是屏幕坐标，先换成控件客户区；MK_ 位在 wParam 低字 */
                    POINT pt;
                    pt.x = (int)(short)LOWORD(lp);
                    pt.y = (int)(short)HIWORD(lp);
                    ScreenToClient(hwnd, &pt);
                    w->ev_x = pt.x * 100 / sc;
                    w->ev_y = pt.y * 100 / sc;
                    w->ev_keys = widget_mods_from_mouse(LOWORD(wp));
                    w->ev_delta = (int)(short)HIWORD(wp);
                } else if (wev == CG_EVENT_KEYDOWN || wev == CG_EVENT_KEYUP || wev == CG_EVENT_CHAR) {
                    w->ev_key = (int)wp;
                    w->ev_keys = widget_mods_from_key();
                } else {
                    w->ev_x = (int)(short)LOWORD(lp) * 100 / sc;
                    w->ev_y = (int)(short)HIWORD(lp) * 100 / sc;
                    w->ev_keys = widget_mods_from_mouse(LOWORD(wp));
                }
                g_win_in_notify = 1;
                cg_dispatch(w, wev);
                g_win_in_notify = 0;
            }
            /* 自绘按钮：悬停/按下只影响外观，不派发事件（点击仍走 BN_CLICKED） */
            if (cgw_is_t(w, "button") || cgw_is_t(w, "checkbox") || cgw_is_t(w, "radio") || cgw_is_t(w, "switch")) {
                if (msg == WM_LBUTTONDOWN) {
                    w->pressed = 1; InvalidateRect(hwnd, NULL, TRUE);
                } else if (msg == WM_LBUTTONUP) {
                    w->pressed = 0; InvalidateRect(hwnd, NULL, TRUE);
                } else if (msg == WM_MOUSEMOVE) {
                    if (!w->hot) {
                        w->hot = 1;
                        TRACKMOUSEEVENT tme;
                        memset(&tme, 0, sizeof(tme));
                        tme.cbSize = sizeof(tme);
                        tme.dwFlags = TME_LEAVE;
                        tme.hwndTrack = hwnd;
                        TrackMouseEvent(&tme);
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                } else if (msg == WM_MOUSELEAVE) {
                    w->hot = 0; w->pressed = 0; InvalidateRect(hwnd, NULL, TRUE);
                }
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/** @brief 给控件挂统一子类（焦点事件 / 通知转发 / 评分分页按点判定） */
void cgw_subclass_for_events(HWND h)
{
    SetWindowSubclass(h, cgw_FocusProc, 1, 0);
}

/* ------------------------------------------------------------ 悬停提示（tooltip）
 * 整窗一个 TOOLTIPS 控件；每个有 hint 的控件注册成一个 tool，
 * TTF_SUBCLASS 让 tooltip 自己接管鼠标转发，不需要任何额外消息处理。
 * 文本用 LPSTR_TEXTCALLBACK 现取，cg_widget_set_hint 改完立即生效。
 */
/** @brief 把控件注册成 tooltip 的一个 tool（文本走 LPSTR_TEXTCALLBACK 现取） */
void cgw_tooltip_add_for(HWND tooltip, HWND forw)
{
    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = forw;
    ti.uId = (UINT_PTR)forw;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

/** @brief hint 变化后同步 tooltip：按需新增 / 删除 / 更新 tool */
void cg_win_set_hint(cg_widget_t *w)
{
    if (!w || !w->native || !g_win_app) return;
    cg_win_state_t *st = (cg_win_state_t *)g_win_app->natives;
    if (!st || !st->tooltip) return;

    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = (HWND)w->native;
    ti.uId = (UINT_PTR)w->native;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    BOOL exists = (BOOL)SendMessageW(st->tooltip, TTM_GETTOOLINFO, 0, (LPARAM)&ti);
    if (w->hint && *w->hint && !exists) {
        cgw_tooltip_add_for(st->tooltip, (HWND)w->native);
    } else if ((!w->hint || !*w->hint) && exists) {
        SendMessageW(st->tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    } else if (exists) {
        SendMessageW(st->tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
}

/** @brief 重设时钟控件的 Win32 定时器（period=0 即停止；id 复用 ctrl_id） */
void cg_win_restart_timer(cg_widget_t *w)
{
    if (!w || !w->app || !w->app->hwnd) return;
    HWND hwnd = (HWND)w->app->hwnd;
    KillTimer(hwnd, (UINT_PTR)w->ctrl_id);
    if (cgw_is_t(w, "timer") && w->period > 0) {
        SetTimer(hwnd, (UINT_PTR)w->ctrl_id, (UINT)w->period, NULL);
    }
}
