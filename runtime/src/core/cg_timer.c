/**
 * @file    cg_timer.c
 * @brief   一次性定时器：事件回调里推迟一件事做（先让界面重绘，再销毁控件 / 弹提示），
 *          后端跑 50ms 心跳，按累计毫秒数触发，精度约 50ms
 */
#include "cg_internal.h"

#include <stdlib.h>

#define CG_TICK_MS 50

struct cg_timer {
    cg_app_t    *app;
    int          delay_ms;
    int          elapsed_ms;
    void       (*fn)(void *);
    void        *user;
    int          killed;     /* 1 = 已被 cg_kill_timer 注销或本轮已触发：绝不再回调 */
    struct cg_timer *next;
};

static struct cg_timer *s_timers = NULL;

/**
 * @brief 建一个一次性定时器，delay_ms 毫秒后在消息循环里调 fn(user)
 * @param app 所属应用（tick 时只触发同一 app 的定时器）
 * @param delay_ms 延迟毫秒数，负数按 0 处理
 * @param fn 到点要执行的函数；置 NULL 等价于取消
 * @param user 传给 fn 的业务数据
 * @return cg_timer_t* 定时器句柄（可传给 cg_kill_timer），失败返回 NULL
 */
cg_timer_t *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user)
{
    cg_timer_t *t;

    if (!app || !fn) return NULL;
    if (delay_ms < 0) delay_ms = 0;

    t = (cg_timer_t *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->app = app;
    t->delay_ms = delay_ms;
    t->fn = fn;
    t->user = user;
    t->next = s_timers;
    s_timers = t;
    return t;
}

/**
 * @brief 取消一个定时器（真正摘链交给 tick / clear，避免遍历时改表）
 * @param t cg_set_timer 返回的句柄，NULL 安全
 * @return void 无返回值
 */
void cg_kill_timer(cg_timer_t *t)
{
    if (!t) return;
    t->fn = NULL;
    t->killed = 1;      /* 本轮 tick 里哪怕还没遍历到它，也绝不再触发 */
}

/**
 * @brief 后端心跳调用：推进同 app 的所有定时器，到点的摘链并执行
 * @param app 当前应用
 * @return void 无返回值
 * @note **回调里 cg_set_timer 新挂的定时器，本轮绝不能再触发**。串口助手那种
 *       "回调里把自己再挂一次当周期"是标准用法：旧实现边遍历边执行，新挂的
 *       落在遍历指针前面，同一轮就被又触发一遍 —— 无限循环，窗口直接未响应。
 *       所以这里先把整条链摘下来再遍历；遍历期间新挂的都落在 s_timers 上，
 *       自然留给下一拍。
 * @note UAF 防护：已注销 / 本轮已触发的定时器，在本轮 tick 内**绝不**再回调；
 *       且它们统一留到"所有回调跑完"之后才 free —— 这样回调里哪怕拿旧句柄
 *       再 cg_kill_timer 一次，也只是写一块仍然有效的内存（无害），不会被
 *       cg_set_timer 的新分配复用而踩坏新定时器、也不会写穿已释放的堆。 */
void cg_timers_tick(cg_app_t *app)
{
    cg_timer_t  *pending = s_timers;
    cg_timer_t  *keep    = NULL;
    cg_timer_t **kt      = &keep;
    cg_timer_t  *fired   = NULL;   /* 本轮已触发 / 已注销：末尾统一释放，
                                    * 回调跑完前内存保持有效 */

    s_timers = NULL;

    while (pending) {
        cg_timer_t *t = pending;
        pending = t->next;

        if (t->app != app) {                /* 别家的：原样保留 */
            *kt = t; kt = &t->next;
            continue;
        }
        if (!t->fn || t->killed) {          /* 已被 kill / 本轮已触发：绝不回调 */
            t->next = fired; fired = t;     /* 先挂到 fired，末尾再 free */
            continue;
        }
        t->elapsed_ms += CG_TICK_MS;
        if (t->elapsed_ms < t->delay_ms) {  /* 还没到点 */
            *kt = t; kt = &t->next;
            continue;
        }
        /* 到点：立刻标记失效并挂到 fired，回调返回后再释放。
         * ① one-shot 只触发一次：标记 killed，回调里拿旧句柄 cg_kill_timer 也是空操作；
         * ② 回调跑完前这块内存仍有效，不会被 cg_set_timer 的新分配复用、也不会被
         *    stale 句柄写穿；③ 本轮已触发 / 已注销的都进 fired，绝不会二次回调。 */
        {
            void (*fn)(void *) = t->fn;
            void *user = t->user;
            t->killed = 1;
            t->fn = NULL;
            t->next = fired; fired = t;
            fn(user);
        }
    }
    *kt = NULL;

    /* 存活的接回去（挂在回调期间新设置的后面，反正下一拍才看它们） */
    if (keep) {
        cg_timer_t **tail = &s_timers;
        while (*tail) tail = &(*tail)->next;
        *tail = keep;
    }

    /* 所有回调结束后再释放本轮已触发 / 已注销的定时器：此刻没有任何代码
     * 还持有这些句柄，释放安全，也不会有悬空写。 */
    while (fired) {
        cg_timer_t *n = fired->next;
        free(fired);
        fired = n;
    }
}

/**
 * @brief 应用关闭时清理该 app 的全部定时器（cg_app.c 调）
 * @param app 正在关闭的应用
 * @return void 无返回值
 */
void cg_timers_clear(cg_app_t *app)
{
    cg_timer_t **pp = &s_timers;
    while (*pp) {
        cg_timer_t *t = *pp;
        if (t->app == app) {
            *pp = t->next;
            free(t);
            continue;
        }
        pp = &t->next;
    }
}
