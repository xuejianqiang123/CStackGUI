/**
 * @file    cg_net.c
 * @brief   网口（cg_net_*）：TCP 连接 / 监听 / 接受 + 读线程 + 接收缓冲
 * @details 与串口（cg_serial.c）同一套形状，因为面对的是同一类问题：
 *          **网络 I/O 不能做在 UI 线程上**（等数据的每一毫秒都是消息循环被堵住的毫秒）。
 *          所以这里同样是"运行时起一个读线程把字节搬进环形缓冲，业务在时钟里拉"：
 *
 *          客户端：cg_net_connect() -> 时钟里 cg_net_read() -> cg_term_write()
 *          服务端：cg_net_listen() -> 时钟里 cg_net_accept() 收连接（每个连接一个句柄）
 *
 *          **一个句柄 = 一条连接**。服务端要同时管多个客户端，就自己拿一个句柄数组 ——
 *          本层不引入"客户端列表"这个概念（那是应用的事，也让 API 面保持一条线）。
 *
 *          v1 的边界（有意不做，别猜）：**只有 TCP**（UDP 的 sendto/recvfrom 下一轮）；
 *          不做 TLS；不做自动重连；写是同步的（带超时，不是写队列）；接收缓冲满了丢最老的
 *          （与串口同一个策略）。要动这些改的是本文件，不是界面。
 *
 *          链接：需要 winsock2（`-lws2_32`）—— 这是本项目第一项"必须加"的链接依赖，
 *          与串口当初为省 `-lsetupapi` 而砍参数相反：ws2_32.dll 是系统核心 DLL，代价可接受。
 */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"

#include <winsock2.h>       /* 必须在 windows.h 之前，否则会拉进老的 winsock.h */
#include <ws2tcpip.h>       /* getaddrinfo / inet_ntop */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 接收缓冲上限：满了丢最老的（与串口、终端控件同一策略） */
#define NET_RX_CAP       (64 * 1024)
/** 一次最多收多少 */
#define NET_CHUNK        4096
/** 收超时：没数据时每约 50ms 返回一次，于是读线程能及时看到"停"标志 */
#define NET_RECV_TIMEO      50
/** 发超时：写是同步的，给 1 秒 */
#define NET_SEND_TIMEO      1000

struct cg_net_t {
    SOCKET           s;
    HANDLE           ev_stop;
    HANDLE           th;
    CRITICAL_SECTION lock;          /* 保护 rx 与两个计数器、err、connected */
    cg_ring_t        rx;
    long long        rx_total;
    long long        tx_total;
    int              listening;     /* 1 = 监听套接字（不能读写） */
    int              udp;           /* 1 = UDP 句柄（只有 sendto/recvfrom，无读线程） */
    int              connected;     /* 对端还在 */
    char             peer[64];      /* "127.0.0.1:5000" 这种 */
    char             err[192];
};

/** 最近一次"没有句柄可挂"的失败原因（连接/监听失败时用；主线程读，不做同步） */
static char g_open_err[192];

/* ------------------------------------------------------------ 内部助手 */

/** @brief 全局 WSA 初始化（整进程一次；WSAStartup 自身是引用计数的，不必 Cleanup） */
static int net_wsa(void)
{
    static int done = 0;
    static int ok = 0;
    WSADATA    ws;

    if (done) return ok;
    done = 1;
    ok = (WSAStartup(MAKEWORD(2, 2), &ws) == 0);
    if (!ok) cg_log(CG_LOG_ERROR, "网络初始化失败（WSAStartup）");
    return ok;
}

/** @brief 本机 IPv4 地址（点分十进制）；拿不到返回 NULL。
 *  @details 放在本文件而不是 cg_kit_sys.c，是因为 winsock2.h **必须在 windows.h 之前**
 *          包含（见文件头），而 cg_kit_sys.c 走的是 cg_kit_internal.h → windows.h 那条路。
 *          与其为了一个函数给那个文件做头文件手术，不如放在本来就用 winsock 的地方。
 *
 *          拿法：开一个 UDP socket、connect 到一个公网地址（**不发任何包**，只是让系统
 *          挑一条默认路由），再 getsockname 问本地端地址。这样**不需要 DNS** ——
 *          gethostbyname(gethostname()) 那套在没解析器的离线工控机上是会失败的。
 */
