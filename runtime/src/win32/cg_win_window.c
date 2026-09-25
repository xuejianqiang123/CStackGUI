/**
 * @file    cg_win_window.c
 * @brief   窗口生命周期（初始化/消息循环/收尾）与窗口、托盘公共 API
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/**
 * @brief .cgui 的窗口属性 -> Win32 窗口样式
 * @param d 文档（取 border/controlBox/minimizeBox/maximizeBox/topmost/taskbar）
 * @param exstyle 出参：扩展样式
 * @return DWORD 窗口样式
 * @note border 不写按 "sizable"（可拉伸），和加属性之前的老行为一致
 */
static DWORD style_of_doc(const cg_doc_t *d, DWORD *exstyle)
{
    const char *bd = (d->border && *d->border) ? d->border : "sizable";
    DWORD style = WS_OVERLAPPED | WS_CAPTION;

    if (!strcmp(bd, "none")) {
        style = WS_POPUP;                 /* 无边框：自己画标题栏那种 */
    } else if (!strcmp(bd, "dialog")) {
        style |= WS_SYSMENU;              /* 固定对话框：只有关闭钮，不可拉伸 */
    } else {
        if (!strcmp(bd, "sizable")) style |= WS_THICKFRAME;
        style |= WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }

    /* 按钮开关只在有系统菜单时成立（没有 WS_SYSMENU 时这两个位会被系统忽略） */
    if (style & WS_SYSMENU) {
        if (!d->control_box) style &= ~WS_SYSMENU;
        if (!d->min_box) style &= ~WS_MINIMIZEBOX;
        if (!d->max_box) style &= ~WS_MAXIMIZEBOX;
    }

    *exstyle = 0;
    if (d->topmost) *exstyle |= WS_EX_TOPMOST;      /* 总在最前 */
    if (!d->taskbar) *exstyle |= WS_EX_TOOLWINDOW;  /* 不在任务栏出现（Alt-Tab 也不出现） */
    return style;
}

/* ------------------------------------------------------------ 后端入口 */

