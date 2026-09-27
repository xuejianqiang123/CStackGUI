/**
 * @file    test_kit_fs.c
 * @brief   二进制文件读写与文件大小：cg_file_read_bytes / cg_file_write_bytes / cg_file_size
 * @details 为什么值得单独一套：**含 0x00 的数据用文本那套读会静默截断** ——
 *          `cg_file_read` 返回的是字符串，`strlen` 到第一个 0 就停；读固件 / 镜像 / 抓包时
 *          你以为读到了、其实只拿到最前面一小段，而**任何地方都不会报错**。
 *          这一套把「文本截断」与「二进制完整」并排钉住，再用 CRC 交叉验证两条路一致。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>

/**
 * @brief 二进制文件读写与大小：含 0x00 不被截断 / 空文件与打不开要分得开 / 参数非法不破坏已有文件
 * @return void 无返回值
 */
static void test_fs_bytes(void)
{
    char          *dir  = cg_app_dir();
    char          *path = cg_path_join(dir, "fs-bytes-test.bin");
    char          *miss = cg_path_join(dir, "__no_such_bin__");
    unsigned char  data[1000];
    unsigned char *got;
    int            n, i;

    for (i = 0; i < 1000; i++) data[i] = (unsigned char)i;   /* 每 256 字节就有一个 0x00 */

    cg_test_note("---- 写二进制（长度 1000，含多个 0x00 与 0xFF） ----");
    cg_file_delete(path);                                    /* 清掉上次跑的残留 */
    cg_test_int("写 1000 字节成功", cg_file_write_bytes(path, data, 1000), 0);
    cg_test_int("问大小 = 1000", (long long)cg_file_size(path), 1000LL);

    cg_test_note("---- 读回来：长度与内容都要一模一样 ----");
    n   = -1;
    got = cg_file_read_bytes(path, &n);
    cg_test_ok("读到了（非 NULL）", got != NULL);
    cg_test_int("长度 = 1000", n, 1000);
    cg_test_int("内容逐字节相同", memcmp(got, data, 1000) == 0, 1);

    cg_test_note("---- 对照：文本那套读同一个文件会被第一个 0x00 截断 ----");
    {
        char *txt = cg_file_read(path);

        cg_test_ok("cg_file_read 也能打开它", txt != NULL);
        cg_test_int("  但它给出的是**空串**（第一个字节就是 0x00）", txt ? (long long)strlen(txt) : -1, 0);
        cg_free(txt);
    }

    cg_test_note("---- 交叉验证：两条路算同一个 CRC ----");
    {
        unsigned crc_file = 0;

        cg_test_int("cg_crc32_file 算成功", cg_crc32_file(path, &crc_file), 1);
        cg_test_int("  与 cg_crc32(读到的字节) 相同", (long long)crc_file, (long long)cg_crc32(got, n));
    }
    cg_free(got);

    cg_test_note("---- 0 字节文件：要能区分「空文件」与「打不开」 ----");
    cg_test_int("写一个空文件成功", cg_file_write_bytes(path, NULL, 0), 0);
    cg_test_int("  大小 = 0", (long long)cg_file_size(path), 0LL);
    n   = -7;
    got = cg_file_read_bytes(path, &n);
    cg_test_ok("  读出来**非 NULL**（空文件不是失败）", got != NULL);
    cg_test_int("  长度 = 0", n, 0);
    cg_free(got);

    cg_test_note("---- 打不开：返回 NULL 且**不动** out_len ----");
    n   = 12345;
    got = cg_file_read_bytes(miss, &n);
    cg_test_ok("文件不存在 -> NULL", got == NULL);
    cg_test_int("  -> out_len 保持原值（不会被写成 0）", n, 12345);
    cg_test_int("问不存在文件的大小 = -1", (long long)cg_file_size(miss), -1LL);
    cg_test_int("问目录的大小 = -1（目录不是文件）", (long long)cg_file_size(dir), -1LL);

    cg_test_note("---- 参数非法：返回失败，且**不破坏已有文件** ----");
    cg_test_int("写 1000 字节成功（准备被误写）", cg_file_write_bytes(path, data, 1000), 0);
    cg_test_int("len 传 -1 -> -1", cg_file_write_bytes(path, data, -1), -1);
    cg_test_int("data 传 NULL 但 len > 0 -> -1", cg_file_write_bytes(path, NULL, 10), -1);
    cg_test_int("路径传 NULL -> -1", cg_file_write_bytes(NULL, data, 10), -1);
    cg_test_int("  -> 原文件**没被截断**（还是 1000 字节）", (long long)cg_file_size(path), 1000LL);
    cg_test_int("读：路径传 NULL -> NULL", cg_file_read_bytes(NULL, &n) == NULL, 1);
    got = cg_file_read_bytes(path, NULL);
    cg_test_ok("读：out_len 传 NULL 也能读（只是不写长度）", got != NULL);
    cg_free(got);

    cg_file_delete(path);
    cg_free(miss);
    cg_free(path);
    cg_free(dir);
}

CG_TEST_SUITE("kit：二进制文件读写（含 0x00 不被截断）", test_fs_bytes);
