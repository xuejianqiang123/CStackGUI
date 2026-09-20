/**
 * @file    cg_win_dialog.c
 * @brief   通用对话框：打开/保存文件、选色、消息框
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* ============================================================ 公共 API
 * 通用对话框 / 画板绘制 / 表格增强 / 网页浏览。
 * 这些都要摸 Win32 句柄，所以住在后端文件里，但对外是普通 C 函数。
 */

/** @brief 弹对话框时的属主窗口：优先父控件，退回主窗口 */
static HWND dialog_owner(const cg_widget_t *parent)
{
    if (parent && parent->native) return (HWND)parent->native;
    return (parent && parent->app && parent->app->hwnd) ? (HWND)parent->app->hwnd : NULL;
}

/** 把 UTF-16 路径写回调用者的 UTF-8 缓冲区（截断安全） */
static void put_path_utf8(char *out, int out_size, const wchar_t *wpath)
{
    int need;
    if (!out || out_size <= 0) return;
    need = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
    if (need <= 0) { out[0] = '\0'; return; }
    if (need > out_size) need = out_size;   /* 缓冲不够就截断 */
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, need, NULL, NULL);
    out[out_size - 1] = '\0';
}

/** filter 写法沿用网页习惯 "文本|*.txt|全部|*.*"，转成 Win32 要的双 0 分隔 */
static wchar_t *make_filter_w(const char *filter)
{
    wchar_t *wf;
    int n;
    if (!filter || !*filter) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, filter, -1, NULL, 0);
    if (n <= 0) return NULL;
    wf = (wchar_t *)calloc((size_t)n + 1, sizeof(wchar_t));
    if (!wf) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, filter, -1, wf, n);
    for (int i = 0; i < n; i++) {
        if (wf[i] == L'|') wf[i] = L'\0';
    }
    wf[n] = L'\0';   /* 结尾补第二个 0 */
    return wf;
}

/**
 * @brief 弹"打开文件"对话框，选中路径写进 out_path
 *
 * @param parent 定位用的控件，可为 NULL
 * @param title  对话框标题，NULL 用系统默认
 * @param filter 文件类型过滤器，格式 "说明|模式|说明|模式"，如 "文本|*.txt|全部|*.*"，可为 NULL
 * @param out_path 选中的完整路径（UTF-8），至少 1024 字节
 * @param out_size out_path 的容量
 * @return int 1 = 用户确认，0 = 取消或失败
 */
int cg_open_file_dialog(cg_widget_t *parent, const char *title,
                        const char *filter, char *out_path, int out_size)
{
    OPENFILENAMEW ofn;
    wchar_t buf[1024];
    wchar_t *wfilter = make_filter_w(filter);
    wchar_t *wtitle = cgw_wc_from_utf8(title);
    BOOL ok;

    if (out_path && out_size > 0) out_path[0] = '\0';
    if (!out_path || out_size <= 0) { free(wfilter); free(wtitle); return 0; }

    memset(&ofn, 0, sizeof(ofn));
    buf[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog_owner(parent);
    ofn.lpstrFilter = wfilter;
    ofn.lpstrTitle = wtitle;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ok = GetOpenFileNameW(&ofn);
    free(wfilter);
    free(wtitle);
    if (!ok) return 0;
    put_path_utf8(out_path, out_size, buf);
    return 1;
}

/**
 * @brief 弹"保存文件"对话框（已存在同名文件时系统会先问一遍）
 *
 * @param parent 定位用的控件，可为 NULL
 * @param title 对话框标题，可为 NULL
 * @param filter 同 cg_open_file_dialog，可为 NULL
 * @param default_ext 缺省扩展名（如 "txt"），可为 NULL
 * @param out_path 选中的完整路径（UTF-8），至少 1024 字节
 * @param out_size out_path 的容量
 * @return int 1 = 用户确认，0 = 取消或失败
 */
int cg_save_file_dialog(cg_widget_t *parent, const char *title,
                        const char *filter, const char *default_ext,
                        char *out_path, int out_size)
{
    OPENFILENAMEW ofn;
    wchar_t buf[1024];
    wchar_t *wfilter = make_filter_w(filter);
    wchar_t *wtitle = cgw_wc_from_utf8(title);
    wchar_t *wext = cgw_wc_from_utf8(default_ext);
    BOOL ok;

    if (out_path && out_size > 0) out_path[0] = '\0';
    if (!out_path || out_size <= 0) { free(wfilter); free(wtitle); free(wext); return 0; }

    memset(&ofn, 0, sizeof(ofn));
    buf[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog_owner(parent);
    ofn.lpstrFilter = wfilter;
    ofn.lpstrTitle = wtitle;
    ofn.lpstrDefExt = wext;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ok = GetSaveFileNameW(&ofn);
    free(wfilter);
    free(wtitle);
    free(wext);
    if (!ok) return 0;
    put_path_utf8(out_path, out_size, buf);
    return 1;
}

/**
 * @brief 弹系统颜色对话框
 *
 * @param parent 定位用的控件，可为 NULL
 * @param rgb 进出都是 0xRRGGBB：进来是初始颜色，用户确认后被更新
 * @return int 1 = 用户确认，0 = 取消
 */
int cg_choose_color(cg_widget_t *parent, unsigned *rgb)
{
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    unsigned cur = (rgb && *rgb) ? (*rgb & 0xFFFFFF) : 0x6750A4;

    if (!rgb) return 0;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = dialog_owner(parent);
    cc.rgbResult = cgw_to_colorref(cur);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return 0;
    *rgb = ((unsigned)GetRValue(cc.rgbResult) << 16) |
           ((unsigned)GetGValue(cc.rgbResult) << 8) |
           (unsigned)GetBValue(cc.rgbResult);
    return 1;
}

/**
 * @brief 消息框（一个"确定"按钮）
 *
 * @param parent 定位用的控件，可为 NULL
 * @param text 内容
 * @param title 标题，NULL 用 "提示"
 * @param icon 0=信息 1=警告 2=错误 3=问号
 * @return void 无返回值
 */
void cg_message_box(cg_widget_t *parent, const char *text, const char *title, int icon)
{
    static const unsigned icons[4] = {
        MB_ICONINFORMATION, MB_ICONWARNING, MB_ICONERROR, MB_ICONQUESTION,
    };
    wchar_t *wt = cgw_wc_from_utf8(text ? text : "");
    wchar_t *tt = cgw_wc_from_utf8(title ? title : "提示");
    DWORD flag = (icon >= 0 && icon <= 3) ? icons[icon] : MB_ICONINFORMATION;
    MessageBoxW(dialog_owner(parent), wt ? wt : L"", tt ? tt : L"", MB_OK | flag);
    free(wt);
    free(tt);
}