/** @brief 后端初始化：DPI、公共控件、类注册、主窗口、tooltip、定时器、按树序建控件 */
int cgw_win_init(cg_app_t *app)
{
    cg_win_state_t *st;
    WNDCLASSEXW wc;
    HWND hwnd;
    int  dpi = 96;
    RECT  rc;
    int   ww, wh;
    DWORD wstyle = 0, wexstyle = 0;
    int   wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    HICON wicon = NULL;
    LPCWSTR wclass = L"CStackGUIMain", wcursor;
    wchar_t *wclass_buf = NULL;
    wchar_t *wtitle;
    HINSTANCE inst = GetModuleHandleW(NULL);

    /* 让窗口在高 DPI 下清晰；老系统上调用失败也无所谓 */
    {
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32) {
            typedef BOOL (WINAPI *set_ctx_t)(void *);
            set_ctx_t set_ctx = (set_ctx_t)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (set_ctx) set_ctx((void *)-4);   /* PER_MONITOR_AWARE_V2 */
            else SetProcessDPIAware();
        }
    }

    cg_win_console_utf8();

    st = (cg_win_state_t *)calloc(1, sizeof(cg_win_state_t));
    if (!st) return -1;
    st->dpi = dpi;
    app->natives = st;
    app->scale = 100;
    g_win_app = app;    /* 焦点子类 / tooltip 反查用（单窗口应用，全局一份） */

    cgw_register_panel_class();
    {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES |
                    ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
                    ICC_UPDOWN_CLASS | ICC_DATE_CLASSES;
        InitCommonControlsEx(&icc);
    }

    /* 富文本不是系统必备组件：优先新版，退旧版，再退普通 EDIT */
    if (LoadLibraryW(L"Msftedit.dll")) {
        g_win_richedit_class = L"RICHEDIT50W";
    } else if (LoadLibraryW(L"Riched20.dll")) {
        g_win_richedit_class = L"RichEdit20W";
        fprintf(stderr, "[cstackgui] 没找到 Msftedit.dll，richtext 退回 RichEdit20W\n");
    } else {
        g_win_richedit_class = L"EDIT";
        fprintf(stderr, "[cstackgui] 没有可用的富文本控件，richtext 退回普通多行输入\n");
    }

    /* browser 的宿主类：atl.dll 各版本 Windows 都带；起不来只影响 browser */
    {
        HMODULE atl = LoadLibraryW(L"atl.dll");
        typedef BOOL (WINAPI *axinit_t)(void);
        axinit_t axinit = atl ? (axinit_t)(void *)GetProcAddress(atl, "AtlAxWinInit") : NULL;
        g_win_atl_ok = (axinit && axinit()) ? 1 : 0;
        if (!g_win_atl_ok) {
            fprintf(stderr, "[cstackgui] atl.dll 不可用，browser 控件会显示占位文本\n");
        }
    }

    /* GDI+ 只用来读图片；起不来也不影响其它控件，图片控件会退回占位框 */
    {
        cg_gdip_input_t in;
        memset(&in, 0, sizeof(in));
        in.GdiplusVersion = 1;
        if (GdiplusStartup(&g_win_gdip_token, &in, NULL) != 0) {
            g_win_gdip_token = 0;
            fprintf(stderr, "[cstackgui] GDI+ 启动失败，image 控件只会显示占位框\n");
        }
    }

    /* ---- 窗口属性（.cgui 的 app 节点 = 画布「属性」面板那一栏） ---- */
    wstyle = style_of_doc(&app->doc, &wexstyle);
    wcursor = cgw_cursor_id_of(app->doc.cursor);
    wicon = cgw_load_icon_file(app, app->doc.icon);   /* 找不到就是 NULL，退回系统默认图标 */
    st->icon = wicon;
    if (app->doc.class_name && *app->doc.class_name) {
        wclass_buf = cgw_wc_from_utf8(app->doc.class_name);
        if (wclass_buf) wclass = wclass_buf;
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cgw_MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, wcursor ? wcursor : IDC_ARROW);
    wc.hIcon = wicon ? wicon : LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = wclass;
    if (!RegisterClassExW(&wc)) {
        free(wclass_buf);
        return -1;
    }

    /* 客户区尺寸要跟控件一样过 DPI 缩放：.cgui 里写的是**逻辑像素**，控件全都过
     * cgw_scale_px，窗口本身原来漏了 —— 表现为 125%/150% 下窗口仍是 1× 大小、
     * 控件却按 1.x 摆放，**右侧与下侧整片被裁掉**（2026-09-23 高 DPI 审计发现）。
     * 建窗时还没有 HWND，先用 GetDpiForSystem()（DPI 不感知的老系统上它给 96，
     * 于是不缩放、由 Windows 自己位图放大，正好也不会双重缩放）。 */
    app->scale = 100;
    {
        HMODULE u32 = LoadLibraryW(L"user32.dll");
        typedef UINT (WINAPI *get_sysdpi_t)(void);
        get_sysdpi_t get_sysdpi = u32 ? (get_sysdpi_t)(void *)GetProcAddress(u32, "GetDpiForSystem") : NULL;
        if (get_sysdpi) {
            UINT d = get_sysdpi();
            if (d > 0) app->scale = (int)((long long)d * 100 / 96);
        }
        if (u32) FreeLibrary(u32);
    }

    rc.left = 0; rc.top = 0;
    rc.right = cgw_scale_px(app, app->doc.width);
    rc.bottom = cgw_scale_px(app, app->doc.height);
    AdjustWindowRectEx(&rc, wstyle, FALSE, wexstyle);
    ww = rc.right - rc.left;
    wh = rc.bottom - rc.top;

    if (app->doc.has_pos) {
        wx = app->doc.x;
        wy = app->doc.y;
    }
    if (app->doc.center) {
        /* 居中：按主显示器的工作区算（任务栏那块不算数） */
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            wx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
            wy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;
        }
    }

    wtitle = cgw_wc_from_utf8(app->doc.title);
    hwnd = CreateWindowExW(wexstyle, wclass, wtitle ? wtitle : L"CStackGUI",
                           wstyle, wx, wy, ww, wh,
                           NULL, NULL, inst, app);
    free(wtitle);
    free(wclass_buf);
    if (!hwnd) return -1;

    /* 任务栏/Alt-Tab 用的小图标也换成自己的 */
    if (wicon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)wicon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wicon);
    }

    app->hwnd = hwnd;

    /* 整窗圆角（M3 观感）。dwmapi 动态加载：老系统无此 API 时自动跳过，零链接依赖。 */
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *dwma_t)(HWND, DWORD, LPCVOID, DWORD);
            dwma_t set_attr = (dwma_t)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (set_attr) {
                int pref = 2;  /* DWMWCP_ROUND */
                set_attr(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(int));
            }
            FreeLibrary(dwm);
        }
    }

    /* DPI 缩放系数（百分比） */
    {
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        typedef UINT (WINAPI *get_dpi_t)(HWND);
        get_dpi_t get_dpi = user32 ? (get_dpi_t)(void *)GetProcAddress(user32, "GetDpiForWindow") : NULL;
        if (get_dpi) {
            UINT d = get_dpi(hwnd);
            if (d > 0) {
                int old = app->scale;

                st->dpi = (int)d;
                app->scale = (int)((long long)d * 100 / 96);
                /* 窗口建起来后才知道自己落在哪块显示器上：如果那边 DPI 与建窗时用的
                 * 不同（多显示器混 DPI），客户区尺寸要按新比例重算一次 ——
                 * 否则窗口尺寸和控件坐标又变成两套基准。 */
                if (app->scale != old) {
                    RECT r2 = { 0, 0, cgw_scale_px(app, app->doc.width), cgw_scale_px(app, app->doc.height) };

                    AdjustWindowRectEx(&r2, wstyle, FALSE, wexstyle);
                    SetWindowPos(hwnd, NULL, 0, 0, r2.right - r2.left, r2.bottom - r2.top,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }
    }

    /* 窗口背景 */
    st->bg_color = (app->doc.bg == CG_COLOR_NONE) ? GetSysColor(COLOR_BTNFACE) : cgw_to_colorref(app->doc.bg);
    st->bg_brush = CreateSolidBrush(st->bg_color);

    /* 整窗一个 tooltip：各控件按需把自己注册成 tool */
    st->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                  WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  hwnd, NULL, inst, NULL);
    if (st->tooltip) {
        SetWindowPos(st->tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    /* 定时器：统一一个 50ms 心跳，供 cg_set_timer 用 */
    st->timer_id = (UINT_PTR)SetTimer(hwnd, CG_TIMER_ID, 50, NULL);

    /* 菜单栏要在建子控件之前挂上：它会占掉客户区顶部，坐标才从正确的位置起算 */
    {
        cg_widget_t *mw = NULL;
        for (cg_widget_t *w = app->widgets; w; w = w->next) {
            if (cgw_is_t(w, "menu")) {
                if (mw) {
                    fprintf(stderr, "[cstackgui] 只支持一个 menu（窗口只能有一根菜单栏），\"%s\" 被忽略\n",
                            w->id ? w->id : "-");
                    continue;
                }
                mw = w;
            }
        }
        if (mw) {
            HMENU hm = cgw_menu_build(mw, 1);   /* 1 = 菜单栏（CreateMenu） */
            if (hm && mw->nmenu > 0) {
                SetMenu(hwnd, hm);
                DrawMenuBar(hwnd);
                /* 菜单栏占掉的高度要补回窗口大小，客户区才是 .cgui 里写的尺寸 */
                {
                    RECT cr, wr;
                    int fw, fh;
                    GetClientRect(hwnd, &cr);
                    GetWindowRect(hwnd, &wr);
                    fw = (wr.right - wr.left) - (cr.right - cr.left);
                    fh = (wr.bottom - wr.top) - (cr.bottom - cr.top);
                    SetWindowPos(hwnd, NULL, 0, 0,
                                 cgw_scale_px(app, app->doc.width) + fw,
                                 cgw_scale_px(app, app->doc.height) + fh,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            } else if (hm) {
                DestroyMenu(hm);
            }
        }
    }

    /* 按树序建原生控件：必须父先于子。
     * 注意不能遍历 app->widgets —— 那是按创建顺序倒插的链表，子控件会排在父前面，
     * 子控件的父 HWND 还是 NULL，CreateWindowEx 会失败（整个分组框里的控件全丢）。 */
    cgw_create_native_tree(app->window);

    /* 时钟控件按各自的周期起定时器（id 复用 ctrl_id） */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (cgw_is_t(w, "timer") && w->period > 0) {
            SetTimer(hwnd, (UINT_PTR)w->ctrl_id, (UINT)w->period, NULL);
        }
    }

    /* 初始状态：禁止（整窗禁用）/ 托盘图标 */
    if (!app->doc.enabled) EnableWindow(hwnd, FALSE);
    if (app->doc.tray) cg_tray_set(app, NULL, NULL);

    /* 创建完毕：控件都建好了、窗口还没显示出来之前派发，
     * 回调里可以调窗口/控件 API（也因此 首次激活 排在它后面）。 */
    cgw_win_emit(app, CG_EVENT_READY);

    ShowWindow(hwnd, app->doc.visible ? SW_SHOW : SW_HIDE);
    UpdateWindow(hwnd);
    return 0;
}

/** @brief 消息循环（IsDialogMessage 处理 Tab/回车导航），quit 置位即退出 */
static int win_run(cg_app_t *app)
{
    MSG msg;

    /* 用 PeekMessage + WaitMessage 而不是 GetMessage：
     * 队列空的时候正好是「空闲」事件的时机 —— 处理完就睡，不空转烧 CPU。 */
    for (;;) {
        if (app->quit) break;
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            if (!IsDialogMessageW((HWND)app->hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            cgw_win_emit(app, CG_EVENT_IDLE);
            WaitMessage();   /* 睡到有消息为止；下一个心跳靠 WM_TIMER 叫醒 */
        }
    }
    return 0;
}

/** @brief 后端清理：杀定时器、关窗口、停 GDI+、释放字体缓存 */
static void win_shutdown(cg_app_t *app)
{
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    if (!st) return;
    cg_tray_remove(app);   /* 托盘图标不主动摘，会一直留在通知区里 */
    if (st->timer_id) KillTimer((HWND)app->hwnd, CG_TIMER_ID);
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (cgw_is_t(w, "timer")) KillTimer((HWND)app->hwnd, (UINT_PTR)w->ctrl_id);
    }
    /* 串口组件：链路必须在这里收掉 —— 它没有原生窗口可销毁（cgw_destroy_native
     * 走不到 DestroyWindow 的路），app_close 随后 free 控件，链路再不关就泄漏了。 */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (cgw_is_t(w, "serial") && w->native2) {
            cg_serial_close((cg_serial_t *)w->native2);
            w->native2 = NULL;
        }
    }
    /* 窗口可能已在 WM_CLOSE 里 DestroyWindow 过（app->hwnd 仍是旧值、句柄已失效），
     * 直接再 DestroyWindow 会踩无效句柄。先 IsWindow 判定，销毁后置空避免二次。 */
    if (app->hwnd && IsWindow((HWND)app->hwnd)) DestroyWindow((HWND)app->hwnd);
    app->hwnd = NULL;
    if (st->icon) {
        DestroyIcon(st->icon);
        st->icon = NULL;
    }
    g_win_app = NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->bitmap) {
            DeleteObject((HGDIOBJ)w->bitmap);
            w->bitmap = NULL;
        }
    }
    if (g_win_gdip_token) {
        GdiplusShutdown(g_win_gdip_token);
        g_win_gdip_token = 0;
    }
    while (st->fonts) {
        cg_font_node_t *n = st->fonts->next;
        if (st->fonts->hfont) DeleteObject(st->fonts->hfont);
        free(st->fonts->key);
        free(st->fonts);
        st->fonts = n;
    }
    if (st->bg_brush) DeleteObject(st->bg_brush);
    free(st);
    app->natives = NULL;
}

/** @brief 改主窗口标题（UTF-8 -> UTF-16） */
static void win_set_title(cg_app_t *app, const char *title)
{
    wchar_t *wt = cgw_wc_from_utf8(title);
    if (wt && app->hwnd) SetWindowTextW((HWND)app->hwnd, wt);
    free(wt);
}

/* ============================================================ 窗口操作 / 托盘
 * .cgui 的 app 节点能一次性写好的窗口属性在这里也能运行时改。
 * 没有窗口（--selftest）时全部空转，业务代码不用自己判。
 */

/** @brief 主窗口句柄；没有窗口返回 NULL（窗口类 API 的统一守卫） */
static HWND win_hwnd_of(const cg_app_t *app)
{
    return (app && app->hwnd) ? (HWND)app->hwnd : NULL;
}

/** @brief 移动窗口到屏幕坐标 x,y */
void cg_win_set_pos(cg_app_t *app, int x, int y)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/** @brief 改窗口客户区尺寸（width/height 是逻辑像素，和 .cgui 里同一套单位） */
void cg_win_set_size(cg_app_t *app, int width, int height)
{
    HWND h = win_hwnd_of(app);
    RECT rc;
    DWORD style, ex;
    if (!h || width <= 0 || height <= 0) return;
    /* width/height 是**逻辑像素**（本函数注释与 cstackgui.h 都这么约定）：这里按 DPI 缩放。
     * 不缩放的话，150% 下业务调 cg_win_set_size(app, 760, 560) 只得到 760 物理像素、
     * 内容仍被裁 —— 与"窗口建窗时乘 scale"那条口径不统一（2026-09-23 高 DPI 审计）。 */
    width  = cgw_scale_px(app, width);
    height = cgw_scale_px(app, height);
    style = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    ex = (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE);
    rc.left = 0; rc.top = 0; rc.right = width; rc.bottom = height;
    /* 带上菜单标志：客户区尺寸才是调用者要的那个数，不含标题栏/边框/菜单栏 */
    AdjustWindowRectEx(&rc, style, GetMenu(h) != NULL, ex);
    SetWindowPos(h, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/** @brief 把窗口移到当前显示器工作区正中 */
void cg_win_center(cg_app_t *app)
{
    HWND h = win_hwnd_of(app);
    RECT wr;
    MONITORINFO mi;
    if (!h) return;
    GetWindowRect(h, &wr);
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
    SetWindowPos(h, NULL,
                 mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - (wr.right - wr.left)) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - (wr.bottom - wr.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/** @brief 显示（show!=0）或隐藏窗口 */
void cg_win_show(cg_app_t *app, int show)
{
    HWND h = win_hwnd_of(app);
    if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}

/** @brief 最小化 / 最大化 / 还原；cmd 传 "min" / "max" / "normal" */
void cg_win_set_state(cg_app_t *app, const char *cmd)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    if (cmd && !strcmp(cmd, "min"))         ShowWindow(h, SW_MINIMIZE);
    else if (cmd && !strcmp(cmd, "max"))    ShowWindow(h, SW_MAXIMIZE);
    else                                    ShowWindow(h, SW_RESTORE);
}

/** @brief 总在最前（on!=0）或取消置顶 */
void cg_win_set_topmost(cg_app_t *app, int on)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/** @brief 换窗口图标（.ico 路径按 cgw_resolve_asset 找）；传 NULL 还原系统默认图标 */
void cg_win_set_icon(cg_app_t *app, const char *ico_path)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    HWND h;
    HICON ic = NULL, use;

    if (!app) return;
    h = win_hwnd_of(app);
    if (ico_path && *ico_path) ic = cgw_load_icon_file(app, ico_path);

    if (st) {
        if (st->icon) DestroyIcon(st->icon);   /* 上一张是我们自己 LoadImage 的，要还 */
        st->icon = ic;
    }
    if (h) {
        use = ic ? ic : LoadIconW(NULL, IDI_APPLICATION);
        SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)use);
        SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)use);
        SetClassLongPtrW(h, GCLP_HICON, (LONG_PTR)use);
    }
}

