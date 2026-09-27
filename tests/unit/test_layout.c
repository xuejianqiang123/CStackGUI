/**
 * @file    test_layout.c
 * @brief   余量分配（core/cg_layout.c）：把"容器尺寸变化"按 grow 分给兄弟控件
 * @details 为什么值得单独一个套件：这是"窗口拉伸看着对不对"的**全部逻辑**，而且没有一行
 *          Win32 —— 所以能在没有窗口的情况下把边界全钉住。它错了不会崩，只会让界面在
 *          某些尺寸下变得没法看（用户不会来报 bug，只会觉得这软件怪）。
 *
 *          要钉的几条：
 *          1. 没人 grow 就一个像素都不动；
 *          2. **同一行/列（位置重叠）里各有各的余量** —— 左右并排的两块都设了 grow 要都变高，
 *             只设一块时另一块**不许被推走**（这是写测试时才发现的一个真错，见计划文件 §2）；
 *          3. 带与带之间按权重分，**余数不许丢**（Σ必须等于容器变化量）；
 *          4. 缩到比设计尺寸还小时压到 0，不出负数；
 *          5. **回到基准尺寸就完全回到设计坐标** —— "从基准重算、不做增量累加"这条设计约束的核心断言。
 *
 *          白盒：直接 include cg_internal.h 拿 cg_layout_box_t 与函数声明（不经过任何窗口）。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>

/** @brief 造一个 box（省得每处写五行） */
static cg_layout_box_t mk(int x, int y, int w, int h, int g)
{
    cg_layout_box_t b;

    b.x = x; b.y = y; b.w = w; b.h = h; b.grow = g;
    return b;
}

/** @brief 没人吃余量：一个像素都不许动 */
static void test_no_grow(void)
{
    cg_layout_box_t b[2];

    b[0] = mk(0, 0, 100, 50, 0);
    b[1] = mk(0, 50, 100, 50, 0);
    cg_layout_apply(b, 2, 100, 100, 300, 400);

    cg_test_int("没人 grow：第 1 块 x 不动", b[0].x, 0);
    cg_test_int("没人 grow：第 1 块 w 不动", b[0].w, 100);
    cg_test_int("没人 grow：第 1 块 h 不动", b[0].h, 50);
    cg_test_int("没人 grow：第 2 块 y 不动（不被推走）", b[1].y, 50);
    cg_test_int("没人 grow：第 2 块 h 不动", b[1].h, 50);

    cg_test_note("---- 非法参数不崩 ----");
    cg_layout_apply(NULL, 2, 100, 100, 200, 200);
    cg_layout_apply(b, 0, 100, 100, 200, 200);
    cg_test_int("NULL / 个数 0 调用后结构没被改", b[0].h, 50);

    cg_test_note("---- 权重非法：负数当 0，超过 99 夹到 99 ----");
    b[0] = mk(0, 0, 100, 40, -5);
    b[1] = mk(0, 40, 100, 60, 1);
    cg_layout_apply(b, 2, 100, 100, 100, 140);
    cg_test_int("负权重那块按 0 算（不变高）", b[0].h, 40);
    cg_test_int("正常那块吃满（+40）", b[1].h, 100);

    b[0] = mk(0, 0, 100, 40, 200);            /* 夹到 99 */
    b[1] = mk(0, 40, 100, 60, 1);
    cg_layout_apply(b, 2, 100, 100, 100, 200);
    cg_test_int("200 夹到 99：拿 100 里的 99", b[0].h, 139);
    cg_test_int("  另一个拿剩下的 1", b[1].h, 61);
}

/** @brief 竖向三段（`[日志区 | 分隔条 | 发送区]` 的形状）：只有一条带吃余量，后面整体下移 */
static void test_vertical_stack(void)
{
    cg_layout_box_t b[3];

    b[0] = mk(0, 0, 700, 300, 1);             /* 日志区：吃余量 */
    b[1] = mk(0, 300, 700, 6, 0);             /* 分隔条 */
    b[2] = mk(0, 306, 700, 200, 0);           /* 发送区 */

    cg_layout_apply(b, 3, 700, 506, 700, 706);   /* 窗口变高 200 */

    cg_test_int("日志区变高 200", b[0].h, 500);
    cg_test_int("分隔条被整体推下去", b[1].y, 500);
    cg_test_int("分隔条自己不变高", b[1].h, 6);
    cg_test_int("发送区跟着下移", b[2].y, 506);
    cg_test_int("发送区自己不变高", b[2].h, 200);
    cg_test_int("三段合计正好等于新高度", b[0].h + b[1].h + b[2].h, 706);

    cg_test_note("---- 横向没人 grow：宽度不动 ----");
    cg_test_int("横向：日志区 w 不变", b[0].w, 700);
    cg_test_int("横向：发送区 w 不变", b[2].w, 700);
}

