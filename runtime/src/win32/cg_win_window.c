/**
 * @file    cg_win_window.c
 * @brief   窗口生命周期（初始化/消息循环/收尾）与窗口、托盘公共 API
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/**
 * @brief 边框名 -> Win32 窗口样式（.cgui 的 app.border 与运行时 cg_win_set_border 共用一条路）
 * @param d 文档（取 controlBox/minimizeBox/maximizeBox/topmost/taskbar）
 * @param bd 边框名："none" / "dialog" / "fixed" / "sizable"；NULL / 空按 "sizable"
 * @param exstyle 出参：扩展样式
 * @return DWORD 窗口样式
 * @note border 不写按 "sizable"（可拉伸），和加属性之前的老行为一致
 */
static DWORD style_of_border(const cg_doc_t *d, const char *bd, DWORD *exstyle)
{
    DWORD style = WS_OVERLAPPED | WS_CAPTION;

    if (!bd || !*bd) bd = "sizable";

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

    /* 主窗口必须带 WS_CLIPCHILDREN：否则父窗口 WM_ERASEBKGND 擦背景时会把子控件那块
     * 也填成窗口底色（见 cgw_MainProc 的 FillRect），而子控件多为自绘/静态、每帧先被底色
     * 盖一下再重画 —— 表现为"拖滑块时其它控件闪烁"。有这个位后，父窗口擦背景会跳过子
     * 窗口矩形，子控件各自的 WM_ERASEBKGND 接管，不再闪。 */
    style |= WS_CLIPCHILDREN;

    *exstyle = 0;
    if (d->topmost) *exstyle |= WS_EX_TOPMOST;      /* 总在最前 */
    if (!d->taskbar) *exstyle |= WS_EX_TOOLWINDOW;  /* 不在任务栏出现（Alt-Tab 也不出现） */
    return style;
}

/**
 * @brief .cgui 的窗口属性 -> Win32 窗口样式
 * @param d 文档（取 border/controlBox/minimizeBox/maximizeBox/topmost/taskbar）
 * @param exstyle 出参：扩展样式
 * @return DWORD 窗口样式
 * @note border 不写按 "sizable"（可拉伸），和加属性之前的老行为一致
 */
static DWORD style_of_doc(const cg_doc_t *d, DWORD *exstyle)
{
    return style_of_border(d, (d->border && *d->border) ? d->border : NULL, exstyle);
}

/* ------------------------------------------------------------ 加速键（动作的快捷键） */

/**
 * @brief 动作的「键 + 修饰键」映射成 Win32 的虚拟键码与 fVirt 标志
 * @param k 动作的键（core 解析出来的、与平台无关的形式）
 * @param fVirt 输出：FVIRTKEY | FCONTROL/FSHIFT/FALT
 * @param vk 输出：虚拟键码
 * @return int 1 = 认识这个键；0 = 不认识（跳过，不建这条加速键）
 */