char *cg_local_ip(void)
{
    SOCKET             s;
    struct sockaddr_in to, me;
    int                len = (int)sizeof(me);
    char               buf[64];

    if (!net_wsa()) return NULL;
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return NULL;

    memset(&to, 0, sizeof(to));
    to.sin_family      = AF_INET;
    to.sin_port        = htons(53);
    to.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(s, (struct sockaddr *)&to, (int)sizeof(to)) != 0 ||
        getsockname(s, (struct sockaddr *)&me, &len) != 0 ||
        !inet_ntop(AF_INET, &me.sin_addr, buf, (size_t)sizeof(buf))) {
        closesocket(s);
        return NULL;
    }
    closesocket(s);

    /* 完全离线时也可能挑到回环 —— 那不是"本机在网上的地址"，如实报失败 */
    if (strcmp(buf, "127.0.0.1") == 0) return NULL;
    return cg_str_format("%s", buf);
}

/** @brief 把最常见的 Winsock 错误码翻成人话 */
static const char *net_why(int e)
{
    switch (e) {
    case WSAECONNREFUSED:  return "对方拒绝连接（端口没人监听？）";
    case WSAETIMEDOUT:     return "连接超时（对方不在线或被防火墙拦了）";
    case WSAEHOSTUNREACH:
    case WSAENETUNREACH:   return "网络不可达（IP 对吗？同一网段吗？）";
    case WSAEADDRINUSE:    return "端口已被占用（上一次没退干净，或别的程序占了）";
    case WSAEADDRNOTAVAIL: return "本机没有这个地址（IP 填错？）";
    case WSAECONNRESET:    return "连接被对方重置（对面断开了）";
    case WSAENOTCONN:      return "还没有连接";
    case WSAEACCES:        return "端口被拒绝（1024 以下要管理员？）";
    case WSAHOST_NOT_FOUND:
    case WSATRY_AGAIN:     return "主机名解析不了";
    case WSAEMSGSIZE:      return "数据报比接收缓冲区大（尾巴取不回来，换更大的 buf）";
    default:               return "网络错误";
    }
}

/** @brief 记一条错误（先在栈上拼好整条，再一次性拷进去：主线程随手读也不会读到半个串） */
static void net_fail(cg_net_t *n, const char *what, int code)
{
    char msg[192];

    snprintf(msg, sizeof(msg), "%s：%s（%d）", what, net_why(code), code);
    if (n) {
        EnterCriticalSection(&n->lock);
        memcpy(n->err, msg, strlen(msg) + 1);
        LeaveCriticalSection(&n->lock);
        cg_log(CG_LOG_ERROR, "网络 %s %s", n->peer, msg);
    } else {
        memcpy(g_open_err, msg, strlen(msg) + 1);
        cg_log(CG_LOG_ERROR, "网络 %s", msg);
    }
}

/** @brief 填 "ip:port" 到 n->peer（失败就写 "?"） */
static void net_set_peer(cg_net_t *n, const struct sockaddr *sa)
{
    char ip[64] = "?";
    int  port = 0;

    if (sa && sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, (const void *)&in->sin_addr, ip, sizeof(ip));
        port = ntohs(in->sin_port);
    }
    snprintf(n->peer, sizeof(n->peer), "%s:%d", ip, port);
}

/* ------------------------------------------------------------ 读线程 */

/** @brief 读线程：把 recv 到的字节搬进接收缓冲。**只做这一件事** —— 不碰控件、不碰界面。
 *  对端关闭（recv 返回 0）时把 connected 清掉：这是业务判断"掉线了"的唯一信号。 */
