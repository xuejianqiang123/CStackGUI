/**
 * @file    cg_win_internal.h
 * @brief   Win32 后端各子模块之间的共享接口（业务代码不应 include）
 * @details cg_win32.c 曾是一个 4200 多行的单体，现按职责拆成 cg_win_*.c 若干子模块。
 *          本头文件集中放三样东西：
 *            1. 后端内部共享的类型与窗口类名宏；
 *            2. GDI+ 的按 ABI 手写声明（MinGW 的 gdiplusflat.h 在 C 下编不过）；
 *            3. 跨模块使用的内部函数与状态变量声明——**只给后端自己用**，
 *               不对外暴露、也不进 cstackgui.h。
 *          分层规矩：子模块之间只允许通过本头文件互相调用，不要 include 对方的 .c；
 *          业务代码只认 cstackgui.h。
 */
#ifndef CG_WIN_INTERNAL_H
#define CG_WIN_INTERNAL_H

/* 明确走 W 系列 API：必须在 windows.h 之前定义 */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"
#include "cg_skin.h"   /* M3 自绘外观层：owner-draw 按钮走这里画 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>   /* 托盘图标 Shell_NotifyIcon */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================ 后端内部类型 */

typedef struct cg_font_node {
    char          *key;
    HFONT          hfont;
    struct cg_font_node *next;
} cg_font_node_t;

/** 后端私有状态，挂在 cg_app_t::natives 上（谁初始化谁释放：cgw_win_init / win_shutdown） */
typedef struct {
    cg_font_node_t *fonts;
    HBRUSH          bg_brush;
    COLORREF        bg_color;
    int             dpi;
    int             focused_widget;
    UINT_PTR        timer_id;
    HWND            tooltip;    /* 整窗一个的悬停提示控件 */
    HICON           icon;       /* 我们自己 LoadImage 出来的窗口图标，退出时要 DestroyIcon */
    int             tray_on;    /* 托盘图标已建 */
    int             activated;  /* 首次激活已经发过了 */
    unsigned long   tray_click_tick; /* 托盘左键单击/双击判定 */
} cg_win_state_t;

/* ============================================================ 窗口类名 / 常量 */

#define CG_PANEL_CLASS L"CStackGUIPanel"
#define CG_IMAGE_CLASS L"CStackGUIImage"
#define CG_RING_CLASS  L"CStackGUIRing"
#define CG_PROGRESS_CLASS L"CStackGUIProgress"
#define CG_CANVAS_CLASS L"CStackGUICanvas"

/* 时钟（timer）控件没有自己的窗口，Win32 定时器 id 直接复用 ctrl_id，
 * 这样 WM_TIMER 里按 wp 反查一次就能找到控件。 */
#define CG_TIMER_ID    1

/* 托盘图标的回调消息。放 WM_APP 段，避免和窗口消息撞号。 */
#define CG_TRAY_CALLBACK (WM_APP + 1)
#define CG_TRAY_ID       1

/* ============================================================ GDI+
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

/* ============================================================ 跨模块接口
 * 下面按「定义在哪个子模块」分组。函数为方便纯代码搬移沿用了拆分前的名字，
 * 但它们仍属后端私有，别从业务代码里调。
 */

/* ---------------- cg_win_util.c：通用工具 ---------------- */

/** @brief UTF-8 转 UTF-16 堆串（调用方 free）；空/失败返回 NULL */
wchar_t *cgw_wc_from_utf8(const char *s);
/** @brief 逻辑像素转物理像素（按 app 的 DPI 百分比四舍五入） */
int cgw_scale_px(const cg_app_t *app, int logical);
/** @brief 取（或缓存创建）一个 HFONT；size_pt<=0 返回 NULL 走系统默认 */
HFONT cgw_font_for(cg_app_t *app, const char *family, int size_pt, int bold, int underline);
/** @brief 0xRRGGBB 转 COLORREF */
COLORREF cgw_to_colorref(unsigned c);
/** @brief 取一个按颜色缓存的实心画刷，供 WM_CTLCOLOR* 返回 */
HBRUSH cgw_brush_for(unsigned rgb);
/** @brief 控件类型判断（NULL 安全） */
int cgw_is_t(const cg_widget_t *w, const char *type);
/** @brief 是否输入类控件（edit/multiedit/spinner/richtext） */
int cgw_is_editlike(const cg_widget_t *w);
/** @brief 解析资源路径（相对 .cgui 目录 / exe 目录 / 绝对），成功返回 0 */
int cgw_resolve_asset(const cg_app_t *app, const char *path, char *out, size_t outsz);
/** @brief 把图片文件读成 HBITMAP（走 GDI+），失败返回 NULL */
HBITMAP cgw_load_image_file(const cg_app_t *app, const char *path);
/** @brief 把 .ico 读成 HICON，失败返回 NULL */
HICON cgw_load_icon_file(const cg_app_t *app, const char *path);
/** @brief 鼠标指针名 -> 系统指针 id（.cgui 的 app.cursor） */
LPCWSTR cgw_cursor_id_of(const char *name);

