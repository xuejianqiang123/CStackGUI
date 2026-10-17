/**
 * @file    test_widget.c
 * @brief   cg_widget.c 的单元测试：控件查找、公共读写与 set_value 夹取契约
 * @details 为什么盯它：cg_widget.c（约 800 行）是 core 层被引用最多的文件，此前却只有
 *          test_bind / test_action 等**间接**回归覆盖 —— 一旦改坏，测试全绿、界面悄悄错
 *          （2026-10-17 架构评审"可维护性欠账"整改）。这里把三条最容易被改坏的隐式
 *          契约钉死：
 *            ① NULL 控件的读写全部兜底不崩（业务代码到处在裸调）；
 *            ② 公共写入一律**双写**（控件字段 + 文档节点），save 出去的才是新值；
 *            ③ set_value 的夹取按类型分三种口径（数值量夹 [vmin,vmax] / 列表夹下标 /
 *               colorpicker 原样）—— 这三条各有真实事故注释在源码里。
 *          白盒：直接 include cg_internal.h 拿控件结构，不需要真的开窗。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 测试文档：
 *   lab_title   label    文字/矩形读写与双写
 *   sld_level   slider   数值量夹取 [0,500]
 *   lst_pick    listbox  下标夹取 0..2
 *   col_pick    colorpicker 值不夹取（0x6750A4）
 *   edt_spin    edit+spin 数值框派生文本（value -> text）
 *   chk_on      checkbox 勾选双写
 */
static const char *k_doc =
    "{\n"
    "  \"app\": { \"title\": \"widget 测试\", \"width\": 420, \"height\": 300 },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"lab_title\", \"type\": \"label\", \"text\": \"标题\", \"x\": 10, \"y\": 20, \"w\": 120, \"h\": 24 },\n"
    "    { \"id\": \"sld_level\", \"type\": \"slider\", \"min\": 0, \"max\": 500, \"value\": 300 },\n"
    "    { \"id\": \"lst_pick\", \"type\": \"listbox\", \"items\": [\"甲\", \"乙\", \"丙\"] },\n"
    "    { \"id\": \"col_pick\", \"type\": \"colorpicker\", \"value\": 6768292 },\n"
    "    { \"id\": \"edt_spin\", \"type\": \"edit\", \"spin\": 1, \"min\": 0, \"max\": 100, \"value\": 42 },\n"
    "    { \"id\": \"chk_on\", \"type\": \"checkbox\", \"text\": \"启用\", \"checked\": 1 }\n"
    "  ]\n"
    "}\n";

/**
 * @brief 解析文档 + 建控件树（与 test_bind.c 同一套白盒建法）
 * @param app 待初始化的应用
 * @param json 要解析的 .cgui 文本
 * @return int 成功返回 0；解析失败返回 -1
 */
