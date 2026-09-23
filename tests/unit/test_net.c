/**
 * @file    test_net.c
 * @brief   网口（cg_net_*）的单元测试
 * @details 网口比串口好验得多的地方在这里体现出来了：**回环可以 100% 自验** ——
 *          同一个进程里"监听 + 连接 + 接受 + 双向收发"全都能真跑通，不需要任何外部设备。
 *          所以这个文件里的端到端断言（第 2 组）是真·协议级验证，不是"代码看起来对"。
 *
 *          验不到的部分也如实说：真跨主机的路由/防火墙、半开连接探测（TCP keepalive）、
 *          大流量吞吐，都不在这里 —— 那些要两台机器。
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <windows.h>        /* Sleep：等异步的读线程把字节搬进来，测试里就得等 */
#include <stdio.h>
#include <string.h>

/** @brief 等接收缓冲里攒够 want 字节（最多等 ms 毫秒）；返回 1 = 等到了 */
static int wait_rx(cg_net_t *n, int want, int ms)
{
    int waited;

    for (waited = 0; waited < ms; waited += 5) {
        if (cg_net_available(n) >= want) return 1;
        Sleep(5);
    }
    return 0;
}

/** @brief 在 3 个候选端口里挑一个能监听的（避免和别的东西撞端口） */
static cg_net_t *listen_any(int *port_out)
{
    static const int ports[3] = { 47831, 47832, 47833 };
    int              i;

    for (i = 0; i < 3; i++) {
        cg_net_t *srv = cg_net_listen(ports[i], 4);

        if (srv) {
            *port_out = ports[i];
            return srv;
        }
    }
    return NULL;
}

/** @brief 参数校验与错误路径（不需要对端） */
static void test_net_params(void)
{
    char      buf[64];

    cg_test_note("---- 端口/主机名不合法：干净失败 + 给得出原因 ----");
    cg_test_ok("端口 0 监不起来", cg_net_listen(0, 4) == NULL);
    cg_test_ok("端口 70000 监不起来", cg_net_listen(70000, 4) == NULL);
    cg_test_ok("失败原因不是空的", cg_net_last_error(NULL)[0] != '\0');
    printf("        监听失败的原因：%s\n", cg_net_last_error(NULL));

    cg_test_ok("连不存在的端口返回 NULL", cg_net_connect("127.0.0.1", 1, 500) == NULL);
    printf("        连接失败的原因：%s\n", cg_net_last_error(NULL));
    cg_test_ok("超时/拒绝也有可读原因", cg_net_last_error(NULL)[0] != '\0');

    cg_test_ok("主机名解析不了时返回 NULL",
               cg_net_connect("no-such-host.cstackgui.invalid", 80, 500) == NULL);
    printf("        解析失败的原因：%s\n", cg_net_last_error(NULL));

    cg_test_note("---- NULL 句柄上的所有 API ----");
    cg_test_ok("accept(NULL) 返回 NULL", cg_net_accept(NULL, 0) == NULL);
    cg_test_int("write 返回 -1", cg_net_write(NULL, "x", 1), -1);
    cg_test_int("read 返回 0", cg_net_read(NULL, buf, (int)sizeof(buf)), 0);
    cg_test_int("available 返回 0", cg_net_available(NULL), 0);
    cg_test_int("connected 返回 0", cg_net_connected(NULL), 0);
    cg_test_ok("rx/tx 都是 0", cg_net_rx_bytes(NULL) == 0 && cg_net_tx_bytes(NULL) == 0);
    cg_test_str("peer 是空串", cg_net_peer(NULL), "");
    cg_net_close(NULL);
    cg_test_ok("都不会崩", 1);

    cg_test_note("---- 对非监听句柄 accept：直接 NULL，不崩 ----");
    {
        int       port = 0;
        cg_net_t *srv = listen_any(&port);

        if (srv) {
            cg_net_t *c = cg_net_connect("127.0.0.1", port, 1000);

            cg_test_ok("连上监听端口", c != NULL);
            if (c) {
                cg_test_ok("对一条已连接的句柄再 accept 返回 NULL（它不是在监听）",
                           cg_net_accept(c, 0) == NULL);
                cg_test_ok("监听句柄自己 connected 是 0", cg_net_connected(srv) == 0);
                cg_net_close(c);
            }
            cg_net_close(srv);
        } else {
            cg_test_ok("本机挑不到测试端口（环境问题，跳过）", 1);
        }
    }
}

