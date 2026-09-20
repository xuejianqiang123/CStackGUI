/**
 * @file    cg_app.c
 * @brief   应用骨架：控件树构建、事件绑定解析与派发、公共 API、--selftest 自检
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const cg_backend_t *cg_backend_active = NULL;

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

/* ------------------------------------------------------------ 控件生命周期 */

/** @brief 复制字符串到堆内存（调用方负责 free）；失败返回 NULL */
static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/** @brief 分配控件 ID（后端建控件时用），从 1001 起递增 */
/** @brief 分配一个控件 ID（1001 起递增，后端建控件用） */
int cg_alloc_ctrl_id(cg_app_t *app)
{
    return ++app->next_ctrl_id + 1000;
}

/** @brief 分配菜单命令 ID */
/** @brief 分配一个菜单命令 ID（0x8000 起，与控件 ID 分开发号）；超限返回 0 */
int cg_alloc_menu_cmd(cg_app_t *app)
{
    if (app->next_menu_cmd == 0) app->next_menu_cmd = 0x8000;
    /* 菜单命令 id 走 WM_COMMAND 的 LOWORD，留出上限避免和系统 id 撞 */
    if (app->next_menu_cmd >= 0xE000) {
        fprintf(stderr, "[cstackgui] 菜单项太多，超过 %d 个之后不再分配 id\n", 0xE000 - 0x8000);
        return 0;
    }
    return app->next_menu_cmd++;
}

/**
 * @brief 新建一个运行时控件（进全局链表，不建原生窗口）
 * @param app 所属应用；type 控件类型名；id 唯一标识
 * @param x,y,w,h 初始矩形；text 初始文字
 * @return cg_widget_t* 控件；失败返回 NULL
 */
cg_widget_t *cg_widget_new(cg_app_t *app, const char *type, const char *id,
                           int x, int y, int width, int height, const char *text)
{
    cg_widget_t *w = (cg_widget_t *)calloc(1, sizeof(cg_widget_t));
    if (!w) return NULL;

    w->app = app;
    w->type = dup_str(type ? type : "label");
    w->id = dup_str(id);
    w->text = dup_str(text ? text : "");
    w->x = x; w->y = y; w->w = width; w->h = height;
    w->visible = 1;
    w->enabled = 1;
    w->vmin = 0;
    w->vmax = 100;
    w->style.fg = CG_COLOR_NONE;
    w->style.bg = CG_COLOR_NONE;
    w->canvas_color = 0x333333;
    w->canvas_penw = 1;
    w->ctrl_id = cg_alloc_ctrl_id(app);

    w->next = app->widgets;
    app->widgets = w;
    return w;
}

/** @brief 释放控件的全部资源（结构化内容、画板记录、行级数组、文字等） */
void cg_widget_free(cg_widget_t *w)
{
    if (!w) return;
    for (int i = 0; i < w->nitems; i++) free(w->items[i]);
    free(w->items);
    for (int i = 0; i < w->ntree; i++) free(w->tree[i].text);
    free(w->tree);
    if (w->cols) {
        for (int i = 0; i < w->ncols; i++) free(w->cols[i].title);
        free(w->cols);
    }
    if (w->rows) {
        for (int r = 0; r < w->nrows; r++) {
            if (!w->rows[r]) continue;
            for (int c = 0; c < w->row_cells; c++) free(w->rows[r][c]);
            free(w->rows[r]);
        }
        free(w->rows);
    }
    for (int i = 0; i < w->nmenu; i++) {
        free(w->menu[i].text);
        free(w->menu[i].id);
    }
    free(w->menu);
    free(w->menu_last_id);
    free(w->menu_last_text);
    free(w->tool_ids);
    free(w->format);
    free(w->hint);
    if (w->ops) {
        for (int i = 0; i < w->nops; i++) {
            if (w->ops[i].type == CG_CANVAS_OP_TEXT) free(w->ops[i].text);
        }
        free(w->ops);
    }
    free(w->row_checked);
    free(w->row_val);
    for (int i = 0; i < w->row_icon_n; i++) free(w->row_icon[i]);
    free(w->row_icon);
    free(w->item_map);
    free(w->src);
    free(w->bitmap_src);
    free(w->id);
    free(w->type);
    free(w->text);
    free(w->style.font);
    free(w->children);
    free(w);
}

/** @brief 给运行时控件挂子控件（尾插，child->parent 回填；分配失败静默丢弃） */
static void widget_add_child(cg_widget_t *parent, cg_widget_t *child)
{
    cg_widget_t **nc = (cg_widget_t **)realloc(parent->children, (parent->nchildren + 1) * sizeof(cg_widget_t *));
    if (!nc) return;
    parent->children = nc;
    parent->children[parent->nchildren++] = child;
    child->parent = parent;
}

/** 把文档节点上的事件名解析成函数指针。
 *  顺便校验"这个控件到底支不支持这个事件"——事件是按控件特性分配的，
 *  绑错了这里就要说清楚，否则用户只能对着"点了没反应"猜。 */
static void bind_events(cg_app_t *app, cg_widget_t *w, const cg_node_t *n)
{
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        const cg_kind_info_t *ki;

        if (!n->ev[i]) continue;
        if (!cg_kind_supports_event(n->type, (cg_event_t)i)) {
            ki = cg_kind_lookup(n->type);
            fprintf(stderr,
                    "[cstackgui] 警告: %s(%s) 不支持 %s 事件，绑定 \"%s\" 被忽略；它支持的是: %s\n",
                    n->type ? n->type : "-", n->id ? n->id : "-",
                    cg_event_name((cg_event_t)i), n->ev[i],
                    cg_kind_event_list(ki ? ki->kind : CG_K_LABEL));
            app->unsupported_events++;
            continue;
        }
        w->handlers[i] = cg_resolve_binding(app, n->ev[i]);
    }
}

/** 把文档里的嵌套树节点摊平成运行时用的数组 */
static int tree_push(cg_widget_t *w, int parent, const char *text, int expanded)
{
    cg_tree_row_t *nt = (cg_tree_row_t *)realloc(w->tree, ((size_t)w->ntree + 1) * sizeof(cg_tree_row_t));
    int idx;

    if (!nt) return -1;
    w->tree = nt;
    idx = w->ntree++;
    w->tree[idx].text = dup_str(text ? text : "");
    w->tree[idx].parent = parent;
    w->tree[idx].expanded = expanded;
    return idx;
}

