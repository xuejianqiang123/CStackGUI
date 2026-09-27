/**
 * @file    cg_doc.c
 * @brief   .cgui 解析与打印：JSON -> 控件文档树
 * @details 控件类型表（k_kinds）与 cg_kind_* 查询拆到 cg_doc_kind.c；
 *          那张表要和画布端 model.ts 的 KINDS 保持同步。
 */
#include "cg_doc.h"
#include "cg_json.h"

#include <stdlib.h>
#include <string.h>

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
            /* 菜单项引用动作（树节点读它也无害：tree 不会去看这个字段） */
            it->action = dup_or_null(cj_str(cj_get(o, "action"), NULL));
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
        free(items[i]->action);
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
    free(n->sport);
    free(n->bind_src);
    free(n->action);
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
    int alias_multi, alias_spin;

    if (!o || o->type != CJ_OBJ) {
        snprintf(err, errsz, "widget entry is not an object");
        return NULL;
    }

    type = cj_str(cj_get(o, "type"), "label");
    /* 老写法自动升级：multiedit、spinner 已合并进 edit（2026-09-22）。
     * 读到旧名不报错，按新字段补齐（见下面两处），dump 出去就已经是 edit ——
     * 这份兼容必须留在解析层：未知类型是**整份文档解析失败**，不是降级成标签。 */
    alias_multi = (strcmp(type, "multiedit") == 0);
    alias_spin  = (strcmp(type, "spinner") == 0);
    if (alias_multi || alias_spin) type = "edit";
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
    /* text 缺失就是**空文字**：.cgui 是唯一真相，别拿类型的默认文字回填。
     * def_text（"按钮"/"标签"/…）只服务于画布调色板新建控件和 cg_widget_add()
     * 程序化创建；在解析时回填会让"画布上清空的文字"被顶回来，
     * 表现出来就是空文字按钮里又长出一个"按钮"（图片按钮最明显）。 */
    n->text = dup_str(cj_str(cj_get(o, "text"), ""));

    /* rect 既支持 {x,y,w,h} 对象，也支持扁平的 x/y/w/h */
    rect = cj_get(o, "rect");
    n->x = cj_int(cj_get(rect, "x"), cj_int(cj_get(o, "x"), 0));
    n->y = cj_int(cj_get(rect, "y"), cj_int(cj_get(o, "y"), 0));
    n->w = cj_int(cj_get(rect, "w"), cj_int(cj_get(o, "w"), ki->def_w));
    n->h = cj_int(cj_get(rect, "h"), cj_int(cj_get(o, "h"), ki->def_h));
    if (n->w < 0) n->w = 0;
    if (n->h < 0) n->h = 0;

    /* grow：余量分配权重。走 cj_int 的常规口径（小数四舍五入、true 当 1、数字串按数字读），
     * 随后**负数与超过 99 的归 0** —— 不报错、不中断加载。
     * 注意别把这里写成"非整数当 0"：契约里那条口径漂移过，源头就是这句注释（2026-09-27 更正）。 */
    n->grow = cj_int(cj_get(o, "grow"), 0);
    if (n->grow < 0 || n->grow > 99) n->grow = 0;

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

    /* 串口组件（serial）：越界值一律回退缺省，别让手写的 .cgui 造出怪参数 */
    n->sport = dup_or_null(cj_str(cj_get(o, "port"), NULL));
    n->s_baud = cj_int(cj_get(o, "baud"), 9600);
    if (n->s_baud <= 0) n->s_baud = 9600;
    n->s_bits = cj_int(cj_get(o, "bits"), 8);
    if (n->s_bits < 5 || n->s_bits > 8) n->s_bits = 8;
    n->s_stop = cj_int(cj_get(o, "stop"), 1);
    if (n->s_stop < 1 || n->s_stop > 2) n->s_stop = 1;
    n->s_parity = cj_int(cj_get(o, "parity"), 0);
    if (n->s_parity < 0 || n->s_parity > 2) n->s_parity = 0;
    n->s_auto = cj_bool(cj_get(o, "auto"), 0);
    n->multi = cj_bool(cj_get(o, "multi"), 0);
    n->check = cj_bool(cj_get(o, "check"), 0);

    /* edit（单行输入）的输入框行为。越界值一律退回缺省：.cgui 允许手写，
     * 一个 scroll=9 不该让输入框变成"没有滚动条但也不能输入"的怪状态。
     * 对齐方式不在这里 —— 它复用 style.align（和标签同一个字段）。 */
    n->multiline = cj_bool(cj_get(o, "multiline"), 0);
    n->scroll = cj_int(cj_get(o, "scroll"), 0);
    if (n->scroll < 0 || n->scroll > 3) n->scroll = 0;
    n->maxlen = cj_int(cj_get(o, "maxlen"), 0);
    if (n->maxlen < 0) n->maxlen = 0;
    n->conv = cj_int(cj_get(o, "conv"), 0);
    if (n->conv != 1 && n->conv != 2) n->conv = 0;
    n->input = cj_int(cj_get(o, "input"), 0);
    if (n->input < 0 || n->input > 11) n->input = 0;   /* 见 cg_doc.h 的输入方式档位 */
    n->border = cj_int(cj_get(o, "border"), 0);
    if (n->border < 0 || n->border > 6) n->border = 0;
    n->spin = cj_int(cj_get(o, "spin"), 0);
    if (n->spin < 0 || n->spin > 2) n->spin = 0;

    /* 旧类型名补齐字段（见函数开头 alias_*）。写在前面的字段优先，作者写了的就不动。 */
    if (alias_multi) {                     /* multiedit：以前写死 ES_MULTILINE + 纵向滚动 */
        n->multiline = 1;
        if (n->scroll == 0) n->scroll = 2;
    }
    if (alias_spin) {                      /* spinner：以前是独立的 EDIT + 上下箭头 */
        if (n->spin == 0) n->spin = 1;     /* 自动：±1 并夹到 min/max */
        if (n->input == 0) n->input = 7;   /* 整数：字符只放行数字与负号，失焦夹到范围内 */
        /* 不用管 text：数值框的显示文本由 value 派生（见 cg_widget.c 的 build_node），
         * 这样老文件里留空的 text 不会被写成一个会和 value 漂移的副本。 */
    }

    /* 引用动作：顶层 actions 里的名字。这里只记名字，校验（动作是否存在、该类型
     * 支不支持 click、有没有同时绑 click）在运行时构建时做 —— 解析层不该知道
     * "哪些类型支持 click"（那是 k_kinds 的事，进来一次就查一次反而更贵）。 */
    n->action = dup_or_null(cj_str(cj_get(o, "action"), NULL));

    /* 控件绑定：bind { "src": "源控件id", "two": true }
     * 只认对象写法；src 空串等于没绑（画布没选源时就是这么写的） */
    v = cj_get(o, "bind");
    if (v && v->type == CJ_OBJ) {
        const char *bind_src = cj_str(cj_get(v, "src"), NULL);
        if (bind_src && *bind_src) {
            n->bind_src = dup_str(bind_src);
            n->bind_two = cj_bool(cj_get(v, "two"), 0);
        }
    }

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

    /* 顶层 actions：命名动作表。**键就是动作名**（不是数组，所以顺序由书写顺序定）。
     * 两个宽容处，都写在文档里：空名的条目直接跳过（没有引用它的办法）；
     * 重名的后者覆盖（画布不会写出来，只有手写 .cgui 才可能）。
     * 缺 handler 的动作**不在这里报错**：它是 UI 便利设施，不该让整份界面打不开 ——
     * 运行时构建时警告，--selftest 报错（与"绑了不支持的事件"同一套兜底）。 */
    v = cj_get(root, "actions");
    if (v && v->type == CJ_OBJ && v->count > 0) {
        doc->actions = (cg_action_t *)calloc(v->count, sizeof(cg_action_t));
        if (doc->actions) {
            for (size_t i = 0; i < v->count; i++) {
                const cj_val_t *o = v->items[i];
                const char *name = (v->keys && v->keys[i]) ? v->keys[i] : NULL;
                cg_action_t *a;
                size_t j;

                if (!name || !*name) continue;

                for (j = 0; j < doc->nactions; j++) {
                    if (doc->actions[j].name && strcmp(doc->actions[j].name, name) == 0) break;
                }
                if (j < doc->nactions) {
                    a = &doc->actions[j];          /* 重名：后者覆盖，名字保留 */
                    free(a->text);
                    free(a->accel);
                    free(a->handler);
                    a->text = a->accel = a->handler = NULL;
                } else {
                    a = &doc->actions[doc->nactions++];
                    a->name = dup_str(name);
                }
                a->text    = dup_or_null(cj_str(cj_get(o, "text"), NULL));
                a->accel   = dup_or_null(cj_str(cj_get(o, "accel"), NULL));
                a->handler = dup_or_null(cj_str(cj_get(o, "handler"), NULL));
                a->checked = cj_bool(cj_get(o, "checked"), 0);
                a->enabled = cj_bool(cj_get(o, "enabled"), 1);
            }
        }
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
    if (doc->actions) {
        for (size_t i = 0; i < doc->nactions; i++) {
            free(doc->actions[i].name);
            free(doc->actions[i].text);
            free(doc->actions[i].accel);
            free(doc->actions[i].handler);
        }
        free(doc->actions);
    }
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
    if (n->sport)
        fprintf(out, " port=%s baud=%d bits=%d stop=%d parity=%d%s",
                n->sport, n->s_baud, n->s_bits, n->s_stop, n->s_parity,
                n->s_auto ? " auto" : "");
    if (n->bind_src) fprintf(out, " bind=%s%s", n->bind_src, n->bind_two ? "+two" : "");
    if (n->action) fprintf(out, " action=%s", n->action);
    if (n->multi) fputs(" multi", out);
    if (n->check) fputs(" check", out);
    if (n->multiline) fputs(" multiline", out);
    if (n->scroll) fprintf(out, " scroll=%d", n->scroll);
    if (n->maxlen) fprintf(out, " maxlen=%d", n->maxlen);
    if (n->conv) fprintf(out, " conv=%d", n->conv);
    if (n->input) fprintf(out, " input=%d", n->input);
    if (n->border) fprintf(out, " border=%d", n->border);
    if (n->spin) fprintf(out, " spin=%d", n->spin);
    if (n->items) fprintf(out, " items=%lu", (unsigned long)n->nitems);
    if (n->nodes) fprintf(out, " tree=%lu", (unsigned long)n->nnodes);
    if (n->cols) fprintf(out, " cols=%lu", (unsigned long)n->ncols);
    if (n->rows) fprintf(out, " rows=%lu", (unsigned long)n->nrows);
    /* grow 也要打出来：示例域的队友指出过——没有它，`selftest OK` 只能证明"文件读进去了"，
     * 证明不了 grow 真的进了控件（门禁对它是盲的）。只在非 0 时打，保持旧输出不变。 */
    if (n->grow) fprintf(out, " grow=%d", n->grow);
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
    /* 动作表：菜单项的 action 引用只在行表里（dump 不展开 menus），
     * 所以这里把动作本身列全，排查"名字拼错"时一眼能对上。 */
    if (doc->nactions) {
        fprintf(out, "actions %lu:\n", (unsigned long)doc->nactions);
        for (size_t i = 0; i < doc->nactions; i++) {
            const cg_action_t *a = &doc->actions[i];
            fprintf(out, "  %s text=\"%s\" handler=%s", a->name ? a->name : "-",
                    a->text ? a->text : "", a->handler ? a->handler : "-");
            if (a->accel) fprintf(out, " accel=%s", a->accel);
            if (a->checked) fputs(" checked", out);
            if (!a->enabled) fputs(" disabled", out);
            fputc('\n', out);
        }
    }
    if (doc->root) {
        for (size_t i = 0; i < doc->root->nchildren; i++) dump_node(doc->root->children[i], out, 1);
    }
}
