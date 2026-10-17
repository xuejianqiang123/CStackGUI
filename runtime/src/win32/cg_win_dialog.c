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

/**
 * @brief 弹对话框时的属主窗口：优先父控件，退回主窗口
 * @param parent 定位用的控件，可为 NULL
 * @return HWND 属主窗口；都没有时为 NULL
 */
static HWND dialog_owner(const cg_widget_t *parent)
{
    if (parent && parent->native) return (HWND)parent->native;
    return (parent && parent->app && parent->app->hwnd) ? (HWND)parent->app->hwnd : NULL;
}

/**
 * @brief 把网页习惯的过滤器串转成文件对话框要的双 NUL 分隔宽串
 * @param filter UTF-8 过滤器串，如 "文本|*.txt|全部|*.*"，可为 NULL / 空
 * @return wchar_t* 新分配的宽串（调用方负责 free）；无内容或转换失败时 NULL
 */
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
    cgw_utf8_into(buf, out_path, out_size);   /* 宽->窄转换统一在 cg_win_util.c */
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
    cgw_utf8_into(buf, out_path, out_size);   /* 宽->窄转换统一在 cg_win_util.c */
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

/* ============================================================ 单行输入框
 * 为什么要用"内存里的对话框模板"：本工程没有 .rc 资源，也不该为一个输入框去引一套资源编译；
 * 于是把 DLGTEMPLATE 在代码里拼出来（头 + 标题 + 4 个控件项），交给系统对话框管理器跑模态循环
 * —— 回车 = 确定、Esc = 取消、Tab 顺序、模态禁用主窗口、回车键的默认按钮高亮，全是白送的。
 * 字体走属主的 LOGFONT（DS_SETFONT）：对话框与主界面字号一致，DLU 也按同一套字体算，
 * 换字号不会错位。
 */

#define CG_INPUT_PROMPT_ID 1001  /**< 提示文字的控件 id */
#define CG_INPUT_EDIT_ID   1002  /**< 单行输入框的控件 id：DlgProc 里按它取文字 */

/** cg_input_box 的上下文：模板里放不下 / 要在 DlgProc 里回写的部分挂在这儿 */
typedef struct {
    HWND           owner;  /**< 属主窗口：用来把对话框居中，也是模态禁用的对象 */
    const wchar_t *prompt; /**< 输入框上方的提示（宽串） */
    const wchar_t *value;  /**< 初始值（宽串） */
    char          *out;    /**< 输出缓冲（UTF-8） */
    int            cap;    /**< out 的字节数 */
    int            ok;     /**< 1 = 用户点了确定 */
} input_ctx_t;

/**
 * @brief 模板游标对齐：每个控件项必须从 4 字节边界开始
 * @param base 模板起始（自身 DWORD 对齐）
 * @param p 当前游标
 * @return void 无返回值（必要时补一个 0 字）
 */
static void tmpl_align(const WORD *base, WORD **p)
{
    if (((*p - base) & 1) != 0) *(*p)++ = 0;
}

/**
 * @brief 往模板里写一个 UTF-16 串（自带结尾 0）
 * @param p 当前游标
 * @param s 宽串；NULL 当空串
 * @return void 无返回值
 */
static void tmpl_wstr(WORD **p, const wchar_t *s)
{
    if (!s) s = L"";
    while (*s) *(*p)++ = (WORD)*s++;
    *(*p)++ = 0;
}

/**
 * @brief 往模板里写一个控件项（DLGITEMTEMPLATE + 类原子 + 文字 + 空创建数据）
 * @param base 模板起始
 * @param p 当前游标
 * @param cls_atom 系统预定义类的原子值：0x0080 按钮 / 0x0081 编辑框 / 0x0082 静态
 * @param style 窗口样式
 * @param id 控件 id
 * @param x 左（对话框单位）
 * @param y 上（对话框单位）
 * @param cx 宽（对话框单位）
 * @param cy 高（对话框单位）
 * @param text 控件文字（宽串），可为 NULL
 * @return void 无返回值
 */