/** @brief 把文档里的嵌套树节点摊平进运行时行表（递归，父下标先于子） */
static void tree_push_doc(cg_widget_t *w, cg_item_t *const *items, size_t n, int parent)
{
    for (size_t i = 0; i < n; i++) {
        int idx;
        if (!items[i]) continue;
        idx = tree_push(w, parent, items[i]->text, items[i]->expanded);
        if (idx >= 0) tree_push_doc(w, items[i]->children, items[i]->nchildren, idx);
    }
}

/** 把 .cgui 的嵌套菜单项摊平成运行时的行表（父项一定排在子项前面） */
static void menu_push_doc(cg_app_t *app, cg_widget_t *w, cg_item_t *const *items, size_t n, int parent)
{
    for (size_t i = 0; i < n; i++) {
        cg_item_t *it = items[i];
        cg_menu_row_t *nr;
        int idx, is_sep;

        if (!it) continue;
        is_sep = (it->text && strcmp(it->text, "-") == 0);

        nr = (cg_menu_row_t *)realloc(w->menu, ((size_t)w->nmenu + 1) * sizeof(cg_menu_row_t));
        if (!nr) return;
        w->menu = nr;
        idx = w->nmenu++;
        memset(&w->menu[idx], 0, sizeof(cg_menu_row_t));
        w->menu[idx].text = dup_str(it->text ? it->text : "");
        w->menu[idx].id = it->id ? dup_str(it->id) : NULL;
        w->menu[idx].checked = it->checked;
        w->menu[idx].parent = parent;
        w->menu[idx].popup = (it->nchildren > 0);
        /* 分隔线和下拉项本身不可点，只有叶子项才有菜单命令 id */
        if (!is_sep && !w->menu[idx].popup) w->menu[idx].cmd = cg_alloc_menu_cmd(app);

        if (it->nchildren) menu_push_doc(app, w, it->children, it->nchildren, idx);
    }
}

/**
 * @brief 把文档节点转成运行时控件（拷贝状态/内容/回调，递归建子）
 * @param app 所属应用；parent 挂靠的父控件；n 文档节点
 * @return cg_widget_t* 控件；失败返回 NULL
 */
static cg_widget_t *build_node(cg_app_t *app, cg_widget_t *parent, const cg_node_t *n)
{
    cg_widget_t *w = cg_widget_new(app, n->type, n->id, n->x, n->y, n->w, n->h, n->text);
    if (!w) return NULL;

    w->node = (cg_node_t *)n;
    w->visible = n->visible;
    w->enabled = n->enabled;
    w->checked = n->checked;
    w->value = n->value;
    w->vmin = n->vmin;
    w->vmax = n->vmax;
    w->page = n->page;
    if (n->src) w->src = dup_str(n->src);
    if (n->format) w->format = dup_str(n->format);
    if (n->hint) w->hint = dup_str(n->hint);
    w->period = n->period;
    w->multi = n->multi;
    w->check = n->check;
    /* 菜单栏和菜单按钮共用同一套菜单行表；tree 虽然也用 nodes，但走的是下面那套摊平 */
    if (n->nodes && (!strcmp(n->type, "menu") || !strcmp(n->type, "menubutton"))) {
        menu_push_doc(app, w, n->nodes, n->nnodes, -1);
    }
    w->style = n->style;
    w->style.font = dup_str(n->style.font);   /* 拷贝一份，文档释放后控件仍可用 */
    w->tree_sel = -1;
    w->table_sel = -1;

    /* 结构化内容：文档 -> 运行时（各存一份，文档释放后控件仍可用） */
    if (n->items && n->nitems) {
        w->items = (char **)calloc(n->nitems, sizeof(char *));
        if (w->items) {
            for (size_t i = 0; i < n->nitems; i++) w->items[i] = dup_str(n->items[i]);
            w->nitems = (int)n->nitems;
            w->items_dirty = 1;
        }
    }
    if (n->nodes && n->nnodes) {
        tree_push_doc(w, n->nodes, n->nnodes, -1);
        w->items_dirty = 1;
    }
    if (n->cols && n->ncols) {
        w->cols = (cg_col_t *)calloc(n->ncols, sizeof(cg_col_t));
        if (w->cols) {
            for (size_t i = 0; i < n->ncols; i++) {
                w->cols[i].title = dup_str(n->cols[i].title);
                w->cols[i].width = n->cols[i].width;
            }
            w->ncols = (int)n->ncols;
            w->items_dirty = 1;
        }
    }
    if (n->rows && n->nrows && w->ncols > 0) {
        w->rows = (char ***)calloc(n->nrows, sizeof(char **));
        if (w->rows) {
            for (size_t r = 0; r < n->nrows; r++) {
                w->rows[r] = (char **)calloc((size_t)w->ncols, sizeof(char *));
                if (!w->rows[r]) continue;
                for (int c = 0; c < w->ncols; c++) {
                    const char *cell = (c < (int)n->row_cells && n->rows[r]) ? n->rows[r][c] : "";
                    w->rows[r][c] = dup_str(cell ? cell : "");
                }
            }
            w->nrows = (int)n->nrows;
            w->row_cells = w->ncols;
            w->items_dirty = 1;
        }
        /* 行级附加状态（勾选 / 数值 / 图标）按行数一起开好 */
        w->row_checked = (unsigned char *)calloc((size_t)w->nrows, 1);
        w->row_val = (int *)calloc((size_t)w->nrows, sizeof(int));
        w->row_icon = (char **)calloc((size_t)w->nrows, sizeof(char *));
        w->row_icon_n = w->nrows;
        w->row_cap = w->nrows;
    }

    /* .cgui 里写的回调名 -> 函数指针 */
    bind_events(app, w, n);
    widget_add_child(parent, w);

    for (size_t i = 0; i < n->nchildren; i++) {
        build_node(app, w, n->children[i]);
    }
    return w;
}

/** @brief 由文档树构建运行时控件树（不含原生句柄），失败返回 NULL */
/** @brief 由文档树构建整棵运行时控件树（窗口伪控件 + 递归），失败返回 NULL */
cg_widget_t *cg_build_widgets(cg_app_t *app)
{
    app->window = cg_widget_new(app, "window", "__window", 0, 0,
                                app->doc.width, app->doc.height,
                                app->doc.title);
    if (!app->window) return NULL;
    app->window->is_window = 1;
    app->window->style.bg = app->doc.bg;
    app->window->vmax = 0;

    if (app->doc.root) {
        /* 文档根上的事件 = 窗口级事件（目前只有 close） */
        bind_events(app, app->window, app->doc.root);
        for (size_t i = 0; i < app->doc.root->nchildren; i++) {
            build_node(app, app->window, app->doc.root->children[i]);
        }
    }
    return app->window;
}

