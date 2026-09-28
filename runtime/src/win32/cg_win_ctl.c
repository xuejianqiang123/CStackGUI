/**
 * @file    cg_win_ctl.c
 * @brief   控件到原生窗口类/样式的映射、日期时间文本转换、控件背景色推导与 WM_CTLCOLOR
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

#include <limits.h>   /* LLONG_MIN / LLONG_MAX：数值输入方式夹取的范围 */
#include <stdlib.h>   /* wcstod / wcstoll：失焦时解析文本 */
#include <wchar.h>    /* wcschr / wcscmp / swprintf */

/* ------------------------------------------------------------ 控件映射 */

/** 富文本控件类：Msftedit.dll 起来后用 RICHEDIT50W，否则退回旧版甚至普通 EDIT。
 *  富文本不是系统的必备组件，所以这里做降级而不是直接失败。 */
const wchar_t *g_win_richedit_class = NULL;

/** browser 控件用 atl.dll 的 AtlAxWin 宿主系统 WebBrowser 控件；
 *  起不来（理论上看不到）就退回静态占位文本。 */
int g_win_atl_ok = 0;

/** @brief 控件 -> Win32 窗口类名（group/box/card/shape 走自注册 Panel 类）
 *  @param w 目标控件；type 字段决定返回哪个窗口类
 *  @return const wchar_t * 静态类名字符串，静态存储不归调用方释放
 */
const wchar_t *cgw_class_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "button"))    return L"BUTTON";      /* 自绘：BS_OWNERDRAW（cg_skin 画 M3 圆角） */
    if (!strcmp(w->type, "menubutton"))return L"BUTTON";      /* 普通按钮，点一下弹菜单 */
    if (!strcmp(w->type, "iconbutton"))return L"BUTTON";      /* BS_OWNERDRAW：画图标 */
    if (!strcmp(w->type, "label"))     return L"STATIC";      /* 静态文本，SS_* 决定对齐 */
    if (!strcmp(w->type, "link"))      return L"STATIC";      /* SS_NOTIFY 才有 STN_CLICKED */
    if (!strcmp(w->type, "edit"))      return L"EDIT";        /* 输入框：单行/多行/数值框都走它（运行时做圆角） */
    if (!strcmp(w->type, "richtext"))  return g_win_richedit_class ? g_win_richedit_class : L"EDIT";  /* 富文本，Msftedit 降级到 EDIT */
    /* 终端也用 RichEdit（只读）：为了逐行着色 + 能选中复制，降级到 EDIT 时只是没有颜色 */
    if (!strcmp(w->type, "terminal"))  return g_win_richedit_class ? g_win_richedit_class : L"EDIT";
    if (!strcmp(w->type, "checkbox"))  return L"BUTTON";      /* 自绘：BS_OWNERDRAW（cg_skin 画勾选框） */
    if (!strcmp(w->type, "radio"))     return L"BUTTON";      /* 自绘：BS_OWNERDRAW（cg_skin 画单选圆） */
    if (!strcmp(w->type, "switch"))    return L"BUTTON";      /* BS_OWNERDRAW 自绘开关 */
    if (!strcmp(w->type, "listbox"))   return L"LISTBOX";     /* 列表框 */
    if (!strcmp(w->type, "combo"))     return L"COMBOBOX";    /* 下拉选择（CBS_DROPDOWNLIST） */
    if (!strcmp(w->type, "slider"))    return TRACKBAR_CLASSW; /* 滑块：NM_CUSTOMDRAW 自绘轨道/滑块 */
    /* progress 走自绘类 CG_PROGRESS_CLASS（见 progressring 行后面）：原生进度条不支持 custom draw */
    if (!strcmp(w->type, "separator")) return L"STATIC";      /* SS_ETCHEDHORZ 横线 */
    if (!strcmp(w->type, "image"))     return CG_IMAGE_CLASS; /* 自绘图片控件 */
    if (!strcmp(w->type, "tabs"))      return WC_TABCONTROLW;  /* 标签页 */
    if (!strcmp(w->type, "tree"))      return WC_TREEVIEWW;   /* 树形视图 */
    if (!strcmp(w->type, "table"))     return WC_LISTVIEWW;    /* 列表视图（报表模式） */
    if (!strcmp(w->type, "statusbar")) return STATUSCLASSNAMEW;/* 状态栏 */
    if (!strcmp(w->type, "datepicker"))return DATETIMEPICK_CLASSW; /* 日期/时间选择 */
    if (!strcmp(w->type, "toolbar"))   return TOOLBARCLASSNAMEW;  /* 工具栏 */
    if (!strcmp(w->type, "monthcal"))  return MONTHCAL_CLASSW;   /* 月历 */
    if (!strcmp(w->type, "progressring")) return CG_RING_CLASS;   /* 自绘圆环（进度环） */
    if (!strcmp(w->type, "progress")) return CG_PROGRESS_CLASS;   /* 自绘进度条（原生的不支持 custom draw） */
    if (!strcmp(w->type, "canvas"))    return CG_CANVAS_CLASS;    /* 自绘画板 */
    if (!strcmp(w->type, "splitter"))  return CG_SPLITTER_CLASS;  /* 自绘分隔条（能拖动） */
    if (!strcmp(w->type, "chart"))     return CG_CHART_CLASS;     /* 自绘折线/柱状图 */
    if (!strcmp(w->type, "led"))       return CG_LED_CLASS;       /* 自绘状态灯（会自己定时重绘） */
    if (!strcmp(w->type, "meter"))     return CG_METER_CLASS;     /* 自绘仪表盘 */
    if (!strcmp(w->type, "colorpicker")) return L"BUTTON";        /* BS_OWNERDRAW 色块 */
    if (!strcmp(w->type, "badge"))     return L"BUTTON";          /* BS_OWNERDRAW 小圆片 */
    if (!strcmp(w->type, "tag"))       return L"BUTTON";          /* BS_OWNERDRAW 标签片 */
    if (!strcmp(w->type, "rate"))      return L"BUTTON";          /* BS_OWNERDRAW 五角星 */
    if (!strcmp(w->type, "pagination"))return L"BUTTON";          /* BS_OWNERDRAW 分页 */
    if (!strcmp(w->type, "browser"))   return g_win_atl_ok ? L"AtlAxWin" : L"STATIC"; /* IE/WebBrowser 嵌入 */
    if (!strcmp(w->type, "timer"))     return L"STATIC";          /* 无窗口，cgw_create_native 会跳过 */
    if (!strcmp(w->type, "card") || !strcmp(w->type, "shape")) return CG_PANEL_CLASS; /* 容器面板 */
    return CG_PANEL_CLASS;   /* group / box */
}

