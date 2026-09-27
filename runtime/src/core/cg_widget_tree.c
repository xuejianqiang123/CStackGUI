/**
 * @file    cg_widget_tree.c
 * @brief   树控件 API：加节点/清空/计数/取文字/选中态/展开折叠，兼放控件的类型守卫 helper
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 *          本文件同时定义 want_type()（原在树段落里），它现在被多个控件 API 文件共用，
 *          声明已提升进 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 树 */

/**
 * @brief 校验控件类型，不匹配时明确报错而不是静默无效
 * @param w 待校验的控件，可为 NULL
 * @param type 期望的控件类型名
 * @param who 调用者名字，仅用于报错
 * @return int 类型匹配返回 1；否则打印错误并返回 0
 * @note 类型检查：这些 API 只对特定控件有意义，用错了要明确报出来而不是静默无效
 */
int want_type(const cg_widget_t *w, const char *type, const char *who)
{
    if (w && w->type && strcmp(w->type, type) == 0) return 1;
    fprintf(stderr, "[cstackgui] %s: 需要 %s 控件，实际给的是 %s\n",
            who, type, (w && w->type) ? w->type : "(null)");
    return 0;
}

/**
 * @brief 给树加一个节点（运行时追加，父下标必须已存在）
 * @param w tree 控件；类型不符直接忽略
 * @param parent 父节点下标，-1 表示加在根上
 * @param text 节点文字，内部复制
 * @return int 新节点下标；失败返回 -1
 */
int cg_tree_add(cg_widget_t *w, int parent, const char *text)
{
    if (!w || !want_type(w, "tree", "cg_tree_add")) return -1;
    if (parent < -1 || parent >= w->ntree) {
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

/**
 * @brief cg_tree_add 的 printf 版本
 * @param w tree 控件；类型不符直接忽略
 * @param parent 父节点下标，-1 表示加在根上
 * @param fmt 节点文字的格式串
 * @return int 新节点下标；失败返回 -1
 */
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

/**
 * @brief 清空树的全部节点（整树重建实现，选中态复位）
 * @param w tree 控件；类型不符直接忽略
 * @return void 无返回值
 */
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

/**
 * @brief 树节点总数；非 tree 控件返回 0
 * @param w 控件，可为 NULL
 * @return int 节点总数；非 tree 控件或 w 为空返回 0
 */
int cg_tree_count(const cg_widget_t *w)
{
    return (w && w->type && strcmp(w->type, "tree") == 0) ? w->ntree : 0;
}

/**
 * @brief 取第 item 个节点的文字；越界返回 NULL
 * @param w tree 控件，可为 NULL
 * @param item 节点下标，从 0 开始
 * @return const char * 节点文字，归控件所有，调用方不要释放；越界返回 NULL
 */
const char *cg_tree_item_text(const cg_widget_t *w, int item)
{
    if (!w || item < 0 || item >= w->ntree) return NULL;
    return w->tree[item].text;
}

/**
 * @brief 当前选中节点下标；没选中返回 -1
 * @param w tree 控件，可为 NULL
 * @return int 选中节点下标；没选中或 w 为空返回 -1
 */
int cg_tree_selected(const cg_widget_t *w)
{
    return w ? w->tree_sel : -1;
}

/**
 * @brief 当前选中节点的文字；没选中返回 NULL
 * @param w tree 控件，可为 NULL
 * @return const char * 选中节点文字，归控件所有，调用方不要释放；没选中或 w 为空返回 NULL
 */
const char *cg_tree_selected_text(const cg_widget_t *w)
{
    if (!w || w->tree_sel < 0 || w->tree_sel >= w->ntree) return NULL;
    return w->tree[w->tree_sel].text;
}

/**
 * @brief 展开/折叠某个节点（整树重建实现，够用且简单）
 * @param w tree 控件，可为 NULL
 * @param item 节点下标，越界直接忽略
 * @param expand 非 0 展开、0 折叠
 * @return void 无返回值
 */
void cg_tree_expand(cg_widget_t *w, int item, int expand)
{
    if (!w || item < 0 || item >= w->ntree) return;
    w->tree[item].expanded = expand != 0;
    w->items_dirty = 1;     /* v1 用整树重建来实现展开/折叠，够用且简单 */
    cg_widget_sync(w);
}