/* ------------------------------------------------------------ 查找 */

/** @brief 按 id 在全局链表里找控件；找不到返回 NULL */
cg_widget_t *cg_widget_by_id(cg_app_t *app, const char *id)
{
    if (!app || !id) return NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->id && strcmp(w->id, id) == 0) return w;
    }
    return NULL;
}

/** @brief 按 Win32 控件 ID 找控件 */
/** @brief 按 Win32 控件 ID 找控件（WM_COMMAND 反查用）；找不到返回 NULL */
cg_widget_t *cg_widget_by_ctrl_id(cg_app_t *app, int ctrl_id)
{
    if (!app) return NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->ctrl_id == ctrl_id) return w;
    }
    return NULL;
}

/** @brief 按原生窗口句柄反查控件（同时比对 native 与 native2，spinner 有两个 HWND）；找不到返回 NULL */
cg_widget_t *cg_widget_by_hwnd(cg_app_t *app, void *hwnd)
{
    if (!app || !hwnd) return NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->native == hwnd || w->native2 == hwnd) return w;
    }
    return NULL;
}

/** @brief 按 id 找控件（cg_widget_by_id 的对外别名） */
cg_widget_t *cg_find(cg_app_t *app, const char *id) { return cg_widget_by_id(app, id); }

/** 控件个数：不含代表窗口自身的那个伪控件 */
int cg_widget_count(const cg_app_t *app)
{
    int n = 0;
    if (!app) return 0;
    for (const cg_widget_t *w = app->widgets; w; w = w->next) {
        if (!w->is_window) n++;
    }
    return n;
}

/* ------------------------------------------------------------ 绑定 */

/**
 * @brief 依名字在绑定表里找函数指针
 * @param app 所属应用；name .cgui 里写的事件回调名
 * @return cg_event_fn 函数指针；找不到打印一次警告并返回 NULL
 */
cg_event_fn cg_resolve_binding(cg_app_t *app, const char *name)
{
    if (!name || !*name) return NULL;
    if (!app || !app->bindings) {
        fprintf(stderr, "[cstackgui] 警告: 事件绑定了 \"%s\"，但没有提供 bindings 表\n", name);
        return NULL;
    }
    for (const cg_binding_t *b = app->bindings; b->name; b++) {
        if (strcmp(b->name, name) == 0) return b->fn;
    }
    fprintf(stderr, "[cstackgui] 警告: .cgui 引用了回调 \"%s\"，但 C 代码里没有 CG_BIND 它\n", name);
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
static void sync(cg_widget_t *w)
{
    if (cg_backend_active && cg_backend_active->sync && w && w->native) {
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
            w->menu_last_id = w->menu[i].id ? dup_str(w->menu[i].id) : NULL;
            w->menu_last_text = dup_str(w->menu[i].text ? w->menu[i].text : "");
            cg_dispatch(w, CG_EVENT_CLICK);
            return 1;
        }
    }
    fprintf(stderr, "[cstackgui] 收到未知的菜单命令 %d（该菜单项可能已被移除）\n", cmd);
    return 0;
}

/* ------------------------------------------------------------ 公共读取
 * 这些读取函数的完整语义见 cstackgui.h；NULL 控件统一兜底，不会崩。
 */

/** @brief 从控件拿到它所属的应用；NULL 返回 NULL */
cg_app_t   *cg_app_of(const cg_widget_t *w)      { return w ? w->app : NULL; }
/** @brief 控件 id；NULL 返回 NULL */
const char *cg_widget_id(const cg_widget_t *w)   { return w ? w->id : NULL; }
/** @brief 控件类型名；NULL 返回 NULL */
const char *cg_widget_type(const cg_widget_t *w) { return w ? w->type : NULL; }
/** @brief 控件当前文字；NULL 控件返回 NULL，文字为空返回空串 */
const char *cg_widget_text(const cg_widget_t *w) { return w ? (w->text ? w->text : "") : NULL; }
/** @brief 控件位置 x（逻辑像素，相对父容器）；NULL 返回 0 */
int cg_widget_x(const cg_widget_t *w) { return w ? w->x : 0; }
/** @brief 控件位置 y；NULL 返回 0 */
int cg_widget_y(const cg_widget_t *w) { return w ? w->y : 0; }
/** @brief 控件宽度；NULL 返回 0 */
int cg_widget_w(const cg_widget_t *w) { return w ? w->w : 0; }
/** @brief 控件高度；NULL 返回 0 */
int cg_widget_h(const cg_widget_t *w) { return w ? w->h : 0; }
/** @brief 是否可见；NULL 返回 0 */
int cg_widget_visible(const cg_widget_t *w) { return w ? w->visible : 0; }
/** @brief 是否可用；NULL 返回 0 */
int cg_widget_enabled(const cg_widget_t *w) { return w ? w->enabled : 0; }
/** @brief 是否勾选；NULL 返回 0 */
int cg_widget_checked(const cg_widget_t *w) { return w ? w->checked : 0; }
/** @brief 当前值（滑块位置 / 列表选中下标等）；NULL 返回 0 */
int cg_widget_value(const cg_widget_t *w)   { return w ? w->value : 0; }

/* ------------------------------------------------------------ 公共修改 */

/** @brief 改控件文字（同步到界面与文档节点） */
void cg_widget_set_text(cg_widget_t *w, const char *text)
{
    if (!w) return;
    free(w->text);
    w->text = dup_str(text ? text : "");
    if (w->node) { free(w->node->text); w->node->text = dup_str(w->text); }
    sync(w);
}

/** @brief 一次改位置和尺寸（逻辑像素，相对父容器） */
void cg_widget_set_rect(cg_widget_t *w, int x, int y, int ww, int hh)
{
    if (!w) return;
    w->x = x; w->y = y; w->w = ww; w->h = hh;
    if (w->node) { w->node->x = x; w->node->y = y; w->node->w = ww; w->node->h = hh; }
    sync(w);
}

