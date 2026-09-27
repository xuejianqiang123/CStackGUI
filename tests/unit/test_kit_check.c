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

/* ------------------------------------------------------------ 哈希用的助手
 *
 * 每处调用都必须把 cg_hash 的返回值放掉：--memcheck 是逐套件查"存活堆块有没有新增"，
 * 顺手漏一个就等于给整套测试加了一个假阳性。
 */

/** @brief 与 build\hash-vectors.mjs 里同一个确定性生成器：第 i 字节 = 'a' + (i % 26) */
/**
 * @brief 与构建脚本里同一个确定性生成器：第 i 字节 = 'a' + (i % 26)
 * @param buf 输出缓冲
 * @param n 要填的字节数
 * @return void 无返回值
 */
static void fill_pattern(char *buf, int n)
{
    int i;

    for (i = 0; i < n; i++) buf[i] = (char)('a' + (i % 26));
}

/** @brief 断言 cg_hash(文本) 等于期望值 */
/**
 * @brief 断言"文本的摘要"等于期望值
 * @param what 这条断言在说什么
 * @param data 待算的字节
 * @param len 字节数
 * @param algo 算法名
 * @param want 期望的摘要串
 * @return void 无返回值
 */
static void hash_eq_str(const char *what, const char *data, int len,
                        const char *algo, const char *want)
{
    char *got = cg_hash(data, len, algo);

    cg_test_str(what, got, want);
    cg_free(got);
}

/** @brief 断言 cg_hash(模式串(n)) 等于期望值 —— 这些期望值由 Node/OpenSSL 派生 */
/**
 * @brief 断言"模式串的摘要"等于期望值 —— 这些期望值由构建脚本用 OpenSSL 派生
 * @param algo 算法名
 * @param n 模式串字节数
 * @param want 期望的摘要串
 * @return void 无返回值
 */
static void hash_eq(const char *algo, int n, const char *want)
{
    char  name[64];
    char  buf[160];
    char *got;

    fill_pattern(buf, n);
    snprintf(name, sizeof(name), "%s(%d 字节模式串)", algo, n);
    got = cg_hash(buf, n, algo);
    cg_test_str(name, got, want);
    cg_free(got);
}

/** @brief 只问"这个算法名认不认/能不能算出来" */
/**
 * @brief 只问"这个算法名认不认 / 能不能算出来"
 * @param data 待算的字节
 * @param len 字节数
 * @param algo 算法名
 * @return int 1 = 认且算得出；0 = 不认或算不出（NULL）
 */
static int hash_ok(const char *data, int len, const char *algo)
{
    char *h  = cg_hash(data, len, algo);
    int   ok = (h != NULL);

    cg_free(h);
    return ok;
}

/** @brief CRC32：公开检验值 + 几条性质 */
/**
 * @brief CRC32：公开检验值 + 几条性质
 * @return void 无返回值
 */
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
/**
 * @brief CRC16/Modbus：公开检验值 + 整帧自校验性质
 * @return void 无返回值
 */
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

/* ------------------------------------------------------------ 哈希（摘要） */

/** @brief 公开向量：RFC 1321（MD5）与 FIPS 180-1/2/4（SHA-1/256/512） */
/**
 * @brief 公开向量：RFC 1321（MD5）与 FIPS 180-1/2/4（SHA-1/256/512）
 * @return void 无返回值
 */
