/**
 * @file    test_splitter.c
 * @brief   分隔条几何的单元测试（cg_widget_split.c）
 * @details 为什么盯它：分隔条那套"撑开前一块、后面让位、最后一块吃掉余量"是纯 rect 运算，
 *          但**错了不会崩**——只是面板慢慢错位、留缝或多出滚动条，肉眼根本看不出是哪一步错的。
 *          而且它有个容易忘的不变式：**整排控件的总尺寸守恒**（谁变大谁就让位），
 *          这里把"拖完仍然严丝合缝"钉死在每一条断言上。
 *          白盒：直接拿控件结构算，不需要开窗（后端的鼠标部分靠真机截图验）。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 三段布局 + 两根分隔条（左右分栏，总宽 600）：
 *   p1[0,200)  sp1[200,206)  p2[206,400)  sp2[400,406)  p3[406,600)
 */
static const char *k_row_doc =
    "{\n"
    "  \"app\": { \"title\": \"splitter\", \"width\": 600, \"height\": 400 },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"p1\",  \"type\": \"box\",      \"rect\": { \"x\": 0,   \"y\": 0, \"w\": 200, \"h\": 400 } },\n"
    "    { \"id\": \"sp1\", \"type\": \"splitter\", \"rect\": { \"x\": 200, \"y\": 0, \"w\": 6,   \"h\": 400 } },\n"
    "    { \"id\": \"p2\",  \"type\": \"box\",      \"rect\": { \"x\": 206, \"y\": 0, \"w\": 194, \"h\": 400 } },\n"
    "    { \"id\": \"sp2\", \"type\": \"splitter\", \"rect\": { \"x\": 400, \"y\": 0, \"w\": 6,   \"h\": 400 } },\n"
    "    { \"id\": \"p3\",  \"type\": \"box\",      \"rect\": { \"x\": 406, \"y\": 0, \"w\": 194, \"h\": 400 } }\n"
    "  ]\n"
    "}\n";

/* 上下分行：p_top[0,100) sp[100,106) p_bot[106,400)，format = row 才是横条 */
static const char *k_col_doc =
    "{\n"
    "  \"app\": { \"title\": \"splitter row\", \"width\": 600, \"height\": 400 },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"p_top\", \"type\": \"box\",      \"rect\": { \"x\": 0, \"y\": 0,   \"w\": 600, \"h\": 100 } },\n"
    "    { \"id\": \"sp\",    \"type\": \"splitter\", \"format\": \"row\",\n"
    "                                             \"rect\": { \"x\": 0, \"y\": 100, \"w\": 600, \"h\": 6 } },\n"
    "    { \"id\": \"p_bot\", \"type\": \"box\",      \"rect\": { \"x\": 0, \"y\": 106, \"w\": 600, \"h\": 294 } }\n"
    "  ]\n"
    "}\n";

/** @brief 解析文档 + 建控件树；**成功返回 1、失败返回 0**（调用点都写成 if (build_app(...))） */
static int build_app(cg_app_t *app, const char *json)
{
    char      err[256] = { 0 };
    cg_doc_t *doc = cg_doc_parse(json, err, sizeof(err));

    if (!doc) {
        printf("        解析失败：%s\n", err);
        return 0;
    }
    memset(app, 0, sizeof(*app));
    app->scale = 1;
    app->doc = *doc;
    free(doc);
    cg_build_widgets(app);
    return 1;
}

/** @brief 收尾：先放控件（它们会回写节点），再放文档 */
static void free_app(cg_app_t *app)
{
    cg_widget_t *w = app->widgets;
    while (w) {
        cg_widget_t *next = w->next;
        cg_widget_free(w);
        w = next;
    }
    app->widgets = NULL;
    cg_doc_release(&app->doc);
}

/** @brief p3 的右边界：总尺寸守恒时它必须一直贴在 600 */
static long long right_edge(cg_app_t *app)
{
    cg_widget_t *p3 = cg_find(app, "p3");
    return p3 ? (long long)(p3->x + p3->w) : -1;
}

