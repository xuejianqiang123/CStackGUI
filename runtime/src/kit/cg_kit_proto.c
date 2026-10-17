/**
 * @file    cg_kit_proto.c
 * @brief   通用报文组装与校验：帧头 / 地址 / 功能码 / 数据 / 校验 / 帧尾
 * @details **为什么进框架**：工控、仪表、模块的私有协议基本都长成这个骨架，区别只在
 *          "哪几段有、校验怎么算"。XCOM 的「协议传输」页签干的就是这件事，但它把**正点原子的
 *          私有 IAP 格式**（地址/功能码/数据长度/帧序列/校验值）写死在界面上 —— 换个厂家的设备
 *          就不适用。这里给**通用骨架**：字段含义由你的协议定，校验方式从五种里选。
 *
 *          与同族两个文件的分工：
 *            - `cg_modbus_*`  —— **一种**具体协议（规范定死了字段含义与 CRC 写法）；
 *            - `cg_frame_*`   —— 把**收到的**字节流切成帧（静默 / 换行符 / 定长 / 长度字段）；
 *            - 本文件         —— 按骨架**拼出去的**帧、并按同一骨架**验收到的**帧。
 *          三者都是纯函数：不持状态、不碰 I/O。
 *
 *          三条替你钉住的约定（手写时最容易错）：
 *          1. **校验覆盖"帧头 + 地址 + 功能码 + [长度] + 数据"** —— 也就是本帧里校验值之前的
 *             全部字节，这是绝大多数私有协议的算法（要算别的范围，自己拼两段也行）；
 *          2. **多字节校验的字节序**：Modbus 那套是**低字节在前**（`sum_big_endian = 0`），
 *             也有设备要高字节在前，所以做成开关而不是替你猜；
 *          3. **地址/功能码传 -1 = 这一段不写**；帧头/帧尾/数据传长度 0 = 这一段没有。
 *
 *          正确性靠**两个独立来源**对齐（见 tests/unit/test_kit_proto.c）：
 *          用本文件拼出来的 Modbus/RTU 帧，必须与 `cg_modbus_build_read()` 的输出**逐字节相同**；
 *          单独校验（累加和/异或）的期望值另用独立脚本算过，不拿"看起来对"当断言。
 */
#include "cstackgui.h"

#include <string.h>

/** @brief 算一段字节的校验值（不含 CRC16 的初值处理：cg_crc16 内部自带）
 *  @param b 字节
 *  @param n 长度
 *  @param kind CG_SUM_*
 *  @return unsigned 校验值（多字节时低 16 位有效）
 */
static unsigned sum_of(const unsigned char *b, int n, int kind)
{
    unsigned s = 0;
    int      i;

    switch (kind) {
    case CG_SUM_ADD8:
        for (i = 0; i < n; i++) s += b[i];
        return s & 0xFFu;
    case CG_SUM_XOR8:
        for (i = 0; i < n; i++) s ^= b[i];
        return s & 0xFFu;
    case CG_SUM_ADD16:
        for (i = 0; i < n; i++) s += b[i];
        return s & 0xFFFFu;
    case CG_SUM_CRC16_MODBUS:
        return cg_crc16(b, n) & 0xFFFFu;
    default:
        return 0;
    }
}

/**
 * @brief 校验值占几个字节（0 = 不校验）
 * @param kind 校验方式，取 CG_SUM_*
 * @return int 校验值字节数；CG_SUM_NONE 返回 0
 */
static int sum_bytes_of(int kind)
{
    if (kind == CG_SUM_NONE) return 0;
    if (kind == CG_SUM_ADD16 || kind == CG_SUM_CRC16_MODBUS) return 2;
    return 1;
}

/**
 * @brief 按报文骨架拼一帧（帧头 / 地址 / 功能码 / [长度] / 数据 / 校验 / 帧尾）
 * @param p 骨架，**先 memset 成 0** 再填要用的字段
 * @param out 输出缓冲，帧写在这里
 * @param out_size out 的字节数
 * @return int 帧长（写进 out）；-1 参数非法或 out 放不下
 */
