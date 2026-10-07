/**
 * @file    cg_win_theme.c
 * @brief   深色模式：窗口/自绘底色、无底色控件的字色、系统列表控件的深色主题
 * @details 两条刻意的限制，写在这里免得以后有人当成"漏了"：
 *
 *          **一、不动链接命令。** `SetWindowTheme` 在 uxtheme 里，而本工程的链接表里没有
 *          `-luxtheme`（2026-09-27 用探针实测：直接链会 `undefined reference to
 *          __imp_SetWindowTheme`）。加它就意味着**既有用户项目全部要改链接参数**，
 *          所以这里走 `LoadLibraryW` + `GetProcAddress` —— 与 `cg_sys_version()` 取 ntdll 的
 *          `RtlGetVersion` 同一个做法。uxtheme.dll 从 XP 起就是系统组件；取不到时只是
 *          "列表/表格/树不跟着变深"，其余照常，不致命。
 *
 *          **二、只改默认值。** 控件自己写了 `fg`/`bg`（内联或命名样式）就一律以它为准 ——
 *          所以一份**为浅色写死配色**的 .cgui 在深色下会呈现"深底 + 用户自己那套浅色配色"的
 *          混合观感。这是设计选择不是缺陷：深色提供的是**默认色**，配色终究归 .cgui。
 *          见 docs/CGUI-FORMAT.md 的 `app.dark` 一节。
 *
 *          落点在哪：本工程"背景色"只有一个出口 —— `cg_win_behind_rgb()`（自绘控件清底、
 *          无底色控件的 WM_CTLCOLOR 都问它）⇒ 深色只需要换掉它的兜底色与窗口底色，
 *          自绘控件与无底色控件就整片跟着变了，不必逐个重画。
 */
#include "cg_win_internal.h"

/* 深色调色板（M3 深色面的近似值：面 #141218、面上一层 #1D1B20、on-surface #E6E0E9） */
#define CG_DARK_SURFACE    0x141218u
#define CG_DARK_SURFACE_2  0x1D1B20u
#define CG_DARK_ON_SURFACE 0xE6E0E9u

/** SetWindowTheme 的函数型；取不到就是 NULL（见文件头"不动链接命令"） */
typedef HRESULT (WINAPI *set_window_theme_fn)(HWND, LPCWSTR, LPCWSTR);

/**
 * @brief 取"设置窗口主题"那个系统函数（进程内一次）
 * @return set_window_theme_fn 取到的函数指针；取不到时为 NULL
 * @note 动态取用是为了不新增链接依赖
 */
static set_window_theme_fn theme_fn(void)
{
    static set_window_theme_fn fn   = NULL;
    static int                 tried = 0;

    if (!tried) {
        HMODULE m;

        tried = 1;
        m = LoadLibraryW(L"uxtheme.dll");
        if (m) fn = (set_window_theme_fn)(void *)GetProcAddress(m, "SetWindowTheme");
    }
    return fn;
}

/* ------------------------------------------------------------ 清单控件的深色开关
 *
 * 光调 SetWindowTheme(h, L"DarkMode_Explorer") 还不够：ListBox / ListView / TreeView
 * 还要**进程先声明"允许深色"**。系统自带的深色支持就靠 uxtheme 的这几个导出，
 * 而它们**只有序号、没有名字**（所以这里用 MAKEINTRESOURCEA 按序号取）：
 *   135 = SetPreferredAppMode(int)      —— 0 默认 / 1 允许深色 / 2 强制深色 / 3 强制浅色
 *   133 = AllowDarkModeForWindow(HWND, BOOL)
 *   104 = RefreshImmersiveColorPolicyState(void)
 *
 * 未公开意味着**将来某天可能变**：取不到就退回浅色，其它功能一切照旧。
 *
 * **实测结论（2026-09-27，Win11 10.0.26100）：这套在清单控件上没生效** ——
 * listbox / 表格 / 树在探针里始终是白底，把进程级开关提前到建控件之前也一样。
 * 保留这段代码是因为"DarkMode_Explorer + 这两个序号"在 Win10 1809–21H2 上是社区通行的做法，
 * 但**我们没有 Win10 机器，没验过** ⇒ 文档里这三类控件按"**不跟随**"写（见 CGUI-FORMAT 的
 * 覆盖表），不要按"跟随"写。哪天在 Win10 上验到生效，把它挪进"跟随"那张表即可。
 */
typedef int  (WINAPI *preferred_app_mode_fn)(int);
typedef BOOL (WINAPI *allow_dark_fn)(HWND, BOOL);
typedef void (WINAPI *refresh_policy_fn)(void);

