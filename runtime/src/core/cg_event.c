/**
 * @file    cg_event.c
 * @brief   事件层：事件名表、事件参数读取、回调绑定解析、事件派发中枢
 * @details 从原 cg_app.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 用指定下标初始化：以后往枚举里插事件也不会把名字和枚举对错 */
static const char *const k_ev_names[CG_EVENT_COUNT] = {
    [CG_EVENT_CLICK]    = "click",
    [CG_EVENT_DBLCLICK] = "dblclick",
    [CG_EVENT_CHANGE]   = "change",
    [CG_EVENT_SUBMIT]   = "submit",
    [CG_EVENT_SELECT]   = "select",
    [CG_EVENT_RELEASE]  = "release",
    [CG_EVENT_EXPAND]   = "expand",
    [CG_EVENT_COLLAPSE] = "collapse",
    [CG_EVENT_CLOSE]    = "close",
    [CG_EVENT_TICK]     = "tick",
    [CG_EVENT_CHECK]    = "check",
    [CG_EVENT_GOTFOCUS] = "gotfocus",
    [CG_EVENT_LOSTFOCUS] = "lostfocus",
    [CG_EVENT_ARRIVE]   = "arrive",
    /* 窗口级事件（绑在 __window 上，写在 .cgui 的 app.events 里） */
    [CG_EVENT_READY]         = "ready",
    [CG_EVENT_DESTROY]       = "destroy",
    [CG_EVENT_MOVE]          = "move",
    [CG_EVENT_SIZE]          = "size",
    [CG_EVENT_ACTIVATE]      = "activate",
    [CG_EVENT_DEACTIVATE]    = "deactivate",
    [CG_EVENT_FIRSTACTIVATE] = "firstactivate",
    [CG_EVENT_IDLE]          = "idle",
    [CG_EVENT_TRAY]          = "tray",
    [CG_EVENT_SHOW]          = "show",
    [CG_EVENT_HIDE]          = "hide",
    [CG_EVENT_CLOSEQUERY]    = "closequery",
    [CG_EVENT_MOUSEDOWN]     = "mousedown",
    [CG_EVENT_MOUSEUP]       = "mouseup",
    [CG_EVENT_RDOWN]         = "rdown",
    [CG_EVENT_RUP]           = "rup",
    [CG_EVENT_MOUSEMOVE]     = "mousemove",
    [CG_EVENT_KEYDOWN]       = "keydown",
    [CG_EVENT_KEYUP]         = "keyup",
    [CG_EVENT_CHAR]          = "char",
    [CG_EVENT_WHEEL]         = "wheel",
    [CG_EVENT_SPIN]          = "spin",
};

/** @brief 事件名的规范写法，也是 .cgui 中 events 对象的键名 */
/** @brief 事件枚举转规范名（"click"...）；越界返回 "?" */
const char *cg_event_name(cg_event_t ev)
{
    return (ev >= 0 && ev < CG_EVENT_COUNT) ? k_ev_names[ev] : "?";
}

/** @brief 名字反查事件 */
/** @brief 事件名反查枚举；不认识返回 -1 */
int cg_event_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (strcmp(k_ev_names[i], name) == 0) return i;
    }
    return -1;
}

/* ------------------------------------------------------------ 事件参数
 * 回调签名固定三个参数，鼠标/按键/滚轮/托盘的参数没法直接传，
 * 就由后端在派发前写进控件的 ev_* 字段，回调里用下面这些读取。
 */
/** @brief 最近一次鼠标事件的 x（相对窗口客户区）；没有则 0 */
int cg_event_x(const cg_widget_t *self) { return self ? self->ev_x : 0; }
/** @brief 最近一次鼠标事件的 y（相对窗口客户区）；没有则 0 */
int cg_event_y(const cg_widget_t *self) { return self ? self->ev_y : 0; }
/** @brief 功能键状态：1=Ctrl 2=Shift 4=Alt（对齐） */
int cg_event_keys(const cg_widget_t *self) { return self ? self->ev_keys : 0; }
/** @brief 键代码（keydown/keyup 是虚拟键码，char 是字符码） */
int cg_event_key(const cg_widget_t *self) { return self ? self->ev_key : 0; }
/** @brief 滚轮滚动量（正数向上） */
int cg_event_delta(const cg_widget_t *self) { return self ? self->ev_delta : 0; }
/** @brief 托盘动作：1=左键单击 2=左键双击 3=右键单击 */
int cg_tray_action(const cg_widget_t *self) { return self ? self->ev_action : 0; }

