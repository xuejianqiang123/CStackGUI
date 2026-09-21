/**
 * @file    cg_widget_data.c
 * @brief   控件内容 API：列表/树/表格/状态栏/标签页/图片/菜单/提示/时钟、动态追加控件
 * @details 从原 cg_app.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>       /* terminal 行的时间戳（和 cg_log.c 用同一套标准 C 时间函数） */

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
    w->items[w->nitems] = cg_str_dup(item);
    if (!w->items[w->nitems]) return -1;
    w->nitems++;
    w->items_dirty = 1;
    w->scroll_bottom = 1;
    cg_widget_sync(w);
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
    cg_widget_sync(w);
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
    cg_widget_sync(w);
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
        int idx = cg_widget_tree_push(w, parent, text, 1);
        if (idx < 0) return -1;
        w->items_dirty = 1;
        cg_widget_sync(w);
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
    cg_widget_sync(w);
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
    cg_widget_sync(w);
}

/* ------------------------------------------------------------ 表格 */

/** @brief 表格行数变多时把行级附加数组（勾选/数值/图标）一起补长到 want */
void cg_table_rows_grow(cg_widget_t *w, int want)
{
    if (want > w->row_cap) {
        unsigned char *nc = (unsigned char *)realloc(w->row_checked, (size_t)want);
        int *nv = (int *)realloc(w->row_val, (size_t)want * sizeof(int));
        if (!nc && !nv) return;   /* 内存不足：两个都没长成，保持原状，别把 row_cap 抬上去 */
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
    w->cols[w->ncols].title = cg_str_dup(title ? title : "");
    w->cols[w->ncols].width = width;
    w->ncols++;
    w->items_dirty = 1;
    cg_widget_sync(w);
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
            w->rows[r][c] = cg_str_dup(cell);
        }
    }
    cg_table_rows_grow(w, w->nrows + 1);
    w->nrows++;
    w->row_cells = w->ncols;
    w->items_dirty = 1;
    cg_widget_sync(w);
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
    cg_widget_sync(w);
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
    w->items[part] = cg_str_dup(text ? text : "");
    w->items_dirty = 1;
    cg_widget_sync(w);
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
    cg_widget_sync(w);
    cg_bind_propagate(w);
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
    w->src = (path && *path) ? cg_str_dup(path) : NULL;
    w->image_failed = 0;     /* 换成新路径，给后端一次重新加载的机会 */
    cg_widget_sync(w);                 /* 后端的 sync 发现路径变了会先释放旧位图 */
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
    w->hint = (text && *text) ? cg_str_dup(text) : NULL;
    if (w->node) {
        free(w->node->hint);
        w->node->hint = w->hint ? cg_str_dup(w->hint) : NULL;
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
    if (parent) cg_widget_attach_child(parent, nw);

    if (cg_backend_active && cg_backend_active->attach) cg_backend_active->attach(nw);
    cg_widget_sync(nw);
    return nw;
}

/* ------------------------------------------------------------ 分隔条（splitter）
 * 语义一句话：拖我 = 撑开我**前面那一块**，我之后的兄弟整体让位，同一容器里**最后一块**
 * 吃掉余量（它反过来缩水）。于是 2 段布局就是"上/左变大、下/右变小"，多段布局也自洽：
 *      [pane1][splitter][pane2][splitter2][pane3]
 * 每根分隔条只认"前一块 + 尾巴"，不需要知道中间有几块，也就**不需要布局引擎**。
 * 没有矩形的东西（菜单栏、时钟）不参与。
 */

/** 分隔条的最小边长：拖到这个尺寸就停住，免得把面板拖没了 */
#define CG_SPLIT_MIN 16

/** @brief 这个兄弟参与分隔条布局吗（菜单栏 / 时钟没有矩形，跳过） */
static int split_layout_sibling(const cg_widget_t *s)
{
    return s && strcmp(s->type, "menu") != 0 && strcmp(s->type, "timer") != 0;
}

