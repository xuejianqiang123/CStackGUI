/**
 * @file    test_kit_bytes.c
 * @brief   kit 字节操作：反转 / 切片 / 查找 / 按字节序读写 16·32 位整数
 * @details 盯它的理由：这些全是「差一位就错、但界面照样跑」的东西 —— 切片差一字节、
 *          字节序反了、越界时写了半个字段，表现出来都是「跟设备对不上」，
 *          一旦发到现场排查成本极高。所以边界一条条钉死，尤其：
 *            · 越界时**不许写半个字段**（用哨兵字节验证"什么都没动"）
 *            · 大小端都要有明确的字节图案对照，不靠"再读一遍看看"
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>

/**
 * @brief 反转：奇数 / 偶数 / 单字节 / 长度 0 / 空指针 / 含 0x00 都要对
 * @return void 无返回值
 */
static void test_bytes_reverse(void)
{
    unsigned char a[] = { 'A', 'B', 'C', 'D', 'E' };
    unsigned char b[] = { 'A', 'B' };
    unsigned char c[] = { 'A' };
    unsigned char d[] = { 'A', 0x00, 'B' };
    unsigned char zero[1] = { 0 };

    cg_test_note("---- 反转（精易的「字节集_反转」） ----");
    cg_bytes_reverse(a, 5);
    cg_test_int("奇数长度：ABCDE -> EDCBA", memcmp(a, "EDCBA", 5) == 0, 1);
    cg_bytes_reverse(b, 2);
    cg_test_int("偶数长度：AB -> BA", memcmp(b, "BA", 2) == 0, 1);
    cg_bytes_reverse(c, 1);
    cg_test_int("一个字节：不动", c[0] == 'A', 1);
    cg_bytes_reverse(zero, 0);
    cg_test_int("长度 0：不崩", zero[0] == 0, 1);
    cg_bytes_reverse(NULL, 5);
    cg_test_int("data 传 NULL：不崩", 1, 1);
    cg_bytes_reverse(d, 3);
    cg_test_int("0x00 不是结束符：A 00 B -> B 00 A", d[0] == 'B' && d[1] == 0x00 && d[2] == 'A', 1);
    cg_bytes_reverse(a, 5);
    cg_test_int("再反转一次回到原样", memcmp(a, "ABCDE", 5) == 0, 1);
}

/**
 * @brief 切片：取到底 / 取左取右取中间 / 超出剩余按剩余算 / 该失败时一个字节都不写
 * @return void 无返回值
 */
