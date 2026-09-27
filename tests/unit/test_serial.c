/**
 * @file    test_serial.c
 * @brief   环形缓冲（cg_ringbuf）与串口（cg_serial）的单元测试
 * @details 这台机器上没有串口设备，所以"打开 COM3 收数据"这条路没法在这里走通 —— 但串口代码里
 *          真正容易错的地方**与设备无关**：环形缓冲的环绕/溢出语义、满了丢最老的那几条边界、
 *          以及"读线程 + 缓冲 + 取回"这条链的时序。所以这里用两个办法绕开设备：
 *            1. 环形缓冲是纯函数，直接锤边界；
 *            2. `cg_serial_from_handle()` 是给 cg_serial_open 用的内部工厂（CreateFile 之后
 *               才进来），测试拿一个**文件句柄**喂进去 —— 于是读线程、临界区、取回、
 *               关闭顺序全部走的是同一条真实代码路径，只是数据来源换成了磁盘文件。
 *          剩下的（真开端口、波特率、DCB）只能用真设备验，本文件里只验错误路径：
 *          打开一个不存在的设备必须**干净失败 + 给出可读原因**，不崩不卡。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 环形缓冲 */

/** @brief 建环/释放/计数，以及"空环取东西"这种边界 */
/**
 * @brief 建环 / 释放 / 计数，以及"空环取东西"这种边界
 * @return void 无返回值
 */
static void test_ring_basic(void)
{
    cg_ring_t r;
    char      out[16];

    cg_test_note("---- 环形缓冲：基本语义 ----");
    /* 这一行故意在一个**没初始化**的结构体上调用：init 必须不读旧内容（否则它会 free 一个
     * 野指针，症状是整套测试跑到一半进程以 STATUS_HEAP_CORRUPTION 退出，离现场很远）。 */
    cg_test_int("在未初始化的结构体上建环也不崩（容量不合法 -> 0）", cg_ring_init(&r, 0), 0);
    cg_test_int("建一个容量 4 的环", cg_ring_init(&r, 4), 1);
    cg_test_int("刚建好是空的", cg_ring_count(&r), 0);

    cg_test_int("写 3 字节", cg_ring_push(&r, "abc", 3), 3);
    cg_test_int("可读 3 字节", cg_ring_count(&r), 3);

    memset(out, 0, sizeof(out));
    cg_test_int("取 2 字节", cg_ring_pop(&r, out, 2), 2);
    cg_test_str("取出来的顺序是先进先出", out, "ab");
    cg_test_int("还剩 1 字节", cg_ring_count(&r), 1);

    memset(out, 0, sizeof(out));
    cg_test_int("再取 8 字节（要的比有的多）", cg_ring_pop(&r, out, 8), 1);
    cg_test_str("只给得出那 1 字节", out, "c");
    cg_test_int("空了", cg_ring_count(&r), 0);
    cg_test_int("空环再取返回 0", cg_ring_pop(&r, out, 8), 0);

    cg_test_note("---- 空环与非法参数都不许崩 ----");
    cg_test_int("往空指针写返回 0", cg_ring_push(NULL, "x", 1), 0);
    cg_test_int("写 0 字节返回 0", cg_ring_push(&r, "x", 0), 0);
    cg_test_int("空指针取返回 0", cg_ring_pop(NULL, out, 4), 0);
    cg_test_int("空指针计数是 0", cg_ring_count(NULL), 0);
    cg_ring_clear(NULL);
    cg_ring_free(NULL);
    cg_test_ok("NULL 都不会崩", 1);

    cg_ring_free(&r);
    cg_test_int("释放后可读数是 0", cg_ring_count(&r), 0);
    cg_ring_free(&r);                       /* 幂等 */
    cg_test_ok("重复释放也没事", 1);
}

/** @brief 环绕：反复写取越过多圈，数据不能错位 */
/**
 * @brief 环绕：反复写取越过多圈，数据不能错位
 * @return void 无返回值
 */
