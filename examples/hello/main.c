/* main.c -- CStackGUI 业务逻辑示例
 *
 * 这个文件只关心「界面上的事件发生时我要做什么」，
 * 完全不需要知道窗口、控件是怎么创建的 —— 那些都在运行时的 Win32 后端里。
 *
 * 编译：build.bat （产出 hello.exe，单文件，无依赖）
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态
 * 业务数据放在自己的结构里，通过 cg_widget_set_user() 挂到控件上，
 * 回调里用 cg_widget_get_user() 取回来。全局变量只是图省事。
 */
typedef struct {
    char  name[64];
    int   vip;
    int   level;
    int   click_count;
} biz_t;

static biz_t g_biz;

/* 把一行日志同时写到控制台和界面上的 lst_log */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    cg_widget_t *log = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[log] %s\n", buf);
    if (log) cg_widget_list_add(log, buf);
}

static void set_status(cg_app_t *app, const char *text)
{
    cg_widget_t *lab = cg_find(app, "lab_status");
    if (lab) cg_widget_set_text(lab, text);
}

/* 把业务状态推回界面：滑块 -> 进度条 + 文字 */
static void refresh_level(cg_app_t *app)
{
    cg_widget_t *lab = cg_find(app, "lab_level");
    cg_widget_t *prg = cg_find(app, "prg_level");
    char buf[64];

    snprintf(buf, sizeof(buf), "等级 %d", g_biz.level);
    if (lab) cg_widget_set_text(lab, buf);
    if (prg) cg_widget_set_value(prg, g_biz.level);
}

/* ------------------------------------------------------------ 回调
 * 回调名要和在 .cgui 里 events 写的一致（画布里填的就是这些名字）。
 */
/** 定时器回调：cg_set_timer 到点后在消息循环里执行，此时跳出原事件栈，安全 */
static void reset_level_later(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_widget_t *sld = cg_find(app, "sld_level");

    g_biz.level = 40;
    if (sld) cg_widget_set_value(sld, g_biz.level);
    refresh_level(app);
    log_line(app, "2 秒后自动回落到等级 40");
}

static void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;         /* on_ready 里挂进来的业务数据 */
    char buf[160];

    (void)ev;
    if (!biz) biz = &g_biz;

    biz->click_count++;
    snprintf(buf, sizeof(buf), "单击：%s，VIP=%s，等级 %d（第 %d 次单击）",
             biz->name[0] ? biz->name : "(未填姓名)",
             biz->vip ? "是" : "否", biz->level, biz->click_count);
    set_status(app, buf);
    log_line(app, "BTN 单击 #%d", biz->click_count);
}

static void on_ok_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char buf[160];

    (void)ev;
    /* 双击时顺手把等级拉满，演示「事件驱动改状态」 */
    g_biz.level = 100;
    {
        cg_widget_t *sld = cg_find(app, "sld_level");
        if (sld) cg_widget_set_value(sld, g_biz.level);   /* 会触发 on_level_change */
    }
    snprintf(buf, sizeof(buf), "双击：等级已拉满到 %d", g_biz.level);
    set_status(app, buf);
    log_line(app, "BTN 双击");

    /* 顺便演示 cg_set_timer：2 秒后自动回落到 40 */
    cg_set_timer(app, 2000, reset_level_later, app);
}

static void on_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_name");
    cg_widget_t *chk = cg_find(app, "chk_vip");
    cg_widget_t *sld = cg_find(app, "sld_level");

    (void)ev;
    memset(&g_biz, 0, sizeof(g_biz));
    g_biz.level = 40;

    if (edt) cg_widget_set_text(edt, "");     /* 界面 -> 会自动回灌 CHANGE，但值一样 */
    if (chk) cg_widget_set_checked(chk, 0);
    if (sld) cg_widget_set_value(sld, g_biz.level);
    refresh_level(app);
    set_status(app, "已重置");
    log_line(app, "重置完成");
}

static void on_quit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    log_line(app, "退出");
    printf("business summary: clicks=%d vip=%d level=%d name=%s\n",
           g_biz.click_count, g_biz.vip, g_biz.level, g_biz.name);
    cg_quit(app);
}

static void on_name_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    const char *text = cg_widget_text(self);

    (void)ev;
    snprintf(g_biz.name, sizeof(g_biz.name), "%s", text ? text : "");
    set_status(app, g_biz.name[0] ? "姓名已记录" : "姓名已清空");
}

static void on_name_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    log_line(app, "姓名回车提交: %s", cg_widget_text(self));
    set_status(app, "回车提交");
}

static void on_vip_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    g_biz.vip = cg_widget_checked(self);
    log_line(app, "VIP = %s", g_biz.vip ? "开" : "关");
    set_status(app, g_biz.vip ? "VIP 已开启" : "VIP 已关闭");
}

static void on_level_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    g_biz.level = cg_widget_value(self);
    refresh_level(app);
    log_line(app, "等级 -> %d", g_biz.level);
}

/* ------------------------------------------------------------ 界面就绪
 * 在窗口建好、消息循环开始之前调用一次。
 * 这里做的是「启动时该干的事」：把业务数据挂到控件上、按业务状态摆初始界面。
 * 回调里的 user 参数就是下面 cg_widget_set_user() 挂进去的东西。
 */
static void on_ready(cg_app_t *app, void *user)
{
    biz_t *biz = (biz_t *)user;
    cg_widget_t *w;

    biz->level = 40;

    /* 业务数据挂到需要它的控件上，回调里就能直接拿到 */
    if ((w = cg_find(app, "btn_ok")) != NULL)  cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "chk_vip")) != NULL) cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "edt_name")) != NULL) cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "lst_log")) != NULL) cg_widget_set_user(w, biz);

    /* 用业务状态摆一次初始界面（.cgui 里给的是设计态默认值） */
    if ((w = cg_find(app, "sld_level")) != NULL) cg_widget_set_value(w, biz->level);
    refresh_level(app);
    set_status(app, "就绪：界面由 hello.cgui 描述，逻辑在 main.c");

    log_line(app, "启动完成，共 %d 个控件", cg_widget_count(app));
}

/* ------------------------------------------------------------ 绑定表
 * 左边是 .cgui 里写的名字，右边是本文件里的函数。
 * 运行时启动时按名字查表，所以画布改事件名时这里同步改一下就行。
 */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_ok_click",      on_ok_click),
    CG_BIND("on_ok_dblclick",   on_ok_dblclick),
    CG_BIND("on_reset_click",   on_reset_click),
    CG_BIND("on_quit_click",    on_quit_click),
    CG_BIND("on_name_change",   on_name_change),
    CG_BIND("on_name_submit",   on_name_submit),
    CG_BIND("on_vip_change",    on_vip_change),
    CG_BIND("on_level_change",  on_level_change),
    CG_BIND_END
};

/* ------------------------------------------------------------ 入口 */

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 双击时先给 click 再给 dblclick */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */
    desc.on_ready = on_ready;
    desc.ready_user = &g_biz;

    /* --selftest 走命令行自检：解析 + 打印控件树 + 合成事件，不开窗口 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "hello.cgui";
        return cg_app_selftest(&desc);
    }
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "hello.cgui";
        desc.trace_doc = 1;
    } else {
        desc.cgui_path = (argc > 1) ? argv[1] : "hello.cgui";
    }

    printf("CStackGUI %s -- 界面文件: %s\n", CG_VERSION_STRING, desc.cgui_path);
    return cg_app_run(&desc);
}