/** @brief 基本拖动：撑开前一块、中间只平移、最后一块吃掉余量 */
static void test_splitter_drag(void)
{
    cg_app_t     app;
    cg_widget_t *p1, *sp1, *p2, *sp2, *p3;

    cg_test_note("---- 左右分栏：拖 50 / 拖回来 ----");
    if (!build_app(&app, k_row_doc)) { cg_test_ok("建应用成功", 0); return; }
    p1 = cg_find(&app, "p1"); sp1 = cg_find(&app, "sp1");
    p2 = cg_find(&app, "p2"); sp2 = cg_find(&app, "sp2");
    p3 = cg_find(&app, "p3");
    cg_test_ok("五个控件都在", p1 && sp1 && p2 && sp2 && p3);
    if (!p1 || !sp1 || !p2 || !sp2 || !p3) { free_app(&app); return; }

    cg_test_int("初始位置就是它的 x", cg_splitter_pos(sp1), 200);

    cg_test_int("右拖 50 的返回值", cg_splitter_apply(sp1, 50), 50);
    cg_test_int("前一块 200 -> 250", p1->w, 250);
    cg_test_int("分隔条跟到 250", sp1->x, 250);
    cg_test_int("中间那块只平移", p2->x, 256);
    cg_test_int("中间那块宽度不变", p2->w, 194);
    cg_test_int("第二根分隔条也平移", sp2->x, 450);
    cg_test_int("最后一块平移", p3->x, 456);
    cg_test_int("最后一块缩水 194 -> 144", p3->w, 144);
    cg_test_int("右边界仍贴着 600（尺寸守恒）", right_edge(&app), 600);

    cg_test_int("左拖 100 的返回值", cg_splitter_apply(sp1, -100), -100);
    cg_test_int("前一块 250 -> 150", p1->w, 150);
    cg_test_int("分隔条跟到 150", sp1->x, 150);
    cg_test_int("最后一块把余量还回去 144 -> 244", p3->w, 244);
    cg_test_int("右边界仍是 600", right_edge(&app), 600);

    free_app(&app);
}

/** @brief 只影响相邻两块 + 夹到最小尺寸（每段都从干净的布局开始，避免互相污染期望值） */
static void test_splitter_clamp(void)
{
    cg_app_t     app;
    cg_widget_t *p1, *sp1, *p2, *sp2, *p3;

    cg_test_note("---- 拖第二根：第一块不受影响 ----");
    if (!build_app(&app, k_row_doc)) return;
    p1 = cg_find(&app, "p1"); sp1 = cg_find(&app, "sp1");
    p2 = cg_find(&app, "p2"); sp2 = cg_find(&app, "sp2");
    p3 = cg_find(&app, "p3");
    if (!p1 || !sp1 || !p2 || !sp2 || !p3) { free_app(&app); return; }

    cg_test_int("拖第二根 20", cg_splitter_apply(sp2, 20), 20);
    cg_test_int("第一块宽度没被碰过", p1->w, 200);
    cg_test_int("第二根前面那块变大 194 -> 214", p2->w, 214);
    cg_test_int("第二根自己右移", sp2->x, 420);
    cg_test_int("最后一块缩水 194 -> 174", p3->w, 174);
    cg_test_int("右边界守恒", right_edge(&app), 600);
    free_app(&app);

    cg_test_note("---- 夹到最小 16px：两侧都停得住 ----");
    if (!build_app(&app, k_row_doc)) return;
    p1 = cg_find(&app, "p1"); sp1 = cg_find(&app, "sp1");
    p3 = cg_find(&app, "p3");
    if (!p1 || !sp1 || !p3) { free_app(&app); return; }

    /* 前一块 200，最小 16 → 最多往回 184 */
    cg_test_int("往前猛拖被夹在 -184", cg_splitter_apply(sp1, -10000), -184);
    cg_test_int("前一块停在 16", p1->w, 16);
    cg_test_int("分隔条贴住它", sp1->x, 16);
    cg_test_int("最后一块撑到 378", p3->w, 378);
    cg_test_int("右边界还是 600", right_edge(&app), 600);

    /* 现在最后一块 378，最小 16 → 最多往后 362 */
    cg_test_int("往后猛拖被夹在 362", cg_splitter_apply(sp1, 10000), 362);
    cg_test_int("前一块吃到 378", p1->w, 378);
    cg_test_int("最后一块停在 16", p3->w, 16);
    cg_test_int("右边界依然是 600", right_edge(&app), 600);

    cg_test_note("---- cg_splitter_set：按绝对位置摆（越界也给夹住） ----");
    cg_splitter_set(sp1, 300);
    cg_test_int("位置变成 300", cg_splitter_pos(sp1), 300);
    cg_test_int("set 之后右边界仍守恒", right_edge(&app), 600);
    cg_splitter_set(sp1, -100000);
    cg_test_int("越界位置被夹住（前一块 = 16）", p1->w, 16);
    cg_test_int("夹住后仍然守恒", right_edge(&app), 600);
    free_app(&app);
}

/** @brief 上下分行（format = row）：同一套逻辑换个轴 */
static void test_splitter_rows(void)
{
    cg_app_t     app;
    cg_widget_t *top, *sp, *bot;

    cg_test_note("---- 上下分行：format = row ----");
    if (!build_app(&app, k_col_doc)) return;
    top = cg_find(&app, "p_top"); sp = cg_find(&app, "sp"); bot = cg_find(&app, "p_bot");
    cg_test_ok("三个控件都在", top && sp && bot);
    if (!top || !sp || !bot) { free_app(&app); return; }

    cg_test_int("横条看 y", cg_splitter_pos(sp), 100);
    cg_test_int("下拖 50", cg_splitter_apply(sp, 50), 50);
    cg_test_int("上面那块变高 100 -> 150", top->h, 150);
    cg_test_int("横条跟到 150", sp->y, 150);
    cg_test_int("下面那块下移", bot->y, 156);
    cg_test_int("下面那块变矮 294 -> 244", bot->h, 244);
    cg_test_int("底边仍贴 400（守恒）", bot->y + bot->h, 400);
    cg_test_int("宽度没被碰过", bot->w, 600);
    free_app(&app);
}

