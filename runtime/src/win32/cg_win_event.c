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

/**
 * @brief 鼠标消息的按键组合 -> 功能键状态（与窗口级同一口径）
 * @param wp 鼠标消息的附加参数（按键位）
 * @return int 1=Ctrl 2=Shift 4=Alt 的按位组合
 */
static int widget_mods_from_mouse(WPARAM wp)
{
    int m = 0;
    if (wp & MK_CONTROL) m |= 1;
    if (wp & MK_SHIFT) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/**
 * @brief 键盘消息的按键组合 -> 功能键状态
 * @return int 1=Ctrl 2=Shift 4=Alt 的按位组合
 */
static int widget_mods_from_key(void)
{
    int m = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= 1;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/**
 * @brief 评分 / 分页的点击判定：按横坐标落点换算成星数或翻页方向并派发变化事件
 * @param w 评分 / 分页控件
 * @param hwnd 控件窗口
 * @param x 点击横坐标（客户区）
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
 * @param hwnd 子类窗口句柄
 * @param msg 窗口消息号
 * @param wp 消息的附加参数
 * @param lp 消息的附加参数
 * @param id 子类 ID（未使用）
 * @param ref 子类引用数据（未使用）
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
        /* 只转发"来自我们某个控件"的通知 —— 与下面 WM_HSCROLL/WM_VSCROLL 的白名单同一个道理。
         * 有些控件自己内部还有子窗口（下拉框的 ComboLBox、日期控件的内嵌 MonthCal 与内嵌
         * Up-Down），那些子窗口的通知是发给**控件自己**的；无条件转发会把它们劫走：
         * 主窗口认不出这个陌生 HWND、没人处理，而我们从不用 DefSubclassProc 补回去
         * ⇒ 控件自己永远收不到。表现为下拉选了不生效 / 日历点了不落值 / 时间的加减钮没反应。 */
        if (g_win_app && g_win_app->hwnd && (HWND)g_win_app->hwnd != hwnd) {
            HWND from = (msg == WM_NOTIFY && lp) ? ((const NMHDR *)lp)->hwndFrom : (HWND)lp;
            if (cgw_widget_by_hwnd(g_win_app, from)) {
                return SendMessageW((HWND)g_win_app->hwnd, msg, wp, lp);
            }
        }
        break;
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
        /* 静态类控件自己不管底色：尤其分割线，系统的 SS_ETCHEDHORZ
         * 连 WM_CTLCOLORSTATIC 都不发，它那块像素还是创建时留下的（窗口底色），
         * 放进带底色的分组框就是一块窗口色。这里统一按规则擦一遍。
         * 标签例外：标签走下面 WM_PAINT 里的双缓冲整帧重画（底色 + 文字一次上屏），
         * 这里若再擦一遍就会多出一帧"只有底色没有字"的中间态 —— 拖滑块这类高频
         * 刷新下就是肉眼可见的文字闪烁（2026-09-25）。 */
        cg_widget_t *ew = cgw_widget_by_hwnd(g_win_app, hwnd);
        if (ew && cgw_is_t(ew, "label")) return 1;
        if (ew && (cgw_is_t(ew, "link") || cgw_is_t(ew, "separator"))) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, (ew->style.bg != CG_COLOR_NONE)
                                      ? cgw_brush_for(ew->style.bg)
                                      : cgw_brush_for(cg_win_behind_rgb(g_win_app, hwnd)));
            return 1;
        }
    }

    if (msg == WM_PAINT && g_win_app) {
        /* 标签：双缓冲整帧重画。先在内存位图里铺底色、再让原生 STATIC 用
         * WM_PRINTCLIENT 把文字画进同一张位图，最后一次 BitBlt 上屏 —— 擦除与
         * 绘制之间不再有"底色裸奔"的中间帧（颜色/对齐/省略号全跟系统渲染一致） */
        cg_widget_t *lw = cgw_widget_by_hwnd(g_win_app, hwnd);
        if (lw && cgw_is_t(lw, "label")) {
            PAINTSTRUCT ps;
            HDC wdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            {
                int wpx = rc.right - rc.left, hpx = rc.bottom - rc.top;
                HDC     mem = CreateCompatibleDC(wdc);
                HBITMAP bmp = CreateCompatibleBitmap(wdc, wpx, hpx);
                HGDIOBJ ob = SelectObject(mem, bmp);
                FillRect(mem, &rc, (lw->style.bg != CG_COLOR_NONE)
                                      ? cgw_brush_for(lw->style.bg)
                                      : cgw_brush_for(cg_win_behind_rgb(g_win_app, hwnd)));
                SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
                BitBlt(wdc, 0, 0, wpx, hpx, mem, 0, 0, SRCCOPY);
                SelectObject(mem, ob);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    if (msg == WM_PAINT) {
        /* 标签页的页体是系统按**它自己的**主题画的（浅色机上是 #F9F9F9）；重填页体矩形
         * （不含标签行）的两种情形：
         *   ① .cgui 给 tabs 设了「背景色」      -> 用那个色（既有行为）
         *   ② 深色模式且没设背景色               -> 用窗口底色，跟窗口/对话框连成一片
         * ② 是 2026-10-09 审计抓到的：深色皮肤下页体仍是系统的浅灰 —— 一块"发亮的浅灰"，
         * 正是深色史诗要消灭的东西；此前没人发现，因为审计页里从来没有 tabs。
         * **浅色且没设背景色时一像素不动**（深色史诗的硬约束：浅色是回归基线）。
         * 子控件是独立窗口、且标签页有 WS_CLIPCHILDREN，所以这一填不会盖到子控件上。 */
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        cg_widget_t *tw = g_win_app ? cgw_widget_by_hwnd(g_win_app, hwnd) : NULL;
        if (tw && cgw_is_t(tw, "tabs")) {
            HBRUSH br = NULL;
            if (tw->style.bg != CG_COLOR_NONE) {
                br = cgw_brush_for(tw->style.bg);
            } else if (g_win_app->dark) {
                cg_win_state_t *st = (cg_win_state_t *)g_win_app->natives;
                if (st && st->bg_brush) br = (HBRUSH)st->bg_brush;   /* 窗口底色刷，换主题时重建 */
            }
            if (br) {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    SendMessageW(hwnd, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
                    FillRect(dc, &rc, br);
                    ReleaseDC(hwnd, dc);
                }
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
            /* 四类自绘按钮不擦背景：WM_DRAWITEM 的皮肤画之前会整帧填"背后"色，
             * 系统擦除却走 WM_CTLCOLORBTN 的默认灰刷，与卡面/页面对不上 ——
             * 悬停进出、点击换态、sync 失效都会先闪一帧错色
             * （2026-09-25 用户报"鼠标移到卡片内开关就闪烁"） */
            if (msg == WM_ERASEBKGND &&
                (cgw_is_t(w, "button") || cgw_is_t(w, "checkbox") || cgw_is_t(w, "radio") || cgw_is_t(w, "switch")))
                return 1;

            /* 自绘按钮：悬停/按下只影响外观，不派发事件（点击仍走 BN_CLICKED） */
            if (cgw_is_t(w, "button") || cgw_is_t(w, "checkbox") || cgw_is_t(w, "radio") || cgw_is_t(w, "switch")) {
                /* 失效一律不带擦除：这四种皮肤画之前都会整帧填"背后"色，擦除帧纯属多余
                 * —— 而擦除走 WM_CTLCOLORBTN 的系统默认灰刷，和卡面/页面对不上，
                 * 悬停进出就是一帧错色闪一下（2026-09-25 用户报"鼠标移到卡片内开关就闪烁"） */
                if (msg == WM_LBUTTONDOWN) {
                    w->pressed = 1; InvalidateRect(hwnd, NULL, FALSE);
                } else if (msg == WM_LBUTTONUP) {
                    w->pressed = 0; InvalidateRect(hwnd, NULL, FALSE);
                } else if (msg == WM_MOUSEMOVE) {
                    if (!w->hot) {
                        w->hot = 1;
                        TRACKMOUSEEVENT tme;
                        memset(&tme, 0, sizeof(tme));
                        tme.cbSize = sizeof(tme);
                        tme.dwFlags = TME_LEAVE;
                        tme.hwndTrack = hwnd;
                        TrackMouseEvent(&tme);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (msg == WM_MOUSELEAVE) {
                    w->hot = 0; w->pressed = 0; InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/**
 * @brief 给控件挂统一子类（焦点事件 / 通知转发 / 评分分页按点判定）
 * @param h 要挂子类的控件窗口
 * @return void 无返回值
 */
void cgw_subclass_for_events(HWND h)
{
    SetWindowSubclass(h, cgw_FocusProc, 1, 0);
}

/* ------------------------------------------------------------ 悬停提示（tooltip）
 * 整窗一个 TOOLTIPS 控件；每个有 hint 的控件注册成一个 tool，
 * TTF_SUBCLASS 让 tooltip 自己接管鼠标转发，不需要任何额外消息处理。
 * 文本用 LPSTR_TEXTCALLBACK 现取，cg_widget_set_hint 改完立即生效。
 */
/**
 * @brief 把控件注册成工具提示的一个条目（文本按需回取）
 * @param tooltip 整窗那个工具提示控件窗口
 * @param forw 要注册的控件窗口
 * @return void 无返回值
 */
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

/**
 * @brief 悬停提示变了之后同步工具提示：按需新增 / 删除 / 更新条目
 * @param w 目标控件
 * @return void 无返回值
 */
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

/**
 * @brief 重设时钟控件的系统定时器（周期为 0 即停止）
 * @param w 目标控件
 * @return void 无返回值
 * @note 拿控件的命令 id 当系统定时器 id 复用
 */
void cg_win_restart_timer(cg_widget_t *w)
{
    if (!w || !w->app || !w->app->hwnd) return;
    HWND hwnd = (HWND)w->app->hwnd;
    KillTimer(hwnd, (UINT_PTR)w->ctrl_id);
    if (cgw_is_t(w, "timer") && w->period > 0) {
        SetTimer(hwnd, (UINT_PTR)w->ctrl_id, (UINT)w->period, NULL);
    }
}

/**
 * @brief 重设串口组件的到达轮询定时器（链路开着才转）
 * @param w 目标控件
 * @return void 无返回值
 * @note 50ms 与心跳同拍：到达事件的延迟上限就是这一拍，再快只是空转；定时器 id 也复用命令 id
 */
void cg_win_restart_serial(cg_widget_t *w)
{
    if (!w || !w->app || !w->app->hwnd) return;
    HWND hwnd = (HWND)w->app->hwnd;
    KillTimer(hwnd, (UINT_PTR)w->ctrl_id);
    if (cgw_is_t(w, "serial") && w->native2) {
        SetTimer(hwnd, (UINT_PTR)w->ctrl_id, 50, NULL);
    }
}
