/**
 * @file    test_kit_proto.c
 * @brief   通用报文组装（cg_proto_*）单测：跨实现对齐 + 五种校验 + 异常路径 + 不越界
 * @details 断言来源分两类，都不靠"看起来对"：
 *            - **跨实现对齐**：用 cg_proto_build 拼 Modbus/RTU 帧，必须与 `cg_modbus_build_read()`
 *              的输出**逐字节相同**（同族两个文件是两套独立实现）；
 *            - **独立脚本算过的常数**：累加和 0x71、异或 0x95、16 位累加和 0x0171
 *              （另用 node 算过一遍，不是手算印象）。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** 五种校验各自拼一帧，逐字节对上独立算过的期望值 */
/**
 * @brief 五种校验各自拼一帧，逐字节对上独立算过的期望值
 * @return void 无返回值
 */
static void test_proto_sum(void)
{
    static const unsigned char k_d2[2] = { 0x00, 0x6B };
    static const unsigned char k_d4[4] = { 0x00, 0x6B, 0x00, 0x03 };
    unsigned char              got[32], mod[32];
    cg_proto_t                 p;
    int                        n, i;

    cg_test_note("---- 跨实现：拼出来的 Modbus/RTU 帧必须与 cg_modbus_build_read 逐字节相同 ----");
    memset(&p, 0, sizeof p);
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d4;
    p.data_len = 4;
    p.sum      = CG_SUM_CRC16_MODBUS;      /* 低字节在前 = Modbus 的写法 */
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("帧长 8", n, 8);
    cg_test_int("与 cg_modbus_build_read 的帧长一致",
                n, cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x006B, 3, mod, (int)sizeof(mod)));
    cg_test_int("逐字节相同（两套独立实现对上了）", memcmp(got, mod, (size_t)n), 0);

    cg_test_note("---- 累加和（ADD8）：AA 55 01 03 00 6B 的累加和 = 0x6E，独立脚本算过 ----");
    memset(&p, 0, sizeof p);
    p.head     = "\xAA\x55";
    p.head_len = 2;
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d2;
    p.data_len = 2;
    p.sum      = CG_SUM_ADD8;
    p.tail     = "\r\n";
    p.tail_len = 2;
    n = cg_proto_build(&p, got, (int)sizeof(got));
    {
        static const unsigned char k_want[9] = { 0xAA, 0x55, 0x01, 0x03, 0x00, 0x6B,
                                                 0x6E, 0x0D, 0x0A };

        cg_test_int("帧长 9（头 2 + 地址 + 功能 + 数据 2 + 校验 1 + 尾 2）", n, 9);
        for (i = 0; i < 9; i++) {          /* 逐字节断言：错了能直接看出是第几个字节 */
            char what[48];

            snprintf(what, sizeof what, "第 %d 个字节 = %02X", i, k_want[i]);
            cg_test_int(what, got[i], k_want[i]);
        }
    }

    cg_test_note("---- 异或（XOR8）：0x95 ----");
    memset(&p, 0, sizeof p);
    p.head     = "\xAA\x55";
    p.head_len = 2;
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d4;
    p.data_len = 4;
    p.sum      = CG_SUM_XOR8;
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("帧长 9", n, 9);
    cg_test_int("校验字节 = 0x95", got[8], 0x95);

    cg_test_note("---- 16 位累加和（ADD16）：0x0171；字节序是开关 ----");
    memset(&p, 0, sizeof p);
    p.head     = "\xAA\x55";
    p.head_len = 2;
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d4;
    p.data_len = 4;
    p.sum      = CG_SUM_ADD16;
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("帧长 10（校验占 2）", n, 10);
    cg_test_int("低字节在前：got[8] = 0x71", got[8], 0x71);
    cg_test_int("低字节在前：got[9] = 0x01", got[9], 0x01);
    p.sum_big_endian = 1;
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("高字节在前：got[8] = 0x01", got[8], 0x01);
    cg_test_int("高字节在前：got[9] = 0x71", got[9], 0x71);

    cg_test_note("---- 长度字段（with_len）：功能码后插一个数据长度字节 ----");
    memset(&p, 0, sizeof p);
    p.addr     = 0x01;
    p.func     = 0x10;
    p.with_len = 1;
    p.data     = k_d4;
    p.data_len = 4;
    p.sum      = CG_SUM_ADD8;              /* 覆盖 01 10 04 00 6B 00 03 = 0x7F */
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("帧长 8（地址+功能+长度+数据4+校验）", n, 8);
    cg_test_int("长度字节 = 4", got[2], 4);
    cg_test_int("累加和 = 0x7F", got[7], 0x01 + 0x10 + 0x04 + 0x00 + 0x6B + 0x00 + 0x03);

    cg_test_note("---- 不写地址 / 不写功能码（传 -1）----");
    memset(&p, 0, sizeof p);
    p.data     = k_d2;
    p.data_len = 2;
    p.addr     = -1;
    p.func     = -1;
    p.sum      = CG_SUM_NONE;
    n = cg_proto_build(&p, got, (int)sizeof(got));
    cg_test_int("只剩数据区，2 字节", n, 2);
    cg_test_int("内容就是数据区", memcmp(got, k_d2, 2), 0);
}

