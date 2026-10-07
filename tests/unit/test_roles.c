/**
 * @file    test_roles.c
 * @brief   色彩角色（.cgui 顶层 roles{}）与颜色字段的 "@名字" 引用
 * @details 盯它的理由与 styles{} 一模一样：这层的错法全是"安静地不对"——
 *          合并把引用丢了、把"角色不存在"当成错误中断加载、求值拿错槽位，
 *          界面依旧能起来，只是颜色悄悄不对。
 *
 *          口径（.codebuddy/plans/roles-dark-sdd.md §1/§3）：
 *          - 引用挂在现有颜色字段的位置："fg": "@accent"；
 *          - 控件自己写了 fg/bg（值或引用）时，命名样式里的都不能再来盖（内联优先）；
 *          - 角色不存在 / 该槽没写 = 当没写（不报错不中断，但解析期计数出 WARN）；
 *          - 求值是 core 层纯函数 cg_role_lookup()，按 dark 参数取槽，不开窗可测。
 *
 *          白盒：直接 include cg_doc.h / cg_internal.h 拿节点、mask 与求值函数。
 */
#include "cg_test.h"
#include "cg_doc.h"
#include "cg_internal.h"     /* cg_role_lookup 的声明 */

#include <string.h>

/**
 * @brief 解析一段 .cgui 文本（失败时打一行说明）
 * @param json 待解析的 .cgui 文本
 * @return cg_doc_t* 解析出的文档；失败返回 NULL
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
 * @brief roles{} 表的形状：非法条目跳过、两槽都缺跳过、重名后者覆盖
 * @return void 无返回值
 */
static void test_roles_shape(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"roles\": {"
        "    \"accent\": { \"light\": \"#6750A4\", \"dark\": \"#D0BCFF\" },"
        "    \"half\":   { \"light\": \"#B3261E\" },"
        "    \"empty\":  { },"
        "    \"dup\":    { \"light\": \"#111111\" }, \"dup\": { \"light\": \"#222222\" },"
        "    \"bad\":    42,"
        "    \"\":       { \"light\": \"#333333\" }"
        "  },"
        "  \"widgets\": [ { \"id\": \"a\", \"type\": \"label\" } ] }");

    cg_test_ok("带 roles{} 的文档能解析", doc != NULL);
    if (!doc) return;

    /* 4 条 = accent / half / dup×2（表不查重，两条都存；"后者覆盖"发生在查找时）；
     * "empty"（两槽都缺）、"bad"（值不是对象）、空名都**跳过** —— 非法条目不炸整份文档。 */
    cg_test_int("角色表读到 4 条（非法条目全被跳过）", (long long)doc->nroles, 4);
    cg_test_int("accent.light", (long long)cg_role_lookup(doc, "accent", 0), 0x6750A4);
    cg_test_int("accent.dark", (long long)cg_role_lookup(doc, "accent", 1), 0xD0BCFF);
    cg_test_note("---- 只写了 light 的角色：dark 槽 = 当没写 ----");
    cg_test_int("half.light 可取", (long long)cg_role_lookup(doc, "half", 0), 0xB3261E);
    cg_test_int("half.dark 取不到（CG_COLOR_NONE）", (long long)cg_role_lookup(doc, "half", 1),
                (long long)CG_COLOR_NONE);
    cg_test_note("---- 重名：后者覆盖 ----");
    cg_test_int("dup 取后写的 0x222222", (long long)cg_role_lookup(doc, "dup", 0), 0x222222);
    cg_test_note("---- 不存在的角色：当没写 ----");
    cg_test_int("nope 取不到", (long long)cg_role_lookup(doc, "nope", 0), (long long)CG_COLOR_NONE);
    cg_doc_free(doc);
}

/**
 * @brief 颜色字段上的 "@引用"：挂载、值/引用互斥、缺失计数
 * @return void 无返回值
 */
static void test_role_refs(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200, \"bg\": \"@surface\" },"
        "  \"roles\": { \"accent\": { \"light\": \"#6750A4\", \"dark\": \"#D0BCFF\" } },"
        "  \"widgets\": ["
        "    { \"id\": \"a_ref\",   \"type\": \"label\", \"style\": { \"fg\": \"@accent\" } },"
        "    { \"id\": \"b_both\",  \"type\": \"label\", \"style\": { \"fg\": \"@accent\", \"bg\": \"#010203\" } },"
        "    { \"id\": \"c_bad\",   \"type\": \"label\", \"style\": { \"fg\": \"@nope\" } },"
        "    { \"id\": \"d_at\",    \"type\": \"label\", \"style\": { \"fg\": \"@\" } },"
        "    { \"id\": \"e_none\",  \"type\": \"label\" }"
        "  ] }");

    cg_test_ok("带 @引用的文档能解析", doc != NULL);
    if (!doc) return;

    cg_test_note("---- fg: \"@accent\" → 引用串就位、值槽待求值 ----");
    cg_test_str("fg_role = accent", kid(doc, 0)->style.fg_role, "accent");
    cg_test_int("fg 值槽是 NONE（等求值）", (long long)kid(doc, 0)->style.fg, (long long)CG_COLOR_NONE);
    cg_test_ok("mask 记了 FG_ROLE 位",
               (kid(doc, 0)->style_mask & CG_STYLE_HAS_FG_ROLE) != 0);

    cg_test_note("---- 值与引用互不覆盖：bg 写了字面色不受 fg 引用影响 ----");
    cg_test_str("fg_role 仍是 accent", kid(doc, 1)->style.fg_role, "accent");
    cg_test_int("bg 是字面色", (long long)kid(doc, 1)->style.bg, 0x010203);

    cg_test_note("---- @不存在的角色：当没写，但**必须计数**（不许静默） ----");
    cg_test_ok("fg_role 挂上了名字", kid(doc, 2)->style.fg_role != NULL &&
                                   strcmp(kid(doc, 2)->style.fg_role, "nope") == 0);
    cg_test_int("失效引用计数 1", (long long)doc->role_missing, 1);
    cg_test_str("第一个失效名", doc->role_missing_name, "nope");

    cg_test_note("---- 孤零零一个 @：当没写（不是引用也没有值） ----");
    cg_test_ok("没有引用串", kid(doc, 3)->style.fg_role == NULL);
    cg_test_int("fg 仍是 NONE", (long long)kid(doc, 3)->style.fg, (long long)CG_COLOR_NONE);
    cg_test_ok("  且不计失效", doc->role_missing == 1);

    cg_test_note("---- app.bg 的 @引用挂在文档上 ----");
    cg_test_str("bg_role = surface", doc->bg_role, "surface");
    cg_test_int("doc.bg 值槽是 NONE", (long long)doc->bg, (long long)CG_COLOR_NONE);
    cg_doc_free(doc);
}