/** @brief 日期时间：是"日期"还是"时间"，决定 DTS_* 样式
 *  @param w datepicker 控件；format == "time" 视为时间
 *  @return int 非 0 = 时间，0 = 日期
 */
int cgw_is_time_only(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "time") == 0;
}

/** @brief datepicker 是否为"日期+时间"双控件模式（format == "datetime"）
 *  @param w datepicker 控件；format == "datetime" 视为双控件模式
 *  @return int 非 0 = 日期+时间，0 = 否
 */
int cgw_is_datetime(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "datetime") == 0;
}

/** @brief 分隔条是不是竖的（左右分栏）：format 缺省 "col" 是竖条，"row" 才是横条
 *  @param w splitter 控件；format == "row" 视为横条
 *  @return int 非 0 = 竖条，0 = 横条
 */
int cgw_splitter_is_vert(const cg_widget_t *w)
{
    return !(w->format && strcmp(w->format, "row") == 0);
}

/* ------------------------------------------------ 输入框的「输入方式」限制
 * 输入方式不只是样式位：整数/小数/字节/日期这些档位要在**按键**和**失焦**两处把关。
 * 规则（写给使用者的版本在 docs/CGUI-FORMAT.md 的 input 字段）：
 *   按键：只放行这类值可能出现的字符（数字、负号、小数点、日期分隔符），其余吞掉；
 *   失焦：能解析就夹到该类型的范围里重写，解析不了就清空（"没填"是合法状态，不弹窗）。
 * 两处都只动文本、不派发 change —— 本工程的规矩是"程序改文本不触发回调"。 */

/** @brief "YYYY-MM-DD" 或 "YYYY-MM-DD HH:MM" 的形状检查（不查日历真假，那是业务的事）
 *  @param s 待校验的宽字符串
 *  @return int 非 0 = 形状合法（长度 10 或 16），0 = 不合法
 */
static int cgw_edit_date_ok(const wchar_t *s)
{
    int i;
    for (i = 0; s[i]; i++) {
        if (i < 10) {
            if (i == 4 || i == 7) {
                if (s[i] != L'-') return 0;
            } else if (s[i] < L'0' || s[i] > L'9') {
                return 0;
            }
        } else if (i == 10) {
            if (s[i] != L' ') return 0;
        } else if (i < 16) {
            if (i == 13) {
                if (s[i] != L':') return 0;
            } else if (s[i] < L'0' || s[i] > L'9') {
                return 0;
            }
        } else {
            return 0;                        /* 超过 16 个字符一律不算 */
        }
    }
    return (i == 10 || i == 16);
}

