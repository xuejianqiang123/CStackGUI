/**
 * @file    cg_doc.c
 * @brief   .cgui 解析与打印：JSON -> 控件树，控件类型表（k_kinds）也在这里
 */
#include "cg_doc.h"
#include "cg_json.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 类型表 */

/* 类型表。**事件按控件特性分配**，不是所有控件都有全套事件：
 *   - 能点的才有 click / dblclick
 *   - 能改内容的才有 change
 *   - 有"选择"概念的才有 select（列表 / 标签页 / 树 / 表格 / 日历）
 *   - 只有树有 expand / collapse，只有滑块有 release
 *   - 纯显示或只被程序写的（label / progress / statusbar / separator / 容器）一个事件都没有
 * 画布只列出这里声明的事件；.cgui 绑了不支持的事件时，运行时启动会警告、自检会报错。
 */
static const cg_kind_info_t k_kinds[] = {
    { "button",     CG_K_BUTTON,     0, 120,  40, "按钮", CG_EV_CLICK | CG_EV_DBLCLICK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "iconbutton", CG_K_ICONBUTTON, 0,  48,  48, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "label",      CG_K_LABEL,      0, 160,  24, "标签", 0 },
    { "link",       CG_K_LINK,       0, 160,  24, "链接", CG_EV_CLICK | CG_EV_DBLCLICK },
    { "switch",     CG_K_SWITCH,     0,  56,  32, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "edit",       CG_K_EDIT,       0, 200,  32, "",     CG_EV_CHANGE | CG_EV_SUBMIT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "multiedit",  CG_K_MULTIEDIT,  0, 240, 120, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "richtext",   CG_K_RICHTEXT,   0, 320, 160, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "spinner",    CG_K_SPINNER,    0, 100,  28, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "datepicker", CG_K_DATEPICKER, 0, 140,  28, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "monthcal",   CG_K_MONTHCAL,   0, 230, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK },
    { "checkbox",   CG_K_CHECKBOX,   0, 160,  28, "复选", CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "radio",      CG_K_RADIO,      0, 160,  28, "单选", CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "listbox",    CG_K_LISTBOX,    0, 220, 140, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "combo",      CG_K_COMBO,      0, 200, 200, "",     CG_EV_SELECT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "slider",     CG_K_SLIDER,     0, 200,  32, "",     CG_EV_CHANGE | CG_EV_RELEASE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "progress",   CG_K_PROGRESS,   0, 200,  20, "",     0 },
    { "progressring", CG_K_RING,     0, 120, 120, "",     0 },
    { "tabs",       CG_K_TABS,       1, 320, 220, "",     CG_EV_SELECT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "tree",       CG_K_TREE,       0, 220, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_EXPAND | CG_EV_COLLAPSE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "table",      CG_K_TABLE,      0, 360, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_CHECK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "statusbar",  CG_K_STATUS,     0, 400,  24, "",     0 },
    { "menu",       CG_K_MENU,       0,   0,   0, "",     CG_EV_CLICK },
    { "menubutton", CG_K_MENUBUTTON, 0, 130,  32, "菜单", CG_EV_CLICK },
    { "toolbar",    CG_K_TOOLBAR,    0, 400,  32, "",     CG_EV_CLICK },
    { "group",      CG_K_GROUP,      1, 260, 180, "分组", 0 },
    { "box",        CG_K_BOX,        1, 200, 120, "",     0 },
    { "separator",  CG_K_SEPARATOR,  0, 200,   2, "",     0 },
    { "image",      CG_K_IMAGE,      0, 200, 120, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "timer",      CG_K_TIMER,      0,   0,   0, "",     CG_EV_TICK },
    { "canvas",     CG_K_CANVAS,     0, 240, 160, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "colorpicker", CG_K_COLORPICK, 0,  64,  28, "",     CG_EV_CHANGE },
    { "browser",    CG_K_BROWSER,    0, 320, 240, "",     0 },
    { "badge",      CG_K_BADGE,      0,  48,  24, "99+",  0 },
    { "tag",        CG_K_TAG,        0,  72,  28, "标签", CG_EV_CLICK },
    { "rate",       CG_K_RATE,       0, 140,  32, "",     CG_EV_CHANGE },
    { "pagination", CG_K_PAGINATION, 0, 180,  32, "",     CG_EV_CHANGE },
    { "card",       CG_K_CARD,       1, 280, 180, "卡片", 0 },
    { "shape",      CG_K_SHAPE,      0, 120, 120, "",     0 },
    /* 伪控件：窗口自身。事件表与「启动窗口」的事件表对齐（见 cg_event_t 注释）。 */
    { "window",     CG_K_WINDOW,     1,   0,   0, "",
      CG_EV_CLOSE | CG_EV_READY | CG_EV_DESTROY | CG_EV_MOVE | CG_EV_SIZE |
      CG_EV_ACTIVATE | CG_EV_DEACTIVATE | CG_EV_FIRSTACTIVATE | CG_EV_IDLE |
      CG_EV_TRAY | CG_EV_SHOW | CG_EV_HIDE | CG_EV_CLOSEQUERY |
      CG_EV_MOUSEDOWN | CG_EV_MOUSEUP | CG_EV_DBLCLICK | CG_EV_RDOWN | CG_EV_RUP |
      CG_EV_MOUSEMOVE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS |
      CG_EV_KEYDOWN | CG_EV_KEYUP | CG_EV_CHAR | CG_EV_WHEEL },
};

