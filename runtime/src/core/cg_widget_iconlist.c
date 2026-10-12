/**
 * @file    cg_widget_iconlist.c
 * @brief   图标列表（iconlist）的数据 API：行的增删改查与勾选态
 * @details 行数据（图标路径 / 主标题 / 副标题 / 右侧状态 / 勾选）是本控件的唯一真相，
 *          绘制在 cg_win_draw_iconlist.c。.cgui 的 items 只在构建时转成第一版行
 *          （见 cg_widget.c 的 build_node），之后一律走这里的 API。
 *          每次变更置 items_dirty + cg_widget_sync：自绘控件不重建原生条目，
 *          sync 的通用尾会清脏并 InvalidateRect，重绘时读内存数据。
 */
#include "cg_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 内部小工具 */

/**
 * @brief 行容量不够就扩容（容量从 0 起按 +8 递增，避免一行一次 realloc）
 * @param w iconlist 控件
 * @return int 1 = 容量够（或扩成功），0 = 内存不足
 */
static int il_ensure_cap(cg_widget_t *w)
{
    cg_ilrow_t *grown;
    int         want;

    if (w->il_n < w->il_cap) return 1;
    want = w->il_cap ? w->il_cap + 8 : 8;
    grown = (cg_ilrow_t *)realloc(w->il_rows, (size_t)want * sizeof(cg_ilrow_t));
    if (!grown) return 0;
    memset(grown + w->il_cap, 0, (size_t)(want - w->il_cap) * sizeof(cg_ilrow_t));
    w->il_rows = grown;
    w->il_cap  = want;
    return 1;
}

/**
 * @brief 拷一个可空字符串进字段（NULL / 空串都存 NULL，绘制侧判 NULL 即跳过）
 * @param s 源串；可为 NULL
 * @return char* 堆串或 NULL，调用方持有
 */
static char *il_dup_or_null(const char *s)
{
    return (s && *s) ? cg_str_dup(s) : NULL;
}

/**
 * @brief 换掉行的一个字段（释放旧的、拷贝新的；NULL/空串统一成 NULL）
 * @param slot 目标字段地址
 * @param s 新值
 * @return void 无返回值
 */
static void il_set_field(char **slot, const char *s)
{
    free(*slot);
    *slot = il_dup_or_null(s);
}

/**
 * @brief 行下标在不在有效范围
 * @param w iconlist 控件
 * @param row 行号
 * @return int 1 = 合法
 */
static int il_row_ok(const cg_widget_t *w, int row)
{
    return w && w->il_rows && row >= 0 && row < w->il_n;
}

/* ------------------------------------------------------------ 对外 API */

/**
 * @brief 往图标列表末尾加一行
 * @param w iconlist 控件；类型不符返回 -1
 * @param icon 图标路径（可 NULL / 空 = 无图标，绘制时画占位框）
 * @param title 主标题（可 NULL / 空）
 * @param sub 副标题（可 NULL / 空）
 * @param status 右侧状态文字（可 NULL / 空）
 * @return int 新行号；内存不足或类型不符返回 -1
 */
int cg_iconlist_add(cg_widget_t *w, const char *icon, const char *title,
                    const char *sub, const char *status)
{
    cg_ilrow_t *row;

    if (!w || !want_type(w, "iconlist", "cg_iconlist_add")) return -1;
    if (!il_ensure_cap(w)) {
        fprintf(stderr, "[cstackgui] cg_iconlist_add: 内存不足\n");
        return -1;
    }
    row = &w->il_rows[w->il_n];
    memset(row, 0, sizeof(*row));
    row->icon   = il_dup_or_null(icon);
    row->title  = il_dup_or_null(title);
    row->sub    = il_dup_or_null(sub);
    row->status = il_dup_or_null(status);
    w->il_n++;
    w->items_dirty = 1;
    cg_widget_sync(w);
    return w->il_n - 1;
}

/**
 * @brief 清空全部行（行数归 0，滚动与悬停状态一并复位）
 * @param w iconlist 控件；类型不符忽略
 * @return void 无返回值
 */
void cg_iconlist_clear(cg_widget_t *w)
{
    int i;

    if (!w || !want_type(w, "iconlist", "cg_iconlist_clear")) return;
    for (i = 0; i < w->il_n; i++) {
        cgw_iconlist_row_free(&w->il_rows[i]);   /* 后端位图（core 不碰 GDI 句柄） */
        free(w->il_rows[i].icon);
        free(w->il_rows[i].title);
        free(w->il_rows[i].sub);
        free(w->il_rows[i].status);
    }
    w->il_n      = 0;
    w->il_scroll = 0;
    w->il_hover  = -1;
    w->value     = 0;      /* 行都没了，选中号不许停在已删除的行上 */
    w->items_dirty = 1;
    cg_widget_sync(w);
}

/**
 * @brief 当前有多少行
 * @param w iconlist 控件；NULL 或类型不符返回 0
 * @return int 行数
 */
int cg_iconlist_count(const cg_widget_t *w)
{
    if (!w || !w->type || strcmp(w->type, "iconlist") != 0) return 0;
    return w->il_n;
}

/**
 * @brief 改一行的勾选态
 * @param w iconlist 控件
 * @param row 行号（越界忽略，不报错 —— 高频调用，越界当"没点着"）
 * @param checked 1 = 勾选，0 = 取消
 * @return void 无返回值
 */
void cg_iconlist_set_checked(cg_widget_t *w, int row, int checked)
{
    if (!il_row_ok(w, row)) return;
    checked = checked ? 1 : 0;
    if (w->il_rows[row].checked == checked) return;
    w->il_rows[row].checked = checked;
    w->items_dirty = 1;
    cg_widget_sync(w);
}

/**
 * @brief 读一行的勾选态
 * @param w iconlist 控件
 * @param row 行号
 * @return int 1 = 已勾选；行无效或没勾返回 0
 */
int cg_iconlist_checked(const cg_widget_t *w, int row)
{
    if (!il_row_ok(w, row)) return 0;
    return w->il_rows[row].checked ? 1 : 0;
}

/**
 * @brief 改写一行的内容（图标 / 主标题 / 副标题 / 状态），勾选态保持不变
 * @param w iconlist 控件
 * @param row 行号（越界忽略）
 * @param icon 新图标路径；NULL / 空 = 清成无图标
 * @param title 新主标题；NULL / 空 = 清空
 * @param sub 新副标题；NULL / 空 = 清空
 * @param status 新状态文字；NULL / 空 = 清空
 * @return void 无返回值
 * @note 图标路径变了之后，旧位图缓存在下一次绘制时按 bmp_src 比对自动释放重载
 *       （与 image 控件的 sync_bitmap 同一口径），这里不用碰 GDI 句柄。
 */
void cg_iconlist_set_row(cg_widget_t *w, int row, const char *icon, const char *title,
                         const char *sub, const char *status)
{
    if (!il_row_ok(w, row)) return;
    il_set_field(&w->il_rows[row].icon,   icon);
    il_set_field(&w->il_rows[row].title,  title);
    il_set_field(&w->il_rows[row].sub,    sub);
    il_set_field(&w->il_rows[row].status, status);
    w->items_dirty = 1;
    cg_widget_sync(w);
}
