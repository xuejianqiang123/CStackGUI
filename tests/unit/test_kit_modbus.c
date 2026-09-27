/**
 * @file    test_kit_modbus.c
 * @brief   cg_kit Modbus 帧编解码：拼帧 / 解帧 / 异常响应 / CRC（cg_kit_modbus.c）
 * @details 这一族最容易"看起来对"：字节顺序摆错、CRC 高低位传反、异常码忘了剥 0x80 ——
 *          结果都是一串像模像样的十六进制，只有拿去跟设备对才发现不对。所以向量分两类：
 *
 *          1. **对得到出处**的：
 *             - `01 03 00 00 00 0A C5 CD`：仓库里 docs/articles/09-校验与摘要.md 那段
 *               手写示例产出的帧（独立来源 = 一篇教人手写的文档）；
 *             - `01 83 02 C0 F1`：异常响应，异常码 02（非法数据地址）；
 *             - `01 03 00 6B 00 03`：规范里"读保持寄存器"的请求负载。
 *          2. **CRC 由独立实现派生**：上面几条的 CRC 字节用一份与 C 代码无关的 Node 实现
 *             （5 行 CRC16/Modbus）算过。这一步抓到了我自己的一个**错记忆**：
 *             我原以为 0x6B 那条的 CRC 是 `76 87`，独立算出来是 **`74 17`**；
 *             而 `C5 CD` 那条与文档逐字节吻合 ⇒ 以算出来的为准。
 *             （同一个套路项目里用过：哈希的检验值也是 Node/OpenSSL 独立派生的。）
 *
 *          另外钉几条**性质**，它们能抓住"反着写也照样跑"的错误：
 *          含 CRC 的整帧再算一遍必须 = 0；坏一个字节必须被拒（-2）；build ↔ parse 自洽；
 *          TCP 帧带 MBAP 头且**不含** CRC。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>
#include <stdio.h>

/** @brief 把帧转成 "01 03 00 6B" 这种大写十六进制，便于断言（调用方给缓冲，不用静态状态） */
/**
 * @brief 把帧转成 "01 03 00 6B" 这种大写十六进制，便于断言
 * @param f 帧字节
 * @param n 帧长
 * @param buf 输出缓冲
 * @param size 输出缓冲大小
 * @return const char* 转好的十六进制串（就是 buf）
 */
static const char *hexs(const unsigned char *f, int n, char *buf, int size)
{
    cg_hex_encode(f, n, " ", 1, buf, size);
    return buf;
}

/* ------------------------------------------------------------ 拼读请求帧 */

/**
 * @brief 拼读请求帧：与规范一致的字节图案、CRC 自校验、参数边界
 * @return void 无返回值
 */
static void test_modbus_build_read(void)
{
    unsigned char f[64];
    char          s[64];
    int           n;

    cg_test_note("---- 文档里那段手写示例产出的帧（独立来源）----");
    n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x0000, 10, f, sizeof f);
    cg_test_int("RTU 读请求长度 = 8", n, 8);
    cg_test_str("逐字节等于 docs/articles/09 手写的那一帧",
                hexs(f, n, s, sizeof s), "01 03 00 00 00 0A C5 CD");

    cg_test_note("---- 规范里 0x6B 那条（CRC 取独立实现算出的值）----");
    n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x006B, 3, f, sizeof f);
    cg_test_str("CRC = 74 17（不是我记忆里的 76 87）",
                hexs(f, n, s, sizeof s), "01 03 00 6B 00 03 74 17");
    cg_test_int("含 CRC 的整帧再算一遍 = 0（现场自校验习惯）",
                (long long)cg_crc16(f, n), 0);

    cg_test_note("---- 位类（读线圈）----");
    n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 2, 1, 0x0013, 0x0025, f, sizeof f);
    cg_test_int("读线圈 0x0013 起 0x25 个：长度 = 8", n, 8);
    cg_test_int("  整帧 CRC 自检 = 0", (long long)cg_crc16(f, n), 0);
    cg_test_str("  逐字节（CRC 由独立实现算出：我手算的 0C84 是错的）",
                hexs(f, n, s, sizeof s), "02 01 00 13 00 25 0C 27");

    cg_test_note("---- 参数边界 ----");
    cg_test_int("count = 0 拒绝", cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0, 0, f, sizeof f), -1);
    cg_test_int("寄存器类 count = 126 拒绝（规范上限 125）",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0, 126, f, sizeof f), -1);
    cg_test_int("寄存器类 count = 125 接受",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0, 125, f, sizeof f) > 0, 1);
    cg_test_int("位类 count = 2000 接受",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 1, 0, 2000, f, sizeof f) > 0, 1);
    cg_test_int("位类 count = 2001 拒绝",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 1, 0, 2001, f, sizeof f), -1);
    cg_test_int("写类功能码（06）不给 build_read 用",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 6, 0, 1, f, sizeof f), -1);
    cg_test_int("从站地址 = 256 拒绝",
                cg_modbus_build_read(CG_MODBUS_RTU, 0, 256, 3, 0, 1, f, sizeof f), -1);
    cg_test_int("out 放不下返回 -1", cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0, 1, f, 7), -1);
}