static int accel_to_win(const cg_accel_t *k, BYTE *fVirt, WORD *vk)
{
    WORD v = 0;
    int  key;

    if (!k) return 0;
    key = k->key;

    if (key >= CG_ACCEL_A && key < CG_ACCEL_A + 26) {
        v = (WORD)('A' + (key - CG_ACCEL_A));
    } else if (key >= CG_ACCEL_0 && key < CG_ACCEL_0 + 10) {
        v = (WORD)('0' + (key - CG_ACCEL_0));
    } else if (key >= CG_ACCEL_F1 && key < CG_ACCEL_F1 + 24) {
        v = (WORD)(VK_F1 + (key - CG_ACCEL_F1));
    } else {
        switch (key) {
        case CG_ACCEL_ESC:      v = VK_ESCAPE; break;
        case CG_ACCEL_ENTER:    v = VK_RETURN; break;
        case CG_ACCEL_TAB:      v = VK_TAB;    break;
        case CG_ACCEL_SPACE:    v = VK_SPACE;  break;
        case CG_ACCEL_BACK:     v = VK_BACK;   break;
        case CG_ACCEL_DELETE:   v = VK_DELETE; break;
        case CG_ACCEL_INSERT:   v = VK_INSERT; break;
        case CG_ACCEL_HOME:     v = VK_HOME;   break;
        case CG_ACCEL_END:      v = VK_END;    break;
        case CG_ACCEL_PAGEUP:   v = VK_PRIOR;  break;
        case CG_ACCEL_PAGEDOWN: v = VK_NEXT;   break;
        case CG_ACCEL_LEFT:     v = VK_LEFT;   break;
        case CG_ACCEL_RIGHT:    v = VK_RIGHT;  break;
        case CG_ACCEL_UP:       v = VK_UP;     break;
        case CG_ACCEL_DOWN:     v = VK_DOWN;   break;
        default: return 0;
        }
    }
    /* FVIRTKEY 必须设：不设的话 key 会被当成字符码，字母/数字键在非英文键盘布局下对不上 */
    *fVirt = (BYTE)(FVIRTKEY
                    | ((k->mods & CG_MOD_CTRL)  ? FCONTROL : 0)
                    | ((k->mods & CG_MOD_SHIFT) ? FSHIFT   : 0)
                    | ((k->mods & CG_MOD_ALT)   ? FALT     : 0));
    *vk = v;
    return 1;
}

/**
 * @brief 按动作表建加速键表（没有带快捷键的动作时是空操作）
 * @param app 应用对象（动作表取自应用，句柄存进后端状态）
 * @return void 无返回值
 */
void cgw_accel_build(cg_app_t *app)
{
    cg_win_state_t *st;
    ACCEL *arr;
    int    n = 0;

    if (!app || !(st = (cg_win_state_t *)app->natives) || app->nactions <= 0) return;
    arr = (ACCEL *)calloc((size_t)app->nactions, sizeof(ACCEL));
    if (!arr) return;

    for (int i = 0; i < app->nactions; i++) {
        const cg_action_row_t *a = &app->actions[i];
        BYTE fVirt;
        WORD vk;

        if (!a->accel_ok || a->accel_cmd == 0) continue;
        if (!accel_to_win(&a->key, &fVirt, &vk)) continue;
        arr[n].fVirt = fVirt;
        arr[n].key   = vk;
        arr[n].cmd   = (WORD)a->accel_cmd;
        n++;
    }
    if (n > 0) st->accel = CreateAcceleratorTableW(arr, n);
    free(arr);
    if (n > 0 && !st->accel) {
        fprintf(stderr, "[cstackgui] 加速键表没建起来，动作的快捷键不会生效\n");
    }
}

/**
 * @brief 销毁加速键表（幂等、NULL 安全）
 * @param app 应用对象（无窗口或无表时为空操作）
 * @return void 无返回值
 */
void cgw_accel_destroy(cg_app_t *app)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    if (st && st->accel) {
        DestroyAcceleratorTable(st->accel);
        st->accel = NULL;
    }
}

/* ------------------------------------------------------------ 全局热键（RegisterHotKey）
 * 与加速键表（应用内 accel）分开的两件事：加速键在消息循环里由 TranslateAccelerator
 * 吃，**窗口失焦就不生效**；全局热键是系统级的，程序在托盘里也按得到。
 * 热键表放**文件私有静态**：一个进程一个应用，id -> 动作名这张表只有本文件需要知道
 * （主窗口过程收到 WM_HOTKEY 只回过来报个 id，见 cgw_hotkey_fire）。
 */

/** @brief 热键表槽位：注册 id -> 动作名 */
typedef struct { unsigned id; char *action; } cgw_hotkey_slot_t;
/** 热键表槽位数（截图工具这类程序一两条够用，给四条余量） */
#define CGW_HOTKEY_MAX 4
/** 热键 id 基址：与菜单 / 加速键的命令 id 空间错开，免得日志里看着像撞号 */
#define CGW_HOTKEY_ID_BASE 0x4300

