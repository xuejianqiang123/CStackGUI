/**
 * @file    test_terminal.c
 * @brief   cg_widget_data.c 里终端（terminal）行处理的单元测试
 * @details 为什么盯它：终端是"业务把字节丢进来、它负责变成给人看的行"的控件，
 *          中间的格式转换全是隐式约定 —— HEX 怎么分组、控制字符怎么处理、中文会不会被搅坏、
 *          带 \r\n 的一坨数据要不要切成多行、超上限丢哪一头。这些错了都不崩，只是日志悄悄变得
 *          不可信（这比崩了还糟：人会照着它排查问题）。白盒：直接读控件的 items。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_doc =
    "{\"app\":{\"title\":\"terminal\",\"width\":400,\"height\":300},"
    "\"widgets\":[{\"id\":\"term\",\"type\":\"terminal\","
    "\"rect\":{\"x\":0,\"y\":0,\"w\":360,\"h\":240}}]}";

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

/** @brief 第 i 行去掉方向标记后的文字（越界返回空串） */
static const char *line_text(cg_widget_t *w, int i)
{
    const char *s = (i >= 0 && i < w->nitems && w->items[i]) ? w->items[i] : "";
    unsigned char tag = (unsigned char)s[0];

    return (tag >= CG_TERM_RX && tag <= CG_TERM_SYS) ? s + 1 : s;
}

/** @brief 第 i 行的方向标记；返回 0 表示没有标记（不该发生） */
static int line_dir(cg_widget_t *w, int i)
{
    if (i < 0 || i >= w->nitems || !w->items[i]) return 0;
    return (unsigned char)w->items[i][0];
}

/** @brief 基本写入：一行一条、方向标记、自动滚到底不算（那是后端的事） */
static void test_term_write(void)
{
    cg_app_t     app;
    cg_widget_t *t;

    cg_test_note("---- 写一行 ----");
    if (!build_app(&app, k_doc)) { cg_test_ok("建应用成功", 0); return; }
    t = cg_find(&app, "term");
    cg_test_ok("terminal 控件在", t != NULL);
    if (!t) { free_app(&app); return; }

    cg_test_int("初始 0 行", cg_term_count(t), 0);
    cg_term_write(t, CG_TERM_RX, "Hello", -1);
    cg_test_int("写一行后 1 行", cg_term_count(t), 1);
    cg_test_int("方向标记是 RX", line_dir(t, 0), CG_TERM_RX);
    cg_test_str("行首带 RX 记号、内容原样", line_text(t, 0), "RX Hello");

    cg_term_write(t, CG_TERM_TX, "AT", -1);
    cg_test_str("TX 行", line_text(t, 1), "TX AT");

    cg_term_write(t, CG_TERM_SYS, "connected", -1);
    cg_test_int("自己记的那种也有标记", line_dir(t, 2), CG_TERM_SYS);
    cg_test_str("SYS 行", line_text(t, 2), "-- connected");

    free_app(&app);
}

