/**
 * @file    test_chart.c
 * @brief   cg_widget_data.c 里图表（chart）点缓冲的单元测试
 * @details 为什么盯它：图表是"业务按固定节奏 push 点、控件管窗口"的关系，最容易错的不是画得
 *          好不好看，而是**窗口滑动时的下标**——满了之后是该丢最老的还是最新的、多系列会不会
 *          串行、改了窗口大小之后旧的 fill 计数有没有跟着清。这些错了都不崩：曲线只是**悄悄画
 *          错**（少一段、串了一条别的系列的数据），肉眼很难判断是数据源的问题还是控件的问题。
 *          白盒：直接读控件的点数组，不画图。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 两个系列、窗口 5 个点：数字都很小，方便把"滑动"看成人能数出来的序列 */
static const char *k_doc2 =
    "{\"app\":{\"title\":\"chart\",\"width\":480,\"height\":240},"
    "\"widgets\":[{\"id\":\"cht\",\"type\":\"chart\","
    "\"rect\":{\"x\":0,\"y\":0,\"w\":440,\"h\":200},"
    "\"value\":5,\"min\":0,\"max\":100,\"items\":[\"温度\",\"湿度\"]}]}";

/* 不写 items：应当按"单系列"用（最简单的 .cgui 不该被逼着写 items） */
static const char *k_doc1 =
    "{\"widgets\":[{\"id\":\"cht\",\"type\":\"chart\",\"value\":4,"
    "\"rect\":{\"x\":0,\"y\":0,\"w\":200,\"h\":120}}]}";

/** @brief 解析文档 + 建控件树；成功返回 1、失败返回 0 */
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

/** @brief 把某系列的点读出来拼成字符串，便于一眼比对（"1,2,3"） */
static void dump_series(cg_widget_t *w, int series, char *out, size_t size)
{
    int   n = cg_chart_filled(w, series);
    size_t used = 0;

    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        int wrote = snprintf(out + used, size - used, i ? ",%d" : "%d", cg_chart_point(w, series, i));
        if (wrote <= 0) break;
        used += (size_t)wrote;
    }
}

/** @brief 基本 push 与"窗口满了丢最老的" */
static void test_chart_push(void)
{
    cg_app_t     app;
    cg_widget_t *c;
    char         buf[128];

    cg_test_note("---- 单系列：窗口滑动 ----");
    if (!build_app(&app, k_doc1)) { cg_test_ok("建应用成功", 0); return; }
    c = cg_find(&app, "cht");
    cg_test_ok("chart 控件在", c != NULL);
    if (!c) { free_app(&app); return; }

    cg_test_int("没写 items 就是 1 个系列", cg_chart_series_count(c), 1);
    cg_test_int("初始没有任何点", cg_chart_filled(c, 0), 0);

    cg_chart_push(c, 0, 11);
    cg_chart_push(c, 0, 22);
    cg_test_int("推两个点", cg_chart_filled(c, 0), 2);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("点的顺序是从老到新", buf, "11,22");

    cg_test_note("---- 窗口 4：第 5 个点进来要顶掉最老的 ----");
    cg_chart_push(c, 0, 33);
    cg_chart_push(c, 0, 44);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("装满 4 个", buf, "11,22,33,44");
    cg_chart_push(c, 0, 55);
    cg_test_int("还是 4 个（窗口不涨）", cg_chart_filled(c, 0), 4);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("丢掉的是最老的 11，最新的在末尾", buf, "22,33,44,55");
    cg_chart_push(c, 0, 66);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("再推一个继续左移", buf, "33,44,55,66");

    cg_test_note("---- 负值与超范围的值都要原样收下（裁剪是画的时候的事） ----");
    cg_chart_push(c, 0, -7);
    cg_chart_push(c, 0, 9999);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("越界的值也存原值", buf, "55,66,-7,9999");
    cg_test_int("读回最新那个", cg_chart_point(c, 0, 3), 9999);

    free_app(&app);
}

/** @brief 多系列：各推各的、互不串行 */
static void test_chart_multi(void)
{
    cg_app_t     app;
    cg_widget_t *c;
    char         buf[128];
    int          vals[2];

    cg_test_note("---- 双系列 ----");
    if (!build_app(&app, k_doc2)) return;
    c = cg_find(&app, "cht");
    if (!c) { free_app(&app); return; }

    cg_test_int("items 里写了 2 个系列", cg_chart_series_count(c), 2);
    cg_test_str("系列名读得到", cg_widget_list_item(c, 0), "温度");

    cg_chart_push(c, 0, 1);
    cg_chart_push(c, 1, 101);
    cg_chart_push(c, 0, 2);
    cg_chart_push(c, 1, 102);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("系列 0 只收自己的点", buf, "1,2");
    dump_series(c, 1, buf, sizeof(buf));
    cg_test_str("系列 1 也是", buf, "101,102");

    cg_test_note("---- push_all：一次给所有系列各一个点 ----");
    vals[0] = 3;
    vals[1] = 103;
    cg_chart_push_all(c, vals, 2);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("系列 0 跟上", buf, "1,2,3");
    dump_series(c, 1, buf, sizeof(buf));
    cg_test_str("系列 1 跟上", buf, "101,102,103");

    cg_test_note("---- push_all 给的比系列多：多的忽略，不越界 ----");
    {
        int three[3];
        three[0] = 4; three[1] = 104; three[2] = 999;
        cg_chart_push_all(c, three, 3);
        cg_test_int("系列 0 收到 4", cg_chart_point(c, 0, cg_chart_filled(c, 0) - 1), 4);
        cg_test_int("系列 1 收到 104", cg_chart_point(c, 1, cg_chart_filled(c, 1) - 1), 104);
    }

    cg_test_note("---- 系列下标越界：归到第 0 个系列，不崩 ----");
    {
        int before = cg_chart_filled(c, 0);
        cg_chart_push(c, 7, 5);
        cg_test_int("越界下标进了系列 0", cg_chart_filled(c, 0), before + 1);
    }

    free_app(&app);
}

