/**
 * @file    test_timer.c
 * @brief   一次性定时器（cg_timer.c）的单元测试
 * @details 定时器只有两条铁律最容易被改坏：
 *            1. **回调里重挂的定时器，本轮 tick 绝不能再触发** —— serialterm 那种
 *               "回调里 cg_set_timer 再挂一次当周期"是标准用法，要是同一轮就把它
 *               又触发一遍，就是无限循环，症状是"一打开串口窗口就未响应"；
 *            2. kill 之后绝不触发，别的 app 的定时器不许被顺手清掉。
 *          这里不需要真开窗：cg_timers_tick 只拿 app 指针做同组比较，用任意非空
 *          指针当"应用令牌"即可。tick 一次推进 CG_TICK_MS（50ms）。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>

/** 任意非空指针就能当 app 令牌：tick / clear 只做指针比较 */
#define APP_A ((cg_app_t *)1)
#define APP_B ((cg_app_t *)2)

/* ------------------------------------------------------------ 计数回调 */

static int s_fires_a;
static int s_fires_b;

/**
 * @brief 计数回调 a：每触发一次自增
 * @param user 未使用的用户数据
 * @return void 无返回值
 */
static void cb_count_a(void *user) { (void)user; s_fires_a++; }
/**
 * @brief 计数回调 b：每触发一次自增
 * @param user 未使用的用户数据
 * @return void 无返回值
 */
static void cb_count_b(void *user) { (void)user; s_fires_b++; }

/** @brief 标准用法：回调里把自己再挂一次（serialterm 的轮询就是这种写法） */
/**
 * @brief 标准用法：回调里把自己再挂一次（串口示例的轮询就是这种写法）
 * @param user 用户数据，这里当应用令牌用（重挂时原样传回）
 * @return void 无返回值
 */
static void cb_rearm(void *user)
{
    s_fires_a++;
    if (s_fires_a < 1000)
        cg_set_timer((cg_app_t *)user, 50, cb_rearm, user);
}

/* ------------------------------------------------------------ 套件 */

/** @brief 到点才触发、没到点不触发、kill 了绝不触发 */
/**
 * @brief 到点才触发、没到点不触发、注销了绝不触发
 * @return void 无返回值
 */
static void test_timer_basic(void)
{
    cg_timer_t *t;

    cg_test_note("---- 一次性：到点才触发 ----");
    s_fires_a = 0;
    t = cg_set_timer(APP_A, 150, cb_count_a, NULL);   /* 3 个 tick 才到点 */
    cg_test_ok("建定时器成功", t != NULL);

    cg_timers_tick(APP_A);
    cg_test_int("第 1 拍（50ms）没到点，不触发", s_fires_a, 0);
    cg_timers_tick(APP_A);
    cg_test_int("第 2 拍（100ms）没到点，不触发", s_fires_a, 0);
    cg_timers_tick(APP_A);
    cg_test_int("第 3 拍（150ms）正好到点，触发一次", s_fires_a, 1);
    cg_timers_tick(APP_A);
    cg_test_int("一次性定时器触发完就没了", s_fires_a, 1);

    cg_test_note("---- kill：挂上了但反悔 ----");
    s_fires_b = 0;
    t = cg_set_timer(APP_A, 50, cb_count_b, NULL);
    cg_test_ok("建定时器成功", t != NULL);
    cg_kill_timer(t);
    cg_kill_timer(t);                       /* 幂等：重复 kill 不崩 */
    cg_timers_tick(APP_A);
    cg_test_int("kill 之后的拍子不触发", s_fires_b, 0);

    cg_test_note("---- 参数校验 ----");
    cg_test_ok("app 是 NULL 建不出来", cg_set_timer(NULL, 50, cb_count_a, NULL) == NULL);
    cg_test_ok("fn 是 NULL 建不出来", cg_set_timer(APP_A, 50, NULL, NULL) == NULL);
    cg_kill_timer(NULL);                    /* NULL 安全 */
    cg_test_ok("NULL kill 不崩", 1);
}

/** @brief 回调里重挂：本轮绝不能再触发（回归：串口助手一开就未响应的那个死循环） */
/**
 * @brief 回调里重挂：本轮绝不能再触发（回归：串口助手一开就未响应的那个死循环）
 * @return void 无返回值
 */