static cgw_hotkey_slot_t s_hotkeys[CGW_HOTKEY_MAX];
static int               s_nhotkey;

/**
 * @brief 注册全局热键：按下组合键 = 触发同名动作（公共 API cg_hotkey_set 的实现）
 * @param app 应用
 * @param combo 组合键（与动作 accel 同一写法）；NULL = 解绑全部
 * @param action 动作名
 * @return int 1 成功 / 0 失败（原因写日志）
 * @note RegisterHotKey 失败的头号原因是**键已被别的程序占用** —— 那不是 bug，
 *       业务必须留兜底入口（面板按钮 / 托盘），所以这里只报日志、不弹框
 */
int cg_hotkey_set(cg_app_t *app, const char *combo, const char *action)
{
    cg_accel_t k;
    BYTE       fVirt;
    WORD       vk;
    DWORD      mods = 0;
    HWND       hwnd;
    int        i;

    if (!combo) { cgw_hotkey_clear(app); return 1; }   /* NULL = 全部解绑 */
    if (!app || !app->hwnd || !action || !*action) return 0;
    if (cg_action_index(app, action) < 0) {
        cg_log(CG_LOG_WARN, "全局热键 %s 注册失败：没有叫 \"%s\" 的动作", combo, action);
        return 0;
    }
    if (!cg_accel_parse(combo, &k) || !accel_to_win(&k, &fVirt, &vk)) {
        cg_log(CG_LOG_WARN, "全局热键注册失败：组合键 \"%s\" 看不懂", combo);
        return 0;
    }
    /* accel_to_win 给的是 TranslateAccelerator 的标志位，映射成 RegisterHotKey 的 MOD_* */
    if (fVirt & FCONTROL) mods |= MOD_CONTROL;
    if (fVirt & FSHIFT)   mods |= MOD_SHIFT;
    if (fVirt & FALT)     mods |= MOD_ALT;

    hwnd = (HWND)app->hwnd;
    /* 同一动作重复注册：先解绑旧的那条再挂新的，表里不会越积越多 */
    for (i = 0; i < s_nhotkey; i++) {
        if (strcmp(s_hotkeys[i].action, action) != 0) continue;
        UnregisterHotKey(hwnd, (int)s_hotkeys[i].id);
        free(s_hotkeys[i].action);
        s_hotkeys[i] = s_hotkeys[--s_nhotkey];
        break;
    }
    if (s_nhotkey >= CGW_HOTKEY_MAX) {
        cg_log(CG_LOG_WARN, "全局热键 %s 注册失败：已挂满 %d 条", combo, CGW_HOTKEY_MAX);
        return 0;
    }
    if (!RegisterHotKey(hwnd, (int)(CGW_HOTKEY_ID_BASE + s_nhotkey), mods, vk)) {
        cg_log(CG_LOG_WARN, "全局热键 %s 注册失败：组合键可能已被别的程序占用", combo);
        return 0;
    }
    s_hotkeys[s_nhotkey].id     = CGW_HOTKEY_ID_BASE + s_nhotkey;
    s_hotkeys[s_nhotkey].action = cg_str_dup(action);
    if (!s_hotkeys[s_nhotkey].action) {
        UnregisterHotKey(hwnd, (int)(CGW_HOTKEY_ID_BASE + s_nhotkey));
        return 0;
    }
    s_nhotkey++;
    cg_log(CG_LOG_INFO, "全局热键已注册：%s -> 动作 %s", combo, action);
    return 1;
}

/**
 * @brief 把 WM_HOTKEY 带回的 id 换算成动作并触发（主窗口过程调）
 * @param app 应用
 * @param id 注册时分配的热键 id
 * @return void 无返回值（不认识的 id 静默忽略）
 */
void cgw_hotkey_fire(cg_app_t *app, unsigned id)
{
    if (!app) return;
    for (int i = 0; i < s_nhotkey; i++) {
        if (s_hotkeys[i].id != id) continue;
        cg_action_invoke(app, s_hotkeys[i].action);
        return;
    }
}