/** @brief 分隔条是不是竖的：左右分栏 = 竖条（format 缺省）；format == "row" 才是横条 */
static int split_is_vert(const cg_widget_t *w)
{
    return !(w->format && strcmp(w->format, "row") == 0);
}

/**
 * @brief 拖动分隔条：撑开前一个兄弟、自己与之后的兄弟整体让位、最后一块吃掉余量
 * @param w 分隔条控件
 * @param delta 本次想移动多少像素（正 = 向右 / 向下）
 * @return int 实际应用的位移；0 = 没动（被最小尺寸夹住，或结构上不够两块）
 */
int cg_splitter_apply(cg_widget_t *w, int delta)
{
    cg_widget_t *parent, *prev = NULL, *last = NULL;
    int          idx = -1, vert, applied, has_next = 0;
    int          i;

    if (!w || !(parent = w->parent) || delta == 0) return 0;

    for (i = 0; i < (int)parent->nchildren; i++) {
        if (parent->children[i] == w) { idx = i; break; }
    }
    if (idx < 0) return 0;

    /* 前面得有块、后面也得有块，否则没什么可撑开的 */
    for (i = idx - 1; i >= 0; i--) {
        if (split_layout_sibling(parent->children[i])) { prev = parent->children[i]; break; }
    }
    for (i = (int)parent->nchildren - 1; i >= 0; i--) {
        if (!split_layout_sibling(parent->children[i])) continue;
        if (!last) last = parent->children[i];      /* 倒着找，第一个就是最后一块 */
        if (i > idx) has_next = 1;
    }
    if (!prev || !last || !has_next) return 0;

    vert = split_is_vert(w);
    applied = delta;
    {
        /* 夹住两头：前一块与最后一块都不许小于 CG_SPLIT_MIN */
        int prev_size = vert ? prev->w : prev->h;
        int last_size = vert ? last->w : last->h;
        if (prev_size + applied < CG_SPLIT_MIN) applied = CG_SPLIT_MIN - prev_size;
        if (last_size - applied < CG_SPLIT_MIN) applied = last_size - CG_SPLIT_MIN;
    }
    if (applied == 0) return 0;

    /* 1) 前一块变大 */
    if (vert) cg_widget_set_size(prev, prev->w + applied, prev->h);
    else      cg_widget_set_size(prev, prev->w, prev->h + applied);

    /* 2) 我以及我之后的（含最后一块）整体让位 */
    for (i = idx; i < (int)parent->nchildren; i++) {
        cg_widget_t *s = parent->children[i];
        if (!split_layout_sibling(s)) continue;
        if (vert) cg_widget_set_pos(s, s->x + applied, s->y);
        else      cg_widget_set_pos(s, s->x, s->y + applied);
    }

    /* 3) 最后一块吃掉余量：它的另一条边不动，所以尾巴不会溢出容器 */
    if (vert) cg_widget_set_size(last, last->w - applied, last->h);
    else      cg_widget_set_size(last, last->w, last->h - applied);

    return applied;
}

/** @brief 分隔条当前位置（相对父容器：竖条看 x，横条看 y）；w 为空返回 -1 */
int cg_splitter_pos(const cg_widget_t *w)
{
    if (!w) return -1;
    return split_is_vert(w) ? w->x : w->y;
}

/** @brief 把分隔条搬到 pos（相对父容器），两侧面板跟着重排（内部会夹到最小尺寸） */
void cg_splitter_set(cg_widget_t *w, int pos)
{
    int delta;

    if (!w || !want_type(w, "splitter", "cg_splitter_set")) return;
    delta = pos - (split_is_vert(w) ? w->x : w->y);
    if (delta) cg_splitter_apply(w, delta);
}