/** @brief 只改位置 */
void cg_widget_set_pos(cg_widget_t *w, int x, int y)  { if (w) cg_widget_set_rect(w, x, y, w->w, w->h); }
/** @brief 只改尺寸 */
void cg_widget_set_size(cg_widget_t *w, int ww, int hh) { if (w) cg_widget_set_rect(w, w->x, w->y, ww, hh); }

/** @brief 显示 / 隐藏控件 */
void cg_widget_set_visible(cg_widget_t *w, int visible)
{
    if (!w) return;
    w->visible = visible != 0;
    if (w->node) w->node->visible = w->visible;
    sync(w);
}

/** @brief 启用 / 禁用控件（禁用后事件不再派发） */
void cg_widget_set_enabled(cg_widget_t *w, int enabled)
{
    if (!w) return;
    w->enabled = enabled != 0;
    if (w->node) w->node->enabled = w->enabled;
    sync(w);
}

/** @brief 设置勾选状态（checkbox / radio / switch） */
void cg_widget_set_checked(cg_widget_t *w, int on)
{
    if (!w) return;
    w->checked = on != 0;
    if (w->node) w->node->checked = w->checked;
    sync(w);
}

/** @brief 设置当前值，自动夹到 [vmin, vmax] */
void cg_widget_set_value(cg_widget_t *w, int value)
{
    if (!w) return;
    if (value < w->vmin) value = w->vmin;
    if (value > w->vmax) value = w->vmax;
    w->value = value;
    if (w->node) w->node->value = value;
    sync(w);
}

/** @brief 设置取值范围；vmax <= vmin 时自动抬到 vmin+1 */
void cg_widget_set_value_range(cg_widget_t *w, int vmin, int vmax)
{
    if (!w) return;
    w->vmin = vmin;
    w->vmax = (vmax <= vmin) ? vmin + 1 : vmax;
    if (w->node) { w->node->vmin = w->vmin; w->node->vmax = w->vmax; }
    sync(w);
}

/** @brief 设置前景 / 背景色（0xRRGGBB） */
void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg)
{
    if (!w) return;
    w->style.fg = fg;
    w->style.bg = bg;
    sync(w);
}

/** @brief 设置字体（family 为 NULL 表示回系统 UI 字体） */
void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold)
{
    if (!w) return;
    free(w->style.font);
    w->style.font = dup_str(family);
    w->style.size = size_pt;
    w->style.bold = bold;
    sync(w);
}

/** @brief 取挂在控件上的业务数据；NULL 返回 NULL */
void  *cg_widget_get_user(const cg_widget_t *w) { return w ? w->user : NULL; }
/** @brief 挂业务数据到控件（回调里的 user 参数就是它） */
void   cg_widget_set_user(cg_widget_t *w, void *user) { if (w) w->user = user; }

/* ------------------------------------------------------------ 列表类控件 */

/**
 * @brief 给列表类控件追加一个条目（listbox/combo/tabs/statusbar/toolbar）
 * @param w 目标控件；item 条目文字
 * @return int 新条目下标；失败返回 -1
 */
int cg_widget_list_add(cg_widget_t *w, const char *item)
{
    char **ni;

    if (!w || !item) return -1;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) return -1;
    w->items = ni;
    w->items[w->nitems] = dup_str(item);
    if (!w->items[w->nitems]) return -1;
    w->nitems++;
    w->items_dirty = 1;
    w->scroll_bottom = 1;
    sync(w);
    return w->nitems - 1;
}

/** @brief cg_widget_list_add 的 printf 版本 */
int cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...)
{
    char    stackbuf[512];
    char   *buf = stackbuf;
    int     n;
    va_list ap;
    int     rc;

    if (!w || !fmt) return -1;

    va_start(ap, fmt);
    n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;

    if ((size_t)n >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)n + 1);
        if (!buf) return -1;
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    rc = cg_widget_list_add(w, buf);
    if (buf != stackbuf) free(buf);
    return rc;
}

/** @brief 清空列表类控件的全部条目 */
void cg_widget_list_clear(cg_widget_t *w)
{
    if (!w) return;
    for (int i = 0; i < w->nitems; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->nitems = 0;
    w->items_dirty = 1;
    sync(w);
}

/** @brief 列表条目数；NULL 返回 0 */
int cg_widget_list_count(const cg_widget_t *w) { return w ? w->nitems : 0; }

/** @brief 取第 index 个条目文字；越界返回 NULL */
const char *cg_widget_list_item(const cg_widget_t *w, int index)
{
    if (!w || index < 0 || index >= w->nitems) return NULL;
    return w->items[index];
}

/** @brief 让列表滚到最后一项（日志类控件常用） */
/** @brief 让列表滚到最后一项（日志类控件常用） */
void cg_widget_list_scroll_bottom(cg_widget_t *w)
{
    if (!w) return;
    w->scroll_bottom = 1;
    sync(w);
}

/* ------------------------------------------------------------ 树 */

/** 类型检查：这些 API 只对特定控件有意义，用错了要明确报出来而不是静默无效 */
static int want_type(const cg_widget_t *w, const char *type, const char *who)
{
    if (w && w->type && strcmp(w->type, type) == 0) return 1;
    fprintf(stderr, "[cstackgui] %s: 需要 %s 控件，实际给的是 %s\n",
            who, type, (w && w->type) ? w->type : "(null)");
    return 0;
}

/** @brief 加一个树节点 */
/**
 * @brief 给树加一个节点（运行时追加，父下标必须已存在）
 * @param w tree 控件；parent 父节点下标，-1 加在根上；text 节点文字
 * @return int 节点下标；失败返回 -1
 */
int cg_tree_add(cg_widget_t *w, int parent, const char *text)
{
    if (!w || !want_type(w, "tree", "cg_tree_add")) return -1;
    if (parent >= w->ntree) {
        fprintf(stderr, "[cstackgui] cg_tree_add: 父节点下标 %d 不存在\n", parent);
        return -1;
    }
    {
        int idx = tree_push(w, parent, text, 1);
        if (idx < 0) return -1;
        w->items_dirty = 1;
        sync(w);
        return idx;
    }
}

/** @brief cg_tree_add 的 printf 版本 */
int cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...)
{
    char    stackbuf[512];
    char   *buf = stackbuf;
    va_list ap;
    int     n, rc;

    if (!w || !fmt) return -1;
    va_start(ap, fmt);
    n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)n + 1);
        if (!buf) return -1;
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    rc = cg_tree_add(w, parent, buf);
    if (buf != stackbuf) free(buf);
    return rc;
}

