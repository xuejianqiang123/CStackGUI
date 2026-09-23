/**
 * @file    cg_widget_tabs.c
 * @brief   标签页控件 API：页数、切页（子控件按 page 自动显隐）、当前页
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <string.h>

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