static DWORD WINAPI net_thread(LPVOID p)
{
    cg_net_t *n = (cg_net_t *)p;
    char      chunk[NET_CHUNK];

    for (;;) {
        int got;

        if (WaitForSingleObject(n->ev_stop, 0) == WAIT_OBJECT_0) break;

        got = recv(n->s, chunk, NET_CHUNK, 0);
        if (got > 0) {
            EnterCriticalSection(&n->lock);
            cg_ring_push(&n->rx, chunk, got);
            n->rx_total += got;
            LeaveCriticalSection(&n->lock);
            continue;
        }
        if (got == 0) {                     /* 对端正常关闭 */
            EnterCriticalSection(&n->lock);
            n->connected = 0;
            LeaveCriticalSection(&n->lock);
            cg_log(CG_LOG_INFO, "网络 %s：对端关闭了连接", n->peer);
            break;
        }
        if (WSAGetLastError() == WSAETIMEDOUT) continue;    /* 没数据，转一圈看要不要停 */
        if (WaitForSingleObject(n->ev_stop, 0) == WAIT_OBJECT_0) break;
        /* 真出错（连接被重置、被关等）：记一笔就退出，别拿着 CPU 空转刷日志 */
        {
            int e = WSAGetLastError();
            EnterCriticalSection(&n->lock);
            n->connected = 0;
            LeaveCriticalSection(&n->lock);
            if (e != WSAENOTSOCK && e != WSAEINTR) net_fail(n, "接收失败", e);
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ 建立与释放 */

/** @brief 内部：拿一个已经连上/接受到的套接字建链路并起读线程 */
static cg_net_t *net_from_socket(SOCKET s, const struct sockaddr *peer, int rx_cap)
{
    cg_net_t *n;
    int       tmo;

    if (s == INVALID_SOCKET) return NULL;

    n = (cg_net_t *)calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->s = s;
    n->connected = 1;
    if (rx_cap <= 0) rx_cap = NET_RX_CAP;
    net_set_peer(n, peer);

    tmo = NET_RECV_TIMEO;
    setsockopt(n->s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
    tmo = NET_SEND_TIMEO;
    setsockopt(n->s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof(tmo));

    if (!cg_ring_init(&n->rx, rx_cap)) { free(n); return NULL; }
    InitializeCriticalSection(&n->lock);
    n->ev_stop = CreateEventW(NULL, TRUE, FALSE, NULL);     /* 手动重置：停下来就一直停 */
    if (!n->ev_stop) {
        DeleteCriticalSection(&n->lock);
        cg_ring_free(&n->rx);
        free(n);
        return NULL;
    }
    n->th = CreateThread(NULL, 0, net_thread, n, 0, NULL);
    if (!n->th) {
        CloseHandle(n->ev_stop);
        DeleteCriticalSection(&n->lock);
        cg_ring_free(&n->rx);
        free(n);
        return NULL;
    }
    return n;
}

/** @brief 新建一个链路壳（只用于监听/未连接状态：它没有读线程） */
static cg_net_t *net_alloc_plain(SOCKET s, int listening)
{
    cg_net_t *n = (cg_net_t *)calloc(1, sizeof(*n));

    if (!n) return NULL;
    n->s = s;
    n->listening = listening;
    snprintf(n->peer, sizeof(n->peer), "%s", listening ? "（监听中）" : "?");
    InitializeCriticalSection(&n->lock);
    return n;
}

/** @brief 连到一台主机（host 传 NULL 或空串 = 127.0.0.1；timeout_ms <= 0 = 3000ms） */
cg_net_t *cg_net_connect(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char             portstr[16];
    SOCKET           s = INVALID_SOCKET;
    cg_net_t        *n;
    int              rc;

    if (!net_wsa()) { snprintf(g_open_err, sizeof(g_open_err), "网络初始化失败"); return NULL; }
    if (port <= 0 || port > 65535) {
        snprintf(g_open_err, sizeof(g_open_err), "端口不合法（%d）", port);
        return NULL;
    }
    if (!host || !*host) host = "127.0.0.1";            /* 回环：本机自测最常用 */
    if (timeout_ms <= 0) timeout_ms = 3000;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0 || !res) {
        snprintf(g_open_err, sizeof(g_open_err), "主机名解析不了：%s（%d）", host, rc);
        cg_log(CG_LOG_ERROR, "网络连接 %s 失败：主机名解析不了", host);
        return NULL;
    }

    /* 用**非阻塞 connect + select** 带超时：直接阻塞 connect 在坏地址上会把 UI 冻住十几秒，
     * 那是这类工具最不能接受的观感问题。 */
    {
        u_long nb = 1;
        fd_set wfds, efds;
        struct timeval tv;
        int    err = 0, elen = (int)sizeof(err);

        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET) {
            freeaddrinfo(res);
            snprintf(g_open_err, sizeof(g_open_err), "创建套接字失败");
            return NULL;
        }
        ioctlsocket(s, FIONBIO, &nb);

        rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
        if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            int e = WSAGetLastError();
            net_fail(NULL, "连接失败", e);
            closesocket(s);
            freeaddrinfo(res);
            return NULL;
        }
        if (rc == SOCKET_ERROR) {                       /* 正在连：等它变成可写 */
            FD_ZERO(&wfds);
            FD_SET(s, &wfds);
            efds = wfds;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            rc = select(0, NULL, &wfds, &efds, &tv);
            if (rc <= 0) {
                net_fail(NULL, "连接失败", WSAETIMEDOUT);   /* 别写"连接超时"：原因串里已经说了 */
                closesocket(s);
                freeaddrinfo(res);
                return NULL;
            }
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &elen);
            if (err != 0) {
                net_fail(NULL, "连接失败", err);
                closesocket(s);
                freeaddrinfo(res);
                return NULL;
            }
        }
        {
            u_long blk = 0;                             /* 回到阻塞模式：读线程靠 SO_RCVTIMEO 转圈 */
            ioctlsocket(s, FIONBIO, &blk);
        }
        n = net_from_socket(s, res->ai_addr, NET_RX_CAP);
    }
    freeaddrinfo(res);
    if (!n) {
        snprintf(g_open_err, sizeof(g_open_err), "建立读线程失败");
        closesocket(s);
        return NULL;
    }
    cg_log(CG_LOG_INFO, "已连接 %s", n->peer);
    return n;
}