static void tmpl_item(const WORD *base, WORD **p, WORD cls_atom, DWORD style, WORD id,
                      short x, short y, short cx, short cy, const wchar_t *text)
{
    DLGITEMTEMPLATE *it;

    tmpl_align(base, p);
    it = (DLGITEMTEMPLATE *)(*p);
    it->style           = style;
    it->dwExtendedStyle = 0;
    it->x  = x;
    it->y  = y;
    it->cx = cx;
    it->cy = cy;
    it->id = id;
    *p += 9;                 /* DLGITEMTEMPLATE = 18 字节 = 9 个 WORD */
    *(*p)++ = 0xFFFF;        /* 类：0xFFFF + 原子值 = 系统预定义类 */
    *(*p)++ = cls_atom;
    tmpl_wstr(p, text);
    *(*p)++ = 0;             /* 创建数据：0 字节 */
}

/**
 * @brief 输入框对话框的窗口过程
 * @param dlg 对话框窗口
 * @param msg 消息号
 * @param wp 附加参数（WM_COMMAND 里低字是控件 id）
 * @param lp 附加参数（WM_INITDIALOG 时是 input_ctx_t 指针）
 * @return INT_PTR TRUE = 已处理，FALSE = 交给系统
 */
static INT_PTR CALLBACK InputDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    input_ctx_t *ctx = (input_ctx_t *)GetWindowLongPtrW(dlg, GWLP_USERDATA);

    switch (msg) {
    case WM_INITDIALOG: {
        RECT ro, rd;

        ctx = (input_ctx_t *)lp;
        SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)ctx);
        if (ctx->prompt) SetDlgItemTextW(dlg, CG_INPUT_PROMPT_ID, ctx->prompt);
        if (ctx->value)  SetDlgItemTextW(dlg, CG_INPUT_EDIT_ID, ctx->value);
        SendDlgItemMessageW(dlg, CG_INPUT_EDIT_ID, EM_SETSEL, 0, (LPARAM)-1);  /* 全选：敲字即替换 */
        SetFocus(GetDlgItem(dlg, CG_INPUT_EDIT_ID));
        /* 居中到属主窗口（DS_CENTER 只居中到屏幕：多显示器时对话框会跑到别的屏上去） */
        if (ctx->owner && GetWindowRect(ctx->owner, &ro) && GetWindowRect(dlg, &rd)) {
            SetWindowPos(dlg, NULL,
                         ro.left + ((ro.right - ro.left) - (rd.right - rd.left)) / 2,
                         ro.top + ((ro.bottom - ro.top) - (rd.bottom - rd.top)) / 2,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return FALSE;        /* 焦点已经自己设好：返回 FALSE 免得系统再抢一次 */
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t wbuf[1024];
            if (ctx) {
                GetDlgItemTextW(dlg, CG_INPUT_EDIT_ID, wbuf, 1024);
                cgw_utf8_into(wbuf, ctx->out, ctx->cap);   /* 截断安全，总以 0 结尾 */
                ctx->ok = 1;
            }
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dlg, 0);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

/**
 * @brief 取属主窗口的字体规格，给对话框模板的 DS_SETFONT 用
 * @param owner 属主窗口
 * @param pt 输出：字号（磅）
 * @param face 输出：字体名（宽串）
 * @param face_cap face 的字符数
 * @return int 1 = 取到了；0 = 拿不到字体信息（调用方就退回系统默认字体）
 * @note lfHeight 是"字符高"（负值 = 按字号给），换算成磅才算对话框模板要的单位
 */
static int owner_font_spec(HWND owner, WORD *pt, wchar_t *face, int face_cap)
{
    LOGFONTW lf;
    HFONT    f;
    HDC      dc;
    int      dpi, h, p, i;

    f = owner ? (HFONT)SendMessageW(owner, WM_GETFONT, 0, 0) : NULL;
    if (!f || !GetObjectW(f, (int)sizeof(lf), &lf)) return 0;
    if (!lf.lfFaceName[0] || lf.lfHeight == 0) return 0;

    dc  = owner ? GetDC(owner) : NULL;
    dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(owner, dc);
    if (dpi <= 0) dpi = 96;

    h = (lf.lfHeight < 0) ? -lf.lfHeight : lf.lfHeight;
    p = (h * 72 + dpi / 2) / dpi;
    if (p <= 0 || p > 72) return 0;
    *pt = (WORD)p;

    for (i = 0; i < face_cap - 1 && lf.lfFaceName[i]; i++) face[i] = lf.lfFaceName[i];
    face[i] = L'\0';
    return i > 0;
}

/**
 * @brief 弹一个单行输入框，等用户点确定 / 取消（模态）
 * @param parent 定位用的控件，可为 NULL
 * @param title 标题，NULL 用 "输入"
 * @param prompt 输入框上方的提示，可为 NULL
 * @param value 初始值，可为 NULL（弹出来就全选，直接敲字等于替换）
 * @param out 输出缓冲，用户确定时写进 UTF-8 文本
 * @param cap out 的字节数
 * @return int 1 = 用户点了确定、0 = 取消 / 失败 / 当前没有窗口
 * @note 没窗口（自检）时**不弹**，直接返回 0：业务据此走"没改"的分支
 * @note 模板放在静态缓冲里：对话框活着的时候系统只读它，函数返回后可以立刻被下次调用覆盖
 */
int cg_input_box(cg_widget_t *parent, const char *title, const char *prompt,
                 const char *value, char *out, int cap)
{
    static DWORD tmpl[1024];          /* 模板缓冲：头 + 字符串 + 4 个控件项，够用且不用分配 */
    input_ctx_t  ctx;
    wchar_t      face[32];
    wchar_t     *wt, *wpr, *wv, *wok, *wno;
    WORD        *base = (WORD *)tmpl;
    WORD        *p;
    WORD         pt = 0;
    DLGTEMPLATE *dt;
    HWND         owner;
    INT_PTR      r;

    if (out && cap > 0) out[0] = '\0';
    if (!out || cap <= 0) return 0;                 /* 没有输出缓冲就别弹 */

    owner = dialog_owner(parent);
    if (!owner) return 0;                           /* 无窗口 / 自检：不弹，直接当取消 */
    owner = GetAncestor(owner, GA_ROOT);
    if (!owner) return 0;

    wt  = cgw_wc_from_utf8(title ? title : "输入");
    wpr = cgw_wc_from_utf8(prompt ? prompt : "");
    wv  = cgw_wc_from_utf8(value ? value : "");
    wok = cgw_wc_from_utf8("确定");
    wno = cgw_wc_from_utf8("取消");

    memset(tmpl, 0, sizeof(tmpl));
    dt = (DLGTEMPLATE *)base;
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    if (owner_font_spec(owner, &pt, face, 32)) dt->style |= DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit            = 4;                        /* 提示 + 输入框 + 确定 + 取消 */
    dt->x  = 0;
    dt->y  = 0;
    dt->cx = 272;
    dt->cy = 82;

    p = base + 9;                                   /* 跳过 DLGTEMPLATE */
    *p++ = 0;                                       /* 无菜单 */
    *p++ = 0;                                       /* 用默认对话框类 */
    tmpl_wstr(&p, wt ? wt : L"输入");
    if (dt->style & DS_SETFONT) {
        *p++ = pt;                                  /* 字号（磅） */
        tmpl_wstr(&p, face);
    }

    tmpl_item(base, &p, 0x0082, SS_LEFT | WS_CHILD | WS_VISIBLE, CG_INPUT_PROMPT_ID,
              7, 8, 258, 20, wpr ? wpr : L"");
    tmpl_item(base, &p, 0x0081,
              ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_CHILD | WS_VISIBLE,
              CG_INPUT_EDIT_ID, 7, 32, 258, 14, L"");
    tmpl_item(base, &p, 0x0080, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_CHILD | WS_VISIBLE,
              IDOK, 160, 56, 50, 16, wok ? wok : L"OK");
    tmpl_item(base, &p, 0x0080, BS_PUSHBUTTON | WS_TABSTOP | WS_CHILD | WS_VISIBLE,
              IDCANCEL, 215, 56, 50, 16, wno ? wno : L"Cancel");

    ctx.owner  = owner;
    ctx.prompt = wpr;
    ctx.value  = wv;
    ctx.out    = out;
    ctx.cap    = cap;
    ctx.ok     = 0;

    r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), dt, owner, InputDlgProc, (LPARAM)&ctx);

    free(wt);
    free(wpr);
    free(wv);
    free(wok);
    free(wno);
    if (r == -1) return 0;                          /* 模板有问题 / 建不出来：当取消 */
    return ctx.ok ? 1 : 0;
}