/**
 * @brief 合并穿透：命名样式里写的 @引用要能落到引用它的控件上（内联优先）
 * @return void 无返回值
 */
static void test_role_merge(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"roles\": { \"accent\": { \"light\": \"#6750A4\", \"dark\": \"#D0BCFF\" } },"
        "  \"styles\": { \"body\": { \"fg\": \"@accent\", \"size\": 9 } },"
        "  \"widgets\": ["
        "    { \"id\": \"a_named\",  \"type\": \"label\", \"style\": \"body\" },"
        "    { \"id\": \"b_inline\", \"type\": \"label\", \"style\": { \"use\": \"body\", \"fg\": \"#123456\" } },"
        "    { \"id\": \"c_refwin\", \"type\": \"label\", \"style\": { \"use\": \"body\", \"fg\": \"@accent\" } }"
        "  ] }");

    cg_test_ok("命名样式带 @引用的文档能解析", doc != NULL);
    if (!doc) return;

    cg_test_note("---- 命名样式里的 @引用**连串带位**落到控件 ----");
    cg_test_str("引用串穿过合并", kid(doc, 0)->style.fg_role, "accent");
    cg_test_int("值槽待求值", (long long)kid(doc, 0)->style.fg, (long long)CG_COLOR_NONE);
    cg_test_int("  命名样式里的 size 照常合并", kid(doc, 0)->style.size, 9);

    cg_test_note("---- 控件自己写了字面色：内联优先，引用不来盖 ----");
    cg_test_ok("没有引用串", kid(doc, 1)->style.fg_role == NULL);
    cg_test_int("fg 是内联值", (long long)kid(doc, 1)->style.fg, 0x123456);

    cg_test_note("---- 控件自己写了 @引用：压过命名样式的值/引用 ----");
    cg_test_str("引用是控件自己的", kid(doc, 2)->style.fg_role, "accent");
    cg_test_int("值槽仍待求值", (long long)kid(doc, 2)->style.fg, (long long)CG_COLOR_NONE);
    cg_doc_free(doc);
}

/**
 * @brief 求值口径：显式模式取对应槽、槽缺失与角色缺失都 = 当没写
 * @return void 无返回值
 */
static void test_role_eval(void)
{
    cg_doc_t *doc = parse(
        "{ \"cgui\": 1,"
        "  \"app\": { \"width\": 300, \"height\": 200 },"
        "  \"roles\": { \"accent\": { \"light\": \"#6750A4\", \"dark\": \"#D0BCFF\" },"
        "               \"half\":   { \"dark\": \"#D0BCFF\" } },"
        "  \"widgets\": [ { \"id\": \"a\", \"type\": \"label\" } ] }");

    cg_test_ok("文档能解析", doc != NULL);
    if (!doc) return;

    cg_test_note("---- cg_role_lookup：按 dark 参数取槽 ----");
    cg_test_int("浅色取 light", (long long)cg_role_lookup(doc, "accent", 0), 0x6750A4);
    cg_test_int("深色取 dark", (long long)cg_role_lookup(doc, "accent", 1), 0xD0BCFF);
    cg_test_note("---- 槽没写 = 当没写（half 只写了 dark） ----");
    cg_test_int("half 浅色取不到", (long long)cg_role_lookup(doc, "half", 0), (long long)CG_COLOR_NONE);
    cg_test_int("half 深色可取", (long long)cg_role_lookup(doc, "half", 1), 0xD0BCFF);
    cg_test_note("---- 边界：NULL/空名/不存在 ----");
    cg_test_int("doc 传 NULL 不崩", (long long)cg_role_lookup(NULL, "accent", 0), (long long)CG_COLOR_NONE);
    cg_test_int("名字空 = 找不到", (long long)cg_role_lookup(doc, "", 0), (long long)CG_COLOR_NONE);
    cg_test_int("名字 NULL = 找不到", (long long)cg_role_lookup(doc, NULL, 0), (long long)CG_COLOR_NONE);
    cg_doc_free(doc);
}

CG_TEST_SUITE("角色：roles{} 表形状与容错", test_roles_shape);
CG_TEST_SUITE("角色：颜色字段上的 @引用", test_role_refs);
CG_TEST_SUITE("角色：合并穿透（命名样式带 @引用）", test_role_merge);
CG_TEST_SUITE("角色：求值口径", test_role_eval);