/** @brief 回环端到端：监听 -> 连接 -> 接受 -> 双向收发 -> 掉线信号 */
static void test_net_loopback(void)
{
    int       port = 0;
    cg_net_t *srv, *cli, *srv_side;
    char      rx[128];
    const char *hello = "HELLO-NET-0123456789";
    const char *reply = "WORLD";

    cg_test_note("---- 回环端到端（这是网口能 100% 自验的地方） ----");
    srv = listen_any(&port);
    cg_test_ok("监听成功", srv != NULL);
    if (!srv) return;

    cli = cg_net_connect("127.0.0.1", port, 2000);
    cg_test_ok("客户端连上", cli != NULL);
    if (!cli) { cg_net_close(srv); return; }

    srv_side = cg_net_accept(srv, 2000);
    cg_test_ok("服务端收到了连接", srv_side != NULL);
    if (!srv_side) { cg_net_close(cli); cg_net_close(srv); return; }

    cg_test_ok("两边都认为连着",
               cg_net_connected(cli) == 1 && cg_net_connected(srv_side) == 1);
    cg_test_ok("对端地址可读（服务端看到 127.0.0.1:某端口）",
               strncmp(cg_net_peer(srv_side), "127.0.0.1:", 10) == 0);
    printf("        服务端看到的对端：%s\n", cg_net_peer(srv_side));

    cg_test_note("---- 客户端 -> 服务端 ----");
    cg_test_int("客户端发出 22 字节", cg_net_write(cli, hello, (int)strlen(hello)),
                (int)strlen(hello));
    cg_test_int("等到了数据", wait_rx(srv_side, (int)strlen(hello), 2000), 1);
    memset(rx, 0, sizeof(rx));
    cg_test_int("服务端读出 22 字节", cg_net_read(srv_side, rx, sizeof(rx) - 1),
                (int)strlen(hello));
    cg_test_str("内容一个字节不差", rx, hello);
    cg_test_int("服务端累计收了 22", (int)cg_net_rx_bytes(srv_side), (int)strlen(hello));
    cg_test_int("取完就空了", cg_net_available(srv_side), 0);

    cg_test_note("---- 服务端 -> 客户端（反向也要通） ----");
    cg_test_int("服务端回 5 字节", cg_net_write(srv_side, reply, (int)strlen(reply)), 5);
    cg_test_int("客户端等到了", wait_rx(cli, 5, 2000), 1);
    memset(rx, 0, sizeof(rx));
    cg_net_read(cli, rx, sizeof(rx) - 1);
    cg_test_str("客户端收到 WORLD", rx, reply);
    cg_test_int("客户端累计发了 22", (int)cg_net_tx_bytes(cli), (int)strlen(hello));

    cg_test_note("---- 掉线信号：客户端一关，服务端 connected 变 0 ----");
    cg_net_close(cli);
    {
        int waited, down = 0;

        for (waited = 0; waited < 2000; waited += 20) {
            if (!cg_net_connected(srv_side)) { down = 1; break; }
            Sleep(20);
        }
        cg_test_int("服务端察觉到了对端关闭", down, 1);
    }

    cg_test_note("---- 没有连接时 accept 超时返回 NULL（不是错误） ----");
    cg_test_ok("空监听队列上 accept 返回 NULL", cg_net_accept(srv, 100) == NULL);
    cg_test_ok("这不算错误（last_error 没被写成错）", cg_net_last_error(srv)[0] == '\0');

    cg_net_close(srv_side);
    cg_net_close(srv);
    cg_test_ok("整条链路关干净了", 1);
}

/** @brief 反复建/关：读线程与套接字的生命周期（也顺带让 memcheck 看泄漏） */
static void test_net_lifecycle(void)
{
    int   port = 0;
    int   i;
    int   ok = 1;

    cg_test_note("---- 连开关 20 次（每次一对独立句柄） ----");
    for (i = 0; i < 20; i++) {
        cg_net_t *srv = cg_net_listen(47841 + i % 3, 4);
        cg_net_t *cli, *acc;

        if (!srv) { ok = 0; break; }
        cli = cg_net_connect("127.0.0.1", 47841 + i % 3, 1000);
        if (!cli) { cg_net_close(srv); ok = 0; break; }
        acc = cg_net_accept(srv, 1000);
        cg_net_write(cli, "x", 1);
        cg_net_close(acc);          /* NULL 安全：万一没接受成功也不崩 */
        cg_net_close(cli);
        cg_net_close(srv);
        port = 1;
    }
    cg_test_ok("20 轮开关没有崩、没有卡死", ok && port);
}