/** @brief 监听一个端口（backlog <= 0 = 4） */
cg_net_t *cg_net_listen(int port, int backlog)
{
    SOCKET            s;
    struct sockaddr_in sa;
    cg_net_t         *n;
    BOOL              yes = TRUE;

    if (!net_wsa()) { snprintf(g_open_err, sizeof(g_open_err), "网络初始化失败"); return NULL; }
    if (port <= 0 || port > 65535) {
        snprintf(g_open_err, sizeof(g_open_err), "端口不合法（%d）", port);
        return NULL;
    }
    if (backlog <= 0) backlog = 4;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        snprintf(g_open_err, sizeof(g_open_err), "创建套接字失败");
        return NULL;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((u_short)port);
    if (bind(s, (const struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        net_fail(NULL, "绑定端口失败", WSAGetLastError());
        closesocket(s);
        return NULL;
    }
    if (listen(s, backlog) == SOCKET_ERROR) {
        net_fail(NULL, "监听失败", WSAGetLastError());
        closesocket(s);
        return NULL;
    }

    n = net_alloc_plain(s, 1);
    if (!n) { closesocket(s); return NULL; }
    cg_log(CG_LOG_INFO, "正在监听端口 %d", port);
    return n;
}

/** @brief 接受一个连接（timeout_ms <= 0 = 不等，立刻返回）；没有连接返回 NULL（**不是错误**） */
cg_net_t *cg_net_accept(cg_net_t *srv, int timeout_ms)
{
    struct sockaddr_in sa;
    int                slen = (int)sizeof(sa);
    SOCKET             c;
    cg_net_t          *n;
    fd_set             rfds;
    struct timeval     tv;

    if (!srv || !srv->listening) return NULL;

    FD_ZERO(&rfds);
    FD_SET(srv->s, &rfds);
    tv.tv_sec = (timeout_ms > 0 ? timeout_ms : 0) / 1000;
    tv.tv_usec = (timeout_ms > 0 ? timeout_ms : 0) % 1000 * 1000;
    if (select(0, &rfds, NULL, NULL, &tv) <= 0) return NULL;    /* 没人来：正常返回 NULL */

    memset(&sa, 0, sizeof(sa));
    c = accept(srv->s, (struct sockaddr *)&sa, &slen);
    if (c == INVALID_SOCKET) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) net_fail(srv, "接受连接失败", e);
        return NULL;
    }
    n = net_from_socket(c, (struct sockaddr *)&sa, NET_RX_CAP);
    if (!n) { closesocket(c); return NULL; }
    cg_log(CG_LOG_INFO, "接受了来自 %s 的连接", n->peer);
    return n;
}

