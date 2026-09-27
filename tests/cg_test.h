/**
 * @file    cg_test.h
 * @brief   极简 C 测试框架：断言 + 套件自动注册
 * @details 为什么要有它：runtime\src\core 里的 JSON 解析、文档模型、控件类型表、
 *          事件名表是「两份契约」的 C 侧真相（画布端 model.ts 跟着它走），
 *          但除了 gallery 整机 --selftest，之前没有任何测试直接盯住它们，
 *          表漂了只有"画布能存、程序起不来"这种事后症状。
 *
 *          两条设计约束：
 *          1) 新增测试文件不用改任何清单：文件里写一行 CG_TEST_SUITE 就自动进测试程序
 *             （编译期由 tools\run-tests.bat 扫 tests\ 下全部 .c，运行时靠 GCC 的
 *             constructor 属性自注册）；
 *          2) 断言失败只记账、不终止：一个套件里能一次看到全部失败项，
 *             退出码非 0 表示有失败（见 cg_test_main.c）。
 */
#ifndef CG_TEST_H
#define CG_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** 已执行的断言条数（cg_test_main.c 定义） */
extern int cg_test_checks;
/** 失败的断言条数；退出码靠它决定 */
extern int cg_test_fails;
/** 1 = 安静模式：只记账不打印（--memcheck 的第一遍热身跑用） */
extern int cg_test_quiet;

/**
 * @brief 测试函数签名：一个套件就是这么一个无参无返回的函数
 * @return void 无返回值（断言失败只记账，不返回错误码）
 */
typedef void (*cg_test_fn)(void);

/**
 * @brief 断言：条件为真算过
 * @param what 打印出来的断言描述
 * @param cond 条件，0 非 0 都收（非 0 算过）
 * @return void 无返回值（失败只记账，不中断本套件）
 */
void cg_test_ok(const char *what, int cond);
/**
 * @brief 断言：两个整数相等
 * @param what 打印出来的断言描述
 * @param got 实际值
 * @param want 期望值（不等时两者都会打出来）
 * @return void 无返回值（失败只记账，不中断本套件）
 */
void cg_test_int(const char *what, long long got, long long want);
/**
 * @brief 断言：两个字符串相等
 * @param what 打印出来的断言描述
 * @param got 实际值；NULL 一律当空串
 * @param want 期望值；NULL 一律当空串
 * @return void 无返回值（失败只记账，不中断本套件）
 */
void cg_test_str(const char *what, const char *got, const char *want);
/**
 * @brief 打一行说明，不计入断言数
 * @param text 说明文字
 * @return void 无返回值（分隔用例组用）
 */
void cg_test_note(const char *text);
/**
 * @brief 注册一个套件
 * @param name 打印出来的套件名
 * @param fn 套件函数
 * @return void 无返回值
 * @note 正常用法是下面的 CG_TEST_SUITE 宏，别直接调用
 */
void cg_test_register(const char *name, cg_test_fn fn);

/** 声明一个测试套件。放在套件函数定义之后、文件末尾。
 *  @param name 打印出来的套件名
 *  @param fn   void f(void) 形式的测试函数 */
#define CG_TEST_SUITE(name, fn)                                            \
    static void cg_test_autoreg_##fn(void)                                 \
    {                                                                      \
        cg_test_register((name), (fn));                                     \
    }                                                                      \
    __attribute__((constructor)) static void cg_test_ctor_##fn(void)        \
    {                                                                      \
        cg_test_autoreg_##fn();                                             \
    }

#ifdef CG_TEST_MEMCHECK
/**
 * @brief 取当前存活的堆块数
 * @return int 存活块数
 * @note 只在 --memcheck 模式有意义（见 cg_test_memcheck.c）
 */
int cg_test_mem_live(void);
#endif

#ifdef __cplusplus
}
#endif
#endif /* CG_TEST_H */
