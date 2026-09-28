/**
 * @file    test_kit_frame.c
 * @brief   字节流切帧（cg_frame_*）单测：碎包 / 粘包 / 静默边界 / 缓冲不越界
 * @details 断言尽量落在**外部不变量**上，而不是"我说它对"：
 *            - 切出来的帧必须能通过 `cg_modbus_parse`（换一个实现来验）；
 *            - RTU 帧尾必须正好是 `cg_crc16` 的低字节在前；
 *            - 灌噪声时缓冲两侧的金丝雀一个字节都不许变。
 *          时间是自造的假时间戳（不读时钟）⇒ **一轮都不用 sleep**，
 *          而且"空档 3ms 不切、4ms 就切"这种边界能当场断住。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>

/** @brief 在帧尾补 RTU 的 CRC16（低字节在前）
 *  @param frame 帧
 *  @param len 补之前多长
 *  @return int 补完多长（+2）
 */
static int with_crc(unsigned char *frame, int len)
{
    unsigned c = cg_crc16(frame, len);

    frame[len]     = (unsigned char)(c & 0xFFu);
    frame[len + 1] = (unsigned char)((c >> 8) & 0xFFu);
    return len + 2;
}

/** @brief 按 1/2/3 字节一块循环喂进去（模拟串口一次只读到几个字节）
 *  @param f 切帧状态
 *  @param data 要喂的字节
 *  @param len 字节数
 *  @param now_ms 起始时刻；每喂一块 +1ms（远小于 gap，所以中途不许切帧）
 *  @return unsigned long 喂完之后的时刻
 */
static unsigned long feed_chunked(cg_frame_t *f, const unsigned char *data, int len,
                                  unsigned long now_ms)
{
    int i = 0;
    int k = 0;

    while (i < len) {
        int n = (k % 3) + 1;

        if (n > len - i) n = len - i;
        cg_frame_feed(f, data + i, n, now_ms);
        now_ms += 1;
        i += n;
        k++;
    }
    return now_ms;
}

/**
 * @brief 静默切帧（Modbus RTU 的 t3.5）：边界两侧、逐字节碎包、空档由下一个字节发现
 * @return void 无返回值
 */
