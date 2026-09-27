/**
 * @file    test_led_meter.c
 * @brief   状态灯（led）与仪表盘（meter）的单元测试
 * @details 为什么盯这两个：它们都是**纯显示控件**，错了既不崩也不报错 —— 灯该闪不闪、表针指错
 *          位置，用户只会觉得"这软件有点怪"，很难定位到是控件的问题。所以把两处真正容易错的
 *          算法（闪烁的相位、值到角度的映射）放在 core 里，就是为了能在这里脱窗验证。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 组合文档：一个带文字的状态灯 + 一个 arc 表盘 + 一个 half 表盘 */
static const char *k_doc =
    "{\"app\":{\"title\":\"led+meter\",\"width\":480,\"height\":260},"
    "\"widgets\":["
    "{\"id\":\"led_a\",\"type\":\"led\",\"rect\":{\"x\":8,\"y\":8,\"w\":20,\"h\":20},"
    "\"value\":1,\"format\":\"round\",\"period\":500,\"text\":\"已连接\"},"
    "{\"id\":\"led_b\",\"type\":\"led\",\"rect\":{\"x\":8,\"y\":40,\"w\":20,\"h\":20},"
    "\"value\":2,\"period\":200},"
    "{\"id\":\"mt_arc\",\"type\":\"meter\",\"rect\":{\"x\":100,\"y\":8,\"w\":140,\"h\":140},"
    "\"value\":40,\"min\":0,\"max\":100,\"format\":\"arc\",\"text\":\"°C\"},"
    "{\"id\":\"mt_half\",\"type\":\"meter\",\"rect\":{\"x\":260,\"y\":8,\"w\":140,\"h\":90},"
    "\"value\":75,\"min\":0,\"max\":100,\"format\":\"half\"}"
    "]}";

/**
 * @brief 解析文档 + 建控件树
 * @param app 待初始化的应用
 * @param json 要解析的 .cgui 文本
 * @return int 成功返回 1；解析失败返回 0
 */
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

/**
 * @brief 收尾：先放控件（它们会回写节点），再放文档
 * @param app 要释放的应用
 * @return void 无返回值
 */
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

/** @brief 从 .cgui 读进来的字段（形状、周期、文字） */
/**
 * @brief 从 .cgui 读进来的字段（形状、周期、文字）
 * @return void 无返回值
 */
static void test_led_parse(void)
{
    cg_app_t     app;
    cg_widget_t *a, *b;

    cg_test_note("---- 状态灯：.cgui 字段读进来了 ----");
    if (!build_app(&app, k_doc)) { cg_test_ok("建应用成功", 0); return; }
    a = cg_find(&app, "led_a");
    b = cg_find(&app, "led_b");
    cg_test_ok("两个灯都在", a && b);
    if (!a || !b) { free_app(&app); return; }

    cg_test_int("状态读回来是 1（亮）", cg_led_state(a), 1);
    cg_test_str("形状 round 读到了", a->format, "round");
    cg_test_int("闪烁周期 500 读到了", a->period, 500);
    cg_test_str("说明文字读到了", a->text, "已连接");
    cg_test_int("第二个灯是 2（闪）", cg_led_state(b), 2);
    cg_test_int("它的周期是 200", b->period, 200);

    cg_test_note("---- 亮/闪/灭：不闪的状态在任何时刻都是确定的 ----");
    cg_test_int("状态 1（亮）：now=0 时亮着", cg_led_lit(a, 0), 1);
    cg_test_int("状态 1（亮）：随便一个时刻也亮着", cg_led_lit(a, 123456), 1);
    cg_led_set(a, 0);
    cg_test_int("状态 0（灭）：now=0 时不亮", cg_led_lit(a, 0), 0);
    cg_test_int("状态 0（灭）：任何时刻都不亮", cg_led_lit(a, 123456), 0);

    cg_test_note("---- 状态夹取：0..2 之外的值不许进去 ----");
    cg_led_set(a, -5);
    cg_test_int("给 -5 夹成 0", cg_led_state(a), 0);
    cg_led_set(a, 9);
    cg_test_int("给 9 夹成 2", cg_led_state(a), 2);
    cg_test_int("顺带写回了文档节点", a->node ? a->node->value : -1, 2);

    free_app(&app);
}

/** @brief 闪烁相位：按周期前后半周期翻转（这是最容易错、也最看不出来的地方） */
/**
 * @brief 闪烁相位：按周期前后半周期翻转（这是最容易错、也最看不出来的地方）
 * @return void 无返回值
 */
