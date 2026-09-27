/**
 * @file    cg_win_util.c
 * @brief   后端通用工具：UTF-8 转换、字体缓存、颜色刷、路径与资源解析、时基
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

#include <wchar.h>   /* wcslen：展开换行时算长度 */

/* ------------------------------------------------------------ 字符串 */

/** @brief UTF-8 转 UTF-16 堆串（调用方 free）；空/失败返回 NULL
 *  @param s 源 UTF-8 字符串；NULL 直接返回 NULL
 *  @return wchar_t * 堆分配的 UTF-16 串，调用方 free；失败返回 NULL
 */
wchar_t *cgw_wc_from_utf8(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/** @brief 同 cgw_wc_from_utf8，但把逻辑换行 `\n` 展开成多行 EDIT 要求的 `\r\n`
 *  @param s 源 UTF-8 字符串；NULL 直接返回 NULL
 *  @return wchar_t * 堆分配的 UTF-16 串，调用方 free；失败返回 NULL
 *  @details 多行 EDIT 只认 `\r\n`：只给 `\n` 的话整段文字会挤成一行（看不出换行）。
 *           已经是 `\r\n` 的不重复插；没有换行的直接原样返回，不多一次分配。
 *          拉回来时 cgw_pull_edit_text 会把 `\r` 去掉，模型和 .cgui 里始终只有 `\n`。
 */
wchar_t *cgw_wc_from_utf8_crlf(const char *s)
{
    wchar_t *w = cgw_wc_from_utf8(s);
    wchar_t *out;
    size_t   need = 0, j = 0, i;

    if (!w) return NULL;
    for (i = 0; w[i]; i++) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) need++;
    }
    if (need == 0) return w;

    out = (wchar_t *)malloc((wcslen(w) + need + 1) * sizeof(wchar_t));
    if (!out) {
        free(w);
        return NULL;
    }
    for (i = 0; w[i]; i++) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) out[j++] = L'\r';
        out[j++] = w[i];
    }
    out[j] = 0;
    free(w);
    return out;
}

/* ------------------------------------------------------------ 字体缓存与缩放 */

/** @brief 逻辑像素转物理像素（app->scale 用百分比存，100 = 96dpi）
 *  @param app 所属应用；scale <= 0 时按 100 处理
 *  @param logical 逻辑像素值（可正可负）
 *  @return int 物理像素值（四舍五入）
 */
int cgw_scale_px(const cg_app_t *app, int logical)
{
    int s = app->scale > 0 ? app->scale : 100;
    return (int)(((long long)logical * s + 50) / 100);
}

/**
 * @brief 取（或缓存创建）一个 HFONT
 * @param app 所属应用；提供 DPI 与字体缓存
 * @param family 字体族；NULL 或空串用系统 UI 字体
 * @param size_pt 磅数；<= 0 视为默认字号
 * @param bold 是否加粗；非 0 = FW_BOLD
 * @param underline 是否下划线；非 0 = 带下划线
 * @return HFONT 字体句柄（缓存，勿删）；分配失败返回 NULL 走系统默认
 */
HFONT cgw_font_for(cg_app_t *app, const char *family, int size_pt, int bold, int underline)
{
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    char key[128];
    int  height;
    cg_font_node_t *node;

    if (!st) return NULL;
    if (!family || !*family) family = "Microsoft YaHei UI";
    if (size_pt <= 0) {
        /* 没声明字号 = 应用默认 UI 字号。以前直接返 NULL 让调用方拿
         * DEFAULT_GUI_FONT —— 那是不随 DPI 缩放的旧系统字体（宋体风、固定小字号），
         * 和声明了 font/size 的控件（雅黑、随 DPI 缩放）同屏混排就"格格不入"
         * （2026-09-25 用户报"分组框标题字体格格不入"，实为无字体声明的控件
         * 掉进 DEFAULT_GUI_FONT）。这里改为走统一缓存：雅黑 9pt @ 当前 DPI。 */
        size_pt = 9;
    }
    height = -MulDiv(size_pt, st->dpi, 72);

    /* key 里必须带 dpi：height 是按 dpi 算出来的，跨屏 DPI 变了若命中旧缓存，
     * 字号就还停在旧比例（2026-09-23 高 DPI 审计发现）。 */
    snprintf(key, sizeof(key), "%s|%d|%d|%d|%d",
             family, size_pt, bold ? 1 : 0, underline ? 1 : 0, st->dpi);
    for (node = st->fonts; node; node = node->next) {
        if (strcmp(node->key, key) == 0) return node->hfont;
    }

    node = (cg_font_node_t *)calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->key = _strdup(key);
    {
        wchar_t *wf = cgw_wc_from_utf8(family);
        node->hfont = CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                                  0, underline ? TRUE : FALSE, 0,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                  wf ? wf : L"Microsoft YaHei UI");
        free(wf);
    }
    /* key 或字体任一分配失败：把这次造出来的字体也还掉，返回 NULL 让调用方走系统默认字体。
     * 不缓存一个残缺节点（key 为 NULL 时下面 strcmp 会崩，hfont 也没人释放会泄漏）。 */
    if (!node->key || !node->hfont) {
        if (node->hfont) DeleteObject(node->hfont);
        free(node->key);
        free(node);
        return NULL;
    }
    node->next = st->fonts;
    st->fonts = node;
    return node->hfont;
}