static void test_bytes_slice(void)
{
    const unsigned char src[5] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    unsigned char       out[16];

    cg_test_note("---- 切片：count = -1 取到底（最常用那一种） ----");
    memset(out, 0xEE, sizeof(out));
    cg_test_int("从 0 取到底 = 5 字节", cg_bytes_slice(src, 5, 0, -1, out, (int)sizeof(out)), 5);
    cg_test_int("  内容一致", memcmp(out, src, 5) == 0, 1);
    cg_test_int("从 2 取到底 = 3 字节", cg_bytes_slice(src, 5, 2, -1, out, (int)sizeof(out)), 3);
    cg_test_int("  拿到 30 40 50", out[0] == 0x30 && out[1] == 0x40 && out[2] == 0x50, 1);

    cg_test_note("---- 取左边 / 取右边 / 取中间 ----");
    cg_test_int("取左边 2 个", cg_bytes_slice(src, 5, 0, 2, out, (int)sizeof(out)), 2);
    cg_test_int("  是 10 20", out[0] == 0x10 && out[1] == 0x20, 1);
    /* 「取右边 N 个」= 从 len-N 起取到底 —— 这一族的惯用法 */
    cg_test_int("取右边 2 个（from = 5-2）", cg_bytes_slice(src, 5, 3, -1, out, (int)sizeof(out)), 2);
    cg_test_int("  是 40 50", out[0] == 0x40 && out[1] == 0x50, 1);
    cg_test_int("取中间 1 个（from 1 count 1）", cg_bytes_slice(src, 5, 1, 1, out, (int)sizeof(out)), 1);
    cg_test_int("  是 20", out[0] == 0x20, 1);

    cg_test_note("---- count 超剩余：按剩余算，不报错 ----");
    cg_test_int("从 3 要 99 个 = 只给 2 个", cg_bytes_slice(src, 5, 3, 99, out, (int)sizeof(out)), 2);
    cg_test_int("count 传 0 = 写出 0 个", cg_bytes_slice(src, 5, 0, 0, out, (int)sizeof(out)), 0);

    cg_test_note("---- 该失败的一定失败，且**什么都不写** ----");
    memset(out, 0xEE, sizeof(out));
    cg_test_int("from == len：-1", cg_bytes_slice(src, 5, 5, -1, out, (int)sizeof(out)), -1);
    cg_test_int("from > len：-1", cg_bytes_slice(src, 5, 9, -1, out, (int)sizeof(out)), -1);
    cg_test_int("from 负数：-1", cg_bytes_slice(src, 5, -1, -1, out, (int)sizeof(out)), -1);
    cg_test_int("out 放不下：-2", cg_bytes_slice(src, 5, 0, -1, out, 4), -2);
    cg_test_int("out 传 NULL：-1", cg_bytes_slice(src, 5, 0, -1, NULL, 8), -1);
    cg_test_int("out_size 传 0：-1", cg_bytes_slice(src, 5, 0, -1, out, 0), -1);
    cg_test_int("data 传 NULL：-1", cg_bytes_slice(NULL, 5, 0, -1, out, 8), -1);
    cg_test_int("  上面几次失败后 out 没被动过（哨兵还是 0xEE）", out[0] == 0xEE && out[15] == 0xEE, 1);
}

/**
 * @brief 找子集：起点推进 / 重叠图案 / 参数非法
 * @return void 无返回值
 */
static void test_bytes_find(void)
{
    const unsigned char hay[6] = { 0x01, 0x02, 0x03, 0x02, 0x03, 0x04 };
    const unsigned char p2[2]  = { 0x02, 0x03 };
    const unsigned char p1[1]  = { 0x04 };
    const unsigned char px[2]  = { 0x09, 0x09 };
    const unsigned char pa[3]  = { 0x01, 0x01, 0x01 };

    cg_test_note("---- 找子集（精易的「字节集_寻找子集」） ----");
    cg_test_int("在 1,2,3,2,3,4 里找 2,3：第一个是 1", cg_bytes_find(hay, 6, p2, 2, 0), 1);
    cg_test_int("从 2 往后找：下一个是 3", cg_bytes_find(hay, 6, p2, 2, 2), 3);
    cg_test_int("从 4 往后找：没有了", cg_bytes_find(hay, 6, p2, 2, 4), -1);
    cg_test_int("单个字节 0x04：在 5", cg_bytes_find(hay, 6, p1, 1, 0), 5);
    cg_test_int("找不到：-1", cg_bytes_find(hay, 6, px, 2, 0), -1);
    cg_test_int("子集比数据长：-1", cg_bytes_find(hay, 6, p2, 2, 5), -1);

    cg_test_note("---- 参数非法 ----");
    cg_test_int("needle 长度 0：-1（空子集是调用方的错）", cg_bytes_find(hay, 6, p2, 0, 0), -1);
    cg_test_int("data 传 NULL：-1", cg_bytes_find(NULL, 6, p2, 2, 0), -1);
    cg_test_int("needle 传 NULL：-1", cg_bytes_find(hay, 6, NULL, 2, 0), -1);
    cg_test_int("from 负数当 0 处理", cg_bytes_find(hay, 6, p1, 1, -5), 5);

    cg_test_note("---- 重叠图案：AAA 在 AAAA 里（从 1 找） ----");
    cg_test_int("AAAA 从下标 1 找 AAA = 1", cg_bytes_find("AAAA", 4, "AAA", 3, 1), 1);
}