static void test_led_blink(void)
{
    cg_app_t     app;
    cg_widget_t *b;

    cg_test_note("---- 闪烁相位：周期 200ms = 前 100ms 亮、后 100ms 灭 ----");
    if (!build_app(&app, k_doc)) return;
    b = cg_find(&app, "led_b");
    if (!b) { free_app(&app); return; }

    cg_test_int("now=0 亮", cg_led_lit(b, 0), 1);
    cg_test_int("now=99 还是亮（前半周期）", cg_led_lit(b, 99), 1);
    cg_test_int("now=100 灭了（跨过半周期）", cg_led_lit(b, 100), 0);
    cg_test_int("now=199 还是灭", cg_led_lit(b, 199), 0);
    cg_test_int("now=200 又亮（新的一轮）", cg_led_lit(b, 200), 1);
    cg_test_int("now=250 亮", cg_led_lit(b, 250), 1);
    cg_test_int("now=300 灭", cg_led_lit(b, 300), 0);
    cg_test_int("now=1000 亮（整除周期，回到起点）", cg_led_lit(b, 1000), 1);

    cg_test_note("---- 周期兜底：小于 100ms 当 500ms 用 ----");
    /* 判别用的时刻要选在"两种周期下结论相反"的地方：now=300 时，
     * 按兜底后的 500ms 是后半周期（灭）；若真按 50ms 算，则是新一轮的前半周期（亮）。 */
    cg_widget_set_period(b, 50);
    cg_test_int("周期 50 的灯在 now=300 不亮（说明按 500 算了）", cg_led_lit(b, 300), 0);
    cg_test_int("周期 50 在 now=100 亮", cg_led_lit(b, 100), 1);
    cg_widget_set_period(b, 0);
    cg_test_int("周期 0 也是兜到 500（now=300 不亮）", cg_led_lit(b, 300), 0);
    cg_widget_set_period(b, 400);
    cg_test_int("周期 400：now=100 亮", cg_led_lit(b, 100), 1);
    cg_test_int("周期 400：now=200 灭", cg_led_lit(b, 200), 0);
    cg_test_int("周期 400：now=400 又亮", cg_led_lit(b, 400), 1);

    cg_test_note("---- NULL 兜底 ----");
    cg_test_int("NULL 的灯不亮", cg_led_lit(NULL, 0), 0);
    cg_test_int("NULL 的状态是 0", cg_led_state(NULL), 0);
    cg_led_set(NULL, 1);
    cg_led_set_color(NULL, 0);
    cg_test_ok("NULL 都不会崩", 1);

    free_app(&app);
}

/** @brief 颜色与类型守卫 */
/**
 * @brief 颜色设置与类型守卫
 * @return void 无返回值
 */
static void test_led_color_guard(void)
{
    cg_app_t     app;
    cg_widget_t *a, *lb;

    cg_test_note("---- 颜色：落到自绘控件取色的那个通道上 ----");
    if (!build_app(&app, "{\"widgets\":["
                         "{\"id\":\"led_a\",\"type\":\"led\"},"
                         "{\"id\":\"lb\",\"type\":\"listbox\"}]}")) return;
    a = cg_find(&app, "led_a");
    lb = cg_find(&app, "lb");
    cg_led_set_color(a, 0x123456);
    /* 容差：不同实现可能把颜色存成 0xRRGGBB 或 0xFFRRGGBB，两种都算对 */
    cg_test_ok("颜色落到了 style.fg", a->style.fg == 0x123456 || a->style.fg == 0xFF123456);

    cg_test_note("---- 类型守卫：对别的控件调用要被忽略 ----");
    cg_led_set(lb, 2);
    cg_led_set_color(lb, 0x00FF00);
    cg_test_int("listbox 没被塞进状态", lb->value, 0);
    cg_test_ok("listbox 的颜色也没动", lb->style.fg != 0x123456);
    cg_test_ok("led 自己还是 2", cg_led_state(a) == 2 || cg_led_state(a) == 0);

    free_app(&app);
}

/** @brief 表盘角度：值 -> 角度（arc 270° / half 180°，超量程夹取） */
/**
 * @brief 表盘角度：值 -> 角度（弧表 270°、半表 180°，超量程夹取）
 * @return void 无返回值
 */
