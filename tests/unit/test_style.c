/**
 * @file    test_style.c
 * @brief   命名样式（.cgui 顶层 styles{}）与 style 的三种形态
 * @details 盯它的理由：这一层的错法**全是"安静地不对"** —— 合并顺序反了、把"没写某个字段"
 *          当成"写成默认值"，界面依旧能起来，只是某个控件的字号/颜色悄悄不对。
 *          其中最要命的一条是**内联优先**：命名样式打底，只填控件自己没写的字段；
 *          如果把整份内联对象无脑覆盖上去，命名样式里那些"控件没写"的字段就全被默认值顶掉了
 *          （表现出来就是"我明明引用了样式，颜色却是黑的"）。
 *
 *          白盒：直接 include cg_doc.h 拿节点与样式结构，不需要开窗。
 */
#include "cg_test.h"
#include "cg_doc.h"
#include "cg_internal.h"     /* cg_app_t 的完整定义（深色那两个 API 要一个 app） */

#include <stdio.h>
#include <string.h>

/**
 * @brief 解析一段 .cgui 文本（失败时打一行说明）
 * @param json 待解析的 .cgui 文本
 * @return cg_doc_t* 解析出的文档；失败返回 NULL（此时会打一行说明，不计入断言数）
 */
static cg_doc_t *parse(const char *json)
{
    char      err[256] = { 0 };
    cg_doc_t *doc = cg_doc_parse(json, err, sizeof(err));

    if (!doc) cg_test_note(err);
    return doc;
}

/**
 * @brief 取顶层第 idx 个控件
 * @param doc 已解析的文档
 * @param idx 顶层子控件下标（从 0 开始）
 * @return cg_node_t* 第 idx 个顶层控件；越界返回 NULL
 */
static cg_node_t *kid(cg_doc_t *doc, size_t idx)
{
    if (!doc || !doc->root || idx >= doc->root->nchildren) return NULL;
    return doc->root->children[idx];
}

/**
 * @brief 样式表 + 三种引用形态 + 合并顺序（内联优先）
 * @return void 无返回值
 */