/**
 * @brief 16 / 32 位整数按字节序读写：明确的字节图案、大小端往返、越界一个字节都不写
 * @return void 无返回值
 */
static void test_bytes_u16u32(void)
{
    unsigned char frame[8];
    unsigned char be[2] = { 0x12, 0x34 };
    unsigned char le[2] = { 0x34, 0x12 };
    unsigned char be4[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    unsigned char le4[4] = { 0xEF, 0xBE, 0xAD, 0xDE };

    cg_test_note("---- 16 位：明确的字节图案对照（不靠「再读一遍看看」） ----");
    cg_test_int("读大端 12 34 = 0x1234", cg_bytes_get_u16(be, 2, 0, 1), 0x1234);
    cg_test_int("读小端 12 34 = 0x3412", cg_bytes_get_u16(be, 2, 0, 0), 0x3412);
    cg_test_int("读小端 34 12 = 0x1234", cg_bytes_get_u16(le, 2, 0, 0), 0x1234);

    cg_test_note("---- 16 位：写进去再读回来（大小端各自往返） ----");
    memset(frame, 0xEE, sizeof(frame));
    cg_test_int("大端写 0x1234 到 offset 1 成功", cg_bytes_put_u16(frame, 8, 1, 0x1234u, 1), 1);
    cg_test_int("  字节图案是 12 34", frame[1] == 0x12 && frame[2] == 0x34, 1);
    cg_test_int("  读回来还是 0x1234", cg_bytes_get_u16(frame, 8, 1, 1), 0x1234);
    cg_test_int("小端写 0x1234 到 offset 1 成功", cg_bytes_put_u16(frame, 8, 1, 0x1234u, 0), 1);
    cg_test_int("  字节图案是 34 12", frame[1] == 0x34 && frame[2] == 0x12, 1);
    cg_test_int("  读回来还是 0x1234", cg_bytes_get_u16(frame, 8, 1, 0), 0x1234);
    cg_test_int("0xFFFF 往返", (cg_bytes_put_u16(frame, 8, 0, 0xFFFFu, 1) &&
                                cg_bytes_get_u16(frame, 8, 0, 1) == 0xFFFF), 1);
    cg_test_int("0 往返", (cg_bytes_put_u16(frame, 8, 0, 0u, 0) &&
                           cg_bytes_get_u16(frame, 8, 0, 0) == 0), 1);

    cg_test_note("---- 16 位：越界要**什么都不写** ----");
    memset(frame, 0xEE, sizeof(frame));
    cg_test_int("写到最后只剩 1 字节：失败", cg_bytes_put_u16(frame, 8, 7, 0x1234u, 1), 0);
    cg_test_int("  且那 1 字节没被动过（哨兵）", frame[7] == 0xEE, 1);
    cg_test_int("offset 负数：失败", cg_bytes_put_u16(frame, 8, -1, 1u, 1), 0);
    cg_test_int("out 传 NULL：失败", cg_bytes_put_u16(NULL, 8, 0, 1u, 1), 0);
    cg_test_int("读到只剩 1 字节：-1", cg_bytes_get_u16(be, 1, 0, 1), -1);
    cg_test_int("读越界 offset：-1", cg_bytes_get_u16(be, 2, 1, 1), -1);
    cg_test_int("读 data 传 NULL：-1", cg_bytes_get_u16(NULL, 2, 0, 1), -1);

    cg_test_note("---- 32 位：大小端 + 高位为 1 的值（这就是不能用 int 返回的原因） ----");
    cg_test_int("读大端 DE AD BE EF = 0xDEADBEEF", (long long)cg_bytes_get_u32(be4, 4, 0, 1), 0xDEADBEEFu);
    cg_test_int("读小端 EF BE AD DE = 0xDEADBEEF", (long long)cg_bytes_get_u32(le4, 4, 0, 0), 0xDEADBEEFu);
    cg_test_int("0xFFFFFFFF 读回来是**正数** 4294967295（不是 -1）",
                (long long)cg_bytes_get_u32("\xFF\xFF\xFF\xFF", 4, 0, 1), 4294967295LL);
    cg_test_int("越界仍返回 -1（与上面的合法值不撞车）", (long long)cg_bytes_get_u32(be4, 3, 0, 1), -1);

    memset(frame, 0xEE, sizeof(frame));
    cg_test_int("大端写 0xDEADBEEF 到 offset 2", cg_bytes_put_u32(frame, 8, 2, 0xDEADBEEFu, 1), 1);
    cg_test_int("  DE AD BE EF", frame[2] == 0xDE && frame[3] == 0xAD && frame[4] == 0xBE && frame[5] == 0xEF, 1);
    cg_test_int("小端写同一个值", cg_bytes_put_u32(frame, 8, 2, 0xDEADBEEFu, 0), 1);
    cg_test_int("  EF BE AD DE", frame[2] == 0xEF && frame[3] == 0xBE && frame[4] == 0xAD && frame[5] == 0xDE, 1);
    cg_test_int("写到最后剩 3 字节：失败", cg_bytes_put_u32(frame, 8, 5, 1u, 1), 0);
    cg_test_int("  且没被动过（哨兵）", frame[5] == 0xDE, 1);
}

/**
 * @brief 合起来用一次：拼一帧、再拆回来（工控的日常）
 * @return void 无返回值
 */
static void test_bytes_frame(void)
{
    unsigned char frame[16];
    int           n;
    long long     reg;

    cg_test_note("---- 合起来用一次：拼一帧、再拆回来（工控的日常） ----");
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x01;                                   /* 从站地址 */
    frame[1] = 0x03;                                   /* 功能码 */
    cg_test_int("往 offset 2 写寄存器地址 0x0000（大端）", cg_bytes_put_u16(frame, 16, 2, 0x0000u, 1), 1);
    cg_test_int("往 offset 4 写数量 0x0002（大端）", cg_bytes_put_u16(frame, 16, 4, 0x0002u, 1), 1);
    n = 6;

    cg_test_int("拼好的帧：01 03 00 00 00 02",
                (frame[0] == 0x01 && frame[1] == 0x03 && frame[2] == 0x00 &&
                 frame[3] == 0x00 && frame[4] == 0x00 && frame[5] == 0x02), 1);
    cg_test_int("在帧里找 00 00 的位置 = 2", cg_bytes_find(frame, n, "\x00\x00", 2, 0), 2);
    reg = cg_bytes_get_u32(frame, n, 0, 1);            /* 前四字节当一个大端数读 */
    cg_test_int("开头四字节按大端 = 0x01030000", reg, 0x01030000LL);
    cg_test_int("反转一份副本（发小端设备用）",
                (memcpy(frame + 8, frame, 6), cg_bytes_reverse(frame + 8, 6), frame[8] == 0x02), 1);
    cg_test_int("  反转后尾字节 02 跑到头上了", frame[8] == 0x02 && frame[13] == 0x01, 1);
    cg_test_int("切片：取功能码之外的后四字节（返回 4）",
                cg_bytes_slice(frame, 6, 2, -1, frame + 8, 4), 4);
    cg_test_int("  第一字节是 00", frame[8] == 0x00, 1);
}

CG_TEST_SUITE("kit：字节反转", test_bytes_reverse);
CG_TEST_SUITE("kit：字节切片", test_bytes_slice);
CG_TEST_SUITE("kit：字节查找", test_bytes_find);
CG_TEST_SUITE("kit：按字节序读写 u16 / u32", test_bytes_u16u32);
CG_TEST_SUITE("kit：拼一帧再拆回来（综合）", test_bytes_frame);