#define K_KIND_COUNT (sizeof(k_kinds) / sizeof(k_kinds[0]))

/** @brief 某个控件类型支不支持这个事件 */
/**
 * @brief 某个控件类型支不支持这个事件
 * @param type 控件类型名（"button"/"edit"...）
 * @param ev 事件枚举
 * @return int 1 = 支持，0 = 不支持或参数不对
 */
int cg_kind_supports_event(const char *type, cg_event_t ev)
{
    const cg_kind_info_t *ki = cg_kind_lookup(type);
    if (!ki || (int)ev < 0 || ev >= CG_EVENT_COUNT) return 0;
    return (ki->events & CG_EV_BIT(ev)) != 0;
}

/** @brief 某个控件类型支持的事件名列表，写成 "click/dblclick" 便于报错时提示 */
/**
 * @brief 列出某控件类型支持的事件名（"click/dblclick" 形式），报错时提示用
 * @param kind 控件类型枚举
 * @return const char* 事件名列表（内部静态缓冲）；类型未知返回"(该控件没有事件)"
 */
const char *cg_kind_event_list(cg_kind_t kind)
{
    static char buf[192];
    const cg_kind_info_t *ki = cg_kind_info(kind);
    size_t used;

    buf[0] = '\0';
    if (!ki) return buf;
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (!(ki->events & CG_EV_BIT(i))) continue;
        used = strlen(buf);
        if (used && used + 1 < sizeof(buf)) strncat(buf, "/", sizeof(buf) - used - 1);
        used = strlen(buf);
        if (used < sizeof(buf) - 1) strncat(buf, cg_event_name((cg_event_t)i), sizeof(buf) - used - 1);
    }
    if (!buf[0]) snprintf(buf, sizeof(buf), "(该控件没有事件)");
    return buf;
}

/**
 * @brief 按类型名查类型表
 * @param type .cgui 里的 type 字段写法
 * @return const cg_kind_info_t* 表项；没找到返回 NULL
 */
const cg_kind_info_t *cg_kind_lookup(const char *type)
{
    if (!type) return NULL;
    for (size_t i = 0; i < K_KIND_COUNT; i++) {
        if (strcmp(k_kinds[i].name, type) == 0) return &k_kinds[i];
    }
    return NULL;
}

/* 按 kind 字段找，**不要**用数组下标当枚举值：
 * 表是按控件逻辑分组排的，和枚举声明顺序故意解耦，
 * 省得以后插一个控件就把所有下标全带歪（事件名表就踩过这个坑）。 */
/**
 * @brief 按枚举查类型表（按 kind 字段线性查找，别用数组下标）
 * @param kind 控件类型枚举
 * @return const cg_kind_info_t* 表项；没找到返回 NULL
 */
const cg_kind_info_t *cg_kind_info(cg_kind_t kind)
{
    for (size_t i = 0; i < K_KIND_COUNT; i++) {
        if (k_kinds[i].kind == kind) return &k_kinds[i];
    }
    return NULL;
}

/**
 * @brief 枚举转类型名
 * @param kind 控件类型枚举
 * @return const char* 类型名；未知返回 "unknown"
 */