static preferred_app_mode_fn g_pref_mode;
static allow_dark_fn         g_allow_dark;
static refresh_policy_fn     g_refresh_policy;

/**
 * @brief 取那三个未公开导出，并把进程切到"允许深色"（只做一次）
 * @return void 无返回值
 */
static void dark_opt_in(void)
{
    static int done = 0;
    HMODULE    m;

    if (done) return;
    done = 1;
    m = LoadLibraryW(L"uxtheme.dll");
    if (!m) return;
    g_pref_mode      = (preferred_app_mode_fn)(void *)GetProcAddress(m, MAKEINTRESOURCEA(135));
    g_allow_dark     = (allow_dark_fn)(void *)GetProcAddress(m, MAKEINTRESOURCEA(133));
    g_refresh_policy = (refresh_policy_fn)(void *)GetProcAddress(m, MAKEINTRESOURCEA(104));
    if (g_pref_mode) g_pref_mode(1);            /* 1 = AllowDark */
    if (g_refresh_policy) g_refresh_policy();
}

/**
 * @brief 读系统的"应用深色"设置
 * @return int 1 = 系统当前是深色；0 = 浅色或读不到（读不到 / 值不认识都按浅色）
 */
int cgw_theme_system_dark(void)
{
    HKEY  k    = NULL;
    DWORD v    = 1, sz = sizeof(v), type = 0;
    int   dark = 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&v, &sz) == ERROR_SUCCESS) {
            dark = (v == 0);            /* 0 = 深色（这个名字问的是"用浅色吗"） */
        }
        RegCloseKey(k);
    }
    return dark;
}

/**
 * @brief 窗口底色：深色是深面，否则用文档里设的底色（没设就用系统面色）
 * @param app 应用对象，可为 NULL
 * @return unsigned 0xRRGGBB 窗口底色
 */
unsigned cgw_theme_window_bg(const cg_app_t *app)
{
    if (app && app->dark) return CG_DARK_SURFACE;
    if (app && app->doc.bg != CG_COLOR_NONE) return app->doc.bg;
    return GetSysColor(COLOR_BTNFACE) & 0x00FFFFFFu;
}

/**
 * @brief 卡片没设底色时自己填的面色（深色下一并变深，否则卡片里会是浅色方块）
 * @param app 应用对象，可为 NULL
 * @return unsigned 0xRRGGBB 卡片默认面色
 */
unsigned cgw_theme_card_face(const cg_app_t *app)
{
    return (app && app->dark) ? CG_DARK_SURFACE_2 : (GetSysColor(COLOR_BTNFACE) & 0x00FFFFFFu);
}

/**
 * @brief 无显式前景色时的文字色
 * @param app 应用对象，可为 NULL
 * @return unsigned 深色返回深色下的文字色；浅色返回"无颜色"（表示别动，保持系统默认字色）
 */
unsigned cgw_theme_text(const cg_app_t *app)
{
    return (app && app->dark) ? CG_DARK_ON_SURFACE : CG_COLOR_NONE;
}

/** EM_SETBKGNDCOLOR（WM_USER + 67）：RichEdit 不理 WM_CTLCOLOR，底色只能这么给。
 *  自己定义这个常量，免得为了一个消息号把 richedit.h 拖进来。 */
#define CG_EM_SETBKGNDCOLOR (WM_USER + 67)

/**
 * @brief 让一个控件的原生窗口跟随当前主题（建控件时调一次；换主题时对全部控件再调一遍）
 * @param w 目标控件，可为 NULL
 * @return void 无返回值
 */
