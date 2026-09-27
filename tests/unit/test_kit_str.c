/**
 * @file    test_kit_str.c
 * @brief   cg_kit 编码类：十六进制 / Base64 / URL（cg_kit_str.c 那六个函数）
 * @details 为什么盯它：这三个都是**协议的公共分母**（串口要发 HEX、HTTP/JSON 要 Base64、
 *          URL 要百分号），错一个字节对方就解析不出来 —— 而它们又都属于"自己写一遍看起来
 *          也能跑"的东西。所以这里只认**公开测试向量**（Base64 用 RFC 4648 §10 的标准样例），
 *          外加从两个示例搬过来的 HEX 语义（那两处手写实现对不上，示例就得改）。
 *
 *          只 include 公共头：这些是给业务用的 API，白盒进来反而测不到"契约"。
 *
 *          顺带一条容易漏的：Base64 解码的"长度合法但尾部有多余位"（改坏了最后一个字符）
 *          必须报错 —— 只查长度的实现会安静地把一个错误的字节解出来。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>
#include <stdio.h>

/** @brief 比较两个字节串是否相同（相等返回 1） */
static int bytes_eq(const void *a, int la, const void *b, int lb)
{
    return (la == lb) && (la == 0 || memcmp(a, b, (size_t)la) == 0);
}

/* ------------------------------------------------------------ Base64 */

/** @brief RFC 4648 §10 的标准样例：编码与解码各走一遍 */
static void test_base64_vectors(void)
{
    static const struct { const char *plain; const char *b64; } k_cases[] = {
        { "",       ""         },
        { "f",      "Zg=="     },
        { "fo",     "Zm8="     },
        { "foo",    "Zm9v"     },
        { "foob",   "Zm9vYg==" },
        { "fooba",  "Zm9vYmE=" },
        { "foobar", "Zm9vYmFy" },
    };
    char buf[64];
    char back[64];

    cg_test_note("---- Base64：RFC 4648 §10 标准样例 ----");
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        const char *plain = k_cases[i].plain;
        const char *want  = k_cases[i].b64;
        int         len   = (int)strlen(plain);
        char        name[80];
        int         n, m;

        n = cg_base64_encode(plain, len, buf, (int)sizeof(buf));
        snprintf(name, sizeof(name), "编码 \"%s\"", plain);
        cg_test_str(name, buf, want);

        m = cg_base64_decode(want, back, (int)sizeof(back));
        snprintf(name, sizeof(name), "  -> 解码回 \"%s\"", plain);
        cg_test_int(name, m, len);
        cg_test_int("  -> 内容一致", bytes_eq(back, m, plain, len), 1);
        cg_test_int("  -> 编码长度与返回值一致", n, (int)strlen(want));
    }
}

/** @brief 解码的边界：空白、无填充、坏长度、坏填充、尾部多余位、缓冲不够 */
static void test_base64_decode_edges(void)
{
    unsigned char out[64];
    int           n;

    cg_test_note("---- Base64 解码边界 ----");
    n = cg_base64_decode("Zm9v\r\nYmFy", out, (int)sizeof(out));
    cg_test_int("换行/空白被忽略", n, 6);
    cg_test_int("  -> 内容", bytes_eq(out, n, "foobar", 6), 1);

    n = cg_base64_decode("Zm9vYmE", out, (int)sizeof(out));   /* "fooba" 不写填充 */
    cg_test_int("无填充也认（7 字符）", n, 5);
    cg_test_int("  -> 内容", bytes_eq(out, n, "fooba", 5), 1);

    cg_test_int("空串 -> 0 字节", cg_base64_decode("", out, (int)sizeof(out)), 0);
    cg_test_int("非法字符 '@' -> -1", cg_base64_decode("Zm9v@mFy", out, (int)sizeof(out)), -1);
    cg_test_int("长度 4n+1 不可能 -> -1", cg_base64_decode("Zm9vY", out, (int)sizeof(out)), -1);
    cg_test_int("填充个数不对（Zg=）-> -1", cg_base64_decode("Zg=", out, (int)sizeof(out)), -1);
    cg_test_int("填充超过两个 -> -1", cg_base64_decode("Zg===", out, (int)sizeof(out)), -1);
    cg_test_int("填充之后还有内容 -> -1", cg_base64_decode("Zg==A", out, (int)sizeof(out)), -1);
    /* 这一条最容易被漏：长度合法，但最后一个字符的低 4 位不是 0 —— 串被改坏了 */
    cg_test_int("尾部有多余位（Zh==）-> -1", cg_base64_decode("Zh==", out, (int)sizeof(out)), -1);

    n = cg_base64_decode("Zm9vYmFy", out, 5);   /* 需要 6 字节 */
    cg_test_int("缓冲放不下 -> -2", n, -2);
    n = cg_base64_decode("Zm9vYmFy", out, 6);
    cg_test_int("刚好放下 -> 6", n, 6);
}

