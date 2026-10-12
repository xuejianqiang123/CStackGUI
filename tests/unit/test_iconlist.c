/**
 * @file    test_iconlist.c
 * @brief   图标列表（iconlist）行数据与类型契约的单元测试
 * @details 为什么盯它：这个控件的行数据是**两份来源**（.cgui 的 items 转换 + 运行时
 *          cg_iconlist_add 灌），转换漏了、夹取用错数组（items vs il_rows）、类型守卫
 *          形同虚设 —— 这三类错了都不崩，只是**悄悄画错 / 越界写**：
 *            - set_value 按 nitems 夹 → 行号越过 il_rows 是实打实的越界；
 *            - want_type 没挡住 → 对 listbox 调 iconlist API 往别人身上写数据；
 *            - .cgui items 没转成行 → 画布上有字、运行时空列表。
 *          白盒：直接读 il_rows / value / check，不画图（无窗即可跑）。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 带 check 开关 + 三条 items（主标题）的图标列表 */
static const char *k_doc =
    "{\"app\":{\"title\":\"iconlist\",\"width\":400,\"height\":300},"
    "\"widgets\":[{\"id\":\"il\",\"type\":\"iconlist\",\"check\":true,"
    "\"rect\":{\"x\":0,\"y\":0,\"w\":320,\"h\":220},"
    "\"items\":[\"复制文件路径\",\"解除文件占用\",\"用记事本打开\"]},"
    "{\"id\":\"lb\",\"type\":\"listbox\",\"rect\":{\"x\":0,\"y\":0,\"w\":100,\"h\":60},"
    "\"items\":[\"a\",\"b\"]}]}";

/**
 * @brief 解析文档 + 建控件树（与 test_chart.c 同一套路）
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

/**
 * @brief .cgui 的 items 转换成行 + check 字段复用 + 事件掩码
 * @return void 无返回值
 */
static void test_iconlist_parse(void)
{
    cg_app_t     app;
    cg_widget_t *il, *lb;

    cg_test_note("---- .cgui 解析：items -> 行、check、事件掩码 ----");
    if (!build_app(&app, k_doc)) { cg_test_ok("建应用成功", 0); return; }

    il = cg_find(&app, "il");
    lb = cg_find(&app, "lb");
    cg_test_ok("iconlist 控件在", il != NULL);
    if (!il) { free_app(&app); return; }

    cg_test_int("items 三个转成三行", cg_iconlist_count(il), 3);
    cg_test_str("第 0 行主标题", (il->il_rows && il->il_n > 0) ? il->il_rows[0].title : NULL,
                "复制文件路径");
    cg_test_str("第 2 行主标题", (il->il_rows && il->il_n > 2) ? il->il_rows[2].title : NULL,
                "用记事本打开");
    cg_test_str("转换出的行没有图标（留空待灌）",
                (il->il_rows && il->il_n > 0) ? il->il_rows[0].icon : "x", NULL);
    cg_test_int("check:true 复用 table 的字段落进 w->check", il->check, 1);
    cg_test_int("初始选中 0（value 缺省）", cg_widget_value(il), 0);

    /* 事件掩码三方契约：声明了就要真支持，没声明的必须是 0（画布/文档同源） */
    cg_test_int("iconlist 支持 select", cg_kind_supports_event("iconlist", CG_EVENT_SELECT), 1);
    cg_test_int("iconlist 支持 check", cg_kind_supports_event("iconlist", CG_EVENT_CHECK), 1);
    cg_test_int("iconlist 支持 click", cg_kind_supports_event("iconlist", CG_EVENT_CLICK), 1);
    cg_test_int("iconlist 支持 dblclick", cg_kind_supports_event("iconlist", CG_EVENT_DBLCLICK), 1);
    cg_test_int("iconlist 不支持 change（没有输入语义）",
                cg_kind_supports_event("iconlist", CG_EVENT_CHANGE), 0);

    /* 类型守卫的反面：listbox 上调 iconlist API 必须被挡下（stderr 会有一行报错，属预期） */
    cg_test_int("对 listbox 调 cg_iconlist_add 被拒绝", cg_iconlist_add(lb, NULL, "x", NULL, NULL), -1);
    cg_test_int("对 listbox 调 cg_iconlist_count 是 0", cg_iconlist_count(lb), 0);

    free_app(&app);
}

/**
 * @brief 运行时增删改查 + 勾选 + set_value 按 il_n 夹取
 * @return void 无返回值
 */
static void test_iconlist_api(void)
{
    cg_app_t     app;
    cg_widget_t *il;

    cg_test_note("---- 运行时 API：add / set_row / checked / clear ----");
    if (!build_app(&app, k_doc)) { cg_test_ok("建应用成功", 0); return; }
    il = cg_find(&app, "il");
    if (!il) { cg_test_ok("iconlist 控件在", 0); free_app(&app); return; }

    cg_test_int("起始 3 行", cg_iconlist_count(il), 3);
    cg_test_int("add 返回新行号 3",
                cg_iconlist_add(il, "assets/icon.png", "新行", "副标题", "在用"), 3);
    cg_test_int("4 行", cg_iconlist_count(il), 4);
    cg_test_str("新行标题", il->il_rows[3].title, "新行");
    cg_test_str("新行状态", il->il_rows[3].status, "在用");

    /* 勾选：默认 0；设 1 读 1；越界 set 不写脏、越界读 0 */
    cg_test_int("默认没勾", cg_iconlist_checked(il, 0), 0);
    cg_iconlist_set_checked(il, 0, 1);
    cg_test_int("勾上读 1", cg_iconlist_checked(il, 0), 1);
    cg_iconlist_set_checked(il, 99, 1);          /* 越界：忽略，不许写到行数组外 */
    cg_test_int("越界读是 0", cg_iconlist_checked(il, 99), 0);
    cg_test_int("越界 set 后行数不变", cg_iconlist_count(il), 4);

    /* set_row 替换与清空（NULL = 清空该段） */
    cg_iconlist_set_row(il, 1, NULL, "改过了", "副", "状态");
    cg_test_str("set_row 换标题", il->il_rows[1].title, "改过了");
    cg_test_str("set_row 清图标", il->il_rows[1].icon, NULL);
    cg_test_str("set_row 写状态", il->il_rows[1].status, "状态");
    cg_test_int("set_row 不动勾选态", cg_iconlist_checked(il, 1), 0);

    /* set_value 按 il_n 夹取：这是"两份数组"最容易错的地方（历史 bug 形态） */
    cg_widget_set_value(il, 99);
    cg_test_int("越界 value 夹到最后一行", cg_widget_value(il), cg_iconlist_count(il) - 1);
    cg_widget_set_value(il, -5);
    cg_test_int("负 value 夹到 0", cg_widget_value(il), 0);

    /* clear：行归零、选中号复位、再 add 还能用（容量复用不炸） */
    cg_widget_set_value(il, 2);
    cg_iconlist_clear(il);
    cg_test_int("clear 后 0 行", cg_iconlist_count(il), 0);
    cg_test_int("clear 后选中号复位（不许停在已删的行上）", cg_widget_value(il), 0);
    cg_test_int("clear 后还能 add", cg_iconlist_add(il, NULL, "又来一行", NULL, NULL), 0);
    cg_test_str("clear 后新行标题", il->il_rows[0].title, "又来一行");

    free_app(&app);
}

/**
 * @brief 套件入口：串起本文件全部用例
 * @return void 无返回值
 */
static void test_iconlist_run_all(void)
{
    test_iconlist_parse();
    test_iconlist_api();
}

CG_TEST_SUITE("iconlist", test_iconlist_run_all);