void cgw_theme_widget(cg_widget_t *w)
{
    cg_app_t           *app = w ? w->app : NULL;
    set_window_theme_fn fn;
    HWND                h;

    if (!app || !w || !w->native) return;
    h  = (HWND)w->native;
    fn = theme_fn();

    /* 角色引用先换成当前模式的具体值（引用串保留 —— 切主题时本函数被再调一遍就重算；
     * 不存在的角色求出来是"没写"，外观退缺省，解析期已有 WARN）。 */
    if (w->style.fg_role) w->style.fg = cg_role_lookup(&app->doc, w->style.fg_role, app->dark);
    if (w->style.bg_role) w->style.bg = cg_role_lookup(&app->doc, w->style.bg_role, app->dark);

    if (fn && (cgw_is_t(w, "listbox") || cgw_is_t(w, "table") || cgw_is_t(w, "tree"))) {
        /* 这三个是 ListBox / ListView / TreeView。深色要两步：先按窗口放行，再换主题
         * （见上面 dark_opt_in 的注释）。浅色时传空串还原。 */
        if (app->dark && g_allow_dark) g_allow_dark(h, TRUE);
        fn(h, app->dark ? L"DarkMode_Explorer" : L"", NULL);
    }
    /* 终端/富文本是 RichEdit：WM_CTLCOLOR 给它的底色它不理，得单独发这条消息。
     * 浅色时 wParam=1 让它回到系统色 —— 换主题要能换回来。 */
    if (cgw_is_t(w, "terminal") || cgw_is_t(w, "richtext")) {
        /* 控件自己写了 bg 就**听它的** —— 此前无条件发主题色，等于把 style.bg 吃掉了
         * （深色示例 2026-09-27 报回来的：终端上写 bg 不生效）。 */
        if (w->style.bg != CG_COLOR_NONE) {
            SendMessageW(h, CG_EM_SETBKGNDCOLOR, 0, (LPARAM)cgw_to_colorref(w->style.bg));
        } else {
            SendMessageW(h, CG_EM_SETBKGNDCOLOR, app->dark ? 0 : 1,
                         app->dark ? (LPARAM)cgw_to_colorref(CG_DARK_SURFACE_2) : 0);
        }
    }
    /* 输入框的边框带补底色（s->bg）是建控件时缓存的，换主题要重算一遍，
     * 否则切深浅后边框带还是旧底、和客户区两截对不上（深色皮肤 T1；
     * 客户区那半走 WM_CTLCOLOR 的深色分支，cg_win_ctl.c）。 */
    if (cgw_is_t(w, "edit")) {
        cg_skin_edit_retheme(w);
    }
}

/**
 * @brief 定下"现在是深色吗"（配置里的"自动"在这里解析成实际值）
 * @param app 应用对象
 * @return void 无返回值
 */
void cgw_theme_init(cg_app_t *app)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;

    if (!app) return;
    app->dark = (app->doc.dark == -1) ? cgw_theme_system_dark()
                                      : (app->doc.dark == 1 ? 1 : 0);
    if (st) st->dark = app->dark;
    /* 进程级"允许深色"必须在**控件创建之前**声明 —— 清单控件是在建窗口时就按当时的
     * 模式初始化主题的，建完再声明它们已经晚了（2026-09-27 实测：晚调时 listbox/表格/树
     * 照样白底）。本函数在 cgw_create_native_tree() 之前被调用，正好是最早的时机。 */
    dark_opt_in();
}

/**
 * @brief 把当前主题应用到窗口与全部控件（换背景刷、重设系统控件主题、整窗重画）
 * @param app 应用对象
 * @return void 无返回值
 */
void cgw_theme_apply(cg_app_t *app)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;
    HWND            h;

    if (!app || !st) return;
    h = (HWND)app->hwnd;

    /* app.bg 的角色引用先换值（下面的窗口底刷要用它），窗口伪控件的 bg 跟着同步；
     * 控件自己的引用在下面 cgw_theme_widget 循环里逐个求值。 */
    if (app->doc.bg_role)
        app->doc.bg = cg_role_lookup(&app->doc, app->doc.bg_role, app->dark);
    if (app->window) app->window->style.bg = app->doc.bg;

    if (st->bg_brush) {
        DeleteObject(st->bg_brush);
        st->bg_brush = NULL;
    }
    st->bg_color = cgw_theme_window_bg(app);
    st->bg_brush = CreateSolidBrush(st->bg_color);
    cgw_clear_behind_cache();   /* 标签页页体的"背后色"按窗口缓存，换主题必须清，否则停在旧色 */

    if (h) {
        /* 窗口类的背景刷是注册类时给的那个（COLOR_BTNFACE+1）—— 深色下必须换掉，
         * 否则拉伸窗口时露出来的边角还是系统浅灰。 */
        SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)st->bg_brush);
    }
    for (cg_widget_t *w = app->widgets; w; w = w->next) cgw_theme_widget(w);
    if (h) RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

/**
 * @brief 运行时切换深色（配置里的值只在加载期读一次；这个是给程序里改的）
 * @param app 应用对象，可为 NULL
 * @param on 非 0 = 切深色，0 = 切浅色
 * @return int 1 = 已应用；0 = app 为 NULL
 */
int cg_theme_set_dark(cg_app_t *app, int on)
{
    if (!app) return 0;
    app->dark = on ? 1 : 0;
    cgw_theme_apply(app);
    return 1;
}

/**
 * @brief 现在是深色吗（自检没有窗口也答得出）
 * @param app 应用对象，可为 NULL
 * @return int 1 = 深色；0 = 浅色或 app 为 NULL
 */
int cg_theme_is_dark(const cg_app_t *app)
{
    return (app && app->dark) ? 1 : 0;
}
