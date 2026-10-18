/**
 * @file    test_log.c
 * @brief   日志：等级过滤 / 文件追加 / 时间戳 / **开档与运行中两轮轮转**
 * @details 为什么值得单独一套：日志是"出事后唯一的证据"，而它最关键的两个行为
 *          长期没有直接判据 —— ① 2026-10-17 加的开档轮转（旧文件超 5MB 挪 .old）；
 *          ② 2026-10-18 加的运行中轮转（24/7 无人值守场景单个文件无界增长，
 *          二轮会诊挂账项）。轮转的"失败不截断原文件"（磁盘满时保留现场）也由
 *          "轮转后新档仍可用、旧内容进了 .old"这组断言间接钉住。
 *          写路径自 2026-10-18 起持 C11 自旋锁（kit 读线程也在写日志），
 *          "轮转后还能继续写"是本套件对那条锁的回归判据。
 */
#include "cg_test.h"
#include "cstackgui.h"
#include "cg_internal.h"     /* cg_log_open/close/file 是 core 内部 API（公开头只有 cg_log/set_level） */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 与 cg_log.c 的 LOG_ROTATE_MAX 保持一致；改了那边这里要跟着改 —— 对不上不会
 * 静默通过：预填量跨过真实阈值时轮转会提前到开档时发生，本套件红着提醒。 */
#define LOG_TEST_MAX (5 * 1024 * 1024)
/** 预填到"差一点就过线"：留 8KB 给随后几条日志跨线 */
#define LOG_TEST_SEED (LOG_TEST_MAX - 8192)

/**
 * @brief 测试用日志文件路径（exe 目录下；run-tests 在 build\tests 里跑，产物不进仓库）
 * @return const char * 静态缓冲，别 free
 */
static const char *log_path(void)
{
    static char buf[1400];
    char       *dir = cg_app_dir();
    char       *p = cg_path_join(dir, "log-test.log");

    snprintf(buf, sizeof(buf), "%s", p ? p : "log-test.log");
    cg_free(p);
    cg_free(dir);
    return buf;
}

/**
 * @brief 测试用日志的轮转副本路径（<日志>.old）
 * @return const char * 静态缓冲，别 free
 */
static const char *old_path(void)
{
    static char buf[1400];

    snprintf(buf, sizeof(buf), "%s.old", log_path());
    return buf;
}

/**
 * @brief 清掉两处残留（上一轮跑测试留下的）
 * @return void 无返回值
 */
static void log_cleanup(void)
{
    cg_file_delete(log_path());
    cg_file_delete(old_path());
}

/**
 * @brief 预填一个指定大小的文件（内容是单字符重复，无 NUL，文本读也不会截断）
 * @param path 目标路径
 * @param size 字节数
 * @param ch 填充字符
 * @return void 无返回值（申请失败只记账，后续断言会红）
 */
static void log_fill(const char *path, size_t size, char ch)
{
    char  *buf = (char *)malloc(size);
    size_t i;

    if (!buf) {
        cg_test_ok("预填缓冲申请失败", 0);
        return;
    }
    for (i = 0; i < size; i++) buf[i] = ch;
    cg_test_int("预填成功", cg_file_write_bytes(path, buf, (int)size), 0);
    free(buf);   /* 本函数自己的 malloc，用标准 free（memcheck 配对由 --wrap 兜） */
}

/**
 * @brief 断言首行是 "YYYY-MM-DD HH:MM:SS [LEVEL] " 形状的时间戳行
 * @param txt 日志文件内容（非 NULL）
 * @return int 1 = 形状对得上
 */
static int log_ts_shape(const char *txt)
{
    return txt && isdigit((unsigned char)txt[0]) && txt[4] == '-' && txt[7] == '-'
        && txt[10] == ' ' && txt[13] == ':' && txt[16] == ':';
}

/**
 * @brief 日志三件事：等级过滤 / 文件追加与时间戳 / 开档与运行中两轮轮转
 * @return void 无返回值
 */
