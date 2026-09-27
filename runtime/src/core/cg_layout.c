/**
 * @file    cg_layout.c
 * @brief   余量分配（.cgui 的 grow）：把"容器尺寸变化"按权重分给兄弟控件
 * @details 要解决的问题：窗口拉伸时布局不重排 —— 绝对坐标是主体，只有 splitter 能在局部硬撑，
 *          于是 `[日志区 | 分隔条 | 发送区]` 这类布局把窗口拉高之后上部不动、下部留白。
 *
 *          两条设计硬约束（详见 .codebuddy/plans/grow-layout.md）：
 *          1. **不做完整布局引擎**：没有 sizePolicy / anchor / 两遍尺寸协商 —— 这里只回答
 *             "多出来的空间给谁"，绝对坐标仍是唯一的位置来源；
 *          2. **每次从基准重算，不做增量累加**：增量法在反复拉伸时会累积舍入误差
 *             （权重 (1,1) 的两块、每次差 1 像素时分两步与分一步的结果就不是同一个），
 *             而"从基准重算"是纯函数 —— 来回拉伸原地不动。
 *
 *          **按"带"分配**（这条是写测试时才想清楚的，见 §算法）：同一行/列里互相重叠的
 *          兄弟属于同一条带，它们**各自**填满这一带的余量（左右并排的两块都要变高，
 *          不是二选一）；带与带之间才按权重分。否则 `[终端 | 工具栏]` 这种左右并排的布局，
 *          窗口一变高就会把右边的工具栏"推下去"，看着像傻了。
 *
 *          放在 core 而不是 win32 是有意的：这里只有整数算术、不碰 Win32，
 *          于是它**能被纯单测覆盖**（tests/unit/test_layout.c），而不是只能靠"开窗口拉一拉看看"。
 */
#include "cg_internal.h"

#include <stdlib.h>

/** .cgui 的 grow 允许的上限（超过按上限算） */
#define CG_LAYOUT_MAX_WEIGHT 99

/** @brief box 在该轴上的位置字段（axis 0 = 横轴用 x，1 = 竖轴用 y） */
static int *box_pos(cg_layout_box_t *b, int axis) { return axis ? &b->y : &b->x; }

/** @brief box 在该轴上的尺寸字段（axis 0 = 横轴用 w，1 = 竖轴用 h） */
static int *box_size(cg_layout_box_t *b, int axis) { return axis ? &b->h : &b->w; }

/** @brief 取值并夹到 0..CG_LAYOUT_MAX_WEIGHT */
static int box_weight(const cg_layout_box_t *b)
{
    int g = b->grow;

    if (g < 0) g = 0;
    if (g > CG_LAYOUT_MAX_WEIGHT) g = CG_LAYOUT_MAX_WEIGHT;
    return g;
}

/**
 * @brief 在**一个轴**上分配余量（横轴与竖轴共用这份逻辑）
 * @param boxes box 数组（原地改）
 * @param count 个数
 * @param axis  0 = 横轴（x/w），1 = 竖轴（y/h）
 * @param delta 该轴上"容器变大了多少"（负数 = 缩小）；0 直接返回
 */
static void layout_axis(cg_layout_box_t *boxes, int count, int axis, int delta)
{
    int *idx;
    int  sum = 0, sum_left, given = 0, shift = 0;
    int  i, j, k;

    if (count <= 0 || delta == 0) return;
    for (i = 0; i < count; i++) sum += box_weight(&boxes[i]);
    if (sum == 0) return;                       /* 没人吃余量：这一轴整个不动 */

    /* 按该轴位置升序处理 —— 分配要沿轴累积（前面的吃了余量，后面的整体让位）。
     * 用索引数组排序，**不重排调用方的数组**：调用方按自己的下标读结果。
     * 插入排序是稳定的：位置相同的兄弟保持原来的先后，结果可预期。 */
    idx = (int *)malloc(sizeof(int) * (size_t)count);
    if (!idx) return;                           /* 分配失败：这一轴不重排，比崩了好 */
    for (i = 0; i < count; i++) idx[i] = i;
    for (i = 1; i < count; i++) {
        int key = idx[i];

        for (j = i - 1; j >= 0 && *box_pos(&boxes[idx[j]], axis) > *box_pos(&boxes[key], axis); j--) {
            idx[j + 1] = idx[j];
        }
        idx[j + 1] = key;
    }

    sum_left = sum;
    i = 0;
    while (i < count) {
        int band_end    = *box_pos(&boxes[idx[i]], axis) + *box_size(&boxes[idx[i]], axis);
        int band_weight = box_weight(&boxes[idx[i]]);
        int take        = 0;

        /* 1) 找出这一带：起点落在带尾之前的都属于同一条带（= 同一行/列） */
        for (j = i + 1; j < count; j++) {
            int p = *box_pos(&boxes[idx[j]], axis);
            int e = p + *box_size(&boxes[idx[j]], axis);

            if (p >= band_end) break;            /* 起点在带尾之后 = 下一条带 */
            if (e > band_end) band_end = e;
            band_weight += box_weight(&boxes[idx[j]]);
        }

        /* 2) 这一带吃多少：按"剩余权重"分，最后一条带把余数全吃了（一个像素都不许丢） */
        if (band_weight > 0 && sum_left > 0) {
            take = (band_weight >= sum_left) ? (delta - given)
                                             : (delta - given) * band_weight / sum_left;
        }

        /* 3) 带内成员先按"**更早的带**"累计的位移摆正 —— 必须在累加本带的 take **之前**做，
         *    否则同一条带的成员会被自己这一带的增长也推一下（并排的工具栏就是这样被推走的）。 */
        for (k = i; k < j; k++) *box_pos(&boxes[idx[k]], axis) += shift;

        /* 4) 带内每个 grow 成员都吃这一份 —— 它们在同一行/列里，各自要把这一带填满；
         *    这一带吃掉的空间由**后面的带**整体让出。 */
        if (take != 0) {
            int applied = take;                  /* 实际吃到的量（可能被"压到 0"夹过） */

            for (k = i; k < j; k++) {
                cg_layout_box_t *b = &boxes[idx[k]];
                int              newsz, d;

                if (box_weight(b) == 0) continue;
                newsz = *box_size(b, axis) + take;
                if (newsz < 0) newsz = 0;        /* 缩到比设计尺寸还小：归零，不出负数 */
                d = newsz - *box_size(b, axis);
                if (take < 0 ? (d > applied) : (d < applied)) applied = d;
                *box_size(b, axis) = newsz;
            }
            shift += applied;
            given += applied;
        }

        sum_left -= band_weight;
        i = j;
    }
    free(idx);
}

/** @brief 按 grow 把"容器尺寸变化"分配到各 box（详见 .codebuddy/plans/grow-layout.md §2） */
void cg_layout_apply(cg_layout_box_t *boxes, int count,
                     int base_w, int base_h, int now_w, int now_h)
{
    if (!boxes || count <= 0) return;
    layout_axis(boxes, count, 0, now_w - base_w);
    layout_axis(boxes, count, 1, now_h - base_h);
}