const char *cg_kind_name(cg_kind_t kind)
{
    const cg_kind_info_t *k = cg_kind_info(kind);
    return k ? k->name : "unknown";
}

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 复制字符串到堆内存（调用方负责 free）
 * @param s 源字符串
 * @return char* 堆副本；失败返回 NULL
 */
static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/**
 * @brief 同 dup_str，但 NULL / 空串一律返回 NULL（可选字段的惯例）
 * @param s 源字符串
 * @return char* 堆副本；空输入返回 NULL
 */
static char *dup_or_null(const char *s)
{
    if (!s || !*s) return NULL;
    return dup_str(s);
}

/** @brief 读文件到堆内存（UTF-8 原文），失败返回 NULL */
/**
 * @brief 读一个文件到堆内存
 * @param path 文件路径
 * @return char* UTF-8 原文，失败返回 NULL（调用方负责 free）
 */
char *cg_read_file(const char *path)
{
    FILE *f;
    long  len;
    char *buf;

    if (!path) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------ 样式 */

static const char *const k_align_names[] = { "left", "center", "right" };

/**
 * @brief 解析 style 对象（font/size/bold/fg/bg/radius/align），缺省给默认值
 * @param o JSON 里的 style 对象，可为 NULL
 * @param st 输出：样式（调用方先清零）
 * @return void 无返回值
 */
static void parse_style(const cj_val_t *o, cg_style_t *st)
{
    const cj_val_t *v;

    st->font = NULL;
    st->size = 0;              /* 0 = 用系统 UI 字体 */
    st->bold = 0;
    st->fg = CG_COLOR_NONE;
    st->bg = CG_COLOR_NONE;
    st->radius = 0;
    st->align = CG_ALIGN_LEFT;

    if (!o || o->type != CJ_OBJ) return;

    st->font = dup_or_null(cj_str(cj_get(o, "font"), NULL));
    st->size = cj_int(cj_get(o, "size"), 0);
    st->bold = cj_bool(cj_get(o, "bold"), 0);
    st->fg = cj_color(cj_get(o, "fg"), CG_COLOR_NONE);
    st->bg = cj_color(cj_get(o, "bg"), CG_COLOR_NONE);
    st->radius = cj_int(cj_get(o, "radius"), 0);

    v = cj_get(o, "align");
    if (v && v->type == CJ_STR) {
        for (int i = 0; i < 3; i++) {
            if (strcmp(v->str ? v->str : "", k_align_names[i]) == 0) {
                st->align = (cg_align_t)i;
                break;
            }
        }
    }
}

/**
 * @brief 释放 style 里的堆字段（font）
 * @param st 目标样式
 * @return void 无返回值
 */
static void style_free(cg_style_t *st)
{
    free(st->font);
    st->font = NULL;
}

/* --------------------------------------------------- 结构化内容的解析 */

/**
 * @brief 解析字符串数组（listbox/combo 等的 items）
 * @param arr JSON 数组
 * @param count 输出：元素个数
 * @return char** 字符串数组（调用方 free）；空/类型不对返回 NULL
 */
static char **parse_str_array(const cj_val_t *arr, size_t *count)
{
    char **out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (char **)calloc(n, sizeof(char *));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[i] = dup_str(cj_str(arr->items[i], ""));
    }
    *count = n;
    return out;
}

/**
 * @brief 释放字符串数组
 * @param arr 数组；n 元素个数
 * @return void 无返回值
 */