static void test_style_forms(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"styles\": {"
        "    \"body\": { \"font\": \"Consolas\", \"size\": 9, \"fg\": \"#00FF00\" },"
        "    \"title\": { \"font\": \"Microsoft YaHei UI\", \"size\": 12, \"bold\": true },"
        "    \"dup\": { \"size\": 3 }, \"dup\": { \"size\": 7 },"
        "    \"bad\": 42"
        "  },"
        "  \"widgets\": ["
        "    { \"id\": \"a_inline\", \"type\": \"label\", \"style\": { \"size\": 11 } },"
        "    { \"id\": \"b_named\", \"type\": \"label\", \"style\": \"body\" },"
        "    { \"id\": \"c_mix\", \"type\": \"label\", \"style\": { \"use\": \"body\", \"size\": 20 } },"
        "    { \"id\": \"d_missing\", \"type\": \"label\", \"style\": { \"use\": \"nope\", \"size\": 8 } },"
        "    { \"id\": \"e_dup\", \"type\": \"label\", \"style\": \"dup\" },"
        "    { \"id\": \"f_default\", \"type\": \"label\", \"style\": { \"size\": 0 } },"
        "    { \"id\": \"g_nostyle\", \"type\": \"label\" }"
        "  ] }");

    cg_test_ok("带 styles{} 的文档能解析", doc != NULL);
    if (!doc) return;

    /* 4 条 = body / title / dup×2；那条 "bad": 42（值不是对象）被**跳过**，
     * 所以这条断言同时证明"非法条目不会让整份文档失败"。 */
    cg_test_int("样式表读到 4 条（值不是对象的那条被跳过）", (long long)doc->nstyles, 4);

    cg_test_note("---- 形态一：内联（行为与以前一致） ----");
    cg_test_int("内联 size = 11", kid(doc, 0)->style.size, 11);
    cg_test_ok("  且没有引用名字", kid(doc, 0)->style_name == NULL);
    cg_test_int("  没写的字段仍是默认（size 之外的 font 为空）", kid(doc, 0)->style.font == NULL, 1);

    cg_test_note("---- 形态二：纯命名引用 ----");
    cg_test_str("文字来自命名样式", kid(doc, 1)->style.font, "Consolas");
    cg_test_int("  字号", kid(doc, 1)->style.size, 9);
    cg_test_int("  前景色", (long long)kid(doc, 1)->style.fg, 0x00FF00);

    cg_test_note("---- 形态三：命名 + 局部覆盖（**内联优先**，其余字段必须保住） ----");
    cg_test_str("字体仍是命名样式里的", kid(doc, 2)->style.font, "Consolas");
    cg_test_int("  字号被内联覆盖成 20", kid(doc, 2)->style.size, 20);
    cg_test_int("  **没被覆盖的前景色仍在**（这条是最容易写错的）",
                (long long)kid(doc, 2)->style.fg, 0x00FF00);

    cg_test_note("---- use 指向不存在的名字：当没写 use，不报错 ----");
    cg_test_int("  只用内联的 size", kid(doc, 3)->style.size, 8);
    cg_test_ok("  字体没被谁填上", kid(doc, 3)->style.font == NULL);

    cg_test_note("---- 样式表重名：后者覆盖 ----");
    cg_test_int("dup 取的是后写的 7", kid(doc, 4)->style.size, 7);

    cg_test_note("---- 关键区别：**写了默认值** 与 **没写** 不是一回事 ----");
    {
        cg_doc_t *d2 = parse(
            "{ \"cgui\": 1, \"app\": { \"width\": 300, \"height\": 200 },"
            "  \"styles\": { \"s\": { \"font\": \"Consolas\", \"size\": 9 } },"
            "  \"widgets\": [ { \"id\": \"x\", \"type\": \"label\", \"style\": { \"use\": \"s\", \"size\": 0 } } ] }");

        cg_test_ok("第二份文档能解析", d2 != NULL);
        if (d2) {
            cg_test_int("显式写了 size:0 就把命名样式里的 9 顶掉（0 = 用系统字体）",
                        kid(d2, 0)->style.size, 0);
            cg_test_str("  而这一个字段被顶掉**不影响**其它字段：字体仍是命名样式里的",
                        kid(d2, 0)->style.font, "Consolas");
            cg_doc_free(d2);
        }
    }

    cg_test_note("---- 没有任何样式表时，引用名字不报错、全默认 ----");
    {
        cg_doc_t *d3 = parse(
            "{ \"cgui\": 1, \"app\": { \"width\": 300, \"height\": 200 },"
            "  \"widgets\": [ { \"id\": \"x\", \"type\": \"label\", \"style\": \"ghost\" } ] }");

        cg_test_ok("没有 styles 也能解析", d3 != NULL);
        if (d3) {
            cg_test_int("样式是全默认（size 0）", kid(d3, 0)->style.size, 0);
            cg_test_ok("  字体为空", kid(d3, 0)->style.font == NULL);
            cg_doc_free(d3);
        }
    }

    cg_doc_free(doc);
}

/**
 * @brief 子控件里的引用也要生效（递归解析到整棵树）
 * @return void 无返回值
 */
static void test_style_nested(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"styles\": { \"inner\": { \"size\": 14, \"bold\": true } },"
        "  \"widgets\": ["
        "    { \"id\": \"grp\", \"type\": \"group\", \"children\": ["
        "      { \"id\": \"deep\", \"type\": \"label\", \"style\": \"inner\" },"
        "      { \"id\": \"deeper\", \"type\": \"group\", \"children\": ["
        "        { \"id\": \"deepest\", \"type\": \"label\", \"style\": { \"use\": \"inner\", \"bold\": false } } ] } ] }"
        "  ] }");

    cg_node_t *grp;

    cg_test_ok("嵌套文档能解析", doc != NULL);
    if (!doc) return;
    grp = kid(doc, 0);
    cg_test_int("第 1 层子控件拿到命名样式", grp->children[0]->style.size, 14);
    cg_test_int("  粗体也在", grp->children[0]->style.bold, 1);
    cg_test_int("第 2 层也拿到", grp->children[1]->children[0]->style.size, 14);
    cg_test_int("  且局部覆盖生效（bold 覆盖成 0）", grp->children[1]->children[0]->style.bold, 0);

    cg_doc_free(doc);
}

