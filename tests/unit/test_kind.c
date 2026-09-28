/**
 * @file    test_kind.c
 * @brief   控件类型表（k_kinds）与事件名表的契约测试
 * @details 这张表有三处"必须对齐"的地方，全都不编译报错、只在运行时露馅：
 *          1) k_kinds 的每一行 ↔ cg_kind_t 枚举：任何一行漏写或写重，按枚举查就查不到；
 *          2) k_kinds[].events 位掩码 ↔ cg_event_t：写错位表示"绑了事件却不发"；
 *          3) k_kinds[].name ↔ .cgui 里的 type 字段 ↔ 画布 model.ts 的 KINDS。
 *          下面用"遍历枚举 + 反向查表"的方式把 1、2 钉死，第 3 条靠 check-format 与
 *          画布端的一致性检查兜底（见 tools\check-support.mjs / check-format.bat）。
 */
#include "cg_test.h"
#include "cg_doc.h"

#include <string.h>

/**
 * @brief 枚举与表项双向一致：每个类型都有行，且行的枚举字段指回自己
 * @return void 无返回值
 */
static void test_kind_table_covers_enum(void)
{
    cg_test_note("---- 枚举 ↔ 表项双向一致 ----");
    for (int k = 0; k < (int)CG_K_COUNT; k++) {
        const cg_kind_info_t *ki = cg_kind_info((cg_kind_t)k);
        char                  what[128];

        snprintf(what, sizeof(what), "cg_kind_info(%d) 有表项", k);
        cg_test_ok(what, ki != NULL);
        if (!ki) continue;

        snprintf(what, sizeof(what), "kind \"%s\" 的 kind 字段指回 %d", ki->name, k);
        cg_test_int(what, (long long)ki->kind, k);

        snprintf(what, sizeof(what), "kind \"%s\" 有非空类型名", ki->name);
        cg_test_ok(what, ki->name && ki->name[0] != '\0');

        snprintf(what, sizeof(what), "kind \"%s\" 按名字能查回同一行", ki->name);
        cg_test_ok(what, cg_kind_lookup(ki->name) == ki);

        snprintf(what, sizeof(what), "kind \"%s\" 的默认尺寸非负", ki->name);
        cg_test_ok(what, ki->def_w >= 0 && ki->def_h >= 0);
    }
}

/**
 * @brief 类型名不重复：两行同名会让后一行成死代码
 * @return void 无返回值
 */
static void test_kind_names_unique(void)
{
    cg_test_note("---- 类型名不重复 ----");
    for (int k = 0; k < (int)CG_K_COUNT; k++) {
        const cg_kind_info_t *ki = cg_kind_info((cg_kind_t)k);
        if (!ki) continue;
        for (int j = k + 1; j < (int)CG_K_COUNT; j++) {
            const cg_kind_info_t *kj = cg_kind_info((cg_kind_t)j);
            char                  what[128];

            if (!kj) continue;
            snprintf(what, sizeof(what), "类型名 \"%s\" 只出现一次", ki->name);
            cg_test_ok(what, strcmp(ki->name, kj->name) != 0);
        }
    }
}

/**
 * @brief 查不到的类型必须返回 NULL / "unknown"，不能瞎命中一行
 * @return void 无返回值
 */
static void test_kind_lookup_misses(void)
{
    cg_test_note("---- 查不到的类型 ----");
    cg_test_ok("cg_kind_lookup(NULL) 返回 NULL", cg_kind_lookup(NULL) == NULL);
    cg_test_ok("cg_kind_lookup(\"没有这个类型\") 返回 NULL", cg_kind_lookup("没有这个类型") == NULL);
    cg_test_ok("cg_kind_lookup(\"\") 返回 NULL", cg_kind_lookup("") == NULL);
    cg_test_str("cg_kind_name(越界) 返回 unknown", cg_kind_name(CG_K_COUNT), "unknown");
    cg_test_int("越界 kind 查不到事件表", cg_kind_info(CG_K_COUNT) == NULL, 1);
}

/**
 * @brief 事件名表：双向可查、不重复、非法输入有兜底
 * @return void 无返回值
 */
static void test_event_names(void)
{
    cg_test_note("---- 事件名表双向一致 ----");
    for (int e = 0; e < (int)CG_EVENT_COUNT; e++) {
        const char *name = cg_event_name((cg_event_t)e);
        char        what[128];

        snprintf(what, sizeof(what), "事件 %d 有名字", e);
        cg_test_ok(what, name && name[0] != '\0' && strcmp(name, "?") != 0);

        snprintf(what, sizeof(what), "事件名 \"%s\" 能反查回 %d", name ? name : "?", e);
        cg_test_int(what, cg_event_from_name(name), e);
    }
    cg_test_str("越界事件名返回 ?", cg_event_name(CG_EVENT_COUNT), "?");
    cg_test_int("cg_event_from_name(NULL) 返回 -1", cg_event_from_name(NULL), -1);
    cg_test_int("cg_event_from_name(\"没有的事件\") 返回 -1", cg_event_from_name("没有的事件"), -1);
}

