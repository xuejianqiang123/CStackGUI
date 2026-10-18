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
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *s_log = NULL;
static char  s_path[1400] = { 0 };
static int   s_level = CG_LOG_INFO;
/** 本次开档以来写进文件的字节数（运行中轮转的判据；开档时用现有文件大小播种） */
static long  s_bytes = 0;

/** 写路径自旋锁：轮转要 fclose + 重开 s_log，而 kit 的读线程（串口/网口退避 WARN、
 *  丢弃上报）也在调 cg_log —— 不锁就是"别的线程拿着已 fclose 的 FILE* 去 fprintf"
 *  的 use-after-free（2026-10-18 补运行中轮转时识别的竞态）。
 *  用 C11 atomic 而不是 CRITICAL_SECTION：core 不许碰 Win32（分层契约第一条），
 *  stdatomic 是平台中立的标准 C。争用几乎不存在（5MB 才转一次，16B/s 的典型速率
 *  下约 9 天过一次），自旋代价可忽略；轮转期间持锁跨磁盘 I/O 是本设计唯一的代价，
 *  换的是"不丢证据、不 UAF"。 */
static atomic_flag s_log_lock = ATOMIC_FLAG_INIT;

/**
 * @brief 拿写路径锁（自旋等）
 * @return void 无返回值
 */
static void log_lock(void)
{
    while (atomic_flag_test_and_set_explicit(&s_log_lock, memory_order_acquire)) {
        /* 自旋：临界区极短（一次 fprintf + fflush），只有轮转才长，而轮转 5MB 一次 */
    }
}

/**
 * @brief 放写路径锁
 * @return void 无返回值
 */
static void log_unlock(void)
{
    atomic_flag_clear_explicit(&s_log_lock, memory_order_release);
}

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

/** 日志轮转上限：开档前若旧文件超过它，整份挪到 "<路径>.old" 再开新档（只留一份 .old）。
 *  长期运行的便携 exe（无人值守现场）日志只增不减，磁盘一满恰好就是最需要日志的时刻
 *  —— 2026-10-17 架构评审"三个无界增长"整改。 */
#define LOG_ROTATE_MAX (5 * 1024 * 1024)

/**
 * @brief 开档前的轮转检查：旧日志超过 LOG_ROTATE_MAX 就整份拷到 .old 并截断原文件
 * @param path 日志文件路径（UTF-8）
 * @return void 无返回值（没超限、打不开或拷贝失败都什么都不做，最坏是继续追加旧文件）
 * @note 纯 std C 实现（core 层不许碰 Win32 API）；不用 rename —— 窄字符 rename 在
 *       Windows 上走 ANSI 代码页，中文路径会失败，所以是"读拷+截断"两步
 */
static void log_rotate_if_big(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  sz;

    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    sz = ftell(f);
    fclose(f);
    if (sz < LOG_ROTATE_MAX) return;

    {
        FILE  *in = fopen(path, "rb");
        FILE  *out = NULL;
        char   oldp[sizeof(s_path) + 8];
        char   buf[4096];
        size_t n;
        int    copied = 0;

        snprintf(oldp, sizeof(oldp), "%s.old", path);
        if (in) out = fopen(oldp, "wb");
        if (in && out) {
            copied = 1;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                if (fwrite(buf, 1, n, out) != n) { copied = 0; break; }
            }
            if (ferror(in)) copied = 0;
        }
        if (in) fclose(in);
        if (out) {
            if (fclose(out) != 0) copied = 0;
            if (!copied) remove(oldp);   /* 半份 .old 没有意义：删掉它，原文件保留 = 证据还在 */
        }
        if (!copied) {
            /* 拷贝失败（多半是磁盘满 —— 恰是轮转最该工作的时刻）：**绝不截断原文件**。
             * 宁可让下一次 append 继续追加（写不进去也只是丢新日志），也绝不在最需要
             * 证据的时刻销毁证据（2026-10-17 二轮评审"清理动作自身失败要保留现场"）。 */
            return;
        }
    }
    f = fopen(path, "w");   /* 拷贝成功才截断开新档；失败就让后面的 append 继续写旧文件，无害 */
    if (f) {
        fprintf(f, "[log] 上一份日志（%ld 字节）已轮转到 .old\n", sz);
        fclose(f);
    }
}

/**
 * @brief 运行中轮转：关当前档，走开档时那套轮转（拷 .old + 截断），再重开追加
 * @return void 无返回值
 * @note **调用方必须已持有 s_log_lock**（它换 s_log 本身）。重开失败就退化成只写
 *       stderr（与 cg_log_open 打不开时同一口径），且字节计数归零 —— 磁盘满时轮转
 *       本来就转不动，再等累满一个阈值才重试，比每次写都重试省。
 */
static void log_rotate_runtime(void)
{
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    log_rotate_if_big(s_path);
    s_log = fopen(s_path, "a");
    s_bytes = 0;
}

/**
 * @brief 打开日志文件（追加）
 * @param path 日志文件路径（UTF-8）；传 NULL 或空串表示只写 stderr
 * @return void 无返回值（打不开就退化成只有 stderr，不打扰用户）
 * @note 开档前若旧文件超过 LOG_ROTATE_MAX 会先轮转（见 log_rotate_if_big）；
 *       之后运行中的大小由 cg_log 的写路径盯着（见 log_rotate_runtime）。
 */
