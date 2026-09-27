/**
 * @file    cg_kit_frame.c
 * @brief   字节流切帧：把"半帧 / 一整帧 / 两帧粘一起"的字节流切成整帧
 * @details **为什么进框架**：串口与 TCP 给的都是**字节流**，没有消息边界；"攒着 + 切出来"
 *          这段每个上位机都要重写一遍。`examples/modbus/main.c` 里就手写了一段（按 MBAP 长度
 *          字段切），而它的文件头还写着"串口上的 Modbus/RTU 没有长度字段，得靠**帧间静默**或
 *          CRC 反向定位 —— **那是另一个话题**"。本文件把那"另一个话题"变成一次 init + 一个循环。
 *
 *          四种定界办法（`cg_frame_init` 的 mode），现场协议基本跑不出这四种：
 *            - `CG_FRAME_SILENCE`  **帧间静默**：隔了 gap_ms 没有新字节就算一帧结束。
 *                                  Modbus RTU 的 t3.5 就是它（9600 下约 3.65ms ⇒ 缺省给 4）。
 *            - `CG_FRAME_FIXED`    定长：每 N 字节一帧。
 *            - `CG_FRAME_DELIM`    分隔符：读到指定字节算一帧结束，**分隔符留在帧里**。
 *            - `CG_FRAME_LENFIELD` 头里带长度：长度字段在第 off 字节（宽 1/2、可小端），
 *                                  帧总长 = 长度值 + (off + 宽度)。MBAP 正好是这个形状
 *                                  （长度字段在偏移 4、宽 2、大端，帧总长 = 长度 + 6）。
 *
 *          **纯状态机**：不持线程、不碰 I/O、**时间由调用方给**（`now_ms` 用 cg_now_ms() 取）。
 *          这是刻意的：内部读时钟就没法用"任意时间戳 + 任意切法的字节序列"去严格测，
 *          只能靠 sleep 碰运气。现在单测里可以断言"空档 3ms 不切、4ms 就切"这种边界。
 *
 *          **静默切帧有一条物理事实**（不是本实现的毛病，写在这里免得被当成 bug）：两帧之间
 *          一旦短于 gap，就**无法**再分开 —— RTU 规范要求帧间静默 ≥ t3.5，从站违反它时任何
 *          切帧器都无能为力。所以静默模式必须配 CRC 用（`cg_crc16` / `cg_modbus_parse` 会告诉你
 *          这一帧坏没坏），坏了就丢帧重来。
 *
 *          **噪声撑不爆内存**：缓冲是调用方给的，越界写的可能被掐死在"放不下就丢"这一句上；
 *          被丢的字节数一直累加（`cg_frame_dropped()`），界面想显示"丢了多少"就读它 ——
 *          **丢要丢得看得见**，不许静默丢。
 */
#include "cstackgui.h"

#include <string.h>

/* ------------------------------------------------------------ 内部：位置与入队 */

/** @brief 当前正在攒的那一帧在缓冲里的起点
 *  @param f 切帧状态
 *  @return int 偏移（字节）
 *  @details 整帧在缓冲里是**连续**排着的，所以位置不必逐个记：队首偏移加上队列里各帧长度
 *           之和就是它。取走帧会让队首偏移往前挪，队列取空后归 0（见 cg_frame_take）。
 */
static int cur_start(const cg_frame_t *f)
{
    int off = f->q_base;
    int i;

    for (i = 0; i < f->q_count; i++) {
        off += f->q_len[(f->q_head + i) % CG_FRAME_QUEUE];
    }
    return off;
}

/** @brief 把当前攒着的 len 字节当作一整帧入队
 *  @param f 切帧状态
 *  @param len 帧长（字节）
 *  @return void
 *  @details 太短（低于 min_len）当噪声丢掉；队列满也丢掉 —— 两种情况都记进 dropped，
 *           不静默丢。入队后当前帧长度归零，下一帧从它后面接着攒。
 */
static void complete(cg_frame_t *f, int len)
{
    if (len < f->min_len || f->q_count >= CG_FRAME_QUEUE) {
        f->dropped += (unsigned long)len;
        f->len      = 0;
        return;
    }
    f->q_len[(f->q_head + f->q_count) % CG_FRAME_QUEUE] = len;
    f->q_count++;
    f->frames++;
    f->len = 0;
}

/** @brief LENFIELD：长度字段收够了就给出该帧总长
 *  @param f 切帧状态
 *  @return int > 0 = 该帧总长；0 = 长度字段还没收够；-1 = 这个长度值不靠谱（整段丢掉重来）
 */
static int need_len(const cg_frame_t *f)
{
    const unsigned char *p;
    unsigned             v;
    int                  total;

    if (f->len < f->lf_off + f->lf_width) return 0;

    p = f->buf + cur_start(f) + f->lf_off;
    if (f->lf_width == 2) {
        v = f->lf_le ? ((unsigned)p[0] | ((unsigned)p[1] << 8))
                     : (((unsigned)p[0] << 8) | (unsigned)p[1]);
    } else {
        v = p[0];
    }
    total = (int)v + f->lf_add;
    /* 比"头 + 长度字段"还短、或超过一帧上限：这不是长度字段，是噪声 */
    if (total <= f->lf_off + f->lf_width || total > f->max_len) return -1;
    return total;
}

/* ------------------------------------------------------------ 公开 API */

