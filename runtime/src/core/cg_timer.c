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
}

/**
 * @brief 后端心跳调用：推进同 app 的所有定时器，到点的摘链并执行
 * @param app 当前应用
 * @return void 无返回值
 * @note **回调里 cg_set_timer 新挂的定时器，本轮绝不能再触发**。串口助手那种
 *       "回调里把自己再挂一次当周期"是标准用法：旧实现边遍历边执行，新挂的
 *       落在遍历指针前面，同一轮就被又触发一遍 —— 无限循环，窗口直接未响应。
 *       所以这里先把整条链摘下来再遍历；遍历期间新挂的都落在 s_timers 上，
 *       自然留给下一拍。 */
void cg_timers_tick(cg_app_t *app)
{
    cg_timer_t  *pending = s_timers;
    cg_timer_t  *keep    = NULL;
    cg_timer_t **kt      = &keep;

    s_timers = NULL;

    while (pending) {
        cg_timer_t *t = pending;
        pending = t->next;

        if (t->app != app) {                /* 别家的：原样保留 */
            *kt = t; kt = &t->next;
            continue;
        }
        if (!t->fn) {                       /* 已被 kill */
            free(t);
            continue;
        }
        t->elapsed_ms += CG_TICK_MS;
        if (t->elapsed_ms < t->delay_ms) {  /* 还没到点 */
            *kt = t; kt = &t->next;
            continue;
        }
        {
            void (*fn)(void *) = t->fn;
            void *user = t->user;

            free(t);
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