/* ------------------------------------------------------------ 绑定 */

/* 启动期"名字没登记"清单：.cgui 与 C 代码靠名字耦合，名字写错不编译报错。
 * 这里把名字记下来，cg_app_run 启动后汇总弹一次（比在 stderr 飘一行有用得多）。 */
#define CG_MAX_UNRESOLVED 64

static const char *s_unresolved[CG_MAX_UNRESOLVED];
static int         s_unresolved_n;

/** @brief 记下一个没登记的回调名（name 指向文档树，生命周期同 app） */
static void unresolved_add(const char *name)
{
    if (s_unresolved_n >= CG_MAX_UNRESOLVED) return;
    s_unresolved[s_unresolved_n++] = name;
}

/** @brief 没登记的回调名个数 */
int cg_unresolved_count(void)
{
    return s_unresolved_n;
}

/** @brief 第 i 个没登记的回调名；越界返回 NULL */
const char *cg_unresolved_name(int i)
{
    return (i >= 0 && i < s_unresolved_n) ? s_unresolved[i] : NULL;
}

/**
 * @brief 依名字在绑定表里找函数指针
 * @param app 所属应用；name .cgui 里写的事件回调名
 * @return cg_event_fn 函数指针；找不到记一条日志/一条待提示并返回 NULL
 */
cg_event_fn cg_resolve_binding(cg_app_t *app, const char *name)
{
    if (!name || !*name) return NULL;
    if (!app || !app->bindings) {
        cg_log(CG_LOG_WARN, "事件绑定了 \"%s\"，但没有提供 bindings 表", name);
        unresolved_add(name);
        return NULL;
    }
    for (const cg_binding_t *b = app->bindings; b->name; b++) {
        if (strcmp(b->name, name) == 0) return b->fn;
    }
    cg_log(CG_LOG_WARN, ".cgui 引用了回调 \"%s\"，但 C 代码里没有 CG_BIND 它", name);
    unresolved_add(name);
    return NULL;
}

/** @brief 直接在 C 里给控件绑回调（覆盖 .cgui 里的名字绑定） */
/** @brief 直接给控件绑回调（覆盖 .cgui 里的名字绑定）；参数不对返回 -1 */
int cg_on(cg_widget_t *w, cg_event_t ev, cg_event_fn fn)
{
    if (!w || ev < 0 || ev >= CG_EVENT_COUNT) return -1;
    w->handlers[ev] = fn;
    return 0;
}

/** @brief 按 .cgui 里的名字绑定 */
/** @brief 按名字给控件绑回调（名字必须在绑定表里登记过）；失败返回 -1 */
int cg_on_named(cg_widget_t *w, cg_event_t ev, const char *callback_name)
{
    if (!w || !w->app || ev < 0 || ev >= CG_EVENT_COUNT) return -1;
    w->handlers[ev] = cg_resolve_binding(w->app, callback_name);
    return w->handlers[ev] ? 0 : -1;
}

/* ------------------------------------------------------------ 状态同步 */

/** @brief 把控件逻辑状态推给后端（无原生句柄时是空操作） */
void cg_widget_sync(cg_widget_t *w)
{
    if (!w) return;
    /* chart 的点缓冲要跟 items / value 对齐（改了窗口大小就得重建）。这一步是纯 core
     * 记账、和后端在不在没关系，所以放在 native 判断**之前** —— 单测（没有窗口）里
     * 也才查得到"和配置一致"的状态。配置没变时它只是一次比较。 */
    if (w->type && strcmp(w->type, "chart") == 0) cg_chart_ensure(w);
    if (cg_backend_active && cg_backend_active->sync && w->native) {
        cg_backend_active->sync(w);
    }
}

/* ------------------------------------------------------------ 事件派发 */

/**
 * @brief 事件派发中枢：校验事件支持、合成双击，再调注册的回调
 * @param w 触发控件；ev 事件类型
 * @return int 1 = 有回调处理了，0 = 没处理（不支持/未绑定/禁用）
 */