/* ------------------------------------------------------------ 十六进制 */

/** @brief 编码：分隔串、大小写；解码：示例里那套语义（分隔符随便写、半个字节算错） */
static void test_hex(void)
{
    const unsigned char data[3] = { 0x01, 0x02, 0xAB };
    unsigned char       out[16];
    char                buf[32];
    int                 n;

    cg_test_note("---- 十六进制编码 ----");
    n = cg_hex_encode(data, 3, " ", 1, buf, (int)sizeof(buf));
    cg_test_int("大写 + 空格分隔：长度", n, 8);
    cg_test_str("  -> 内容", buf, "01 02 AB");
    cg_hex_encode(data, 3, NULL, 1, buf, (int)sizeof(buf));
    cg_test_str("不分隔", buf, "0102AB");
    cg_hex_encode(data, 3, ":", 0, buf, (int)sizeof(buf));
    cg_test_str("小写 + 冒号", buf, "01:02:ab");
    cg_test_int("缓冲不够 -> -1", cg_hex_encode(data, 3, " ", 1, buf, 8), -1);
    cg_test_int("空数据 -> 0", cg_hex_encode(data, 0, " ", 1, buf, (int)sizeof(buf)), 0);

    cg_test_note("---- 十六进制解码（示例里那两段手写实现的语义） ----");
    {
        const unsigned char want[3] = { 0x1A, 0x2B, 0x2C };

        n = cg_hex_decode("1A 2B 2c", out, (int)sizeof(out));
        cg_test_int("空格分隔 + 混合大小写", n, 3);
        cg_test_int("  -> 字节", bytes_eq(out, n, want, 3), 1);

        n = cg_hex_decode("1a,2b-2c\r\n", out, (int)sizeof(out));
        cg_test_int("逗号/连字符/换行都算分隔符", n, 3);
        cg_test_int("  -> 字节", bytes_eq(out, n, want, 3), 1);
    }

    n = cg_hex_decode("01 02 AB", out, (int)sizeof(out));
    cg_test_int("不放分隔符也认（1A2B 那种写法）", cg_hex_decode("0102AB", out, 8), 3);
    cg_test_int("  -> 与带分隔的一致", bytes_eq(out, n, data, 3), 1);

    cg_test_int("只剩半个字节 -> -1", cg_hex_decode("1A2", out, (int)sizeof(out)), -1);
    cg_test_int("非法字符 -> -1", cg_hex_decode("1G", out, (int)sizeof(out)), -1);
    cg_test_int("空串 -> 0", cg_hex_decode("", out, (int)sizeof(out)), 0);
    cg_test_int("缓冲放不下 -> -2", cg_hex_decode("010203", out, 2), -2);

    cg_test_note("---- 来回一趟（示例就是这么用的：发出去再显示出来） ----");
    {
        unsigned char rt[8];
        char          show[32];

        n = cg_hex_encode(data, 3, " ", 1, show, (int)sizeof(show));
        cg_test_ok("编码出的文本能再解回去", n > 0);
        n = cg_hex_decode(show, rt, (int)sizeof(rt));
        cg_test_int("往返字节数一致", n, 3);
        cg_test_int("往返内容一致", bytes_eq(rt, n, data, 3), 1);
    }
}

/* ------------------------------------------------------------ URL */