/** @brief 结构不允许时的兜底：不该崩、也不该乱动 */
static void test_splitter_refuse(void)
{
    cg_app_t     app;
    cg_widget_t *sp;

    cg_test_note("---- 分隔条在最前 / 最后：拒绝并保持不动 ----");
    if (build_app(&app, "{\"widgets\":["
                       "{\"id\":\"sp\",\"type\":\"splitter\",\"rect\":{\"x\":0,\"y\":0,\"w\":6,\"h\":100}},"
                       "{\"id\":\"p\",\"type\":\"box\",\"rect\":{\"x\":6,\"y\":0,\"w\":100,\"h\":100}}]}")) {
        sp = cg_find(&app, "sp");
        cg_test_int("前面没有块时不动", cg_splitter_apply(sp, 50), 0);
        free_app(&app);
    }

    if (build_app(&app, "{\"widgets\":["
                       "{\"id\":\"p\",\"type\":\"box\",\"rect\":{\"x\":0,\"y\":0,\"w\":100,\"h\":100}},"
                       "{\"id\":\"sp\",\"type\":\"splitter\",\"rect\":{\"x\":100,\"y\":0,\"w\":6,\"h\":100}}]}")) {
        sp = cg_find(&app, "sp");
        cg_test_int("后面没有块时不动（没人吃余量）", cg_splitter_apply(sp, 50), 0);
        free_app(&app);
    }

    cg_test_note("---- 两段布局能拖 ----");
    if (build_app(&app, "{\"widgets\":["
                       "{\"id\":\"p1\",\"type\":\"box\",\"rect\":{\"x\":0,\"y\":0,\"w\":100,\"h\":100}},"
                       "{\"id\":\"sp\",\"type\":\"splitter\",\"rect\":{\"x\":100,\"y\":0,\"w\":6,\"h\":100}},"
                       "{\"id\":\"p2\",\"type\":\"box\",\"rect\":{\"x\":106,\"y\":0,\"w\":100,\"h\":100}}]}")) {
        sp = cg_find(&app, "sp");
        cg_test_int("拖 30", cg_splitter_apply(sp, 30), 30);
        cg_test_int("后一块缩水 100 -> 70", cg_find(&app, "p2")->w, 70);
        free_app(&app);
    }

    cg_test_note("---- 中间夹着没有矩形的控件（时钟）：跳过它 ----");
    if (build_app(&app, "{\"widgets\":["
                       "{\"id\":\"p1\",\"type\":\"box\",\"rect\":{\"x\":0,\"y\":0,\"w\":100,\"h\":100}},"
                       "{\"id\":\"sp\",\"type\":\"splitter\",\"rect\":{\"x\":100,\"y\":0,\"w\":6,\"h\":100}},"
                       "{\"id\":\"tm\",\"type\":\"timer\",\"period\":1000},"
                       "{\"id\":\"p2\",\"type\":\"box\",\"rect\":{\"x\":106,\"y\":0,\"w\":100,\"h\":100}}]}")) {
        sp = cg_find(&app, "sp");
        cg_test_int("跳过时钟照常拖", cg_splitter_apply(sp, 20), 20);
        cg_test_int("吃的还是最后那块 p2", cg_find(&app, "p2")->w, 80);
        free_app(&app);
    }

    cg_test_note("---- 参数兜底 ----");
    cg_test_int("NULL 控件返回 0", cg_splitter_apply(NULL, 10), 0);
    cg_test_int("NULL 控件的 cg_splitter_pos 返回 -1", cg_splitter_pos(NULL), -1);
    cg_splitter_set(NULL, 100);
    cg_test_ok("cg_splitter_set(NULL) 不崩", 1);

    if (build_app(&app, k_row_doc)) {
        cg_widget_t *p1 = cg_find(&app, "p1");
        sp = cg_find(&app, "sp1");
        cg_test_int("位移 0 时什么也不做", cg_splitter_apply(sp, 0), 0);
        cg_test_int("非分隔条也能读位置（只当它是一根线）", cg_splitter_pos(p1), 0);
        cg_splitter_set(p1, 999);        /* 不是分隔条：cg_splitter_set 直接忽略 */
        cg_test_int("非分隔条调 cg_splitter_set 被忽略", p1->x, 0);
        free_app(&app);
    }
}

CG_TEST_SUITE("分隔条：拖动与守恒", test_splitter_drag);
CG_TEST_SUITE("分隔条：相邻两块与夹取", test_splitter_clamp);
CG_TEST_SUITE("分隔条：上下分行", test_splitter_rows);
CG_TEST_SUITE("分隔条：结构兜底", test_splitter_refuse);