/**
 * @brief 深色标志三态与垃圾值
 * @return void 无返回值
 */
static void test_dark_flag(void)
{
    cg_doc_t *d1 = parse("{ \"cgui\": 1, \"app\": { \"dark\": true }, \"widgets\": [] }");
    cg_doc_t *d2 = parse("{ \"cgui\": 1, \"app\": { \"dark\": \"auto\" }, \"widgets\": [] }");
    cg_doc_t *d3 = parse("{ \"cgui\": 1, \"app\": { \"dark\": false }, \"widgets\": [] }");
    cg_doc_t *d4 = parse("{ \"cgui\": 1, \"app\": {}, \"widgets\": [] }");
    cg_doc_t *d5 = parse("{ \"cgui\": 1, \"app\": { \"dark\": \"yes please\" }, \"widgets\": [] }");

    cg_test_int("dark: true → 1", d1 ? d1->dark : -99, 1);
    cg_test_int("dark: \"auto\" → -1（由后端去读系统主题）", d2 ? d2->dark : -99, -1);
    cg_test_int("dark: false → 0", d3 ? d3->dark : -99, 0);
    cg_test_int("不写 → 0（不改变现有工程外观）", d4 ? d4->dark : -99, 0);
    cg_test_int("垃圾字符串 → 0（不当成 auto）", d5 ? d5->dark : -99, 0);

    if (d1) cg_doc_free(d1);
    if (d2) cg_doc_free(d2);
    if (d3) cg_doc_free(d3);
    if (d4) cg_doc_free(d4);
    if (d5) cg_doc_free(d5);
}

/**
 * @brief **值取不到的字段算"没写"**：不许把命名样式里的好值顶掉
 * @return void 无返回值
 * @note 契约原先只把对齐写成这个口径，字体 / 前景 / 背景是"键存在即算写了" ——
 *       那是实现顺手带来的，不是设计。后果很隐蔽：在引用覆盖对象里写错一个色值
 *       就会盖掉命名样式里的前景色 ⇒ 控件变黑，而"我明明引用了样式"的人根本想不到是这里。
 *       这条是**与画布域对齐口径**时一起定的（2026-09-27）：画布对这种键只能丢掉
 *       （语法里没有"写了但没有值"的写法），所以运行时要跟它一致 —— 丢掉 = 没写
 */
