/**
 * @file    test_action.c
 * @brief   cg_action.c 的单元测试：快捷键解析 + 动作表 + 引用点（文字/启用）
 * @details 为什么盯它：动作层把"一条命令"从三处绑定收成一处，代价是引入了三张
 *          松散约定 —— ① 快捷键文本的语法；② 引用点的两条规则（文字以动作为准、
 *          启用跟着动作走）；③ 三类必须报错但**不崩**的错配置。这三类的共同失效
 *          方式都是"安静地不生效"（配了 Ctrl+S 按不出来、动作禁用了按钮却还能点），
 *          所以这里全部钉死，包括"错误配置不能悄悄改动界面"。
 *          白盒：直接 include cg_internal.h 拿运行时结构，不需要真的开窗。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 快捷键解析 */

/** @brief 能认的写法：大小写、顺序、重复、空白 */
static void test_accel_parse_ok(void)
{
    cg_accel_t a;

    cg_test_note("---- 快捷键：能认的写法 ----");
    memset(&a, 0, sizeof(a));
    cg_test_int("\"Ctrl+S\" 能解析", cg_accel_parse("Ctrl+S", &a), 1);
    cg_test_int("  -> 键是 S", a.key, CG_ACCEL_A + ('S' - 'A'));
    cg_test_int("  -> 修饰键是 Ctrl", (int)a.mods, (int)CG_MOD_CTRL);

    cg_test_int("小写 \"ctrl+s\" 也认", cg_accel_parse("ctrl+s", &a), 1);
    cg_test_int("  -> 键是 S", a.key, CG_ACCEL_A + ('S' - 'A'));

    cg_test_int("顺序无关 \"Shift+Ctrl+Z\"", cg_accel_parse("Shift+Ctrl+Z", &a), 1);
    cg_test_int("  -> 两个修饰键都在",
                (int)a.mods, (int)(CG_MOD_CTRL | CG_MOD_SHIFT));
    cg_test_int("  -> 键是 Z", a.key, CG_ACCEL_A + ('Z' - 'A'));

    cg_test_int("重复的修饰键算一次 \"Ctrl+Ctrl+S\"", cg_accel_parse("Ctrl+Ctrl+S", &a), 1);
    cg_test_int("  -> 还是只有 Ctrl", (int)a.mods, (int)CG_MOD_CTRL);

    cg_test_int("功能键 \"Ctrl+F5\"", cg_accel_parse("Ctrl+F5", &a), 1);
    cg_test_int("  -> 键是 F5", a.key, CG_ACCEL_F1 + 4);

    cg_test_int("裸功能键 \"F5\" 允许", cg_accel_parse("F5", &a), 1);
    cg_test_int("  -> 没有修饰键", (int)a.mods, 0);

    cg_test_int("三个修饰键 \"Ctrl+Alt+Delete\"", cg_accel_parse("Ctrl+Alt+Delete", &a), 1);
    cg_test_int("  -> Ctrl+Alt", (int)a.mods, (int)(CG_MOD_CTRL | CG_MOD_ALT));
    cg_test_int("  -> 键是 Delete", a.key, CG_ACCEL_DELETE);

    cg_test_int("带空白的 \" Ctrl + S \"", cg_accel_parse(" Ctrl + S ", &a), 1);
    cg_test_int("  -> 键是 S", a.key, CG_ACCEL_A + ('S' - 'A'));

    cg_test_int("数字键 \"Ctrl+0\"", cg_accel_parse("Ctrl+0", &a), 1);
    cg_test_int("  -> 键是数字 0", a.key, CG_ACCEL_0);

    cg_test_int("越界参数不崩（out 传 NULL）", cg_accel_parse("Ctrl+S", NULL), 1);
}

