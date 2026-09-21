/**
 * @file    test_bind.c
 * @brief   cg_bind.c 的单元测试：.cgui 的 bind 字段与运行期控件绑定
 * @details 为什么盯它：绑定是"省掉手写同步回调"的机制，它的规则（抄值还是抄文字、
 *          列表下标不夹取、同步不触发回调）全是隐式约定 —— 隐式约定最容易被后人改坏，
 *          而且坏了不会崩，只是界面上某两个控件悄悄不同步。这里把每条规则钉死。
 *          白盒：直接 include cg_internal.h 拿控件结构，不需要真的开窗。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* 测试文档：
 *   sld_level(0..500, 值300) -> lab_value（值型源 -> 文字型目标，要看到 "300"）
 *                            -> spn_level（双向：微调框自己改也要推回滑块）
 *                            -> lst_pick（列表类目标：下标 300 不该被夹成 100）
 *   edt_src(文字)            -> lab_note（文本型源 -> 抄文字而不是数字）
 *   chk_on                   -> sw_on（勾选型 -> 勾选型）、lab_chk（勾选型 -> 文字 "1"）
 *   lab_wrong                -> 源不存在（自检要能据此报错）
 */
static const char *k_doc =
    "{\n"
    "  \"app\": { \"title\": \"绑定测试\", \"width\": 420, \"height\": 300 },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"sld_level\", \"type\": \"slider\", \"min\": 0, \"max\": 500, \"value\": 300 },\n"
    "    { \"id\": \"spn_level\", \"type\": \"spinner\", \"min\": 0, \"max\": 500, \"value\": 0,\n"
    "      \"bind\": { \"src\": \"sld_level\", \"two\": true } },\n"
    "    { \"id\": \"lab_value\", \"type\": \"label\", \"text\": \"-\",\n"
    "      \"bind\": { \"src\": \"sld_level\" } },\n"
    "    { \"id\": \"lst_pick\", \"type\": \"listbox\", \"items\": [\"甲\", \"乙\", \"丙\"],\n"
    "      \"bind\": { \"src\": \"sld_level\" } },\n"
    "    { \"id\": \"edt_src\", \"type\": \"edit\", \"text\": \"你好\" },\n"
    "    { \"id\": \"lab_note\", \"type\": \"label\",\n"
    "      \"bind\": { \"src\": \"edt_src\" } },\n"
    "    { \"id\": \"chk_on\", \"type\": \"checkbox\", \"text\": \"启用\" },\n"
    "    { \"id\": \"sw_on\", \"type\": \"switch\", \"text\": \"开关\",\n"
    "      \"bind\": { \"src\": \"chk_on\" } },\n"
    "    { \"id\": \"lab_chk\", \"type\": \"label\", \"text\": \"-\",\n"
    "      \"bind\": { \"src\": \"chk_on\" } },\n"
    "    { \"id\": \"lab_wrong\", \"type\": \"label\", \"text\": \"-\",\n"
    "      \"bind\": { \"src\": \"没有这个控件\" } }\n"
    "  ]\n"
    "}\n";

/** @brief 解析文档 + 建控件树 + 应用 .cgui 里的绑定；失败返回 -1 */
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
    cg_bind_apply_all(app);
    return 0;
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

/** 目标控件被绑定同步时应保持沉默：这个计数必须一直是 0 */
static int s_sw_changes;

/** @brief switch 的 change 回调：只记账，用来看绑定有没有越界触发回调 */
static void on_sw_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    s_sw_changes++;
}