/** @brief 百分号编码：不编码集、空格两种风格、UTF-8 中文按字节展开 */
static void test_url(void)
{
    char buf[128];
    int  n;

    cg_test_note("---- URL 编码 ----");
    n = cg_url_encode("a b", 1, buf, (int)sizeof(buf));
    cg_test_int("空格 -> '+'（表单风格）", n, 3);
    cg_test_str("  -> 内容", buf, "a+b");
    cg_url_encode("a b", 0, buf, (int)sizeof(buf));
    cg_test_str("空格 -> %20（路径风格）", buf, "a%20b");

    cg_url_encode("A-Za-z0-9-_.~", 0, buf, (int)sizeof(buf));
    cg_test_str("unreserved 一个都不编", buf, "A-Za-z0-9-_.~");

    n = cg_url_encode("中文", 0, buf, (int)sizeof(buf));
    cg_test_int("UTF-8 中文按字节展开：长度", n, 18);
    cg_test_str("  -> 内容", buf, "%E4%B8%AD%E6%96%87");

    cg_test_int("缓冲不够 -> -1", cg_url_encode("abcdef", 0, buf, 4), -1);

    cg_test_note("---- URL 解码 ----");
    n = cg_url_decode("%E4%B8%AD%E6%96%87", buf, (int)sizeof(buf));
    cg_test_int("百分号解码：长度", n, 6);
    cg_test_int("  -> 是那个 UTF-8 串", bytes_eq(buf, n, "中文", 6), 1);

    cg_url_decode("a+b", buf, (int)sizeof(buf));
    cg_test_str("'+' 还原成空格", buf, "a b");

    cg_test_int("'%' 后面不足两位 -> -1", cg_url_decode("a%2", buf, (int)sizeof(buf)), -1);
    cg_test_int("'%' 后面不是十六进制 -> -1", cg_url_decode("a%G1", buf, (int)sizeof(buf)), -1);
    cg_test_int("缓冲放不下 -> -2", cg_url_decode("abcd", buf, 4), -2);

    cg_test_note("---- 来回一趟 ----");
    {
        char enc[256];
        char dec[256];

        n = cg_url_encode("name=值 & more/2?x", 0, enc, (int)sizeof(enc));
        cg_test_ok("编码成功", n > 0);
        n = cg_url_decode(enc, dec, (int)sizeof(dec));
        cg_test_int("解码长度一致", n, (int)strlen("name=值 & more/2?x"));
        cg_test_str("往返内容一致", dec, "name=值 & more/2?x");
    }
}

/* ------------------------------------------------------------ 进制与补码 */