/** @brief 校验/过滤用的输入方式档位。
 *  数值输入框（edit + 调节器）没写 input 时按「整数」办：value 是整数，
 *  放任字母进来只会被 wcstol 解析成 0（原来独立类型 spinner 是写死 ES_NUMBER 挡住的）。
 *  作者显式写了档位（含只读 1 / 密码 2 / 整数文本 3）就听作者的。
 *  @param w edit 控件；input 为 0 且属数值框时按整数档位
 *  @return int 输入方式档位
 */
static int edit_input_mode(const cg_widget_t *w)
{
    if (cg_widget_is_numedit(w) && w->input == 0) return 7;   /* 整数 */
    return w->input;
}

/** @brief 这个字符要不要吞掉（由 cg_win_event.c 的 WM_CHAR 调用）
 *  @param w 目标控件；非 edit 或档位 < 4 一律放行
 *  @param hwnd 控件窗口；用于读光标位置与已输入文本
 *  @param ch 待判定的输入字符（UTF-16 码元）
 *  @return int 非 0 = 吞掉，0 = 放行
 */
int cgw_edit_char_reject(const cg_widget_t *w, HWND hwnd, wchar_t ch)
{
    DWORD   sel_a = 0, sel_b = 0;
    wchar_t buf[64];
    int     mode, signed_ok, dot_ok;

    if (!w || strcmp(w->type, "edit")) return 0;
    mode = edit_input_mode(w);
    if (mode < 4) return 0;                  /* 通常/只读/密码/整数文本：不在这里拦 */
    if (ch < 32) return 0;                   /* 退格 / 回车 / 粘贴用的控制字符一律放行 */
    if (ch >= L'0' && ch <= L'9') return 0;

    signed_ok = (mode == 4 || mode == 6 || mode == 7 ||
                 mode == 8 || mode == 9 || mode == 10);
    dot_ok    = (mode == 4 || mode == 9 || mode == 10);

    if (signed_ok && ch == L'-') {
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&sel_a, (LPARAM)&sel_b);
        return sel_a == 0 ? 0 : 1;           /* 负号只能出现在最前面 */
    }
    if (dot_ok && ch == L'.') {
        if (GetWindowTextW(hwnd, buf, 64) > 0 && wcschr(buf, L'.')) return 1;  /* 只许一个小数点 */
        return 0;
    }
    if (mode == 11 && (ch == L'-' || ch == L':' || ch == L' ')) return 0;
    return 1;
}

/** @brief 失焦时校验文本：越界夹回、非法清空（由 cg_win_notify.c 的 EN_KILLFOCUS 调用）
 *  @param w 目标控件；非 edit 或档位 < 4 不校验
 *  @param hwnd 控件窗口；文本以它为读写来源与去处
 *  @return void 无返回值
 */
void cgw_edit_validate(cg_widget_t *w, HWND hwnd)
{
    wchar_t buf[128];
    int     len, mode;

    if (!w || !hwnd || strcmp(w->type, "edit")) return;
    mode = edit_input_mode(w);
    if (mode < 4) return;
    len = GetWindowTextW(hwnd, buf, 128);
    if (len <= 0 || len >= (int)(sizeof(buf) / sizeof(buf[0])) - 1) return;  /* 超长不入校验，防溢出 */

    if (mode == 11) {
        if (!cgw_edit_date_ok(buf)) SetWindowTextW(hwnd, L"");
        return;
    }
    if (mode == 4 || mode == 9 || mode == 10) {
        wchar_t *end = NULL;
        wcstod(buf, &end);
        if (!end || *end) SetWindowTextW(hwnd, L"");   /* 还剩字符 = 不是个数 */
        return;
    }
    {
        long long lo = LLONG_MIN, hi = LLONG_MAX, v;
        wchar_t  *end = NULL;
        wchar_t   nb[32];
        switch (mode) {
        case 5: lo = 0;                    hi = 255;                 break;   /* 字节 */
        case 6: lo = -32768;               hi = 32767;               break;   /* 短整数 */
        case 7: lo = -2147483647LL - 1;    hi = 2147483647LL;        break;   /* 整数 */
        default: break;                                                        /* 8 长整数 */
        }
        v = wcstoll(buf, &end, 10);
        if (!end || *end) {
            SetWindowTextW(hwnd, L"");
            return;
        }
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        swprintf(nb, 32, L"%lld", v);
        if (wcscmp(nb, buf) != 0) SetWindowTextW(hwnd, nb);   /* 夹过/去过空格才重写 */
    }
}