/** @brief 清空树的全部节点（整树重建实现，选中态复位） */
void cg_tree_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "tree", "cg_tree_clear")) return;
    for (int i = 0; i < w->ntree; i++) free(w->tree[i].text);
    free(w->tree);
    w->tree = NULL;
    w->ntree = 0;
    w->tree_sel = -1;
    w->items_dirty = 1;
    sync(w);
}

/** @brief 树节点总数；非 tree 控件返回 0 */
int cg_tree_count(const cg_widget_t *w)
{
    return (w && w->type && strcmp(w->type, "tree") == 0) ? w->ntree : 0;
}

/** @brief 取第 item 个节点的文字；越界返回 NULL */
const char *cg_tree_item_text(const cg_widget_t *w, int item)
{
    if (!w || item < 0 || item >= w->ntree) return NULL;
    return w->tree[item].text;
}

/** @brief 当前选中的节点下标，没选中返回 -1 */
/** @brief 当前选中节点下标；没选中返回 -1 */
int cg_tree_selected(const cg_widget_t *w)
{
    return w ? w->tree_sel : -1;
}

/** @brief 当前选中节点的文字；没选中返回 NULL */
const char *cg_tree_selected_text(const cg_widget_t *w)
{
    if (!w || w->tree_sel < 0 || w->tree_sel >= w->ntree) return NULL;
    return w->tree[w->tree_sel].text;
}

/** @brief 展开/折叠某个节点（整树重建实现，够用且简单） */
void cg_tree_expand(cg_widget_t *w, int item, int expand)
{
    if (!w || item < 0 || item >= w->ntree) return;
    w->tree[item].expanded = expand != 0;
    w->items_dirty = 1;     /* v1 用整树重建来实现展开/折叠，够用且简单 */
    sync(w);
}

/* ------------------------------------------------------------ 表格 */

/** @brief 表格行数变多时把行级附加数组（勾选/数值/图标）一起补长到 want */
void cg_table_rows_grow(cg_widget_t *w, int want)
{
    if (want > w->row_cap) {
        unsigned char *nc = (unsigned char *)realloc(w->row_checked, (size_t)want);
        int *nv = (int *)realloc(w->row_val, (size_t)want * sizeof(int));
        if (nc) {
            memset(nc + w->row_cap, 0, (size_t)(want - w->row_cap));
            w->row_checked = nc;
        }
        if (nv) {
            memset(nv + w->row_cap, 0, (size_t)(want - w->row_cap) * sizeof(int));
            w->row_val = nv;
        }
        w->row_cap = want;
    }
    if (want > w->row_icon_n) {
        char **ni = (char **)realloc(w->row_icon, (size_t)want * sizeof(char *));
        if (ni) {
            memset(ni + w->row_icon_n, 0, (size_t)(want - w->row_icon_n) * sizeof(char *));
            w->row_icon = ni;
            w->row_icon_n = want;
        }
    }
}

/** @brief 加一列 */
/**
 * @brief 给表格加一列
 * @param w table 控件；title 列标题；width 列宽（0 = 按控件宽度均分）
 * @return int 列号；失败返回 -1
 */
int cg_table_add_column(cg_widget_t *w, const char *title, int width)
{
    cg_col_t *nc;

    if (!w || !want_type(w, "table", "cg_table_add_column")) return -1;
    nc = (cg_col_t *)realloc(w->cols, ((size_t)w->ncols + 1) * sizeof(cg_col_t));
    if (!nc) return -1;
    w->cols = nc;
    w->cols[w->ncols].title = dup_str(title ? title : "");
    w->cols[w->ncols].width = width;
    w->ncols++;
    w->items_dirty = 1;
    sync(w);
    return w->ncols - 1;
}

/** @brief 加一行 */
/**
 * @brief 给表格加一行（不足的列按空串处理）
 * @param w table 控件；cells 列数个字符串；ncells 个数
 * @return int 行号；失败返回 -1
 */
int cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells)
{
    char ***nr;

    if (!w || !want_type(w, "table", "cg_table_add_row")) return -1;
    if (w->ncols == 0) {
        fprintf(stderr, "[cstackgui] cg_table_add_row: 还没加列，先 cg_table_add_column\n");
        return -1;
    }
    nr = (char ***)realloc(w->rows, ((size_t)w->nrows + 1) * sizeof(char **));
    if (!nr) return -1;
    w->rows = nr;
    {
        int r = w->nrows;
        w->rows[r] = (char **)calloc((size_t)w->ncols, sizeof(char *));
        if (!w->rows[r]) return -1;
        for (int c = 0; c < w->ncols; c++) {
            const char *cell = (cells && c < ncells && cells[c]) ? cells[c] : "";
            w->rows[r][c] = dup_str(cell);
        }
    }
    cg_table_rows_grow(w, w->nrows + 1);
    w->nrows++;
    w->row_cells = w->ncols;
    w->items_dirty = 1;
    sync(w);
    return w->nrows - 1;
}

/** @brief 清空表格全部行（列保留） */
void cg_table_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "table", "cg_table_clear")) return;
    if (w->rows) {
        for (int r = 0; r < w->nrows; r++) {
            if (!w->rows[r]) continue;
            for (int c = 0; c < w->row_cells; c++) free(w->rows[r][c]);
            free(w->rows[r]);
        }
        free(w->rows);
    }
    w->rows = NULL;
    w->nrows = 0;
    w->table_sel = -1;
    w->items_dirty = 1;
    sync(w);
}

/** @brief 表格行数；非 table 控件返回 0 */
int cg_table_row_count(const cg_widget_t *w)
{
    return (w && w->type && strcmp(w->type, "table") == 0) ? w->nrows : 0;
}

/** @brief 取某行某列的文字；越界返回 NULL */
const char *cg_table_cell(const cg_widget_t *w, int row, int col)
{
    if (!w || row < 0 || row >= w->nrows || col < 0 || col >= w->row_cells) return NULL;
    return w->rows[row][col];
}

/** @brief 当前选中行，没选中返回 -1 */
/** @brief 当前选中行（最后一次点击的行）；没选中返回 -1 */
int cg_table_selected(const cg_widget_t *w)
{
    return w ? w->table_sel : -1;
}

/* ------------------------------------------------------------ 状态栏 */

/**
 * @brief 改状态栏第 part 段的文字（段不够且正好差一段时相当于追加）
 * @param w statusbar 控件；part 段号（0 起）；text 文字
 * @return void 无返回值
 */