static void str_array_free(char **arr, size_t n)
{
    if (!arr) return;
    for (size_t i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

/** 树节点（可嵌套："nodes": [{ "text": "根", "expanded": true, "children": [...] }]）
 *  children 是"指针数组"，因为嵌套深度不定，逐层单独分配比连续数组好写。 */
static cg_item_t **parse_tree_items(const cj_val_t *arr, size_t *count)
{
    cg_item_t **out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (cg_item_t **)calloc(n, sizeof(cg_item_t *));
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *o = arr->items[i];
        cg_item_t *it = (cg_item_t *)calloc(1, sizeof(cg_item_t));
        if (!it) break;
        if (o && o->type == CJ_OBJ) {
            it->text = dup_str(cj_str(cj_get(o, "text"), ""));
            it->id = dup_or_null(cj_str(cj_get(o, "id"), NULL));
            it->expanded = cj_bool(cj_get(o, "expanded"), 0);
            it->checked = cj_bool(cj_get(o, "checked"), 0);
            it->children = parse_tree_items(cj_get(o, "children"), &it->nchildren);
        } else {
            it->text = dup_str(cj_str(o, ""));
        }
        out[i] = it;
    }
    *count = n;
    return out;
}

/**
 * @brief 递归释放树节点数组（含子节点）
 * @param items 节点指针数组；n 个数
 * @return void 无返回值
 */
static void tree_items_free(cg_item_t **items, size_t n)
{
    if (!items) return;
    for (size_t i = 0; i < n; i++) {
        if (!items[i]) continue;
        tree_items_free(items[i]->children, items[i]->nchildren);
        free(items[i]->text);
        free(items[i]->id);
        free(items[i]);
    }
    free(items);
}

/** 表格列："columns": [{ "title": "姓名", "w": 120 }] */
static cg_col_t *parse_columns(const cj_val_t *arr, size_t *count)
{
    cg_col_t *out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (cg_col_t *)calloc(n, sizeof(cg_col_t));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        const cj_val_t *o = arr->items[i];
        if (o && o->type == CJ_STR) {                 /* 允许 "columns": ["姓名","年龄"] */
            out[i].title = dup_str(cj_str(o, ""));
            out[i].width = 0;                         /* 0 = 均分 */
        } else {
            out[i].title = dup_str(cj_str(cj_get(o, "title"), ""));
            out[i].width = cj_int(cj_get(o, "w"), 0);
        }
    }
    *count = n;
    return out;
}

/** 表格行："rows": [["张三","18"], ["李四","20"]] */
static char ***parse_rows(const cj_val_t *arr, size_t *nrows, size_t *row_cells)
{
    char ***out;
    size_t  n, cells = 0;

    *nrows = 0;
    *row_cells = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (char ***)calloc(n, sizeof(char **));
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *row = arr->items[i];
        if (row && row->type == CJ_ARR && row->count > cells) cells = row->count;
    }
    if (cells == 0) cells = 1;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *row = arr->items[i];
        out[i] = (char **)calloc(cells, sizeof(char *));
        if (!out[i]) continue;
        for (size_t c = 0; c < cells; c++) {
            const cj_val_t *cell = (row && row->type == CJ_ARR && c < row->count) ? row->items[c] : NULL;
            out[i][c] = dup_str(cj_str(cell, ""));
        }
    }
    *nrows = n;
    *row_cells = cells;
    return out;
}

/**
 * @brief 释放表格行二维数组
 * @param rows 行数组；nrows 行数；cells 每行单元格数
 * @return void 无返回值
 */
static void rows_free(char ***rows, size_t nrows, size_t cells)
{
    if (!rows) return;
    for (size_t i = 0; i < nrows; i++) {
        if (!rows[i]) continue;
        for (size_t c = 0; c < cells; c++) free(rows[i][c]);
        free(rows[i]);
    }
    free(rows);
}

/* ------------------------------------------------------------ 节点 */

/**
 * @brief 递归释放文档节点（子节点、结构化内容、事件名、样式全清）
 * @param n 目标节点
 * @return void 无返回值
 */
static void node_free(cg_node_t *n)
{
    if (!n) return;
    for (size_t i = 0; i < n->nchildren; i++) node_free(n->children[i]);
    free(n->children);
    for (int i = 0; i < CG_EVENT_COUNT; i++) free(n->ev[i]);
    str_array_free(n->items, n->nitems);
    tree_items_free(n->nodes, n->nnodes);
    if (n->cols) {
        for (size_t i = 0; i < n->ncols; i++) free(n->cols[i].title);
        free(n->cols);
    }
    rows_free(n->rows, n->nrows, n->row_cells);
    style_free(&n->style);
    free(n->type);
    free(n->id);
    free(n->text);
    free(n->src);
    free(n->format);
    free(n->hint);
    free(n);
}

/**
 * @brief 给文档节点挂一个子节点（数组尾插，child->parent 回填）
 * @param parent 父节点
 * @param child 子节点
 * @return void 无返回值（分配失败静默丢弃）
 */
static void node_add_child(cg_node_t *parent, cg_node_t *child)
{
    cg_node_t **nc = (cg_node_t **)realloc(parent->children, (parent->nchildren + 1) * sizeof(cg_node_t *));
    if (!nc) return;
    parent->children = nc;
    parent->children[parent->nchildren++] = child;
    child->parent = parent;
}