static void test_ring_wrap(void)
{
    cg_ring_t r;
    char      out[8];
    char      expect[8];
    int       round;

    cg_test_note("---- 环形缓冲：绕圈（这是最典型的实现错法） ----");
    if (!cg_ring_init(&r, 8)) { cg_test_ok("建环成功", 0); return; }

    for (round = 0; round < 5; round++) {
        char in[5];
        int  i;

        /* 每轮写 5 字节、取 5 字节：head 会一步步绕过整个环 */
        for (i = 0; i < 5; i++) {
            in[i] = (char)('0' + ((round * 5 + i) % 10));
            expect[i] = in[i];
        }
        cg_ring_push(&r, in, 5);
        memset(out, 0, sizeof(out));
        cg_test_int("取回的字节数对得上", cg_ring_pop(&r, out, 8), 5);
        expect[5] = '\0';
        cg_test_str("绕到第几圈数据都不错位", out, expect);
    }
    cg_ring_free(&r);
}

/** @brief 满了丢最老的：这是有意选定的策略，得钉住 */
/**
 * @brief 满了丢最老的：这是有意选定的策略，得钉住
 * @return void 无返回值
 */
static void test_ring_overflow(void)
{
    cg_ring_t r;
    char      out[16];

    cg_test_note("---- 满了丢最老的（与终端控件的行数上限同一策略） ----");
    if (!cg_ring_init(&r, 4)) { cg_test_ok("建环成功", 0); return; }

    cg_ring_push(&r, "abcd", 4);
    cg_ring_push(&r, "ef", 2);                  /* 超了 2 个 */
    cg_test_int("容量不变，还是 4", cg_ring_count(&r), 4);
    memset(out, 0, sizeof(out));
    cg_ring_pop(&r, out, 8);
    cg_test_str("丢掉的是最老的 ab，留最新的 cdef", out, "cdef");

    cg_test_note("---- 一次写进来就比容量还大：只留最后 cap 个字节 ----");
    cg_ring_push(&r, "1234567890", 10);
    cg_test_int("还是 4", cg_ring_count(&r), 4);
    memset(out, 0, sizeof(out));
    cg_ring_pop(&r, out, 8);
    cg_test_str("留的是尾巴 7890", out, "7890");

    cg_test_note("---- 清空：不丢容量与丢弃计数 ----");
    cg_ring_push(&r, "ab", 2);
    cg_ring_clear(&r);
    cg_test_int("清空后可读数是 0", cg_ring_count(&r), 0);
    cg_ring_push(&r, "xy", 2);
    memset(out, 0, sizeof(out));
    cg_test_int("清空后还能继续用", cg_ring_pop(&r, out, 4), 2);
    cg_test_str("内容对", out, "xy");
    cg_test_ok("丢过的字节数被记着（排错用）", r.dropped > 0);

    cg_ring_free(&r);
}

/* ------------------------------------------------------------ 串口 */

/** @brief 参数校验与错误信息：没有句柄的那种失败 */
/**
 * @brief 参数校验与错误信息：没有句柄的那种失败
 * @return void 无返回值
 */