static void test_timer_rearm_same_tick(void)
{
    s_fires_a = 0;

    cg_test_note("---- 回调里重挂的定时器，同一轮 tick 不许再触发 ----");
    cg_set_timer(APP_A, 50, cb_rearm, APP_A);
    cg_timers_tick(APP_A);
    cg_test_int("第 1 轮只触发一次", s_fires_a, 1);
    cg_timers_tick(APP_A);
    cg_test_int("第 2 轮再触发一次（这才是周期）", s_fires_a, 2);
    cg_timers_tick(APP_A);
    cg_test_int("第 3 轮还是一次", s_fires_a, 3);
    cg_test_ok("没有变成无限循环（否则上一句就回不来了）", s_fires_a == 3);

    cg_timers_clear(APP_A);                 /* 收尾：把还挂着的那颗摘掉 */
}

/** @brief 同轮注销：一颗定时器的回调里 cg_kill_timer 另一颗，后者本轮绝不触发
 *         （回归：原先 free 在 fn 之前，被注销/已触发的定时器同轮仍可能被回调，
 *          回调访问已释放的窗口/控件 → UAF）。 */
static int s_killer_fired;
static int s_victim_fired;
static cg_timer_t *g_victim;

/**
 * @brief 受害者回调：每触发一次自增
 * @param user 未使用的用户数据
 * @return void 无返回值
 */
static void cb_victim(void *user) { (void)user; s_victim_fired++; }
/**
 * @brief 凶手回调：自增计数，并在同轮注销另一颗定时器
 * @param user 未使用的用户数据
 * @return void 无返回值
 */
static void cb_killer(void *user)
{
    (void)user;
    s_killer_fired++;
    cg_kill_timer(g_victim);   /* 同轮注销另一颗，它绝不该再触发 */
}

/**
 * @brief 同轮注销：一颗定时器的回调里注销另一颗，后者本轮绝不触发
 * @return void 无返回值
 * @note 回归：原先先释放再回调，被注销 / 已触发的定时器同轮仍可能被回调，
 *       回调访问已释放的窗口 / 控件 → 释放后使用
 */
static void test_timer_kill_in_tick(void)
{
    s_killer_fired = 0;
    s_victim_fired = 0;

    cg_test_note("---- 同轮被注销的定时器，本轮绝不触发（UAF 防护） ----");
    /* victim 先挂（链尾），killer 后挂（链头，先被遍历到） */
    g_victim = cg_set_timer(APP_A, 50, cb_victim, NULL);
    cg_set_timer(APP_A, 50, cb_killer, NULL);
    cg_timers_tick(APP_A);
    cg_test_int("killer 正常触发一次", s_killer_fired, 1);
    cg_test_int("同轮被注销的 victim 绝不触发", s_victim_fired, 0);

    cg_timers_clear(APP_A);     /* 收尾 */
}

/** @brief 多应用隔离：tick 只推进自己的，clear 也只清自己的 */
/**
 * @brief 多应用隔离：推进只推自己的，清理也只清自己的
 * @return void 无返回值
 */
static void test_timer_two_apps(void)
{
    s_fires_a = 0;
    s_fires_b = 0;

    cg_test_note("---- 两个 app 各走各的 ----");
    cg_set_timer(APP_A, 50, cb_count_a, NULL);
    cg_set_timer(APP_B, 50, cb_count_b, NULL);

    cg_timers_tick(APP_A);
    cg_test_int("A 的拍子只触发 A", s_fires_a, 1);
    cg_test_int("B 的定时器原地不动", s_fires_b, 0);

    cg_timers_tick(APP_B);
    cg_test_int("B 的拍子只触发 B", s_fires_b, 1);
    cg_test_int("A 不会多触发", s_fires_a, 1);

    cg_test_note("---- clear 只清自己 app 的 ----");
    cg_set_timer(APP_A, 50, cb_count_a, NULL);
    cg_set_timer(APP_B, 50, cb_count_b, NULL);
    cg_timers_clear(APP_A);

    s_fires_a = 0;
    cg_timers_tick(APP_A);
    cg_test_int("A 被清干净了", s_fires_a, 0);
    cg_timers_tick(APP_B);
    cg_test_int("B 的还活着", s_fires_b, 2);

    cg_timers_clear(APP_B);                 /* 收尾 */
}

CG_TEST_SUITE("定时器：一次性与 kill", test_timer_basic);
CG_TEST_SUITE("定时器：回调里重挂不同级触发", test_timer_rearm_same_tick);
CG_TEST_SUITE("定时器：多应用隔离", test_timer_two_apps);
CG_TEST_SUITE("定时器：同轮注销绝不触发", test_timer_kill_in_tick);