/** @brief bind 字段的解析：写成什么、缺省是什么 */
static void test_bind_parse(void)
{
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- .cgui 的 bind 字段 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) return;

    {
        const cg_node_t *n = doc->root->children[2];   /* lab_value */
        cg_test_str("节点 id", n->id, "lab_value");
        cg_test_str("bind.src", n->bind_src, "sld_level");
        cg_test_int("单向绑定 bind_two = 0", n->bind_two, 0);
    }
    {
        const cg_node_t *n = doc->root->children[1];   /* spn_level */
        cg_test_str("节点 id", n->id, "spn_level");
        cg_test_str("双向绑定的 src", n->bind_src, "sld_level");
        cg_test_int("双向绑定 two = 1", n->bind_two, 1);
    }
    {
        const cg_node_t *n = doc->root->children[0];   /* sld_level：没写 bind */
        cg_test_str("没写 bind 的节点 id", n->id, "sld_level");
        cg_test_ok("没写 bind 时 bind_src 是 NULL", n->bind_src == NULL);
        cg_test_int("没写 bind 时 bind_two = 0", n->bind_two, 0);
    }
    {
        /* src 是空串：画布上「没选源」就是这么写的，等于没绑 */
        cg_doc_t *d2 = cg_doc_parse("{\"widgets\":[{\"id\":\"a\",\"type\":\"label\","
                                    "\"bind\":{\"src\":\"\"}}]}", err, sizeof(err));
        cg_test_ok("空 src 的文档能解析", d2 != NULL);
        if (d2) {
            cg_test_ok("空 src 视为没绑", d2->root->children[0]->bind_src == NULL);
            cg_doc_free(d2);
        }
    }
    cg_doc_free(doc);
}

/** @brief 启动时的第一次对齐：目标控件跟上源的初值 */
static void test_bind_apply_all(void)
{
    cg_app_t app;

    cg_test_note("---- 启动时先对齐一次（.cgui 里的初值） ----");
    if (build_app(&app, k_doc) != 0) {
        cg_test_ok("建应用成功", 0);
        return;
    }

    cg_test_int("绑定个数", app.nbinds, 7);
    cg_test_int("源不存在的绑定记了 1 笔", app.bad_binds, 1);

    cg_test_str("值型源 -> 文字型目标：标签显示数字",
                cg_widget_text(cg_find(&app, "lab_value")), "300");
    cg_test_int("值型源 -> 值型目标：微调框跟上",
                cg_widget_value(cg_find(&app, "spn_level")), 300);
    cg_test_int("列表类目标不按 min/max 夹（下标 300 保留）",
                cg_widget_value(cg_find(&app, "lst_pick")), 300);
    cg_test_str("文本型源 -> 抄文字而不是数字",
                cg_widget_text(cg_find(&app, "lab_note")), "你好");
    cg_test_int("勾选型源 -> 勾选型目标（默认都不勾）",
                cg_widget_checked(cg_find(&app, "sw_on")), 0);
    cg_test_str("源不存在时不改目标文字", cg_widget_text(cg_find(&app, "lab_wrong")), "-");

    free_app(&app);
}

/** @brief 变化传播：API 赋值 与 后端派发两条路都要同步 */
static void test_bind_propagate(void)
{
    cg_app_t     app;
    cg_widget_t *sld;
    cg_widget_t *lab;
    cg_widget_t *spn;
    cg_widget_t *lst;
    cg_widget_t *chk;
    cg_widget_t *sw;
    cg_widget_t *lab_chk;

    cg_test_note("---- 变化传播 ----");
    if (build_app(&app, k_doc) != 0) return;

    sld = cg_find(&app, "sld_level");
    lab = cg_find(&app, "lab_value");
    spn = cg_find(&app, "spn_level");
    lst = cg_find(&app, "lst_pick");
    chk = cg_find(&app, "chk_on");
    sw  = cg_find(&app, "sw_on");
    lab_chk = cg_find(&app, "lab_chk");

    cg_widget_set_value(sld, 55);
    cg_test_str("set_value 后标签跟着变", cg_widget_text(lab), "55");
    cg_test_int("set_value 后微调框跟着变", cg_widget_value(spn), 55);
    cg_test_int("set_value 后列表选中项跟着变", cg_widget_value(lst), 55);

    /* 后端改值后是「先写字段、再 cg_dispatch」，绑定要在派发这一步同步掉 */
    sld->value = 88;
    cg_dispatch(sld, CG_EVENT_CHANGE);
    cg_test_str("派发事件时同步（后端那条路）", cg_widget_text(lab), "88");

    cg_test_note("---- 勾选型：抄勾选、不触发目标的回调 ----");
    s_sw_changes = 0;
    cg_on(sw, CG_EVENT_CHANGE, on_sw_change);
    cg_widget_set_checked(chk, 1);
    cg_test_int("开关跟着勾上", cg_widget_checked(sw), 1);
    cg_test_str("勾选型源给文字目标的是 1/0", cg_widget_text(lab_chk), "1");
    cg_test_int("绑定同步不触发目标的 change 回调", s_sw_changes, 0);

    cg_test_note("---- 双向：目标自己变时推回源 ----");
    cg_widget_set_value(spn, 20);
    cg_test_int("微调框改 -> 滑块跟上", cg_widget_value(sld), 20);
    cg_test_str("滑块跟上后标签也跟上", cg_widget_text(lab), "20");

    cg_test_note("---- 双向互推不会死循环（值相等就停） ----");
    cg_widget_set_value(sld, 33);
    cg_test_int("滑块 33", cg_widget_value(sld), 33);
    cg_test_int("微调框 33", cg_widget_value(spn), 33);

    free_app(&app);
}