/* ------------------------------------------------------------ 拼写请求帧 */

/**
 * @brief 拼写请求帧：单寄存器 / 单线圈 / 多寄存器 / 多线圈（低位在前）及参数边界
 * @return void 无返回值
 */
static void test_modbus_build_write(void)
{
    unsigned char f[64];
    char          s[64];
    unsigned short one[1] = { 3 };
    unsigned short two[2] = { 0x0102, 0x0304 };
    unsigned short coil_on[1] = { 1 };
    unsigned short coil_raw[1] = { 0xFF00 };
    unsigned short bits[10];

    cg_test_note("---- 06 写单个寄存器 ----");
    cg_test_int("长度 = 8", cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 6, 0x0001, one, 1, f, sizeof f), 8);
    cg_test_int("  整帧 CRC 自检 = 0", (long long)cg_crc16(f, 8), 0);
    cg_test_str("  前 6 字节（负载部分）", hexs(f, 6, s, sizeof s), "01 06 00 01 00 03");

    cg_test_note("---- 05 写单个线圈：收 1/0，发 FF00/0000 ----");
    cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 5, 0x00AC, coil_on, 1, f, sizeof f);
    cg_test_str("给 1 -> 线上 FF00", hexs(f, 6, s, sizeof s), "01 05 00 AC FF 00");
    cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 5, 0x00AC, coil_raw, 1, f, sizeof f);
    cg_test_str("给 0xFF00 -> 原样", hexs(f, 6, s, sizeof s), "01 05 00 AC FF 00");

    cg_test_note("---- 10 写多个寄存器（2 个）----");
    {
        int n = cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 16, 0x0001, two, 2, f, sizeof f);

        cg_test_int("长度 = 1+1+2+2+1+4+2 = 13", n, 13);
        cg_test_str("字节数 = 04、值大端", hexs(f, n - 2, s, sizeof s), "01 10 00 01 00 02 04 01 02 03 04");
        cg_test_int("  整帧 CRC 自检 = 0", (long long)cg_crc16(f, n), 0);
    }

    cg_test_note("---- 0F 写多个线圈（10 位，低位在前）----");
    {
        int i, n;

        for (i = 0; i < 10; i++) bits[i] = (unsigned short)(i % 3 == 0);   /* 1,0,0,1,0,0,1,0,0,1 */
        n = cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 15, 0x0013, bits, 10, f, sizeof f);
        cg_test_int("长度 = 1+1+2+2+1+2+2 = 11", n, 11);
        /* 打包：第 0 字节 = bit0,3,6 ⇒ 0x49；第 1 字节 = bit9（也就是它的 bit1）⇒ 0x02。
         * 这一步手算很容易差一位（我第一版就把 0x02 写成了 0x01），所以由独立实现复核过。 */
        cg_test_str("字节数 = 02、打包低位在前", hexs(f, n - 2, s, sizeof s),
                    "01 0F 00 13 00 0A 02 49 02");
        cg_test_int("  整帧 CRC 自检 = 0", (long long)cg_crc16(f, n), 0);
    }

    cg_test_note("---- 参数边界 ----");
    cg_test_int("06 传两个值拒绝", cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 6, 0, two, 2, f, sizeof f), -1);
    cg_test_int("0F 传 0 个拒绝", cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 15, 0, bits, 0, f, sizeof f), -1);
    cg_test_int("读类功能码（03）不给 build_write 用",
                cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 3, 0, one, 1, f, sizeof f), -1);
    cg_test_int("vals = NULL 拒绝", cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 6, 0, NULL, 1, f, sizeof f), -1);
}

/* ------------------------------------------------------------ 解响应帧 */

/**
 * @brief 解响应帧：正常读响应、异常响应剥异常位、坏帧区分 CRC 与格式错
 * @return void 无返回值
 */
