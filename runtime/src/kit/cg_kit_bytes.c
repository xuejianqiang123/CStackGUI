/**
 * @file    cg_kit_bytes.c
 * @brief   kit 字节操作：反转 / 切片 / 查找 / 按字节序读写 16·32 位整数
 * @details 来历见 cstackgui.h 里那一段的注释：精易 `字节集_` 那一族（27 条）在 C 侧**不需要**
 *          一个新的"字节缓冲类型" —— 房屋风格是 `(data, len, …, out, out_size)` 进、
 *          返回写出的长度，加一个会分配内存的类型等于多一套所有权模型；而那一族里最常用的
 *          两个（字节 ↔ 十六进制）早就在 cg_kit_str.c 里了。所以这里只补缺的基元。
 *
 *          为什么需要 get/put u16·u32：**工控报文里就是大端小端的 16/32 位整数**
 *          （Modbus 的寄存器是大端、很多私协议是小端）。手写 `(d[0]<<8)|d[1]` 到处撒
 *          最容易错的就是字节序与越界，收成两个函数、把字节序做成参数，比让每个人各写一遍强。
 *
 *          边界语义（全族统一）：**越界/参数非法一律"什么都不写并返回失败码"** ——
 *          绝不会写半个字段（半个 u16 写进帧里比报错更难查）。
 */
#include "cg_kit_internal.h"

#include <string.h>

/**
 * @brief 原地反转一段字节
 * @param data 待反转的缓冲区（就在它上面改）；NULL 时什么都不做
 * @param len 字节数；<= 1 时什么都不做
 * @return void 无返回值
 */
void cg_bytes_reverse(void *data, int len)
{
    unsigned char *p = (unsigned char *)data;
    int            i, j;

    if (!p || len <= 1) return;
    for (i = 0, j = len - 1; i < j; i++, j--) {
        unsigned char t = p[i];

        p[i] = p[j];
        p[j] = t;
    }
}

/**
 * @brief 取一段字节切片
 * @param data 源缓冲；NULL 直接失败
 * @param len 源缓冲字节数；负值直接失败
 * @param from 起点下标（0 起），必须小于 len
 * @param count 要取的字节数；负值或超过剩余量就取到末尾
 * @param out 出口缓冲（只写 count 个字节，多余部分不动）；NULL 直接失败
 * @param out_size out 的字节容量；<= 0 直接失败
 * @return int 写出的字节数；-1 参数非法或 from 越界，-2 out_size 装不下（此时什么都不写）
 */
int cg_bytes_slice(const void *data, int len, int from, int count, void *out, int out_size)
{
    int avail;

    if (!data || !out || len < 0 || out_size <= 0) return -1;
    if (from < 0 || from >= len) return -1;      /* 起点越界：明确失败，别给个空结果糊过去 */
    avail = len - from;
    if (count < 0 || count > avail) count = avail;   /* "取右边"就是 count 比剩余大的那种 */
    if (count > out_size) return CG_ERR_NO_BUF;
    if (count > 0) memcpy(out, (const unsigned char *)data + from, (size_t)count);
    return count;
}

/**
 * @brief 在缓冲里找子串
 * @param data 源缓冲；NULL 直接失败
 * @param len 源缓冲字节数；<= 0 直接失败
 * @param needle 要找的字节串；NULL 或长度 <= 0 直接失败
 * @param nlen needle 的字节数
 * @param from 起始下标（0 起）；负值当 0
 * @return int 首个匹配处的下标；没找到 / 参数非法 / needle 比 data 还长返回 -1
 */
int cg_bytes_find(const void *data, int len, const void *needle, int nlen, int from)
{
    const unsigned char *d = (const unsigned char *)data;
    const unsigned char *n = (const unsigned char *)needle;
    int                  i;

    if (!d || !n || len <= 0 || nlen <= 0) return -1;
    if (from < 0) from = 0;
    if (nlen > len) return -1;
    for (i = from; i + nlen <= len; i++) {
        if (memcmp(d + i, n, (size_t)nlen) == 0) return i;
    }
    return -1;
}

