/* cg_timer.c -- 一次性定时器
 *
 * 用途：在事件回调里推迟一件事做（例如先让界面重绘，再销毁控件 / 弹提示）。
 * 实现：后端跑一个 50ms 心跳，这里按累计毫秒数触发，精度约 50ms。
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

static struct cg_timer *g_timers = NULL;

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
    t->next = g_timers;
    g_timers = t;
    return t;
}

void cg_kill_timer(cg_timer_t *t)
{
    if (!t) return;
    t->fn = NULL;
    /* 真正摘链交给 tick / clear，避免遍历时改表 */
}

/** 后端心跳调用 */
void cg_timers_tick(cg_app_t *app)
{
    cg_timer_t **pp = &g_timers;

    while (*pp) {
        cg_timer_t *t = *pp;
        if (t->app != app) { pp = &t->next; continue; }
        if (!t->fn) {                       /* 已被 kill */
            *pp = t->next;
            free(t);
            continue;
        }
        t->elapsed_ms += CG_TICK_MS;
        if (t->elapsed_ms >= t->delay_ms) {
            void (*fn)(void *) = t->fn;
            void *user = t->user;
            *pp = t->next;
            free(t);
            fn(user);
            continue;
        }
        pp = &t->next;
    }
}

/** 应用关闭时清理（cg_app.c 调） */
void cg_timers_clear(cg_app_t *app)
{
    cg_timer_t **pp = &g_timers;
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