/**
 * @brief 事件位掩码与"类型支不支持这个事件"必须完全一致（写错位就是"绑了不发"）
 * @return void 无返回值
 */
static void test_event_mask_consistency(void)
{
    cg_test_note("---- 事件位掩码与查询函数一致 ----");
    for (int k = 0; k < (int)CG_K_COUNT; k++) {
        const cg_kind_info_t *ki = cg_kind_info((cg_kind_t)k);
        if (!ki) continue;
        for (int e = 0; e < (int)CG_EVENT_COUNT; e++) {
            int want = (ki->events & CG_EV_BIT((cg_event_t)e)) ? 1 : 0;
            int got = cg_kind_supports_event(ki->name, (cg_event_t)e);
            if (got == want) continue;   /* 一致就不刷屏，只报不一致的 */
            {
                char what[160];
                snprintf(what, sizeof(what), "kind \"%s\" 对事件 \"%s\" 的支持位与查询一致",
                         ki->name, cg_event_name((cg_event_t)e));
                cg_test_int(what, got, want);
            }
        }
    }
    cg_test_ok("事件支持位掩码总检查通过（无输出即一致）", 1);
    cg_test_int("未知控件类型不支持任何事件", cg_kind_supports_event("没有这个类型", CG_EVENT_CLICK), 0);
    cg_test_int("越界事件码一律不支持", cg_kind_supports_event("button", (cg_event_t)CG_EVENT_COUNT), 0);
}

/**
 * @brief 有代表性的取值：类型表被误改时这里会先响
 * @return void 无返回值
 */
static void test_kind_spot_checks(void)
{
    cg_test_note("---- 代表性取值 ----");
    cg_test_int("button 支持 click", cg_kind_supports_event("button", CG_EVENT_CLICK), 1);
    cg_test_int("label 不支持 click（纯显示）", cg_kind_supports_event("label", CG_EVENT_CLICK), 0);
    cg_test_int("progress 没有任何事件", cg_kind_info(CG_K_PROGRESS)->events != 0, 0);
    cg_test_int("tree 支持展开", cg_kind_supports_event("tree", CG_EVENT_EXPAND), 1);
    cg_test_int("tree 支持折叠", cg_kind_supports_event("tree", CG_EVENT_COLLAPSE), 1);
    cg_test_int("slider 有 release（松手才算一次）", cg_kind_supports_event("slider", CG_EVENT_RELEASE), 1);
    cg_test_int("table 有 check（勾选列）", cg_kind_supports_event("table", CG_EVENT_CHECK), 1);
    cg_test_int("window 支持 close", cg_kind_supports_event("window", CG_EVENT_CLOSE), 1);
    cg_test_int("window 支持 ready", cg_kind_supports_event("window", CG_EVENT_READY), 1);
    cg_test_int("window 支持 tray", cg_kind_supports_event("window", CG_EVENT_TRAY), 1);
    cg_test_int("window 支持按键", cg_kind_supports_event("window", CG_EVENT_KEYDOWN), 1);
    cg_test_int("group 是容器", cg_kind_info(CG_K_GROUP)->container, 1);
    cg_test_int("tabs 是容器", cg_kind_info(CG_K_TABS)->container, 1);
    cg_test_int("label 不是容器", cg_kind_info(CG_K_LABEL)->container, 0);
    cg_test_str("cg_kind_name 能回类型名", cg_kind_name(CG_K_BUTTON), "button");
    cg_test_ok("cg_kind_event_list 会列事件名", cg_kind_event_list(CG_K_BUTTON)[0] != '\0');
    cg_test_str("cg_kind_event_list 对无事件控件有占位提示",
                cg_kind_event_list(CG_K_PROGRESS), "(该控件没有事件)");
}

CG_TEST_SUITE("控件类型表与事件契约", test_kind_table_covers_enum);
CG_TEST_SUITE("控件类型名不重复", test_kind_names_unique);
CG_TEST_SUITE("控件类型查不到的边界", test_kind_lookup_misses);
CG_TEST_SUITE("事件名表双向一致", test_event_names);
CG_TEST_SUITE("事件掩码与查询一致", test_event_mask_consistency);
CG_TEST_SUITE("类型表代表取值", test_kind_spot_checks);
