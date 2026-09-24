/**
 * @file    cg_widget_term.c
 * @brief   终端/日志视图控件 API：按行写入（方向标记/时间戳/HEX）、清空、行数上限、存盘
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>       /* terminal 行的时间戳（和 cg_log.c 用同一套标准 C 时间函数） */

/* ------------------------------------------------------------ 终端 / 日志视图（terminal）
 * 业务只管把收到的字节丢进来（`cg_term_write`），格式（时间戳、HEX、方向标记）与
 * 行数上限都由它管。行的颜色按方向分，后端用 RichEdit 的字符格式着色 ——
 * 不用 ownerdraw，白捡"选择/复制"与自动滚动（日志最常被做的事就是复制出去）。
 *
 * 行的内存表示：`items[i]` 的**首字节**是方向标记（CG_TERM_RX/TX/SYS），后面才是
 * 给人看的文字。用首字节而不是平行数组：裁剪、清空、保存都只面对一个数组，
 * 标记天然跟着行走。这几个字节都 < 0x20，在 UTF-8 里是合法单字节，不影响显示。
 */

/** 默认保留多少行（.cgui 的 value 可以改） */
#define CG_TERM_DEF_LINES 500
/** 单行最长多少字符（一次 write 粘进来一坨巨长数据时兜底） */
#define CG_TERM_MAX_LINE 1024

/** @brief 时间戳 "HH:MM:SS"（和 cg_log 的写法一致：秒级够看间隔） */
static void term_stamp(char *buf, size_t size)
{
    time_t     t = time(NULL);
    struct tm *tmv = localtime(&t);

    if (tmv && strftime(buf, size, "%H:%M:%S", tmv) > 0) return;
    snprintf(buf, size, "--:--:--");
}

/** @brief 方向标记 -> 行首那三个字符的记号 */
static const char *term_dir_tag(int dir)
{
    switch (dir) {
    case CG_TERM_RX:  return "RX ";
    case CG_TERM_TX:  return "TX ";
    default:          return "-- ";
    }
}

/** @brief 当前是不是 HEX 显示模式（.cgui 的 format == "hex"） */
static int term_is_hex(const cg_widget_t *w)
{
    return w->format && strcmp(w->format, "hex") == 0;
}

/**
 * @brief 把一段字节渲染成可读文字
 * @details HEX 模式两个一组空格分开；文本模式原样保留可见字节，
 *          只把控制字符换成 '.' —— 0x80 以上一律保留，这样中文（UTF-8 多字节）
 *          不会被搅成点，而 \r\n\0 这些也不会把界面搞乱。
 */
static void term_format_payload(const cg_widget_t *w, const unsigned char *data, int len,
                                char *out, size_t out_size)
{
    size_t used = 0;
    int    i;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!data || len <= 0) return;

    if (term_is_hex(w)) {
        for (i = 0; i < len; i++) {
            int wrote = snprintf(out + used, out_size - used, i ? " %02X" : "%02X", data[i]);
            if (wrote <= 0 || used + (size_t)wrote >= out_size) break;
            used += (size_t)wrote;
        }
    } else {
        for (i = 0; i < len && used + 1 < out_size; i++) {
            unsigned char c = data[i];
            out[used++] = (c < 0x20 || c == 0x7F) ? '.' : (char)c;
        }
        out[used] = '\0';
    }
    if (i < len && used + 4 < out_size) {          /* 被截断了就给个记号，别让人以为数据就这些 */
        snprintf(out + used, out_size - used, " ...");
    }
}

/** @brief 组装一行（首字节 = 方向标记，其余是给人看的文字）；调用方 free */
static char *term_make_line(cg_widget_t *w, int dir, const unsigned char *data, int len)
{
    char    payload[CG_TERM_MAX_LINE];
    char    stamp[16] = "";
    char    prefix[32] = "";
    size_t  cap;
    char   *line;

    if (dir < CG_TERM_RX || dir > CG_TERM_SYS) dir = CG_TERM_SYS;
    term_format_payload(w, data, len, payload, sizeof(payload));
    if (w->checked) term_stamp(stamp, sizeof(stamp));   /* checked = 显示时间戳 */
    if (stamp[0]) snprintf(prefix, sizeof(prefix), "[%s] ", stamp);

    cap = strlen(prefix) + strlen(payload) + 16;
    line = (char *)malloc(cap);
    if (!line) return NULL;
    line[0] = (char)dir;
    snprintf(line + 1, cap - 1, "%s%s%s", prefix, term_dir_tag(dir), payload);
    return line;
}

/** @brief 追加一行到 items 并按上限丢弃最老的（不 sync，给 cg_term_write 批量用）
 *  @return 本次从头部裁剪掉的行数（0 = 没裁）。裁剪必须同步回退 term_pushed，
 *          否则后端 sync_terminal 看到 `term_pushed >= nitems` 直接 return、新行永不推送
 *          —— 终端到上限后"彻底停刷新"就是这样来的（2026-09-25）。 */