/** @brief 控件 -> WS 样式位（table 按 multi 决定单/多选）
 *  @param w 目标控件；type 与其它字段共同决定样式位
 *  @return DWORD WS_* 及控件类样式位的按位组合
 */
DWORD cgw_style_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "button"))    return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;  /* M3 圆角自绘 */
    if (!strcmp(w->type, "menubutton"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    if (!strcmp(w->type, "iconbutton"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "link"))      return WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE;
    if (!strcmp(w->type, "monthcal"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | MCS_NOTODAYCIRCLE;
    if (!strcmp(w->type, "progressring")) return WS_CHILD | WS_VISIBLE;
    /* 分隔条：自绘、不需要 WS_TABSTOP（它是鼠标用的，别占 Tab 遍历） */
    if (!strcmp(w->type, "splitter"))  return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "chart"))     return WS_CHILD | WS_VISIBLE;   /* 自绘，纯显示 */
    if (!strcmp(w->type, "led"))       return WS_CHILD | WS_VISIBLE;   /* 自绘，纯显示 */
    if (!strcmp(w->type, "meter"))     return WS_CHILD | WS_VISIBLE;   /* 自绘，纯显示 */
    if (!strcmp(w->type, "richtext"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    /* 终端：只读（内容由 cg_term_write 控制）、要能选中（ES_NOHIDESEL 免得失焦看不清选中） */
    if (!strcmp(w->type, "terminal"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
                                              ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_VSCROLL;
    if (!strcmp(w->type, "datepicker"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                              (cgw_is_time_only(w) ? DTS_TIMEFORMAT : DTS_SHORTDATEFORMAT);
    if (!strcmp(w->type, "toolbar"))   return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
                                              TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER;
    if (!strcmp(w->type, "label")) {
        /* SS_CENTERIMAGE：单行文本垂直居中（水平仍按 align，默认左对齐）——和画布一致 */
        DWORD s = WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_CENTERIMAGE;
        s |= (w->style.align == CG_ALIGN_CENTER) ? SS_CENTER
           : (w->style.align == CG_ALIGN_RIGHT)  ? SS_RIGHT : SS_LEFT;
        return s;
    }
    if (!strcmp(w->type, "edit")) {
        /* 单行/多行、滚动条、对齐、大小写转换、只读/密码/整数，全部由 .cgui 字段决定；
         * 字段全缺省 = 单行左对齐输入框。多行=原来的 multiedit，调节器（数值框）=
         * 原来的 spinner（箭头是独立窗口，见 cg_win_create.c）。
         * 对齐复用 style.align（和标签是同一个字段）。 */
        DWORD s = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        s |= (w->style.align == CG_ALIGN_CENTER) ? ES_CENTER
           : (w->style.align == CG_ALIGN_RIGHT)  ? ES_RIGHT : ES_LEFT;
        if (w->multiline) s |= ES_MULTILINE | ES_WANTRETURN;
        if (w->scroll & 1) s |= ES_AUTOHSCROLL | WS_HSCROLL;
        else if (!w->multiline) s |= ES_AUTOHSCROLL;   /* 单行默认：文字长了横向滚动 */
        if (w->scroll & 2) s |= ES_AUTOVSCROLL | WS_VSCROLL;
        if (w->conv == 1) s |= ES_LOWERCASE;
        else if (w->conv == 2) s |= ES_UPPERCASE;
        if (w->input == 1) s |= ES_READONLY;
        else if (w->input == 2) s |= ES_PASSWORD;
        else if (w->input == 3) s |= ES_NUMBER;
        return s;
    }
    if (!strcmp(w->type, "checkbox"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;  /* 自绘勾选框 */
    if (!strcmp(w->type, "radio"))     return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | WS_GROUP;  /* 自绘单选圆，WS_GROUP 标记组首 */
    if (!strcmp(w->type, "listbox"))   return WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY;
    if (!strcmp(w->type, "combo"))     return WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
    if (!strcmp(w->type, "slider"))    return WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS;
    if (!strcmp(w->type, "progress"))  return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "separator")) return WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ;
    if (!strcmp(w->type, "image"))     return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "switch"))    return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "tabs"))      return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP | TCS_HOTTRACK;
    if (!strcmp(w->type, "tree"))      return WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
    if (!strcmp(w->type, "table"))     return WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS |
                                              (w->multi ? 0 : LVS_SINGLESEL);
    if (!strcmp(w->type, "canvas"))    return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "colorpicker")) return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "badge"))     return WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    if (!strcmp(w->type, "tag"))       return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "rate"))      return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "pagination"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "browser"))   return g_win_atl_ok ? (WS_CHILD | WS_VISIBLE | WS_TABSTOP)
                                                       : (WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE);
    if (!strcmp(w->type, "statusbar")) return WS_CHILD | WS_VISIBLE;
    /* group / box：自己注册的面板类，能装子控件。
     * 边框和标题都自己画，**不能用 WS_BORDER** —— 系统重绘非客户区时会盖掉标题，
     * 看起来就是"边框线穿过文字"。 */
    return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
}