void cg_log_open(const char *path)
{
    log_lock();
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    s_path[0] = '\0';
    s_bytes = 0;
    if (!path || !*path) {
        log_unlock();
        return;
    }

    log_rotate_if_big(path);
    s_log = fopen(path, "a");
    if (!s_log) {                    /* 打不开就退化成只有 stderr，不打扰用户 */
        log_unlock();
        return;
    }
    snprintf(s_path, sizeof(s_path), "%s", path);
    /* 用现有文件大小播种：开档轮转失败（磁盘满）时文件可能仍超限，播种让紧接着的
     * 第一次写就重试轮转，而不是再白写满一个阈值。追加流 fseek 不影响写位置
     * （O_APPEND 语义由系统保证），这里只为量出文件尾。 */
    if (fseek(s_log, 0, SEEK_END) == 0) {
        long pos = ftell(s_log);

        if (pos > 0) s_bytes = pos;
    }
    log_unlock();
}

/**
 * @brief 关闭日志文件（之后 cg_log 只写 stderr）
 * @return void 无返回值
 * @note 进程收尾时调
 */
void cg_log_close(void)
{
    log_lock();
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    log_unlock();
}

/**
 * @brief 取日志文件路径（当前打开的，或最近一次打开过的）
 * @return const char * 内部缓冲，别 free；从未打开、或以 NULL/空串开档（只写 stderr）时返回 NULL
 * @note **关档后仍返回上一个路径是刻意的**（契约见 cg_internal.h）：致命错误对话框与
 *       SEH 崩溃处理器靠它在"关档之后才崩"的时刻仍能指出日志在哪。
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
 * @brief 日志落地：stderr 镜像 + 追加写文件（时间戳 + 等级 + 可选模块标签），并盯运行中轮转
 * @param level 日志等级（CG_LOG_INFO / WARN / ERROR）
 * @param mod 模块标签（NULL 或空串 = 不标）：短 ASCII，如 "serial" / "net" / "gif"
 * @param msg 已格式化好的正文（调用方 vsnprintf 的产物）
 * @return void 无返回值
 * @note 文件部分整条在 s_log_lock 保护下：轮转（log_rotate_runtime）会换 s_log，
 *       而 kit 的读线程也在写日志 —— 锁是"别的线程不拿已关闭的 FILE* 写字"的保证。
 *       stderr 那份不进锁（CRT 自己锁它，且它不参与轮转）。
 *       模块标签的意义：几十万行的现场日志里，"串口相关"一键 grep 可得，
 *       不用逐行读文案猜是哪个子系统写的（2026-10-18 二轮会诊挂账项结账）。
 */
static void log_emit(int level, const char *mod, const char *msg)
{
    char modtag[64] = "";

    if (mod && *mod) snprintf(modtag, sizeof(modtag), " [%s]", mod);
    /* 开发时（有控制台）走这里看得最顺；GUI 版这些字进不了任何地方 */
    fprintf(stderr, "[cstackgui]%s %s\n", modtag, msg);

    log_lock();
    if (s_log) {
        time_t    t = time(NULL);
        struct tm *tmv = localtime(&t);
        char      ts[32] = "";
        int       n;

        if (tmv) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
        n = fprintf(s_log, "%s [%s]%s %s\n", ts[0] ? ts : "-", level_tag(level), modtag, msg);
        if (n > 0) s_bytes += n;
        fflush(s_log);      /* 崩溃/被杀时不至于丢最后几行 */

        /* 运行中轮转（2026-10-18 补，二轮会诊挂账项）：开档时那一轮转管不到
         * "一个进程跑几周"的无人值守场景，单个文件会无界增长。判据用自己累加的
         * 字节数（精确，且不用每次 ftell）；过线就在持锁状态下换档。 */
        if (s_bytes >= LOG_ROTATE_MAX) log_rotate_runtime();
    }
    log_unlock();
}

/**
 * @brief 写一条日志：镜像到 stderr，并追加到日志文件（带时间戳）
 * @param level 日志等级，低于当前过滤等级的条目直接丢弃
 * @param fmt printf 风格格式串
 * @return void 无返回值（没打开日志文件时只写 stderr）
 * @note 不带模块标签；要按子系统 grep 请用 cg_logm
 */
void cg_log(int level, const char *fmt, ...)
{
    char    msg[1024];
    va_list ap;

    if (!fmt || level < s_level) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    log_emit(level, NULL, msg);
}

/**
 * @brief 写一条日志（带模块标签）：镜像到 stderr，并追加到日志文件
 * @param level 日志等级，低于当前过滤等级的条目直接丢弃
 * @param mod 模块标签（短 ASCII 如 "serial"；NULL/空串 = 不标，等同 cg_log）
 * @param fmt printf 风格格式串
 * @return void 无返回值（没打开日志文件时只写 stderr）
 * @note 与 cg_log 同一条落地路径（log_emit），只多一个标签
 */
void cg_logm(int level, const char *mod, const char *fmt, ...)
{
    char    msg[1024];
    va_list ap;

    if (!fmt || level < s_level) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    log_emit(level, mod, msg);
}