int cg_dispatch(cg_widget_t *w, cg_event_t ev)
{
    int handled = 0;

    if (!w || !w->app) return 0;
    if (!w->enabled && ev != CG_EVENT_CLOSE) return 0;

    /* 引用了动作的控件：click 交给动作。**放在最前面**是为了让每一条产生 click 的路径
     * （BN_CLICKED、自绘控件在 WM_LBUTTONUP 里的补派发、菜单项…）都自动走同一条路 ——
     * 否则得在十几个后端调用点各加一次判断，漏一个就是"画布上设了却没反应"。
     * 自己绑的那份 click 永远不会被调用（build 时已经警告过，这里不重复报）。 */
    if (ev == CG_EVENT_CLICK && w->action && *w->action) {
        return cg_action_invoke(w->app, w->action);
    }

    /* 控件状态（值/文字/勾选）是后端在派发前刚写好的，这里先把绑定同步做掉：
     * 回调里看到的界面就已经是一致的，不用自己去刷关联控件。
     * 注意绑定同步本身不派发事件，所以不会和下面的回调互相触发。 */
    cg_bind_propagate(w);

    /* 事件是按控件特性分配的：不在该类型支持范围里的，直接丢掉。
     * 正常画布不会绑出不支持的事件（画布只列支持的那些）；
     * 手写 .cgui 绑错了，cg_resolve_binding / 自检会报出来。 */
    if (!cg_kind_supports_event(w->type, ev)) return 0;

    /* 双击是运行时按系统双击间隔合成的（原生按钮不发 BN_DOUBLECLICKED）。
     * 只有支持 dblclick 的控件才需要这套判定；不支持的原样当两次单击。 */
    if (ev == CG_EVENT_CLICK && cg_kind_supports_event(w->type, CG_EVENT_DBLCLICK)) {
        unsigned long now = cg_ticks();
        if (w->dbl_pending && (now - w->last_click_tick) <= cg_double_click_ms()) {
            w->last_click_tick = now;
            w->dbl_pending = 0;
            if (!w->app->dblclick_suppress_click && w->handlers[CG_EVENT_CLICK]) {
                w->handlers[CG_EVENT_CLICK](w, CG_EVENT_CLICK, w->user);
                handled = 1;
                if (w->app->quit) return handled;
            }
            if (w->handlers[CG_EVENT_DBLCLICK]) {
                w->handlers[CG_EVENT_DBLCLICK](w, CG_EVENT_DBLCLICK, w->user);
                handled = 1;
            } else if (w->node && w->node->ev[CG_EVENT_DBLCLICK]) {
                /* 名字绑了但函数没登记，cg_resolve_binding 已报过警 */
            }
            return handled;
        }
        w->last_click_tick = now;
        w->dbl_pending = 1;
    }

    if (w->handlers[ev]) {
        w->handlers[ev](w, ev, w->user);
        handled = 1;
    }
    return handled;
}

/** @brief 后端子控件注册回调：原生事件 -> cg_dispatch */
/** @brief 后端子控件注册回调的入口（等同 cg_dispatch，供后端通知用） */
void cg_backend_notify(cg_widget_t *w, cg_event_t ev)
{
    cg_dispatch(w, ev);
}

/** @brief 菜单项被点中：记下是哪一项，再给对应控件派发 click */
/**
 * @brief 菜单命令 ID 反查控件，记下菜单项再派发 click
 * @param app 所属应用；cmd WM_COMMAND 里的菜单命令 ID
 * @return int 1 = 命中并派发，0 = 没找到（会打警告）
 */
int cg_dispatch_menu_cmd(cg_app_t *app, int cmd)
{
    if (!app || cmd == 0) return 0;

    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        for (int i = 0; i < w->nmenu; i++) {
            if (w->menu[i].cmd != cmd) continue;
            free(w->menu_last_id);
            free(w->menu_last_text);
            w->menu_last_id = w->menu[i].id ? cg_str_dup(w->menu[i].id) : NULL;
            w->menu_last_text = cg_str_dup(w->menu[i].text ? w->menu[i].text : "");
            /* 动作项：点它就是激活那条命令（不再给菜单控件派发 click，
             * 否则菜单项绑的 click 与动作会各走一遍，两个回调都响） */
            if (w->menu[i].action && *w->menu[i].action) {
                return cg_action_invoke(app, w->menu[i].action);
            }
            cg_dispatch(w, CG_EVENT_CLICK);
            return 1;
        }
    }
    fprintf(stderr, "[cstackgui] 收到未知的菜单命令 %d（该菜单项可能已被移除）\n", cmd);
    return 0;
}
