/**
 * @file    cg_win_ctl.c
 * @brief   控件到原生窗口类/样式的映射、日期时间文本转换、控件背景色推导与 WM_CTLCOLOR
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 控件映射 */

/** 富文本控件类：Msftedit.dll 起来后用 RICHEDIT50W，否则退回旧版甚至普通 EDIT。
 *  富文本不是系统的必备组件，所以这里做降级而不是直接失败。 */
const wchar_t *g_win_richedit_class = NULL;

/** browser 控件用 atl.dll 的 AtlAxWin 宿主系统 WebBrowser 控件；
 *  起不来（理论上看不到）就退回静态占位文本。 */
int g_win_atl_ok = 0;

/** @brief 控件 -> Win32 窗口类名（group/box/card/shape 走自注册 Panel 类） */
const wchar_t *cgw_class_of(const cg_widget_t *w)
{
    if (!strcmp(w->type, "button"))    return L"BUTTON";      /* 自绘：BS_OWNERDRAW（cg_skin 画 M3 圆角） */
    if (!strcmp(w->type, "menubutton"))return L"BUTTON";      /* 普通按钮，点一下弹菜单 */
    if (!strcmp(w->type, "iconbutton"))return L"BUTTON";      /* BS_OWNERDRAW：画图标 */
    if (!strcmp(w->type, "label"))     return L"STATIC";      /* 静态文本，SS_* 决定对齐 */
    if (!strcmp(w->type, "link"))      return L"STATIC";      /* SS_NOTIFY 才有 STN_CLICKED */
    if (!strcmp(w->type, "edit"))      return L"EDIT";        /* 单行输入框（运行时做圆角） */
    if (!strcmp(w->type, "multiedit")) return L"EDIT";        /* 多行输入框（运行时做圆角） */
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
    if (!strcmp(w->type, "spinner"))   return L"EDIT";        /* 数字框：edit + 箭头 native2 */
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

/** 日期时间：是"日期"还是"时间"，决定 DTS_* 样式 */
int cgw_is_time_only(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "time") == 0;
}

/** @brief datepicker 是否为"日期+时间"双控件模式（format == "datetime"） */
int cgw_is_datetime(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "datetime") == 0;
}

/** @brief 分隔条是不是竖的（左右分栏）：format 缺省 "col" 是竖条，"row" 才是横条 */
int cgw_splitter_is_vert(const cg_widget_t *w)
{
    return !(w->format && strcmp(w->format, "row") == 0);
}

/** @brief 控件 -> WS 样式位（table 按 multi 决定单/多选） */
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
    if (!strcmp(w->type, "edit"))      return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT;
    if (!strcmp(w->type, "multiedit")) return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
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
    /* 微调框：数字左对齐（原先 ES_RIGHT 在窄框里会贴着箭头，很难看） */
    if (!strcmp(w->type, "spinner"))   return WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_LEFT | ES_AUTOHSCROLL;
    /* group / box：自己注册的面板类，能装子控件。
     * 边框和标题都自己画，**不能用 WS_BORDER** —— 系统重绘非客户区时会盖掉标题，
     * 看起来就是"边框线穿过文字"。 */
    return WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
}

/** @brief 控件 -> 扩展样式位（输入类控件给立体边框） */
DWORD cgw_exstyle_of(const cg_widget_t *w)
{
    /* edit/multiedit 的边框由 cg_skin 自绘（灰圆角+聚焦紫），
     * WS_EX_CLIENTEDGE 的凹陷立体边和圆角裁切叠加会把内容切没 */
    if (!strcmp(w->type, "edit"))    return 0;
    if (!strcmp(w->type, "multiedit")) return 0;
    if (!strcmp(w->type, "richtext")) return WS_EX_CLIENTEDGE;
    if (!strcmp(w->type, "listbox")) return WS_EX_CLIENTEDGE;
    return 0;
}

/* ------------------------------------------------------------ 控件背景（WM_CTLCOLOR*） */

/* 系统色索引最大到 30（COLOR_MENUBAR）；比它大的值一定是真画刷句柄。
 * MinGW 的 winuser.h 没定义 COLOR_ENDCOLORS，这里补一个。 */
