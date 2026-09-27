/**
 * @file    test_kit_check.c
 * @brief   cg_kit 校验：CRC32 / CRC16-Modbus（cg_kit_check.c）
 * @details CRC 的对错**不能靠"看起来像"判断** —— 多项式错一位、或者忘了取反，结果依旧是一串
 *          像模像样的随机数，只有拿去跟设备对才发现不对。所以这里只用**公开检验值**
 *          （CRC 目录里的 check value，对文本 "123456789"）：
 *          CRC-32/ISO-HDLC = 0xCBF43926、CRC-16/MODBUS = 0x4B37。
 *
 *          另外钉两条**性质**，因为它们能抓住"反着写也照样跑"的那类错误：
 *          ① 改一个字节结果必须变；② Modbus 的自校验性质 —— 把算出来的 CRC 按低字节在前
 *          接回帧尾，整帧再算一遍必须是 0（这是现场验证帧对不对的习惯做法，
 *          也顺带验了"低字节在前"这个最容易传错的约定）。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>
#include <stdio.h>

/** @brief CRC32：公开检验值 + 几条性质 */
static void test_crc32(void)
{
    cg_test_note("---- CRC32（IEEE 802.3 / zlib 同一套） ----");
    cg_test_int("空输入 = 0（= 空消息的 CRC）", (long long)cg_crc32("", 0), 0);
    cg_test_int("data 传 NULL 也当空消息", (long long)cg_crc32(NULL, 0), 0);
    cg_test_int("\"123456789\" 检验值 = 0xCBF43926",
                (long long)cg_crc32("123456789", 9), 0xCBF43926LL);
    cg_test_int("\"a\" 检验值 = 0xE8B7BE43", (long long)cg_crc32("a", 1), 0xE8B7BE43LL);
    cg_test_int("改一个字节结果就变",
                cg_crc32("123456788", 9) != cg_crc32("123456789", 9), 1);
    cg_test_int("换个顺序结果不同（不是求和的替代品）",
                cg_crc32("ab", 2) != cg_crc32("ba", 2), 1);
    cg_test_int("同内容两次一致", cg_crc32("abc", 3) == cg_crc32("abc", 3), 1);
}

/** @brief CRC16/Modbus：公开检验值 + 整帧自校验性质 */
static void test_crc16(void)
{
    cg_test_note("---- CRC16/Modbus（工控设备帧最常用的那个） ----");
    cg_test_int("空输入 = 0xFFFF（初值）", (long long)cg_crc16(NULL, 0), 0xFFFF);
    cg_test_int("\"123456789\" 检验值 = 0x4B37",
                (long long)cg_crc16("123456789", 9), 0x4B37);
    cg_test_int("高 16 位是 0（可以直接当 16 位用）",
                (long long)(cg_crc16("123456789", 9) >> 16), 0);
    cg_test_int("改一个字节结果就变",
                cg_crc16("123456788", 9) != cg_crc16("123456789", 9), 1);
    cg_test_int("和 CRC32 的低 16 位不是同一个数（别把两个算法混用）",
                (long long)(cg_crc16("123456789", 9) & 0xFFFFu) !=
                (long long)(cg_crc32("123456789", 9) & 0xFFFFu), 1);

    cg_test_note("---- 一帧 Modbus 请求的实际用法（含自校验） ----");
    {
        /* 01 03 00 00 00 01 = 读从机 1 的保持寄存器，从 0 起读 1 个。
         * 协议里 CRC 是**低字节在前**挂在帧尾的。 */
        unsigned char frame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
        unsigned      crc      = cg_crc16(frame, 6);

        frame[6] = (unsigned char)(crc & 0xFF);
        frame[7] = (unsigned char)(crc >> 8);

        /* 把 CRC 一起算进去，含 CRC 的整帧再算一遍 = 0 —— 这是现场核对帧有没有坏的
         * 习惯做法，也顺带证明了"低字节在前"这个约定没写反（写反了这里就不是 0）。 */
        cg_test_int("整帧（含 CRC）再算一遍 = 0", (long long)cg_crc16(frame, 8), 0);
    }
}

CG_TEST_SUITE("kit：CRC32", test_crc32);
CG_TEST_SUITE("kit：CRC16/Modbus", test_crc16);
