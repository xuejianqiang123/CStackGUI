/**
 * @file    main.c
 * @brief   入门 03：事件进阶 —— 时钟、键盘、鼠标、关闭拦截
 * @details 入门 01/02 用的都是"控件自己"的事件（click / change / select）。
 *          这一个例子讲另外一大类：**窗口级事件**。它们写在 .cgui 的 app.events 里，
 *          绑在窗口伪控件（id = CG_WINDOW_ID）上，和控件事件共用同一套 CG_BIND 机制：
 *
 *            ready      界面刚建好（还没显示）：适合做初始化
 *            close      用户点了关闭（含 Alt+F4、Esc）：cg_cancel_close() 可以拦下来
 *            mousemove  鼠标在窗口上移动：坐标用 cg_event_x() / cg_event_y() 读
 *            keydown    按下按键：键码用 cg_event_key()，功能键用 cg_event_keys()
 *
 *          还有一个特殊的控件：timer（时钟）。它没有窗口，靠 backend 的心跳按 period
 *          毫秒派发 tick 事件 —— 凡是"每隔一会儿做点什么"都用它，别自己开线程。
 *
 *          为什么事件参数要用函数读？因为回调签名固定是三参数（self/ev/user），
 *          多出来的坐标、键码、滚轮量只能挂在"最近一次事件"上，用 cg_event_* 系列取。
 *
 * 界面在 lesson03_events.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson03_events
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int ticks;        /**< 时钟走了多少下 */
    int running;      /**< 时钟当前开没开 */
    int close_asked;  /**< 已经拦过一次关闭 */
} biz_t;

static biz_t s_biz;

/** @brief 往日志框追加一行（printf 风格，顺手滚到最后一行） */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;
    cg_widget_t *log = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_widget_list_add(log, buf);
    cg_widget_list_scroll_bottom(log);
    printf("[log] %s\n", buf);            /* 调试版有控制台，同步印一份 */
}

/** @brief 时钟周期到（period 毫秒一次） */
static void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     *now;

    (void)ev;
    (void)user;

    s_biz.ticks++;
    /* 进度条是"只被程序写"的控件：直接 set_value 就是刷新（不用手动重绘） */
    cg_widget_set_value(cg_find(app, "prg_tick"), s_biz.ticks % 101);

    /* cg_time_now 返回堆内存：拼完记得还 */
    now = cg_time_now(NULL);              /* NULL = "2026-09-21 02:33:53" 这种默认格式 */
    {
        char *text = cg_str_format("已走 %d 秒　当前时间 %s", s_biz.ticks, now ? now : "?");
        cg_widget_set_text(cg_find(app, "lab_clock"), text);
        cg_free(text);
    }
    cg_free(now);

    if (s_biz.ticks % 5 == 0) log_line(app, "第 %d 次 tick", s_biz.ticks);   /* 别每下都写日志，太吵 */
}

/**
 * @brief 开始 / 停止共用：用 cg_widget_id() 分辨按钮
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 */
static void on_timer_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tmr = cg_find(app, "tmr_tick");
    const char  *id = cg_widget_id(self);

    (void)ev;
    (void)user;

    if (cg_str_equal(id, "btn_start")) {
        /* period 是毫秒；设成 >0 会立刻按新周期重新计时，设成 0 就是停 */
        cg_widget_set_period(tmr, 1000);
        s_biz.running = 1;
        log_line(app, "时钟已启动（period = %d 毫秒）", cg_widget_period(tmr));
    } else {
        cg_widget_set_period(tmr, 0);
        s_biz.running = 0;
        log_line(app, "时钟已停止");
    }

    /* 两个按钮互相把对方"灰掉"，用户一眼看出当前状态 */
    cg_widget_set_enabled(cg_find(app, "btn_start"), !s_biz.running);
    cg_widget_set_enabled(cg_find(app, "btn_stop"), s_biz.running);
    cg_status_setf(cg_find(app, "sb_main"), 0, "时钟%s，已走 %d 秒",
                   s_biz.running ? "运行中" : "已停止", s_biz.ticks);
}

/**
 * @brief 鼠标在窗口上移动（窗口级事件）
 * @param self 窗口伪控件
 * @param ev mousemove
 * @param user 未使用
 */
static void on_mouse_move(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    /* 坐标是相对窗口客户区的；非鼠标事件会返回 0 */
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0, "鼠标位置 (%d, %d)",
                   cg_event_x(self), cg_event_y(self));
}

/**
 * @brief 按下按键（窗口级事件）
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 未使用
 */
static void on_key_down(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app  = cg_app_of(self);
    int       key  = cg_event_key(self);    /* 虚拟键码，和 Win32 的 VK_* 一样 */
    int       mods = cg_event_keys(self);   /* 1=Ctrl 2=Shift 4=Alt，可以位或 */
    char      buf[128];

    (void)ev;
    (void)user;

    /* 拼一段长度可控的文字用栈上的 buf 就够，比 cg_str_format 省一次分配/释放。
     * 要拼不定长内容时才用 cg_str_format —— 那条路返回堆串，必须 cg_free。 */
    snprintf(buf, sizeof(buf), "刚按下键码 %d（Ctrl=%d Shift=%d Alt=%d）",
             key, (mods & 1) ? 1 : 0, (mods & 2) ? 1 : 0, (mods & 4) ? 1 : 0);
    cg_widget_set_text(cg_find(app, "lab_key"), buf);
}

/** @brief 界面建好（还没显示）：初始化界面状态放这里最稳 */
static void on_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_widget_set_period(cg_find(app, "tmr_tick"), 1000);   /* 上来就让时钟走 */
    s_biz.running = 1;
    cg_widget_set_enabled(cg_find(app, "btn_start"), 0);
    log_line(app, "界面就绪，时钟自动启动");
}

/**
 * @brief 关闭前：第一次拦下来问一句，第二次放行
 * @param self 窗口伪控件
 * @param ev close
 * @param user 未使用
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (s_biz.close_asked) return;      /* 第二次点了：不再拦，窗口正常关闭 */

    s_biz.close_asked = 1;
    /* --selftest 不开窗（也会走到这里）：先问一句有没有真窗口，
     * 否则自检会被一个没人点的消息框卡死。 */
    if (cg_app_has_window(app)) {
        cg_message_box(cg_find(app, "lst_log"),
                       "再点一次关闭就真的退出。\n\n（这个提示来自 close 回调里的 cg_cancel_close）",
                       "确认关闭", 3);
    }
    cg_cancel_close(app);               /* 取消这一次关闭 */
    log_line(app, "拦截了一次关闭");
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_tick",       on_tick),
    CG_BIND("on_timer_click", on_timer_click),
    CG_BIND("on_mouse_move", on_mouse_move),
    CG_BIND("on_key_down",   on_key_down),
    CG_BIND("on_ready",      on_ready),
    CG_BIND("on_close",      on_close),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson03_events.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson03_events.cgui";
    return cg_app_run(&desc);
}