/** @brief 同一条带（左右并排）：各自填满，谁也不推谁 —— 这是写测试时揪出来的真错 */
static void test_same_band(void)
{
    cg_layout_box_t b[2];

    cg_test_note("---- 并排两块，只有左边 grow：右边**不许被推走** ----");
    b[0] = mk(0, 0, 100, 50, 1);              /* 终端 */
    b[1] = mk(101, 0, 100, 50, 0);            /* 工具栏（同一行） */
    cg_layout_apply(b, 2, 201, 50, 201, 150);
    cg_test_int("左边变高 100", b[0].h, 150);
    cg_test_int("右边高度不变", b[1].h, 50);
    cg_test_int("右边 y 不动（旧规则会把它推到 100）", b[1].y, 0);

    cg_test_note("---- 并排两块都给 grow：两块都要填满这一带 ----");
    b[0] = mk(0, 0, 100, 50, 1);
    b[1] = mk(101, 0, 100, 50, 1);
    cg_layout_apply(b, 2, 201, 50, 201, 150);
    cg_test_int("左边变高", b[0].h, 150);
    cg_test_int("右边也变高（它不是「竞争」而是同一行）", b[1].h, 150);
    cg_test_int("右边 y 仍不动", b[1].y, 0);

    cg_test_note("---- 竖向两条带按权重分（1 : 3） ----");
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 3);
    cg_layout_apply(b, 2, 100, 100, 100, 140);   /* 变高 40 */
    cg_test_int("权重 1 的那条 +10", b[0].h, 50);
    cg_test_int("权重 3 的那条 +30", b[1].h, 90);
    cg_test_int("  且它被推下去 10", b[1].y, 50);
}

/** @brief 余数不许丢：三条带权重相同、变化量除不尽时，最后一个把余数全吃了 */
static void test_remainder(void)
{
    cg_layout_box_t b[3];

    b[0] = mk(0, 0, 100, 10, 1);
    b[1] = mk(0, 10, 100, 10, 1);
    b[2] = mk(0, 20, 100, 10, 1);
    cg_layout_apply(b, 3, 100, 30, 100, 40);     /* 变高 10：10/3 除不尽 */

    cg_test_int("第 1 条 +3", b[0].h, 13);
    cg_test_int("第 2 条 +3", b[1].h, 13);
    cg_test_int("第 3 条吃掉余数 +4", b[2].h, 14);
    cg_test_int("三条合计 = 原来的 30 + 10（一个像素都没丢）",
                b[0].h + b[1].h + b[2].h, 40);
    cg_test_int("第 2 条被推到 13", b[1].y, 13);
    cg_test_int("第 3 条被推到 26", b[2].y, 26);
}