/**
 * @brief 解绑全部全局热键并释放表（关窗收尾时调）
 * @param app 应用
 * @return void 无返回值（幂等、NULL 安全；没窗口时只释放内存）
 * @note 少这一步：进程退出前系统会替我们解绑，但表里的动作名就漏了 —— 按"谁申请谁释放"
 *       收在这里，和加速键表 cgw_accel_destroy 成对
 */
void cgw_hotkey_clear(cg_app_t *app)
{
    HWND hwnd = (app && app->hwnd) ? (HWND)app->hwnd : NULL;

    for (int i = 0; i < s_nhotkey; i++) {
        if (hwnd) UnregisterHotKey(hwnd, (int)s_hotkeys[i].id);
        free(s_hotkeys[i].action);
        s_hotkeys[i].action = NULL;
    }
    s_nhotkey = 0;
}

/* ------------------------------------------------------------ 后端入口 */

/**
 * @brief 后端初始化：DPI、公共控件、类注册、主窗口、菜单/加速键、tooltip、定时器、按树序建控件
 * @param app 应用对象（读文档建窗，句柄与状态写回应用）
 * @return int 0 = 成功；-1 = 失败（状态分配或建窗失败）
 */
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

    /* 窗口背景。深色在**这里**定下（app.dark 的 "auto" 也在这一步解析成实际值），
     * 之后所有"背后是什么颜色"的询问都会跟着变 —— 见 cg_win_theme.c 文件头。 */
    cgw_theme_init(app);
    st->bg_color = cgw_theme_window_bg(app);
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
                /* 菜单栏占掉的高度要补回窗口大小，客户区才是 .cgui 里写的尺寸。
                 * 运行时重建菜单栏（cg_win_menu_rebuild）走的是同一个函数 ——
                 * 同一规则只有一份实现，重建时忘了它客户区就会少一条菜单栏的高度。 */
                cgw_menu_fit_client(app);
            } else if (hm) {
                DestroyMenu(hm);
            }
        }
    }

    /* 加速键表：动作的快捷键靠它生效（消息循环里 TranslateAccelerator 用）。
     * 建在这里是因为它必须在消息循环之前就绪，且不依赖任何原生控件。 */
    cgw_accel_build(app);

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

    /* 接受"把文件拖进来"：走老的 WM_DROPFILES（cg_win_drop.c 的文件头解释了为什么不上
     * OLE 拖放）。放在这里而不是建窗口之前 —— 那时 app->hwnd 还没写进去。
     * 没在 .cgui 里绑 drop 事件时也没什么副作用：拖进来只有一条 INFO 日志。 */
    DragAcceptFiles((HWND)app->hwnd, TRUE);

    /* 创建完毕：控件都建好了、窗口还没显示出来之前派发，
     * 回调里可以调窗口/控件 API（也因此 首次激活 排在它后面）。 */
    cgw_win_emit(app->window, CG_EVENT_READY);

    ShowWindow(hwnd, app->doc.visible ? SW_SHOW : SW_HIDE);
    UpdateWindow(hwnd);
    return 0;
}

/**
 * @brief 消息循环（IsDialogMessage 处理 Tab/回车导航），quit 置位即退出
 * @param app 应用对象
 * @return int 固定返回 0（退出原因由应用的 quit 表达）
 */