/** @brief 控件 -> 扩展样式位（输入类控件给立体边框）
 *  @param w 目标控件；type 决定扩展样式位
 *  @return DWORD WS_EX_* 的按位组合，0 表示无扩展样式
 */
DWORD cgw_exstyle_of(const cg_widget_t *w)
{
    /* edit 的边框由 cg_skin 自绘（六种边框样式 + 聚焦紫），
     * WS_EX_CLIENTEDGE 的凹陷立体边和圆角裁切叠加会把内容切没 */
    if (!strcmp(w->type, "edit"))    return 0;
    if (!strcmp(w->type, "richtext")) return WS_EX_CLIENTEDGE;
    if (!strcmp(w->type, "listbox")) return WS_EX_CLIENTEDGE;
    /* 容器类要给 WS_EX_CONTROLPARENT：键盘导航（Tab / 方向键）由 IsDialogMessage
     * 驱动，它只在"父是控制父"时才递归枚举容器里的子窗口 —— 少了这一位，
     * Tab 焦点永远进不了 group / box / card / shape / tabs 里的按钮和输入框 */
    if (!strcmp(w->type, "group") || !strcmp(w->type, "box") ||
        !strcmp(w->type, "card") || !strcmp(w->type, "shape") ||
        !strcmp(w->type, "tabs")) return WS_EX_CONTROLPARENT;
    return 0;
}

/* ------------------------------------------------------------ 控件背景（WM_CTLCOLOR*） */

/* 系统色索引最大到 30（COLOR_MENUBAR）；比它大的值一定是真画刷句柄。
 * MinGW 的 winuser.h 没定义 COLOR_ENDCOLORS，这里补一个。 */
#ifndef COLOR_ENDCOLORS
#define COLOR_ENDCOLORS 30
#endif

/** @brief COLORREF(0x00BBGGRR) -> 0xRRGGBB
 *  @param c 系统 COLORREF，红/绿/蓝各 8 位
 *  @return unsigned 0xRRGGBB
 */
static unsigned rgb_of(COLORREF c)
{
    return ((unsigned)GetRValue(c) << 16) | ((unsigned)GetGValue(c) << 8) | (unsigned)GetBValue(c);
}

/** @brief 让父容器"自己画"出背后那一小块，取它的真实颜色
 * @param parent 父容器窗口；目标 DC 与页体矩形都取自它
 * @param child 子控件窗口；采样时避开它及其它可见子窗口覆盖的点
 * @param out 输出：采样到的颜色（COLORREF），仅成功时写入
 * @return int 非 0 = 采样成功，0 = 无可用采样点或取 DC 失败
 * @details 标签页的页体是主题画的（本机 #F9F9F9）：既不是类背景刷（BTNFACE 灰）也不是
 * COLOR_WINDOW（纯白），而且它裁掉子窗口（子控件下面什么都没画，所以不能"不画背景"）。
 * 与其猜颜色（uxtheme 的 GetThemeColor 对 TAB 返回的是没用的高亮色），不如让父窗口
 * 把子控件中心那一小块画进 1x1 位图、读像素 —— 什么主题、什么渐变都对得上。
 * 结果按父窗口缓存，一个容器只采样一次。
 */