int cg_proto_build(const cg_proto_t *p, void *out, int out_size)
{
    unsigned char *o = (unsigned char *)out;
    int            n = 0, cover, sb;
    unsigned       s;

    if (!p || !o || out_size <= 0) return -1;
    if (p->head_len < 0 || p->data_len < 0 || p->tail_len < 0) return -1;
    if (p->head_len > 0 && !p->head) return -1;
    if (p->data_len > 0 && !p->data) return -1;
    if (p->tail_len > 0 && !p->tail) return -1;
    if (p->addr < -1 || p->addr > 255 || p->func < -1 || p->func > 255) return -1;
    if (p->data_len > 255 && p->with_len) return -1;       /* 长度字段只有一个字节 */

    sb = sum_bytes_of(p->sum);

    /* 放不下就早点拒绝：算长度不靠猜，逐段累加 */
    {
        int need = p->head_len + (p->addr >= 0) + (p->func >= 0) + (p->with_len ? 1 : 0) +
                   p->data_len + sb + p->tail_len;

        if (need > out_size) return -1;
    }

    if (p->head_len > 0) {
        memcpy(o + n, p->head, (size_t)p->head_len);
        n += p->head_len;
    }
    if (p->addr >= 0) o[n++] = (unsigned char)p->addr;
    if (p->func >= 0) o[n++] = (unsigned char)p->func;
    if (p->with_len)  o[n++] = (unsigned char)p->data_len;
    if (p->data_len > 0) {
        memcpy(o + n, p->data, (size_t)p->data_len);
        n += p->data_len;
    }

    cover = n;                                  /* 校验覆盖到这里为止（校验值之前的全部）*/
    if (sb > 0) {
        s = sum_of(o, cover, p->sum);
        if (sb == 1) {
            o[n++] = (unsigned char)(s & 0xFFu);
        } else if (p->sum_big_endian) {
            o[n++] = (unsigned char)((s >> 8) & 0xFFu);
            o[n++] = (unsigned char)(s & 0xFFu);
        } else {
            o[n++] = (unsigned char)(s & 0xFFu);
            o[n++] = (unsigned char)((s >> 8) & 0xFFu);
        }
    }
    if (p->tail_len > 0) {
        memcpy(o + n, p->tail, (size_t)p->tail_len);
        n += p->tail_len;
    }
    return n;
}

/**
 * @brief 按同一骨架验一帧，并把字段拆出来
 * @param p 骨架，必须与拼帧时一致
 * @param frame 收到的整帧
 * @param len 帧长度
 * @param out_addr 地址出口，可传 NULL（不想看就不给）
 * @param out_func 功能码出口，可传 NULL
 * @param out_data 数据区指针出口（指向 frame 内部，不用释放），可传 NULL
 * @param out_data_len 数据区长度出口，可传 NULL
 * @return int 0 通过；-1 帧太短或参数非法；-2 校验不对（线上数据坏了）；
 *             -3 对不上骨架（帧头 / 地址 / 功能码 / 帧尾 / 长度字节 不一致）
 * @note 数据长度：`with_len` 时取长度字节（并与实际长度核对），否则由帧长推算
 *       （所以解析时**不看** `cg_proto_t.data_len`）
 */
int cg_proto_parse(const cg_proto_t *p, const void *frame, int len,
                   int *out_addr, int *out_func, const unsigned char **out_data, int *out_data_len)
{
    const unsigned char *f = (const unsigned char *)frame;
    int                  pos = 0, sb, dlen, need;
    unsigned             s, want;

    if (out_addr) *out_addr = -1;
    if (out_func) *out_func = -1;
    if (out_data) *out_data = NULL;
    if (out_data_len) *out_data_len = 0;

    if (!p || !f || len <= 0) return -1;
    if (p->head_len < 0 || p->tail_len < 0) return -1;
    if (p->head_len > 0 && !p->head) return -1;
    if (p->tail_len > 0 && !p->tail) return -1;

    sb = sum_bytes_of(p->sum);

    if (p->head_len > 0) {
        if (len < p->head_len || memcmp(f, p->head, (size_t)p->head_len) != 0) return -3;
        pos = p->head_len;
    }
    if (p->addr >= 0) {
        if (pos + 1 > len) return -1;
        if (f[pos] != (unsigned char)p->addr) return -3;    /* 不是发给这一台的 */
        if (out_addr) *out_addr = f[pos];
        pos++;
    }
    if (p->func >= 0) {
        if (pos + 1 > len) return -1;
        if (f[pos] != (unsigned char)p->func) return -3;
        if (out_func) *out_func = f[pos];
        pos++;
    }
    if (p->with_len) {
        if (pos + 1 > len) return -1;
        dlen = f[pos];
        pos++;
    } else {
        dlen = len - pos - sb - p->tail_len;                /* 数据长度由帧长推算 */
        if (dlen < 0) return -1;
    }
    if (dlen > 0 && pos + dlen > len) return -3;            /* 长度字段说不够，实际也没有 */

    /* 校验：覆盖范围与 build 完全一致（校验值之前的全部字节）*/
    if (sb > 0) {
        if (pos + dlen + sb > len) return -1;
        s    = sum_of(f, pos + dlen, p->sum);
        want = (sb == 1) ? f[pos + dlen]
                         : (p->sum_big_endian ? (((unsigned)f[pos + dlen] << 8) | f[pos + dlen + 1])
                                              : (((unsigned)f[pos + dlen + 1] << 8) | f[pos + dlen]));
        if (s != want) return CG_ERR_CHECKSUM;
    }

    if (p->tail_len > 0) {
        need = pos + dlen + sb;
        if (need + p->tail_len > len) return -1;
        if (memcmp(f + need, p->tail, (size_t)p->tail_len) != 0) return -3;
    }

    if (out_data) *out_data = (dlen > 0) ? f + pos : NULL;
    if (out_data_len) *out_data_len = dlen;
    return 0;
}
