/**
 * @file    cg_widget_split.c
 * @brief   分隔条控件 API：拖动时重排两侧面板的纯几何运算，以及读/设位置
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <string.h>

/* ------------------------------------------------------------ 分隔条（splitter）
 * 语义一句话：拖我 = 撑开我**前面那一块**，我之后的兄弟整体让位，同一容器里**最后一块**
 * 吃掉余量（它反过来缩水）。于是 2 段布局就是"上/左变大、下/右变小"，多段布局也自洽：
 *      [pane1][splitter][pane2][splitter2][pane3]
 * 每根分隔条只认"前一块 + 尾巴"，不需要知道中间有几块，也就**不需要布局引擎**。
 * 没有矩形的东西（菜单栏、时钟）不参与。
 */

/** 分隔条的最小边长：拖到这个尺寸就停住，免得把面板拖没了 */
#define CG_SPLIT_MIN 16

/**
 * @brief 这个兄弟参与分隔条布局吗（菜单栏 / 状态栏 / 时钟没有可拖的矩形，跳过）
 * @param s 待判断的兄弟控件，可为 NULL
 * @return int 参与返回 1，跳过返回 0
 * @note 原注释只列了菜单栏 / 时钟，按实现（同时排除状态栏）补上状态栏
 */
static int split_layout_sibling(const cg_widget_t *s)
{
    return s && strcmp(s->type, "menu") != 0 && strcmp(s->type, "timer") != 0
           && strcmp(s->type, "statusbar") != 0;
}

/**
 * @brief 分隔条是不是竖的：左右分栏 = 竖条（format 缺省）；format == "row" 才是横条
 * @param w 分隔条控件
 * @return int 竖条返回 1，横条返回 0
 */
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

/**
 * @brief 分隔条当前位置（相对父容器：竖条看 x，横条看 y）
 * @param w 分隔条控件，可为 NULL
 * @return int 相对父容器的位置（像素）；w 为空返回 -1
 */
int cg_splitter_pos(const cg_widget_t *w)
{
    if (!w) return -1;
    return split_is_vert(w) ? w->x : w->y;
}

/**
 * @brief 把分隔条搬到 pos（相对父容器），两侧面板跟着重排（内部会夹到最小尺寸）
 * @param w 分隔条控件；类型不符直接忽略
 * @param pos 目标位置，相对父容器，单位像素
 * @return void 无返回值
 */
void cg_splitter_set(cg_widget_t *w, int pos)
{
    int delta;

    if (!w || !want_type(w, "splitter", "cg_splitter_set")) return;
    delta = pos - (split_is_vert(w) ? w->x : w->y);
    if (delta) cg_splitter_apply(w, delta);
}