/** @brief 建/改托盘图标：ico_path 传 NULL 用窗口图标，tip 传 NULL 用窗口标题 */
void cg_tray_set(cg_app_t *app, const char *ico_path, const char *tip)
{
    cg_win_state_t *st;
    HWND h;
    NOTIFYICONDATAW nid;
    HICON ic = NULL;
    int owned = 0;
    wchar_t *wt;

    if (!app) return;
    st = (cg_win_state_t *)app->natives;
    h = win_hwnd_of(app);
    if (!st || !h) return;

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = h;
    nid.uID = CG_TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = CG_TRAY_CALLBACK;   /* cgw_MainProc 里按这个号收托盘消息 */

    if (ico_path && *ico_path) {
        ic = cgw_load_icon_file(app, ico_path);
        owned = 1;
    }
    if (!ic) {
        /* 没指定就用窗口图标，一路退到系统默认 */
        ic = (HICON)SendMessageW(h, WM_GETICON, ICON_SMALL, 0);
        if (!ic) ic = (HICON)SendMessageW(h, WM_GETICON, ICON_BIG, 0);
        if (!ic) ic = (HICON)GetClassLongPtrW(h, GCLP_HICONSM);
        if (!ic) ic = LoadIconW(NULL, IDI_APPLICATION);
    }
    nid.hIcon = ic;

    wt = cgw_wc_from_utf8((tip && *tip) ? tip : app->doc.title);
    if (wt) {
        _snwprintf(nid.szTip, 128, L"%ls", wt);
        nid.szTip[127] = L'\0';
        free(wt);
    }

    Shell_NotifyIconW(st->tray_on ? NIM_MODIFY : NIM_ADD, &nid);
    st->tray_on = 1;
    if (owned && ic) DestroyIcon(ic);   /* 外壳已拷走图标，我们这张可以放了 */
}