/** @brief clear / 改窗口 / 加系列：缓冲重建后的计数必须干净 */
static void test_chart_rebuild(void)
{
    cg_app_t     app;
    cg_widget_t *c;
    char         buf[128];

    cg_test_note("---- 清空：点没了，系列还在 ----");
    if (!build_app(&app, k_doc2)) return;
    c = cg_find(&app, "cht");
    if (!c) { free_app(&app); return; }

    cg_chart_push_all(c, (const int[]){ 1, 2 }, 2);
    cg_test_int("两系列各有 1 个点", cg_chart_filled(c, 0) + cg_chart_filled(c, 1), 2);
    cg_chart_clear(c);
    cg_test_int("清空后系列 0 归零", cg_chart_filled(c, 0), 0);
    cg_test_int("清空后系列 1 归零", cg_chart_filled(c, 1), 0);
    cg_test_int("系列数没变", cg_chart_series_count(c), 2);

    cg_test_note("---- 改窗口大小：缓冲重建，旧点不残留 ----");
    cg_chart_push(c, 0, 8);
    cg_widget_set_value(c, 3);                 /* value = 每系列窗口点数 */
    cg_test_int("重建后没有点（旧的窗口对不上了）", cg_chart_filled(c, 0), 0);
    cg_test_int("新窗口生效", c->chart_cap, 3);
    for (int i = 1; i <= 4; i++) cg_chart_push(c, 0, i);
    dump_series(c, 0, buf, sizeof(buf));
    cg_test_str("按新窗口滑动", buf, "2,3,4");

    cg_test_note("---- 运行时加系列 ----");
    cg_test_int("加第 3 个系列返回下标 2", cg_chart_add_series(c, "压力"), 2);
    cg_test_int("系列数变成 3", cg_chart_series_count(c), 3);
    cg_test_str("名字记下了", cg_widget_list_item(c, 2), "压力");
    cg_chart_push(c, 2, 77);
    cg_test_int("新系列能收点", cg_chart_point(c, 2, 0), 77);

    cg_test_note("---- 边界兜底 ----");
    c->value = 1;
    cg_chart_push(c, 0, 1);                      /* 触发 ensure 重建 */
    cg_test_int("1 当作缺省 120", c->chart_cap, 120);
    c->value = 999999;
    cg_chart_push(c, 0, 1);
    cg_test_int("超大值夹到 4096", c->chart_cap, 4096);

    cg_test_note("---- 参数兜底 ----");
    cg_chart_push(NULL, 0, 1);
    cg_chart_push_all(NULL, NULL, 0);
    cg_chart_clear(NULL);
    cg_test_int("NULL 的系列数是 0", cg_chart_series_count(NULL), 0);
    cg_test_int("NULL 的点读回 0", cg_chart_point(NULL, 0, 0), 0);
    cg_test_int("NULL 加系列返回 -1", cg_chart_add_series(NULL, "x"), -1);
    cg_chart_set_range(NULL, 0, 10);
    cg_test_ok("NULL 都不会崩", 1);

    cg_test_int("读越界的点返回 0", cg_chart_point(c, 0, 999999), 0);
    cg_test_int("读不存在的系列返回 0", cg_chart_point(c, 9, 0), 0);
    cg_chart_set_range(c, 10, 10);
    cg_test_int("范围反着给会纠正（max 至少 min+1）", c->vmax - c->vmin >= 1, 1);

    free_app(&app);
}

/** @brief 非 chart 控件上调用要被忽略（类型守卫生效） */
static void test_chart_wrong_type(void)
{
    cg_app_t     app;
    cg_widget_t *lb;

    cg_test_note("---- 对别的控件调用 chart API：忽略，不崩 ----");
    if (!build_app(&app, "{\"widgets\":[{\"id\":\"lb\",\"type\":\"listbox\"}]}")) return;
    lb = cg_find(&app, "lb");
    cg_chart_push(lb, 0, 1);
    cg_chart_push_all(lb, (const int[]){ 1 }, 1);
    cg_chart_clear(lb);
    cg_test_int("listbox 没被塞进点", cg_chart_filled(lb, 0), 0);
    cg_test_ok("也没崩", 1);
    free_app(&app);
}

CG_TEST_SUITE("图表：窗口滑动", test_chart_push);
CG_TEST_SUITE("图表：多系列", test_chart_multi);
CG_TEST_SUITE("图表：重建与兜底", test_chart_rebuild);
CG_TEST_SUITE("图表：类型守卫", test_chart_wrong_type);