void cg_status_set(cg_widget_t *w, int part, const char *text)
{
    if (!w || !want_type(w, "statusbar", "cg_status_set")) return;
    if (part < 0) return;
    while (w->nitems <= part) {          /* 段不够就补空段 */
        if (cg_widget_list_add(w, "") < 0) return;
    }
    free(w->items[part]);
    w->items[part] = dup_str(text ? text : "");
    w->items_dirty = 1;
    sync(w);
}

/** @brief cg_status_set 的 printf 版本 */
void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...)
{
    char    stackbuf[512];
    char   *buf = stackbuf;
    va_list ap;
    int     n;

    if (!w || !fmt) return;
    va_start(ap, fmt);
    n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)n + 1);
        if (!buf) return;
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    cg_status_set(w, part, buf);
    if (buf != stackbuf) free(buf);
}

/* ------------------------------------------------------------ 标签页 */

/** @brief 标签页页数；非 tabs 控件返回 0 */
int cg_tabs_count(const cg_widget_t *w)
{
    return (w && w->type && strcmp(w->type, "tabs") == 0) ? w->nitems : 0;
}

/** @brief 切到第 index 页（子控件按 page 自动显隐）；越界忽略 */
void cg_tabs_select(cg_widget_t *w, int index)
{
    if (!w || !want_type(w, "tabs", "cg_tabs_select")) return;
    if (index < 0 || index >= w->nitems) return;
    w->value = index;
    if (w->node) w->node->value = index;
    w->items_dirty = 1;
    sync(w);
}

/** @brief 当前页下标（0 起） */
int cg_tabs_selected(const cg_widget_t *w)
{
    return w ? w->value : 0;
}

/* ------------------------------------------------------------ 图片 */

/** @brief 换图片文件（png/jpg/bmp/gif）；路径不存在时显示占位框 */
void cg_widget_set_image(cg_widget_t *w, const char *path)
{
    if (!w) return;
    free(w->src);
    w->src = (path && *path) ? dup_str(path) : NULL;
    w->image_failed = 0;     /* 换成新路径，给后端一次重新加载的机会 */
    sync(w);                 /* 后端的 sync 发现路径变了会先释放旧位图 */
}

/** @brief 当前图片路径；未设置返回 NULL */
const char *cg_widget_image(const cg_widget_t *w)
{
    return w ? w->src : NULL;
}

/* ------------------------------------------------------------ 菜单 */

/** @brief 最近点中的菜单项 id（.cgui 里写的）；没写 id 返回 NULL */
const char *cg_menu_last_id(const cg_widget_t *w)
{
    return w ? w->menu_last_id : NULL;
}

/** @brief 最近点中的菜单项文字 */
const char *cg_menu_last_text(const cg_widget_t *w)
{
    return w ? w->menu_last_text : NULL;
}

/** @brief 菜单里有多少个可点的项（不含分隔线和顶级弹出项），调试用 */
/** @brief 菜单里可点的项数（不含分隔线和下拉项），调试用 */
int cg_menu_count(const cg_widget_t *w)
{
    int n = 0;
    if (!w) return 0;
    for (int i = 0; i < w->nmenu; i++) {
        if (w->menu[i].cmd != 0) n++;
    }
    return n;
}

/** @brief 改窗口标题（同步后端与文档） */
void cg_set_title(cg_app_t *app, const char *title)
{
    if (!app || !title) return;
    if (cg_backend_active && cg_backend_active->set_title) {
        cg_backend_active->set_title(app, title);
    }
    if (app->window) cg_widget_set_text(app->window, title);
}

/** @brief 请求退出消息循环（进程随后结束，退出码 0） */
void cg_quit(cg_app_t *app) { if (app) app->quit = 1; }

/** @brief 当前有没有真正的窗口（--selftest 不开窗）：回调里想弹对话框之前先问一句， 免得自检/CI 被弹窗卡住 */
/** @brief 当前有没有真正的窗口（--selftest 不开窗）；弹对话框前先问一句 */
int cg_app_has_window(const cg_app_t *app)
{
    return (app && app->hwnd) ? 1 : 0;
}

/** @brief 在 close 回调里调用：这次不关了（"还有没保存的内容"那种） */
void cg_cancel_close(cg_app_t *app) { if (app) app->close_cancel = 1; }

/** @brief 把 .cgui 文档树打印到 stdout（调试/自检用） */
/** @brief 把 .cgui 文档树打印到 stdout（调试/自检用） */
void cg_dump_doc(cg_app_t *app)
{
    if (!app) return;
    cg_doc_dump(&app->doc, stdout);
}

/* ------------------------------------------------------------ 悬停提示 / 时钟 */

/** @brief 改控件悬停提示（空串清除），立即生效 */
void cg_widget_set_hint(cg_widget_t *w, const char *text)
{
    if (!w) return;
    free(w->hint);
    w->hint = (text && *text) ? dup_str(text) : NULL;
    if (w->node) {
        free(w->node->hint);
        w->node->hint = w->hint ? dup_str(w->hint) : NULL;
    }
    cg_win_set_hint(w);
}

/** @brief 读控件悬停提示；NULL 控件返回 NULL，未设置返回空串 */
const char *cg_widget_hint(const cg_widget_t *w)
{
    return w ? (w->hint ? w->hint : "") : NULL;
}

/** @brief 时钟（timer 控件）的触发周期，毫秒 */
/** @brief 改时钟（timer）触发周期：0 停止，>0 立即按新周期重新计时 */
void cg_widget_set_period(cg_widget_t *w, int period_ms)
{
    if (!w) return;
    if (period_ms < 0) period_ms = 0;
    w->period = period_ms;
    if (w->node) w->node->period = period_ms;
    cg_win_restart_timer(w);
}

/** @brief 读时钟当前周期（毫秒，0 = 停） */
int cg_widget_period(const cg_widget_t *w)
{
    return w ? w->period : 0;
}

/* ------------------------------------------------------------ 动态追加控件 */

/**
 * @brief 运行时用代码追加控件（不进 .cgui），新控件的 id 也参与事件名查找
 * @param app 所属应用；parent 父容器（NULL 挂窗口）；type 类型名；id 唯一标识
 * @param x,y 位置；aw,ah 尺寸（0 用类型默认）；text 初始文字
 * @return cg_widget_t* 新控件；类型未知/id 冲突/父不是容器返回 NULL
 */
cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent,
                           const char *type, const char *id,
                           int x, int y, int aw, int ah, const char *text)
{
    cg_widget_t *nw;
    const cg_kind_info_t *ki;
    const cg_kind_info_t *pk;

    if (!app) return NULL;
    ki = cg_kind_lookup(type);
    if (!ki) {
        fprintf(stderr, "[cstackgui] cg_widget_add: 未知控件类型 \"%s\"\n", type ? type : "(null)");
        return NULL;
    }
    if (id && cg_widget_by_id(app, id)) {
        fprintf(stderr, "[cstackgui] cg_widget_add: id \"%s\" 已存在\n", id);
        return NULL;
    }
    if (!parent) parent = app->window;
    if (parent && !parent->is_window) {
        pk = cg_kind_lookup(parent->type);
        if (!pk || !pk->container) {
            fprintf(stderr, "[cstackgui] cg_widget_add: \"%s\" (%s) 不是容器\n",
                    parent->id ? parent->id : "-", parent->type ? parent->type : "-");
            return NULL;
        }
    }

    if (aw <= 0) aw = ki->def_w;
    if (ah <= 0) ah = ki->def_h;

    nw = cg_widget_new(app, ki->name, id, x, y, aw, ah,
                       text ? text : ki->def_text);
    if (!nw) return NULL;
    if (parent) widget_add_child(parent, nw);

    if (cg_backend_active && cg_backend_active->attach) cg_backend_active->attach(nw);
    sync(nw);
    return nw;
}

/* ------------------------------------------------------------ 启动 */

/* ------------------------------------------------------------ 找界面文件
 * 双击 exe 时没有命令行参数，当前目录也不一定是项目目录，
 * 所以 auto_locate_cgui 打开时会按几个约定位置去找：
 *   1) exe 旁边           build\hello.cgui
 *   2) 标准布局           build\..\apps\hello\hello.cgui
 *   3) 标准布局但名字不同  build\..\apps\hello\<原文件名>
 */

/** @brief 取路径的文件名部分（兼容 \\ 和 /） */
static const char *path_base(const char *path)
{
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s1) s1 = s2;
    return s1 ? s1 + 1 : path;
}

/** @brief 取路径的目录部分（无目录时写 "."） */
static void path_dir(const char *path, char *out, size_t size)
{
    const char *b = path_base(path);
    size_t n = (size_t)(b - path);

    if (n == 0) {
        snprintf(out, size, ".");
        return;
    }
    if (n > size - 1) n = size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/**
 * @brief 按约定位置找界面文件并读入（内嵌文本 > 显式路径 > exe 旁 > apps/examples）
 * @param desc 启动参数；found 输出实际用到的路径；found_size 容量
 * @return char* JSON 原文（调用方 free）；找不到返回 NULL 并打印试过的位置
 */
static char *open_cgui_text(const cg_app_desc_t *desc, char *found, size_t found_size)
{
    char  exe[512], dir[512], stem[512], cand[2048];
    char *json;
    const char *base;
    const char *exe_name;
    char *dot;

    if (found && found_size) found[0] = '\0';
    if (desc->cgui_text) return dup_str(desc->cgui_text);

    if (desc->cgui_path) {
        json = cg_read_file(desc->cgui_path);
        if (json) {
            if (found) snprintf(found, found_size, "%s", desc->cgui_path);
            return json;
        }
        if (!desc->auto_locate_cgui) {
            fprintf(stderr, "[cstackgui] 读不到界面文件: %s\n", desc->cgui_path);
            return NULL;
        }
        fprintf(stderr, "[cstackgui] 读不到 %s，在 exe 附近找找看\n", desc->cgui_path);
    } else if (!desc->auto_locate_cgui) {
        fprintf(stderr, "[cstackgui] 没有提供 cgui_path 或 cgui_text\n");
        return NULL;
    }

    if (cg_exe_path(exe, sizeof(exe)) != 0) return NULL;
    path_dir(exe, dir, sizeof(dir));

    exe_name = path_base(exe);
    snprintf(stem, sizeof(stem), "%s", exe_name);
    dot = strrchr(stem, '.');
    if (dot) *dot = '\0';

    base = desc->cgui_path ? path_base(desc->cgui_path) : "";

    if (base[0]) {
        snprintf(cand, sizeof(cand), "%s\\%s", dir, base);
        json = cg_read_file(cand);
        if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }
    }

    snprintf(cand, sizeof(cand), "%s\\..\\apps\\%s\\%s.cgui", dir, stem, stem);
    json = cg_read_file(cand);
    if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }

    snprintf(cand, sizeof(cand), "%s\\..\\examples\\%s\\%s.cgui", dir, stem, stem);
    json = cg_read_file(cand);
    if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }

    if (base[0] && strcmp(base, stem) != 0) {
        snprintf(cand, sizeof(cand), "%s\\..\\apps\\%s\\%s", dir, stem, base);
        json = cg_read_file(cand);
        if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }
    }

    fprintf(stderr, "[cstackgui] 找不到界面文件，试过这些位置：\n");
    if (desc->cgui_path) fprintf(stderr, "    %s\n", desc->cgui_path);
    if (base[0]) fprintf(stderr, "    %s\\%s\n", dir, base);
    fprintf(stderr, "    %s\\..\\apps\\%s\\%s.cgui\n", dir, stem, stem);
    fprintf(stderr, "  用法: %s <界面文件.cgui>\n", exe_name);
    return NULL;
}

/**
 * @brief 打开应用：读界面文件、解析、构建控件树（不开窗）
 * @param app 应用结构（先清零）；desc 启动参数；allow_ui 预留
 * @return int 0 成功；2 找不到界面文件；3 解析失败
 */