/** @brief 摘掉托盘图标 */
void cg_tray_remove(cg_app_t *app)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    NOTIFYICONDATAW nid;
    if (!st || !st->tray_on || !app->hwnd) return;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = (HWND)app->hwnd;
    nid.uID = CG_TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    st->tray_on = 0;
}

/** @brief 气泡提示（托盘图标已有时才有效） */
void cg_tray_balloon(cg_app_t *app, const char *tip_title, const char *text)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    NOTIFYICONDATAW nid;
    wchar_t *wt = NULL, *wb = NULL;

    if (!st || !st->tray_on || !app->hwnd) return;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = (HWND)app->hwnd;
    nid.uID = CG_TRAY_ID;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;

    wt = cgw_wc_from_utf8(tip_title);
    if (wt) { _snwprintf(nid.szInfoTitle, 64, L"%ls", wt); nid.szInfoTitle[63] = L'\0'; free(wt); }
    wb = cgw_wc_from_utf8(text);
    if (wb) { _snwprintf(nid.szInfo, 256, L"%ls", wb); nid.szInfo[255] = L'\0'; free(wb); }

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

const cg_backend_t cg_backend_win32 = {
    "win32",
    cgw_win_init,
    win_run,
    win_shutdown,
    cgw_sync_widget,
    cgw_create_native,
    cgw_destroy_native,
    win_set_title,
};