static int s_auto_id = 0;

/**
 * @brief 解析单个控件节点（矩形/样式/状态/事件/结构化内容/children 递归）
 * @param o JSON 对象
 * @param err 解析失败时写错误信息
 * @param errsz err 容量
 * @return cg_node_t* 节点；失败返回 NULL
 */
static cg_node_t *parse_node(const cj_val_t *o, char *err, size_t errsz)
{
    cg_node_t *n;
    const cj_val_t *v, *rect;
    const cg_kind_info_t *ki;
    const char *type;
    char auto_id[32];

    if (!o || o->type != CJ_OBJ) {
        snprintf(err, errsz, "widget entry is not an object");
        return NULL;
    }

    type = cj_str(cj_get(o, "type"), "label");
    ki = cg_kind_lookup(type);
    if (!ki) {
        snprintf(err, errsz, "unknown widget type \"%s\"", type);
        return NULL;
    }

    n = (cg_node_t *)calloc(1, sizeof(cg_node_t));
    if (!n) { snprintf(err, errsz, "out of memory"); return NULL; }

    n->type = dup_str(ki->name);
    v = cj_get(o, "id");
    if (v && v->type == CJ_STR && v->str && *v->str) {
        n->id = dup_str(v->str);
    } else {
        snprintf(auto_id, sizeof(auto_id), "%s_%d", ki->name, ++s_auto_id);
        n->id = dup_str(auto_id);
    }
    n->text = dup_str(cj_str(cj_get(o, "text"), ki->def_text));

    /* rect 既支持 {x,y,w,h} 对象，也支持扁平的 x/y/w/h */
    rect = cj_get(o, "rect");
    n->x = cj_int(cj_get(rect, "x"), cj_int(cj_get(o, "x"), 0));
    n->y = cj_int(cj_get(rect, "y"), cj_int(cj_get(o, "y"), 0));
    n->w = cj_int(cj_get(rect, "w"), cj_int(cj_get(o, "w"), ki->def_w));
    n->h = cj_int(cj_get(rect, "h"), cj_int(cj_get(o, "h"), ki->def_h));
    if (n->w < 0) n->w = 0;
    if (n->h < 0) n->h = 0;

    parse_style(cj_get(o, "style"), &n->style);

    /* state 与顶层字段都读，顶层优先（方便手写小文件） */
    {
        const cj_val_t *st = cj_get(o, "state");
        n->visible = cj_bool(cj_get(o, "visible"), cj_bool(cj_get(st, "visible"), 1));
        n->enabled = cj_bool(cj_get(o, "enabled"), cj_bool(cj_get(st, "enabled"), 1));
        n->checked = cj_bool(cj_get(o, "checked"), cj_bool(cj_get(st, "checked"), 0));
        n->value   = cj_int(cj_get(o, "value"), cj_int(cj_get(st, "value"), 0));
        n->vmin    = cj_int(cj_get(o, "min"), cj_int(cj_get(st, "min"), 0));
        n->vmax    = cj_int(cj_get(o, "max"), cj_int(cj_get(st, "max"), 100));
    }
    if (n->vmax <= n->vmin) n->vmax = n->vmin + 1;

    v = cj_get(o, "events");
    if (v && v->type == CJ_OBJ) {
        for (size_t i = 0; i < v->count; i++) {
            const char *key = (v->keys && v->keys[i]) ? v->keys[i] : NULL;
            const char *val = cj_str(v->items[i], NULL);
            int ev = key ? cg_event_from_name(key) : -1;
            if (ev >= 0 && val && *val) n->ev[ev] = dup_str(val);
        }
    }

    /* 标签页归属、图片路径、细分格式、悬停提示、时钟周期、表格增强 */
    n->page = cj_int(cj_get(o, "page"), 0);
    if (n->page < 0) n->page = 0;
    n->src = dup_or_null(cj_str(cj_get(o, "src"), NULL));
    n->format = dup_or_null(cj_str(cj_get(o, "format"), NULL));
    n->hint = dup_or_null(cj_str(cj_get(o, "hint"), NULL));
    n->period = cj_int(cj_get(o, "period"), 0);
    if (n->period < 0) n->period = 0;
    n->multi = cj_bool(cj_get(o, "multi"), 0);
    n->check = cj_bool(cj_get(o, "check"), 0);

    /* 结构化内容 */
    n->items = parse_str_array(cj_get(o, "items"), &n->nitems);
    n->nodes = parse_tree_items(cj_get(o, "nodes"), &n->nnodes);
    n->cols  = parse_columns(cj_get(o, "columns"), &n->ncols);
    n->rows  = parse_rows(cj_get(o, "rows"), &n->nrows, &n->row_cells);

    v = cj_get(o, "children");
    if (v && v->type == CJ_ARR) {
        if (!ki->container) {
            snprintf(err, errsz, "widget \"%s\" (%s) cannot hold children", n->id, n->type);
            node_free(n);
            return NULL;
        }
        for (size_t i = 0; i < v->count; i++) {
            cg_node_t *c = parse_node(v->items[i], err, errsz);
            if (!c) { node_free(n); return NULL; }
            node_add_child(n, c);
        }
    }
    return n;
}