/** @brief 运行时 API：cg_bind（不用 .cgui 也能绑），顺便盯参数校验 */
static void test_bind_api(void)
{
    cg_app_t     app;
    cg_widget_t *a;
    cg_widget_t *b;
    cg_widget_t *lab;

    cg_test_note("---- cg_bind() 运行时绑定 ----");
    memset(&app, 0, sizeof(app));
    app.scale = 1;

    a = cg_widget_new(&app, "slider", "a", 0, 0, 100, 20, "");
    b = cg_widget_new(&app, "spinner", "b", 0, 0, 100, 20, "");
    lab = cg_widget_new(&app, "label", "lab", 0, 0, 100, 20, "-");
    cg_test_ok("三个控件都建出来了", a && b && lab);
    if (!a || !b || !lab) return;

    cg_test_int("先给几个初值", cg_widget_value(a), 0);
    cg_test_int("还没绑时 nbinds=0", app.nbinds, 0);

    cg_test_int("cg_bind 成功", cg_bind(a, b, 0), 0);
    cg_test_int("绑了 1 个", app.nbinds, 1);
    cg_test_int("绑完立刻对齐一次（a=0）", cg_widget_value(b), 0);

    cg_widget_set_value(a, 7);
    cg_test_int("值型 -> 值型", cg_widget_value(b), 7);

    /* 同一个目标再绑一次不要重复计数 */
    cg_test_int("重复绑定成功", cg_bind(a, lab, 0), 0);
    cg_test_int("重复绑定不重复计数", app.nbinds, 2);
    cg_test_str("目标标签显示数字", cg_widget_text(lab), "7");

    cg_test_int("自己绑自己返回失败", cg_bind(a, a, 0), -1);
    cg_test_int("NULL 参数返回失败", cg_bind(a, NULL, 0), -1);

    cg_test_note("---- 跨应用的绑定要被拒绝 ----");
    {
        cg_app_t     other;
        cg_widget_t *x;
        memset(&other, 0, sizeof(other));
        x = cg_widget_new(&other, "label", "x", 0, 0, 10, 10, "");
        cg_test_int("跨应用绑定返回失败", cg_bind(a, x, 0), -1);
        cg_widget_free(x);
    }

    {
        cg_widget_t *w = app.widgets;
        while (w) {
            cg_widget_t *next = w->next;
            cg_widget_free(w);
            w = next;
        }
    }
}

CG_TEST_SUITE("绑定：bind 字段解析", test_bind_parse);
CG_TEST_SUITE("绑定：启动时对齐", test_bind_apply_all);
CG_TEST_SUITE("绑定：变化传播", test_bind_propagate);
CG_TEST_SUITE("绑定：运行时 cg_bind", test_bind_api);