/** @brief 不能认的写法：**尤其是裸字母/数字**（那会抢走输入框的按键） */
static void test_accel_parse_bad(void)
{
    cg_accel_t a;

    cg_test_note("---- 快捷键：拒绝的写法 ----");
    cg_test_int("空串", cg_accel_parse("", &a), 0);
    cg_test_int("NULL", cg_accel_parse(NULL, &a), 0);
    /* 这几条是**故意的**：加速键在焦点位于输入框时照样命中，
     * 裸 S 会让用户打不出 s，裸 Delete/Home 会毁掉编辑操作 */
    cg_test_int("裸字母 \"S\"（会抢走输入框的按键）", cg_accel_parse("S", &a), 0);
    cg_test_int("裸数字 \"5\"", cg_accel_parse("5", &a), 0);
    cg_test_int("裸具名键 \"Delete\"", cg_accel_parse("Delete", &a), 0);
    cg_test_int("裸 \"Esc\"", cg_accel_parse("Esc", &a), 0);
    cg_test_int("只有修饰键 \"Ctrl+Shift\"", cg_accel_parse("Ctrl+Shift", &a), 0);
    cg_test_int("尾随加号 \"Ctrl+\"", cg_accel_parse("Ctrl+", &a), 0);
    cg_test_int("空段 \"Ctrl++S\"", cg_accel_parse("Ctrl++S", &a), 0);
    cg_test_int("开头加号 \"+S\"", cg_accel_parse("+S", &a), 0);
    cg_test_int("不认识的修饰键 \"Foo+S\"", cg_accel_parse("Foo+S", &a), 0);
    cg_test_int("功能键越界 \"Ctrl+F25\"", cg_accel_parse("Ctrl+F25", &a), 0);
    cg_test_int("功能键 \"Ctrl+F0\"", cg_accel_parse("Ctrl+F0", &a), 0);
    cg_test_int("不认识的键名 \"Ctrl+阿\"", cg_accel_parse("Ctrl+阿", &a), 0);
    cg_test_int("超长串（不越界读）",
                cg_accel_parse("Ctrl+AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                               "AAAAAAAAAAAAAAAAAAAAAAA", &a), 0);
}

/** @brief 规范化显示：修饰键顺序固定、具名键用规范名 */
static void test_accel_format(void)
{
    char buf[64];

    cg_test_note("---- 快捷键：规范化显示 ----");
    cg_test_int("\"shift+ctrl+s\" 能格式化", cg_accel_format("shift+ctrl+s", buf, sizeof(buf)), 1);
    cg_test_str("  -> 修饰键顺序固定 Ctrl+Shift+S", buf, "Ctrl+Shift+S");
    cg_accel_format("Ctrl+Alt+F5", buf, sizeof(buf));
    cg_test_str("\"Ctrl+Alt+F5\"", buf, "Ctrl+Alt+F5");
    cg_accel_format("ctrl+escape", buf, sizeof(buf));
    cg_test_str("别名 \"escape\" 显示成规范名 Esc", buf, "Ctrl+Esc");
    cg_accel_format("Ctrl+PageUp", buf, sizeof(buf));
    cg_test_str("具名键 PageUp", buf, "Ctrl+PageUp");
    cg_accel_format("F12", buf, sizeof(buf));
    cg_test_str("裸功能键 F12", buf, "F12");

    buf[0] = 'x'; buf[1] = '\0';
    cg_test_int("解析不了的返回 0", cg_accel_format("S", buf, sizeof(buf)), 0);
    cg_test_str("  -> 并把缓冲清成空串", buf, "");
    cg_test_int("NULL 缓冲返回 0", cg_accel_format("Ctrl+S", NULL, 0), 0);
}

/* ------------------------------------------------------------ 文档解析 */

/** 干净的动作文档：3 条动作 + 控件引用 + 菜单项引用 */
static const char *k_doc =
    "{\n"
    "  \"app\": { \"title\": \"动作测试\", \"width\": 400, \"height\": 300 },\n"
    "  \"actions\": {\n"
    "    \"save\":  { \"text\": \"保存(&S)\", \"accel\": \"Ctrl+S\", \"handler\": \"on_save\" },\n"
    "    \"grid\":  { \"text\": \"显示网格\", \"handler\": \"on_grid\",\n"
    "               \"checked\": true, \"enabled\": false },\n"
    "    \"plain\": { \"handler\": \"on_grid\" }\n"
    "  },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"btn_save\", \"type\": \"button\", \"text\": \"存盘\", \"action\": \"save\" },\n"
    "    { \"id\": \"btn_grid\", \"type\": \"button\", \"text\": \"网格\", \"action\": \"grid\" },\n"
    "    { \"id\": \"lab_free\", \"type\": \"label\", \"text\": \"普通标签\" },\n"
    "    { \"id\": \"mnu_main\", \"type\": \"menu\", \"nodes\": [\n"
    "        { \"text\": \"文件(&F)\", \"children\": [\n"
    "          { \"text\": \"项自己的文字\", \"id\": \"m_save\", \"action\": \"save\" },\n"
    "          { \"text\": \"网格项\", \"id\": \"m_grid\", \"action\": \"grid\" },\n"
    "          { \"text\": \"-\" }\n"
    "        ] }\n"
    "    ] }\n"
    "  ]\n"
    "}\n";

/** @brief 在文档的动作表里按名字找一个（顺序无关，改测试数据不会连带改断言） */
static const cg_action_t *action_by_name(const cg_doc_t *doc, const char *name)
{
    if (!doc) return NULL;
    for (size_t i = 0; i < doc->nactions; i++) {
        if (doc->actions[i].name && strcmp(doc->actions[i].name, name) == 0) return &doc->actions[i];
    }
    return NULL;
}

/** @brief 动作字段的解析：写成什么、缺省是什么 */
static void test_action_parse(void)
{
    char      err[256];
    cg_doc_t *doc;
    const cg_action_t *a;
    const cg_node_t   *w;

    cg_test_note("---- .cgui 的 actions 字段 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }
    cg_test_int("动作条数 3", (long long)doc->nactions, 3);

    a = action_by_name(doc, "save");
    cg_test_ok("有 save 动作", a != NULL);
    if (a) {
        cg_test_str("  text", a->text, "保存(&S)");
        cg_test_str("  accel", a->accel, "Ctrl+S");
        cg_test_str("  handler", a->handler, "on_save");
        cg_test_int("  checked 缺省 0", a->checked, 0);
        cg_test_int("  enabled 缺省 1", a->enabled, 1);
    }

    a = action_by_name(doc, "grid");
    cg_test_ok("有 grid 动作", a != NULL);
    if (a) {
        cg_test_int("  checked = 1", a->checked, 1);
        cg_test_int("  enabled = 0", a->enabled, 0);
        cg_test_int("  没写 accel（NULL）", a->accel == NULL, 1);
    }

    a = action_by_name(doc, "plain");
    cg_test_ok("有 plain 动作", a != NULL);
    if (a) cg_test_int("  没写 text（NULL）", a->text == NULL, 1);

    cg_test_note("---- 引用点：控件上的 action ----");
    w = doc->root->children[0];
    cg_test_str("btn_save 的 action", w->action, "save");
    w = doc->root->children[2];
    cg_test_str("lab_free 没有 action", w->action, NULL);

    cg_test_note("---- 引用点：菜单项上的 action ----");
    {
        const cg_node_t *mnu = doc->root->children[3];
        const cg_item_t *it  = (mnu && mnu->nnodes) ? mnu->nodes[0] : NULL;
        const cg_item_t *sub = (it && it->nchildren) ? it->children[0] : NULL;

        cg_test_ok("菜单节点在", sub != NULL);
        if (sub) {
            cg_test_str("第一项的 id", sub->id, "m_save");
            cg_test_str("第一项的 action", sub->action, "save");
        }
    }
    cg_doc_free(doc);
}

/** @brief 边界：空名、重名、缺 handler、坏 accel、actions 不是对象 */
static void test_action_parse_edge(void)
{
    char      err[256];
    cg_doc_t *doc;
    const cg_action_t *a;

    cg_test_note("---- 空名条目被跳过、重名后者覆盖 ----");
    doc = cg_doc_parse("{\"actions\":{\"\":{\"text\":\"没名字\",\"handler\":\"on_save\"},"
                       "\"dup\":{\"text\":\"第一次\",\"handler\":\"on_save\"},"
                       "\"dup\":{\"text\":\"第二次\",\"handler\":\"on_save\"}},"
                       "\"widgets\":[]}",
                       err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (doc) {
        cg_test_int("空名不占条目", (long long)doc->nactions, 1);
        a = action_by_name(doc, "dup");
        cg_test_ok("重名合成一条", a != NULL);
        if (a) cg_test_str("  -> 后者覆盖前者", a->text, "第二次");
        cg_doc_free(doc);
    }

    cg_test_note("---- 缺 handler 不报错、不丢弃（留给自检） ----");
    doc = cg_doc_parse("{\"actions\":{\"nohandler\":{\"text\":\"没有 handler\"}},\"widgets\":[]}",
                       err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (doc) {
        cg_test_int("条目还在", (long long)doc->nactions, 1);
        a = action_by_name(doc, "nohandler");
        if (a) cg_test_int("handler 是 NULL", a->handler == NULL, 1);
        cg_doc_free(doc);
    }

    cg_test_note("---- actions 不是对象 / 根本没有 actions ----");
    doc = cg_doc_parse("{\"actions\":[1,2,3],\"widgets\":[]}", err, sizeof(err));
    cg_test_ok("数组写法不报错", doc != NULL);
    if (doc) {
        cg_test_int("当成没有动作", (long long)doc->nactions, 0);
        cg_doc_free(doc);
    }
    doc = cg_doc_parse("{\"widgets\":[{\"id\":\"b\",\"type\":\"button\"}]}", err, sizeof(err));
    cg_test_ok("老 .cgui 照旧能读", doc != NULL);
    if (doc) {
        cg_test_int("没有动作表", (long long)doc->nactions, 0);
        cg_test_int("控件不带 action", doc->root->children[0]->action == NULL, 1);
        cg_doc_free(doc);
    }
}

/* ------------------------------------------------------------ 运行时动作表 */

/** 被调用的动作名（回调里用 cg_action_name(self) 取） */
static char s_last_action[64];
static int  s_save_calls;
static int  s_grid_calls;

/** @brief on_save 的动作回调：self 恒为主窗口伪控件 */
static void on_save(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *name;
    (void)user;
    s_save_calls++;
    cg_test_int("  动作回调的 ev 恒为 click", (long long)ev, (long long)CG_EVENT_CLICK);
    cg_test_ok("  动作回调的 self 是窗口伪控件", self && cg_widget_id(self) &&
               strcmp(cg_widget_id(self), "__window") == 0);
    name = cg_action_name(self);
    snprintf(s_last_action, sizeof(s_last_action), "%s", name ? name : "(NULL)");
}

/** @brief on_grid 的动作回调 */
static void on_grid(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self; (void)ev; (void)user;
    s_grid_calls++;
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_save", on_save),
    CG_BIND("on_grid", on_grid),
    CG_BIND_END
};

/** @brief 解析文档 + 建控件树 + 建动作表；失败返回 -1 */
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
    app->bindings = k_bindings;
    app->doc = *doc;      /* app->doc 是内嵌结构，所有权转移 */
    free(doc);
    cg_build_widgets(app);
    cg_action_build(app);
    return 0;
}

/** @brief 收尾：动作表自己持有一份字符串，要和控件一起收 */
static void free_app(cg_app_t *app)
{
    cg_widget_t *w = app->widgets;

    cg_action_release(app);
    while (w) {
        cg_widget_t *next = w->next;
        cg_widget_free(w);
        w = next;
    }
    app->widgets = NULL;
    cg_doc_release(&app->doc);
}

/** @brief 动作表构建：accel 归一化、发号、初始状态 */
static void test_action_build(void)
{
    cg_app_t app;

    cg_test_note("---- 运行时动作表 ----");
    s_save_calls = 0;
    s_grid_calls = 0;
    s_last_action[0] = '\0';
    if (build_app(&app, k_doc) != 0) return;

    cg_test_int("动作条数 3", app.nactions, 3);
    cg_test_int("没有配置错误", app.bad_actions, 0);

    cg_test_str("cg_action_at(0)", cg_action_at(&app, 0), "save");
    cg_test_str("cg_action_at(1)", cg_action_at(&app, 1), "grid");
    cg_test_int("cg_action_at(99) 越界返回 NULL", cg_action_at(&app, 99) == NULL, 1);
    cg_test_int("cg_action_count", cg_action_count(&app), 3);

    cg_test_str("cg_action_accel(save)", cg_action_accel(&app, "save"), "Ctrl+S");
    cg_test_str("cg_action_accel(grid) 没快捷键回空串", cg_action_accel(&app, "grid"), "");
    cg_test_str("cg_action_accel(不存在) 回空串", cg_action_accel(&app, "没有"), "");

    cg_test_int("save 初始启用", cg_action_is_enabled(&app, "save"), 1);
    cg_test_int("grid 声明成禁用", cg_action_is_enabled(&app, "grid"), 0);
    cg_test_int("grid 声明成勾选", cg_action_is_checked(&app, "grid"), 1);
    cg_test_int("save 初始没勾选", cg_action_is_checked(&app, "save"), 0);
    cg_test_int("不存在的动作 is_enabled 回 0", cg_action_is_enabled(&app, "没有"), 0);

    cg_test_note("---- 声明式状态落到引用点 ----");
    {
        cg_widget_t *btn = cg_find(&app, "btn_save");
        cg_widget_t *grid = cg_find(&app, "btn_grid");
        cg_widget_t *lab = cg_find(&app, "lab_free");
        cg_widget_t *mnu = cg_find(&app, "mnu_main");

        cg_test_str("按钮文字被动作 text 覆盖", cg_widget_text(btn), "保存(&S)");
        cg_test_str("没被引用的控件文字不动", cg_widget_text(lab), "普通标签");
        cg_test_int("引用禁用动作的按钮从第一帧起就是灰的", cg_widget_enabled(grid), 0);
        cg_test_int("引用启用动作的按钮可用", cg_widget_enabled(btn), 1);

        cg_test_ok("菜单控件在", mnu != NULL);
        if (mnu) {
            cg_test_int("菜单项 4 条（下拉 + 2 项 + 分隔线）", mnu->nmenu, 4);
            cg_test_str("菜单项文字也被动作覆盖", mnu->menu[1].text, "保存(&S)");
            cg_test_str("第二条菜单项", mnu->menu[2].text, "显示网格");
            cg_test_str("分隔线保持原样", mnu->menu[3].text, "-");
            cg_test_int("可点的项才有菜单命令 id", mnu->menu[1].cmd != 0, 1);
            cg_test_int("下拉项没有命令 id", mnu->menu[0].cmd, 0);
        }
    }

    cg_test_note("---- 触发：按名字 / 按快捷键命令 id ----");
    cg_test_int("cg_action_invoke(save)", cg_action_invoke(&app, "save"), 1);
    cg_test_int("  -> 回调跑了一次", s_save_calls, 1);
    cg_test_str("  -> cg_action_name(self) 给的是 save", s_last_action, "save");

    cg_test_int("不存在的动作返回 0", cg_action_invoke(&app, "没有"), 0);
    cg_test_int("NULL 名字返回 0", cg_action_invoke(&app, NULL), 0);
    cg_test_int("禁用的动作返回 0", cg_action_invoke(&app, "grid"), 0);
    cg_test_int("  -> 回调没跑", s_grid_calls, 0);

    cg_test_int("按快捷键命令 id 触发",
                cg_action_invoke_cmd(&app, app.actions[0].accel_cmd), 1);
    cg_test_int("  -> 回调又跑了一次", s_save_calls, 2);
    cg_test_int("没快捷键的动作没有命令 id", app.actions[1].accel_cmd, 0);
    cg_test_int("不是动作的 id 返回 0", cg_action_invoke_cmd(&app, 1), 0);
    cg_test_int("id 0 返回 0", cg_action_invoke_cmd(&app, 0), 0);

    cg_test_note("---- 运行时改启用：引用它的控件一起变 ----");
    cg_action_set_enabled(&app, "save", 0);
    cg_test_int("动作已禁用", cg_action_is_enabled(&app, "save"), 0);
    cg_test_int("按钮跟着灰", cg_widget_enabled(cg_find(&app, "btn_save")), 0);
    cg_test_int("禁用后触发返回 0", cg_action_invoke(&app, "save"), 0);
    cg_test_int("  -> 回调没再跑", s_save_calls, 2);

    cg_action_set_enabled(&app, "save", 1);
    cg_test_int("恢复启用后按钮也能点", cg_widget_enabled(cg_find(&app, "btn_save")), 1);
    cg_test_int("恢复后触发成功", cg_action_invoke(&app, "save"), 1);
    cg_test_int("  -> 回调跑第三次", s_save_calls, 3);

    cg_test_note("---- 勾选：无窗口时是个空操作（不崩） ----");
    cg_action_set_checked(&app, "grid", 0);
    cg_test_int("状态记下了", cg_action_is_checked(&app, "grid"), 0);
    cg_action_set_checked(&app, "没有这个动作", 1);   /* 不存在的动作：空操作 */
    cg_action_set_enabled(&app, "没有这个动作", 0);
    cg_test_ok("对不存在的动作设状态不崩", 1);

    free_app(&app);
}

/** 错配置文档：三类问题各来一个 */
static const char *k_bad_doc =
    "{\n"
    "  \"actions\": {\n"
    "    \"nohandler\": { \"text\": \"没 handler\" },\n"
    "    \"badaccel\":  { \"text\": \"坏快捷键\", \"accel\": \"S\", \"handler\": \"on_save\" }\n"
    "  },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"btn_missing\", \"type\": \"button\", \"text\": \"x\", \"action\": \"没有这个动作\" },\n"
    "    { \"id\": \"lab_noclick\", \"type\": \"label\", \"text\": \"y\", \"action\": \"nohandler\" },\n"
    "    { \"id\": \"btn_both\", \"type\": \"button\", \"text\": \"z\", \"action\": \"nohandler\",\n"
    "      \"events\": { \"click\": \"on_save\" } }\n"
    "  ]\n"
    "}\n";

/** @brief 错配置：四类都要记账（自检据此非 0 退出），但界面不能崩、也不能被悄悄改 */
static void test_action_bad_refs(void)
{
    cg_app_t app;

    cg_test_note("---- 错配置：全部记账，且不悄悄改动界面 ----");
    if (build_app(&app, k_bad_doc) != 0) return;

    /* 1) 动作引用了不存在的动作名  2) 类型不支持 click  3) 有 action 又绑了 click
     * 4) 缺 handler  5) accel 解析不了 */
    cg_test_int("五类问题各记一笔", app.bad_actions, 5);

    cg_test_int("缺 handler 的动作触发不了", cg_action_invoke(&app, "nohandler"), 0);
    cg_test_str("缺 handler 的动作没有快捷键", cg_action_accel(&app, "nohandler"), "");
    cg_test_str("坏 accel 被忽略、动作本身还在", cg_action_accel(&app, "badaccel"), "");
    cg_test_int("坏 accel 的动作照样能触发", cg_action_invoke(&app, "badaccel"), 1);

    cg_test_str("引用不存在的动作：文字不动", cg_widget_text(cg_find(&app, "btn_missing")), "x");
    cg_test_str("不支持 click 的控件：文字不动",
                cg_widget_text(cg_find(&app, "lab_noclick")), "y");

    free_app(&app);
}

CG_TEST_SUITE("动作：快捷键解析（能认的）", test_accel_parse_ok);
CG_TEST_SUITE("动作：快捷键解析（拒绝的）", test_accel_parse_bad);
CG_TEST_SUITE("动作：快捷键规范化显示", test_accel_format);
CG_TEST_SUITE("动作：.cgui 字段解析", test_action_parse);
CG_TEST_SUITE("动作：解析边界（空名/重名/缺 handler）", test_action_parse_edge);
CG_TEST_SUITE("动作：运行时动作表与引用点", test_action_build);
CG_TEST_SUITE("动作：错配置记账", test_action_bad_refs);