static int term_append_line(cg_widget_t *w, int dir, const unsigned char *data, int len)
{
    char **ni;
    char  *line = term_make_line(w, dir, data, len);
    int    max;
    int    trimmed = 0;

    if (!line) return 0;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) { free(line); return 0; }
    w->items = ni;
    w->items[w->nitems++] = line;

    max = w->value > 0 ? w->value : CG_TERM_DEF_LINES;
    while (w->nitems > max) {
        free(w->items[0]);
        memmove(w->items, w->items + 1, ((size_t)w->nitems - 1) * sizeof(char *));
        w->nitems--;
        if (w->term_pushed > 0) w->term_pushed--;   /* 模型丢了最老一行，已推送指针同步回退 */
        trimmed++;
    }
    return trimmed;
}

/**
 * @brief 往终端里写一段数据（一次调用 = 一行；带 \r\n 的会自动切成多行）
 * @param w terminal 控件
 * @param dir 方向：CG_TERM_RX / CG_TERM_TX / CG_TERM_SYS（决定行首标记与颜色）
 * @param data 数据；len 传 -1 表示按 C 字符串取长度（文本用）
 * @param len 字节数
 * @details 串口/网络一次收进来 "OK\r\nERR\r\n" 是常态，所以这里按 '\n' 切行，
 *          '\r' 丢掉；一次调用只触发一次界面刷新。
 */
void cg_term_write(cg_widget_t *w, int dir, const void *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    int start = 0;
    int i;
    int trimmed = 0;

    if (!w || !want_type(w, "terminal", "cg_term_write")) return;
    if (len < 0) len = data ? (int)strlen((const char *)data) : 0;
    if (len <= 0) {
        if (term_append_line(w, dir, p, 0)) w->items_dirty = 1;   /* 裁过 → 后端整体重灌 */
        cg_widget_sync(w);
        return;
    }

    for (i = 0; i <= len; i++) {
        if (i < len && p[i] != '\n') continue;
        {
            int end = i;
            while (end > start && p[end - 1] == '\r') end--;
            /* 结尾多出来的空段（数据以 \n 收尾）不再补一行空行 */
            if (end > start || i < len) trimmed |= term_append_line(w, dir, p + start, end - start);
        }
        start = i + 1;
    }
    /* 没裁剪 → 后端走增量追加（见 sync_terminal）；裁过 → 标脏让后端整体重灌，
     * 否则 RichEdit 里最老那行不会被删掉、界面会比上限多显示一行（且持续堆积）。 */
    if (!trimmed) w->items_dirty = 0;
    cg_widget_sync(w);
}

/** @brief 清空终端 */
void cg_term_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "terminal", "cg_term_clear")) return;
    for (int i = 0; i < w->nitems; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->nitems = 0;
    w->term_pushed = 0;
    w->items_dirty = 1;      /* 让后端整体重灌（清屏） */
    cg_widget_sync(w);
}

/** @brief 切 HEX / 文本显示（只影响**之后**写入的行，已经显示的不动） */
void cg_term_set_hex(cg_widget_t *w, int on)
{
    if (!w || !want_type(w, "terminal", "cg_term_set_hex")) return;
    free(w->format);
    w->format = on ? cg_str_dup("hex") : NULL;
    if (w->node) {
        free(w->node->format);
        w->node->format = on ? cg_str_dup("hex") : NULL;
    }
}

/** @brief 当前是不是 HEX 模式 */
int cg_term_hex(const cg_widget_t *w)
{
    return w && term_is_hex(w);
}

/** @brief 每行前面要不要带 [HH:MM:SS]（存的是控件的 checked 字段） */
void cg_term_set_timestamp(cg_widget_t *w, int on)
{
    cg_widget_set_checked(w, on ? 1 : 0);
}

/** @brief 最多保留多少行（超了从最老的丢；0 或负数 = 恢复默认 500） */
void cg_term_set_max_lines(cg_widget_t *w, int lines)
{
    if (!w || !want_type(w, "terminal", "cg_term_set_max_lines")) return;
    w->value = lines > 0 ? lines : 0;
    if (w->node) w->node->value = w->value;
}

/** @brief 当前有多少行 */
int cg_term_count(const cg_widget_t *w)
{
    return w ? w->nitems : 0;
}

/**
 * @brief 把终端里的内容存成文本文件（UTF-8，行尾 CRLF；不带方向标记与时间戳外的装饰）
 * @return int 0 成功；-1 参数不对；其它 = fopen 失败
 */
int cg_term_save(cg_widget_t *w, const char *path)
{
    FILE *f;

    if (!w || !path || !*path) return -1;
    if (!want_type(w, "terminal", "cg_term_save")) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;

    for (int i = 0; i < w->nitems; i++) {
        const char *line = w->items[i] ? w->items[i] : "";
        unsigned char tag = (unsigned char)line[0];
        if (tag >= CG_TERM_RX && tag <= CG_TERM_SYS) line++;   /* 去掉方向标记 */
        fputs(line, f);
        fputs("\r\n", f);
    }
    fclose(f);
    return 0;
}