/** GDI+ 启动令牌，0 表示没起来；cgw_win_init 起、win_shutdown 停 */
extern ULONG_PTR g_win_gdip_token;

/* ---------------- cg_win_ctl.c：控件映射与背景色 ---------------- */

/** 富文本窗口类名，Msftedit.dll 起来后是 RICHEDIT50W，否则 NULL（退回 EDIT） */
extern const wchar_t *g_win_richedit_class;
/** atl.dll 是否可用（browser 控件用 AtlAxWin 宿主 WebBrowser） */
extern int g_win_atl_ok;

/** @brief 控件 -> Win32 窗口类名（group/box/card/shape 走自注册 Panel 类） */
const wchar_t *cgw_class_of(const cg_widget_t *w);
/** @brief datepicker 是否为"时间"格式 */
int cgw_is_time_only(const cg_widget_t *w);
/** @brief datepicker 是否为"日期+时间"双控件模式 */
int cgw_is_datetime(const cg_widget_t *w);
/** @brief 控件 -> WS 样式位 */
DWORD cgw_style_of(const cg_widget_t *w);

/** @brief 控件 -> WS_EX 扩展样式位 */
DWORD cgw_exstyle_of(const cg_widget_t *w);
/** @brief 自绘控件画内容前，先把整块填成"背后"的颜色 */
void cgw_clear_to_behind(cg_widget_t *w, HDC dc, const RECT *rc);
/** @brief WM_CTLCOLOR* 的共用处理；返回 NULL 表示交给系统默认 */
HBRUSH cgw_ctlcolor_brush(cg_app_t *app, WPARAM wp, LPARAM lp);
/** @brief 把 text 里的日期时间写法解析成 SYSTEMTIME */
void cgw_datetime_from_text(const cg_widget_t *w, SYSTEMTIME *date, SYSTEMTIME *time);
/** @brief 把控件里的日期时间写回 text */
void cgw_datetime_pull_text(cg_widget_t *w, HWND date_hwnd, HWND time_hwnd);

/* ---------------- cg_win_draw.c：自绘窗口类 ---------------- */

void cgw_register_image_class(void);
void cgw_register_ring_class(void);
void cgw_register_progress_class(void);
void cgw_register_canvas_class(void);
void cgw_register_panel_class(void);
/** @brief 面板窗口过程（group/box/card/shape）：转发子控件通知 + 自绘底板 */
LRESULT CALLBACK cgw_PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ---------------- cg_win_ownerdraw.c：owner-draw 项绘制 ---------------- */

/** @brief WM_DRAWITEM 统一分发：按钮/勾选/开关交给 cg_skin，其余交给各 draw_* */
void cgw_draw_own_item(cg_app_t *app, const DRAWITEMSTRUCT *dis);
/** @brief 把一条原生通知（code/cmd_id）归一后交给 cg_dispatch */
void cgw_notify_from(cg_app_t *app, HWND hwnd, int code, int cmd_id);

/* ---------------- cg_win_create.c：原生控件创建 ---------------- */

/** @brief 按原生窗口反查控件（同时比对 native 与 native2），找不到返回 NULL */
cg_widget_t *cgw_widget_by_hwnd(cg_app_t *app, HWND h);
/** @brief 控件的原生父窗口（容器控件有原生窗口就是容器子控件） */
HWND cgw_parent_hwnd_of(const cg_app_t *app, const cg_widget_t *w);
/** @brief 建立某控件的原生窗口（menu/timer 除外），并做首次同步 */
void cgw_create_native(cg_widget_t *w);
/** @brief 递归建原生控件树（父先于子） */
void cgw_create_native_tree(cg_widget_t *parent);
/** @brief 销毁控件的原生窗口并释放相关资源（位图 / imagelist / 映射表） */
void cgw_destroy_native(cg_widget_t *w);

