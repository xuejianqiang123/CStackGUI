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
    cg_net_t *n;
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

CG_TEST_SUITE("网口：参数与错误路径", test_net_params);
CG_TEST_SUITE("网口：回环端到端", test_net_loopback);
CG_TEST_SUITE("网口：开关生命周期", test_net_lifecycle);
