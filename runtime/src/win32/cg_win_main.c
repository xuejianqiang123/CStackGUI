/**
 * @file    cg_win_main.c
 * @brief   主窗口过程：窗口消息、窗口级事件填充与托盘回调
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 窗口级事件支撑 */

/**
 * @brief 给"窗口事件宿主"派发一个窗口级事件
 * @param win 宿主控件：主窗口是 __window 伪控件，模态对话框是 dialog 节点自己
 * @param ev 事件（就绪 / 尺寸 / 激活 / 按键……）
 * @return void 无返回值
 * @note 和控件通知共用同一个防重入标志：回调里再触发窗口消息不会套娃
 */
void cgw_win_emit(cg_widget_t *win, cg_event_t ev)
{
    if (!win || g_win_in_notify) return;
    g_win_in_notify = 1;
    cg_dispatch(win, ev);
    g_win_in_notify = 0;
}

/**
 * @brief 窗口级事件的宿主：主窗口 = __window 伪控件，对话框 = 那个 dialog 节点
 * @param app 应用
 * @param hwnd 收到消息的窗口
 * @param win 消息入口已算出的宿主（主窗口 / **模态**对话框），没有则 NULL
 * @return cg_widget_t* 真正的宿主；找不着返回 NULL（= 不该派发）
 * @note **非模态**对话框不在 app->modal_dlg 里，得按 HWND 在树里反查（cgw_dialog_by_hwnd，
 *       全树 O(n)）—— 所以它只用在**窗口级事件**（用户拖窗口 / 改尺寸 / 切焦点这类低频消息），
 *       绝不进逐消息的路径（WM_PAINT、鼠标移动那种每来一次走一遍树不值当）。
 *       补这条之前：非模态对话框的 move/size/activate 一律不派发（T4 前半记着的 v1 边界）。
 */
static cg_widget_t *win_event_host(cg_app_t *app, HWND hwnd, cg_widget_t *win)
{
    if (win) return win;
    if (!app || !hwnd) return NULL;
    return cgw_dialog_by_hwnd(app, hwnd);
}

/**
 * @brief 鼠标消息的按键组合 -> 功能键状态
 * @param wp 鼠标消息的附加参数；低字含按键位
 * @return int 1=Ctrl 2=Shift 4=Alt 的按位组合；0 = 无修饰键
 */
static int mods_from_mouse(WPARAM wp)
{
    int m = 0;
    if (wp & MK_CONTROL) m |= 1;
    if (wp & MK_SHIFT) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/**
 * @brief 键盘消息的按键组合 -> 功能键状态
 * @return int 1=Ctrl 2=Shift 4=Alt 的按位组合；0 = 无修饰键
 */
static int mods_from_key(void)
{
    int m = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= 1;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

/**
 * @brief 把鼠标消息的客户区坐标与按键状态写进窗口宿主控件
 * @param win 宿主控件（主窗口的 __window 或对话框节点）
 * @param app 所属应用；取 DPI 缩放
 * @param wp 鼠标消息的附加参数；低字含按键位
 * @param lp 鼠标消息的附加参数；低 / 高字为物理像素坐标，内部折回逻辑像素
 * @return void 无返回值（无宿主时直接返回）
 */
static void fill_mouse(cg_widget_t *win, cg_app_t *app, WPARAM wp, LPARAM lp)
{
    cg_widget_t *w = win;
    int sc = (app && app->scale > 0) ? app->scale : 100;
    if (!w) return;
    /* 坐标对业务是**逻辑像素**（与 .cgui、控件坐标同一套）：消息给的是物理像素，
     * 这里按 DPI 折回 —— 否则业务在缩放屏上自己算布局会差 1.x 倍（2026-09-23 审计）。 */
    w->ev_x = (int)(short)LOWORD(lp) * 100 / sc;
    w->ev_y = (int)(short)HIWORD(lp) * 100 / sc;
    w->ev_keys = mods_from_mouse(wp);
}

/**
 * @brief 托盘图标回调：左键单击显示窗口、左键双击、右键弹默认菜单，并派发托盘事件
 * @param app 所属应用；提供托盘状态与窗口伪控件
 * @param hwnd 主窗口；用于显示 / 恢复与右键菜单
 * @param wp 消息的附加参数；本回调未使用
 * @param lp 托盘消息类型
 * @return void 无返回值
 * @note 业务绑了托盘事件就能自己接管动作；没绑也有「显示 / 退出」的默认行为，不会变成孤儿图标
 */
static void tray_callback(cg_app_t *app, HWND hwnd, WPARAM wp, LPARAM lp)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    cg_widget_t *w = app ? app->window : NULL;
    unsigned long now;
    int action = 0;

    (void)wp;
    if (!st) return;

    if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
        action = 1;
        if (lp == WM_LBUTTONDBLCLK) {
            action = 2;
        } else {
            /* 托盘的单/双击要自己判：两次单击间隔在双击时间内就升格为双击 */
            now = GetTickCount();
            if (st->tray_click_tick && now - st->tray_click_tick <= GetDoubleClickTime()) action = 2;
            st->tray_click_tick = now;
        }
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        else ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    } else if (lp == WM_RBUTTONUP) {
        action = 3;
        /* 右键：默认给一个「显示窗口 / 退出」菜单。
         * TrackPopupMenu 自己跑消息循环，弹之前必须放开防重入锁。 */
        HMENU m = CreatePopupMenu();
        if (m) {
            POINT pt;
            int cmd, keep = g_win_in_notify;
            AppendMenuW(m, MF_STRING, 1, L"显示窗口");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 2, L"退出");
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            g_win_in_notify = 0;
            cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      pt.x, pt.y, 0, hwnd, NULL);
            g_win_in_notify = keep;
            DestroyMenu(m);
            if (cmd == 1) {
                if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                else ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            } else if (cmd == 2) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
    } else {
        return;   /* 其它托盘消息（气泡被点等）不派发 */
    }

    if (w) w->ev_action = action;
    cgw_win_emit(app->window, CG_EVENT_TRAY);   /* 托盘是主窗口的资源，宿主恒为 __window */
}