/* ------------------------------------------------------------ 文档 */

/**
 * @brief 解析整份 .cgui 文本
 * @param json UTF-8 JSON 原文
 * @param err 解析失败时写错误信息
 * @param errsz err 容量
 * @return cg_doc_t* 文档（调用方 cg_doc_free）；失败返回 NULL
 */
cg_doc_t *cg_doc_parse(const char *json, char *err, size_t errsz)
{
    cj_val_t *root;
    cg_doc_t *doc;
    const cj_val_t *v, *app, *ws;
    char errbuf[256] = { 0 };

    errbuf[0] = '\0';
    root = cj_parse(json, errbuf, sizeof(errbuf));
    if (!root) {
        snprintf(err, errsz, "JSON: %s", errbuf[0] ? errbuf : "parse failed");
        return NULL;
    }
    if (root->type != CJ_OBJ) {
        cj_free(root);
        snprintf(err, errsz, "top level of a .cgui must be an object");
        return NULL;
    }

    doc = (cg_doc_t *)calloc(1, sizeof(cg_doc_t));
    if (!doc) { cj_free(root); snprintf(err, errsz, "out of memory"); return NULL; }

    v = cj_get(root, "cgui");
    if (v && cj_int(v, 1) > 1) {
        snprintf(err, errsz, "unsupported .cgui version %d (runtime speaks 1)", cj_int(v, 1));
        cj_free(root);
        free(doc);
        return NULL;
    }

    app = cj_get(root, "app");
    doc->title = dup_str(cj_str(cj_get(app, "title"), "CStackGUI Application"));
    doc->width = cj_int(cj_get(app, "width"), 640);
    doc->height = cj_int(cj_get(app, "height"), 420);
    doc->bg = cj_color(cj_get(app, "bg"), CG_COLOR_NONE);
    if (doc->width < 80) doc->width = 80;
    if (doc->height < 60) doc->height = 60;

    /* 窗口属性（app 节点，全部可选；不写就保持 Windows 默认观感） */
    {
        const cj_val_t *vx = cj_get(app, "x");
        const cj_val_t *vy = cj_get(app, "y");
        doc->has_pos = (vx != NULL || vy != NULL);
        doc->x = cj_int(vx, 0);
        doc->y = cj_int(vy, 0);
        doc->center = cj_bool(cj_get(app, "center"), 0);
        doc->border = dup_str(cj_str(cj_get(app, "border"), "sizable"));
        doc->control_box = cj_bool(cj_get(app, "controlBox"), 1);
        doc->min_box = cj_bool(cj_get(app, "minimizeBox"), 1);
        doc->max_box = cj_bool(cj_get(app, "maximizeBox"), 1);
        doc->topmost = cj_bool(cj_get(app, "topmost"), 0);
        doc->taskbar = cj_bool(cj_get(app, "taskbar"), 1);
        doc->visible = cj_bool(cj_get(app, "visible"), 1);
        doc->enabled = cj_bool(cj_get(app, "enabled"), 1);
        doc->enter_tab = cj_bool(cj_get(app, "enterTab"), 0);
        doc->esc_close = cj_bool(cj_get(app, "escClose"), 1);
        doc->tray = cj_bool(cj_get(app, "tray"), 0);
        doc->icon = dup_str(cj_str(cj_get(app, "icon"), NULL));
        doc->cursor = dup_str(cj_str(cj_get(app, "cursor"), NULL));
        doc->class_name = dup_str(cj_str(cj_get(app, "class"), NULL));
        doc->note = dup_str(cj_str(cj_get(app, "note"), NULL));
    }

    /* 程序配置（app.info）：编译进 exe 的版本信息 + 运行时能读到的程序信息 */
    {
        const cj_val_t *in = cj_get(app, "info");
        doc->info.product  = dup_str(cj_str(cj_get(in, "product"), NULL));
        doc->info.desc     = dup_str(cj_str(cj_get(in, "desc"), NULL));
        doc->info.comments = dup_str(cj_str(cj_get(in, "comments"), NULL));
        doc->info.version  = dup_str(cj_str(cj_get(in, "version"), NULL));
        doc->info.build    = dup_str(cj_str(cj_get(in, "build"), NULL));
        doc->info.author   = dup_str(cj_str(cj_get(in, "author"), NULL));
        doc->info.company  = dup_str(cj_str(cj_get(in, "company"), NULL));
        doc->info.copyright = dup_str(cj_str(cj_get(in, "copyright"), NULL));
        doc->info.address  = dup_str(cj_str(cj_get(in, "address"), NULL));
        doc->info.postcode = dup_str(cj_str(cj_get(in, "postcode"), NULL));
        doc->info.phone    = dup_str(cj_str(cj_get(in, "phone"), NULL));
        doc->info.fax      = dup_str(cj_str(cj_get(in, "fax"), NULL));
        doc->info.email    = dup_str(cj_str(cj_get(in, "email"), NULL));
        doc->info.homepage = dup_str(cj_str(cj_get(in, "homepage"), NULL));
    }

    doc->root = (cg_node_t *)calloc(1, sizeof(cg_node_t));
    if (!doc->root) {
        cj_free(root);
        cg_doc_free(doc);   /* 窗口属性那些字段也要一起放（别只手写 free(title)） */
        snprintf(err, errsz, "out of memory");
        return NULL;
    }
    doc->root->type = dup_str("window");
    doc->root->id = dup_str("__window");
    doc->root->w = doc->width;
    doc->root->h = doc->height;

    /* 窗口级事件写成 app.events.close，挂在虚拟根上（窗口自身也是个"控件"） */
    {
        const cj_val_t *ae = cj_get(app, "events");
        if (ae && ae->type == CJ_OBJ) {
            for (int i = 0; i < CG_EVENT_COUNT; i++) {
                const char *cb = cj_str(cj_get(ae, cg_event_name((cg_event_t)i)), NULL);
                if (cb && *cb) doc->root->ev[i] = dup_str(cb);
            }
        }
    }

    ws = cj_get(root, "widgets");
    if (ws && ws->type != CJ_ARR) {
        snprintf(err, errsz, "\"widgets\" must be an array");
        cj_free(root);
        cg_doc_free(doc);
        return NULL;
    }
    for (size_t i = 0; i < cj_count(ws); i++) {
        cg_node_t *n = parse_node(cj_at(ws, i), err, errsz);
        if (!n) {
            cj_free(root);
            cg_doc_free(doc);
            return NULL;
        }
        node_add_child(doc->root, n);
    }

    cj_free(root);
    return doc;
}