/** @brief 2/8/10/16/36 双向转换、0x 前缀、严格性；以及补码解释 */
static void test_base(void)
{
    char      buf[80];
    long long v;

    cg_test_note("---- 整数 -> 任意进制 ----");
    cg_int_to_base(255, 16, buf, (int)sizeof(buf));
    cg_test_str("255 -> 十六进制", buf, "FF");
    cg_int_to_base(255, 2, buf, (int)sizeof(buf));
    cg_test_str("255 -> 二进制", buf, "11111111");
    cg_int_to_base(8, 8, buf, (int)sizeof(buf));
    cg_test_str("8 -> 八进制", buf, "10");
    cg_int_to_base(0, 10, buf, (int)sizeof(buf));
    cg_test_str("0 也要出 \"0\"（不是空串）", buf, "0");
    cg_int_to_base(35, 36, buf, (int)sizeof(buf));
    cg_test_str("36 进制的最大位", buf, "Z");
    cg_int_to_base(-255, 16, buf, (int)sizeof(buf));
    cg_test_str("负数带前导 '-'", buf, "-FF");
    cg_test_int("进制 1 非法", cg_int_to_base(1, 1, buf, (int)sizeof(buf)), -1);
    cg_test_int("进制 37 非法", cg_int_to_base(1, 37, buf, (int)sizeof(buf)), -1);
    cg_test_int("缓冲放不下 -> -1", cg_int_to_base(255, 2, buf, 4), -1);

    cg_test_note("---- 文本 -> 整数（看寄存器值与报文用） ----");
    cg_test_int("FF(16)", cg_base_to_int("FF", 16, &v), 1);
    cg_test_int("  -> 255", v, 255);
    cg_test_int("0x1A 认前缀", cg_base_to_int("0x1A", 16, &v), 1);
    cg_test_int("  -> 26", v, 26);
    cg_test_int("0b1010 认前缀", cg_base_to_int("0b1010", 2, &v), 1);
    cg_test_int("  -> 10", v, 10);
    cg_test_int("八进制 17", cg_base_to_int("17", 8, &v), 1);
    cg_test_int("  -> 15", v, 15);
    cg_test_int("-FF(16)", cg_base_to_int("-FF", 16, &v), 1);
    cg_test_int("  -> -255", v, -255);
    cg_test_int("首尾空白允许", cg_base_to_int("  FF  ", 16, &v), 1);
    cg_test_int("  -> 255", v, 255);

    cg_test_note("---- 该失败的一定要失败（宁可让人重看一眼） ----");
    cg_test_int("空串", cg_base_to_int("", 16, &v), 0);
    cg_test_int("只有符号 '-'(16)", cg_base_to_int("-", 16, &v), 0);
    cg_test_int("只有前缀 '0x'(16)", cg_base_to_int("0x", 16, &v), 0);
    cg_test_int("非法字符 '1G'(16)", cg_base_to_int("1G", 16, &v), 0);
    cg_test_int("数字超出进制（8 在八进制里）", cg_base_to_int("8", 8, &v), 0);
    cg_test_int("中间有空白（1A 2B 是两段，不拼成一段）", cg_base_to_int("1A 2B", 16, &v), 0);
    cg_test_int("溢出（17 个 F）", cg_base_to_int("FFFFFFFFFFFFFFFFF", 16, &v), 0);
    cg_test_int("out 传 NULL 也能问合不合法", cg_base_to_int("FF", 16, NULL), 1);

    cg_test_note("---- 进制互转（字符串进、字符串出） ----");
    cg_test_int("FF(16) -> 二进制", cg_base_convert("FF", 16, 2, buf, (int)sizeof(buf)), 8);
    cg_test_str("  -> 11111111", buf, "11111111");
    cg_base_convert("1111", 2, 16, buf, (int)sizeof(buf));
    cg_test_str("1111(2) -> 十六进制", buf, "F");
    cg_base_convert("-10", 10, 16, buf, (int)sizeof(buf));
    cg_test_str("-10(10) -> 十六进制", buf, "-A");
    cg_test_int("源串非法 -> -1", cg_base_convert("1G", 16, 2, buf, (int)sizeof(buf)), -1);
    cg_test_str("  -> out 被清成空串（不留上次的残渣）", buf, "");

    cg_test_note("---- 补码：报文给的是 0x…，协议说的是 N 位有符号 ----");
    cg_test_int("0xFFF6 按 12 位读 = -10", cg_to_signed(0xFFF6u, 12), -10);
    cg_test_int("0x80 按 8 位读 = -128", cg_to_signed(0x80u, 8), -128);
    cg_test_int("0x7F 按 8 位读 = 127", cg_to_signed(0x7Fu, 8), 127);
    cg_test_int("0xFFFF 按 16 位读 = -1", cg_to_signed(0xFFFFu, 16), -1);
    cg_test_int("位宽不同结论不同：0xF 按 4 位 = -1", cg_to_signed(0xFu, 4), -1);
    cg_test_int("  -> 同一个数按 8 位 = 15", cg_to_signed(0xFu, 8), 15);
    cg_test_int("0x8000 按 16 位 = -32768", cg_to_signed(0x8000u, 16), -32768);
    cg_test_int("0xFFF6 按 16 位也是 -10（bit15 是 1）", cg_to_signed(0xFFF6u, 16), -10);
    cg_test_int("-10 写成 12 位补码 = 0xFF6", (long long)cg_from_signed(-10, 12), 0xFF6);
    cg_test_int("-1 写成 8 位补码 = 0xFF", (long long)cg_from_signed(-1, 8), 0xFF);
    cg_test_int("来回一趟还是 -10", cg_to_signed(cg_from_signed(-10, 12), 12), -10);
    cg_test_int("bits 传 64 就按 long long 原样", cg_to_signed(0xFFFFFFFFFFFFFFFFULL, 64), -1);
}

CG_TEST_SUITE("kit：Base64（RFC 4648 向量）", test_base64_vectors);
CG_TEST_SUITE("kit：Base64 解码边界", test_base64_decode_edges);
CG_TEST_SUITE("kit：十六进制编解码", test_hex);
CG_TEST_SUITE("kit：URL 编解码", test_url);
CG_TEST_SUITE("kit：进制转换与补码", test_base);
