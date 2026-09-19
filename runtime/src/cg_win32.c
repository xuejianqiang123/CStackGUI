/* cg_win32.c -- Win32 原生后端
 *
 * 设计取舍：v1 直接把 .cgui 的每个控件映射成一个标准 Win32 控件。
 * 好处是零依赖、真单文件 exe、无障碍与输入法全部白送；
 * 代价是原生控件不支持圆角/自定义配色（style.radius 目前只在自绘后端生效）。
 * 后续要 M3 那种外观时，只需再加一个 backend（owner-draw / D2D），
 * cg_doc / cg_app 与业务代码都不用动。
 */
/* 明确走 W 系列 API */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 字符串 */

static wchar_t *wc_from_utf8(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/* ------------------------------------------------------------ 字体缓存 */

typedef struct cg_font_node {
    char          *key;
    HFONT          hfont;
    struct cg_font_node *next;
} cg_font_node_t;

typedef struct {
    cg_font_node_t *fonts;
    HBRUSH          bg_brush;
    COLORREF        bg_color;
    int             dpi;
    int             focused_widget;
    UINT_PTR        timer_id;
    HWND            tooltip;    /* 整窗一个的悬停提示控件 */
} cg_win_state_t;

#define CG_PANEL_CLASS L"CStackGUIPanel"
#define CG_IMAGE_CLASS L"CStackGUIImage"
#define CG_RING_CLASS  L"CStackGUIRing"
#define CG_CANVAS_CLASS L"CStackGUICanvas"
#define CG_TIMER_ID    1

/* 时钟（timer）控件没有自己的窗口，Win32 定时器 id 直接复用 ctrl_id，
 * 这样 WM_TIMER 里按 wp 反查一次就能找到控件。 */

/* app->scale 用百分比存（100 = 96dpi），便于整数运算 */
static int scale_px(const cg_app_t *app, int logical)
{
    int s = app->scale > 0 ? app->scale : 100;
    return (int)(((long long)logical * s + 50) / 100);
}

static HFONT font_for(cg_app_t *app, const char *family, int size_pt, int bold, int underline)
{
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    char key[128];
    int  height;
    cg_font_node_t *node;

    if (!st) return NULL;
    if (!family || !*family) family = "Microsoft YaHei UI";
    if (size_pt <= 0) {
        /* 不指定就跟随系统 UI 字体，交给 DEFAULT_GUI_FONT */
        return NULL;
    }
    height = -MulDiv(size_pt, st->dpi, 72);

    snprintf(key, sizeof(key), "%s|%d|%d|%d", family, size_pt, bold ? 1 : 0, underline ? 1 : 0);
    for (node = st->fonts; node; node = node->next) {
        if (strcmp(node->key, key) == 0) return node->hfont;
    }

    node = (cg_font_node_t *)calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->key = _strdup(key);
    {
        wchar_t *wf = wc_from_utf8(family);
        node->hfont = CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                                  0, underline ? TRUE : FALSE, 0,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                  wf ? wf : L"Microsoft YaHei UI");
        free(wf);
    }
    node->next = st->fonts;
    st->fonts = node;
    return node->hfont;
}

/* ------------------------------------------------------------ 控件映射 */

/** 富文本控件类：Msftedit.dll 起来后用 RICHEDIT50W，否则退回旧版甚至普通 EDIT。
 *  富文本不是系统的必备组件，所以这里做降级而不是直接失败。 */
static const wchar_t *g_richedit_class = NULL;

/** browser 控件用 atl.dll 的 AtlAxWin 宿主系统 WebBrowser 控件；
 *  起不来（理论上看不到）就退回静态占位文本。 */
static int g_atl_ok = 0;

static const wchar_t *class_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "button"))    return L"BUTTON";
    if (!strcmp(w->type, "menubutton"))return L"BUTTON";
    if (!strcmp(w->type, "iconbutton"))return L"BUTTON";      /* BS_OWNERDRAW 画图标 */
    if (!strcmp(w->type, "label"))     return L"STATIC";
    if (!strcmp(w->type, "link"))      return L"STATIC";      /* SS_NOTIFY 才有 STN_CLICKED */
    if (!strcmp(w->type, "edit"))      return L"EDIT";
    if (!strcmp(w->type, "multiedit")) return L"EDIT";
    if (!strcmp(w->type, "richtext"))  return g_richedit_class ? g_richedit_class : L"EDIT";
    if (!strcmp(w->type, "checkbox"))  return L"BUTTON";
    if (!strcmp(w->type, "radio"))     return L"BUTTON";
    if (!strcmp(w->type, "switch"))    return L"BUTTON";      /* BS_OWNERDRAW 自绘 */
    if (!strcmp(w->type, "listbox"))   return L"LISTBOX";
    if (!strcmp(w->type, "combo"))     return L"COMBOBOX";
    if (!strcmp(w->type, "slider"))    return TRACKBAR_CLASSW;
    if (!strcmp(w->type, "progress"))  return PROGRESS_CLASSW;
    if (!strcmp(w->type, "separator")) return L"STATIC";
    if (!strcmp(w->type, "image"))     return CG_IMAGE_CLASS;
    if (!strcmp(w->type, "tabs"))      return WC_TABCONTROLW;
    if (!strcmp(w->type, "tree"))      return WC_TREEVIEWW;
    if (!strcmp(w->type, "table"))     return WC_LISTVIEWW;
    if (!strcmp(w->type, "statusbar")) return STATUSCLASSNAMEW;
    if (!strcmp(w->type, "spinner"))   return L"EDIT";        /* 箭头是 native2 */
    if (!strcmp(w->type, "datepicker"))return DATETIMEPICK_CLASSW;
    if (!strcmp(w->type, "toolbar"))   return TOOLBARCLASSNAMEW;
    if (!strcmp(w->type, "monthcal"))  return MONTHCAL_CLASSW;
    if (!strcmp(w->type, "progressring")) return CG_RING_CLASS;   /* 自绘圆环 */
    if (!strcmp(w->type, "canvas"))    return CG_CANVAS_CLASS;    /* 自绘画板 */
    if (!strcmp(w->type, "colorpicker")) return L"BUTTON";        /* BS_OWNERDRAW 色块 */
    if (!strcmp(w->type, "badge"))     return L"BUTTON";          /* BS_OWNERDRAW 小圆片 */
    if (!strcmp(w->type, "tag"))       return L"BUTTON";          /* BS_OWNERDRAW 标签片 */
    if (!strcmp(w->type, "rate"))      return L"BUTTON";          /* BS_OWNERDRAW 五角星 */
    if (!strcmp(w->type, "pagination"))return L"BUTTON";          /* BS_OWNERDRAW 分页 */
    if (!strcmp(w->type, "browser"))   return g_atl_ok ? L"AtlAxWin" : L"STATIC";
    if (!strcmp(w->type, "timer"))     return L"STATIC";          /* 无窗口，create_native 会跳过 */
    if (!strcmp(w->type, "card") || !strcmp(w->type, "shape")) return CG_PANEL_CLASS;
    return CG_PANEL_CLASS;   /* group / box */
}

/** 日期时间：是"日期"还是"时间"，决定 DTS_* 样式 */
static int is_time_only(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "time") == 0;
}

static int is_datetime(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "datetime") == 0;
}

static DWORD style_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "button"))    return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    if (!strcmp(w->type, "menubutton"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    if (!strcmp(w->type, "iconbutton"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
    if (!strcmp(w->type, "link"))      return WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_LEFTNOWORDWRAP;
    if (!strcmp(w->type, "monthcal"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | MCS_NOTODAYCIRCLE;
    if (!strcmp(w->type, "progressring")) return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "richtext"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    if (!strcmp(w->type, "datepicker"))return WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                              (is_time_only(w) ? DTS_TIMEFORMAT : DTS_SHORTDATEFORMAT);
    if (!strcmp(w->type, "toolbar"))   return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
                                              TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER;
    if (!strcmp(w->type, "label")) {
        DWORD s = WS_CHILD | WS_VISIBLE | SS_NOPREFIX;
        s |= (w->style.align == CG_ALIGN_CENTER) ? SS_CENTER
           : (w->style.align == CG_ALIGN_RIGHT)  ? SS_RIGHT : SS_LEFT;
        return s;
    }
    if (!strcmp(w->type, "edit"))      return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT;
    if (!strcmp(w->type, "multiedit")) return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
    if (!strcmp(w->type, "checkbox"))  return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
    if (!strcmp(w->type, "radio"))     return WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP;
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
    if (!strcmp(w->type, "browser"))   return g_atl_ok ? (WS_CHILD | WS_VISIBLE | WS_TABSTOP)
                                                       : (WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE);
    if (!strcmp(w->type, "statusbar")) return WS_CHILD | WS_VISIBLE;
    if (!strcmp(w->type, "spinner"))   return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL;
    /* group / box：自己注册的面板类，能装子控件 */
    return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_BORDER;
}

static DWORD exstyle_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "edit"))    return WS_EX_CLIENTEDGE;
    if (!strcmp(w->type, "multiedit")) return WS_EX_CLIENTEDGE;
    if (!strcmp(w->type, "richtext")) return WS_EX_CLIENTEDGE;
    if (!strcmp(w->type, "listbox")) return WS_EX_CLIENTEDGE;
    return 0;
}


/* ------------------------------------------------------------ 颜色 */

static COLORREF to_colorref(unsigned c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

/** 一组小画刷缓存，给 WM_CTLCOLOR* 返回用；颜色数量有限，链表足够 */
static HBRUSH brush_for(unsigned rgb)
{
    static struct { unsigned rgb; HBRUSH br; } cache[16];
    static int used = 0;

    for (int i = 0; i < used; i++) {
        if (cache[i].rgb == rgb) return cache[i].br;
    }
    if (used < 16) {
        cache[used].br = CreateSolidBrush(to_colorref(rgb));
        cache[used].rgb = rgb;
        return cache[used++].br;
    }
    return (HBRUSH)GetStockObject(WHITE_BRUSH);
}

/* ------------------------------------------------------------ 类型判断 */

static int is_t(const cg_widget_t *w, const char *type)
{
    return w && w->type && strcmp(w->type, type) == 0;
}

static int is_editlike(const cg_widget_t *w)
{
    return is_t(w, "edit") || is_t(w, "multiedit") || is_t(w, "spinner") || is_t(w, "richtext");
}

/* ------------------------------------------------------------ 日期时间
 * text 里的写法：date -> "2026-09-19"、time -> "14:30"、datetime -> "2026-09-19 14:30"
 */
static void datetime_from_text(const cg_widget_t *w, SYSTEMTIME *date, SYSTEMTIME *time)
{
    char buf[64];
    int  y = 0, mo = 0, d = 0, h = 0, mi = 0;

    GetLocalTime(date);
    *time = *date;
    if (!w->text || !*w->text) return;
    snprintf(buf, sizeof(buf), "%s", w->text);

    if (is_time_only(w)) {
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

/** 把控件里的日期时间写回 text（回调里用 cg_widget_text 读） */
static void datetime_pull_text(cg_widget_t *w, HWND date_hwnd, HWND time_hwnd)
{
    SYSTEMTIME d, t;
    char buf[64];

    if (!date_hwnd) return;
    memset(&d, 0, sizeof(d));
    memset(&t, 0, sizeof(t));
    if (SendMessageW(date_hwnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&d) != GDT_VALID) return;

    if (is_time_only(w)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", d.wHour, d.wMinute);
    } else if (is_datetime(w) && time_hwnd &&
               SendMessageW(time_hwnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&t) == GDT_VALID) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                 d.wYear, d.wMonth, d.wDay, t.wHour, t.wMinute);
    } else {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.wYear, d.wMonth, d.wDay);
    }
    cg_widget_set_text(w, buf);
}

/* 防重入开关：定义提前到这里，因为下面几个辅助函数要用到它。
 * 含义见「事件映射」那一节。 */
static int g_in_notify = 0;

/**
 * @brief 有控制台时把控制台输入/输出代码页切到 UTF-8
 *
 * printf 的中文是 UTF-8 字节（源码与执行字符集都是 UTF-8），控制台默认
 * 代码页（如 GBK）会显示成乱码。带控制台的 debug 版调用后中文正常显示；
 * 无控制台（-mwindows 的 release 版、输出重定向）时 GetConsoleWindow()
 * 返回 NULL，本函数不做任何事。
 *
 * @param void 无参数
 * @return void 无返回值
 */
void cg_win_console_utf8(void)
{
    if (GetConsoleWindow() != NULL) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
}

static HMENU menu_build(const cg_widget_t *w);   /* 定义在「控件创建」一节 */

/** 菜单按钮：点击时在按钮下方弹出菜单，选中项直接派发（TPM_RETURNCMD 省掉消息路由） */
static void popup_menu_for(cg_app_t *app, cg_widget_t *w, HWND hwnd)
{
    HMENU hm;
    RECT  r;
    int   cmd;
    int   saved = g_in_notify;

    hm = menu_build(w);
    if (!hm || w->nmenu == 0) {
        if (hm) DestroyMenu(hm);
        return;
    }
    GetWindowRect(hwnd, &r);

    /* TrackPopupMenu 自己会跑消息循环：这段时间放开防重入，
     * 否则弹出菜单期间其它控件的事件会被吞掉。 */
    g_in_notify = 0;
    if (app->hwnd) SetForegroundWindow((HWND)app->hwnd);
    cmd = (int)TrackPopupMenu(hm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              r.left, r.bottom, 0, (HWND)app->hwnd, NULL);
    if (app->hwnd) PostMessageW((HWND)app->hwnd, WM_NULL, 0, 0);
    g_in_notify = saved;

    DestroyMenu(hm);
    if (cmd) cg_dispatch_menu_cmd(app, cmd);
}

/** 按原生窗口反查控件：spinner 这类一个控件有两个 HWND，按 ctrl id 找会漏 */
static cg_widget_t *widget_by_hwnd(cg_app_t *app, HWND h)
{
    if (!app || !h) return NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if ((HWND)w->native == h || (HWND)w->native2 == h) return w;
    }
    return NULL;
}

/* ------------------------------------------------------------ 全局焦点子类
 * 标准控件不会主动把焦点变化告诉父窗口（EDIT 有 EN_SETFOCUS，但按钮/滑块/树
 * 什么都没有）。给每个控件套一层 SetWindowSubclass，WM_SETFOCUS / WM_KILLFOCUS
 * 在这里统一转成 gotfocus / lostfocus 事件。
 * 顺手干两件"必须知道鼠标坐标"的活：评分点第几颗星、分页点左半边还是右半边。
 */
static cg_app_t *g_app = NULL;

static void rate_or_pag_click(cg_widget_t *w, HWND hwnd, int x)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    if (width <= 0) return;

    if (is_t(w, "rate")) {
        int stars = w->vmax - w->vmin;
        if (stars < 1) stars = 1;
        int v = w->vmin + (x * stars + width / 2) / width;   /* 落在第几颗星 */
        if (v < w->vmin) v = w->vmin;
        if (v > w->vmax) v = w->vmax;
        if (v != w->value) {
            g_in_notify = 1;
            cg_widget_set_value(w, v);
            cg_dispatch(w, CG_EVENT_CHANGE);
            g_in_notify = 0;
        }
        return;
    }
    /* pagination：左 1/3 上一页，右 1/3 下一页，中间是页码显示 */
    int v = w->value;
    if (x < width / 3)      v = w->value - 1;
    else if (x > width * 2 / 3) v = w->value + 1;
    else return;
    if (v < w->vmin || v > w->vmax) return;   /* 越界的一下当没点着 */
    g_in_notify = 1;
    cg_widget_set_value(w, v);
    cg_dispatch(w, CG_EVENT_CHANGE);
    g_in_notify = 0;
}