#ifndef COLOR_ENDCOLORS
#define COLOR_ENDCOLORS 30
#endif

/** @brief COLORREF(0x00BBGGRR) -> 0xRRGGBB */
static unsigned rgb_of(COLORREF c)
{
    return ((unsigned)GetRValue(c) << 16) | ((unsigned)GetGValue(c) << 8) | (unsigned)GetBValue(c);
}

/* ---- 让父容器"自己画"出背后那一小块，取它的真实颜色 ----
 * 标签页的页体是主题画的（本机 #F9F9F9）：既不是类背景刷（BTNFACE 灰）也不是
 * COLOR_WINDOW（纯白），而且它裁掉子窗口（子控件下面什么都没画，所以不能"不画背景"）。
 * 与其猜颜色（uxtheme 的 GetThemeColor 对 TAB 返回的是没用的高亮色），不如让父窗口
 * 把子控件中心那一小块画进 1x1 位图、读像素 —— 什么主题、什么渐变都对得上。
 * 结果按父窗口缓存，一个容器只采样一次。 */
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
 *  @return unsigned 0xRRGGBB；采样失败退回纯白
 */
static unsigned parent_body_rgb(HWND parent, HWND child)
{
    static struct { HWND parent; unsigned rgb; } cache[8];
    static int used = 0;
    COLORREF c;
    unsigned rgb;
    int i;

    for (i = 0; i < used; i++) {
        if (cache[i].parent == parent) return cache[i].rgb;
    }
    rgb = 0xFFFFFF;
    if (parent_paint_point(parent, child, &c)) rgb = rgb_of(c);
    if (used < 8) {
        cache[used].parent = parent;
        cache[used].rgb = rgb;
        used++;
    }
    return rgb;
}

/** @brief 某个控件"背后"是什么颜色（0xRRGGBB）
 *  @param app 所属应用；hwnd 目标控件（按它的**直接父窗口**判断"背后"是谁）
 *  @return unsigned 0xRRGGBB
 *  @details 主窗口上的控件 -> 窗口底色；分组框 / 容器块 / 卡片 / 图形里的 -> 该面板的
 *           style.bg（没设就用它类背景的系统色）；标签页里的 -> 采样页体真实像素。
 *           无底色标签、以及"清掉自身底色"的自绘按钮/开关/勾选框都用它，
 *           否则它们会把窗口底色画到带底色的面板上，露出一块方块。
 */
unsigned cg_win_behind_rgb(cg_app_t *app, void *hwnd)
{
    cg_win_state_t *st;
    HWND child = (HWND)hwnd;
    HWND parent = child ? GetParent(child) : NULL;
    cg_widget_t *pw;
    ULONG_PTR v;

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
        v = (ULONG_PTR)GetClassLongPtrW(parent, GCLP_HBRBACKGROUND);
        if (v == 0 || v > (ULONG_PTR)(COLOR_ENDCOLORS + 1)) return rgb_of(st->bg_color);
        return rgb_of(GetSysColor((int)v - 1));
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
 *  @return HBRUSH 直接当 WM_CTLCOLOR* 的返回值用
 */
static HBRUSH transparent_bg_for(cg_app_t *app, HWND child)
{
    return cgw_brush_for(cg_win_behind_rgb(app, child));
}

/** @brief 自绘控件画自己的内容前，先把整块填成"背后"的颜色
 *  @details owner-draw 按钮/自绘控件默认会铺一层系统面色（BTNFACE），不清掉就是一块灰底；
 *           徽章 / 标签片 / 评分这类"只在中间画一块"的控件必须清，"背后"是什么就填什么。
 */
void cgw_clear_to_behind(cg_widget_t *w, HDC dc, const RECT *rc)
{
    FillRect(dc, rc, cgw_brush_for(cg_win_behind_rgb(w->app, w->native)));
}

/** @brief WM_CTLCOLOR* 的共用处理：决定控件文字/背景色该用哪个画刷
 *  @param app 所属应用；wp 目标 DC；lp 那个控件窗口
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
 * @param w datepicker 控件；date/time 输出：日期与时间
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

/** 把控件里的日期时间写回 text（回调里用 cg_widget_text 读） */
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