/** 拆回来：健帧通过、坏帧按原因分别报 -2 / -3 / -1 */
/**
 * @brief 拆回来：健帧通过、坏帧按原因分别报不同错误码
 * @return void 无返回值
 */
static void test_proto_parse(void)
{
    static const unsigned char k_d4[4] = { 0x00, 0x6B, 0x00, 0x03 };
    unsigned char              frame[32];
    cg_proto_t                 p, q;
    int                        n, addr, func, dlen;
    const unsigned char       *data;

    memset(&p, 0, sizeof p);
    p.head     = "\xAA\x55";
    p.head_len = 2;
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d4;
    p.data_len = 4;
    p.sum      = CG_SUM_ADD8;
    p.tail     = "\r\n";
    p.tail_len = 2;
    n = cg_proto_build(&p, frame, (int)sizeof(frame));
    cg_test_int("拼出一帧（头2+地址+功能+数据4+校验+尾2 = 11）", n, 11);

    cg_test_note("---- 同一骨架验它：通过并拆出字段 ----");
    cg_test_int("解析通过", cg_proto_parse(&p, frame, n, &addr, &func, &data, &dlen), 0);
    cg_test_int("地址 = 1", addr, 1);
    cg_test_int("功能码 = 3", func, 3);
    cg_test_int("数据长度 = 4（帧长推算出来的）", dlen, 4);
    cg_test_int("数据区指到帧里正确的位置", memcmp(data, k_d4, 4), 0);

    cg_test_note("---- 坏帧：改一个数据字节 -> 校验不对（-2）----");
    frame[5] ^= 0x01;
    cg_test_int("校验不对返回 -2", cg_proto_parse(&p, frame, n, NULL, NULL, NULL, NULL), -2);
    frame[5] ^= 0x01;
    cg_test_int("改回来又能过", cg_proto_parse(&p, frame, n, NULL, NULL, NULL, NULL), 0);

    cg_test_note("---- 对不上骨架：帧头 / 地址 / 帧尾 各错一次（-3）----");
    frame[0] ^= 0x01;
    cg_test_int("帧头不对 -> -3", cg_proto_parse(&p, frame, n, NULL, NULL, NULL, NULL), -3);
    frame[0] ^= 0x01;
    q = p;
    q.addr = 0x02;
    cg_test_int("不是发给这一台的（地址不匹配）-> -3",
                cg_proto_parse(&q, frame, n, NULL, NULL, NULL, NULL), -3);
    frame[n - 1] ^= 0x01;
    cg_test_int("帧尾不对 -> -3", cg_proto_parse(&p, frame, n, NULL, NULL, NULL, NULL), -3);
    frame[n - 1] ^= 0x01;

    cg_test_note("---- 帧太短（-1）----");
    cg_test_int("只给 3 字节 -> -1", cg_proto_parse(&p, frame, 3, NULL, NULL, NULL, NULL), -1);
    cg_test_int("给 0 字节 -> -1", cg_proto_parse(&p, frame, 0, NULL, NULL, NULL, NULL), -1);

    cg_test_note("---- with_len：长度字节说了算，且要与实际长度核对 ----");
    memset(&q, 0, sizeof q);
    q.addr     = 0x01;
    q.func     = 0x10;
    q.with_len = 1;
    q.data     = k_d4;
    q.data_len = 4;
    q.sum      = CG_SUM_ADD8;
    n = cg_proto_build(&q, frame, (int)sizeof(frame));
    cg_test_int("解析通过", cg_proto_parse(&q, frame, n, &addr, &func, &data, &dlen), 0);
    cg_test_int("长度字节给出 4", dlen, 4);
    cg_test_int("数据区对得上", memcmp(data, k_d4, 4), 0);
    {
        unsigned char bad[16];

        memcpy(bad, frame, (size_t)n);
        bad[2] = 9;                        /* 长度字节说 9，实际只有 4 —— 数据区放不下 */
        cg_test_int("长度字节与实际不符 -> -3", cg_proto_parse(&q, bad, n, NULL, NULL, NULL, NULL), -3);
    }
}