static int parent_paint_point(HWND parent, HWND child, COLORREF *out)
{
    RECT disp;
    POINT cand[6];
    HDC dc;
    int i;

    GetClientRect(parent, &disp);
    SendMessageW(parent, TCM_ADJUSTRECT, FALSE, (LPARAM)&disp);   /* 去掉标签行 -> 页体矩形 */
    if (disp.right - disp.left < 8 || disp.bottom - disp.top < 8) return 0;

    /* 候选点：页体四角往里缩一点（这些位置基本没有子控件），避开子控件区域采样 */
    cand[0].x = disp.left + 3;                cand[0].y = disp.bottom - 3;
    cand[1].x = disp.right - 4;               cand[1].y = disp.bottom - 3;
    cand[2].x = disp.right - 4;               cand[2].y = disp.top + 3;
    cand[3].x = disp.left + 3;                cand[3].y = disp.top + 3;
    cand[4].x = (disp.left + disp.right) / 2; cand[4].y = disp.bottom - 3;
    cand[5].x = (disp.left + disp.right) / 2; cand[5].y = disp.top + 3;

    dc = GetDC(parent);   /* 父窗口的 DC：能直接读到它已经画好的页体像素 */
    if (!dc) return 0;
    for (i = 0; i < 6; i++) {
        HWND c;
        int covered = 0;
        for (c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            RECT wr;
            if (c == child || !IsWindowVisible(c)) continue;
            GetWindowRect(c, &wr);
            MapWindowPoints(NULL, parent, (POINT *)&wr, 2);
            if (cand[i].x >= wr.left && cand[i].x < wr.right &&
                cand[i].y >= wr.top && cand[i].y < wr.bottom) {
                covered = 1;
                break;
            }
        }
        if (!covered) {
            COLORREF c2 = GetPixel(dc, cand[i].x, cand[i].y);
            if (c2 != CLR_INVALID) {
                ReleaseDC(parent, dc);
                *out = c2;
                return 1;
            }
        }
    }
    ReleaseDC(parent, dc);
    return 0;
}

/** @brief 某个父容器"背后"的颜色（按父窗口缓存，一个容器只采样一次）
 *  @param parent 父容器窗口；同时用作缓存键
 *  @param child 子控件窗口；转给采样函数以避开它覆盖的点
 *  @return unsigned 0xRRGGBB；采样失败退回纯白
 */
static struct { HWND parent; unsigned rgb; } s_behind_cache[8];
static int s_behind_cache_used = 0;

/**
 * @brief 清掉父容器"背后色"的采样缓存（换主题前后必调）
 * @return void 无返回值
 * @note 缓存里存的是**采样当时**的像素值，命中即返回、永不失效 —— 切主题后挂在 tabs 页体里的
 *       无底色控件会一直停在旧主题的底色上（2026-09-28 排查发现的真缺陷）。重采样代价极低，
 *       而换主题本身是低频操作，所以换主题时直接清掉最省事
 */
void cgw_clear_behind_cache(void)
{
    s_behind_cache_used = 0;
}

/**
 * @brief 取某个子控件"背后"那片父容器的像素色（无底色控件的兜底背景用）
 * @param parent 父窗口（典型是 tabs 页体这类系统容器）
 * @param child 子控件窗口
 * @return unsigned 0xRRGGBB；采样失败回退白色
 * @note 结果按父窗口缓存（最多 8 条），命中即返回；**换主题时必须由 cgw_clear_behind_cache()
 *       清掉**，否则页体里的控件会停在旧主题的底色上
 */
static unsigned parent_body_rgb(HWND parent, HWND child)
{
    COLORREF c;
    unsigned rgb;
    int i;

    for (i = 0; i < s_behind_cache_used; i++) {
        if (s_behind_cache[i].parent == parent) return s_behind_cache[i].rgb;
    }
    rgb = 0xFFFFFF;
    if (parent_paint_point(parent, child, &c)) rgb = rgb_of(c);
    if (s_behind_cache_used < 8) {
        s_behind_cache[s_behind_cache_used].parent = parent;
        s_behind_cache[s_behind_cache_used].rgb = rgb;
        s_behind_cache_used++;
    }
    return rgb;
}

/**
 * @brief 某个控件"背后"是什么颜色（0xRRGGBB）
 * @param app 所属应用；取主窗口底色与主题，NULL 时给默认面色
 * @param hwnd 目标控件窗口；按它的**直接父窗口**判断"背后"是谁
 * @return unsigned 0xRRGGBB 颜色值
 * @note 主窗口上的控件 -> 窗口底色；分组框 / 容器块 / 卡片 / 图形里的 -> 该面板的底色
 *       （没设就顺着父链继续往上看）；标签页里的 -> 采样页体真实像素。
 *       无底色标签、以及"清掉自身底色"的自绘按钮 / 开关 / 勾选框都用它，
 *       否则它们会把窗口底色画到带底色的面板上，露出一块方块
 */
