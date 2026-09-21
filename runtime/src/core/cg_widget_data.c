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