static void test_modbus_parse(void)
{
    /* 规范例子的响应（数据 3 个寄存器 = AE41 / 5652 / 4340，CRC 由独立实现派生） */
    static const unsigned char resp[] = { 0x01, 0x03, 0x06, 0xAE, 0x41, 0x56, 0x52, 0x43, 0x40, 0x84, 0x6D };
    /* 异常响应：功能码 03 | 0x80、异常码 02。整帧 CRC 恰好是 C0 F1 */
    static const unsigned char exc[]  = { 0x01, 0x83, 0x02, 0xC0, 0xF1 };
    cg_modbus_rsp_t           r;
    unsigned char             bad[16];
    int                       ret;

    cg_test_note("---- 读响应 ----");
    ret = cg_modbus_parse(CG_MODBUS_RTU, resp, (int)sizeof resp, &r);
    cg_test_int("解析成功", ret, 0);
    cg_test_int("从站 = 1", r.slave, 1);
    cg_test_int("功能码 = 3", r.func, 3);
    cg_test_int("不是异常", r.exception, 0);
    cg_test_int("读到 3 个寄存器", r.nvalues, 3);
    cg_test_int("第 1 个 = 0xAE41", (long long)r.values[0], 0xAE41);
    cg_test_int("第 2 个 = 0x5652", (long long)r.values[1], 0x5652);
    cg_test_int("第 3 个 = 0x4340", (long long)r.values[2], 0x4340);
    cg_test_int("读响应没有地址回显（-1）", r.addr, -1);

    cg_test_note("---- 异常响应：剥掉 0x80，给原功能码 ----");
    ret = cg_modbus_parse(CG_MODBUS_RTU, exc, (int)sizeof exc, &r);
    cg_test_int("解析成功", ret, 0);
    cg_test_int("func 给的是**原**功能码 3（不是 0x83）", r.func, 3);
    cg_test_int("异常码 = 2（非法数据地址）", r.exception, 2);
    cg_test_int("异常响应没有值", r.nvalues, 0);

    cg_test_note("---- 坏帧：CRC 不对必须被拒，且能跟'格式不对'区分开 ----");
    memcpy(bad, resp, sizeof resp);
    bad[3] ^= 0x01;                                     /* 翻一个 bit */
    cg_test_int("坏一个 bit -> -2（CRC 不对）",
                cg_modbus_parse(CG_MODBUS_RTU, bad, (int)sizeof resp, &r), -2);

    memcpy(bad, resp, sizeof resp);
    {
        /* 字节数写大 2 个，**并重算 CRC** —— 不重算的话会先被 CRC 那条拦下（-2），
         * 就测不到"格式不对"这条分支了。这里要证明的是：CRC 没错、但跟规范对不上，也得拒。 */
        unsigned c;

        bad[2] = 0x08;
        c = cg_crc16(bad, (int)sizeof resp - 2);
        bad[sizeof resp - 2] = (unsigned char)(c & 0xFF);
        bad[sizeof resp - 1] = (unsigned char)((c >> 8) & 0xFF);
    }
    cg_test_int("CRC 正确但字节数与帧长不符 -> -3",
                cg_modbus_parse(CG_MODBUS_RTU, bad, (int)sizeof resp, &r), -3);

    cg_test_int("太短 -> -1", cg_modbus_parse(CG_MODBUS_RTU, resp, 4, &r), -1);
    cg_test_int("NULL 帧 -> -1", cg_modbus_parse(CG_MODBUS_RTU, NULL, 8, &r), -1);
    cg_test_int("NULL 出口 -> -1", cg_modbus_parse(CG_MODBUS_RTU, resp, (int)sizeof resp, NULL), -1);
    /* 认不出的功能码（07）：**CRC 得是对的**，否则先撞 -2（这条帧的 CRC 由独立实现算出） */
    {
        static const unsigned char unknown[] = { 0x01, 0x07, 0x00, 0x00, 0x00, 0x18, 0xB4 };

        cg_test_int("认不出的功能码 -> -3", cg_modbus_parse(CG_MODBUS_RTU, unknown, 7, &r), -3);
    }
}

/* ------------------------------------------------------------ TCP 封装 */

/**
 * @brief TCP 封装：请求不带 CRC、响应解析、协议标识与长度字段的坏帧
 * @return void 无返回值
 */