/* ------------------------------------------------------------ 终端 / 日志视图（terminal）
 * 业务只管把收到的字节丢进来（`cg_term_write`），格式（时间戳、HEX、方向标记）与
 * 行数上限都由它管。行的颜色按方向分，后端用 RichEdit 的字符格式着色 ——
 * 不用 ownerdraw，白捡"选择/复制"与自动滚动（日志最常被做的事就是复制出去）。
 *
 * 行的内存表示：`items[i]` 的**首字节**是方向标记（CG_TERM_RX/TX/SYS），后面才是
 * 给人看的文字。用首字节而不是平行数组：裁剪、清空、保存都只面对一个数组，
 * 标记天然跟着行走。这几个字节都 < 0x20，在 UTF-8 里是合法单字节，不影响显示。
 */

/** 默认保留多少行（.cgui 的 value 可以改） */
#define CG_TERM_DEF_LINES 500
/** 单行最长多少字符（一次 write 粘进来一坨巨长数据时兜底） */
#define CG_TERM_MAX_LINE 1024

/** @brief 时间戳 "HH:MM:SS"（和 cg_log 的写法一致：秒级够看间隔） */
static void term_stamp(char *buf, size_t size)
{
    time_t     t = time(NULL);
    struct tm *tmv = localtime(&t);

    if (tmv && strftime(buf, size, "%H:%M:%S", tmv) > 0) return;
    snprintf(buf, size, "--:--:--");
}

/** @brief 方向标记 -> 行首那三个字符的记号 */
static const char *term_dir_tag(int dir)
{
    switch (dir) {
    case CG_TERM_RX:  return "RX ";
    case CG_TERM_TX:  return "TX ";
    default:          return "-- ";
    }
}

/** @brief 当前是不是 HEX 显示模式（.cgui 的 format == "hex"） */
static int term_is_hex(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "hex") == 0;
}

/**
 * @brief 把一段字节渲染成可读文字
 * @details HEX 模式两个一组空格分开；文本模式原样保留可见字节，
 *          只把控制字符换成 '.' —— 0x80 以上一律保留，这样中文（UTF-8 多字节）
 *          不会被搅成点，而 \r\n\0 这些也不会把界面搞乱。
 */
static void term_format_payload(const cg_widget_t *w, const unsigned char *data, int len,
                                char *out, size_t out_size)
{
    size_t used = 0;
    int    i;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!data || len <= 0) return;

    if (term_is_hex(w)) {
        for (i = 0; i < len; i++) {
            int wrote = snprintf(out + used, out_size - used, i ? " %02X" : "%02X", data[i]);
            if (wrote <= 0 || used + (size_t)wrote >= out_size) break;
            used += (size_t)wrote;
        }
    } else {
        for (i = 0; i < len && used + 1 < out_size; i++) {
            unsigned char c = data[i];
            out[used++] = (c < 0x20 || c == 0x7F) ? '.' : (char)c;
        }
        out[used] = '\0';
    }
    if (i < len && used + 4 < out_size) {          /* 被截断了就给个记号，别让人以为数据就这些 */
        snprintf(out + used, out_size - used, " ...");
    }
}

/** @brief 组装一行（首字节 = 方向标记，其余是给人看的文字）；调用方 free */
static char *term_make_line(cg_widget_t *w, int dir, const unsigned char *data, int len)
{
    char    payload[CG_TERM_MAX_LINE];
    char    stamp[16] = "";
    char    prefix[32] = "";
    size_t  cap;
    char   *line;

    if (dir < CG_TERM_RX || dir > CG_TERM_SYS) dir = CG_TERM_SYS;
    term_format_payload(w, data, len, payload, sizeof(payload));
    if (w->checked) term_stamp(stamp, sizeof(stamp));   /* checked = 显示时间戳 */
    if (stamp[0]) snprintf(prefix, sizeof(prefix), "[%s] ", stamp);

    cap = strlen(prefix) + strlen(payload) + 16;
    line = (char *)malloc(cap);
    if (!line) return NULL;
    line[0] = (char)dir;
    snprintf(line + 1, cap - 1, "%s%s%s", prefix, term_dir_tag(dir), payload);
    return line;
}