static void test_hash_public(void)
{
    cg_test_note("---- RFC 1321：MD5 ----");
    hash_eq_str("md5(\"\")", "", 0, "md5", "d41d8cd98f00b204e9800998ecf8427e");
    hash_eq_str("md5(\"abc\")", "abc", 3, "md5", "900150983cd24fb0d6963f7d28e17f72");
    hash_eq_str("md5(\"message digest\")", "message digest", 14, "md5",
                "f96b697d7cb7938d525a2f31aaf161d0");
    hash_eq_str("md5(a..z)", "abcdefghijklmnopqrstuvwxyz", 26, "md5",
                "c3fcd3d76192e4007dfb496cca67e13b");

    cg_test_note("---- FIPS 180-1：SHA-1 ----");
    hash_eq_str("sha1(\"abc\")", "abc", 3, "sha1", "a9993e364706816aba3e25717850c26c9cd0d89d");
    hash_eq_str("sha1(56 字节) —— 正好卡在补位边界",
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, "sha1",
                "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    cg_test_note("---- FIPS 180-2：SHA-256 ----");
    hash_eq_str("sha256(\"abc\")", "abc", 3, "sha256",
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hash_eq_str("sha256(56 字节) —— 同一个补位边界",
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, "sha256",
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    cg_test_note("---- FIPS 180-4：SHA-512 ----");
    hash_eq_str("sha512(\"abc\")", "abc", 3, "sha512",
                "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    hash_eq_str("sha512(112 字节) —— 128 字节块的补位边界",
                "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112, "sha512",
                "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
                "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

/**
 * @brief 分块边界：长度 64 / 65 / 128 / 129（期望值由构建脚本用 OpenSSL 派生）
 * @return void 无返回值
 * @note 补位长度算错是哈希实现最典型的错法，而且在短输入上看不出来：
 *       64 与 128 是"补位正好占满整块 / 要另起一块"的分水岭，65 与 129 是它的另一侧
 */
static void test_hash_block_edges(void)
{
    cg_test_note("---- 分块边界（常量来自 Node/OpenSSL，不是手算的）----");
    hash_eq("md5", 64, "a2eaf6295c32adc403865fd96a2f182b");
    hash_eq("sha1", 64, "93249d4c2f8903ebf41ac358473148ae6ddd7042");
    hash_eq("sha256", 64, "2fcd5a0d60e4c941381fcc4e00a4bf8be422c3ddfafb93c809e8d1e2bfffae8e");
    hash_eq("sha512", 64,
            "1cdcbebccccaed3fa2acc8bcd1835c67514bd29657a348de629aedb0d5503377"
            "285b78656a3a794f4cef88708916d23f78ead08d710101e3c9ba6a823ffc6067");
    hash_eq("md5", 65, "eba2cce0ca8df47e62414a736b3105a2");
    hash_eq("sha1", 65, "cf2a63cc308225cf07b498d2309a01dd0df52f67");
    hash_eq("sha256", 65, "1b3cd1877ab2f2f19f7be001722554f336cb799df0329de0bb4c118dc6abc06d");
    hash_eq("sha512", 65,
            "a10611b486051ecfba0086e5e0d4f324feabc5ff21e62fe775ff40a71b455993"
            "f4ba4da3e9d917f2e2d5a2d28cc84be0ac608c0d631eafc593dd0ba1bca30764");
    hash_eq("md5", 128, "3e8c1ccbd71838ef3df4b72e57fb9bf6");
    hash_eq("sha1", 128, "3698f9beb7cdf5b1e1ce786672fb234143d4df89");
    hash_eq("sha256", 128, "6c05be2c4268843ae47e68e611277ce62c02153f2f4d2e1e2a1a4b44f766cf74");
    hash_eq("sha512", 128,
            "217d3d9c0952c3e4907f06d4fbf34460ee852c6af591b07c2fa1c5e164558363"
            "74c95ae33e184227913f8a2e227e3bbd5187ce57aa1bad11a80f622412eb0884");
    hash_eq("md5", 129, "a9ca66cd8dd6b0f6d8cbb6a21bbf5206");
    hash_eq("sha1", 129, "1d96f2c973fdd492d08f2a16f950d654912b61ca");
    hash_eq("sha256", 129, "cd6bba8374324cbcc0c296b94f35299c0b9820393116358ac3afaf091a4955a5");
    hash_eq("sha512", 129,
            "18d5a20f40cd7903e625ac1e4fc10971fb3bd6cecb12a684f7600975340649ee"
            "bccd11cc97fe9e36bbcb3ae75d6d7a0dc2e60e0022c579e87c3ee2e3ffaf65cc");
}

/** @brief 算法名、错误路径，以及"文件分块读"与"一次算"必须同值 */
/**
 * @brief 算法名、错误路径，以及"文件分块读"与"一次算"必须同值
 * @return void 无返回值
 */
static void test_hash_edges(void)
{
    char    *dir  = cg_app_dir();
    char    *path = cg_path_join(dir, "hash-file-vector.txt");
    char    *miss = cg_path_join(dir, "__no_such_file_here__");
    char    *a, *b;
    unsigned c1;

    cg_test_note("---- 算法名 ----");
    cg_test_ok("大小写不敏感：\"MD5\" 认", hash_ok("abc", 3, "MD5"));
    cg_test_ok("\"Sha256\" 也认", hash_ok("abc", 3, "Sha256"));
    cg_test_ok("不认识的名字 -> NULL", !hash_ok("abc", 3, "md6"));
    cg_test_ok("算法名传 NULL -> NULL", !hash_ok("abc", 3, NULL));
    cg_test_ok("空数据也能算（按空数据）", hash_ok(NULL, 0, "md5"));
    a = cg_hash("abc", 3, "MD5");
    b = cg_hash("abc", 3, "md5");
    cg_test_str("\"MD5\" 与 \"md5\" 同一个值", a, b);
    cg_free(a);
    cg_free(b);

    cg_test_note("---- 文件摘要：分块读不许漏字节 ----");
    cg_file_delete(path);                                  /* 清掉上次跑的残留 */
    cg_test_int("写测试文件", cg_file_write(path, "0123456789abcdef"), 0);
    a = cg_hash("0123456789abcdef", 16, "md5");
    b = cg_hash_file(path, "md5");
    cg_test_str("cg_hash_file = cg_hash（同一份内容）", b, a);
    cg_free(a);
    cg_free(b);

    cg_test_int("cg_crc32_file 成功", cg_crc32_file(path, &c1), 1);
    cg_test_int("  -> 与 cg_crc32 同值", (long long)c1,
                (long long)cg_crc32("0123456789abcdef", 16));

    cg_test_note("---- 打不开的文件：报失败，不许给出「看着像」的值 ----");
    cg_test_ok("cg_hash_file 文件不存在 -> NULL", cg_hash_file(miss, "md5") == NULL);
    c1 = 0xDEADBEEFu;
    cg_test_int("cg_crc32_file 文件不存在 -> 0", cg_crc32_file(miss, &c1), 0);
    cg_test_int("  -> 且 out 没被动过", (long long)c1, (long long)0xDEADBEEFu);

    cg_test_note("---- 跨块：40 KB 文件 > 16 KB 一块 ----");
    {
        char big[40 * 1024 + 1];
        char *dir2  = cg_app_dir();
        char *bpath = cg_path_join(dir2, "hash-file-big.txt");
        char *h1, *h2;

        fill_pattern(big, (int)sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        cg_file_delete(bpath);
        cg_test_int("写 40 KB 测试文件", cg_file_write(bpath, big), 0);
        h1 = cg_hash(big, (int)strlen(big), "sha256");
        h2 = cg_hash_file(bpath, "sha256");
        cg_test_str("跨块读与一次算同值", h2, h1);
        cg_free(h1);
        cg_free(h2);
        cg_file_delete(bpath);
        cg_free(bpath);
        cg_free(dir2);
    }

    cg_file_delete(path);
    cg_free(miss);
    cg_free(path);
    cg_free(dir);
}

CG_TEST_SUITE("kit：CRC32", test_crc32);
CG_TEST_SUITE("kit：CRC16/Modbus", test_crc16);
CG_TEST_SUITE("kit：MD5 / SHA-1 / SHA-256 / SHA-512（公开向量）", test_hash_public);
CG_TEST_SUITE("kit：哈希的补位边界", test_hash_block_edges);
CG_TEST_SUITE("kit：哈希的算法名、错误路径与文件分块读", test_hash_edges);