static LRESULT CALLBACK FocusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;

    /* 子控件的通知永远先到直接父窗口。放进 tabs / 其它系统控件的容器里后，
     * 父窗口不再是主窗口，这些通知必须转发上去，否则 ownerdraw 按钮
     * （colorpicker/rate/badge/...）画不出来、点击也没反应。
     * PanelProc 对 group/box 做的是同一件事；这里覆盖剩下所有控件。 */
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_HSCROLL:
    case WM_VSCROLL:
        if (g_app && g_app->hwnd && (HWND)g_app->hwnd != hwnd) {
            return SendMessageW((HWND)g_app->hwnd, msg, wp, lp);
        }
        break;
    default:
        break;
    }

    if (!g_in_notify && g_app) {
        cg_widget_t *w = widget_by_hwnd(g_app, hwnd);
        if (w) {
            if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
                cg_event_t ev = (msg == WM_SETFOCUS) ? CG_EVENT_GOTFOCUS : CG_EVENT_LOSTFOCUS;
                if (cg_kind_supports_event(w->type, ev)) {
                    g_in_notify = 1;
                    cg_dispatch(w, ev);
                    g_in_notify = 0;
                }
            } else if (msg == WM_LBUTTONUP &&
                       (is_t(w, "rate") || is_t(w, "pagination"))) {
                rate_or_pag_click(w, hwnd, (short)LOWORD(lp));
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void subclass_for_events(HWND h)
{
    SetWindowSubclass(h, FocusProc, 1, 0);
}

/* ------------------------------------------------------------ 悬停提示（tooltip）
 * 整窗一个 TOOLTIPS 控件；每个有 hint 的控件注册成一个 tool，
 * TTF_SUBCLASS 让 tooltip 自己接管鼠标转发，不需要任何额外消息处理。
 * 文本用 LPSTR_TEXTCALLBACK 现取，cg_widget_set_hint 改完立即生效。
 */
static void tooltip_add_for(HWND tooltip, HWND forw)
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

void cg_win_set_hint(cg_widget_t *w)
{
    if (!w || !w->native || !g_app) return;
    cg_win_state_t *st = (cg_win_state_t *)g_app->natives;
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
        tooltip_add_for(st->tooltip, (HWND)w->native);
    } else if ((!w->hint || !*w->hint) && exists) {
        SendMessageW(st->tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    } else if (exists) {
        SendMessageW(st->tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
}

void cg_win_restart_timer(cg_widget_t *w)
{
    if (!w || !w->app || !w->app->hwnd) return;
    HWND hwnd = (HWND)w->app->hwnd;
    KillTimer(hwnd, (UINT_PTR)w->ctrl_id);
    if (is_t(w, "timer") && w->period > 0) {
        SetTimer(hwnd, (UINT_PTR)w->ctrl_id, (UINT)w->period, NULL);
    }
}

/* ------------------------------------------------------------ GDI+
 * 只用来把 png/jpg/bmp/gif 读成 HBITMAP。
 * MinGW 的 gdiplusflat.h 在 C 下编不过（类型有前置依赖），而我们只用这几个函数，
 * 所以按官方 ABI 手写声明。gdiplus.dll 从 XP 起就是系统组件，不增加分发负担。
 */
typedef int GpStatus;

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} cg_gdip_input_t;

__declspec(dllimport) GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const cg_gdip_input_t *input, void *output);
__declspec(dllimport) void     WINAPI GdiplusShutdown(ULONG_PTR token);
__declspec(dllimport) GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR *filename, void **bitmap);
__declspec(dllimport) GpStatus WINAPI GdipCreateHBITMAPFromBitmap(void *bitmap, HBITMAP *hbm, DWORD back);
__declspec(dllimport) GpStatus WINAPI GdipDisposeImage(void *image);

#define CG_IMAGE_CLASS L"CStackGUIImage"

static ULONG_PTR g_gdip_token = 0;   /* GDI+ 的启动令牌，0 表示没起来 */

/* 本文件里几处互相调用（切页要同步子控件、建控件后要同步），先声明 */
static void sync_widget(cg_widget_t *w);

static int file_exists(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(h);
    return 1;
}

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

/** 打开图片成位图。路径按三处依次找：
 *  1) 原样（相对当前目录）  2) 相对 .cgui 所在目录  3) 相对 exe 所在目录
 *  第 2 条最符合直觉：.cgui 和 assets\ 放在一起，换台机器整个目录搬走也能用。 */
static HBITMAP load_image_file(const cg_app_t *app, const char *path)
{
    char   full[1024];
    char   base[512];
    WCHAR *wpath;
    void  *gdip_bmp = NULL;
    HBITMAP hbmp = NULL;
    int     need;
    int     found = 0;

    if (!path || !*path) return NULL;

    snprintf(full, sizeof(full), "%s", path);
    if (file_exists(full)) {
        found = 1;
    } else {
        if (app && app->cgui_dir) {
            snprintf(full, sizeof(full), "%s\\%s", app->cgui_dir, path);
            if (file_exists(full)) found = 1;
        }
        if (!found) {
            char exe[512], dir[512];
            if (cg_exe_path(exe, sizeof(exe)) == 0) {
                dir_of(exe, dir, sizeof(dir));
                snprintf(full, sizeof(full), "%s\\%s", dir, path);
                if (file_exists(full)) found = 1;
            }
        }
    }
    if (!found) return NULL;

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
    (void)base;
    return hbmp;
}

/** 图片控件：按控件大小 StretchBlt，缺图时画占位框 */
static LRESULT CALLBACK ImageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);

        if (w && w->bitmap) {
            BITMAP bm;
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, (HGDIOBJ)w->bitmap);
            if (GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm)) {
                int prev = SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, NULL);
                StretchBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                           mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SetStretchBltMode(dc, prev);
            }
            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            COLORREF face = GetSysColor(COLOR_BTNFACE);
            HBRUSH br = CreateSolidBrush(face);
            HGDIOBJ oldbr = SelectObject(dc, br);
            HPEN pen = CreatePen(PS_DASH, 1, RGB(120, 120, 128));
            HGDIOBJ oldpen = SelectObject(dc, pen);
            Rectangle(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(dc, oldpen);
            DeleteObject(pen);
            SelectObject(dc, oldbr);
            DeleteObject(br);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(90, 90, 96));
            if (w && w->src && *w->src) {
                wchar_t *wt = wc_from_utf8(w->src);
                if (wt) {
                    DrawTextW(dc, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    free(wt);
                }
            } else {
                const wchar_t *ph = L"图片";
                DrawTextW(dc, ph, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_image_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ImageProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_IMAGE_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 环状进度（自绘）
 * 原生控件里没有圆环进度，这个和 switch 一样是 owner-draw 思路的样板：
 * 自己画在窗口上，所以能吃 style 的颜色，也才能有圆角/圆弧这种原生控件画不出的东西。
 */
#define RING_STEPS 72
#define CG_PI 3.14159265358979

static void draw_ring(HDC dc, const cg_widget_t *w, const RECT *rc)
{
    int  width = rc->right - rc->left;
    int  height = rc->bottom - rc->top;
    int  thick = width < 80 ? 6 : (width < 160 ? 10 : 14);
    int  radius;
    int  cx = (rc->left + rc->right) / 2;
    int  cy = (rc->top + rc->bottom) / 2;
    int  span = (w->vmax > w->vmin) ? (w->vmax - w->vmin) : 1;
    int  val = w->value;
    int  sweep;
    int  fg = (w->style.fg != CG_COLOR_NONE) ? (int)w->style.fg : 0x6750A4;
    HBRUSH  bg;
    HGDIOBJ oldbr, oldpen;
    HPEN    pen;
    wchar_t text[32];
    RECT    tr;

    if (val < w->vmin) val = w->vmin;
    if (val > w->vmax) val = w->vmax;

    /* 底 */
    bg = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg)
                                                         : GetSysColor(COLOR_BTNFACE));
    oldbr = SelectObject(dc, bg);
    oldpen = SelectObject(dc, GetStockObject(NULL_PEN));
    Rectangle(dc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(dc, oldpen);
    SelectObject(dc, oldbr);
    DeleteObject(bg);

    radius = ((width < height ? width : height) - thick) / 2 - 2;
    if (radius < 4) return;

    /* 轨道 */
    pen = CreatePen(PS_SOLID, thick, RGB(0xE0, 0xDA, 0xE6));
    oldpen = SelectObject(dc, pen);
    MoveToEx(dc, cx + radius, cy, NULL);
    AngleArc(dc, cx, cy, radius, 0.0f, 359.9f);
    SelectObject(dc, oldpen);
    DeleteObject(pen);

    /* 进度弧：从 12 点方向顺时针。
     * 不用 GDI 的 AngleArc —— 它的角度约定和屏幕方向对不上，容易画出
     * 一根从 12 点连到弧起点的"棒棒糖"线。自己算点最稳。 */
    sweep = (int)((long long)(val - w->vmin) * 360 / span);
    if (sweep > 0) {
        POINT pts[RING_STEPS + 1];
        int   steps = RING_STEPS * sweep / 360;
        int   k;

        if (steps < 1) steps = 1;
        if (steps > RING_STEPS) steps = RING_STEPS;
        for (k = 0; k <= steps; k++) {
            double a = -CG_PI / 2 + (CG_PI / 180) * (double)sweep * (double)k / (double)steps;
            pts[k].x = cx + (int)((double)radius * cos(a) + 0.5);
            pts[k].y = cy + (int)((double)radius * sin(a) + 0.5);
        }
        pen = CreatePen(PS_SOLID, thick, to_colorref((unsigned)fg));
        oldpen = SelectObject(dc, pen);
        Polyline(dc, pts, steps + 1);
        SelectObject(dc, oldpen);
        DeleteObject(pen);
    }

    /* 中间百分比 */
    {
        HFONT f = w->app ? font_for(w->app, w->style.font, w->style.size, w->style.bold, 0) : NULL;
        HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        swprintf(text, 32, L"%d%%", (val - w->vmin) * 100 / span);
        tr = *rc;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldf);
    }
}

static LRESULT CALLBACK RingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            draw_ring(dc, w, &rc);
        } else {
            FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_ring_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RingProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_RING_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 画板（自绘）
 * cg_canvas_* 命令把每笔记录在 w->ops 里，WM_PAINT 按序回放。
 * 坐标是逻辑像素（跟 .cgui 同一套），绘制时按 DPI 缩放。
 */
static int canvas_scale(const cg_widget_t *w, int logical)
{
    return scale_px(w->app, logical);
}

static void draw_canvas(HDC dc, cg_widget_t *w, const RECT *rc)
{
    COLORREF bgc = (w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg)
                                                  : GetSysColor(COLOR_WINDOW);
    HBRUSH bg = CreateSolidBrush(bgc);
    FillRect(dc, rc, bg);
    DeleteObject(bg);

    for (int i = 0; i < w->nops; i++) {
        const cg_canvas_op_t *op = &w->ops[i];
        HPEN pen = CreatePen(PS_SOLID, op->penw > 0 ? op->penw : 1, to_colorref(op->color));
        HGDIOBJ oldpen = SelectObject(dc, pen);
        HBRUSH br = NULL;
        HGDIOBJ oldbr = NULL;

        if (op->fill) {
            br = CreateSolidBrush(to_colorref(op->color));
            oldbr = SelectObject(dc, br);
        } else {
            oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        }

        switch (op->type) {
        case CG_CANVAS_OP_LINE:
            MoveToEx(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1), NULL);
            LineTo(dc, canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_RECT:
            Rectangle(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                      canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_ELLIPSE:
            Ellipse(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                    canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_TEXT: {
            wchar_t *wt = wc_from_utf8(op->text ? op->text : "");
            HFONT f = font_for(w->app, w->style.font,
                               w->style.size > 0 ? w->style.size : 10, w->style.bold, 0);
            HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, to_colorref(op->color));
            if (wt) {
                TextOutW(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                         wt, (int)wcslen(wt));
                free(wt);
            }
            SelectObject(dc, oldf);
            break;
        }
        default:
            break;
        }
        SelectObject(dc, oldpen);
        DeleteObject(pen);
        if (br) { SelectObject(dc, oldbr); DeleteObject(br); }
        else SelectObject(dc, oldbr);
    }
}

static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            draw_canvas(dc, w, &rc);
        } else {
            FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        cg_widget_t *w = widget_by_hwnd(app, hwnd);
        if (w && !g_in_notify) {
            g_in_notify = 1;
            cg_dispatch(w, CG_EVENT_CLICK);   /* 双击由运行时按系统间隔合成 */
            g_in_notify = 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_canvas_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CanvasProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_CANVAS_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 面板窗口 */

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_SETCURSOR:
        /* 子控件把通知发给直接父窗口，这里转发到主窗口统一处理。
         * 分组框里的树/表格/开关/链接都靠这条转发才能收到事件。 */
        if (app && app->hwnd && (HWND)app->hwnd != hwnd) {
            return SendMessageW((HWND)app->hwnd, msg, wp, lp);
        }
        break;
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }
    case WM_PAINT: {
        /* card / shape 要自己画边框（圆角、椭圆），group / box 保持系统默认 */
        cg_widget_t *w = app ? widget_by_hwnd(app, hwnd) : NULL;
        if (!w || (!is_t(w, "card") && !is_t(w, "shape"))) break;
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int inset = scale_px(app, 1);
            COLORREF fillc = (w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg) : 0;
            COLORREF penc = (w->style.fg != CG_COLOR_NONE) ? to_colorref(w->style.fg)
                                                           : RGB(0xB4, 0xB0, 0xBB);
            HPEN pen = CreatePen(PS_SOLID, inset ? inset : 1, penc);
            HGDIOBJ oldpen = SelectObject(dc, pen);
            HBRUSH br = NULL;
            HGDIOBJ oldbr;
            if (is_t(w, "card") || w->style.bg != CG_COLOR_NONE) {
                br = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? fillc : GetSysColor(COLOR_BTNFACE));
                oldbr = SelectObject(dc, br);
            } else {
                oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            }
            if (is_t(w, "shape") && w->format && strcmp(w->format, "ellipse") == 0) {
                Ellipse(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            } else if (is_t(w, "shape") && (!w->format || strcmp(w->format, "rect") != 0)) {
                int r = scale_px(app, (w->style.radius > 0) ? w->style.radius : 8);
                RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, r, r);
            } else if (is_t(w, "card")) {
                int r = scale_px(app, (w->style.radius > 0) ? w->style.radius : 12);
                RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, r, r);
            } else {
                Rectangle(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            }
            SelectObject(dc, oldbr);
            if (br) DeleteObject(br);
            SelectObject(dc, oldpen);
            DeleteObject(pen);

            /* 卡片顶部画标题 */
            if (is_t(w, "card") && w->text && *w->text) {
                HFONT f = font_for(app, w->style.font, w->style.size ? w->style.size : 10, w->style.bold, 0);
                HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
                RECT tr = rc;
                tr.left += scale_px(app, 12);
                tr.top += scale_px(app, 6);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? to_colorref(w->style.fg)
                                                                : GetSysColor(COLOR_WINDOWTEXT));
                wchar_t *wt = wc_from_utf8(w->text);
                if (wt) { DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_TOP); free(wt); }
                SelectObject(dc, oldf);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_panel_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CG_PANEL_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 事件映射 */

/** 把编辑框里的当前文字读回控件模型 */
static void pull_edit_text(cg_widget_t *w, HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(hwnd, buf, len + 1);
    {
        int need = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
        char *u8 = (char *)malloc((size_t)(need > 0 ? need : 1));
        if (u8) {
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, u8, need, NULL, NULL);
            cg_widget_set_text(w, u8);
            free(u8);
        }
    }
    free(buf);
}