/** @brief 追加一行到 items 并按上限丢弃最老的（不 sync，给 cg_term_write 批量用） */
static void term_append_line(cg_widget_t *w, int dir, const unsigned char *data, int len)
{
    char **ni;
    char  *line = term_make_line(w, dir, data, len);
    int    max;

    if (!line) return;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) { free(line); return; }
    w->items = ni;
    w->items[w->nitems++] = line;

    max = w->value > 0 ? w->value : CG_TERM_DEF_LINES;
    while (w->nitems > max) {
        free(w->items[0]);
        memmove(w->items, w->items + 1, ((size_t)w->nitems - 1) * sizeof(char *));
        w->nitems--;
    }
}

/**
 * @brief 往终端里写一段数据（一次调用 = 一行；带 \r\n 的会自动切成多行）
 * @param w terminal 控件
 * @param dir 方向：CG_TERM_RX / CG_TERM_TX / CG_TERM_SYS（决定行首标记与颜色）
 * @param data 数据；len 传 -1 表示按 C 字符串取长度（文本用）
 * @param len 字节数
 * @details 串口/网络一次收进来 "OK\r\nERR\r\n" 是常态，所以这里按 '\n' 切行，
 *          '\r' 丢掉；一次调用只触发一次界面刷新。
 */
void cg_term_write(cg_widget_t *w, int dir, const void *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    int start = 0;
    int i;

    if (!w || !want_type(w, "terminal", "cg_term_write")) return;
    if (len < 0) len = data ? (int)strlen((const char *)data) : 0;
    if (len <= 0) { term_append_line(w, dir, p, 0); cg_widget_sync(w); return; }

    for (i = 0; i <= len; i++) {
        if (i < len && p[i] != '\n') continue;
        {
            int end = i;
            while (end > start && p[end - 1] == '\r') end--;
            /* 结尾多出来的空段（数据以 \n 收尾）不再补一行空行 */
            if (end > start || i < len) term_append_line(w, dir, p + start, end - start);
        }
        start = i + 1;
    }
    w->items_dirty = 0;      /* 不是"重建"，后端只需追加（见 sync_terminal） */
    cg_widget_sync(w);
}

/** @brief 清空终端 */
void cg_term_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "terminal", "cg_term_clear")) return;
    for (int i = 0; i < w->nitems; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->nitems = 0;
    w->term_pushed = 0;
    w->items_dirty = 1;      /* 让后端整体重灌（清屏） */
    cg_widget_sync(w);
}

/** @brief 切 HEX / 文本显示（只影响**之后**写入的行，已经显示的不动） */
void cg_term_set_hex(cg_widget_t *w, int on)
{
    if (!w || !want_type(w, "terminal", "cg_term_set_hex")) return;
    free(w->format);
    w->format = on ? cg_str_dup("hex") : NULL;
    if (w->node) {
        free(w->node->format);
        w->node->format = on ? cg_str_dup("hex") : NULL;
    }
}

/** @brief 当前是不是 HEX 模式 */
int cg_term_hex(const cg_widget_t *w)
{
    return w && term_is_hex(w);
}

/** @brief 每行前面要不要带 [HH:MM:SS]（存的是控件的 checked 字段） */
void cg_term_set_timestamp(cg_widget_t *w, int on)
{
    cg_widget_set_checked(w, on ? 1 : 0);
}

/** @brief 最多保留多少行（超了从最老的丢；0 或负数 = 恢复默认 500） */
void cg_term_set_max_lines(cg_widget_t *w, int lines)
{
    if (!w || !want_type(w, "terminal", "cg_term_set_max_lines")) return;
    w->value = lines > 0 ? lines : 0;
    if (w->node) w->node->value = w->value;
}

/** @brief 当前有多少行 */
int cg_term_count(const cg_widget_t *w)
{
    return w ? w->nitems : 0;
}

/**
 * @brief 把终端里的内容存成文本文件（UTF-8，行尾 CRLF；不带方向标记与时间戳外的装饰）
 * @return int 0 成功；-1 参数不对；其它 = fopen 失败
 */