static void test_serial_params(void)
{
    cg_test_note("---- 参数不合法：干净失败 + 给得出原因 ----");
    cg_test_ok("端口名是 NULL 时打不开", cg_serial_open(NULL, 115200) == NULL);
    cg_test_ok("失败原因不是空的", cg_serial_last_error(NULL)[0] != '\0');
    cg_test_ok("端口名是空串时打不开", cg_serial_open("", 115200) == NULL);
    cg_test_ok("波特率是 0 时打不开", cg_serial_open("COM1", 0) == NULL);
    cg_test_ok("波特率是负数时打不开", cg_serial_open("COM1", -9600) == NULL);

    cg_test_note("---- 全参数版（open_ex）：不合法的参数不许往驱动送 ----");
    cg_test_ok("数据位 4 不行",   cg_serial_open_ex("COM1", 9600, 4, 1, 0) == NULL);
    cg_test_ok("数据位 9 不行",   cg_serial_open_ex("COM1", 9600, 9, 1, 0) == NULL);
    cg_test_ok("停止位 0 不行",   cg_serial_open_ex("COM1", 9600, 8, 0, 0) == NULL);
    cg_test_ok("停止位 3 不行",   cg_serial_open_ex("COM1", 9600, 8, 3, 0) == NULL);
    cg_test_ok("校验 3 不行",     cg_serial_open_ex("COM1", 9600, 8, 1, 3) == NULL);
    cg_test_ok("校验 -1 不行",    cg_serial_open_ex("COM1", 9600, 8, 1, -1) == NULL);
    cg_test_str("每个失败都说得出人话原因（最后一个：校验位）",
                cg_serial_last_error(NULL), "校验位不合法（-1，0=无 1=奇 2=偶）");
    /* 合法参数组合 + 不存在的设备：走真正的打开路径，验的是"到驱动为止都干净" */
    cg_test_ok("参数全对但设备不存在：也干净失败",
               cg_serial_open_ex("\\\\.\\CStackGUI_NoSuchDevice", 115200, 7, 2, 2) == NULL);
    cg_test_ok("这回的原因是找不到设备", strstr(cg_serial_last_error(NULL), "找不到") != NULL);

    cg_test_note("---- 打开一个不存在的设备：必须干净失败，不许崩、不许卡 ----");
    cg_test_ok("不存在的设备名打不开",
               cg_serial_open("\\\\.\\CStackGUI_NoSuchDevice", 9600) == NULL);
    cg_test_ok("这次失败也有可读原因", cg_serial_last_error(NULL)[0] != '\0');
    printf("        实际报出来的原因是：%s\n", cg_serial_last_error(NULL));

    cg_test_note("---- NULL 句柄上的所有 API ----");
    cg_test_int("write 返回 -1", cg_serial_write(NULL, "x", 1), -1);
    cg_test_int("read 返回 0", cg_serial_read(NULL, (void *)"x", 1), 0);
    cg_test_int("available 返回 0", cg_serial_available(NULL), 0);
    cg_test_ok("rx_bytes 是 0", cg_serial_rx_bytes(NULL) == 0);
    cg_test_ok("tx_bytes 是 0", cg_serial_tx_bytes(NULL) == 0);
    cg_serial_flush_rx(NULL);
    cg_serial_close(NULL);                      /* 幂等、NULL 安全：没连上时也要敢调 */
    cg_test_ok("NULL 都不会崩", 1);

    cg_test_note("---- 端口枚举：没有串口也不能崩、不能留半行 ----");
    {
        char buf[256];
        int  n;

        memset(buf, 'X', sizeof(buf));          /* 先弄脏，验证它会写结尾 0 */
        n = cg_serial_list(buf, sizeof(buf));
        cg_test_ok("返回值非负", n >= 0);
        cg_test_ok("写出来的是以 0 结尾的串", n < (int)sizeof(buf) && buf[n] == '\0');
        printf("        本机枚举到 %d 字节的端口名：%s", n, n ? buf : "（一个都没有）\n");
        cg_test_int("容量给 1 时直接返回 0", cg_serial_list(buf, 1), 0);
        cg_test_int("buf 是 NULL 时返回 0", cg_serial_list(NULL, 64), 0);
    }

    cg_test_ok("以上失败路径全部没崩、没卡", 1);
}

/** @brief 用**文件句柄**走一遍真实的读线程 + 环形缓冲 + 取回 */
/**
 * @brief 用**文件句柄**走一遍真实的读线程 + 环形缓冲 + 取回
 * @return void 无返回值
 */