static void test_frame_silence(void)
{
    static unsigned char buf[256];
    unsigned char        req[16];
    unsigned char        got[256];
    cg_frame_t           fr;
    unsigned long        t;
    int                  n, i;

    cg_test_note("---- 一个真实 Modbus/RTU 请求帧，逐字节喂进去 ----");
    n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x006B, 3, req, sizeof req);
    cg_test_int("请求帧 8 字节", n, 8);
    cg_test_int("帧尾低字节 = CRC16 低 8 位", req[6], (int)(cg_crc16(req, 6) & 0xFFu));
    cg_test_int("帧尾高字节 = CRC16 高 8 位", req[7], (int)((cg_crc16(req, 6) >> 8) & 0xFFu));

    cg_test_int("init（静默 4ms）", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_SILENCE, 4), 0);

    t = 100;
    for (i = 0; i < n; i++) {
        cg_frame_feed(&fr, req + i, 1, t);
        t += 1;                                   /* 字节间 1ms：不够静默 */
    }
    cg_test_int("逐字节喂完，还没有整帧", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_tick(&fr, t + 2);                    /* 距最后一个字节 3ms = gap - 1 */
    cg_test_int("空档 3ms（gap-1）不切", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_tick(&fr, t + 3);                    /* 距最后一个字节 4ms = gap */
    cg_test_int("空档正好到 gap，切出 8 字节", cg_frame_take(&fr, got, sizeof got), 8);
    cg_test_int("切出来的与请求帧逐字节相同", memcmp(got, req, 8), 0);
    cg_test_int("再取就没有了", cg_frame_take(&fr, got, sizeof got), 0);
    cg_test_int("切出的帧数 = 1", (int)cg_frame_frames(&fr), 1);
    cg_test_int("一字节都没丢", (int)cg_frame_dropped(&fr), 0);

    cg_test_note("---- 空档由下一个字节发现（没有定时器也能用）----");
    cg_frame_reset(&fr);
    t = 200;
    for (i = 0; i < n; i++) {
        cg_frame_feed(&fr, req + i, 1, t);
        t += 1;
    }
    cg_frame_feed(&fr, "Z", 1, t + 4);            /* 空档 5ms：这句该先把上一帧收尾 */
    cg_test_int("补一个字节时发现空档，上一帧照样切出来", cg_frame_take(&fr, got, sizeof got), 8);
    cg_test_int("内容还是那一帧", memcmp(got, req, 8), 0);
    cg_test_int("新来的那个字节算下一帧的开头", fr.len, 1);
}

/**
 * @brief 三种"自带边界"的切法：定长 / 分隔符 / 头里带长度
 * @return void 无返回值
 */
static void test_frame_bounded(void)
{
    static unsigned char buf[256];
    unsigned char        got[256];
    unsigned char        tcp[16];
    cg_frame_t           fr;
    int                  n, i;

    cg_test_note("---- 定长 ----");
    cg_test_int("init 定长 4", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_FIXED, 4), 0);
    cg_frame_feed(&fr, "AAAABBBBCC", 10, 0);
    memset(got, 0x5A, sizeof got);
    n = cg_frame_take(&fr, got, sizeof got);
    cg_test_int("第一帧 4 字节", n, 4);
    cg_test_int("take 不擅自补 NUL（后面那字节原样留着）", got[4], 0x5A);
    got[n] = 0;                                   /* 想当字符串比，得自己补 NUL */
    cg_test_str("第一帧内容", (char *)got, "AAAA");
    n = cg_frame_take(&fr, got, sizeof got);
    got[n] = 0;
    cg_test_str("第二帧内容", (char *)got, "BBBB");
    cg_test_int("剩 2 字节不够一帧，先不给", cg_frame_take(&fr, got, sizeof got), 0);

    cg_test_note("---- 分隔符（分隔符留在帧里）----");
    cg_test_int("init 分隔符 '\\n'", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_DELIM, '\n'), 0);
    cg_frame_feed(&fr, "abc\nde", 6, 0);
    n = cg_frame_take(&fr, got, sizeof got);
    cg_test_int("先切出完整的那一帧", n, 4);
    got[n] = 0;
    cg_test_str("分隔符留在帧里", (char *)got, "abc\n");
    cg_test_int("半截的留着，先不切", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_feed(&fr, "fg\n", 3, 1);
    n = cg_frame_take(&fr, got, sizeof got);
    cg_test_int("补上后半截就切出来", n, 5);
    got[n] = 0;
    cg_test_str("第二帧内容", (char *)got, "defg\n");

    cg_test_note("---- 头里带长度（Modbus/TCP 的 MBAP：偏移 4、宽 2、大端）----");
    tcp[0] = 0x00; tcp[1] = 0x01;                 /* 事务号 */
    tcp[2] = 0x00; tcp[3] = 0x00;                 /* 协议号 */
    tcp[4] = 0x00; tcp[5] = 0x09;                 /* 长度 = 后面 9 字节（单元 1 + PDU 8）*/
    tcp[6] = 0x01;                                /* 单元标识 */
    tcp[7] = 0x03; tcp[8] = 0x06;                 /* 功能码 + 字节数 */
    tcp[9] = 0xAE; tcp[10] = 0x41;
    tcp[11] = 0x56; tcp[12] = 0x52;
    tcp[13] = 0x43; tcp[14] = 0x40;               /* 整帧 15 字节 = 6 + 长度 9 */

    cg_test_int("init 长度字段模式", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_LENFIELD, 4), 0);
    cg_frame_feed(&fr, tcp, 5, 0);
    cg_test_int("头没收全：不切", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_feed(&fr, tcp + 5, 1, 0);
    cg_test_int("长度字段够了、帧没够：不切", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_feed(&fr, tcp + 6, 4, 0);
    cg_test_int("还差 5 字节：不切", cg_frame_take(&fr, got, sizeof got), 0);
    cg_frame_feed(&fr, tcp + 10, 5, 0);
    n = cg_frame_take(&fr, got, sizeof got);
    cg_test_int("收满 15 字节就切一帧", n, 15);
    cg_test_int("切出来的与原样一致", memcmp(got, tcp, 15), 0);

    /* 外部不变量：切出来的帧交给 Modbus 解析器，必须解得对 */
    {
        cg_modbus_rsp_t r;

        cg_test_int("切出的帧 cg_modbus_parse 解得通", cg_modbus_parse(CG_MODBUS_TCP, got, n, &r), 0);
        cg_test_int("解出 3 个寄存器", r.nvalues, 3);
        cg_test_int("第 1 个寄存器 = 0xAE41", (int)r.values[0], 0xAE41);
        cg_test_int("第 3 个寄存器 = 0x4340", (int)r.values[2], 0x4340);
    }

    cg_test_note("---- 粘包：两帧贴着来（TCP 常见），长度字段模式必须照样分成两帧 ----");
    cg_frame_reset(&fr);
    {
        unsigned char two[32];

        memcpy(two, tcp, 15);
        memcpy(two + 15, tcp, 15);
        cg_frame_feed(&fr, two, 30, 0);
        cg_test_int("粘包：第一帧", cg_frame_take(&fr, got, sizeof got), 15);
        cg_test_int("粘包：第二帧照样分得开", cg_frame_take(&fr, got, sizeof got), 15);
        cg_test_int("粘包：没有了", cg_frame_take(&fr, got, sizeof got), 0);
    }

    cg_test_note("---- 长度字段不靠谱：整段丢掉并计数，不假装切出一帧 ----");
    cg_frame_reset(&fr);
    {
        unsigned char junk[8];

        for (i = 0; i < 8; i++) junk[i] = 0xFF;
        cg_frame_feed(&fr, junk, 8, 0);
        cg_test_int("不切帧", cg_frame_take(&fr, got, sizeof got), 0);
        cg_test_int("丢掉的那 6 个字节记在 dropped 里", (int)cg_frame_dropped(&fr), 6);
    }
}

/**
 * @brief 缓冲不越界（金丝雀）+ 噪声不当帧 + 排队上限 + 长期小缓冲不"往后爬"
 * @return void 无返回值
 */
static void test_frame_guards(void)
{
    static unsigned char raw[64];
    static unsigned char buf[64];
    unsigned char        got[64];
    cg_frame_t           fr;
    int                  i, ok = 0, n;

    cg_test_note("---- 灌 200 字节噪声：缓冲两侧的金丝雀一个字节都不许变 ----");
    memset(raw, 0xA5, sizeof raw);
    cg_test_int("init（缓冲 = raw[8..40)、32 字节一帧上限）",
                cg_frame_init(&fr, raw + 8, 32, CG_FRAME_SILENCE, 5), 0);
    cg_frame_limits(&fr, 16, 32);                 /* 少于 16 字节不当帧 */
    for (i = 0; i < 200; i++) cg_frame_feed(&fr, "Z", 1, (unsigned long)i);
    for (i = 0; i < 8; i++) cg_test_int("缓冲前面的金丝雀没被写", raw[i], 0xA5);
    for (i = 40; i < 64; i++) cg_test_int("缓冲后面的金丝雀没被写", raw[i], 0xA5);
    cg_frame_tick(&fr, 500);                      /* 收尾：剩下的零头也不够 min_len */
    cg_test_int("噪声一个字节都没被当成帧", (int)cg_frame_frames(&fr), 0);
    cg_test_int("取不到帧", cg_frame_take(&fr, got, sizeof got), 0);
    cg_test_int("灌进去的 200 字节全都记进了 dropped（丢得看得见）",
                (int)cg_frame_dropped(&fr), 200);

    cg_test_note("---- 排队上限：一次排不下就丢并计数，不静默 ----");
    cg_test_int("init 定长 4", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_FIXED, 4), 0);
    cg_frame_feed(&fr, "0123456789abcdefghijklmnopqrstuvwxyzABCD", 40, 0);   /* 10 帧 */
    n = 0;
    while (cg_frame_take(&fr, got, sizeof got) > 0) n++;
    cg_test_int("队列上限 8：只排得下 8 帧", n, 8);
    cg_test_int("多出来的 2 帧记进 dropped", (int)cg_frame_dropped(&fr), 8);
    cg_test_int("切出的帧数也只算排上队的", (int)cg_frame_frames(&fr), 8);

    cg_test_note("---- 取帧缓冲太小：返回 -1，帧还在队列里 ----");
    cg_frame_reset(&fr);
    cg_frame_feed(&fr, "AAAA", 4, 0);
    cg_test_int("缓冲太小 -> -1", cg_frame_take(&fr, got, 2), -1);
    cg_test_int("换够大的缓冲就取得到", cg_frame_take(&fr, got, sizeof got), 4);

    cg_test_note("---- 500 轮小缓冲收发：一帧都不许丢（缓冲别往后爬）----");
    cg_frame_reset(&fr);
    {
        /* 统计量是**累计**的（reset 只清缓冲、不清统计，见头文件说明）⇒ 这里断增量 */
        unsigned long d0 = cg_frame_dropped(&fr);
        unsigned long f0 = cg_frame_frames(&fr);

        for (i = 0; i < 500; i++) {
            cg_frame_feed(&fr, "XYZW", 4, (unsigned long)i);
            if (cg_frame_take(&fr, got, sizeof got) == 4 && memcmp(got, "XYZW", 4) == 0) ok++;
        }
        cg_test_int("每轮都切出一帧", ok, 500);
        cg_test_int("这 500 轮一帧都没丢", (int)(cg_frame_dropped(&fr) - d0), 0);
        cg_test_int("这 500 轮切出 500 帧", (int)(cg_frame_frames(&fr) - f0), 500);
    }
}