int cg_term_save(cg_widget_t *w, const char *path)
{
    FILE *f;

    if (!w || !path || !*path) return -1;
    if (!want_type(w, "terminal", "cg_term_save")) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;

    for (int i = 0; i < w->nitems; i++) {
        const char *line = w->items[i] ? w->items[i] : "";
        unsigned char tag = (unsigned char)line[0];
        if (tag >= CG_TERM_RX && tag <= CG_TERM_SYS) line++;   /* 去掉方向标记 */
        fputs(line, f);
        fputs("\r\n", f);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------ 折线 / 柱状图（chart）
 * 实时可视化的那块：业务按采样节奏 push 点，窗口、范围、网格与绘制都由控件管。
 *   - 系列名存在 items[] 里（每个条目一个系列），颜色按调色板自动分配，不用额外字段；
 *   - 点数据在拍平的 chart_pts（chart_nseries × chart_cap）里，按需分配；
 *   - **窗口满了整体左移一格**，不做环形缓冲：cap 只有几十到几百，memmove 一下比维护
 *     读写指针 + 一圈边界判断便宜得多，也不会出现"绕回时算错下标"这种隐蔽 bug。
 * 绘制策略见 cg_win_draw.c 的 ChartProc（点数超过像素宽度时按列画 min/max）。
 */

/** 每系列保留多少点（.cgui 的 value）；小于 2 就按缺省 120 */
#define CG_CHART_DEF_CAP 120
/** 系列数上限：再多就该换控件了，顺便防 .cgui 写错把内存吃光 */
#define CG_CHART_MAX_SERIES 8
/** 窗口点数上限（一条曲线最多几万个点也够用了） */
#define CG_CHART_MAX_CAP 4096

/** @brief 每系列窗口点数（夹到合理区间） */
static int chart_cap(const cg_widget_t *w)
{
    int c = w->value;

    if (c < 2) c = CG_CHART_DEF_CAP;
    if (c > CG_CHART_MAX_CAP) c = CG_CHART_MAX_CAP;
    return c;
}

/** @brief 系列数：items 为空时按 1 个系列算（单条曲线的 .cgui 不用写 items） */
static int chart_nseries(const cg_widget_t *w)
{
    int n = w->nitems > 0 ? w->nitems : 1;

    return n > CG_CHART_MAX_SERIES ? CG_CHART_MAX_SERIES : n;
}

/** @brief 点缓冲按需建 / 重建（items 或 value 变了就重建），返回系列数 */
int cg_chart_ensure(cg_widget_t *w)
{
    int n = chart_nseries(w);
    int cap = chart_cap(w);

    if (w->chart_pts && w->chart_fill && w->chart_nseries == n && w->chart_cap == cap) return n;

    free(w->chart_pts);
    free(w->chart_fill);
    w->chart_pts = (int *)calloc((size_t)n * (size_t)cap, sizeof(int));
    w->chart_fill = (int *)calloc((size_t)n, sizeof(int));
    w->chart_nseries = (w->chart_pts && w->chart_fill) ? n : 0;
    w->chart_cap = w->chart_nseries ? cap : 0;
    return w->chart_nseries;
}

/** @brief 往某个系列塞一个点（不刷新，给批量接口用） */
static void chart_push_raw(cg_widget_t *w, int series, int value)
{
    int *buf;
    int  cap = w->chart_cap;

    if (!w->chart_pts || !w->chart_fill || cap <= 0) return;
    if (series < 0 || series >= w->chart_nseries) series = 0;
    buf = w->chart_pts + (size_t)series * (size_t)cap;
    if (w->chart_fill[series] < cap) {
        buf[w->chart_fill[series]++] = value;
    } else {
        memmove(buf, buf + 1, (size_t)(cap - 1) * sizeof(int));
        buf[cap - 1] = value;
    }
}

/** @brief 给某个系列追加一个点（满了顶掉最老的） */
void cg_chart_push(cg_widget_t *w, int series, int value)
{
    if (!w || !want_type(w, "chart", "cg_chart_push")) return;
    if (cg_chart_ensure(w) <= 0) return;
    chart_push_raw(w, series, value);
    cg_widget_sync(w);
}

/** @brief 一次给前 nvalues 个系列各追加一个点（多通道采样最常用的一次调用） */
void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues)
{
    int i, n;

    if (!w || !values || !want_type(w, "chart", "cg_chart_push_all")) return;
    n = cg_chart_ensure(w);
    if (n <= 0) return;
    for (i = 0; i < nvalues && i < n; i++) chart_push_raw(w, i, values[i]);
    cg_widget_sync(w);
}

/** @brief 清空所有点（保留系列与范围） */
void cg_chart_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "chart", "cg_chart_clear")) return;
    if (cg_chart_ensure(w) <= 0) return;
    memset(w->chart_fill, 0, (size_t)w->chart_nseries * sizeof(int));
    cg_widget_sync(w);
}