static void test_log_file(void)
{
    const char *p = log_path();
    const char *o = old_path();
    char       *txt;
    char        msg[901];
    int         k;

    log_cleanup();

    cg_test_note("---- 基本：开档写两条再关档，内容 / 时间戳 / 路径查询 ----");
    cg_log_open(p);
    cg_test_str("cg_log_file 返回刚开的路径", cg_log_file(), p);
    cg_log(CG_LOG_INFO, "log-test marker-%d", 42);
    cg_log(CG_LOG_WARN, "second line");
    cg_log_close();
    /* 关档后仍返回上一个路径是**刻意语义**（cg_internal.h 的契约）：致命错误对话框与
     * SEH 崩溃处理器靠它在"关档之后才崩"的时刻仍能指出日志在哪 —— 那时用户比任何
     * 时候都需要这个路径。这里钉住它，防后人"顺手修干净"把崩溃对话框的指南删掉。 */
    cg_test_str("关档后仍返回上一个路径（崩溃对话框要靠它）", cg_log_file(), p);
    /* 只写 stderr 模式（NULL 开档）才真的没有路径 */
    cg_log_open(NULL);
    cg_test_ok("NULL 开档（只写 stderr）后返回 NULL", cg_log_file() == NULL);
    cg_log_close();

    txt = cg_file_read(p);
    cg_test_ok("写出去读得回来", txt != NULL);
    cg_test_ok("  第一条内容在（marker-42）", txt && strstr(txt, "log-test marker-42") != NULL);
    cg_test_ok("  第二条也在", txt && strstr(txt, "second line") != NULL);
    cg_test_ok("  时间戳是 年-月-日 时:分:秒 形状", log_ts_shape(txt));
    cg_test_ok("  等级标签 [INFO ] 在", txt && strstr(txt, "[INFO ]") != NULL);
    cg_test_ok("  等级标签 [WARN ] 在", txt && strstr(txt, "[WARN ]") != NULL);
    cg_free(txt);

    cg_test_note("---- 等级过滤：设 ERROR 之后 INFO 不落盘、ERROR 落 ----");
    cg_log_set_level(CG_LOG_ERROR);
    cg_file_delete(p);
    cg_log_open(p);
    cg_log(CG_LOG_INFO, "should-not-appear");
    cg_log(CG_LOG_ERROR, "should-appear");
    cg_log_close();
    cg_log_set_level(CG_LOG_INFO);   /* 还原：别把过滤等级漏给后面的套件 */
    txt = cg_file_read(p);
    cg_test_ok("被过滤的 INFO 不在文件里", txt && strstr(txt, "should-not-appear") == NULL);
    cg_test_ok("ERROR 在文件里", txt && strstr(txt, "should-appear") != NULL);
    cg_free(txt);

    cg_test_note("---- 模块标签：cg_logm 带 [模块]、cg_log 不带、NULL/空串等同不标 ----");
    log_cleanup();
    cg_log_open(p);
    cg_logm(CG_LOG_ERROR, "serial", "tagged line");
    cg_log(CG_LOG_WARN, "untagged line");
    cg_logm(CG_LOG_INFO, NULL, "null mod falls back to untagged");
    cg_logm(CG_LOG_INFO, "", "empty mod also untagged");
    cg_log_close();
    txt = cg_file_read(p);
    cg_test_ok("  cg_logm 的行带 [ERROR] [serial] 标签", txt && strstr(txt, "[ERROR] [serial] tagged line") != NULL);
    cg_test_ok("  cg_log 的行不带模块标签（[WARN ] 直接接正文）", txt && strstr(txt, "[WARN ] untagged line") != NULL);
    cg_test_ok("  mod=NULL 等同不标（老调用方行为不变）", txt && strstr(txt, "[INFO ] null mod falls back") != NULL);
    cg_test_ok("  mod 空串等同不标", txt && strstr(txt, "[INFO ] empty mod also") != NULL);
    cg_test_ok("  时间戳仍在行首（格式只做增量，旧解析不破）", log_ts_shape(txt));
    cg_free(txt);

    cg_test_note("---- 开档轮转：旧文件超限，开档即挪 .old，新档从头写 ----");
    log_cleanup();
    log_fill(p, LOG_TEST_MAX + 1024, 'x');
    cg_test_ok("  开档前 .old 不存在", cg_file_size(o) < 0);
    cg_log_open(p);
    cg_log(CG_LOG_INFO, "after-open-rotation");
    cg_log_close();
    cg_test_ok("  开档后 .old 出现（装着旧内容）", cg_file_size(o) >= LOG_TEST_MAX);
    cg_test_ok("  新档很小（远小于上限）", cg_file_size(p) > 0 && cg_file_size(p) < 4096);
    txt = cg_file_read(p);
    cg_test_ok("  新档里有开档后写的那条", txt && strstr(txt, "after-open-rotation") != NULL);
    cg_free(txt);

    cg_test_note("---- 运行中轮转：开档不超限，写到过线那一刻自己换档 ----");
    log_cleanup();
    log_fill(p, LOG_TEST_SEED, 'y');
    cg_log_open(p);
    cg_test_ok("  开档没触发轮转（还差一截）", cg_file_size(o) < 0);
    cg_test_ok("  开档后文件就是预填大小（字节计数已播种）", cg_file_size(p) == LOG_TEST_SEED);

    /* 12 条约 930 字节的日志：前几条把 8KB 余量磨完，过线那一刻换档 */
    memset(msg, 'z', sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    for (k = 0; k < 12; k++) cg_log(CG_LOG_INFO, "%s tail-%02d", msg, k);
    cg_log_close();

    cg_test_ok("  写过线后 .old 出现（预填内容整份搬过去）", cg_file_size(o) >= LOG_TEST_SEED);
    cg_test_ok("  换档后当前文件重新变小", cg_file_size(p) > 0 && cg_file_size(p) < 65536);
    txt = cg_file_read(p);
    cg_test_ok("  过线前写的内容不在新档里（tail-00）", txt && strstr(txt, "tail-00") == NULL);
    cg_test_ok("  最后一条还在新档里（尾部没丢，tail-11）", txt && strstr(txt, "tail-11") != NULL);
    cg_free(txt);

    cg_test_note("---- 轮转之后还能继续写（持锁换档没把后续写堵死）----");
    cg_log_open(p);
    cg_log(CG_LOG_ERROR, "after-runtime-rotation");
    cg_log_close();
    txt = cg_file_read(p);
    cg_test_ok("  轮转后重开照常写", txt && strstr(txt, "after-runtime-rotation") != NULL);
    cg_free(txt);

    log_cleanup();
}

CG_TEST_SUITE("core：日志（等级 / 文件 / 时间戳 / 开档与运行中两轮轮转）", test_log_file);