/**
 * @brief 主窗口过程：命令 / 通知分派、自绘、配色、DPI、时钟、窗口事件、关闭流程
 * @param hwnd 主窗口句柄
 * @param msg 窗口消息号
 * @param wp 消息的附加参数；含义随 msg 变化
 * @param lp 消息的附加参数；含义随 msg 变化
 * @return LRESULT 已处理的消息返回 0 / 非 0 等；未处理的交给系统默认处理
 */
LRESULT CALLBACK cgw_MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    /* 本窗口的"窗口事件宿主"：主窗口是 __window 伪控件，模态对话框是 dialog 节点自己。
     * 两个顶层窗口共用这一个过程 —— 控件派发（cgw_notify_from / cg_widget_by_ctrl_id）
     * 按 HWND/ctrl_id 在 app 上查，本来就不看宿主；只有窗口级事件要认宿主。 */
    cg_widget_t *win = NULL;
    if (app) win = ((HWND)app->hwnd == hwnd) ? app->window : app->modal_dlg;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_COMMAND: {
        if (!app) break;
        /* lParam 为 0 = 这条命令来自窗口菜单（菜单栏）或**加速键**，不是控件。
         * 加速键与菜单项共用号段（都从 WM_COMMAND 回来），所以先问动作表是谁的：
         * 动作的快捷键命中就激活那条命令，否则按菜单项那条路走。 */
        if (!lp) {
            int cmd = (int)LOWORD(wp);
            if (!cg_action_invoke_cmd(app, cmd)) cg_dispatch_menu_cmd(app, cmd);
            return 0;
        }
        /* IsDialogMessage 把回车变成 IDOK、Esc 变成 IDCANCEL：
         * 默认当 SUBMIT 用；app.enterTab / app.escClose 可以改成"回车下移焦点 / Esc 关窗" */
        if (LOWORD(wp) == IDOK && app->doc.enter_tab) {
            SendMessageW(hwnd, WM_NEXTDLGCTL, 0, FALSE);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL && app->doc.esc_close) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            HWND focus = GetFocus();
            if (focus) {
                cg_widget_t *fw = cg_widget_by_ctrl_id(app, GetDlgCtrlID(focus));
                if (fw && !strcmp(fw->type, "edit")) {
                    cgw_pull_edit_text(fw, focus);
                    cg_dispatch(fw, CG_EVENT_SUBMIT);
                }
            }
            return 0;
        }
        cgw_notify_from(app, (HWND)lp, HIWORD(wp), (int)LOWORD(wp));
        return 0;
    }
    case WM_HSCROLL:
    case WM_VSCROLL: {
        if (!app) break;
        if (lp) {
            cg_widget_t *w = cgw_widget_by_hwnd(app, (HWND)lp);
            if (w && !strcmp(w->type, "slider")) {
                LRESULT v = SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
                /* 拖动结束才发 release：拖的过程中 change 会刷很多次，
                 * 需要"松手才存一次"的场景（联网保存、重算）就绑 release。 */
                cg_widget_set_value(w, (int)v);
                cg_dispatch(w, LOWORD(wp) == TB_ENDTRACK ? CG_EVENT_RELEASE : CG_EVENT_CHANGE);
                return 0;
            }
            if (w && cg_widget_is_numedit(w)) {
                /* 数值框的箭头滚动（键盘上下键之类）：位置是箭头自己的，取回来落进
                 * value 并刷文本，再报一次 change。走 cg_widget_set_value 是为了
                 * 顺手夹到 min/max 并把文本同步成新值。 */
                HWND up = (HWND)w->native2;
                if (up && !g_win_in_notify) {
                    DWORD pos = (DWORD)SendMessageW(up, UDM_GETPOS32, 0, 0);
                    g_win_in_notify = 1;
                    cg_widget_set_value(w, (int)pos);
                    cg_dispatch(w, CG_EVENT_CHANGE);
                    g_win_in_notify = 0;
                }
                return 0;
            }
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        if (!app) break;
        if (((const NMHDR *)lp)->code == TTN_GETDISPINFOW) {
            /* tooltip 文本现取：控件上挂的 hint 就是提示内容 */
            NMTTDISPINFOW *di = (NMTTDISPINFOW *)lp;
            cg_widget_t *tw = cgw_widget_by_hwnd(app, ((const NMHDR *)lp)->hwndFrom);
            if (tw && tw->hint && *tw->hint) {
                static wchar_t tip_buf[256];
                wchar_t *wt = cgw_wc_from_utf8(tw->hint);
                if (wt) {
                    _snwprintf(tip_buf, 256, L"%ls", wt);   /* 宽串必须 %ls（见 cg_win_ownerdraw.c 的 draw_rate 那条注释） */
                    tip_buf[255] = L'\0';
                    free(wt);
                } else {
                    tip_buf[0] = L'\0';
                }
                /* NMTTDISPINFOW 里 lpszText 指回 szText（80 字符）装不下的用外面缓冲 */
                di->lpszText = tip_buf;
            }
            return 0;
        }
        return cgw_notify_common(app, (const NMHDR *)lp);
    }
    case WM_DRAWITEM: {
        if (!app) break;
        cgw_draw_own_item(app, (const DRAWITEMSTRUCT *)lp);
        return TRUE;
    }
    case WM_SETCURSOR: {
        /* 链接要手型光标，否则用户不知道那能点 */
        if (app && LOWORD(lp) == HTCLIENT && wp) {
            cg_widget_t *cw = cgw_widget_by_hwnd(app, (HWND)wp);
            if (cw && cgw_is_t(cw, "link")) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        HBRUSH cbr = cgw_ctlcolor_brush(app, wp, lp);
        if (cbr) return (LRESULT)cbr;
        break;
    }
    case WM_ERASEBKGND: {
        if (app) {
            cg_win_state_t *st = (cg_win_state_t *)app->natives;
            if (st && st->bg_brush) {
                HDC dc = (HDC)wp;
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(dc, &rc, st->bg_brush);
                return 1;
            }
        }
        break;
    }
    case WM_TIMER:
        if (app && app->natives && wp == ((cg_win_state_t *)app->natives)->timer_id) {
            cg_timers_tick(app);
            return 0;
        }
        if (app) {
            /* 时钟控件：定时器 id 复用 ctrl_id */
            cg_widget_t *tw = cg_widget_by_ctrl_id(app, (int)wp);
            if (tw && cgw_is_t(tw, "timer") && tw->period > 0) {
                if (!g_win_in_notify) {
                    g_win_in_notify = 1;
                    cg_dispatch(tw, CG_EVENT_TICK);
                    g_win_in_notify = 0;
                }
                return 0;
            }
            /* 串口组件：到达轮询（id 同样复用 ctrl_id）。缓冲里有数据才派发 ——
             * 没绑 arrive 或没读完，下一拍接着来；回调里自己 cg_serial_read 取。 */
            if (tw && cgw_is_t(tw, "serial") && tw->native2) {
                if (!g_win_in_notify &&
                    cg_serial_available((const cg_serial_t *)tw->native2) > 0) {
                    g_win_in_notify = 1;
                    cg_dispatch(tw, CG_EVENT_ARRIVE);
                    g_win_in_notify = 0;
                }
                return 0;
            }
        }
        break;
    case WM_DROPFILES:
        /* hdrop 的所有权在系统：处理完必须 DragFinish，所以"读路径"和"释放"都收在这一处，
         * 不让 cgw_win_drop 去管句柄（它只管把路径变成字符串并派发事件）。 */
        if (app) {
            cgw_win_drop(app, (HDROP)wp);
            DragFinish((HDROP)wp);
        }
        return 0;
    case WM_THEMECHANGED:
        /* 用户在系统设置里换了浅/深色。**只有 auto 才该跟着变** —— 显式写了
         * dark:true/false 的工程照旧，不然用户设的配色被系统主题悄悄改掉。 */
        if (app && app->doc.dark == -1) {
            cgw_theme_init(app);
            cgw_theme_apply(app);
        }
        return 0;
    case WM_DPICHANGED: {
        if (app) {
            RECT *r = (RECT *)lp;
            int dpi = HIWORD(wp);
            cg_win_state_t *st = (cg_win_state_t *)app->natives;
            if (st) st->dpi = dpi;
            app->scale = (int)((long long)dpi * 100 / 96);
            /* 这次尺寸变化是"换显示器"引起的，不是用户拉的：丢掉布局基准重取，
             * 免得 grow 控件把显示器的缩放也吃一遍（见 cg_win_layout.c）。 */
            cgw_layout_reset(app);
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            for (cg_widget_t *w = app->widgets; w; w = w->next) {
                cgw_sync_widget(w);
                /* 与 scale 绑定的皮肤量不会自己跟着变：输入框的圆角 / 边框带 / 箭头宽度
                 * 都是建控件时按**旧** scale 算的（cg_skin_apply_edit 只被调用一次），
                 * 跨到不同 DPI 的显示器后在这里补一次。 */
                if (!strcmp(w->type, "edit")) cg_skin_edit_rescale(w);
            }
        }
        return 0;
    }
    /* ---- 窗口级事件：原生窗口消息派发给 __window（「启动窗口」事件表） ---- */
    case WM_MOVE:
        win = win_event_host(app, hwnd, win);
        if (win) {
            RECT r;
            GetWindowRect(hwnd, &r);
            win->ev_x = r.left;    /* 位置被改变：窗口在屏幕上的左上角 */
            win->ev_y = r.top;
        }
        cgw_win_emit(win, CG_EVENT_MOVE);
        break;
    case WM_SIZE:
        win = win_event_host(app, hwnd, win);
        if (win) {
            /* 尺寸被改变：新的客户区宽高 —— **逻辑像素**（与 .cgui 同一套单位）。
             * 这样业务在 size 回调里读到的 w×h 可以原样回喂 cg_win_set_size()，
             * 缩放屏上也不会差 1.x 倍。 */
            int sc = (app->scale > 0) ? app->scale : 100;
            win->ev_x = (int)(short)LOWORD(lp) * 100 / sc;
            win->ev_y = (int)(short)HIWORD(lp) * 100 / sc;
        }
        /* 先按 .cgui 的 grow 重排（改的是控件的逻辑坐标），**再**派发 size 事件 ——
         * 这样业务在 size 回调里读到的控件坐标已经是对的了。
         * 只对主窗口做：cgw_layout_apply 写死排 app->window 的孩子，对话框是固定框
         * （没有可拉的 WM_SIZE），不拦的话它的 WM_SIZE 会拿自己的尺寸把主窗口的
         * 孩子（尤其带 grow 的）重排歪。 */
        if (app && win == app->window) cgw_layout_apply(app, win->ev_x, win->ev_y);
        cgw_win_emit(win, CG_EVENT_SIZE);
        break;
    case WM_ACTIVATE: {
        int active = (LOWORD(wp) != WA_INACTIVE);
        win = win_event_host(app, hwnd, win);
        if (app && active) {
            cg_win_state_t *wst = (cg_win_state_t *)app->natives;
            if (wst && !wst->activated) {
                wst->activated = 1;
                cgw_win_emit(win, CG_EVENT_FIRSTACTIVATE);
            }
        }
        cgw_win_emit(win, active ? CG_EVENT_ACTIVATE : CG_EVENT_DEACTIVATE);
        break;
    }
    case WM_SHOWWINDOW:
        cgw_win_emit(win, wp ? CG_EVENT_SHOW : CG_EVENT_HIDE);
        break;
    case WM_SETFOCUS:
        cgw_win_emit(win, CG_EVENT_GOTFOCUS);
        break;
    case WM_KILLFOCUS: {
        /* 焦点只是挪到本窗口的某个控件上不算"窗口失去焦点"，
         * 否则 Tab 一下用户就收到一次失焦事件。 */
        HWND nf = (HWND)wp;
        if (nf && IsChild(hwnd, nf)) break;
        cgw_win_emit(win, CG_EVENT_LOSTFOCUS);
        break;
    }
    case WM_LBUTTONDOWN:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_MOUSEDOWN);
        break;
    case WM_LBUTTONUP:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_MOUSEUP);
        break;
    case WM_LBUTTONDBLCLK:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_DBLCLICK);
        break;
    case WM_RBUTTONDOWN:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_RDOWN);
        break;
    case WM_RBUTTONUP:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_RUP);
        break;
    case WM_MOUSEMOVE:
        fill_mouse(win, app, wp, lp);
        cgw_win_emit(win, CG_EVENT_MOUSEMOVE);
        break;
    case WM_MOUSEWHEEL:
        if (win) {
            /* 滚轮消息的坐标是屏幕坐标，先换成客户区；MK_ 位在 wParam 低字 */
            POINT pt;
            pt.x = (int)(short)LOWORD(lp);
            pt.y = (int)(short)HIWORD(lp);
            ScreenToClient(hwnd, &pt);
            win->ev_x = pt.x;
            win->ev_y = pt.y;
            win->ev_keys = mods_from_mouse(LOWORD(wp));
            win->ev_delta = (int)(short)HIWORD(wp);
        }
        cgw_win_emit(win, CG_EVENT_WHEEL);
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (win) {
            win->ev_key = (int)wp;
            win->ev_keys = mods_from_key();
        }
        cgw_win_emit(win, CG_EVENT_KEYDOWN);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (win) {
            win->ev_key = (int)wp;
            win->ev_keys = mods_from_key();
        }
        cgw_win_emit(win, CG_EVENT_KEYUP);
        break;
    case WM_CHAR:
        if (win) win->ev_key = (int)wp;
        cgw_win_emit(win, CG_EVENT_CHAR);
        break;
    case CG_TRAY_CALLBACK:
        tray_callback(app, hwnd, wp, lp);
        return 0;
    case WM_CLOSE: {
        /* 非模态对话框没有 modal_dlg 快路径：按 HWND 反查（低频消息，O(n) 可接受）。
         * 查到了才有 close/closequery 回调——查不到就是普通顶层窗口，直接销毁。 */
        if (!win) win = cgw_dialog_by_hwnd(app, hwnd);
        if (win) {
            app->close_cancel = 0;
            cg_dispatch(win, CG_EVENT_CLOSE);       /* 可否被关闭 */
            cg_dispatch(win, CG_EVENT_CLOSEQUERY);  /* 窗口可否被关闭（同一时机） */
            /* 回调里调了 cg_cancel_close() 就先别关（"还有没保存的东西"那种） */
            if (app->close_cancel && !app->quit) return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        if ((HWND)app->hwnd == hwnd) {
            /* 主窗口没了：进程退出。**只有这里才 PostQuitMessage** ——
             * 对话框也走这个过程，它销毁时只结束自己的模态泵（关对话框退出整个应用
             * 是多窗体之前的老行为，A5 要治的就是它） */
            cgw_win_emit(win, CG_EVENT_DESTROY);
            PostQuitMessage(0);
        } else {
            /* 对话框销毁：模态的有 modal_dlg 快路径；非模态按 HWND 反查（T4）。
             * 先把 destroy 派给宿主（业务还能在回调里读子控件收尾），再收子树的原生 ——
             * 顺序反了业务就拿不到值。modal_dlg 置 NULL 即模态泵的退出条件。 */
            if (!win) win = cgw_dialog_by_hwnd(app, hwnd);
            if (win) {
                cgw_win_emit(win, CG_EVENT_DESTROY);
                cgw_dialog_teardown(app, win);
                if (win == app->modal_dlg) app->modal_dlg = NULL;
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        /* **Alt 组合的加速键会以 WM_SYSCOMMAND 回来**（TranslateAccelerator 的规矩：
         * 带 ALT 的加速键走系统命令那条路）。所以这里也要问一次动作表 ——
         * 不问的话 "Alt+X" 配了永远不生效，而这是最容易归因到"快捷键坏了"的那类坑。
         * 不是我们发的号就原样交给系统（SC_CLOSE 这类系统命令都在其中）。 */
        if (app && cg_action_invoke_cmd(app, (int)wp)) return 0;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