/** @brief 运行时加一个系列（返回下标；超过上限或内存不足返回 -1） */
int cg_chart_add_series(cg_widget_t *w, const char *name)
{
    char **ni;

    if (!w || !want_type(w, "chart", "cg_chart_add_series")) return -1;
    if (w->nitems >= CG_CHART_MAX_SERIES) return -1;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) return -1;
    w->items = ni;
    w->items[w->nitems++] = cg_str_dup(name && *name ? name : "系列");
    cg_chart_ensure(w);              /* 系列数变了：缓冲按新的宽度重建 */
    cg_widget_sync(w);
    return w->nitems - 1;
}

/** @brief 系列数（items 为空时是 1） */
int cg_chart_series_count(const cg_widget_t *w)
{
    return w ? chart_nseries(w) : 0;
}

/** @brief 某系列已经填了多少点 */
int cg_chart_filled(const cg_widget_t *w, int series)
{
    if (!w || !w->chart_fill) return 0;
    if (series < 0 || series >= w->chart_nseries) return 0;
    return w->chart_fill[series];
}

/** @brief 读回某个点（下标 0 = 最老的点）；越界返回 0 */
int cg_chart_point(const cg_widget_t *w, int series, int index)
{
    if (!w || !w->chart_pts || w->chart_cap <= 0) return 0;
    if (series < 0 || series >= w->chart_nseries) return 0;
    if (index < 0 || index >= w->chart_fill[series]) return 0;
    return w->chart_pts[(size_t)series * (size_t)w->chart_cap + (size_t)index];
}

/** @brief 设 Y 轴范围（超出的值按边界裁剪后画） */
void cg_chart_set_range(cg_widget_t *w, int min, int max)
{
    if (!w || !want_type(w, "chart", "cg_chart_set_range")) return;
    if (max <= min) max = min + 1;
    w->vmin = min;
    w->vmax = max;
    if (w->node) {
        w->node->vmin = min;
        w->node->vmax = max;
    }
    cg_widget_sync(w);
}

/* ------------------------------------------------------------ 状态灯（led）
 * 最小的一块"看得见状态"的控件：通信连上没有、串口在不在收、设备就绪没有。
 * 状态就存在普通的 value 里（0 灭 / 1 亮 / 2 闪），形状在 format、闪烁周期在 period、
 * 亮灯颜色在 style.fg —— **全是既有字段，没为它加新存储**。
 * 闪烁相位的算法放在这里而不是画的时候，是为了能脱窗单测：闪错了不崩，只是"看不出来在闪"。
 */

/** 闪烁周期的缺省值：period 太小（<100ms）兜到这里 —— 再快也看不出在闪，只会看着像暗的 */
#define CG_LED_DEF_PERIOD 500

/** @brief 状态夹到 0..2 */
static int led_state(const cg_widget_t *w)
{
    if (w->value < 0) return 0;
    if (w->value > 2) return 2;
    return w->value;
}

