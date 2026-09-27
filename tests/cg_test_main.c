/**
 * @file    cg_test_main.c
 * @brief   C 层单测入口：收集自注册套件、按需过滤、汇总结果
 * @details 跑法见 tools\run-tests.bat（本程序不单独使用）。
 *          退出码：0 = 全部通过，1 = 有失败，供 bat / CI 判断。
 *          命令行：
 *            --list          只列套件名，不跑
 *            --grep=子串     只跑套件名包含该子串的（ASCII 忽略大小写）
 *            --help          用法
 *          --memcheck 由 run-tests.bat 负责加编译开关，这里只负责
 *          "跑两遍：第一遍热身把惰性分配打出来，第二遍才量存活块增量"。
 */
#include "cg_test.h"

#include <stdio.h>
#include <string.h>

int cg_test_checks = 0;
int cg_test_fails = 0;
int cg_test_quiet = 0;

#define CG_TEST_MAX_SUITES 256

typedef struct {
    const char *name;
    cg_test_fn  fn;
} cg_test_suite_t;

static cg_test_suite_t s_suites[CG_TEST_MAX_SUITES];
static int             s_suite_count = 0;
static int             s_suite_fails = 0;
/** 因为超出上限被丢掉的套件数（>0 就是**测试没跑**，必须让整轮失败） */
static int             s_suite_dropped = 0;

#ifdef CG_TEST_MEMCHECK
/** 出现过"释放比分配多"就说明有没被拦到的分配源，泄漏判定不可信 */
static int s_mem_unreliable = 0;
#endif

/**
 * @brief 注册一个套件（框架内部用，套件文件通过 CG_TEST_SUITE 宏调用）
 * @param name 套件名
 * @param fn 测试函数
 */
void cg_test_register(const char *name, cg_test_fn fn)
{
    /* **装不下必须说出来**。2026-09-27 撞过一次：新加的 7 个套件正好卡在原来的 64 上限上，
     * 被静默丢掉，而报告仍是"64 个套件，0 处失败 / ALL PASS" —— 新增的测试根本没跑，
     * 却看起来一切正常。这是测试框架最不该有的失效方式（"没跑"伪装成"通过"），
     * 所以这里既报 stderr，也计入总数让**整轮失败**。 */
    if (s_suite_count >= CG_TEST_MAX_SUITES) {
        s_suite_dropped++;
        fprintf(stderr, "[cstackgui 单测] 套件数超过上限 %d，被丢弃：%s\n",
                CG_TEST_MAX_SUITES, name ? name : "?");
        return;
    }
    s_suites[s_suite_count].name = name;
    s_suites[s_suite_count].fn = fn;
    s_suite_count++;
}

/**
 * @brief 断言真假
 * @param what 这条断言在说什么
 * @param cond 非 0 算过
 */
void cg_test_ok(const char *what, int cond)
{
    cg_test_checks++;
    if (cond) {
        if (!cg_test_quiet) printf("  [ok]   %s\n", what);
        return;
    }
    cg_test_fails++;
    s_suite_fails++;
    if (!cg_test_quiet) printf("  [FAIL] %s\n", what);
}

/**
 * @brief 断言两个整数相等
 * @param what 这条断言在说什么
 * @param got 实际值
 * @param want 期望值
 */
void cg_test_int(const char *what, long long got, long long want)
{
    cg_test_checks++;
    if (got == want) {
        if (!cg_test_quiet) printf("  [ok]   %s\n", what);
        return;
    }
    cg_test_fails++;
    s_suite_fails++;
    if (!cg_test_quiet) printf("  [FAIL] %s （实际 %lld，期望 %lld）\n", what, got, want);
}

/**
 * @brief 断言两个字符串相等（NULL 当空串）
 * @param what 这条断言在说什么
 * @param got 实际字符串，可为 NULL
 * @param want 期望字符串，可为 NULL
 */
void cg_test_str(const char *what, const char *got, const char *want)
{
    const char *a = got ? got : "";
    const char *b = want ? want : "";

    cg_test_checks++;
    if (strcmp(a, b) == 0) {
        if (!cg_test_quiet) printf("  [ok]   %s\n", what);
        return;
    }
    cg_test_fails++;
    s_suite_fails++;
    if (!cg_test_quiet) printf("  [FAIL] %s （实际 \"%s\"，期望 \"%s\"）\n", what, a, b);
}

/**
 * @brief 打一行说明（不计入断言数）
 * @param text 要打印的文本
 */
void cg_test_note(const char *text)
{
    if (!cg_test_quiet) printf("  ---- %s\n", text);
}