/** 真正的映射：原生通知 -> CStackGUI 事件。cmd_id 是 WM_COMMAND 的 LOWORD。 */
static void notify_body(cg_app_t *app, cg_widget_t *w, HWND hwnd, int code, int cmd_id)
{
    /* 菜单按钮：不派发 click，改成弹菜单（菜单项被选中时再派发 click） */
    if (!strcmp(w->type, "menubutton")) {
        if (code == BN_CLICKED) popup_menu_for(app, w, hwnd);
        return;
    }
    /* 颜色选择：点色块弹系统选色对话框，选中后写回 value（0xRRGGBB）并报 change。
     * 注意 value 存的是颜色，不能走 cg_widget_set_value —— 那里会按 min/max 截断。 */
    if (!strcmp(w->type, "colorpicker")) {
        if (code != BN_CLICKED) return;
        {
            static COLORREF custom[16];
            CHOOSECOLORW cc;
            unsigned cur = (w->value > 0) ? (unsigned)w->value : 0x6750A4;
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = app->hwnd ? (HWND)app->hwnd : NULL;
            cc.rgbResult = to_colorref(cur);
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (!ChooseColorW(&cc)) return;
            w->value = (int)(((unsigned)GetRValue(cc.rgbResult) << 16) |
                             ((unsigned)GetGValue(cc.rgbResult) << 8) |
                             (unsigned)GetBValue(cc.rgbResult));
            if (w->node) w->node->value = w->value;
            if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
            cg_dispatch(w, CG_EVENT_CHANGE);
        }
        return;
    }
    /* tag：能点的圆角小片，当按钮用 */
    if (!strcmp(w->type, "tag")) {
        if (code == BN_CLICKED) cg_dispatch(w, CG_EVENT_CLICK);
        return;
    }
    /* rate / pagination 的点击在 FocusProc 的 WM_LBUTTONUP 里处理
     * （要知道 x 坐标才能判断点的是哪颗星 / 左右翻页），这里忽略 */
    if (!strcmp(w->type, "rate") || !strcmp(w->type, "pagination")) {
        return;
    }
    /* 工具栏：按钮点击走 WM_COMMAND，命令 id 反查是第几个按钮 */
    if (!strcmp(w->type, "toolbar")) {
        if (code == BN_CLICKED && cmd_id) {
            for (int i = 0; i < w->ntool; i++) {
                if (w->tool_ids[i] != cmd_id) continue;
                w->value = i;
                if (w->node) w->node->value = i;
                cg_dispatch(w, CG_EVENT_CLICK);
                break;
            }
        }
        return;
    }
    if (!strcmp(w->type, "checkbox") || !strcmp(w->type, "radio")) {
        /* 这两种是 BS_CHECKBOX/BS_RADIOBUTTON：点击时 BUTTON 自己 toggle 过
         * 内部 check 状态，读回来就是新值。 */
        LRESULT st = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
        cg_widget_set_checked(w, st == BST_CHECKED);
        cg_dispatch(w, CG_EVENT_CHANGE);
        return;
    }
    if (!strcmp(w->type, "switch")) {
        /* switch 是 BS_OWNERDRAW 按钮：点击时 BUTTON 不会 toggle check 状态
         * （BM_SETCHECK/BM_GETCHECK 只对 checkbox/radio 有定义），读 BM_GETCHECK
         * 只会拿到恒定旧值，表现为"关掉后就再也打不开"。所以自己取反。 */
        cg_widget_set_checked(w, !w->checked);
        cg_dispatch(w, CG_EVENT_CHANGE);
        return;
    }
    if (!strcmp(w->type, "listbox")) {
        if (code == LBN_SELCHANGE) {
            LRESULT sel = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                cg_widget_set_value(w, (int)sel);
                cg_dispatch(w, CG_EVENT_SELECT);
            }
        } else if (code == LBN_DBLCLK) {
            cg_dispatch(w, CG_EVENT_DBLCLICK);
        }
        return;
    }
    /* 这三种都是"点一下 = click"，双击由运行时按系统间隔合成（原生控件报的
     * STN_CLICKED / BN_CLICKED 都是 0，走同一条路更一致） */
    if (!strcmp(w->type, "button") || !strcmp(w->type, "iconbutton") ||
        !strcmp(w->type, "link")) {
        if (code == BN_CLICKED || code == STN_CLICKED) cg_dispatch(w, CG_EVENT_CLICK);
        return;
    }
    if (is_editlike(w)) {
        if (code == EN_CHANGE) {
            if (strcmp(w->type, "spinner") == 0) {
                /* 微调框要让 value 跟上手输的数字 */
                char buf[64];
                HWND up = (HWND)w->native2;
                int  v;
                pull_edit_text(w, hwnd);
                snprintf(buf, sizeof(buf), "%s", w->text ? w->text : "0");
                v = atoi(buf);
                if (v < w->vmin) v = w->vmin;
                if (v > w->vmax) v = w->vmax;
                w->value = v;
                if (w->node) w->node->value = v;
                if (up) SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)v);
            } else {
                pull_edit_text(w, hwnd);
            }
            cg_dispatch(w, CG_EVENT_CHANGE);
        }
        return;
    }
    (void)app;
}