static void test_serial_data_path(void)
{
    wchar_t  tmppath[MAX_PATH];
    wchar_t  tmpfile[MAX_PATH];
    HANDLE   h;
    DWORD    wrote = 0;
    cg_serial_t *s;
    char     out[64];
    int      n, waited;
    const char *payload = "HELLO-SERIAL-0123456789";

    cg_test_note("---- 数据路径：拿文件句柄当数据源，验读线程 + 缓冲 + 取回 ----");
    if (!GetTempPathW(MAX_PATH, tmppath)) { cg_test_ok("拿到临时目录", 0); return; }
    if (!GetTempFileNameW(tmppath, L"cgs", 0, tmpfile)) { cg_test_ok("建临时文件", 0); return; }

    h = CreateFileW(tmpfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { cg_test_ok("打开临时文件写", 0); return; }
    WriteFile(h, payload, (DWORD)strlen(payload), &wrote, NULL);
    CloseHandle(h);
    cg_test_int("先把测试数据写进临时文件", (int)wrote, (int)strlen(payload));

    /* 只读打开，交给串口的内部工厂 —— 它会起真正的读线程 */
    h = CreateFileW(tmpfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { cg_test_ok("打开临时文件读", 0); return; }

    s = cg_serial_from_handle(h, "临时文件", 1024);
    cg_test_ok("用文件句柄建起了链路（读了线程起来了）", s != NULL);
    if (!s) { CloseHandle(h); DeleteFileW(tmpfile); return; }

    /* 读线程是异步的：等它把字节搬进来（等够 2 秒就算失败，不无限等） */
    for (waited = 0; waited < 400; waited++) {
        if (cg_serial_available(s) >= (int)strlen(payload)) break;
        Sleep(5);
    }
    cg_test_int("接收缓冲里攒齐了全部字节", cg_serial_available(s), (int)strlen(payload));
    cg_test_ok("累计收字节数也记上了",
               cg_serial_rx_bytes(s) == (long long)strlen(payload));

    memset(out, 0, sizeof(out));
    n = cg_serial_read(s, out, sizeof(out) - 1);
    cg_test_int("取回来的字节数对", n, (int)strlen(payload));
    cg_test_str("取回来的内容一个字节不差", out, payload);
    cg_test_int("取完就空了", cg_serial_available(s), 0);
    cg_test_int("空了以后再取返回 0", cg_serial_read(s, out, sizeof(out)), 0);

    cg_test_note("---- 满了丢最老：真实 I/O 路径上再验一次（容量 16 喂 23 字节） ----");
    {
        cg_serial_t *small;
        HANDLE       h2 = CreateFileW(tmpfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

        small = cg_serial_from_handle(h2, "临时文件", 16);
        cg_test_ok("建起容量 16 的小链路", small != NULL);
        if (small) {
            for (waited = 0; waited < 400; waited++) {
                if (cg_serial_rx_bytes(small) >= (long long)strlen(payload)) break;
                Sleep(5);
            }
            cg_test_int("容量 16，所以只留得住 16 字节", cg_serial_available(small), 16);
            memset(out, 0, sizeof(out));
            cg_serial_read(small, out, 16);
            cg_test_str("留住的是最后 16 个字节", out, payload + strlen(payload) - 16);
            cg_test_ok("累计收字节数仍然是全部 23（丢的是缓冲里的，不是账）",
                       cg_serial_rx_bytes(small) == (long long)strlen(payload));
            cg_serial_flush_rx(small);
            cg_serial_close(small);
        }
    }

    cg_test_note("---- write 到只读句柄：失败要干净 ----");
    cg_test_int("往只读文件句柄写会失败（返回 -1）", cg_serial_write(s, "x", 1), -1);
    cg_test_ok("失败原因有话说", cg_serial_last_error(s)[0] != '\0');

    cg_serial_close(s);
    DeleteFileW(tmpfile);                       /* 自己建的临时文件，收尾删掉 */
    cg_test_ok("关掉之后没崩", 1);
}

/** @brief 反复建/关：读线程的生命周期（关了之后再关、连着开关多次都不许挂） */
/**
 * @brief 反复建 / 关：读线程的生命周期（关了之后再关、连着开关多次都不许挂）
 * @return void 无返回值
 */
static void test_serial_lifecycle(void)
{
    wchar_t      tmppath[MAX_PATH];
    wchar_t      tmpfile[MAX_PATH];
    HANDLE       h;
    DWORD        wrote = 0;
    int          i;
    int          ok = 1;

    cg_test_note("---- 建了关、关了再建：读线程生命周期不许留下野线程 ----");
    if (!GetTempPathW(MAX_PATH, tmppath)) return;
    if (!GetTempFileNameW(tmppath, L"cgs", 0, tmpfile)) return;
    h = CreateFileW(tmpfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, "tick", 4, &wrote, NULL);
    CloseHandle(h);

    /* 每次用一个**独立**的句柄建链路再关：验的是"停读线程 -> 关句柄 -> 拆缓冲"这个顺序
     * 有没有留下野线程。注意真实约定是**只能关一次**（关第二次是调用方的错）。 */
    cg_test_note("---- 连开关 30 次（每次一个独立句柄）----");
    for (i = 0; i < 30; i++) {
        HANDLE       hr = CreateFileW(tmpfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        cg_serial_t *s = cg_serial_from_handle(hr, "临时文件", 256);

        if (!s) { ok = 0; CloseHandle(hr); break; }
        Sleep(1);
        cg_serial_close(s);
    }
    cg_test_ok("30 次开关没有崩、没有卡死", ok);

    DeleteFileW(tmpfile);
}

CG_TEST_SUITE("环形缓冲：基本语义", test_ring_basic);
CG_TEST_SUITE("环形缓冲：绕圈", test_ring_wrap);
CG_TEST_SUITE("环形缓冲：溢出丢最老", test_ring_overflow);
CG_TEST_SUITE("串口：参数与错误路径", test_serial_params);
CG_TEST_SUITE("串口：数据路径（文件句柄）", test_serial_data_path);
CG_TEST_SUITE("串口：开关生命周期", test_serial_lifecycle);
