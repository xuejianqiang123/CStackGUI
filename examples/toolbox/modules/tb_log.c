/**
 * @file    tb_log.c
 * @brief   toolbox 公共小工具实现：日志列表（含镜像/时间戳开关）+ 状态栏回执
 */
#include "tb_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "tb_opts.h"   /**< 日志镜像 / 时间戳受沙箱开关控制 */

/**
 * @brief 往日志列表框追加一行；镜像 stdout 与时间戳都受沙箱开关控制
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void tb_log_line(cg_app_t *app, const char *fmt, ...)
{
    char        buf[256];
    char        line[300];
    va_list     ap;
    cg_widget_t *lst = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (tb_opt_get(1)) {                                  /* LogTimestamp：行首加时间戳 */
        char *now = cg_time_now(NULL);
        snprintf(line, sizeof(line), "[%s] %s", now ? now : "--:--:--", buf);
        cg_free(now);
    } else {
        snprintf(line, sizeof(line), "%s", buf);
    }

    if (lst) {
        if (cg_widget_list_count(lst) > 400) cg_widget_list_clear(lst);
        cg_widget_list_add(lst, line);
        cg_widget_list_scroll_bottom(lst);
    }
    if (tb_opt_get(0)) printf("[log] %s\n", line);        /* MirrorLog：镜像到控制台 */
}

/**
 * @brief 把最近一次结果写进状态栏第 1 段（第 0 段固定放计数）
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void tb_status_msg(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_status_set(cg_find(app, "sb_main"), 1, buf);
}