/**
 * @brief 20 轮"碎包往返"：从站回的帧碎着来，切出来的帧必须每轮都能被解析器认下
 * @return void 无返回值
 */
static void test_frame_rtu_roundtrip(void)
{
    static unsigned char buf[128];
    unsigned char        req[16];
    unsigned char        rsp[16];
    unsigned char        got[128];
    cg_frame_t           fr;
    unsigned long        t;
    int                  n, i, round, ok = 0;

    n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x006B, 3, req, sizeof req);
    cg_test_int("请求帧 8 字节", n, 8);

    rsp[0] = 0x01; rsp[1] = 0x03; rsp[2] = 0x06;
    rsp[3] = 0xAE; rsp[4] = 0x41;
    rsp[5] = 0x56; rsp[6] = 0x52;
    rsp[7] = 0x43; rsp[8] = 0x40;
    n = with_crc(rsp, 9);
    cg_test_int("响应帧 11 字节（含 CRC）", n, 11);

    cg_test_int("init（静默 4ms）", cg_frame_init(&fr, buf, sizeof buf, CG_FRAME_SILENCE, 4), 0);

    t = 1000;
    for (round = 0; round < 20; round++) {
        t = feed_chunked(&fr, rsp, n, t);          /* 1/2/3 字节一块，每块 1ms */
        cg_frame_tick(&fr, t + 4);                 /* 空档到点 */
        t += 10;
        if (cg_frame_take(&fr, got, sizeof got) == n) {
            cg_modbus_rsp_t r;

            if (cg_modbus_parse(CG_MODBUS_RTU, got, n, &r) == 0 && r.nvalues == 3 &&
                r.values[0] == 0xAE41 && r.values[2] == 0x4340) {
                ok++;
            }
        }
    }
    cg_test_int("20 轮：每轮切出的帧都被解析器认下", ok, 20);
    cg_test_int("一共切出 20 帧", (int)cg_frame_frames(&fr), 20);
    cg_test_int("一字节都没丢", (int)cg_frame_dropped(&fr), 0);

    cg_test_note("---- 静默模式的物理限制：噪声粘在帧前面时它们是一整段 ----");
    cg_frame_reset(&fr);
    cg_frame_feed(&fr, "\x00\x00", 2, 5000);
    cg_frame_feed(&fr, rsp, 11, 5003);             /* 3ms 后：噪声与帧粘成一段（< gap）*/
    cg_frame_tick(&fr, 5003 + 4);
    i = cg_frame_take(&fr, got, sizeof got);
    cg_test_int("切出来是 2 + 11 一整段", i, 13);
    {
        cg_modbus_rsp_t r;

        cg_test_int("这种帧 CRC 一定不对（所以静默模式必须配 CRC 用）",
                    cg_modbus_parse(CG_MODBUS_RTU, got, i, &r), -2);
    }
}

CG_TEST_SUITE("切帧：静默（Modbus RTU）", test_frame_silence)
CG_TEST_SUITE("切帧：定长 / 分隔符 / 长度字段", test_frame_bounded)
CG_TEST_SUITE("切帧：缓冲与噪声边界", test_frame_guards)
CG_TEST_SUITE("切帧：碎包往返 20 轮", test_frame_rtu_roundtrip)