/** WM_NOTIFY：标签页 / 树 / 表格 / 微调 的富通知都从这条路进来 */
static void notify_common(cg_app_t *app, const NMHDR *hdr)
{
    cg_widget_t *w = widget_by_hwnd(app, hdr->hwndFrom);

    if (!w) return;
    if (g_in_notify) return;
    g_in_notify = 1;

    switch (hdr->code) {
    case TCN_SELCHANGE: {
        LRESULT sel = SendMessageW((HWND)w->native, TCM_GETCURSEL, 0, 0);
        if (sel >= 0) {
            w->value = (int)sel;
            if (w->node) w->node->value = (int)sel;
            sync_widget(w);          /* 切页：子控件按 page 显隐 */
            cg_dispatch(w, CG_EVENT_SELECT);
        }
        break;
    }
    case TVN_SELCHANGEDW: {
        const NMTREEVIEWW *ntv = (const NMTREEVIEWW *)hdr;
        for (int i = 0; i < w->item_map_n; i++) {
            if ((HTREEITEM)w->item_map[i] == ntv->itemNew.hItem) {
                w->tree_sel = i;
                cg_dispatch(w, CG_EVENT_SELECT);
                break;
            }
        }
        break;
    }
    case TVN_FIRST - 1:                     /* TVN_DBLCLK，有些 SDK 头没定义名字 */
        cg_dispatch(w, CG_EVENT_DBLCLICK);
        break;
    case LVN_ITEMCHANGED: {
        const NMLISTVIEW *nlv = (const NMLISTVIEW *)hdr;
        /* 勾选列：状态位图（高 4 位）变了才是勾选切换，别和选中混在一起 */
        if ((nlv->uChanged & LVIF_STATE) && w->check &&
            (nlv->uOldState & LVIS_STATEIMAGEMASK) != (nlv->uNewState & LVIS_STATEIMAGEMASK)) {
            int on = ((nlv->uNewState >> 12) & 0xF) > 1;
            if (nlv->iItem >= 0 && nlv->iItem < w->row_cap && w->row_checked) {
                w->row_checked[nlv->iItem] = on ? 1 : 0;
            }
            cg_dispatch(w, CG_EVENT_CHECK);
        }
        if ((nlv->uChanged & LVIF_STATE) && (nlv->uNewState & LVIS_SELECTED)) {
            w->table_sel = nlv->iItem;
            cg_dispatch(w, CG_EVENT_SELECT);
        }
        break;
    }
    case NM_DBLCLK:
        cg_dispatch(w, CG_EVENT_DBLCLICK);
        break;
    case TVN_FIRST - 11: {                  /* TVN_ITEMEXPANDEDW，有些 SDK 头没定义名字 */
        const NMTREEVIEWW *ntv = (const NMTREEVIEWW *)hdr;
        for (int i = 0; i < w->item_map_n; i++) {
            if ((HTREEITEM)w->item_map[i] != ntv->itemNew.hItem) continue;
            w->tree_sel = i;                /* 回调里用 cg_tree_selected 能问到是哪个节点 */
            cg_dispatch(w, (ntv->action & TVE_COLLAPSE) ? CG_EVENT_COLLAPSE : CG_EVENT_EXPAND);
            break;
        }
        break;
    }
    case MCN_SELECT: {                      /* 月历：用户点选了某天 */
        SYSTEMTIME st;
        if (SendMessageW((HWND)w->native, MCM_GETCURSEL, 0, (LPARAM)&st)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            cg_widget_set_text(w, buf);
        }
        cg_dispatch(w, CG_EVENT_SELECT);
        break;
    }
    case DTN_DATETIMECHANGE: {
        const NMDATETIMECHANGE *dt = (const NMDATETIMECHANGE *)hdr;
        if (dt->dwFlags == GDT_VALID) {
            datetime_pull_text(w, (HWND)w->native, (HWND)w->native2);
        }
        cg_dispatch(w, CG_EVENT_CHANGE);
        break;
    }
    case UDN_DELTAPOS: {
        const NMUPDOWN *nud = (const NMUPDOWN *)hdr;
        int v = nud->iPos + nud->iDelta;
        if (v < w->vmin) v = w->vmin;
        if (v > w->vmax) v = w->vmax;
        w->value = v;
        if (w->node) w->node->value = v;
        cg_dispatch(w, CG_EVENT_CHANGE);
        break;
    }
    default:
        break;
    }
    g_in_notify = 0;
}

/** 图标按钮：把 src 图片按比例居中画在按钮里（没有图就画个占位框） */
static void draw_icon_button(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT   rc = dis->rcItem;
    HDC    dc = dis->hDC;
    HBRUSH bg;
    int    box_w = rc.right - rc.left;
    int    box_h = rc.bottom - rc.top;

    bg = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg)
                                                         : GetSysColor(COLOR_BTNFACE));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    if (w->bitmap) {
        BITMAP bm;
        HDC    mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, (HGDIOBJ)w->bitmap);
        if (GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            int maxw = box_w - 8 > 1 ? box_w - 8 : box_w;
            int maxh = box_h - 8 > 1 ? box_h - 8 : box_h;
            int dw = bm.bmWidth;
            int dh = bm.bmHeight;
            int x, y;

            /* 只缩不放：小图标保持原尺寸，大图标等比缩到装得下 */
            if (dw > maxw || dh > maxh) {
                int sx = (long long)maxw * 100 / dw;
                int sy = (long long)maxh * 100 / dh;
                int s = sx < sy ? sx : sy;
                dw = bm.bmWidth * s / 100;
                dh = bm.bmHeight * s / 100;
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;
            }
            x = rc.left + (box_w - dw) / 2;
            y = rc.top + (box_h - dh) / 2;

            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, x, y, dw, dh, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        }
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        RECT inner = rc;

        InflateRect(&inner, -4, -4);
        FrameRect(dc, &inner, (HBRUSH)GetStockObject(GRAY_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 120, 128));
        DrawTextW(dc, L"图标", -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ------------------------------------------------------ 自绘小控件
 * colorpicker / badge / tag / rate / pagination 都是 BS_OWNERDRAW 按钮，
 * 画什么由各自的状态决定，和 switch 一个路数。
 */

static unsigned widget_fg_or(const cg_widget_t *w, unsigned fallback)
{
    return (w->style.fg != CG_COLOR_NONE) ? w->style.fg : fallback;
}

/** 颜色选择：整个按钮就是一块当前颜色，边上勾一圈边框 */
static void draw_color_swatch(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    unsigned rgb = (w->value > 0) ? (unsigned)w->value : 0x6750A4;
    HBRUSH br = CreateSolidBrush(to_colorref(rgb));
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);
    FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
}

/** 徽章：圆角小圆片 + 文字，底色用 style.bg（没设用品牌红） */
static void draw_badge(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    HBRUSH br = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg)
                                                                : RGB(0xB3, 0x26, 0x1E));
    HGDIOBJ oldbr = SelectObject(dis->hDC, br);
    HGDIOBJ oldpen = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
    RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, rc.bottom - rc.top, rc.bottom - rc.top);
    SelectObject(dis->hDC, oldpen);
    SelectObject(dis->hDC, oldbr);
    DeleteObject(br);
    {
        wchar_t *wt = wc_from_utf8(w->text ? w->text : "");
        if (wt) {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, to_colorref(widget_fg_or(w, 0xFFFFFF)));
            DrawTextW(dis->hDC, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            free(wt);
        }
    }
}

/** 标签片：圆角底 + 同色系边框 + 文字，像网页里的 tag */
static void draw_tag(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    HBRUSH br = CreateSolidBrush((w->style.bg != CG_COLOR_NONE) ? to_colorref(w->style.bg)
                                                                : RGB(0xE8, 0xDE, 0xF8));
    HPEN pen = CreatePen(PS_SOLID, 1, to_colorref(widget_fg_or(w, 0x6750A4)));
    HGDIOBJ oldbr = SelectObject(dis->hDC, br);
    HGDIOBJ oldpen = SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, rc.left, rc.top, rc.right - 1, rc.bottom - 1,
              rc.bottom - rc.top, rc.bottom - rc.top);
    SelectObject(dis->hDC, oldpen);
    SelectObject(dis->hDC, oldbr);
    DeleteObject(pen);
    DeleteObject(br);
    {
        wchar_t *wt = wc_from_utf8(w->text ? w->text : "");
        if (wt) {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, to_colorref(widget_fg_or(w, 0x1D1B20)));
            DrawTextW(dis->hDC, wt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            free(wt);
        }
    }
}