static int win_run(cg_app_t *app)
{
    MSG msg;
    cg_win_state_t *st = (cg_win_state_t *)app->natives;

    /* 用 PeekMessage + WaitMessage 而不是 GetMessage：
     * 队列空的时候正好是「空闲」事件的时机 —— 处理完就睡，不空转烧 CPU。 */
    for (;;) {
        if (app->quit) break;
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            /* 加速键（动作的快捷键）**必须排在 IsDialogMessage 之前**：
             * 它要处理的正是那些会被焦点控件吃掉、或被 Tab/回车导航截走的按键。
             * 它在**消息循环**这一层，与焦点在谁身上无关 —— 所以焦点在输入框里
             * Ctrl+S 照样生效（窗口级 keydown 事件做不到这点：EDIT 会吃掉按键）。 */
            if (st && st->accel && TranslateAcceleratorW((HWND)app->hwnd, st->accel, &msg)) {
                continue;
            }
            if (!IsDialogMessageW((HWND)app->hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            cgw_win_emit(app->window, CG_EVENT_IDLE);
            WaitMessage();   /* 睡到有消息为止；下一个心跳靠 WM_TIMER 叫醒 */
        }
    }
    return 0;
}

/**
 * @brief 后端清理：杀定时器、关窗口、停 GDI+、释放字体缓存
 * @param app 应用对象（后端状态为空时直接返回）
 * @return void 无返回值
 * @note 除上述四项，还要按序收掉托盘图标、加速键表、串口链路、窗口图标，以及各控件的
 *       位图 / imagelist / 背景刷 —— 关窗路径不走控件销毁流程，这些句柄只能在这里
 *       统一释放，否则每次关窗都会漏
 */
static void win_shutdown(cg_app_t *app)
{
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    if (!st) return;
    cg_tray_remove(app);   /* 托盘图标不主动摘，会一直留在通知区里 */
    cgw_accel_destroy(app);   /* 加速键表不是窗口对象，关窗不会替我们收 */
    cgw_hotkey_clear(app);    /* 全局热键表同理：解绑 + 释放动作名 */
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
        cgw_iconlist_free_bmps(w);              /* 图标列表的行图标位图（关窗路径不走 destroy） */
        if (w->imglist) {                 /* 表格/列表的图标列表：关窗路径不走 cgw_destroy_native
             * （cgw_destroy_native 只在单个控件动态删除时调），必须在这里收，
             * 否则每次关窗漏一个 HIMAGELIST */
            ImageList_Destroy((HIMAGELIST)w->imglist);
            w->imglist = NULL;
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

/**
 * @brief 改主窗口标题（UTF-8 -> UTF-16）
 * @param app 应用对象
 * @param title UTF-8 标题文本
 * @return void 无返回值
 */
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

/**
 * @brief 主窗口句柄；没有窗口返回 NULL（窗口类 API 的统一守卫）
 * @param app 应用对象（可为 NULL）
 * @return HWND 主窗口句柄；无窗口时为 NULL（借用，不转移所有权）
 */
static HWND win_hwnd_of(const cg_app_t *app)
{
    return (app && app->hwnd) ? (HWND)app->hwnd : NULL;
}

/**
 * @brief 移动窗口到屏幕坐标 x,y
 * @param app 应用对象
 * @param x 屏幕 X（物理像素）
 * @param y 屏幕 Y（物理像素）
 * @return void 无返回值
 */
void cg_win_set_pos(cg_app_t *app, int x, int y)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/**
 * @brief 改窗口客户区尺寸（width/height 是逻辑像素，和 .cgui 里同一套单位）
 * @param app 应用对象
 * @param width 客户区宽（逻辑像素）
 * @param height 客户区高（逻辑像素）
 * @return void 无返回值
 * @note 这里按 DPI 缩放。不缩放的话，150% 下业务调 cg_win_set_size(app, 760, 560) 只得到
 *       760 物理像素、内容仍被裁 —— 与"窗口建窗时乘 scale"那条口径不统一（2026-09-23 高 DPI 审计）
 */
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

/**
 * @brief 把窗口移到当前显示器工作区正中
 * @param app 应用对象
 * @return void 无返回值
 */
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

/**
 * @brief 显示或隐藏窗口
 * @param app 应用对象
 * @param show 非 0 显示、0 隐藏
 * @return void 无返回值
 */
void cg_win_show(cg_app_t *app, int show)
{
    HWND h = win_hwnd_of(app);
    if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}

/**
 * @brief 最小化 / 最大化 / 还原
 * @param app 应用对象
 * @param cmd 状态字符串，传 "min" / "max" / "normal"，非 min/max 时一律还原
 * @return void 无返回值
 */
void cg_win_set_state(cg_app_t *app, const char *cmd)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    if (cmd && !strcmp(cmd, "min"))         ShowWindow(h, SW_MINIMIZE);
    else if (cmd && !strcmp(cmd, "max"))    ShowWindow(h, SW_MAXIMIZE);
    else                                    ShowWindow(h, SW_RESTORE);
}

/**
 * @brief 总在最前或取消置顶
 * @param app 应用对象
 * @param on 非 0 置顶、0 取消置顶
 * @return void 无返回值
 */
void cg_win_set_topmost(cg_app_t *app, int on)
{
    HWND h = win_hwnd_of(app);
    if (!h) return;
    SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/**
 * @brief 逻辑像素换算成物理像素（公共 API，实现就是 cgw_scale_px 那一层）
 * @param app 应用；NULL 按不缩放处理
 * @param logical 逻辑像素值（可正可负）
 * @return int 物理像素值（四舍五入）
 * @note 只为 `cg_win_set_pos`（按物理像素收参）这类接口换算；尺寸类接口收逻辑像素，别多此一举
 */
int cg_scale_px(const cg_app_t *app, int logical)
{
    return app ? cgw_scale_px(app, logical) : logical;
}

/**
 * @brief 运行时改主窗口边框（公共 API，样式拼装与建窗时共用 style_of_border）
 * @param app 应用
 * @param mode "sizable" / "fixed" / "dialog" / "none"
 * @return int 1 成功；0 失败（没有窗口 / mode 认不出来）
 * @note **只换 GWL_STYLE**：扩展样式（TOPMOST / TOOLWINDOW）一个位都不碰 —— 置顶与
 *       任务栏显隐有各自的接口（cg_win_set_topmost / .cgui 的 taskbar），混在这里改会让
 *       "exstyle 里的 TOPMOST 位"与真实 z 序对不上。改样式必须带 SWP_FRAMECHANGED，
 *       否则非客户区还按旧样式画（表现为无边框了标题栏还赖着不走）
 */
int cg_win_set_border(cg_app_t *app, const char *mode)
{
    HWND  h = win_hwnd_of(app);
    DWORD style, ex;

    if (!h || !mode || !*mode) return 0;
    if (strcmp(mode, "sizable") && strcmp(mode, "fixed") &&
        strcmp(mode, "dialog")  && strcmp(mode, "none")) {
        cg_log(CG_LOG_WARN, "改边框失败：mode \"%s\" 认不出来（sizable/fixed/dialog/none）", mode);
        return 0;
    }
    style = style_of_border(&app->doc, mode, &ex);
    (void)ex;                       /* 扩展样式按上面的注释：不落盘、不覆盖 */
    SetWindowLongPtrW(h, GWL_STYLE, (LONG_PTR)style);
    SetWindowPos(h, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return 1;
}

/**
 * @brief 换窗口图标（.ico 路径按资源查找规则解析）
 * @param app 应用对象
 * @param ico_path .ico 路径；传 NULL 或空串还原系统默认图标，并释放上一张自建图标
 * @return void 无返回值
 */
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

/**
 * @brief 建 / 改托盘图标
 * @param app 应用对象
 * @param ico_path 托盘图标 .ico 路径；NULL 或空串时退用窗口图标、再退系统默认
 * @param tip 悬停提示；NULL 或空串时用窗口标题
 * @return void 无返回值
 */
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

/**
 * @brief 摘掉托盘图标
 * @param app 应用对象（没有托盘图标时为空操作）
 * @return void 无返回值
 */
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

/**
 * @brief 气泡提示（托盘图标已有时才有效）
 * @param app 应用对象
 * @param tip_title 气泡标题（UTF-8，可空）
 * @param text 气泡正文（UTF-8，可空）
 * @return void 无返回值
 */
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