static void test_style_bad_values(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"styles\": {"
        "    \"s\": { \"font\": \"Consolas\", \"size\": 9, \"bold\": true, \"radius\": 8,"
        "             \"fg\": \"#00FF00\", \"bg\": \"#101010\" }"
        "  },"
        "  \"widgets\": ["
        "    { \"id\": \"a_bad\", \"type\": \"label\","
        "      \"style\": { \"use\": \"s\", \"font\": 123, \"fg\": \"乱码\", \"bg\": \"#GGGGGG\" } },"
        "    { \"id\": \"b_good\", \"type\": \"label\","
        "      \"style\": { \"use\": \"s\", \"fg\": \"#0000FF\" } },"
        "    { \"id\": \"c_size\", \"type\": \"label\", \"style\": { \"use\": \"s\", \"size\": 20 } },"
        "    { \"id\": \"d_align\", \"type\": \"label\", \"style\": { \"use\": \"s\", \"align\": \"middle\" } },"
        "    { \"id\": \"e_junk\", \"type\": \"label\","
        "      \"style\": { \"use\": \"s\", \"size\": \"abc\", \"bold\": \"yes\", \"radius\": \"x\" } },"
        "    { \"id\": \"f_missing\", \"type\": \"label\", \"style\": \"nope\" },"
        "    { \"id\": \"g_missing2\", \"type\": \"label\", \"style\": { \"use\": \"nope2\" } }"
        "  ] }");

    cg_test_ok("能解析", doc != NULL);
    if (!doc) return;

    cg_test_note("---- 取不到值的字段 = 没写（不许顶掉命名样式） ----");
    cg_test_str("font 写错 -> 命名样式的字体还在", kid(doc, 0)->style.font, "Consolas");
    cg_test_int("fg 写错 -> 命名样式的颜色还在", (long long)kid(doc, 0)->style.fg, 0x00FF00);
    cg_test_int("bg 写错 -> 命名样式的底色还在", (long long)kid(doc, 0)->style.bg, 0x101010);
    cg_test_int("  且这三个字段没被记成「写了」（掩码里一位都没有）",
                (long long)(kid(doc, 0)->style_mask &
                            (CG_STYLE_HAS_FONT | CG_STYLE_HAS_FG | CG_STYLE_HAS_BG)), 0);

    cg_test_note("---- size / bold / radius 也是同一个口径（哨兵实现） ----");
    cg_test_int("size 写错 -> 命名样式的字号还在", kid(doc, 4)->style.size, 9);
    cg_test_int("bold 写错 -> 命名样式的粗体还在", kid(doc, 4)->style.bold, 1);
    cg_test_int("radius 写错 -> 命名样式的圆角还在", kid(doc, 4)->style.radius, 8);
    cg_test_int("  这三个字段也没被记成「写了」",
                (long long)(kid(doc, 4)->style_mask &
                            (CG_STYLE_HAS_SIZE | CG_STYLE_HAS_BOLD | CG_STYLE_HAS_RADIUS)), 0);

    cg_test_note("---- 失效引用要**计数**（供 WARN 与 --selftest 用），不许一声不吭 ----");
    cg_test_int("两个控件引用了不存在的样式名", doc->style_missing, 2);
    cg_test_str("  并记下第一个失效的名字（写进那条 WARN）", doc->style_missing_name, "nope");
    cg_test_int("  失效的控件风格是全缺省（= 没写 style）", kid(doc, 5)->style.size, 0);
    cg_test_int("  但引用名仍然留着（画布标红 / 诊断要看得出它引的是什么）",
                kid(doc, 5)->style_name != NULL, 1);

    cg_test_note("---- 反面对照：写了真值当然要盖 ----");
    cg_test_int("合法 fg 覆盖生效", (long long)kid(doc, 1)->style.fg, 0x0000FF);
    cg_test_int("合法 size 覆盖生效", kid(doc, 2)->style.size, 20);
    cg_test_int("align 写不认识的名字 = 没写（老口径，未变）", kid(doc, 3)->style.align, 0);

    cg_doc_free(doc);
}

CG_TEST_SUITE("样式：style 的三种形态与合并", test_style_forms);
CG_TEST_SUITE("样式：嵌套容器里的引用", test_style_nested);
/**
 * @brief 运行时切深色的两个 API：空指针安全、状态跟着走、没有窗口也不崩
 * @return void 无返回值
 * @note 真机观感只能靠截图（见 build\darktest\ 的探针），这里只钉**接口契约**：
 *       传 NULL 不许崩、设置之后查询要变、自检这种没有窗口的场合调设置也必须安全
 */
static void test_theme_api(void)
{
    cg_app_t app;                     /* 白盒：栈上一个"没有窗口"的应用（natives = NULL） */

    memset(&app, 0, sizeof(app));
    cg_test_int("没有窗口的应用默认不是深色", cg_theme_is_dark(&app), 0);
    cg_test_int("切成深色返回成功", cg_theme_set_dark(&app, 1), 1);
    cg_test_int("  -> 现在报深色", cg_theme_is_dark(&app), 1);
    cg_test_int("切回浅色", cg_theme_set_dark(&app, 0), 1);
    cg_test_int("  -> 现在报浅色", cg_theme_is_dark(&app), 0);

    cg_test_int("传 NULL 给 set 不崩、报失败", cg_theme_set_dark(NULL, 1), 0);
    cg_test_int("传 NULL 给 is 报 0（不是深色）", cg_theme_is_dark(NULL), 0);
}

CG_TEST_SUITE("样式：app.dark 三态", test_dark_flag);
CG_TEST_SUITE("样式：深色的两个 API", test_theme_api);
CG_TEST_SUITE("样式：取不到值的字段算没写", test_style_bad_values);