/** 评分：★☆ 按值画，样式字体没给就用默认大小 */
static void draw_rate(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    int stars = w->vmax - w->vmin;
    int val = w->value - w->vmin;
    wchar_t buf[32];
    int filled, empty;

    if (stars < 1) stars = 1;
    if (stars > 10) stars = 10;
    if (val < 0) val = 0;
    if (val > stars) val = stars;

    filled = val;
    empty = stars - filled;
    {
        HFONT f = font_for(w->app, w->style.font, w->style.size > 0 ? w->style.size : 12, 0, 0);
        HFONT oldf = (HFONT)SelectObject(dis->hDC, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dis->hDC, TRANSPARENT);
        if (filled > 0) {
            RECT fr = rc;
            swprintf(buf, 32, L"%.*s", filled, L"★★★★★★★★★★");
            SetTextColor(dis->hDC, to_colorref(widget_fg_or(w, 0xF2B219)));
            DrawTextW(dis->hDC, buf, filled, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (empty > 0) {
            RECT er = rc;
            /* 空星接在实星后面：先量实星的宽度 */
            SIZE sz = {0, 0};
            wchar_t one = L'★';
            GetTextExtentPoint32W(dis->hDC, &one, 1, &sz);
            er.left += sz.cx * filled;
            swprintf(buf, 32, L"%.*s", empty, L"☆☆☆☆☆☆☆☆☆☆");
            SetTextColor(dis->hDC, RGB(0xC4, 0xC7, 0xCC));
            DrawTextW(dis->hDC, buf, empty, &er, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dis->hDC, oldf);
    }
}

/** 分页：<  页码  >，点两侧翻页（点击判定在 FocusProc） */
static void draw_pagination(cg_widget_t *w, const DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    wchar_t mid[32];
    swprintf(mid, 32, L"%d / %d", w->value - w->vmin + 1, w->vmax - w->vmin + 1);

    FillRect(dis->hDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
    {
        RECT l = rc, r = rc;
        wchar_t *lt = wc_from_utf8(w->value <= w->vmin ? "" : "<");
        wchar_t *rt = wc_from_utf8(w->value >= w->vmax ? "" : ">");
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
        l.right = rc.left + (rc.right - rc.left) / 3;
        r.left = rc.right - (rc.right - rc.left) / 3;
        if (lt) { DrawTextW(dis->hDC, lt, -1, &l, DT_CENTER | DT_VCENTER | DT_SINGLELINE); free(lt); }
        if (rt) { DrawTextW(dis->hDC, rt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); free(rt); }
        DrawTextW(dis->hDC, mid, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/** WM_DRAWITEM：开关和图标按钮是自绘的 */
static void draw_own_item(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    cg_widget_t *w = widget_by_hwnd(app, dis->hwndItem);
    RECT  rc = dis->rcItem;
    HDC   dc = dis->hDC;
    int   h, pad, knob, track_w;
    int   on;
    COLORREF track, knob_c = RGB(255, 255, 255);
    HBRUSH  br;
    HGDIOBJ oldbr, oldpen;
    wchar_t *label = NULL;

    if (!w) return;
    if (is_t(w, "iconbutton")) {
        draw_icon_button(w, dis);
        return;
    }
    if (is_t(w, "colorpicker")) { draw_color_swatch(w, dis); return; }
    if (is_t(w, "badge"))       { draw_badge(w, dis); return; }
    if (is_t(w, "tag"))         { draw_tag(w, dis); return; }
    if (is_t(w, "rate"))        { draw_rate(w, dis); return; }
    if (is_t(w, "pagination"))  { draw_pagination(w, dis); return; }
    on = w->checked != 0;
    h = rc.bottom - rc.top;
    pad = h >= 28 ? 4 : 3;
    knob = h - pad * 2;

    /* 有文字时：左边文字，右边开关 */
    track_w = h * 2 - 4;
    if (track_w > rc.right - rc.left) track_w = rc.right - rc.left;
    if (track_w < 24) track_w = 24;

    if (w->text && *w->text) {
        RECT tr = rc;
        tr.right = rc.right - track_w - 6;
        wchar_t *wt = wc_from_utf8(w->text);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, (w->style.fg != CG_COLOR_NONE) ? to_colorref(w->style.fg) : GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(dc, wt ? wt : L"", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        free(wt);
        rc.left = rc.right - track_w;
    }

    track = on ? RGB(0x67, 0x50, 0xA4) : RGB(0x9A, 0x94, 0x9E);
    br = CreateSolidBrush(track);
    oldbr = SelectObject(dc, br);
    oldpen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rc.left, rc.top + pad, rc.left + track_w, rc.bottom - pad, rc.bottom - rc.top, rc.bottom - rc.top);
    SelectObject(dc, oldbr);
    DeleteObject(br);

    br = CreateSolidBrush(knob_c);
    oldbr = SelectObject(dc, br);
    SelectObject(dc, GetStockObject(NULL_PEN));
    {
        int cx = on ? (rc.left + track_w - pad - knob / 2) : (rc.left + pad + knob / 2);
        int cy = (rc.top + rc.bottom) / 2;
        Ellipse(dc, cx - knob / 2, cy - knob / 2, cx + knob / 2, cy + knob / 2);
    }
    SelectObject(dc, oldbr);
    SelectObject(dc, oldpen);
    DeleteObject(br);
    (void)label;
}

static void notify_from(cg_app_t *app, HWND hwnd, int code, int cmd_id)
{
    /* 先按窗口句柄找：spinner 有两个 HWND，按 ctrl id 会漏掉箭头 */
    cg_widget_t *w = widget_by_hwnd(app, hwnd);
    if (!w) w = cg_widget_by_ctrl_id(app, GetDlgCtrlID(hwnd));

    if (!w || g_in_notify) return;
    g_in_notify = 1;
    notify_body(app, w, hwnd, code, cmd_id);
    g_in_notify = 0;
}

/* ------------------------------------------------------------ 主窗口 */

static void sync_widget(cg_widget_t *w);

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_COMMAND: {
        if (!app) break;
        /* lParam 为 0 = 这条命令来自窗口菜单（菜单栏），不是控件 */
        if (!lp) {
            cg_dispatch_menu_cmd(app, (int)LOWORD(wp));
            return 0;
        }
        /* IsDialogMessage 把回车变成 IDOK/IDCANCEL：交给当前焦点控件当 SUBMIT 用 */
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            HWND focus = GetFocus();
            if (focus) {
                cg_widget_t *fw = cg_widget_by_ctrl_id(app, GetDlgCtrlID(focus));
                if (fw && (!strcmp(fw->type, "edit") || !strcmp(fw->type, "multiedit"))) {
                    pull_edit_text(fw, focus);
                    cg_dispatch(fw, CG_EVENT_SUBMIT);
                }
            }
            return 0;
        }
        notify_from(app, (HWND)lp, HIWORD(wp), (int)LOWORD(wp));
        return 0;
    }
    case WM_HSCROLL:
    case WM_VSCROLL: {
        if (!app) break;
        if (lp) {
            cg_widget_t *w = widget_by_hwnd(app, (HWND)lp);
            if (w && !strcmp(w->type, "slider")) {
                LRESULT v = SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
                /* 拖动结束才发 release：拖的过程中 change 会刷很多次，
                 * 需要"松手才存一次"的场景（联网保存、重算）就绑 release。 */
                cg_widget_set_value(w, (int)v);
                cg_dispatch(w, LOWORD(wp) == TB_ENDTRACK ? CG_EVENT_RELEASE : CG_EVENT_CHANGE);
                return 0;
            }
            if (w && !strcmp(w->type, "spinner")) {
                HWND up = (HWND)w->native2;
                if (up && !g_in_notify) {
                    DWORD pos = (DWORD)SendMessageW(up, UDM_GETPOS32, 0, 0);
                    g_in_notify = 1;
                    w->value = (int)pos;
                    if (w->node) w->node->value = (int)pos;
                    cg_dispatch(w, CG_EVENT_CHANGE);
                    g_in_notify = 0;
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
            cg_widget_t *tw = widget_by_hwnd(app, ((const NMHDR *)lp)->hwndFrom);
            if (tw && tw->hint && *tw->hint) {
                static wchar_t tip_buf[256];
                wchar_t *wt = wc_from_utf8(tw->hint);
                if (wt) {
                    _snwprintf(tip_buf, 256, L"%s", wt);
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
        notify_common(app, (const NMHDR *)lp);
        return 0;
    }
    case WM_DRAWITEM: {
        if (!app) break;
        draw_own_item(app, (const DRAWITEMSTRUCT *)lp);
        return TRUE;
    }
    case WM_SETCURSOR: {
        /* 链接要手型光标，否则用户不知道那能点 */
        if (app && LOWORD(lp) == HTCLIENT && wp) {
            cg_widget_t *cw = widget_by_hwnd(app, (HWND)wp);
            if (cw && is_t(cw, "link")) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        if (app) {
            HWND child = (HWND)lp;
            cg_widget_t *w = cg_widget_by_ctrl_id(app, GetDlgCtrlID(child));
            if (w) {
                HDC dc = (HDC)wp;
                if (w->style.fg != CG_COLOR_NONE) {
                    SetTextColor(dc, to_colorref(w->style.fg));
                } else if (is_t(w, "link")) {
                    SetTextColor(dc, RGB(0x0B, 0x57, 0xD0));   /* 链接默认蓝，没设颜色时用 */
                }
                if (w->style.bg != CG_COLOR_NONE) {
                    SetBkColor(dc, to_colorref(w->style.bg));
                    SetBkMode(dc, OPAQUE);
                    return (LRESULT)brush_for(w->style.bg);
                }
            }
        }
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
            if (tw && is_t(tw, "timer") && tw->period > 0) {
                if (!g_in_notify) {
                    g_in_notify = 1;
                    cg_dispatch(tw, CG_EVENT_TICK);
                    g_in_notify = 0;
                }
                return 0;
            }
        }
        break;
    case WM_DPICHANGED: {
        if (app) {
            RECT *r = (RECT *)lp;
            int dpi = HIWORD(wp);
            cg_win_state_t *st = (cg_win_state_t *)app->natives;
            if (st) st->dpi = dpi;
            app->scale = (int)((long long)dpi * 100 / 96);
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            for (cg_widget_t *w = app->widgets; w; w = w->next) sync_widget(w);
        }
        return 0;
    }
    case WM_CLOSE: {
        if (app && app->window) {
            app->close_cancel = 0;
            cg_dispatch(app->window, CG_EVENT_CLOSE);
            /* 回调里调了 cg_cancel_close() 就先别关（"还有没保存的东西"那种） */
            if (app->close_cancel && !app->quit) return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------ 控件创建 */

static HWND parent_hwnd_of(const cg_app_t *app, const cg_widget_t *w)
{
    if (!w || !w->parent || w->parent->is_window) return (HWND)app->hwnd;
    return (HWND)w->parent->native;
}

/** 按运行时行表建 HMENU。行表里父项下标一定小于子项，所以一遍扫过去就够。 */
static HMENU menu_build(const cg_widget_t *w)
{
    HMENU  root;
    HMENU *subs;

    root = CreatePopupMenu();
    if (!root || w->nmenu <= 0) return root;

    subs = (HMENU *)calloc((size_t)w->nmenu, sizeof(HMENU));
    if (!subs) return root;

    for (int i = 0; i < w->nmenu; i++) {
        const cg_menu_row_t *r = &w->menu[i];
        HMENU   parent = root;
        wchar_t *wt = wc_from_utf8(r->text);
        DWORD   flags = MF_STRING;

        if (r->parent >= 0 && r->parent < w->nmenu && subs[r->parent]) parent = subs[r->parent];

        if (r->text && strcmp(r->text, "-") == 0) {
            AppendMenuW(parent, MF_SEPARATOR, 0, NULL);
        } else if (r->popup) {
            subs[i] = CreatePopupMenu();
            if (r->checked) flags |= MF_CHECKED;
            AppendMenuW(parent, flags | MF_POPUP, (UINT_PTR)subs[i], wt ? wt : L"");
        } else {
            if (r->checked) flags |= MF_CHECKED;
            AppendMenuW(parent, flags, (UINT_PTR)r->cmd, wt ? wt : L"");
        }
        free(wt);
    }
    free(subs);
    return root;
}

/** 工具栏按钮：没有图标，用「文字池 + BTNS_SHOWTEXT」做纯文字按钮 */
static void build_toolbar(HWND h, cg_widget_t *w)
{
    wchar_t  *pool;
    TBBUTTON *btns;
    int       total, base;
    size_t    off;

    while (SendMessageW(h, TB_BUTTONCOUNT, 0, 0) > 0) {
        SendMessageW(h, TB_DELETEBUTTON, 0, 0);
    }
    SendMessageW(h, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    free(w->tool_ids);
    w->tool_ids = NULL;
    w->ntool = 0;
    if (w->nitems <= 0) return;

    /* 文字池：一条接一条的 UTF-16 字符串，最后再补一个 0 结尾 */
    total = 1;
    for (int i = 0; i < w->nitems; i++) total += (int)strlen(w->items[i] ? w->items[i] : "") + 1;
    pool = (wchar_t *)calloc((size_t)total, sizeof(wchar_t));
    if (!pool) return;
    off = 0;
    for (int i = 0; i < w->nitems; i++) {
        wchar_t *one = wc_from_utf8(w->items[i] ? w->items[i] : "");
        size_t len = one ? wcslen(one) : 0;
        if (off + len >= (size_t)total) { free(one); break; }
        memcpy(pool + off, one, len * sizeof(wchar_t));
        off += len + 1;
        free(one);
    }
    base = (int)SendMessageW(h, TB_ADDSTRINGW, 0, (LPARAM)pool);
    free(pool);
    if (base < 0) return;

    w->tool_ids = (int *)calloc((size_t)w->nitems, sizeof(int));
    btns = (TBBUTTON *)calloc((size_t)w->nitems, sizeof(TBBUTTON));
    if (!w->tool_ids || !btns) {
        free(btns);
        return;
    }
    for (int i = 0; i < w->nitems; i++) {
        w->tool_ids[i] = cg_alloc_ctrl_id(w->app);   /* 按钮点击走 WM_COMMAND */
        btns[i].iBitmap = I_IMAGENONE;
        btns[i].idCommand = w->tool_ids[i];
        btns[i].fsState = TBSTATE_ENABLED;
        btns[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        btns[i].iString = base + i;
    }
    w->ntool = w->nitems;
    SendMessageW(h, TB_ADDBUTTONSW, (WPARAM)w->nitems, (LPARAM)btns);
    free(btns);
    SendMessageW(h, TB_AUTOSIZE, 0, 0);
}

static void create_native(cg_widget_t *w)
{
    cg_app_t *app = w->app;
    HWND parent = parent_hwnd_of(app, w);
    HWND h;
    wchar_t *wt = wc_from_utf8(w->text);
    const char *type = w->type;
    DWORD style = style_of(w);
    DWORD ex = exstyle_of(w);

    /* 菜单栏没有自己的窗口：它是挂在主窗口上的 HMENU，在 win_init 里建。
     * 时钟也一样没有窗口：这里只负责给它起定时器（cg_widget_add 动态加的也走这）。 */
    if (!strcmp(type, "menu")) {
        free(wt);
        return;
    }
    if (!strcmp(type, "timer")) {
        free(wt);
        cg_win_restart_timer(w);
        return;
    }

    if (!strcmp(type, "group") || !strcmp(type, "box")) register_panel_class();
    if (!strcmp(type, "image")) register_image_class();
    if (!strcmp(type, "progressring")) register_ring_class();
    if (!strcmp(type, "canvas")) register_canvas_class();

    /* browser 的窗口名就是起始地址（AtlAxWin 的约定） */
    if (!strcmp(type, "browser") && w->src && *w->src) {
        free(wt);
        wt = wc_from_utf8(w->src);
    }

    h = CreateWindowExW(ex, class_of(w), wt ? wt : L"",
                        style,
                        scale_px(app, w->x), scale_px(app, w->y),
                        scale_px(app, w->w), scale_px(app, w->h),
                        parent, (HMENU)(INT_PTR)w->ctrl_id,
                        GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!h) {
        fprintf(stderr, "[cstackgui] 创建控件失败: %s #%s\n", type ? type : "-", w->id ? w->id : "-");
        return;
    }
    w->native = h;

    if (!strcmp(type, "group") || !strcmp(type, "box") ||
        !strcmp(type, "card") || !strcmp(type, "shape") ||
        !strcmp(type, "image") || !strcmp(type, "progressring") ||
        !strcmp(type, "canvas")) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)app);
    }

    /* 统一挂焦点子类：gotfocus/lostfocus，rate/pagination 的按点判定 */
    subclass_for_events(h);
    {
        cg_win_state_t *st = (cg_win_state_t *)app->natives;
        if (st && st->tooltip && w->hint && *w->hint) {
            tooltip_add_for(st->tooltip, h);
        }
    }

    /* 数字微调：输入框 + 右侧上下箭头，是两个原生窗口拼出来的一个控件 */
    if (!strcmp(type, "spinner")) {
        HWND up = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_NOTHOUSANDS,
                                  0, 0, 0, 0, parent, NULL, GetModuleHandleW(NULL), NULL);
        if (up) {
            SendMessageW(up, UDM_SETBUDDY, (WPARAM)h, 0);
            SendMessageW(up, UDM_SETRANGE32, (WPARAM)w->vmin, (LPARAM)w->vmax);
            SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)w->value);
            w->native2 = up;
        }
    }

    /* 日期时间：Windows 没有「日期+时间」合一的控件，用两个 DTP 并排拼 */
    if (!strcmp(type, "datepicker") && is_datetime(w)) {
        HWND t = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT,
                                 0, 0, 0, 0, parent, NULL, GetModuleHandleW(NULL), NULL);
        w->native2 = t;
    }

    /* 第二个原生窗口也要能报焦点事件 */
    if (w->native2) subclass_for_events((HWND)w->native2);

    sync_widget(w);
}

/** 递归建原生控件：父先于子 */
static void create_native_tree(cg_widget_t *parent)
{
    if (!parent) return;
    for (size_t i = 0; i < parent->nchildren; i++) {
        cg_widget_t *c = parent->children[i];
        create_native(c);
        create_native_tree(c);
    }
}

static void destroy_native(cg_widget_t *w)
{
    if (w->native2) {
        DestroyWindow((HWND)w->native2);       /* spinner 的箭头 */
        w->native2 = NULL;
    }
    if (w->bitmap) {                            /* 图片控件的位图 */
        DeleteObject((HGDIOBJ)w->bitmap);
        w->bitmap = NULL;
    }
    if (w->imglist) {                           /* 表格行图标 */
        ImageList_Destroy((HIMAGELIST)w->imglist);
        w->imglist = NULL;
    }
    if (w->item_map) {
        free(w->item_map);
        w->item_map = NULL;
        w->item_map_n = 0;
    }
    if (w->native) {
        DestroyWindow((HWND)w->native);
        w->native = NULL;
    }
}

/* ------------------------------------------------------------ 状态同步 */

/* spinner 的箭头宽度（逻辑像素） */
#define CG_SPIN_ARROW_W 18

/** 标签页当前显示第几页时，某个子控件该不该出现 */
static int page_visible(const cg_widget_t *w)
{
    const cg_widget_t *p = w->parent;
    if (!p || !is_t(p, "tabs")) return 1;
    return w->page == p->value;
}

/** 重建树：逻辑下标 -> HTREEITEM 的映射存在 w->item_map 里 */
static void build_tree(HWND h, cg_widget_t *w)
{
    if (w->item_map) { free(w->item_map); w->item_map = NULL; }
    w->item_map_n = 0;
    if (w->ntree <= 0) return;

    w->item_map = (void **)calloc((size_t)w->ntree, sizeof(void *));
    if (!w->item_map) return;
    w->item_map_n = w->ntree;

    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (int i = 0; i < w->ntree; i++) {
        TVINSERTSTRUCTW ins;
        wchar_t *wt = wc_from_utf8(w->tree[i].text);
        HTREEITEM parent = TVI_ROOT;
        memset(&ins, 0, sizeof(ins));
        if (w->tree[i].parent >= 0 && w->tree[i].parent < i) {
            parent = (HTREEITEM)w->item_map[w->tree[i].parent];
        }
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT;
        ins.item.pszText = wt ? wt : L"";
        w->item_map[i] = (void *)SendMessageW(h, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        if (wt && w->tree[i].expanded) {
            SendMessageW(h, TVM_EXPAND, TVE_EXPAND, (LPARAM)w->item_map[i]);
        }
        free(wt);
    }
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    if (w->tree_sel >= 0 && w->tree_sel < w->item_map_n) {
        SendMessageW(h, TVM_SELECTITEM, TVGN_CARET, (LPARAM)w->item_map[w->tree_sel]);
    }
    InvalidateRect(h, NULL, TRUE);
}

/* 表格行图标：把图片缩到 imagelist 的格子尺寸再放进去，返回下标，失败 -1 */
static int imagelist_add_file(cg_widget_t *w, HIMAGELIST il, const char *path, int cell)
{
    HBITMAP src = load_image_file(w->app, path);
    HBITMAP fitted = NULL;
    int idx = -1;
    HDC sdc, ddc, sdc0;
    HGDIOBJ olds, oldd;

    if (!src || !il || cell < 4) {
        if (src) DeleteObject(src);
        return -1;
    }
    sdc0 = GetDC(NULL);
    fitted = CreateCompatibleBitmap(sdc0, cell, cell);
    ReleaseDC(NULL, sdc0);
    sdc = CreateCompatibleDC(NULL);
    ddc = CreateCompatibleDC(NULL);
    olds = SelectObject(sdc, src);
    oldd = SelectObject(ddc, fitted);
    SetStretchBltMode(ddc, HALFTONE);
    SetBrushOrgEx(ddc, 0, 0, NULL);
    {
        BITMAP bm;
        int sw = 16, sh = 16;
        if (GetObjectW(src, sizeof(bm), &bm)) { sw = bm.bmWidth; sh = bm.bmHeight; }
        StretchBlt(ddc, 0, 0, cell, cell, sdc, 0, 0, sw, sh, SRCCOPY);
    }
    SelectObject(sdc, olds);
    SelectObject(ddc, oldd);
    DeleteDC(sdc);
    DeleteDC(ddc);
    idx = ImageList_Add(il, fitted, NULL);
    DeleteObject(fitted);
    DeleteObject(src);
    return idx;
}

/** 惰性建表格的 imagelist（行图标用），建好就挂到 LVSIL_SMALL 上 */
static HIMAGELIST table_imagelist(HWND h, cg_widget_t *w)
{
    int cell = scale_px(w->app, 16);
    if (cell < 12) cell = 12;
    if (!w->imglist) {
        w->imglist = ImageList_Create(cell, cell, ILC_COLOR32, 4, 4);
        SendMessageW(h, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)w->imglist);
    }
    return (HIMAGELIST)w->imglist;
}

/** 重建表格：列 + 行 */
static void build_table(HWND h, cg_widget_t *w)
{
    RECT rc;
    int  total, auto_w, fixed = 0, autos = 0;

    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    while (SendMessageW(h, LVM_DELETECOLUMN, 0, 0)) { /* 清空所有列 */ }
    SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
    SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
    if (w->check) {
        SendMessageW(h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
    }

    GetClientRect(h, &rc);
    total = rc.right - rc.left;
    for (int i = 0; i < w->ncols; i++) {
        if (w->cols[i].width > 0) fixed += w->cols[i].width;
        else autos++;
    }
    auto_w = (autos > 0 && total > fixed) ? (total - fixed) / autos : 90;

    for (int i = 0; i < w->ncols; i++) {
        LVCOLUMNW col;
        wchar_t *wt = wc_from_utf8(w->cols[i].title);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = wt ? wt : L"";
        col.cx = (w->cols[i].width > 0) ? w->cols[i].width : auto_w;
        col.iSubItem = i;
        SendMessageW(h, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        free(wt);
    }

    for (int r = 0; r < w->nrows; r++) {
        LVITEMW it;
        char *first = (w->rows[r] && w->row_cells > 0) ? w->rows[r][0] : NULL;
        wchar_t *wfirst = wc_from_utf8(first ? first : "");
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = r;
        it.iSubItem = 0;
        it.pszText = wfirst ? wfirst : L"";
        it.lParam = (w->row_val && r < w->row_cap) ? w->row_val[r] : 0;
        SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)&it);
        free(wfirst);

        for (int c = 1; c < w->ncols && c < w->row_cells; c++) {
            LVITEMW sub;
            wchar_t *wt = wc_from_utf8(w->rows[r][c] ? w->rows[r][c] : "");
            memset(&sub, 0, sizeof(sub));
            sub.iItem = r;
            sub.iSubItem = c;
            sub.pszText = wt ? wt : L"";
            SendMessageW(h, LVM_SETITEMTEXTW, (WPARAM)r, (LPARAM)&sub);
            free(wt);
        }
    }

    /* 行图标：行表刚重建，图片下标要重新放一遍 */
    for (int r = 0; r < w->nrows && r < w->row_icon_n; r++) {
        if (!w->row_icon[r]) continue;
        {
            HIMAGELIST il = table_imagelist(h, w);
            int idx = imagelist_add_file(w, il, w->row_icon[r], scale_px(w->app, 16));
            if (idx >= 0) {
                LVITEMW it;
                memset(&it, 0, sizeof(it));
                it.mask = LVIF_IMAGE;
                it.iItem = r;
                it.iImage = idx;
                SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
            }
        }
    }

    /* 勾选状态：重建后按记录恢复 */
    if (w->check) {
        for (int r = 0; r < w->nrows && r < w->row_cap; r++) {
            if (!w->row_checked || !w->row_checked[r]) continue;
            {
                LVITEMW it;
                memset(&it, 0, sizeof(it));
                it.mask = LVIF_STATE;
                it.state = INDEXTOSTATEIMAGEMASK(2);
                it.stateMask = LVIS_STATEIMAGEMASK;
                SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)r, (LPARAM)&it);
            }
        }
    }

    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);
}

/** 重建标签页标签 + 按当前页决定子控件显隐 */
static void build_tabs(HWND h, cg_widget_t *w)
{
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, TCM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < w->nitems; i++) {
        TCITEMW tci;
        wchar_t *wt = wc_from_utf8(w->items[i]);
        memset(&tci, 0, sizeof(tci));
        tci.mask = TCIF_TEXT;
        tci.pszText = wt ? wt : L"";
        SendMessageW(h, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&tci);
        free(wt);
    }
    if (w->value >= w->nitems) w->value = 0;
    SendMessageW(h, TCM_SETCURSEL, (WPARAM)w->value, 0);
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);

    for (size_t i = 0; i < w->nchildren; i++) {
        sync_widget(w->children[i]);
    }
}

/** 状态栏：按段数均分，最后一段吃掉余量 */
static void build_status(HWND h, cg_widget_t *w)
{
    RECT rc;
    int  parts[32];
    int  n = w->nitems > 0 ? w->nitems : 1;
    int  total;

    if (n > 32) n = 32;
    GetClientRect(h, &rc);
    total = rc.right - rc.left;
    for (int i = 0; i < n; i++) {
        parts[i] = (i == n - 1) ? -1 : total * (i + 1) / n;
    }
    SendMessageW(h, SB_SETPARTS, (WPARAM)n, (LPARAM)parts);
    for (int i = 0; i < n; i++) {
        const char *text = (i < w->nitems) ? w->items[i] : "";
        wchar_t *wt = wc_from_utf8(text ? text : "");
        SendMessageW(h, SB_SETTEXTW, (WPARAM)i, (LPARAM)(wt ? wt : L""));
        free(wt);
    }
}

/** 图片 / 图标按钮：按需加载，换图时释放旧位图 */
static void sync_bitmap(cg_widget_t *w)
{
    const char *want = w->src ? w->src : "";
    const char *have = w->bitmap_src ? w->bitmap_src : "";

    if (strcmp(want, have) != 0) {
        if (w->bitmap) {
            DeleteObject((HGDIOBJ)w->bitmap);
            w->bitmap = NULL;
        }
        free(w->bitmap_src);
        w->bitmap_src = w->src ? _strdup(w->src) : NULL;
        w->image_failed = 0;
        if (w->src && *w->src) {
            w->bitmap = load_image_file(w->app, w->src);
            if (!w->bitmap) {
                w->image_failed = 1;
                fprintf(stderr, "[cstackgui] 读不到图片: %s（显示为占位框）\n", w->src);
            }
        }
        if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
    }
}

/** browser 只在创建时认一次地址，换地址就整个重建（省掉一套 COM 调用） */
static void browser_recreate(cg_widget_t *w)
{
    cg_app_t *app = w->app;
    wchar_t *wurl;
    HWND h;

    if (w->native) {
        DestroyWindow((HWND)w->native);
        w->native = NULL;
    }
    if (!g_atl_ok || !app) return;

    wurl = wc_from_utf8(w->src && *w->src ? w->src : "about:blank");
    h = CreateWindowExW(0, L"AtlAxWin", wurl ? wurl : L"about:blank",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        scale_px(app, w->x), scale_px(app, w->y),
                        scale_px(app, w->w), scale_px(app, w->h),
                        parent_hwnd_of(app, w), (HMENU)(INT_PTR)w->ctrl_id,
                        GetModuleHandleW(NULL), NULL);
    free(wurl);
    w->native = h;
}

static void sync_widget_body(cg_widget_t *w)
{
    HWND h = (HWND)w->native;
    cg_app_t *app = w->app;

    if (!h || !app) return;

    if (w->is_window) {
        if (w->text) {
            wchar_t *wt = wc_from_utf8(w->text);
            if (wt) { SetWindowTextW(h, wt); free(wt); }
        }
        return;
    }

    /* browser：src 变了就整个重建；位置也由重建时铺好 */
    if (is_t(w, "browser")) {
        const char *want = w->src ? w->src : "";
        if (strcmp(want, w->bitmap_src ? w->bitmap_src : "") != 0) {
            free(w->bitmap_src);
            w->bitmap_src = _strdup(want);
            browser_recreate(w);
            return;
        }
    }

    /* 位置尺寸 */
    if (is_t(w, "spinner")) {
        /* 输入框让出右侧给箭头 */
        int arrow = scale_px(app, CG_SPIN_ARROW_W);
        int width = scale_px(app, w->w);
        SetWindowPos(h, NULL, scale_px(app, w->x), scale_px(app, w->y),
                     width > arrow ? width - arrow : width, scale_px(app, w->h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (is_t(w, "datepicker") && is_datetime(w) && w->native2) {
        /* 日期在左半边，时间在右半边 */
        int x = scale_px(app, w->x), y = scale_px(app, w->y);
        int width = scale_px(app, w->w), height = scale_px(app, w->h);
        int half = width / 2;
        SetWindowPos(h, NULL, x, y, half - 2, height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos((HWND)w->native2, NULL, x + half + 2, y, width - half - 2, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(h, NULL, scale_px(app, w->x), scale_px(app, w->y),
                     scale_px(app, w->w), scale_px(app, w->h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* 文本（switch / tabs / tree / table / statusbar / image / toolbar / datepicker / monthcal 自己管内容） */
    if (!strcmp(w->type, "label") || !strcmp(w->type, "button") ||
        !strcmp(w->type, "menubutton") || !strcmp(w->type, "link") ||
        !strcmp(w->type, "checkbox") || !strcmp(w->type, "radio") ||
        !strcmp(w->type, "group") || !strcmp(w->type, "box") ||
        is_editlike(w)) {
        wchar_t *wt = wc_from_utf8(w->text);
        if (wt) { SetWindowTextW(h, wt); free(wt); }
    }

    /* 字体（链接默认带下划线，像链接该有的样子） */
    {
        int underline = is_t(w, "link") ? 1 : 0;
        HFONT f = font_for(app, w->style.font, w->style.size, w->style.bold, underline);
        SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }

    /* 条目内容 */
    if (w->items_dirty) {
        if (!strcmp(w->type, "listbox")) {
            SendMessageW(h, WM_SETREDRAW, FALSE, 0);
            SendMessageW(h, LB_RESETCONTENT, 0, 0);
            for (int i = 0; i < w->nitems; i++) {
                wchar_t *wt = wc_from_utf8(w->items[i]);
                if (wt) {
                    SendMessageW(h, LB_ADDSTRING, 0, (LPARAM)wt);
                    free(wt);
                }
            }
            SendMessageW(h, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(h, NULL, TRUE);
            if (w->scroll_bottom && w->nitems > 0) {
                SendMessageW(h, LB_SETTOPINDEX, (WPARAM)(w->nitems - 1), 0);
            }
        } else if (!strcmp(w->type, "combo")) {
            SendMessageW(h, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i < w->nitems; i++) {
                wchar_t *wt = wc_from_utf8(w->items[i]);
                if (wt) {
                    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)wt);
                    free(wt);
                }
            }
            SendMessageW(h, CB_SETCURSEL, 0, 0);
        } else if (!strcmp(w->type, "tabs")) {
            build_tabs(h, w);
        } else if (!strcmp(w->type, "tree")) {
            build_tree(h, w);
        } else if (!strcmp(w->type, "table")) {
            build_table(h, w);
        } else if (!strcmp(w->type, "statusbar")) {
            build_status(h, w);
        } else if (!strcmp(w->type, "toolbar")) {
            build_toolbar(h, w);
        }
        w->items_dirty = 0;
        w->scroll_bottom = 0;
    }

    /* 值 */
    if (!strcmp(w->type, "progress")) {
        SendMessageW(h, PBM_SETRANGE32, (WPARAM)w->vmin, (LPARAM)w->vmax);
        SendMessageW(h, PBM_SETPOS, (WPARAM)w->value, 0);
    } else if (!strcmp(w->type, "slider")) {
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(w->vmin, w->vmax));
        SendMessageW(h, TBM_SETPOS, TRUE, w->value);
    } else if (!strcmp(w->type, "checkbox") || !strcmp(w->type, "radio")) {
        /* switch 走 WM_DRAWITEM 自绘（按 w->checked 画），不用 BM_SETCHECK：
         * 那对 BS_OWNERDRAW 按钮没有定义，只会留下一个误导人的内部状态。 */
        SendMessageW(h, BM_SETCHECK, w->checked ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (!strcmp(w->type, "spinner")) {
        HWND up = (HWND)w->native2;
        if (up) {
            SendMessageW(up, UDM_SETRANGE32, (WPARAM)w->vmin, (LPARAM)w->vmax);
            SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)w->value);
        }
    } else if (!strcmp(w->type, "image") || !strcmp(w->type, "iconbutton")) {
        sync_bitmap(w);
    } else if (!strcmp(w->type, "monthcal")) {
        /* text 写日期就定位到那天，没写就用今天（.cgui 里通常留空） */
        SYSTEMTIME st;
        SYSTEMTIME dummy;
        if (w->text && *w->text) {
            datetime_from_text(w, &st, &dummy);
            SendMessageW(h, MCM_SETCURSEL, 0, (LPARAM)&st);
        }
    } else if (!strcmp(w->type, "datepicker")) {
        SYSTEMTIME d, t;
        datetime_from_text(w, &d, &t);
        /* 显示格式跟 .cgui 里 text 的写法对齐，免得控件显示成系统区域格式 */
        SendMessageW(h, DTM_SETFORMATW, 0, (LPARAM)(is_time_only(w) ? L"HH':'mm" : L"yyyy-MM-dd"));
        SendMessageW(h, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&d);
        if (w->native2) {
            SendMessageW((HWND)w->native2, DTM_SETFORMATW, 0, (LPARAM)L"HH':'mm");
            SendMessageW((HWND)w->native2, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&t);
        }
    } else if (!strcmp(w->type, "tabs")) {
        SendMessageW(h, TCM_SETCURSEL, (WPARAM)w->value, 0);
    } else if (!strcmp(w->type, "listbox") && !w->items_dirty) {
        SendMessageW(h, LB_SETCURSEL, (WPARAM)w->value, 0);
    } else if (!strcmp(w->type, "table") && w->table_sel >= 0) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_STATE;
        it.state = LVIS_SELECTED | LVIS_FOCUSED;
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)w->table_sel, (LPARAM)&it);
    }

    EnableWindow(h, w->enabled ? TRUE : FALSE);

    /* 标签页的子控件要按当前页显隐。
     * spinner / datetime 是两个 HWND 拼的控件：native2（箭头 / 时间框）
     * 必须跟 native 同进同退，否则切页/隐藏时会漏出半个控件。 */
    if (w->parent && is_t(w->parent, "tabs") && !page_visible(w)) {
        ShowWindow(h, SW_HIDE);
        if (w->native2) ShowWindow((HWND)w->native2, SW_HIDE);
    } else {
        ShowWindow(h, w->visible ? SW_SHOWNA : SW_HIDE);
        if (w->native2) ShowWindow((HWND)w->native2, w->visible ? SW_SHOWNA : SW_HIDE);
    }
    InvalidateRect(h, NULL, TRUE);
}

/** 外面统一包一层防重入：同步会触发原生通知（EN_CHANGE 等），
 *  不能让它们再跑回回调里，否则「回调改控件」会自己套自己。 */
static void sync_widget(cg_widget_t *w)
{
    int saved = g_in_notify;
    g_in_notify = 1;
    sync_widget_body(w);
    g_in_notify = saved;
}

/* ------------------------------------------------------------ 后端入口 */

static int win_init(cg_app_t *app)
{
    cg_win_state_t *st;
    WNDCLASSEXW wc;
    HWND hwnd;
    int  dpi = 96;
    RECT rc;
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
    g_app = app;    /* 焦点子类 / tooltip 反查用（单窗口应用，全局一份） */

    register_panel_class();
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
        g_richedit_class = L"RICHEDIT50W";
    } else if (LoadLibraryW(L"Riched20.dll")) {
        g_richedit_class = L"RichEdit20W";
        fprintf(stderr, "[cstackgui] 没找到 Msftedit.dll，richtext 退回 RichEdit20W\n");
    } else {
        g_richedit_class = L"EDIT";
        fprintf(stderr, "[cstackgui] 没有可用的富文本控件，richtext 退回普通多行输入\n");
    }

    /* browser 的宿主类：atl.dll 各版本 Windows 都带；起不来只影响 browser */
    {
        HMODULE atl = LoadLibraryW(L"atl.dll");
        typedef BOOL (WINAPI *axinit_t)(void);
        axinit_t axinit = atl ? (axinit_t)(void *)GetProcAddress(atl, "AtlAxWinInit") : NULL;
        g_atl_ok = (axinit && axinit()) ? 1 : 0;
        if (!g_atl_ok) {
            fprintf(stderr, "[cstackgui] atl.dll 不可用，browser 控件会显示占位文本\n");
        }
    }

    /* GDI+ 只用来读图片；起不来也不影响其它控件，图片控件会退回占位框 */
    {
        cg_gdip_input_t in;
        memset(&in, 0, sizeof(in));
        in.GdiplusVersion = 1;
        if (GdiplusStartup(&g_gdip_token, &in, NULL) != 0) {
            g_gdip_token = 0;
            fprintf(stderr, "[cstackgui] GDI+ 启动失败，image 控件只会显示占位框\n");
        }
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"CStackGUIMain";
    if (!RegisterClassExW(&wc)) return -1;

    rc.left = 0; rc.top = 0;
    rc.right = app->doc.width;
    rc.bottom = app->doc.height;
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    wtitle = wc_from_utf8(app->doc.title);
    hwnd = CreateWindowExW(0, L"CStackGUIMain", wtitle ? wtitle : L"CStackGUI",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, inst, app);
    free(wtitle);
    if (!hwnd) return -1;

    app->hwnd = hwnd;

    /* DPI 缩放系数（百分比） */
    {
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        typedef UINT (WINAPI *get_dpi_t)(HWND);
        get_dpi_t get_dpi = user32 ? (get_dpi_t)(void *)GetProcAddress(user32, "GetDpiForWindow") : NULL;
        if (get_dpi) {
            UINT d = get_dpi(hwnd);
            if (d > 0) { st->dpi = (int)d; app->scale = (int)((long long)d * 100 / 96); }
        }
    }

    /* 窗口背景 */
    st->bg_color = (app->doc.bg == CG_COLOR_NONE) ? GetSysColor(COLOR_BTNFACE) : to_colorref(app->doc.bg);
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
            if (is_t(w, "menu")) {
                if (mw) {
                    fprintf(stderr, "[cstackgui] 只支持一个 menu（窗口只能有一根菜单栏），\"%s\" 被忽略\n",
                            w->id ? w->id : "-");
                    continue;
                }
                mw = w;
            }
        }
        if (mw) {
            HMENU hm = menu_build(mw);
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
                    SetWindowPos(hwnd, NULL, 0, 0, app->doc.width + fw, app->doc.height + fh,
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
    create_native_tree(app->window);

    /* 时钟控件按各自的周期起定时器（id 复用 ctrl_id） */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (is_t(w, "timer") && w->period > 0) {
            SetTimer(hwnd, (UINT_PTR)w->ctrl_id, (UINT)w->period, NULL);
        }
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return 0;
}

static int win_run(cg_app_t *app)
{
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0 && !app->quit) {
        if (!IsDialogMessageW((HWND)app->hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

static void win_shutdown(cg_app_t *app)
{
    cg_win_state_t *st = (cg_win_state_t *)app->natives;
    if (!st) return;
    if (st->timer_id) KillTimer((HWND)app->hwnd, CG_TIMER_ID);
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (is_t(w, "timer")) KillTimer((HWND)app->hwnd, (UINT_PTR)w->ctrl_id);
    }
    if (app->hwnd) DestroyWindow((HWND)app->hwnd);
    g_app = NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->bitmap) {
            DeleteObject((HGDIOBJ)w->bitmap);
            w->bitmap = NULL;
        }
    }
    if (g_gdip_token) {
        GdiplusShutdown(g_gdip_token);
        g_gdip_token = 0;
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

static void win_set_title(cg_app_t *app, const char *title)
{
    wchar_t *wt = wc_from_utf8(title);
    if (wt && app->hwnd) SetWindowTextW((HWND)app->hwnd, wt);
    free(wt);
}

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

unsigned long cg_ticks(void)
{
    return (unsigned long)GetTickCount64();
}

unsigned long cg_double_click_ms(void)
{
    UINT t = GetDoubleClickTime();
    return t ? (unsigned long)t : 500;
}

/* ============================================================ 公共 API
 * 通用对话框 / 画板绘制 / 表格增强 / 网页浏览。
 * 这些都要摸 Win32 句柄，所以住在后端文件里，但对外是普通 C 函数。
 */

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
    wchar_t *wtitle = wc_from_utf8(title);
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
    wchar_t *wtitle = wc_from_utf8(title);
    wchar_t *wext = wc_from_utf8(default_ext);
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
    cc.rgbResult = to_colorref(cur);
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
    wchar_t *wt = wc_from_utf8(text ? text : "");
    wchar_t *tt = wc_from_utf8(title ? title : "提示");
    DWORD flag = (icon >= 0 && icon <= 3) ? icons[icon] : MB_ICONINFORMATION;
    MessageBoxW(dialog_owner(parent), wt ? wt : L"", tt ? tt : L"", MB_OK | flag);
    free(wt);
    free(tt);
}

/* ------------------------------------------------------------ 画板 */

/** 类型检查：canvas 的绘制命令只能用在 canvas 上 */
static int canvas_ok(const cg_widget_t *w, const char *who)
{
    if (w && w->type && strcmp(w->type, "canvas") == 0) return 1;
    fprintf(stderr, "[cstackgui] %s: 需要 canvas 控件，实际给的是 %s\n",
            who, (w && w->type) ? w->type : "(null)");
    return 0;
}

static void canvas_push(cg_widget_t *w, const cg_canvas_op_t *op)
{
    if (w->nops >= w->op_cap) {
        int nc = w->op_cap > 0 ? w->op_cap * 2 : 64;
        cg_canvas_op_t *no = (cg_canvas_op_t *)realloc(w->ops, (size_t)nc * sizeof(cg_canvas_op_t));
        if (!no) return;
        w->ops = no;
        w->op_cap = nc;
    }
    w->ops[w->nops++] = *op;
    if (w->native) InvalidateRect((HWND)w->native, NULL, FALSE);
}

/**
 * @brief 清空画板上所有绘制内容
 * @param w canvas 控件
 * @return void 无返回值
 */
void cg_canvas_clear(cg_widget_t *w)
{
    if (!canvas_ok(w, "cg_canvas_clear")) return;
    for (int i = 0; i < w->nops; i++) {
        if (w->ops[i].type == CG_CANVAS_OP_TEXT) free(w->ops[i].text);
    }
    w->nops = 0;
    if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
}

/**
 * @brief 设置后续图形的颜色（0xRRGGBB）
 * @param w canvas 控件
 * @param rgb 颜色，如 CG_RGB(0x33,0x33,0x33) 或 0x333333
 * @return void 无返回值
 */
void cg_canvas_set_color(cg_widget_t *w, unsigned rgb)
{
    if (!canvas_ok(w, "cg_canvas_set_color")) return;
    w->canvas_color = rgb & 0xFFFFFF;
}

/**
 * @brief 设置后续图形的线宽（像素，最小 1）
 * @param w canvas 控件
 * @param px 线宽
 * @return void 无返回值
 */
void cg_canvas_set_line_width(cg_widget_t *w, int px)
{
    if (!canvas_ok(w, "cg_canvas_set_line_width")) return;
    w->canvas_penw = px < 1 ? 1 : px;
}

/**
 * @brief 画一条线段
 * @param w canvas 控件
 * @param x1,y1 起点；x2,y2 终点（控件内坐标，左上角为 0,0）
 * @return void 无返回值
 */
void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_line")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_LINE;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.x1 = x1; op.y1 = y1; op.x2 = x2; op.y2 = y2;
    canvas_push(w, &op);
}

/**
 * @brief 画矩形
 * @param w canvas 控件
 * @param x,y 左上角；wd,ht 宽高
 * @param fill 1 = 实心，0 = 描边
 * @return void 无返回值
 */
void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_rect")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_RECT;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.fill = fill ? 1 : 0;
    op.x1 = x; op.y1 = y; op.x2 = x + wd; op.y2 = y + ht;
    canvas_push(w, &op);
}

/**
 * @brief 画椭圆（含圆）
 * @param w canvas 控件
 * @param x,y 外接矩形左上角；wd,ht 外接矩形宽高
 * @param fill 1 = 实心，0 = 描边
 * @return void 无返回值
 */
void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_ellipse")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_ELLIPSE;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.fill = fill ? 1 : 0;
    op.x1 = x; op.y1 = y; op.x2 = x + wd; op.y2 = y + ht;
    canvas_push(w, &op);
}

/**
 * @brief 写一行文字（左上角定位，颜色用当前画笔颜色）
 * @param w canvas 控件
 * @param x,y 文字左上角
 * @param text UTF-8 文本
 * @return void 无返回值
 */
void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_text")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_TEXT;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.x1 = x; op.y1 = y;
    op.text = (text && *text) ? _strdup(text) : NULL;
    canvas_push(w, &op);
}

/* ------------------------------------------------------------ 表格增强 */

/**
 * @brief 给表格某行绑一个业务数值（不显示在表格里，取回用）
 * @param w table 控件
 * @param row 行号（0 起）
 * @param value 要存的整数
 * @return int 0 成功，-1 参数不对
 */
int cg_table_set_row_value(cg_widget_t *w, int row, int value)
{
    HWND h;
    LVITEMW it;
    if (!w || !is_t(w, "table")) return -1;
    if (row < 0 || row >= w->nrows) return -1;
    cg_table_rows_grow(w, w->nrows);
    w->row_val[row] = value;
    h = (HWND)w->native;
    if (!h) return 0;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = row;
    it.lParam = value;
    SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
    return 0;
}

/**
 * @brief 读某行绑定的业务数值
 * @param w table 控件
 * @param row 行号（0 起）
 * @return int 绑定的数值；参数不对返回 0
 */
int cg_table_row_value(const cg_widget_t *w, int row)
{
    if (!w || !is_t(w, "table")) return 0;
    if (row < 0 || row >= w->nrows) return 0;
    return (w->row_val && row < w->row_cap) ? w->row_val[row] : 0;
}

/**
 * @brief 设置某行的勾选状态（.cgui 里 table 要开 check）
 * @param w table 控件
 * @param row 行号（0 起）
 * @param on 1 = 勾上，0 = 取消
 * @return void 无返回值（程序设置的勾选不触发 check 事件）
 */
void cg_table_set_checked(cg_widget_t *w, int row, int on)
{
    HWND h;
    LVITEMW it;
    if (!w || !is_t(w, "table")) return;
    if (row < 0 || row >= w->nrows) return;
    cg_table_rows_grow(w, w->nrows);
    if (w->row_checked) w->row_checked[row] = on ? 1 : 0;
    h = (HWND)w->native;
    if (!h || !w->check) return;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_STATE;
    it.state = on ? INDEXTOSTATEIMAGEMASK(2) : INDEXTOSTATEIMAGEMASK(1);
    it.stateMask = LVIS_STATEIMAGEMASK;
    SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&it);
}

/**
 * @brief 查某行是否被勾选
 * @param w table 控件
 * @param row 行号（0 起）
 * @return int 1 = 勾选，0 = 没勾 / 参数不对
 */
int cg_table_checked(const cg_widget_t *w, int row)
{
    if (!w || !is_t(w, "table")) return 0;
    if (row < 0 || row >= w->nrows) return 0;
    return (w->row_checked && row < w->row_cap) ? w->row_checked[row] : 0;
}

/**
 * @brief 设置某行行首图标；path 传 NULL 清除
 * @param w table 控件
 * @param row 行号（0 起）
 * @param path 图片路径（png/jpg/bmp/gif，查找规则同 image 控件）
 * @return void 无返回值
 */
void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path)
{
    HWND h;
    HIMAGELIST il;
    int idx;
    if (!w || !is_t(w, "table")) return;
    if (row < 0 || row >= w->nrows) return;
    cg_table_rows_grow(w, w->nrows);
    free(w->row_icon[row]);
    w->row_icon[row] = (path && *path) ? _strdup(path) : NULL;

    h = (HWND)w->native;
    if (!h) return;
    if (!w->row_icon[row]) {
        /* 清图标：指向不存在的下标即可 */
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_IMAGE;
        it.iItem = row;
        it.iImage = -1;
        SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
        InvalidateRect(h, NULL, FALSE);
        return;
    }
    il = table_imagelist(h, w);
    idx = imagelist_add_file(w, il, w->row_icon[row], scale_px(w->app, 16));
    if (idx >= 0) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_IMAGE;
        it.iItem = row;
        it.iImage = idx;
        SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
    }
    InvalidateRect(h, NULL, FALSE);
}

/**
 * @brief 多选表格当前选中了几行
 * @param w table 控件
 * @return int 选中行数（单选表格最多 1）
 */
int cg_table_selected_count(const cg_widget_t *w)
{
    if (!w || !is_t(w, "table") || !w->native) return 0;
    return (int)SendMessageW((HWND)w->native, LVM_GETSELECTEDCOUNT, 0, 0);
}

/**
 * @brief 取第 i 个选中的行号（配合 cg_table_selected_count 遍历）
 * @param w table 控件
 * @param index 第几个选中行（0 起）
 * @return int 行号；没有了返回 -1
 */
int cg_table_selected_row(const cg_widget_t *w, int index)
{
    int row = -1;
    if (!w || !is_t(w, "table") || !w->native || index < 0) return -1;
    for (int i = 0; i <= index; i++) {
        row = (int)SendMessageW((HWND)w->native, LVM_GETNEXTITEM, (WPARAM)row,
                                MAKELPARAM(LVNI_SELECTED, 0));
        if (row == -1) return -1;
    }
    return row;
}

/* ------------------------------------------------------------ 网页浏览 */

/**
 * @brief 让 browser 控件打开一个地址（实验性：IE 内核，仅本机文件 / 简单页面）
 * @param w browser 控件
 * @param url http/https 地址或本地文件完整路径
 * @return void 无返回值
 */
void cg_browser_navigate(cg_widget_t *w, const char *url)
{
    if (!w || !is_t(w, "browser")) return;
    free(w->src);
    w->src = (url && *url) ? _strdup(url) : NULL;
    sync_widget(w);   /* sync 发现 src 变了会整个重建 AtlAxWin */
}

const cg_backend_t cg_backend_win32 = {
    "win32",
    win_init,
    win_run,
    win_shutdown,
    sync_widget,
    create_native,
    destroy_native,
    win_set_title,
};