/** @brief 释放文档内容，但 doc 本身（可能嵌在别的结构里，或是栈变量）不动 */
/**
 * @brief 释放文档内容（doc 本身不动——它可能内嵌在 cg_app_t 里）
 * @param doc 目标文档
 * @return void 无返回值
 */
void cg_doc_release(cg_doc_t *doc)
{
    if (!doc) return;
    free(doc->title);
    free(doc->border);
    free(doc->icon);
    free(doc->cursor);
    free(doc->class_name);
    free(doc->note);
    free(doc->info.product);  free(doc->info.desc);     free(doc->info.comments);
    free(doc->info.version);  free(doc->info.build);    free(doc->info.author);
    free(doc->info.company);  free(doc->info.copyright); free(doc->info.address);
    free(doc->info.postcode); free(doc->info.phone);    free(doc->info.fax);
    free(doc->info.email);    free(doc->info.homepage);
    node_free(doc->root);
    memset(doc, 0, sizeof(*doc));
}

/** @brief 释放内容并释放 doc 本身 */
/**
 * @brief 释放文档内容并释放 doc 本身（只给 cg_doc_parse 的返回值用）
 * @param doc cg_doc_parse 返回的文档
 * @return void 无返回值
 */
void cg_doc_free(cg_doc_t *doc)
{
    if (!doc) return;
    cg_doc_release(doc);
    free(doc);
}