/** @brief 横向生长：右边兄弟被推走但自己不变宽 */
static void test_horizontal(void)
{
    cg_layout_box_t b[2];

    b[0] = mk(0, 0, 100, 50, 1);              /* 左：吃宽度 */
    b[1] = mk(101, 0, 99, 50, 0);             /* 右：保持宽度 */
    cg_layout_apply(b, 2, 200, 50, 300, 50);  /* 加宽 100 */

    cg_test_int("左边加宽 100", b[0].w, 200);
    cg_test_int("右边宽度不变", b[1].w, 99);
    cg_test_int("右边被推到 201", b[1].x, 201);
    cg_test_int("两块合计铺满容器", b[0].w + b[1].w + 1, 300);

    cg_test_note("---- 两条性质：平移保持带间距；本来贴边的末带正好落到新边界 ----");
    b[0] = mk(0, 0, 100, 50, 1);              /* 带 1：吃余量 */
    b[1] = mk(190, 0, 100, 50, 0);            /* 带 2：与带 1 间距 90 */
    b[2] = mk(390, 0, 110, 50, 0);            /* 带 3：与带 2 间距 100，右端 500 = 容器右边界 */
    cg_layout_apply(b, 3, 500, 50, 590, 50);   /* 加宽 90 */
    cg_test_int("带 1 吃满 90", b[0].w, 190);
    cg_test_int("带 2 整体右移 90", b[1].x, 280);
    cg_test_int("  -> 与带 1 的间距没变（仍 90）", b[1].x - (b[0].x + b[0].w), 90);
    cg_test_int("带 3 也整体右移 90", b[2].x, 480);
    cg_test_int("  -> 与带 2 的间距没变（仍 100）", b[2].x - (b[1].x + b[1].w), 100);
    cg_test_int("本来贴边的末带右端正好落在新边界 590", b[2].x + b[2].w, 590);

    cg_test_note("---- 横竖同时变：两个轴互相独立 ----");
    b[0] = mk(0, 0, 50, 50, 1);
    b[1] = mk(51, 0, 49, 50, 1);
    cg_layout_apply(b, 2, 100, 100, 200, 200);
    cg_test_int("横向：两块各按权重加宽", b[0].w, 100);
    cg_test_int("横向：右边被推开", b[1].x, 101);
    cg_test_int("竖向：同一行两块都变高", b[0].h, 150);
    cg_test_int("竖向：右边也变高", b[1].h, 150);
}

/** @brief 缩小、以及"回到设计尺寸就完全回到设计坐标" */
static void test_shrink_and_reset(void)
{
    cg_layout_box_t b[2];
    int             x0, y0, w0, h0;

    cg_test_note("---- 缩到比设计尺寸还小：压到 0，不出负数 ----");
    b[0] = mk(0, 0, 100, 80, 1);
    b[1] = mk(0, 80, 100, 20, 0);
    cg_layout_apply(b, 2, 100, 100, 100, 10);    /* 缩 90，比 grow 那块的高度还多 */
    cg_test_int("grow 那块被压到 0（不是负数）", b[0].h, 0);
    cg_test_int("上块归零后，下块整体上移被回收掉的 80",
                b[1].y, 0);
    cg_test_int("下块自己高度不变（它没设 grow）", b[1].h, 20);

    cg_test_note("---- 来回拉伸：每次从基准重算，回到设计尺寸就一个像素都不差 ----");
    /* 运行时每次都是从**基准**算的（cg_win_layout.c 把 base_* 装进 boxes），
     * 所以这里也必须每次都摆回基准 —— 摆回基准这件事本身由后端负责，不是本函数的职责。 */
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 0);
    x0 = b[0].x; y0 = b[1].y; w0 = b[0].w; h0 = b[1].h;
    cg_layout_apply(b, 2, 100, 100, 100, 260);   /* 拉高 */
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 0);
    cg_layout_apply(b, 2, 100, 100, 100, 173);   /* 再拉到一个怪尺寸 */
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 0);
    cg_layout_apply(b, 2, 100, 100, 100, 100);   /* 回设计尺寸 */
    cg_test_int("上块回到设计高", b[0].h, 40);
    cg_test_int("下块回到设计 y", b[1].y, y0);
    cg_test_int("上块 w 也没漂", b[0].w, w0);
    cg_test_int("下块 h 也没漂", b[1].h, h0);
    cg_test_int("上块 x 也没漂", b[0].x, x0);

    cg_test_note("---- 同一个输入算两遍结果必须一样（纯函数） ----");
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 0);
    cg_layout_apply(b, 2, 100, 100, 133, 177);
    y0 = b[1].y;
    b[0] = mk(0, 0, 100, 40, 1);
    b[1] = mk(0, 40, 100, 60, 0);
    cg_layout_apply(b, 2, 100, 100, 133, 177);
    cg_test_int("第二遍与第一遍一致", b[1].y, y0);
}

CG_TEST_SUITE("kit：余量分配（grow）基础规则", test_no_grow);
CG_TEST_SUITE("kit：余量分配（grow）竖向堆叠", test_vertical_stack);
CG_TEST_SUITE("kit：余量分配（grow）同一带与权重", test_same_band);
CG_TEST_SUITE("kit：余量分配（grow）余数与横向", test_remainder);
CG_TEST_SUITE("kit：余量分配（grow）缩小与回到基准", test_shrink_and_reset);
