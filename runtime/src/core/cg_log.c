/**
 * @file    cg_log.c
 * @brief   日志：stderr 镜像 + 追加写日志文件
 * @details 为什么需要文件：发布版是 -mwindows（没有控制台），所有 fprintf(stderr, ...)
 *          用户一个字都看不到 —— 双击 exe 只会"什么都没发生"。
 *          日志路径由后端在启动时用 cg_log_open() 指定（%LOCALAPPDATA%\<exe名>\app.log），
 *          没指定时只写 stderr。每行写完就 fflush，崩溃时最后一行也不会丢。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *s_log = NULL;
static char  s_path[1400] = { 0 };
static int   s_level = CG_LOG_INFO;

/**
 * @brief 等级标签（对齐日志里的列宽，方便 grep）
 * @param level 日志等级（CG_LOG_INFO / WARN / ERROR）
 * @return const char * 静态字符串，定长 5 字符，别 free
 */
static const char *level_tag(int level)
{
    switch (level) {
    case CG_LOG_WARN:  return "WARN ";
    case CG_LOG_ERROR: return "ERROR";
    default:           return "INFO ";
    }
}

/**
 * @brief 打开日志文件（追加）
 * @param path 日志文件路径（UTF-8）；传 NULL 或空串表示只写 stderr
 * @return void 无返回值（打不开就退化成只有 stderr，不打扰用户）
 */
void cg_log_open(const char *path)
{
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    s_path[0] = '\0';
    if (!path || !*path) return;

    s_log = fopen(path, "a");
    if (!s_log) return;                 /* 打不开就退化成只有 stderr，不打扰用户 */
    snprintf(s_path, sizeof(s_path), "%s", path);
}

/**
 * @brief 关闭日志文件（之后 cg_log 只写 stderr）
 * @return void 无返回值
 * @note 进程收尾时调
 */
void cg_log_close(void)
{
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
}

/**
 * @brief 取当前日志文件路径
 * @return const char * 内部缓冲，别 free；没打开返回 NULL
 */
const char *cg_log_file(void)
{
    return s_path[0] ? s_path : NULL;
}

/**
 * @brief 设日志的过滤等级
 * @param level 只记 >= 它的条目
 * @return void 无返回值（默认 CG_LOG_INFO）
 */
void cg_log_set_level(int level)
{
    s_level = level;
}

/**
 * @brief 写一条日志：镜像到 stderr，并追加到日志文件（带时间戳）
 * @param level 日志等级，低于当前过滤等级的条目直接丢弃
 * @param fmt printf 风格格式串
 * @return void 无返回值（没打开日志文件时只写 stderr）
 */
void cg_log(int level, const char *fmt, ...)
{
    char    msg[1024];
    va_list ap;

    if (!fmt || level < s_level) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* 开发时（有控制台）走这里看得最顺；GUI 版这些字进不了任何地方 */
    fprintf(stderr, "[cstackgui] %s\n", msg);

    if (!s_log) return;
    {
        time_t    t = time(NULL);
        struct tm *tmv = localtime(&t);
        char      ts[32] = "";
        if (tmv) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
        fprintf(s_log, "%s [%s] %s\n", ts[0] ? ts : "-", level_tag(level), msg);
        fflush(s_log);      /* 崩溃/被杀时不至于丢最后几行 */
    }
}