/** @brief 此刻灯亮着吗（状态 = 闪时按 period 前后半周期翻转） */
int cg_led_lit(const cg_widget_t *w, unsigned long now_ms)
{
    unsigned long p;
    int           s;

    if (!w) return 0;
    s = led_state(w);
    if (s != 2) return s == 1;                      /* 灭 = 不亮；亮 = 一直亮 */
    p = (w->period >= 100) ? (unsigned long)w->period : (unsigned long)CG_LED_DEF_PERIOD;
    return ((now_ms % p) < (p / 2)) ? 1 : 0;        /* 前半周期亮、后半周期灭 */
}

/** @brief 设状态（0 灭 / 1 亮 / 2 闪；越界夹取） */
void cg_led_set(cg_widget_t *w, int state)
{
    if (!w || !want_type(w, "led", "cg_led_set")) return;
    if (state < 0) state = 0;
    if (state > 2) state = 2;
    w->value = state;
    if (w->node) w->node->value = state;
    cg_widget_sync(w);
}

/** @brief 读状态（0 / 1 / 2） */
int cg_led_state(const cg_widget_t *w)
{
    return w ? led_state(w) : 0;
}

/** @brief 亮起的颜色（走既有的颜色通道：自绘控件都从 style.fg 取主色） */
void cg_led_set_color(cg_widget_t *w, unsigned rgb)
{
    if (!w || !want_type(w, "led", "cg_led_set_color")) return;
    cg_widget_set_colors(w, rgb, CG_COLOR_NONE);
}

/* ------------------------------------------------------------ 仪表盘（meter）
 * 看单个读数"现在处在量程的哪个位置"。value/min/max 就是滑块那套（超量程夹取），
 * 样式在 format、单位在 text、主色在 style.fg —— 同样没加新存储。
 * 角度算法放 core 是为了能脱窗单测：表盘画歪了，肉眼很难判断是数据的问题还是控件的问题。
 */

#define CG_METER_ARC_FROM   135      /* arc 样式：135° 起（左下）…… */
#define CG_METER_ARC_SWEEP  270      /* ……顺时针扫 270°（经正上方，到右下 45°） */
#define CG_METER_HALF_FROM  180      /* half 样式：180° 起（正左）…… */
#define CG_METER_HALF_SWEEP 180      /* ……顺时针扫 180°（到正右） */

/** @brief 值 -> 表盘角度（度，顺时针、以 +x 轴为 0°）；超量程按边界夹取 */
int cg_meter_angle_deg(const cg_widget_t *w, int value)
{
    int from, sweep, range;

    if (!w) return CG_METER_ARC_FROM;
    if (w->format && strcmp(w->format, "half") == 0) {
        from = CG_METER_HALF_FROM;
        sweep = CG_METER_HALF_SWEEP;
    } else {
        from = CG_METER_ARC_FROM;
        sweep = CG_METER_ARC_SWEEP;
    }
    range = w->vmax - w->vmin;
    if (range <= 0) return from;                    /* 量程非法：只给起点，不除零 */
    if (value < w->vmin) value = w->vmin;
    if (value > w->vmax) value = w->vmax;
    return from + (int)((long long)(value - w->vmin) * sweep / range);
}

/** @brief 设当前值（夹到量程；既有 set_value 就带夹取与同步） */
void cg_meter_set(cg_widget_t *w, int value)
{
    if (!w || !want_type(w, "meter", "cg_meter_set")) return;
    cg_widget_set_value(w, value);
}

/** @brief 设量程（max<=min 时纠正成 min+1，免得除零） */
void cg_meter_set_range(cg_widget_t *w, int min, int max)
{
    if (!w || !want_type(w, "meter", "cg_meter_set_range")) return;
    if (max <= min) max = min + 1;
    w->vmin = min;
    w->vmax = max;
    if (w->node) {
        w->node->vmin = min;
        w->node->vmax = max;
    }
    cg_widget_sync(w);
}