/* ------------------------------------------------------------ 颜色 */

/** @brief 0xRRGGBB 转 COLORREF
 *  @param c 0xRRGGBB 形式的颜色
 *  @return COLORREF 系统 COLORREF(0x00BBGGRR)
 */
COLORREF cgw_to_colorref(unsigned c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

/** @brief 一组小画刷缓存，给 WM_CTLCOLOR* 返回用；颜色数量有限，固定表足够
 *  @param rgb 颜色 0xRRGGBB
 *  @return HBRUSH 缓存的实心画刷（共享缓存，勿删）
 */
HBRUSH cgw_brush_for(unsigned rgb)
{
    static struct { unsigned rgb; HBRUSH br; } cache[64];
    static int used = 0;

    for (int i = 0; i < used; i++) {
        if (cache[i].rgb == rgb) return cache[i].br;
    }
    if (used < (int)(sizeof(cache) / sizeof(cache[0]))) {
        cache[used].br = CreateSolidBrush(cgw_to_colorref(rgb));
        cache[used].rgb = rgb;
        return cache[used++].br;
    }
    return (HBRUSH)GetStockObject(WHITE_BRUSH);
}

/* ------------------------------------------------------------ 类型判断 */

/** @brief 控件类型判断（NULL 安全）
 *  @param w 待判断控件；NULL 直接返回 0
 *  @param type 目标类型名（如 "edit"）
 *  @return int 非 0 = 类型匹配，0 = 不匹配或 w 为 NULL
 */
int cgw_is_t(const cg_widget_t *w, const char *type)
{
    return w && w->type && strcmp(w->type, type) == 0;
}

/** @brief 是否输入类控件（edit/richtext，文字要拉回模型的那类）。
 *  多行与数值框都是 edit，所以这里只剩两种类型 —— 要区分是不是数值框用
 *  cg_widget_is_numedit()（core/cg_widget.c）。
 *  @param w 待判断控件
 *  @return int 非 0 = edit 或 richtext，0 = 其它
 */
int cgw_is_editlike(const cg_widget_t *w)
{
    return cgw_is_t(w, "edit") || cgw_is_t(w, "richtext");
}

/**
 * @brief 有控制台时把控制台输入 / 输出代码页切到 UTF-8
 * @return void 无返回值
 * @note printf 的中文是 UTF-8 字节（源码与执行字符集都是 UTF-8），控制台默认代码页
 *       （如 GBK）会显示成乱码；无控制台（release 版、输出重定向）时是空操作
 */
void cg_win_console_utf8(void)
{
    if (GetConsoleWindow() != NULL) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
}
ULONG_PTR g_win_gdip_token = 0;   /* GDI+ 的启动令牌，0 表示没起来 */

/** @brief 文件是否存在
 *  @param path 待检查的路径（窄字符，按系统 ANSI 代码页）
 *  @return int 1 = 存在，0 = 不存在
 */
static int file_exists(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(h);
    return 1;
}

/** @brief 取路径的目录部分（无目录时写 "."）
 *  @param path 源路径；按最后一个 '\' 或 '/' 切分
 *  @param out 输出缓冲；写目录（结尾不带分隔符）并保证 NUL 结尾
 *  @param size out 的容量（字节）
 *  @return void 无返回值
 */
static void dir_of(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    size_t n;
    if (slash2 > slash) slash = slash2;
    if (!slash) { snprintf(out, size, "."); return; }
    n = (size_t)(slash - path);
    if (n > size - 1) n = size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/** @brief 资产路径按三处依次找，返回是否找到
 *  @param app 所属应用；用它的 cgui_dir 做第 2 处查找，可为 NULL
 *  @param path 资产相对/绝对路径（窄字符）
 *  @param out 输出：命中的完整路径，保证 NUL 结尾
 *  @param outsz out 的容量（字节）；为 0 直接失败
 *  @return int 1 = 找到，0 = 三处都没找到或参数非法
 *  @details 三处依次是：1) 原样（相对当前目录）  2) 相对 .cgui 所在目录  3) 相对 exe 所在目录。
 *  第 2 条最符合直觉：.cgui 和 assets\ 放在一起，换台机器整个目录搬走也能用。
 */
int cgw_resolve_asset(const cg_app_t *app, const char *path, char *out, size_t outsz)
{
    if (!path || !*path || outsz == 0) return 0;
    snprintf(out, outsz, "%s", path);
    if (file_exists(out)) return 1;
    if (app && app->cgui_dir) {
        snprintf(out, outsz, "%s\\%s", app->cgui_dir, path);
        if (file_exists(out)) return 1;
    }
    {
        char exe[512], dir[512];
        if (cg_exe_path(exe, sizeof(exe)) == 0) {
            dir_of(exe, dir, sizeof(dir));
            snprintf(out, outsz, "%s\\%s", dir, path);
            if (file_exists(out)) return 1;
        }
    }
    return 0;
}

/** @brief 打开图片成位图（路径查找见 cgw_resolve_asset）
 *  @param app 所属应用；转给路径解析
 *  @param path 图片路径（窄字符）；空或找不到返回 NULL
 *  @return HBITMAP 位图句柄，调用方负责 DeleteObject；失败返回 NULL
 */
HBITMAP cgw_load_image_file(const cg_app_t *app, const char *path)
{
    char   full[1024];
    WCHAR *wpath;
    void  *gdip_bmp = NULL;
    HBITMAP hbmp = NULL;
    int     need;

    if (!path || !*path) return NULL;
    if (!cgw_resolve_asset(app, path, full, sizeof(full))) return NULL;

    need = MultiByteToWideChar(CP_UTF8, 0, full, -1, NULL, 0);
    if (need <= 0) return NULL;
    wpath = (WCHAR *)malloc((size_t)need * sizeof(WCHAR));
    if (!wpath) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, full, -1, wpath, need);

    if (GdipCreateBitmapFromFile(wpath, &gdip_bmp) == 0 && gdip_bmp) {
        GdipCreateHBITMAPFromBitmap(gdip_bmp, &hbmp, 0x00FFFFFF);
        GdipDisposeImage(gdip_bmp);
    }
    free(wpath);
    return hbmp;
}

/** @brief 打开 .ico 成 HICON（路径查找见 cgw_resolve_asset）；找不到返回 NULL
 *  @param app 所属应用；转给路径解析
 *  @param path 图标路径（窄字符）；空或找不到返回 NULL
 *  @return HICON 图标句柄，调用方负责 DestroyIcon；失败返回 NULL
 */
HICON cgw_load_icon_file(const cg_app_t *app, const char *path)
{
    char    full[1024];
    WCHAR  *wp;
    HICON   ic = NULL;
    int     need;

    if (!path || !*path) return NULL;
    if (!cgw_resolve_asset(app, path, full, sizeof(full))) return NULL;

    need = MultiByteToWideChar(CP_UTF8, 0, full, -1, NULL, 0);
    if (need <= 0) return NULL;
    wp = (WCHAR *)malloc((size_t)need * sizeof(WCHAR));
    if (!wp) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, full, -1, wp, need);
    ic = (HICON)LoadImageW(NULL, wp, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    free(wp);
    return ic;
}

/** @brief 鼠标指针名 -> 系统光标 id（.cgui 的 app.cursor，对齐「鼠标指针」属性）
 *  @param name 指针名（如 "hand"）；空或未知名退回箭头
 *  @return LPCWSTR 系统预定义光标 id（IDC_*），静态资源勿释放
 */
LPCWSTR cgw_cursor_id_of(const char *name)
{
    if (!name || !*name) return NULL;
    if (!strcmp(name, "arrow"))    return IDC_ARROW;
    if (!strcmp(name, "hand"))     return IDC_HAND;
    if (!strcmp(name, "ibeam"))    return IDC_IBEAM;
    if (!strcmp(name, "wait"))     return IDC_WAIT;
    if (!strcmp(name, "cross"))    return IDC_CROSS;
    if (!strcmp(name, "sizeall"))  return IDC_SIZEALL;
    if (!strcmp(name, "sizens"))   return IDC_SIZENS;
    if (!strcmp(name, "sizewe"))   return IDC_SIZEWE;
    if (!strcmp(name, "sizenesw")) return IDC_SIZENESW;
    if (!strcmp(name, "sizenwse")) return IDC_SIZENWSE;
    if (!strcmp(name, "no"))       return IDC_NO;
    if (!strcmp(name, "help"))     return IDC_HELP;
    if (!strcmp(name, "uparrow"))  return IDC_UPARROW;
    return IDC_ARROW;
}

/** @brief 当前 exe 的完整路径（含文件名），成功返回 0
 *  @param buf 输出缓冲；写 UTF-8 完整路径并保证 NUL 结尾
 *  @param size buf 的容量（字节）；不足则不写
 *  @return int 0 = 成功，-1 = 参数非法 / 取路径失败 / 缓冲不足
 */
int cg_exe_path(char *buf, size_t size)
{
    wchar_t wbuf[MAX_PATH * 2];
    DWORD   n;
    int     need;

    if (!buf || size == 0) return -1;
    n = GetModuleFileNameW(NULL, wbuf, (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n == 0 || n >= (DWORD)(sizeof(wbuf) / sizeof(wbuf[0]))) return -1;

    need = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (need <= 0 || (size_t)need > size) return -1;
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, need, NULL, NULL);
    return 0;
}

/** @brief 宽字符 -> UTF-8 堆串（调用方 free）；NULL 进 NULL 出，转换失败也返回 NULL
 *  @param ws 源宽字符串；NULL 直接返回 NULL
 *  @return char * 堆分配的 UTF-8 串，调用方 free；失败返回 NULL
 *  @note 后端里"写进调用方缓冲"的调用点统一走下面的 cgw_utf8_into()：宽->窄转换的
 *        实现在本文件里只有这一处，其它子模块不再各留一份私有副本。
 */
char *cgw_utf8_from_wcs(const wchar_t *ws)
{
    int   need;
    char *out;

    if (!ws) return NULL;
    need = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    out = (char *)malloc((size_t)need);
    if (!out) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, need, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

/** @brief 宽字符 -> UTF-8，写进调用方给的缓冲（截断安全、失败或缓冲不足留空串）
 *  @details 要缓冲语义的调用点（对话框回填路径、诊断取 %LOCALAPPDATA%）不想为一次
 *           转换再 malloc/free —— 尤其诊断路径要在崩溃/低内存下跑 —— 所以保留这个
 *           入口，但它复用 cgw_utf8_from_wcs 的转换逻辑，不是第二份实现。
 *  @param ws       宽字符串；NULL 视作空串
 *  @param out      目标缓冲；NULL 或 out_size<=0 时什么也不做
 *  @param out_size out 的容量（字节）
 *  @return void 无返回值
 *  @note 缓冲装不下整串（含结尾 NUL）时，与合并前三处私有副本一致地留空串
 *        （不返回半条路径）；任何路径下 out 都以 NUL 结尾。
 */
void cgw_utf8_into(const wchar_t *ws, char *out, int out_size)
{
    char  *u8;
    size_t need;

    if (!out || out_size <= 0) return;
    out[0] = '\0';
    if (!ws) return;
    u8 = cgw_utf8_from_wcs(ws);
    if (!u8) return;
    need = strlen(u8) + 1;                  /* 含结尾 NUL */
    if (need <= (size_t)out_size) memcpy(out, u8, need);
    free(u8);
}

/** @brief 毫秒级时基（后端提供），用于双击判定与定时器
 *  @return unsigned long 自开机起的毫秒数（截断为 32 位）
 */
unsigned long cg_ticks(void)
{
    return (unsigned long)GetTickCount64();
}

/** @brief 系统双击间隔，默认 500ms
 *  @return unsigned long 双击判定毫秒阈值；系统返回 0 时用默认 500
 */
unsigned long cg_double_click_ms(void)
{
    UINT t = GetDoubleClickTime();
    return t ? (unsigned long)t : 500;
}