static int build_app(cg_app_t *app, const char *json)
{
    char      err[256] = { 0 };
    cg_doc_t *doc = cg_doc_parse(json, err, sizeof(err));

    if (!doc) {
        printf("        解析失败：%s\n", err);
        return -1;
    }
    memset(app, 0, sizeof(*app));
    app->scale = 1;
    app->doc = *doc;      /* app->doc 是内嵌结构，所有权转移 */
    free(doc);
    cg_build_widgets(app);
    return 0;
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
 * @brief 套件主体：NULL 兜底 / 查找与计数 / 双写 / set_value 三种夹取口径
 * @return void 无返回值
 */
static void test_widget_core(void)
{
    cg_app_t     app;
    cg_widget_t *lab, *sld, *lst, *col, *edt, *chk;

    if (build_app(&app, k_doc) != 0) {
        cg_test_ok("测试文档能解析", 0);
        return;
    }

    cg_test_note("---- NULL 控件：读返回兜底值、写不崩 ----");
    cg_test_ok("cg_widget_id(NULL) == NULL", cg_widget_id(NULL) == NULL);
    cg_test_ok("cg_widget_type(NULL) == NULL", cg_widget_type(NULL) == NULL);
    cg_test_ok("cg_widget_text(NULL) == NULL", cg_widget_text(NULL) == NULL);
    cg_test_int("cg_widget_x(NULL) == 0", cg_widget_x(NULL), 0);
    cg_test_int("cg_widget_h(NULL) == 0", cg_widget_h(NULL), 0);
    cg_test_int("cg_widget_value(NULL) == 0", cg_widget_value(NULL), 0);
    cg_test_ok("cg_app_of(NULL) == NULL", cg_app_of(NULL) == NULL);
    cg_test_ok("cg_widget_by_id(NULL,..) == NULL", cg_widget_by_id(NULL, "x") == NULL);
    cg_test_ok("cg_widget_by_id(..,NULL) == NULL", cg_widget_by_id(&app, NULL) == NULL);
    cg_test_ok("cg_widget_by_hwnd(NULL,..) == NULL", cg_widget_by_hwnd(NULL, (void *)1) == NULL);
    cg_widget_set_text(NULL, "x");
    cg_widget_set_rect(NULL, 1, 2, 3, 4);
    cg_widget_set_value(NULL, 5);
    cg_widget_set_checked(NULL, 1);
    cg_widget_set_font(NULL, "Arial", 10, 1);
    cg_test_ok("对 NULL 控件连写一轮不崩", 1);

    cg_test_note("---- 查找与计数（count 不含窗口伪控件）----");
    lab = cg_find(&app, "lab_title");
    sld = cg_find(&app, "sld_level");
    lst = cg_find(&app, "lst_pick");
    col = cg_find(&app, "col_pick");
    edt = cg_find(&app, "edt_spin");
    chk = cg_find(&app, "chk_on");
    cg_test_ok("cg_find 命中 lab_title", lab != NULL);
    cg_test_ok("cg_find 未命中返回 NULL", cg_find(&app, "没有这个控件") == NULL);
    cg_test_str("cg_widget_id", cg_widget_id(lab), "lab_title");
    cg_test_str("cg_widget_type", cg_widget_type(sld), "slider");
    cg_test_str("cg_widget_text（无文字的控件按空串）", cg_widget_text(sld), "");
    cg_test_int("cg_widget_count = 6（窗口伪控件不计）", cg_widget_count(&app), 6);
    cg_test_int("cg_widget_count(NULL) = 0", cg_widget_count(NULL), 0);
    cg_test_ok("by_ctrl_id 能按控件自己的 ctrl_id 反查",
               cg_widget_by_ctrl_id(&app, lab->ctrl_id) != NULL);
    cg_test_ok("by_ctrl_id 未命中返回 NULL",
               cg_widget_by_ctrl_id(&app, 99999) == NULL);

    cg_test_note("---- 读取：矩形 / 勾选 / 值 ----");
    cg_test_int("lab x", cg_widget_x(lab), 10);
    cg_test_int("lab y", cg_widget_y(lab), 20);
    cg_test_int("lab w", cg_widget_w(lab), 120);
    cg_test_int("lab h", cg_widget_h(lab), 24);
    cg_test_int("slider 初始值", cg_widget_value(sld), 300);
    cg_test_int("checkbox 初始勾选", cg_widget_checked(chk), 1);
    cg_test_int("checkbox 文字", strcmp(cg_widget_text(chk), "启用"), 0);

    cg_test_note("---- 双写：改控件必须同步文档节点（save 出去的才是新值）----");
    cg_widget_set_text(lab, "新标题");
    cg_test_str("set_text 后控件字段", cg_widget_text(lab), "新标题");
    cg_test_str("set_text 回写节点", lab->node ? lab->node->text : NULL, "新标题");
    cg_widget_set_rect(lab, 1, 2, 3, 4);
    cg_test_int("set_rect 回写节点 x", lab->node ? lab->node->x : -1, 1);
    cg_test_int("set_rect 回写节点 h", lab->node ? lab->node->h : -1, 4);
    cg_widget_set_visible(lab, 0);
    cg_test_int("set_visible 后 getter", cg_widget_visible(lab), 0);
    cg_test_int("set_visible 回写节点", lab->node ? (int)lab->node->visible : -1, 0);
    cg_widget_set_enabled(chk, 0);
    cg_test_int("set_enabled 后 getter", cg_widget_enabled(chk), 0);
    cg_widget_set_checked(chk, 0);
    cg_test_int("set_checked 后 getter", cg_widget_checked(chk), 0);
    cg_test_int("set_checked 回写节点", chk->node ? (int)chk->node->checked : -1, 0);

    cg_test_note("---- set_value 三种夹取口径 ----");
    cg_widget_set_value(sld, 600);
    cg_test_int("slider 夹到 vmax=500", cg_widget_value(sld), 500);
    cg_widget_set_value(sld, -5);
    cg_test_int("slider 夹到 vmin=0", cg_widget_value(sld), 0);
    cg_widget_set_value(lst, 99);
    cg_test_int("listbox 下标夹到项数-1=2", cg_widget_value(lst), 2);
    cg_widget_set_value(lst, -1);
    cg_test_int("listbox 下标夹到 0", cg_widget_value(lst), 0);
    cg_widget_set_value(col, 0x6750A4);
    cg_test_int("colorpicker 值不夹取", cg_widget_value(col), 0x6750A4);
    cg_widget_set_value(edt, 250);
    cg_test_int("数值框夹到 vmax=100", cg_widget_value(edt), 100);
    cg_test_str("数值框 value 派生出 text", cg_widget_text(edt), "100");

    cg_test_note("---- set_value_range：vmax <= vmin 自动抬到 vmin+1 ----");
    cg_widget_set_value_range(sld, 10, 10);
    cg_test_int("range vmax 抬到 vmin+1", sld->vmax, 11);
    cg_widget_set_value(sld, 99);
    cg_test_int("新范围生效：夹到 11", cg_widget_value(sld), 11);

    free_app(&app);
}

CG_TEST_SUITE("core：控件读写与夹取契约（cg_widget.c 直测）", test_widget_core);