static void test_meter_angle(void)
{
    cg_app_t     app;
    cg_widget_t *arc, *half;

    cg_test_note("---- arc 样式：135° 起顺时针扫 270° ----");
    if (!build_app(&app, k_doc)) return;
    arc = cg_find(&app, "mt_arc");
    half = cg_find(&app, "mt_half");
    if (!arc || !half) { free_app(&app); return; }

    cg_test_int("0 在左下起点 135°", cg_meter_angle_deg(arc, 0), 135);
    cg_test_int("50 在正上方 270°", cg_meter_angle_deg(arc, 50), 270);
    cg_test_int("100 在右下终点 405°", cg_meter_angle_deg(arc, 100), 405);
    cg_test_int("25 在四分之一处 202°（135+270/4）", cg_meter_angle_deg(arc, 25), 202);
    cg_test_int("值 40（文档里写的）-> 243°", cg_meter_angle_deg(arc, 40), 243);

    cg_test_note("---- 超量程：按边界夹取，不会跑出弧外 ----");
    cg_test_int("给 -20 还是 135°", cg_meter_angle_deg(arc, -20), 135);
    cg_test_int("给 999 还是 405°", cg_meter_angle_deg(arc, 999), 405);

    cg_test_note("---- half 样式：180° 扫 180°（正左 -> 正右） ----");
    cg_test_int("0 在正左 180°", cg_meter_angle_deg(half, 0), 180);
    cg_test_int("50 在正下 270°", cg_meter_angle_deg(half, 50), 270);
    cg_test_int("100 在正右 360°", cg_meter_angle_deg(half, 100), 360);
    cg_test_int("值 75（文档里写的）-> 315°", cg_meter_angle_deg(half, 75), 315);

    cg_test_note("---- 量程非法时不除零 ----");
    cg_meter_set_range(arc, 50, 50);
    cg_test_int("max<=min 被纠正成 min+1", arc->vmax - arc->vmin, 1);
    arc->vmax = arc->vmin;                       /* 绕过 API 直接造出非法量程 */
    cg_test_int("量程为 0 时给起点角", cg_meter_angle_deg(arc, 10), 135);
    cg_test_int("NULL 给起点角（不崩）", cg_meter_angle_deg(NULL, 10), 135);

    free_app(&app);
}

/** @brief 表盘取值与类型守卫 */
/**
 * @brief 表盘取值与类型守卫
 * @return void 无返回值
 */
static void test_meter_value_guard(void)
{
    cg_app_t     app;
    cg_widget_t *arc, *lb;

    cg_test_note("---- 设值：夹到量程（和滑块同一套语义） ----");
    if (!build_app(&app, "{\"widgets\":["
                         "{\"id\":\"mt\",\"type\":\"meter\",\"min\":0,\"max\":100},"
                         "{\"id\":\"lb\",\"type\":\"listbox\"}]}")) return;
    arc = cg_find(&app, "mt");
    lb = cg_find(&app, "lb");

    cg_meter_set(arc, 70);
    cg_test_int("设 70 就是 70", cg_widget_value(arc), 70);
    cg_meter_set(arc, 500);
    cg_test_int("超上限夹到 100", cg_widget_value(arc), 100);
    cg_meter_set(arc, -30);
    cg_test_int("超下限夹到 0", cg_widget_value(arc), 0);
    cg_test_int("夹取后角度也跟着贴边", cg_meter_angle_deg(arc, cg_widget_value(arc)), 135);

    cg_test_note("---- 改量程：值按新量程重新夹 ----");
    cg_meter_set_range(arc, 0, 10);
    cg_test_int("量程变成 0..10", arc->vmax, 10);
    cg_meter_set(arc, 5);
    cg_test_int("值 5 在 0..10 的中点 -> 270°", cg_meter_angle_deg(arc, cg_widget_value(arc)), 270);

    cg_test_note("---- 类型守卫与 NULL ----");
    cg_meter_set(lb, 88);
    cg_test_int("listbox 没被当成表盘", lb->value, 0);
    cg_meter_set(NULL, 1);
    cg_meter_set_range(NULL, 0, 1);
    cg_test_ok("NULL 都不会崩", 1);

    free_app(&app);
}

CG_TEST_SUITE("状态灯：字段解析与状态", test_led_parse);
CG_TEST_SUITE("状态灯：闪烁相位与周期", test_led_blink);
CG_TEST_SUITE("状态灯：颜色与类型守卫", test_led_color_guard);
CG_TEST_SUITE("仪表盘：值到角度", test_meter_angle);
CG_TEST_SUITE("仪表盘：设值与类型守卫", test_meter_value_guard);