unsigned cg_win_behind_rgb(cg_app_t *app, void *hwnd)
{
    cg_win_state_t *st;
    HWND child = (HWND)hwnd;
    HWND parent = child ? GetParent(child) : NULL;
    cg_widget_t *pw;

    /* app 为空 = 自绘控件在**建窗口的过程中**就收到了一次 WM_PAINT（运行时还没把
     * GWLP_USERDATA 挂上）。这不是错误状态，只是还问不出"背后是谁"，给个默认面色就行 ——
     * 少了这一句，任何自绘控件的首帧都会在 app->natives 上访问冲突。 */
    if (!app) return 0xF3EDF7u;
    st = (cg_win_state_t *)app->natives;
    if (!st) return 0xF3EDF7u;
    if (!parent || parent == (HWND)app->hwnd) return rgb_of(st->bg_color);

    pw = cg_widget_by_hwnd(app, parent);
    if (pw && (cgw_is_t(pw, "group") || cgw_is_t(pw, "box") || cgw_is_t(pw, "card") || cgw_is_t(pw, "shape"))) {
        if (pw->style.bg != CG_COLOR_NONE) return pw->style.bg;
        if (cgw_is_t(pw, "card")) {
            /* 卡片没设底色时自己填的就是系统面色（panel_draw_card_shape 的 facergb 分支），
             * 子控件的"背后"必须跟它对齐 —— 否则卡片里的无底色 label 按递归拿到页面色，
             * 灰卡上糊一块页面色方块（2026-09-25 用户截图：卡片里的标签是页面青色底）。
             * 深色下这个面色也跟着变深（色调由 cg_win_theme.c 给）。 */
            return cgw_theme_card_face(app);
        }
        /* 父容器没设底色：递归问"它背后是什么"（容器自己画底色也走这条递归 ——
         * cg_win_draw_panel.c 的 panel_draw_group 就是调 cg_win_behind_rgb()）。不能去拿窗口类背景刷 —— 那是面板类
         * 注册的 COLOR_BTNFACE+1(#F0F0F0)，口径和容器自绘对不上，容器里每个无底色
         * label / separator / badge 底下就会露一块系统灰。 */
        return cg_win_behind_rgb(app, parent);
    }
    /* 标签页等系统容器：设了背景色就用它（cgw_FocusProc 会把页体填成这个色），
     * 没设才让页体自己把"背后那块"画出来取色（什么主题都对得上） */
    if (pw && cgw_is_t(pw, "tabs")) {
        if (pw->style.bg != CG_COLOR_NONE) return pw->style.bg;
        return parent_body_rgb(parent, child);
    }

    return rgb_of(st->bg_color);
}

/** @brief 无底色标签该用哪个背景刷（= "背后"的颜色）
 *  @param app 所属应用；转给"背后色"查询
 *  @param child 目标控件窗口；按它的**直接父窗口**判断"背后"
 *  @return HBRUSH 直接当 WM_CTLCOLOR* 的返回值用（共享缓存，勿删）
 */
static HBRUSH transparent_bg_for(cg_app_t *app, HWND child)
{
    return cgw_brush_for(cg_win_behind_rgb(app, child));
}

/** @brief 自绘控件画自己的内容前，先把整块填成"背后"的颜色
 *  @param w 目标控件；用于查它"背后"的颜色
 *  @param dc 目标设备上下文；在它上面填充
 *  @param rc 要填充的矩形区域（左上右下像素坐标）
 *  @return void 无返回值
 *  @details owner-draw 按钮/自绘控件默认会铺一层系统面色（BTNFACE），不清掉就是一块灰底；
 *           徽章 / 标签片 / 评分这类"只在中间画一块"的控件必须清，"背后"是什么就填什么。
 */
void cgw_clear_to_behind(cg_widget_t *w, HDC dc, const RECT *rc)
{
    FillRect(dc, rc, cgw_brush_for(cg_win_behind_rgb(w->app, w->native)));
}

/** @brief WM_CTLCOLOR* 的共用处理：决定控件文字/背景色该用哪个画刷
 *  @param app 所属应用；用于查主窗口状态与主题色
 *  @param wp 目标 DC（WM_CTLCOLOR* 的 wParam）
 *  @param lp 那个控件窗口（WM_CTLCOLOR* 的 lParam）
 *  @return HBRUSH NULL = 不处理（交给系统默认），否则返回值该直接当 LRESULT 返回
 *  @details WM_CTLCOLOR* 只发给控件的**直接父窗口**，所以控件放进分组框/标签页后
 *           消息会落到容器上；cgw_PanelProc / cgw_FocusProc 必须把它转发到主窗口走这里，
 *           否则容器里的标签会退回系统默认灰底（看不到"透明"效果）。
 */