/** @brief 关掉一条连接/监听（幂等、NULL 安全） */
void cg_net_close(cg_net_t *n)
{
    if (!n) return;

    /* 顺序同串口：**先请读线程停、等它退出，再关套接字、再拆缓冲** ——
     * 反过来就是"读线程往已释放的缓冲里写"，表现是偶发崩溃。 */
    if (n->th) {
        SetEvent(n->ev_stop);
        WaitForSingleObject(n->th, 3000);
        CloseHandle(n->th);
    }
    if (n->ev_stop) CloseHandle(n->ev_stop);
    if (n->s != INVALID_SOCKET) {
        shutdown(n->s, SD_BOTH);        /* 让对方立刻看到断开，而不是等 FIN 超时 */
        closesocket(n->s);
    }
    DeleteCriticalSection(&n->lock);
    cg_ring_free(&n->rx);
    free(n);
}

/* ------------------------------------------------------------ 收发 */

/** @brief 发送；返回实际写出字节数，< 0 失败 */
int cg_net_write(cg_net_t *n, const void *data, int len)
{
    int sent;

    /* UDP 句柄走 cg_net_sendto：它要目的地，而"流写"没有目的地这个概念 —— 明确拒绝 */
    if (!n || n->listening || n->udp || !data || len <= 0) return -1;
    sent = send(n->s, (const char *)data, len, 0);
    if (sent == SOCKET_ERROR) {
        net_fail(n, "发送失败", WSAGetLastError());
        return -1;
    }
    EnterCriticalSection(&n->lock);
    n->tx_total += sent;
    LeaveCriticalSection(&n->lock);
    return sent;
}

/** @brief 从接收缓冲取最多 cap 字节（非阻塞；对端已关闭时也还能把剩下的取完） */
int cg_net_read(cg_net_t *n, void *buf, int cap)
{
    int got;

    if (!n || n->udp || !buf || cap <= 0) return 0;
    EnterCriticalSection(&n->lock);
    got = cg_ring_pop(&n->rx, buf, cap);
    LeaveCriticalSection(&n->lock);
    return got;
}

/** @brief 接收缓冲里还有多少字节没取走 */
int cg_net_available(const cg_net_t *n)
{
    int got;

    if (!n || n->udp) return 0;
    EnterCriticalSection((CRITICAL_SECTION *)&n->lock);
    got = cg_ring_count(&n->rx);
    LeaveCriticalSection((CRITICAL_SECTION *)&n->lock);
    return got;
}

/** @brief 对端还在吗（对方关闭后变 0；监听句柄与 UDP 句柄恒为 0） */
int cg_net_connected(const cg_net_t *n)
{
    if (!n || n->udp) return 0;
    return n->connected ? 1 : 0;
}

/** @brief 累计收 / 发字节数 */
long long cg_net_rx_bytes(const cg_net_t *n) { return n ? n->rx_total : 0; }
long long cg_net_tx_bytes(const cg_net_t *n) { return n ? n->tx_total : 0; }

/** @brief 对端 "ip:port"（监听句柄返回"（监听中）"） */
const char *cg_net_peer(const cg_net_t *n)
{
    return n ? n->peer : "";
}

/** @brief 最后一次错误的中文描述；n 为 NULL 时给"最近一次连接/监听失败"的原因 */
const char *cg_net_last_error(const cg_net_t *n)
{
    if (!n) return g_open_err;
    return n->err;
}

/* ------------------------------------------------------------ UDP（数据报）
 * 与 TCP 那边刻意不同：**不开读线程**。数据报的报文边界与来源地址必须原样交给业务，
 * 而"读线程 + 字节环"是给字节流设计的 —— 它会把两个数据报粘成一条流、把"谁发的"丢掉。
 * 所以这里是拉取式：业务在时钟里 cg_net_recvfrom()，一次一个完整数据报。
 */