/** @brief UDP：开端口、参数校验、以及"流那套 API 在 UDP 上明确拒绝" */
static void test_udp_open(void)
{
    cg_net_t *u;
    char      buf[16];

    cg_test_note("---- UDP 开端口 ----");
    cg_test_ok("本地端口 -1 不合法", cg_net_udp_open(-1, 0) == NULL);
    cg_test_ok("本地端口 70000 不合法", cg_net_udp_open(70000, 0) == NULL);
    cg_test_ok("失败原因可读", cg_net_last_error(NULL)[0] != '\0');

    u = cg_net_udp_open(0, 0);
    cg_test_ok("本地端口传 0 = 让系统分一个", u != NULL);
    if (u) {
        cg_test_ok("peer 里带上了实际端口", strncmp(cg_net_peer(u), "UDP 本地 ", 9) == 0);
        printf("        系统分配：%s\n", cg_net_peer(u));

        cg_test_note("---- 流那套 API 在 UDP 句柄上要**明确拒绝**，不许悄悄失败 ----");
        cg_test_int("write 返回 -1（要发就用 sendto）", cg_net_write(u, "x", 1), -1);
        cg_test_int("read 返回 0", cg_net_read(u, buf, sizeof(buf)), 0);
        cg_test_int("available 返回 0", cg_net_available(u), 0);
        cg_test_int("connected 恒为 0（UDP 没有连接概念）", cg_net_connected(u), 0);
        cg_net_close(u);
    }

    u = cg_net_udp_open(0, 1);
    cg_test_ok("要求广播的端口也能开（SO_BROADCAST 设上了）", u != NULL);
    if (u) {
        cg_test_ok("peer 里标了可广播", strstr(cg_net_peer(u), "可广播") != NULL);
        cg_net_close(u);
    }
}

