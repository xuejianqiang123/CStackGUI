/**
 * @file    cg_widget.c
 * @brief   控件模型：运行时控件构造/析构、文档树构建、查找与公共属性 API
 * @details 从原 cg_app.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 控件生命周期 */

/** @brief 复制字符串到堆内存（调用方负责 free）；失败返回 NULL */
char *cg_str_dup(const char *s)
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
    w->type = cg_str_dup(type ? type : "label");
    w->id = cg_str_dup(id);
    w->text = cg_str_dup(text ? text : "");
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
    free(w->bind_src);
    free(w->chart_pts);      /* chart 的点缓冲（按需分配，可能为 NULL） */
    free(w->chart_fill);
    free(w->id);
    free(w->type);
    free(w->text);
    free(w->style.font);
    free(w->children);
    free(w);
}

/** @brief 给运行时控件挂子控件（尾插，child->parent 回填；分配失败静默丢弃） */
void cg_widget_attach_child(cg_widget_t *parent, cg_widget_t *child)
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
int cg_widget_tree_push(cg_widget_t *w, int parent, const char *text, int expanded)
{
    cg_tree_row_t *nt = (cg_tree_row_t *)realloc(w->tree, ((size_t)w->ntree + 1) * sizeof(cg_tree_row_t));
    int idx;

    if (!nt) return -1;
    w->tree = nt;
    idx = w->ntree++;
    w->tree[idx].text = cg_str_dup(text ? text : "");
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
        idx = cg_widget_tree_push(w, parent, items[i]->text, items[i]->expanded);
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
        w->menu[idx].text = cg_str_dup(it->text ? it->text : "");
        w->menu[idx].id = it->id ? cg_str_dup(it->id) : NULL;
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
    if (n->src) w->src = cg_str_dup(n->src);
    if (n->format) w->format = cg_str_dup(n->format);
    if (n->hint) w->hint = cg_str_dup(n->hint);
    w->period = n->period;
    if (n->bind_src) {                      /* 控件绑定：真值留着，运行期按 id 找源 */
        w->bind_src = cg_str_dup(n->bind_src);
        w->bind_two = n->bind_two;
        if (w->bind_src) app->nbinds++;
    }
    w->multi = n->multi;
    w->check = n->check;
    w->multiline = n->multiline;
    w->scroll = n->scroll;
    w->maxlen = n->maxlen;
    w->conv = n->conv;
    w->input = n->input;
    w->border = n->border;
    w->spin = n->spin;
    /* 数值框（edit + 调节器）的显示文本由 value 派生：.cgui 里不用写 text，
     * 这里补成 value 的十进制，cg_widget_text() 从第一帧起就和界面一致。 */
    if (cg_widget_is_numedit(w) && (!w->text || !*w->text)) {
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "%d", w->value);
        free(w->text);
        w->text = cg_str_dup(vbuf);
    }
    /* 菜单栏和菜单按钮共用同一套菜单行表；tree 虽然也用 nodes，但走的是下面那套摊平 */
    if (n->nodes && (!strcmp(n->type, "menu") || !strcmp(n->type, "menubutton"))) {
        menu_push_doc(app, w, n->nodes, n->nnodes, -1);
    }
    w->style = n->style;
    w->style.font = cg_str_dup(n->style.font);   /* 拷贝一份，文档释放后控件仍可用 */
    w->tree_sel = -1;
    w->table_sel = -1;

    /* 结构化内容：文档 -> 运行时（各存一份，文档释放后控件仍可用） */
    if (n->items && n->nitems) {
        w->items = (char **)calloc(n->nitems, sizeof(char *));
        if (w->items) {
            for (size_t i = 0; i < n->nitems; i++) w->items[i] = cg_str_dup(n->items[i]);
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
                w->cols[i].title = cg_str_dup(n->cols[i].title);
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
                    w->rows[r][c] = cg_str_dup(cell ? cell : "");
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
    cg_widget_attach_child(parent, w);

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

/** @brief 按原生窗口句柄反查控件（同时比对 native 与 native2，数值框有两个 HWND）；找不到返回 NULL */
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
/** @brief 是否数值输入框：`edit` + 调节器（spin），且不是多行。
 *  只有这种输入框的 `value` 才是真相（文本由 value 派生）—— 也就是原来独立类型
 *  spinner 的语义。bind 抄 value、cg_widget_set_value 写文本都按它分流。 */
int cg_widget_is_numedit(const cg_widget_t *w)
{
    if (!w || !w->type || strcmp(w->type, "edit") != 0) return 0;
    return (w->spin != 0 && !w->multiline);
}

/** @brief 当前值（滑块位置 / 列表选中下标等）；NULL 返回 0 */
int cg_widget_value(const cg_widget_t *w)   { return w ? w->value : 0; }

/* ------------------------------------------------------------ 公共修改 */

/** @brief 改控件文字（同步到界面与文档节点） */
void cg_widget_set_text(cg_widget_t *w, const char *text)
{
    if (!w) return;
    free(w->text);
    w->text = cg_str_dup(text ? text : "");
    if (w->node) { free(w->node->text); w->node->text = cg_str_dup(w->text); }
    cg_widget_sync(w);
    cg_bind_propagate(w);
}

/** @brief 一次改位置和尺寸（逻辑像素，相对父容器） */
void cg_widget_set_rect(cg_widget_t *w, int x, int y, int ww, int hh)
{
    if (!w) return;
    w->x = x; w->y = y; w->w = ww; w->h = hh;
    if (w->node) { w->node->x = x; w->node->y = y; w->node->w = ww; w->node->h = hh; }
    cg_widget_sync(w);
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
    cg_widget_sync(w);
}

/** @brief 启用 / 禁用控件（禁用后事件不再派发） */
void cg_widget_set_enabled(cg_widget_t *w, int enabled)
{
    if (!w) return;
    w->enabled = enabled != 0;
    if (w->node) w->node->enabled = w->enabled;
    cg_widget_sync(w);
}

/** @brief 设置勾选状态（checkbox / radio / switch） */
void cg_widget_set_checked(cg_widget_t *w, int on)
{
    if (!w) return;
    w->checked = on != 0;
    if (w->node) w->node->checked = w->checked;
    cg_widget_sync(w);
    cg_bind_propagate(w);
}

/** @brief 设置当前值，自动夹到 [vmin, vmax] */
void cg_widget_set_value(cg_widget_t *w, int value)
{
    char vbuf[32];
    if (!w) return;
    if (value < w->vmin) value = w->vmin;
    if (value > w->vmax) value = w->vmax;
    w->value = value;
    if (w->node) w->node->value = value;
    /* 数值框的文本是 value 的派生物：一起更新，cg_widget_text() 才不会读到旧数字 */
    if (cg_widget_is_numedit(w)) {
        snprintf(vbuf, sizeof(vbuf), "%d", value);
        free(w->text);
        w->text = cg_str_dup(vbuf);
        if (w->node) {
            free(w->node->text);
            w->node->text = cg_str_dup(vbuf);
        }
    }
    cg_widget_sync(w);
    cg_bind_propagate(w);
}

/** @brief 设置取值范围；vmax <= vmin 时自动抬到 vmin+1 */
void cg_widget_set_value_range(cg_widget_t *w, int vmin, int vmax)
{
    if (!w) return;
    w->vmin = vmin;
    w->vmax = (vmax <= vmin) ? vmin + 1 : vmax;
    if (w->node) { w->node->vmin = w->vmin; w->node->vmax = w->vmax; }
    cg_widget_sync(w);
}

/** @brief 设置前景 / 背景色（0xRRGGBB） */
void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg)
{
    if (!w) return;
    w->style.fg = fg;
    w->style.bg = bg;
    cg_widget_sync(w);
}

/** @brief 设置字体（family 为 NULL 表示回系统 UI 字体） */
void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold)
{
    if (!w) return;
    free(w->style.font);
    w->style.font = cg_str_dup(family);
    w->style.size = size_pt;
    w->style.bold = bold;
    cg_widget_sync(w);
}