/** @brief 文本模式：控制字符变点、中文不被搅坏、UTF-8 多字节保留 */
static void test_term_text_mode(void)
{
    cg_app_t     app;
    cg_widget_t *t;

    cg_test_note("---- 文本模式 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_term_write(t, CG_TERM_RX, "a\tb", -1);
    cg_test_str("制表符按控制字符处理（换成点）", line_text(t, 0), "RX a.b");

    {
        const char *cn = "温度=25";
        cg_term_write(t, CG_TERM_RX, cn, -1);
        {
            /* 中文是 UTF-8 多字节，必须原样保留（把 0x80 以上也变成点的话中文就废了） */
            const char *got = line_text(t, 1);
            cg_test_ok("中文没被搅坏（'温' 的 UTF-8 首字节 0xE6 还在）",
                       ((const unsigned char *)got)[3] == 0xE6);
            cg_test_ok("整行长度 = 3(记号) + 6(两个汉字) + 3(=25)", strlen(got) == 12);
        }
    }

    cg_test_note("---- 显式长度（不是 C 字符串：中间可以有 0 字节） ----");
    {
        char big[64];
        memset(big, 'x', sizeof(big));
        cg_term_write(t, CG_TERM_RX, big, (int)sizeof(big));
        cg_test_ok("64 个字节都进去了", strlen(line_text(t, 2)) == 3 + sizeof(big));
    }

    free_app(&app);
}

/** @brief HEX 模式 */
static void test_term_hex_mode(void)
{
    cg_app_t     app;
    cg_widget_t *t;
    const unsigned char bytes[3] = { 0x01, 0xAB, 0x20 };

    cg_test_note("---- HEX 模式 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_test_int("默认不是 HEX", cg_term_hex(t), 0);
    cg_term_set_hex(t, 1);
    cg_test_int("切到 HEX", cg_term_hex(t), 1);
    cg_term_write(t, CG_TERM_RX, bytes, 3);
    cg_test_str("两个一组、空格分开、大写", line_text(t, 0), "RX 01 AB 20");

    cg_term_set_hex(t, 0);
    cg_test_int("切回文本", cg_term_hex(t), 0);
    cg_term_write(t, CG_TERM_RX, bytes, 3);
    /* 0x01 -> '.'；0xAB 原样留着（>0x7F 不碰）；0x20 是空格，当然也是原样 */
    cg_test_str("文本模式只把控制字节换成点", line_text(t, 1), "RX .\xAB ");

    free_app(&app);
}

/** @brief 时间戳开关（存的是控件的 checked 字段） */
static void test_term_timestamp(void)
{
    cg_app_t     app;
    cg_widget_t *t;

    cg_test_note("---- 时间戳 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_term_write(t, CG_TERM_RX, "no-stamp", -1);
    cg_test_str("默认不带时间戳", line_text(t, 0), "RX no-stamp");

    cg_term_set_timestamp(t, 1);
    cg_test_int("开关落到控件的 checked 上", cg_widget_checked(t), 1);
    cg_term_write(t, CG_TERM_RX, "stamped", -1);
    {
        const char *got = line_text(t, 1);
        cg_test_ok("带 [HH:MM:SS] 前缀", got[0] == '[' && got[3] == ':' && got[6] == ':' && got[9] == ']');
        cg_test_ok("时间戳后面还是 RX 记号 + 内容", strstr(got, "] RX stamped") != NULL);
    }
    cg_term_set_timestamp(t, 0);
    free_app(&app);
}

/** @brief 一段数据里带换行：要切成多行（串口/网络是常态） */
static void test_term_split_lines(void)
{
    cg_app_t     app;
    cg_widget_t *t;

    cg_test_note("---- 一段数据里的 \r\n 要切成多行 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_term_write(t, CG_TERM_RX, "OK\r\nERR\nDONE", -1);
    cg_test_int("切成 3 行", cg_term_count(t), 3);
    cg_test_str("第 1 行不吃 \\r", line_text(t, 0), "RX OK");
    cg_test_str("第 2 行", line_text(t, 1), "RX ERR");
    cg_test_str("第 3 行后面没换行也算一行", line_text(t, 2), "RX DONE");
    cg_test_int("三行都是 RX", line_dir(t, 0) + line_dir(t, 1) + line_dir(t, 2), CG_TERM_RX * 3);

    cg_term_clear(t);
    cg_term_write(t, CG_TERM_RX, "A\r\n", -1);
    cg_test_int("以 \\r\\n 收尾不补空行", cg_term_count(t), 1);

    cg_term_clear(t);
    cg_term_write(t, CG_TERM_RX, "\n", -1);
    cg_test_int("只有一个换行 = 一行空行", cg_term_count(t), 1);
    cg_test_str("空行只有记号", line_text(t, 0), "RX ");

    free_app(&app);
}

/** @brief 行数上限：超了从最老的丢 */
static void test_term_max_lines(void)
{
    cg_app_t     app;
    cg_widget_t *t;
    char         buf[32];

    cg_test_note("---- 行数上限 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_term_set_max_lines(t, 3);
    cg_test_int("上限落到 value 上", cg_widget_value(t), 3);

    for (int i = 1; i <= 5; i++) {
        snprintf(buf, sizeof(buf), "L%d", i);
        cg_term_write(t, CG_TERM_RX, buf, -1);
    }
    cg_test_int("只剩 3 行", cg_term_count(t), 3);
    cg_test_str("留下的是最后 3 行里的第 1 条", line_text(t, 0), "RX L3");
    cg_test_str("最新那条在最下面", line_text(t, 2), "RX L5");

    cg_test_note("---- 清空 ----");
    cg_term_clear(t);
    cg_test_int("清空后 0 行", cg_term_count(t), 0);
    cg_test_int("通知后端整体重灌", t->items_dirty, 1);
    cg_test_int("已推送行数归零", t->term_pushed, 0);
    cg_test_ok("items 也释放了", t->items == NULL);

    free_app(&app);
}

/** @brief 存文件：不该把方向标记写进去 */
static void test_term_save(void)
{
    cg_app_t     app;
    cg_widget_t *t;
    FILE        *f;
    char         buf[256];
    size_t       n;

    cg_test_note("---- 存成文件 ----");
    if (!build_app(&app, k_doc)) return;
    t = cg_find(&app, "term");
    if (!t) { free_app(&app); return; }

    cg_term_write(t, CG_TERM_RX, "line-one", -1);
    cg_term_write(t, CG_TERM_TX, "line-two", -1);
    cg_test_int("保存成功", cg_term_save(t, "term_test.txt"), 0);

    f = fopen("term_test.txt", "rb");
    cg_test_ok("文件能打开（当前目录是 build\\tests）", f != NULL);
    if (f) {
        n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        cg_test_ok("内容里有第 1 行", strstr(buf, "RX line-one") != NULL);
        cg_test_ok("内容里有第 2 行", strstr(buf, "TX line-two") != NULL);
        cg_test_ok("行尾是 CRLF", strstr(buf, "\r\n") != NULL);
        cg_test_ok("没把方向标记字节写进去", ((unsigned char)buf[0]) >= 0x20);
    }

    cg_test_int("路径为空返回 -1", cg_term_save(t, NULL), -1);

    cg_test_note("---- 参数兜底 ----");
    cg_term_write(NULL, CG_TERM_RX, "x", -1);
    cg_term_clear(NULL);
    cg_term_set_hex(NULL, 1);
    cg_term_set_max_lines(NULL, 10);
    cg_test_int("NULL 控件的 count 是 0", cg_term_count(NULL), 0);
    cg_test_int("NULL 控件的 hex 是 0", cg_term_hex(NULL), 0);
    cg_test_ok("NULL 都不会崩", 1);

    cg_test_note("---- 非 terminal 控件上调用要被忽略 ----");
    if (build_app(&app, "{\"widgets\":[{\"id\":\"lb\",\"type\":\"listbox\"}]}")) {
        cg_widget_t *lb = cg_find(&app, "lb");
        cg_term_write(lb, CG_TERM_RX, "x", -1);
        cg_test_int("listbox 没被塞进行", cg_term_count(lb), 0);
        free_app(&app);
    }

    free_app(&app);
}

CG_TEST_SUITE("终端：写入与文本模式", test_term_write);
CG_TEST_SUITE("终端：文本框细节", test_term_text_mode);
CG_TEST_SUITE("终端：HEX 模式", test_term_hex_mode);
CG_TEST_SUITE("终端：时间戳", test_term_timestamp);
CG_TEST_SUITE("终端：按换行切行", test_term_split_lines);
CG_TEST_SUITE("终端：行数上限与清空", test_term_max_lines);
CG_TEST_SUITE("终端：存文件与兜底", test_term_save);