static int app_open(cg_app_t *app, const cg_app_desc_t *desc, int allow_ui)
{
    char  err[256] = { 0 };
    char  resolved[2048] = { 0 };
    char *json = NULL;

    memset(app, 0, sizeof(*app));
    app->scale = 1;
    app->bindings = desc ? desc->bindings : NULL;
    app->dblclick_suppress_click = desc ? desc->dblclick_suppress_click : 0;

    json = desc ? open_cgui_text(desc, resolved, sizeof(resolved)) : NULL;
    if (!json) return 2;

    /* 记住界面文件在哪：image 的相对路径按它所在目录解释，符合直觉 */
    if (resolved[0]) {
        const char *slash = strrchr(resolved, '\\');
        const char *slash2 = strrchr(resolved, '/');
        if (slash2 > slash) slash = slash2;
        app->cgui_path = dup_str(resolved);
        if (slash) {
            size_t n = (size_t)(slash - resolved);
            app->cgui_dir = (char *)malloc(n + 1);
            if (app->cgui_dir) {
                memcpy(app->cgui_dir, resolved, n);
                app->cgui_dir[n] = '\0';
            }
        }
    }

    {
        cg_doc_t *doc = cg_doc_parse(json, err, sizeof(err));
        free(json);
        if (!doc) {
            fprintf(stderr, "[cstackgui] 解析 .cgui 失败: %s\n", err);
            return 3;
        }
        app->doc = *doc;
        free(doc);   /* doc 的所有权已转移 */
    }

    if (desc && desc->title_override) {
        free(app->doc.title);
        app->doc.title = dup_str(desc->title_override);
    }

    cg_build_widgets(app);
    app->start_tick = cg_ticks();

    if (desc && desc->trace_doc) cg_doc_dump(&app->doc, stdout);
    (void)allow_ui;
    return 0;
}

/** @brief 关闭应用：清定时器、关后端、断开文档、释放全部控件 */
static void app_close(cg_app_t *app)
{
    cg_timers_clear(app);
    free(app->cgui_path);
    free(app->cgui_dir);
    app->cgui_path = NULL;
    app->cgui_dir = NULL;
    if (cg_backend_active && cg_backend_active->shutdown) cg_backend_active->shutdown(app);
    if (app->doc.root) {      /* 运行时控件会回写节点，先断开再放文档 */
        for (cg_widget_t *w = app->widgets; w; w = w->next) w->node = NULL;
        cg_doc_release(&app->doc);   /* 注意：app->doc 是内嵌结构，不能 free 它 */
    }
    cg_widget_t *w = app->widgets;
    while (w) {
        cg_widget_t *next = w->next;
        cg_widget_free(w);
        w = next;
    }
    app->widgets = NULL;
}

/** @brief 建窗口 + 消息循环，返回进程退出码 */
/**
 * @brief 建窗口 + 跑消息循环，结束后释放全部资源
 * @param desc 启动参数
 * @return int 进程退出码（0 正常，2/3/4 见各失败点）
 */
int cg_app_run(const cg_app_desc_t *desc)
{
    cg_app_t app;
    int rc = app_open(&app, desc, 1);

    if (rc != 0) return rc;

    cg_backend_active = &cg_backend_win32;
    if (cg_backend_active->init(&app) != 0) {
        fprintf(stderr, "[cstackgui] 创建窗口失败\n");
        app_close(&app);
        return 4;
    }
    /* 建好原生控件后再同步一次初始状态 */
    for (cg_widget_t *w = app.widgets; w; w = w->next) {
        if (w->native && cg_backend_active->sync) cg_backend_active->sync(w);
    }

    if (desc && desc->on_ready) desc->on_ready(&app, desc->ready_user);

    rc = cg_backend_active->run(&app);
    app_close(&app);
    cg_backend_active = NULL;
    return rc;
}

/* ------------------------------------------------------------ 自检 */

typedef struct {
    int total;
    int fired;
    int unresolved;
} selftest_stat_t;

/** @brief 递归走控件树，对每个绑了回调的事件做一次合成触发并统计 */
static void selftest_walk(cg_app_t *app, cg_widget_t *w, selftest_stat_t *st, int depth)
{
    (void)app;
    (void)depth;

    if (w->is_window) {
        for (size_t i = 0; i < w->nchildren; i++) selftest_walk(app, w->children[i], st, depth);
        return;
    }

    st->total++;
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (i == CG_EVENT_CLOSE) continue;      /* 关窗事件不适合合成 */
        if (!w->node || !w->node->ev[i]) continue;
        if (!cg_kind_supports_event(w->type, (cg_event_t)i)) continue;  /* 不支持的事件上面已报过 */
        if (!w->handlers[i]) {
            fprintf(stderr, "  [FAIL] %-10s #%-14s %-9s 绑定 \"%s\" 未在 C 侧登记\n",
                    w->type ? w->type : "-", w->id ? w->id : "-",
                    cg_event_name((cg_event_t)i), w->node->ev[i]);
            st->unresolved++;
            continue;
        }
        printf("  [FIRE] %-10s #%-14s %-9s -> %s\n",
               w->type ? w->type : "-", w->id ? w->id : "-",
               cg_event_name((cg_event_t)i), w->node->ev[i]);
        w->handlers[i](w, (cg_event_t)i, w->user);
        st->fired++;
    }
    for (size_t i = 0; i < w->nchildren; i++) selftest_walk(app, w->children[i], st, depth);
}

/** @brief 不开窗的自检：解析 .cgui、打印控件树、对每个绑定了回调的控件合成一次事件 */
/**
 * @brief 不开窗的自检：解析 .cgui、打印控件树、合成触发全部回调
 * @param desc 启动参数（同 cg_app_run）
 * @return int 0 全部通过；5 有未登记/不支持的事件；2/3 同 app_open
 */
int cg_app_selftest(const cg_app_desc_t *desc)
{
    cg_app_t app;
    selftest_stat_t st = { 0, 0, 0 };
    int rc;

    /* 树/事件名输出里有中文（UTF-8 字节），控制台默认代码页会显示成乱码 */
    cg_win_console_utf8();

    /* app_open 里会调 cg_ticks()，后端在自检模式下不参与，这里给个空后端 */
    rc = app_open(&app, desc, 0);
    if (rc != 0) return rc;

    printf("== CStackGUI selftest ==\n");
    printf("-- 解析出的控件树 --\n");
    cg_doc_dump(&app.doc, stdout);

    if (desc && desc->on_ready) {
        printf("-- on_ready --\n");
        desc->on_ready(&app, desc->ready_user);
    }

    printf("-- 合成事件 --\n");
    if (app.window) selftest_walk(&app, app.window, &st, 0);
    printf("-- 结果: 控件 %d 个, 触发回调 %d 次, 未登记 %d 个, 事件不支持 %d 个 --\n",
           st.total, st.fired, st.unresolved, app.unsupported_events);

    {
        int bad = st.unresolved + app.unsupported_events;
        app_close(&app);
        if (bad) return 5;
    }
    printf("selftest OK\n");
    return 0;
}