/* ---------------- cg_win_sync.c：状态同步 ---------------- */

/** @brief 切页后把 tabs 的当前页子控件刷新到最新 */
void cgw_sync_tabs_pages(cg_widget_t *tabs);
/** @brief 把一个图标文件加进 imagelist，返回下标，失败 -1 */
int cgw_imagelist_add_file(cg_widget_t *w, HIMAGELIST il, const char *path, int cell);
/** @brief 惰性创建表格的 imagelist */
HIMAGELIST cgw_table_imagelist(HWND h, cg_widget_t *w);
/** @brief 原生控件当前文本是否已经是 wt */
int cgw_hwnd_text_is(HWND h, const wchar_t *wt);
/** @brief 把控件逻辑状态同步到原生控件（入口带防重入保护） */
void cgw_sync_widget(cg_widget_t *w);

/* ---------------- cg_win_notify.c：通知映射 ---------------- */

/** @brief 把编辑框当前文字拉回控件模型 */
void cgw_pull_edit_text(cg_widget_t *w, HWND hwnd);
/** @brief 通知映射：原生通知 -> CStackGUI 事件（cmd_id 是 WM_COMMAND 的 LOWORD） */
void cgw_notify_body(cg_app_t *app, cg_widget_t *w, HWND hwnd, int code, int cmd_id);
/** @brief WM_NOTIFY：标签页 / 树 / 表格 / 微调 四类通知的公共路由 */
LRESULT cgw_notify_common(cg_app_t *app, const NMHDR *hdr);

/* ---------------- cg_win_event.c：焦点子类与窗口事件 ---------------- */

/** 防重入开关：回调里改控件会再触发原生通知，靠它挡住嵌套（别删） */
extern int g_win_in_notify;
/** 全局焦点子类要用的当前应用 */
extern cg_app_t *g_win_app;

/** @brief 控件统一子类过程：焦点事件、通知转发、评分/分页点击判定 */
LRESULT CALLBACK cgw_FocusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
/** @brief 给控件套统一子类（每个控件一次） */
void cgw_subclass_for_events(HWND h);
/** @brief 把一个控件注册进 tooltip（文本用 LPSTR_TEXTCALLBACK 现取） */
void cgw_tooltip_add_for(HWND tooltip, HWND forw);

/* ---------------- cg_win_main.c：主窗口过程 ---------------- */

/** @brief 向"代表主窗口"的伪控件派发一个窗口级事件 */
void cgw_win_emit(cg_app_t *app, cg_event_t ev);
/** @brief 主窗口过程：创建/通知/绘制/配色/DPI/定时器/窗口事件/关闭处理 */
LRESULT CALLBACK cgw_MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ---------------- cg_win_menu.c：菜单与工具栏 ---------------- */

/** @brief 菜单按钮：在按钮下方弹出菜单，选中项直接派发 */
void cgw_popup_menu_for(cg_app_t *app, cg_widget_t *w, HWND hwnd);
/** @brief 由控件的菜单项数组建一个 HMENU（调用方 DestroyMenu）。
 *  as_bar=1 建**菜单栏**（窗口挂载用，必须 CreateMenu）；as_bar=0 建弹出菜单
 *  （menubutton / TrackPopupMenu 用，必须 CreatePopupMenu）。
 *  两者不能混：给 SetMenu 传 CreatePopupMenu 的表会静默失败 —— 菜单栏就是不显示。 */
HMENU cgw_menu_build(const cg_widget_t *w, int as_bar);
/** @brief 填充工具栏按钮（文字按钮 + BTNS_SHOWTEXT） */
void cgw_build_toolbar(HWND h, cg_widget_t *w);

/* ---------------- cg_win_window.c：窗口生命周期 ---------------- */

/** @brief 按顺序初始化：DPI、注册自绘类、建窗口、tooltip、定时器、建控件树 */
int cgw_win_init(cg_app_t *app);

#endif /* CG_WIN_INTERNAL_H */