static void test_modbus_tcp(void)
{
    unsigned char f[64];
    char          s[64];
    int           n;
    /* 一条 TCP 读响应：事务号 0x0007、协议 0、长度 0x0009（含 unit）、单元 1；PDU = 03 06 + 6 字节数据 */
    static const unsigned char resp[] = { 0x00, 0x07, 0x00, 0x00, 0x00, 0x09, 0x01,
                                          0x03, 0x06, 0xAE, 0x41, 0x56, 0x52, 0x43, 0x40 };
    cg_modbus_rsp_t           r;

    cg_test_note("---- TCP 请求：MBAP 头 + PDU，**没有** CRC ----");
    n = cg_modbus_build_read(CG_MODBUS_TCP, 7, 1, 3, 0x006B, 3, f, sizeof f);
    cg_test_int("长度 = 7 + 5 = 12", n, 12);
    cg_test_str("逐字节（事务号 0007 / 协议 0 / 长度 0006 / 单元 01）",
                hexs(f, n, s, sizeof s), "00 07 00 00 00 06 01 03 00 6B 00 03");

    cg_test_note("---- TCP 响应解析 ----");
    cg_test_int("解析成功", cg_modbus_parse(CG_MODBUS_TCP, resp, (int)sizeof resp, &r), 0);
    cg_test_int("单元标识当从站地址", r.slave, 1);
    cg_test_int("功能码 = 3", r.func, 3);
    cg_test_int("读到 3 个寄存器", r.nvalues, 3);
    cg_test_int("第 1 个 = 0xAE41", (long long)r.values[0], 0xAE41);

    cg_test_note("---- TCP 的坏帧：协议标识 / 长度字段 ----");
    {
        unsigned char bad[16];

        memcpy(bad, resp, sizeof resp);
        bad[3] = 1;                                     /* 协议标识必须为 0 */
        cg_test_int("协议标识非 0 -> -3", cg_modbus_parse(CG_MODBUS_TCP, bad, (int)sizeof resp, &r), -3);

        memcpy(bad, resp, sizeof resp);
        bad[5] = 0x05;                                  /* 头说 5 字节，实际 9 */
        cg_test_int("长度字段与实际不符 -> -3", cg_modbus_parse(CG_MODBUS_TCP, bad, (int)sizeof resp, &r), -3);
    }
    cg_test_int("TCP 帧太短 -> -1", cg_modbus_parse(CG_MODBUS_TCP, resp, 6, &r), -1);
}

/* ------------------------------------------------------------ 自洽性：拼出来的一定能解回去 */

/**
 * @brief 拼解自洽：各功能码拼出的请求帧 CRC 自检为 0，写响应的回显能解回来
 * @return void 无返回值
 */
static void test_modbus_roundtrip(void)
{
    unsigned char f[64];
    int           n, i;
    unsigned short regs[4] = { 0x1234, 0x5678, 0x9ABC, 0xDEF0 };

    cg_test_note("---- 拼请求：每种功能码都过一遍参数校验与 CRC 自检 ----");
    {
        static const int reads[4]  = { 1, 2, 3, 4 };
        static const int writes[4] = { 5, 6, 15, 16 };

        for (i = 0; i < 4; i++) {
            n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, reads[i], 0x0010, 4, f, sizeof f);
            cg_test_int("读请求 CRC 自检 = 0", (long long)cg_crc16(f, n), 0);
        }
        for (i = 0; i < 4; i++) {
            int cnt = (writes[i] == 5 || writes[i] == 6) ? 1 : 4;

            n = cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, writes[i], 0x0010, regs, cnt, f, sizeof f);
            cg_test_int("写请求 CRC 自检 = 0", (long long)cg_crc16(f, n), 0);
        }
    }

    cg_test_note("---- 写响应的回显（地址 + 值）能解回来 ----");
    {
        unsigned char echo[16];
        cg_modbus_rsp_t r;
        int             len;

        /* 自己拼一条 06 的响应：从站 1、功能 06、地址 0x0001、值 0x0003 —— 就是请求的翻版 */
        len = cg_modbus_build_write(CG_MODBUS_RTU, 0, 1, 6, 0x0001, regs, 1, echo, sizeof echo);
        cg_test_int("06 的响应与请求同形（规范如此）", len, 8);
        cg_test_int("能解回来", cg_modbus_parse(CG_MODBUS_RTU, echo, len, &r), 0);
        cg_test_int("  地址回显 = 0x0001", r.addr, 0x0001);
        cg_test_int("  值回显 = 0x1234", (long long)r.values[0], 0x1234);
        cg_test_int("  功能码 = 6", r.func, 6);
    }
}

CG_TEST_SUITE("Modbus：拼读请求帧", test_modbus_build_read);
CG_TEST_SUITE("Modbus：拼写请求帧", test_modbus_build_write);
CG_TEST_SUITE("Modbus：解响应帧（含异常与坏帧）", test_modbus_parse);
CG_TEST_SUITE("Modbus：TCP 封装（MBAP）", test_modbus_tcp);
CG_TEST_SUITE("Modbus：拼解自洽", test_modbus_roundtrip);