/**
 * @brief 从 offset 读一个 16 位无符号整数
 * @param data 源缓冲；NULL 直接失败
 * @param len 源缓冲字节数
 * @param offset 起始下标（0 起），越界算失败
 * @param big_endian 非 0 按大端读（Modbus 那种）；0 按小端读
 * @return int 0 ~ 65535；越界 / 参数非法返回 -1
 */
int cg_bytes_get_u16(const void *data, int len, int offset, int big_endian)
{
    const unsigned char *d = (const unsigned char *)data;

    if (!d || offset < 0 || offset + 2 > len) return -1;
    return big_endian ? ((d[offset] << 8) | d[offset + 1])
                      : ((d[offset + 1] << 8) | d[offset]);
}

/**
 * @brief 从 offset 读一个 32 位无符号整数
 * @param data 源缓冲；NULL 直接失败
 * @param len 源缓冲字节数
 * @param offset 起始下标（0 起），越界算失败
 * @param big_endian 非 0 按大端读；0 按小端读
 * @return long long 0 ~ 4294967295；越界 / 参数非法返回 -1
 * @note 返回 long long 是为了让 0xFFFFFFFF 这个合法值不与 -1 撞车
 */
long long cg_bytes_get_u32(const void *data, int len, int offset, int big_endian)
{
    const unsigned char *d = (const unsigned char *)data;
    unsigned             v;

    if (!d || offset < 0 || offset + 4 > len) return -1;
    if (big_endian) {
        v = ((unsigned)d[offset] << 24) | ((unsigned)d[offset + 1] << 16) |
            ((unsigned)d[offset + 2] << 8) | (unsigned)d[offset + 3];
    } else {
        v = ((unsigned)d[offset + 3] << 24) | ((unsigned)d[offset + 2] << 16) |
            ((unsigned)d[offset + 1] << 8) | (unsigned)d[offset];
    }
    return (long long)v;
}

/**
 * @brief 把 16 位写进 out 的 offset 处
 * @param out 目标缓冲；NULL 直接失败
 * @param out_size out 的字节容量
 * @param offset 起始下标（0 起），越界算失败
 * @param v 要写的值（只用低 16 位）
 * @param big_endian 非 0 按大端写；0 按小端写
 * @return int 1 成功；0 越界或参数非法（此时一个字节都不写）
 */
int cg_bytes_put_u16(void *out, int out_size, int offset, unsigned v, int big_endian)
{
    unsigned char *d = (unsigned char *)out;

    if (!d || offset < 0 || offset + 2 > out_size) return 0;   /* 先判完再写，不写半个字段 */
    if (big_endian) {
        d[offset]     = (unsigned char)((v >> 8) & 0xFFu);
        d[offset + 1] = (unsigned char)(v & 0xFFu);
    } else {
        d[offset]     = (unsigned char)(v & 0xFFu);
        d[offset + 1] = (unsigned char)((v >> 8) & 0xFFu);
    }
    return 1;
}

/**
 * @brief 把 32 位写进 out 的 offset 处
 * @param out 目标缓冲；NULL 直接失败
 * @param out_size out 的字节容量
 * @param offset 起始下标（0 起），越界算失败
 * @param v 要写的值
 * @param big_endian 非 0 按大端写；0 按小端写
 * @return int 1 成功；0 越界或参数非法（此时一个字节都不写）
 */
int cg_bytes_put_u32(void *out, int out_size, int offset, unsigned v, int big_endian)
{
    unsigned char *d = (unsigned char *)out;

    if (!d || offset < 0 || offset + 4 > out_size) return 0;
    if (big_endian) {
        d[offset]     = (unsigned char)((v >> 24) & 0xFFu);
        d[offset + 1] = (unsigned char)((v >> 16) & 0xFFu);
        d[offset + 2] = (unsigned char)((v >> 8) & 0xFFu);
        d[offset + 3] = (unsigned char)(v & 0xFFu);
    } else {
        d[offset]     = (unsigned char)(v & 0xFFu);
        d[offset + 1] = (unsigned char)((v >> 8) & 0xFFu);
        d[offset + 2] = (unsigned char)((v >> 16) & 0xFFu);
        d[offset + 3] = (unsigned char)((v >> 24) & 0xFFu);
    }
    return 1;
}