/* ------------------------------------------------------------ 打印 */

/* ------------------------------------------------------------ 打印 */

/**
 * @brief 以缩进形式递归打印一个节点（含状态、事件绑定、结构化内容摘要）
 * @param n 节点；out 输出流；depth 缩进层数
 * @return void 无返回值
 */
static void dump_node(const cg_node_t *n, FILE *out, int depth)
{
    for (int i = 0; i < depth; i++) fputs("  ", out);
    fprintf(out, "%s #%s rect=(%d,%d,%d,%d) text=\"%s\"",
            n->type ? n->type : "?", n->id ? n->id : "-", n->x, n->y, n->w, n->h,
            n->text ? n->text : "");
    if (!n->visible) fputs(" hidden", out);
    if (!n->enabled) fputs(" disabled", out);
    if (n->checked) fputs(" checked", out);
    if (n->page) fprintf(out, " page=%d", n->page);
    if (n->src) fprintf(out, " src=%s", n->src);
    if (n->format) fprintf(out, " format=%s", n->format);
    if (n->hint) fprintf(out, " hint=\"%s\"", n->hint);
    if (n->period) fprintf(out, " period=%d", n->period);
    if (n->multi) fputs(" multi", out);
    if (n->check) fputs(" check", out);
    if (n->items) fprintf(out, " items=%lu", (unsigned long)n->nitems);
    if (n->nodes) fprintf(out, " tree=%lu", (unsigned long)n->nnodes);
    if (n->cols) fprintf(out, " cols=%lu", (unsigned long)n->ncols);
    if (n->rows) fprintf(out, " rows=%lu", (unsigned long)n->nrows);
    if (n->style.font || n->style.size)
        fprintf(out, " font=%s/%d%s", n->style.font ? n->style.font : "-", n->style.size,
                n->style.bold ? " bold" : "");
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (n->ev[i]) fprintf(out, " %s->%s", cg_event_name((cg_event_t)i), n->ev[i]);
    }
    fputc('\n', out);
    for (size_t i = 0; i < n->nchildren; i++) dump_node(n->children[i], out, depth + 1);
}

/** @brief 以缩进树的形式打印到 out（自检 / 排查用） */
/**
 * @brief 把整份文档树打印到 out（自检 / 排查用）
 * @param doc 文档；out 输出流
 * @return void 无返回值
 */
void cg_doc_dump(const cg_doc_t *doc, FILE *out)
{
    if (!doc || !out) return;
    fprintf(out, "app \"%s\" %dx%d bg=#%06X",
            doc->title ? doc->title : "-", doc->width, doc->height,
            doc->bg == CG_COLOR_NONE ? 0 : doc->bg);
    if (doc->has_pos) fprintf(out, " pos=(%d,%d)", doc->x, doc->y);
    if (doc->center) fputs(" centered", out);
    if (doc->border && strcmp(doc->border, "sizable")) fprintf(out, " border=%s", doc->border);
    if (!doc->control_box) fputs(" no-controlbox", out);
    if (!doc->min_box) fputs(" no-minbox", out);
    if (!doc->max_box) fputs(" no-maxbox", out);
    if (doc->topmost) fputs(" topmost", out);
    if (!doc->taskbar) fputs(" no-taskbar", out);
    if (!doc->visible) fputs(" hidden", out);
    if (!doc->enabled) fputs(" disabled", out);
    if (doc->enter_tab) fputs(" enter-tab", out);
    if (!doc->esc_close) fputs(" no-esc-close", out);
    if (doc->tray) fputs(" tray", out);
    if (doc->icon) fprintf(out, " icon=%s", doc->icon);
    if (doc->cursor) fprintf(out, " cursor=%s", doc->cursor);
    if (doc->class_name) fprintf(out, " class=%s", doc->class_name);
    if (doc->info.product) fprintf(out, " product=\"%s\"", doc->info.product);
    if (doc->info.version) fprintf(out, " version=%s", doc->info.version);
    fputc('\n', out);
    if (doc->root) {
        for (size_t i = 0; i < doc->root->nchildren; i++) dump_node(doc->root->children[i], out, 1);
    }
}