/** @brief 套件名是否匹配 --grep 的子串（ASCII 忽略大小写） */
static int name_matches(const char *name, const char *pat)
{
    size_t plen;

    if (!pat || !*pat) return 1;
    plen = strlen(pat);
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < plen && p[i]) {
            char a = p[i];
            char b = pat[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == plen) return 1;
    }
    return 0;
}

/**
 * @brief 跑一个套件：打印分节、跑它、报这一节的失败数（--memcheck 时量存活块增量）
 * @param s 要跑的套件
 */
static void run_one(const cg_test_suite_t *s)
{
#ifdef CG_TEST_MEMCHECK
    int live_before = cg_test_mem_live();
#endif

    if (!cg_test_quiet) printf("[ RUN  ] %s\n", s->name);
    s_suite_fails = 0;
    s->fn();
    if (!cg_test_quiet && s_suite_fails > 0) {
        printf("[FAILED] %s —— %d 处失败\n", s->name, s_suite_fails);
    }

#ifdef CG_TEST_MEMCHECK
    if (!cg_test_quiet) {
        int delta = cg_test_mem_live() - live_before;
        if (delta < 0) s_mem_unreliable = 1;
        cg_test_ok("这一套跑完没有新增存活堆块", delta <= 0);
    }
#endif
}

/** @brief 按过滤结果跑一遍全部套件，返回跑了的套件数 */
static int run_all(const char *grep)
{
    int ran = 0;

    for (int i = 0; i < s_suite_count; i++) {
        if (!name_matches(s_suites[i].name, grep)) continue;
        run_one(&s_suites[i]);
        ran++;
    }
    return ran;
}

/**
 * @brief 入口：解析参数 -> 跑套件 -> 汇总
 * @param argc 参数个数
 * @param argv 参数数组
 * @return int 0 = 全部通过，1 = 有失败
 */
int main(int argc, char **argv)
{
    const char *grep = NULL;
    int         list_only = 0;

    setvbuf(stdout, NULL, _IONBF, 0);   /* 行级实时落盘：卡死/被杀也能看到最后进度 */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_only = 1;
        } else if (strncmp(argv[i], "--grep=", 7) == 0) {
            grep = argv[i] + 7;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("用法: cgu-tests [--list] [--grep=子串]\n");
            printf("  套件由 tests\\ 下的文件用 CG_TEST_SUITE 自注册；\n");
            printf("  --memcheck 是 tools\\run-tests.bat 的编译开关，不是这里的参数。\n");
            return 0;
        }
        /* 不认识的参数忽略：run-tests.bat 可能把别的开关透传进来 */
    }

    if (list_only) {
        for (int i = 0; i < s_suite_count; i++) {
            if (name_matches(s_suites[i].name, grep)) printf("%s\n", s_suites[i].name);
        }
        return 0;
    }

#ifdef CG_TEST_MEMCHECK
    /* 热身跑：把 printf 缓冲、locale 之类的惰性分配先打出来，
       量的时候只看第二遍的增量，否则启动期噪声会被当成泄漏。 */
    cg_test_quiet = 1;
    run_all(grep);
    cg_test_quiet = 0;
    cg_test_checks = 0;
    cg_test_fails = 0;
    printf("[memcheck] 记账已开启，逐套件报告存活堆块增量\n");
#endif

    {
        int selected = 0;
        for (int i = 0; i < s_suite_count; i++) {
            if (name_matches(s_suites[i].name, grep)) selected++;
        }
        if (selected == s_suite_count) {
            printf("[cstackgui 单测] %d 个套件\n", selected);
        } else {
            printf("[cstackgui 单测] %d / %d 个套件（--grep 过滤后）\n", selected, s_suite_count);
        }
        printf("\n");
    }
    int ran = run_all(grep);

    printf("\n%d 个套件，%d 条断言，%d 处失败\n", ran, cg_test_checks, cg_test_fails);
#ifdef CG_TEST_MEMCHECK
    if (s_mem_unreliable) {
        printf("[memcheck] 注意：出现过「释放比分配多」，说明有未拦截的分配源，本次泄漏判定不完全可信\n");
    }
#endif
    /* 有套件被丢 = 有测试**根本没跑**：那是失败，不是通过（见 cg_test_register 的注释） */
    if (s_suite_dropped > 0) {
        printf("[fail] 有 %d 个套件因为超出上限 %d 没跑 —— 把 CG_TEST_MAX_SUITES 调大\n",
               s_suite_dropped, CG_TEST_MAX_SUITES);
        printf("== 单测 FAILED ==\n");
        return 1;
    }
    if (cg_test_fails > 0) {
        printf("== 单测 FAILED ==\n");
        return 1;
    }
    printf("== 单测 ALL PASS ==\n");
    return 0;
}