/** @brief UDP 回环：**报文边界与来源地址都得原样保住**（这套拉取式 API 存在的理由） */
static void test_udp_loopback(void)
{
    cg_net_t *a, *b;
    char      buf[64];
    char      ip[64];
    int       port = 0;
    int       n, waited;

    cg_test_note("---- 两个 UDP 端口在 127.0.0.1 上互发 ----");
    a = cg_net_udp_open(47861, 0);
    b = cg_net_udp_open(47862, 1);
    cg_test_ok("两个端口都开起来了（固定端口，撞了就换个号）", a != NULL && b != NULL);
    if (!a || !b) { cg_net_close(a); cg_net_close(b); return; }

    cg_test_int("b 发 3 字节给 a", cg_net_sendto(b, "127.0.0.1", 47861, "ABC", 3), 3);
    n = -1;
    for (waited = 0; waited < 2000 && n < 0; waited += 10) {
        n = cg_net_recvfrom(a, ip, (int)sizeof(ip), &port, buf, (int)sizeof(buf) - 1);
        if (n < 0) Sleep(10);
    }
    cg_test_int("a 收到 3 字节", n, 3);
    if (n > 0) buf[n] = '\0';
    cg_test_str("内容正确", buf, "ABC");
    cg_test_str("来源 IP 是回环地址", ip, "127.0.0.1");
    cg_test_int("来源端口就是 b 的端口 —— 这就是「来源地址没丢」", port, 47862);

    cg_test_note("---- 报文边界：连发两个数据报，必须收到两个独立报文，不能粘成一条流 ----");
    cg_net_sendto(b, "127.0.0.1", 47861, "ONE", 3);
    cg_net_sendto(b, "127.0.0.1", 47861, "TWO", 3);
    {
        char one[16];
        char two[16];
        int  got = 0;

        memset(one, 0, sizeof(one));
        memset(two, 0, sizeof(two));
        for (waited = 0; waited < 2000 && got < 2; waited += 10) {
            n = cg_net_recvfrom(a, NULL, 0, NULL, got == 0 ? one : two, 15);
            if (n >= 0) got++;
            else Sleep(10);
        }
        cg_test_int("收到两个独立数据报（不是一条 6 字节的流）", got, 2);
        cg_test_str("第一个是 ONE", one, "ONE");
        cg_test_str("第二个是 TWO", two, "TWO");
    }

    cg_test_note("---- 空数据报：返回 0（UDP 允许，是正常现象不是错误） ----");
    cg_test_int("空数据报发得出去", cg_net_sendto(b, "127.0.0.1", 47861, "", 0), 0);
    n = -1;
    for (waited = 0; waited < 2000 && n < 0; waited += 10) {
        char dummy[8];
        n = cg_net_recvfrom(a, NULL, 0, NULL, dummy, (int)sizeof(dummy));
        if (n < 0) Sleep(10);
    }
    cg_test_int("收到空数据报，返回值是 0", n, 0);
    cg_test_ok("这不是错误（last_error 是空的）", cg_net_last_error(a)[0] == '\0');

    cg_test_note("---- 没有数据时返回 -1，同样不是错误 ----");
    cg_test_int("立刻返回 -1", cg_net_recvfrom(a, NULL, 0, NULL, buf, (int)sizeof(buf)), -1);
    cg_test_ok("last_error 仍然干净", cg_net_last_error(a)[0] == '\0');

    cg_test_note("---- 累计字节与 NULL 兜底 ----");
    cg_test_int("a 累计收了 3+3+3+0 = 9", (int)cg_net_rx_bytes(a), 9);
    cg_test_int("b 累计发了 9", (int)cg_net_tx_bytes(b), 9);

    cg_test_note("---- 端口被占：要**响亮地失败**，不能悄悄共用 ----");
    /* 这条钉的是一个真缺陷的修复：`cg_net_udp_open` 原先无条件设 SO_REUSEADDR，而 Windows 上它的
     * 语义是"允许后来者抢同一个端口" → 第二个句柄也能开起来，但数据报只会进其中一个，
     * 表现是**不报错但收不到**。UDP 没有 TIME_WAIT，本来就不需要它，所以去掉了。 */
    {
        cg_net_t *u1 = cg_net_udp_open(47871, 0);
        cg_test_ok("第一个句柄开起来了", u1 != NULL);
        if (u1) {
            cg_net_t *u2 = cg_net_udp_open(47871, 0);

            cg_test_ok("同一端口再开一次必须失败（不是静默共用）", u2 == NULL);
            if (u2) cg_net_close(u2);
            cg_test_ok("失败原因可读", cg_net_last_error(NULL)[0] != '\0');
            printf("        端口被占的报错：%s\n", cg_net_last_error(NULL));
            cg_net_close(u1);
        }
    }

    cg_test_note("---- 数据报比接收缓冲区大：不能静默丢掉 ----");
    /* 这条本来是文档同事问我的问题（"单个数据报比 buf 大时是截断还是报错？"）。答案取决于
     * Win32 的 WSAEMSGSIZE 路径，而我们的实现把非 WSAEWOULDBLOCK 的错误都记进 last_error
     * 并返回 -1 —— 这里把它跑出来，并把**实际行为**打印出来供文档对齐（两种都不算静默丢数据）。 */
    {
        char big[2048];
        char small[16];

        memset(big, 'X', sizeof(big));
        cg_test_int("先发一个 2048 字节的数据报", 
                    cg_net_sendto(b, "127.0.0.1", 47861, big, (int)sizeof(big)), 2048);
        n = -1;
        for (waited = 0; waited < 2000; waited += 10) {
            n = cg_net_recvfrom(a, NULL, 0, NULL, small, (int)sizeof(small));
            if (n != -1) break;
            Sleep(10);
        }
        printf("        16 字节缓冲区收到 2048 字节的数据报时返回 %d，last_error：%s\n",
               n, cg_net_last_error(a));
        cg_test_ok("要么截断到 16 字节、要么失败并留了记录（都不是静默丢）",
                   n == 16 || (n == -1 && cg_net_last_error(a)[0] != '\0'));
    }
    cg_test_int("sendto(NULL) 返回 -1", cg_net_sendto(NULL, "127.0.0.1", 1, "x", 1), -1);
    cg_test_int("recvfrom(NULL) 返回 -1",
                cg_net_recvfrom(NULL, NULL, 0, NULL, buf, (int)sizeof(buf)), -1);
    cg_test_int("主机名解析不了时返回 -1",
                cg_net_sendto(b, "no-such-host.cstackgui.invalid", 1, "x", 1), -1);
    cg_test_ok("解析失败的原因可读", cg_net_last_error(b)[0] != '\0');
    cg_net_close(NULL);
    cg_test_ok("都不会崩", 1);

    cg_net_close(a);
    cg_net_close(b);
}

CG_TEST_SUITE("网口：参数与错误路径", test_net_params);
CG_TEST_SUITE("网口：回环端到端", test_net_loopback);
CG_TEST_SUITE("网口：开关生命周期", test_net_lifecycle);
CG_TEST_SUITE("网口 UDP：开端口与流 API 的拒绝", test_udp_open);
CG_TEST_SUITE("网口 UDP：回环与报文边界", test_udp_loopback);