/** 参数越界与"不越界写"：金丝雀两字节守着输出缓冲 */
/**
 * @brief 参数越界与"不越界写"：金丝雀两字节守着输出缓冲
 * @return void 无返回值
 */
static void test_proto_guard(void)
{
    static const unsigned char k_d2[2] = { 0x00, 0x6B };
    unsigned char              raw[16];
    cg_proto_t                 p;
    int                        i;

    memset(&p, 0, sizeof p);
    p.head     = "\xAA\x55";
    p.head_len = 2;
    p.addr     = 0x01;
    p.func     = 0x03;
    p.data     = k_d2;
    p.data_len = 2;
    p.sum      = CG_SUM_ADD8;
    p.tail     = "\r\n";
    p.tail_len = 2;

    cg_test_note("---- 输出缓冲只给 8 字节（需要 9）：必须拒绝，且一个字节都不许写出去 ----");
    memset(raw, 0xA5, sizeof raw);
    cg_test_int("放不下返回 -1", cg_proto_build(&p, raw + 4, 8), -1);
    for (i = 0; i < (int)sizeof(raw); i++) {
        if (raw[i] != 0xA5) break;
    }
    cg_test_int("金丝雀一个字节都没被动（从第 0 个开始查）", i, 16);

    cg_test_int("刚好放下（9 字节）就成功", cg_proto_build(&p, raw + 4, 9), 9);

    cg_test_note("---- 非法参数一律 -1 ----");
    cg_test_int("out = NULL", cg_proto_build(&p, NULL, 32), -1);
    cg_test_int("out_size = 0", cg_proto_build(&p, raw, 0), -1);
    cg_test_int("结构体为 NULL", cg_proto_build(NULL, raw, 32), -1);
    {
        cg_proto_t bad = p;

        bad.head_len = 2;
        bad.head     = NULL;
        cg_test_int("说了有帧头却没给指针", cg_proto_build(&bad, raw, 32), -1);
        bad = p;
        bad.data_len = 2;
        bad.data     = NULL;
        cg_test_int("说了有数据却没给指针", cg_proto_build(&bad, raw, 32), -1);
        bad = p;
        bad.addr = 300;
        cg_test_int("地址越界（>255）", cg_proto_build(&bad, raw, 32), -1);
        bad = p;
        bad.with_len = 1;
        bad.data_len = 256;                /* 长度字段只有一个字节 */
        cg_test_int("长度字段放不下 256", cg_proto_build(&bad, raw, 32), -1);
    }
    cg_test_int("parse 传 NULL 结构体 -> -1", cg_proto_parse(NULL, raw, 9, NULL, NULL, NULL, NULL), -1);
}

CG_TEST_SUITE("组帧：五种校验与跨实现对齐", test_proto_sum)
CG_TEST_SUITE("组帧：拆回来与坏帧", test_proto_parse)
CG_TEST_SUITE("组帧：参数越界与不越界写", test_proto_guard)