/** @brief 开一个 UDP 端口（local_port 传 0 = 让系统随便分一个；broadcast != 0 时允许广播） */
cg_net_t *cg_net_udp_open(int local_port, int broadcast)
{
    SOCKET             s;
    struct sockaddr_in sa;
    struct sockaddr_in me;
    int                mlen = (int)sizeof(me);
    cg_net_t          *n;
    BOOL               yes = TRUE;

    if (!net_wsa()) { snprintf(g_open_err, sizeof(g_open_err), "网络初始化失败"); return NULL; }
    if (local_port < 0 || local_port > 65535) {
        snprintf(g_open_err, sizeof(g_open_err), "本地端口不合法（%d）", local_port);
        return NULL;
    }

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        snprintf(g_open_err, sizeof(g_open_err), "创建 UDP 套接字失败");
        return NULL;
    }
    /* 这里**故意不设 SO_REUSEADDR**（TCP 监听那边设了，UDP 这边不设）：Windows 上它的语义是
     * "允许后来者抢同一个端口"，于是"端口被占"会退化成**不报错但收不到** —— 对调试工具来说，
     * 沉默的失败比响亮的失败贵得多。UDP 又没有 TIME_WAIT，本来也不需要靠它来快速重绑。 */
    if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((u_short)local_port);
    if (bind(s, (const struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        net_fail(NULL, "绑定 UDP 端口失败（被占用？）", WSAGetLastError());
        closesocket(s);
        return NULL;
    }

    n = net_alloc_plain(s, 0);
    if (!n) { closesocket(s); return NULL; }
    n->udp = 1;

    /* local_port 传 0 时只有绑完才知道系统分了哪个端口，记下来给日志/界面看 */
    if (getsockname(s, (struct sockaddr *)&me, &mlen) == 0) {
        snprintf(n->peer, sizeof(n->peer), "UDP 本地 %d%s",
                 (int)ntohs(me.sin_port), broadcast ? "（可广播）" : "");
    }
    cg_log(CG_LOG_INFO, "已开 UDP 端口：%s", n->peer);
    return n;
}

/** @brief 发一个数据报；返回实际发出字节数，< 0 失败 */
int cg_net_sendto(cg_net_t *n, const char *host, int port, const void *data, int len)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char             portstr[16];
    int              sent;

    if (!n || !n->udp || !data || len < 0) return -1;      /* 空数据报合法（len 可以为 0） */
    if (!host || !*host || port <= 0 || port > 65535) return -1;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        EnterCriticalSection(&n->lock);
        snprintf(n->err, sizeof(n->err), "主机名解析不了：%s", host);
        LeaveCriticalSection(&n->lock);
        return -1;
    }

    sent = sendto(n->s, (const char *)data, len, 0, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (sent == SOCKET_ERROR) {
        net_fail(n, "发送失败", WSAGetLastError());
        return -1;
    }
    EnterCriticalSection(&n->lock);
    n->tx_total += sent;
    LeaveCriticalSection(&n->lock);
    return sent;
}

/** @brief 收一个数据报（非阻塞）；> 0 字节数、= 0 空数据报、= -1 暂时没有（不是错误） */
int cg_net_recvfrom(cg_net_t *n, char *ip_out, int ip_cap, int *port_out, void *buf, int cap)
{
    struct sockaddr_in from;
    int                flen = (int)sizeof(from);
    int                got;
    fd_set             rfds;
    struct timeval     tv;

    if (!n || !n->udp || !buf || cap <= 0) return -1;

    /* 先问"有没有待收的数据报"（零超时 select），有才 recvfrom —— 这样它天然是非阻塞的，
     * 也不会因为把套接字设成非阻塞模式而影响 sendto */
    FD_ZERO(&rfds);
    FD_SET(n->s, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(0, &rfds, NULL, NULL, &tv) <= 0) return -1;      /* 没有数据：不是错误 */

    memset(&from, 0, sizeof(from));
    got = recvfrom(n->s, (char *)buf, cap, 0, (struct sockaddr *)&from, &flen);
    if (got == SOCKET_ERROR) {
        int e = WSAGetLastError();

        if (e == WSAEWOULDBLOCK) return -1;
        net_fail(n, "接收失败", e);
        return -1;
    }

    if (ip_out && ip_cap > 0) inet_ntop(AF_INET, (const void *)&from.sin_addr, ip_out, ip_cap);
    if (port_out) *port_out = (int)ntohs(from.sin_port);
    EnterCriticalSection(&n->lock);
    n->rx_total += got;
    LeaveCriticalSection(&n->lock);
    return got;
}