int cg_frame_init(cg_frame_t *f, void *buf, int buf_size, int mode, int arg)
{
    if (!f || !buf || buf_size < 8) return -1;
    if (mode != CG_FRAME_SILENCE && mode != CG_FRAME_FIXED &&
        mode != CG_FRAME_DELIM && mode != CG_FRAME_LENFIELD) {
        return -1;
    }

    memset(f, 0, sizeof *f);
    f->buf     = (unsigned char *)buf;
    f->cap     = buf_size;
    f->mode    = mode;
    f->min_len = 1;
    f->max_len = buf_size;
    f->gap_ms  = 4;             /* Modbus RTU @9600 的 t3.5 ≈ 3.65ms；只是缺省，按现场改 */
    f->delim   = '\n';
    f->fixed   = 8;

    switch (mode) {
    case CG_FRAME_SILENCE:
        if (arg < 1) return -1;                 /* 0 = 每个字节都算一帧，没有意义 */
        f->gap_ms = (unsigned long)arg;
        break;
    case CG_FRAME_FIXED:
        if (arg < 1 || arg > buf_size) return -1;
        f->fixed = arg;
        break;
    case CG_FRAME_DELIM:
        if (arg < 0 || arg > 255) return -1;
        f->delim = arg;
        break;
    default:                                    /* CG_FRAME_LENFIELD：arg = 长度字段的偏移 */
        if (arg < 0 || arg + 2 > buf_size - 1) return -1;
        f->lf_off   = arg;
        f->lf_width = 2;
        f->lf_le    = 0;
        f->lf_add   = arg + 2;                  /* 缺省按 MBAP 的形状：总长 = 长度 + (偏移 + 宽) */
        break;
    }
    return 0;
}

void cg_frame_lenfield(cg_frame_t *f, int width, int little_endian, int add)
{
    if (!f || (width != 1 && width != 2)) return;
    f->lf_width = width;
    f->lf_le    = little_endian ? 1 : 0;
    if (add > 0) f->lf_add = add;
}

void cg_frame_limits(cg_frame_t *f, int min_len, int max_len)
{
    if (!f) return;
    if (min_len > 0) f->min_len = min_len;
    if (max_len > 0 && max_len <= f->cap) f->max_len = max_len;
}

int cg_frame_feed(cg_frame_t *f, const void *data, int len, unsigned long now_ms)
{
    const unsigned char *p = (const unsigned char *)data;
    int                  i;

    if (!f || !f->buf || !p || len < 0) return -1;

    for (i = 0; i < len; i++) {
        /* 静默到点：这个字节与上一个之间空档够久 ⇒ 前面攒的那一帧已经完整了 */
        if (f->mode == CG_FRAME_SILENCE && f->len > 0 &&
            (now_ms - f->last_ms) >= f->gap_ms) {
            complete(f, f->len);
        }
        if (cur_start(f) + f->len >= f->cap) {
            /* 放不下：把攒着的这一段当噪声丢掉（计数），绝不多写一个字节出去 */
            f->dropped += (unsigned long)f->len;
            f->len      = 0;
            if (cur_start(f) >= f->cap) {
                /* 缓冲被"没取走的整帧"占满，一点缝都没有：连这个字节也丢（已计数），
                 * 调用方该先把队列里的帧取走 */
                f->dropped += 1u;
                continue;
            }
        }
        f->buf[cur_start(f) + f->len] = p[i];
        f->len++;
        f->last_ms = now_ms;

        if (f->mode == CG_FRAME_FIXED) {
            if (f->len >= f->fixed) complete(f, f->len);
        } else if (f->mode == CG_FRAME_DELIM) {
            if (p[i] == (unsigned char)f->delim) complete(f, f->len);
        } else if (f->mode == CG_FRAME_LENFIELD) {
            int total = need_len(f);

            if (total < 0) {                     /* 长度字段不靠谱：整段丢掉重来（丢多少看得见） */
                f->dropped += (unsigned long)f->len;
                f->len      = 0;
            } else if (total > 0 && f->len >= total) {
                complete(f, total);
            }
        } else if (f->len >= f->max_len) {       /* 静默模式：攒到上限还没有空档 ⇒ 当噪声丢 */
            f->dropped += (unsigned long)f->len;
            f->len      = 0;
        }
    }
    return 0;
}

int cg_frame_take(cg_frame_t *f, void *out, int cap)
{
    int n;

    if (!f || !f->buf || !out) return -1;
    if (f->q_count <= 0) return 0;

    n = f->q_len[f->q_head];
    if (cap < n) return -1;                      /* 放不下：留在队列里，换个够大的缓冲再来取 */

    memcpy(out, f->buf + f->q_base, (size_t)n);
    f->q_base += n;
    f->q_head  = (f->q_head + 1) % CG_FRAME_QUEUE;
    f->q_count--;
    if (f->q_count == 0) {
        /* 队列空了：把没攒完的半帧挪到缓冲头上，缓冲才不会一直往后爬 */
        if (f->len > 0) memmove(f->buf, f->buf + f->q_base, (size_t)f->len);
        f->q_base = 0;
    }
    return n;
}

void cg_frame_tick(cg_frame_t *f, unsigned long now_ms)
{
    if (!f || !f->buf) return;
    if (f->mode != CG_FRAME_SILENCE || f->len <= 0) return;
    if ((now_ms - f->last_ms) >= f->gap_ms) complete(f, f->len);
}

void cg_frame_reset(cg_frame_t *f)
{
    if (!f) return;
    f->q_base  = 0;
    f->q_head  = 0;
    f->q_count = 0;
    f->len     = 0;
}

unsigned long cg_frame_dropped(const cg_frame_t *f)
{
    return f ? f->dropped : 0;
}

unsigned long cg_frame_frames(const cg_frame_t *f)
{
    return f ? f->frames : 0;
}
