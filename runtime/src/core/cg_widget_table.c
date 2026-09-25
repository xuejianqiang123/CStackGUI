/**
 * @file    cg_widget_table.c
 * @brief   表格控件 API：行列增长/追加列/追加行/清空/行数/取单元格/选中行
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 表格 */

/** @brief 表格行数变多时把行级附加数组（勾选/数值/图标）一起补长到 want */
void cg_table_rows_grow(cg_widget_t *w, int want)
{
    if (want > w->row_cap) {
        unsigned char *nc = (unsigned char *)realloc(w->row_checked, (size_t)want);
        int *nv = (int *)realloc(w->row_val, (size_t)want * sizeof(int));
        if (!nc || !nv) return;   /* 任一个没长成：保持原状，不抬 row_cap，避免越界写 */
        memset(nc + w->row_cap, 0, (size_t)(want - w->row_cap));
        w->row_checked = nc;
        memset(nv + w->row_cap, 0, (size_t)(want - w->row_cap) * sizeof(int));
        w->row_val = nv;
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
    int      old_ncols, new_ncols, r;

    if (!w || !want_type(w, "table", "cg_table_add_column")) return -1;
    old_ncols = w->ncols;
    new_ncols = old_ncols + 1;
    nc = (cg_col_t *)realloc(w->cols, (size_t)new_ncols * sizeof(cg_col_t));
    if (!nc) return -1;
    w->cols = nc;
    w->cols[old_ncols].title = cg_str_dup(title ? title : "");
    w->cols[old_ncols].width = width;

    /* 已有行的单元格数组都要扩到新列数：否则之后按新列数释放/访问老行会越界 */
    for (r = 0; r < w->nrows; r++) {
        char **nr = (char **)realloc(w->rows[r], (size_t)new_ncols * sizeof(char *));
        if (!nr) return -1;            /* OOM：列已记录但行未全扩，row_cells 不变故仍安全 */
        w->rows[r] = nr;
        for (int c = old_ncols; c < new_ncols; c++) nr[c] = cg_str_dup("");
    }
    w->ncols = new_ncols;
    w->row_cells = new_ncols;
    w->items_dirty = 1;
    cg_widget_sync(w);
    return old_ncols;
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