HBRUSH cgw_ctlcolor_brush(cg_app_t *app, WPARAM wp, LPARAM lp)
{
    HWND child = (HWND)lp;
    cg_widget_t *w;
    HDC dc;

    if (!app || !child) return NULL;
    w = cg_widget_by_ctrl_id(app, GetDlgCtrlID(child));
    if (!w) return NULL;
    dc = (HDC)wp;

    if (w->style.fg != CG_COLOR_NONE) {
        SetTextColor(dc, cgw_to_colorref(w->style.fg));
    } else if (cgw_is_t(w, "link")) {
        SetTextColor(dc, RGB(0x0B, 0x57, 0xD0));   /* 链接默认蓝，没设颜色时用 */
    } else {
        /* 没写 fg 的输入框/标签：深色下必须给浅字，否则深底黑字等于看不见。
         * 浅色下 cgw_theme_text() 返回 CG_COLOR_NONE = 这一支什么都不做（保持系统默认）。 */
        unsigned fg = cgw_theme_text(app);

        if (fg != CG_COLOR_NONE) SetTextColor(dc, cgw_to_colorref(fg));
    }
    if (w->style.bg != CG_COLOR_NONE) {
        SetBkColor(dc, cgw_to_colorref(w->style.bg));
        SetBkMode(dc, OPAQUE);
        return cgw_brush_for(w->style.bg);
    }
    /* 没设背景色：标签 / 链接 / 分割线默认透明，透出"背后"的颜色
     * （要底色就在属性里设「背景色」；edit 类由系统画白底，不动它） */
    if (cgw_is_t(w, "label") || cgw_is_t(w, "link") || cgw_is_t(w, "separator")) {
        SetBkMode(dc, TRANSPARENT);
        return transparent_bg_for(app, child);
    }
    return NULL;
}

/* ------------------------------------------------------------ 日期时间
 * text 里的写法：date -> "2026-09-19"、time -> "14:30"、datetime -> "2026-09-19 14:30"
 */
/**
 * @brief 把 text 里的日期时间写法解析成 SYSTEMTIME（缺省用今天/现在）
 * @param w datepicker 控件；text/format 提供要解析的写法
 * @param date 输出：日期部分；先填本地当天，解析成功再覆盖
 * @param time 输出：时间部分；先与 date 同值，解析成功再覆盖
 * @return void 无返回值
 */
void cgw_datetime_from_text(const cg_widget_t *w, SYSTEMTIME *date, SYSTEMTIME *time)
{
    char buf[64];
    int  y = 0, mo = 0, d = 0, h = 0, mi = 0;

    GetLocalTime(date);
    *time = *date;
    if (!w->text || !*w->text) return;
    snprintf(buf, sizeof(buf), "%s", w->text);

    if (cgw_is_time_only(w)) {
        if (sscanf(buf, "%d:%d", &h, &mi) == 2) {
            time->wHour = (WORD)h;
            time->wMinute = (WORD)mi;
            date->wHour = (WORD)h;
            date->wMinute = (WORD)mi;
        }
        return;
    }

    {
        int n = sscanf(buf, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
        if (n >= 3) {
            date->wYear = (WORD)y;
            date->wMonth = (WORD)mo;
            date->wDay = (WORD)d;
            time->wYear = (WORD)y;
            time->wMonth = (WORD)mo;
            time->wDay = (WORD)d;
        }
        if (n == 5) {
            time->wHour = (WORD)h;
            time->wMinute = (WORD)mi;
        }
    }
}

/** @brief 把控件里的日期时间写回 text（回调里用 cg_widget_text 读）
 *  @param w datepicker 控件；写回的 text 供 cg_widget_text 读取
 *  @param date_hwnd 日期控件窗口；取它当前的 SYSTEMTIME，为 NULL 直接返回
 *  @param time_hwnd 时间控件窗口；datetime 模式取它的时间，其余模式可为 NULL
 *  @return void 无返回值
 */
void cgw_datetime_pull_text(cg_widget_t *w, HWND date_hwnd, HWND time_hwnd)
{
    SYSTEMTIME d, t;
    char buf[64];

    if (!date_hwnd) return;
    memset(&d, 0, sizeof(d));
    memset(&t, 0, sizeof(t));
    if (SendMessageW(date_hwnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&d) != GDT_VALID) return;

    if (cgw_is_time_only(w)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", d.wHour, d.wMinute);
    } else if (cgw_is_datetime(w) && time_hwnd &&
               SendMessageW(time_hwnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&t) == GDT_VALID) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                 d.wYear, d.wMonth, d.wDay, t.wHour, t.wMinute);
    } else {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.wYear, d.wMonth, d.wDay);
    }
    cg_widget_set_text(w, buf);
}
