/**
 * @file    main.c
 * @brief   网口调试助手示例：cg_net_* 的正确用法（客户端 / 服务端两种角色）
 * @details 这是"跟网络上的东西说话"那类工具的样板：界面全在 netterm.cgui 里，这里只有行为。
 *
 *          **线程模型是这个示例唯一想讲清楚的事，也是唯一容易出事的地方**（和串口示例同一套）：
 *            - cg_net_connect() / cg_net_accept() 内部各起一个读线程，它只干一件事 —— 把 socket
 *              上的字节搬进 64KB 环形缓冲（满了丢最老的），**它绝不碰控件**；
 *            - 控件（HWND）只能在建它的那个线程里碰，这是 Win32 的硬约束，不是本项目的风格偏好；
 *            - 所以数据要"按时来取"：cg_set_timer(50) 定周期 → 时钟回调里 cg_net_read()
 *              → cg_term_write() 灌进 terminal 控件 → 顺带刷状态栏与曲线。
 *
 *          cg_net 比 cg_serial 多的一件事是**一个句柄 = 一条连接**：
 *            - 客户端角色：cg_net_connect() 拿到的就是唯一那条连接，掉线看 cg_net_connected() 变 0；
 *            - 服务端角色：cg_net_listen() 拿到的只是"监听句柄"（它自己不收发数据），每条进来的
 *              连接都是 cg_net_accept() 新给的独立句柄。所以服务端要同时管多个客户端，就得
 *              自己拿一个数组（本示例 8 个），在时钟里逐个 read / 检查掉线。
 *            本层故意不做"客户端列表"这个概念，API 面保持一条线。
 *
 *          界面上用到的控件（都在 .cgui 里声明）：
 *            combo（角色）、edit（地址）、spinner（端口）、button ×4、led（链路灯）、
 *            terminal（收发日志）、splitter（日志区 / 发送区之间）、group（发送区）、
 *            edit + checkbox（发送内容 / HEX）、chart（每秒收到多少字节）、statusbar。
 *
 *          几个新手会踩的点，这里都写对了：
 *            1. cg_app_selftest 会**合成触发每个回调**，所以凡是要真连网的地方，先问一句
 *               cg_app_has_window()，自检模式下直接返回（否则 CI 会卡在真连接上）；
 *            2. cg_net_accept(srv, 0) 的 timeout=0 表示"不等、立刻返回"：**没有连接时返回 NULL
 *               是正常情况，不是错误** —— 正因如此它才适合放在时钟里轮询；
 *            3. 连接/监听失败时句柄是 NULL，原因要用 cg_net_last_error(NULL) 取（上一节示例里
 *               也是这个套路），写进日志视图；
 *            4. 掉线的**唯一信号**是 cg_net_connected() 变 0；对端关闭后缓冲里可能还有尾巴，
 *               所以先取干净再回收句柄（本文件 drain_conn + 先读后判的顺序）；
 *            5. 断开与退出都要把**每一个**句柄 cg_net_close() 掉，它幂等且 NULL 安全；
 *            6. 发送在 HEX 模式下按十六进制解析；解析是手写的 —— 不用 strtok（它会就地改原串），
 *               而且"1A2B"这种没分隔符的也得算对。
 *
 *          编译：在项目根跑 build-app.bat netterm
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** 轮询周期（毫秒）：50ms 足够跟手，也不会给界面添负担 */
#define POLL_MS        50
/** 多少拍算一秒（把"每秒收到多少字节"聚合成曲线上的一个点） */
#define TICKS_PER_SEC  (1000 / POLL_MS)
/** 一拍最多从一条连接取多少字节 */
#define RX_CHUNK       4096
/** 一拍最多从一条连接取几轮（防"数据一直比界面快"把这一拍拖久；剩下的下一拍再取） */
#define RX_MAX_CHUNKS  32
/** 服务端最多同时管几条连接（一个句柄 = 一条连接，超了就拒收） */
#define MAX_CLIENTS    8
/** 一拍最多收几个新连接（防"连接风暴"把这一拍拖久） */
#define ACCEPT_PER_TICK 4
/** 曲线 Y 轴的最小上限，也是初始上限（和 .cgui 里 chart 的 max 对齐） */
#define CHART_TOP_MIN  256

/** 角色：和 .cgui 里 cmb_role 的条目顺序一致（下拉框的 value 就是下标） */
enum { ROLE_CLIENT = 0, ROLE_SERVER = 1 };

/** 业务状态：监听句柄、客户端连接、服务端连接数组、轮询定时器、累计量、曲线聚合量 */
typedef struct {
    cg_net_t    *srv;                 /**< 监听句柄；NULL = 没在监听 */
    cg_net_t    *cli;                 /**< 客户端角色下那条连接；NULL = 没连 */
    cg_net_t    *peers[MAX_CLIENTS];  /**< 服务端收上来的连接（一个句柄 = 一条连接） */
    int          n_peer;              /**< peers 里有效句柄的个数 */
    int          role;                /**< ROLE_CLIENT / ROLE_SERVER */
    cg_timer_t  *poll;                /**< 轮询定时器（一次性的，"再挂一次"就是周期） */
    char         ini[1200];           /**< 配置文件路径（记住上次的地址 / 端口 / 角色 / 分隔条） */
    int          ticks;               /**< 距曲线上一个点过了几拍 */
    int          rx_window;           /**< 这一秒内收到的字节数 */
    int          chart_top;           /**< 曲线 Y 轴当前上限（只往上扩） */
    long long    rx_total;            /**< 累计收到字节（自己留一份，断开后还要显示） */
    long long    tx_total;            /**< 累计发出字节 */
} biz_t;

static biz_t s_biz;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 往日志终端补一条"程序自己记的"消息（灰色，行首 --）
 * @param app 所属应用
 * @param text 要写的中文（UTF-8）
 * @return void
 */
static void term_sys(cg_app_t *app, const char *text)
{
    cg_widget_t *term = cg_find(app, "term_log");

    if (term) cg_term_write(term, CG_TERM_SYS, text, -1);
}

/**
 * @brief 读「端口」微调框里的端口号
 * @param app 所属应用
 * @return int 端口号；控件不在返回 0
 */
static int port_of(cg_app_t *app)
{
    cg_widget_t *sp = cg_find(app, "spn_port");

    return sp ? cg_widget_value(sp) : 0;
}

/**
 * @brief 把 "1A 2B 2c" / "1A2B2C" 这种十六进制文本解析成字节
 * @param text 待解析文本（空格、逗号、连字符、换行都当分隔符）
 * @param out 输出字节缓冲
 * @param cap out 的容量（字节数）
 * @return int 解析出的字节数；遇到非法字符返回 -1；放不下返回 -2（落单半个字节也算 -1）
 * @note 不用 strtok：它会就地改原串（text 是控件内部的字符串，动不得），而且
 *       "1A2B"这种没写分隔符的也得算对，手写一遍反而更短更好懂。
 */
static int hex_parse(const char *text, unsigned char *out, int cap)
{
    int hi = -1;    /* 攒着的半个字节；-1 = 手里还没有 */
    int n  = 0;

    for (; text && *text; text++) {
        int  v;
        char c = *text;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '-') continue;
        if (c >= '0' && c <= '9')       v = c - '0';
        else if (c >= 'a' && c <= 'f')  v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')  v = c - 'A' + 10;
        else return -1;

        if (hi < 0) {
            hi = v;
        } else {
            if (n >= cap) return -2;
            out[n++] = (unsigned char)((hi << 4) | v);
            hi = -1;
        }
    }
    return (hi >= 0) ? -1 : n;   /* 只剩半个字节 = 写错了 */
}

/* ------------------------------------------------------------ 连接数组维护 */

/**
 * @brief 从服务端连接数组里删掉第 idx 个（后面的整体前移）
 * @param idx 要删的下标
 * @return void
 * @note 只搬指针，不关句柄 —— 关句柄是调用方的事，顺序（先打印对端再关）由调用方保证。
 */
static void peer_remove(int idx)
{
    int i;

    if (idx < 0 || idx >= s_biz.n_peer) return;
    for (i = idx; i + 1 < s_biz.n_peer; i++) s_biz.peers[i] = s_biz.peers[i + 1];
    s_biz.n_peer--;
    s_biz.peers[s_biz.n_peer] = NULL;
}

/** @brief 现在有没有一条能收发的链路（客户端连接、或服务端已收到连接） */
static int has_link(void)
{
    return (s_biz.cli != NULL) || (s_biz.n_peer > 0);
}

/* ------------------------------------------------------------ 界面刷新 */

/**
 * @brief 把链路状态刷到链路灯、状态栏（状态 / 对端 / RX / TX）
 * @param app 所属应用
 * @return void
 * @note 服务端"只监听、还没人来"和"已经有人连上"是两种状态，这里分开显示 ——
 *       不看日志也知道到底有没有人进来。
 */
static void refresh_ui(cg_app_t *app)
{
    cg_widget_t *led = cg_find(app, "led_link");
    cg_widget_t *sb  = cg_find(app, "sb_main");
    const char  *state = "未连接";
    char         peer[192] = "对端 —";
    int          lamps = 0;

    if (s_biz.cli) {                                  /* 客户端：连上了 */
        state = "已连接";
        lamps = 1;
        snprintf(peer, sizeof(peer), "%s", cg_net_peer(s_biz.cli));
    } else if (s_biz.n_peer > 0) {                    /* 服务端：有人连进来了 */
        state = "已连接";
        lamps = 1;
        snprintf(peer, sizeof(peer), "%s 等 %d 个连接", cg_net_peer(s_biz.peers[0]), s_biz.n_peer);
    } else if (s_biz.srv) {                           /* 服务端：只在等 */
        state = "监听中";
        lamps = 2;                                    /* 闪 = 在等连接 */
        snprintf(peer, sizeof(peer), "等待连接…");
    }

    cg_widget_set_text(led, state);
    cg_led_set(led, lamps);                           /* 0 灭 / 1 亮 / 2 闪 */
    cg_status_set(sb, 0, state);
    cg_status_set(sb, 1, peer);
    cg_status_setf(sb, 2, "RX %lld B", s_biz.rx_total);
    cg_status_setf(sb, 3, "TX %lld B", s_biz.tx_total);
}

/* ------------------------------------------------------------ 轮询（取数据）

 * 为什么要有这一段、为什么是这个形状，见文件头"线程模型"。
 */

/**
 * @brief 把一条连接的接收缓冲取空并灌进终端
 * @param term 终端控件
 * @param n 连接句柄
 * @return int 这次共取到多少字节
 * @note **它不在读线程里跑**：读线程只往环形缓冲搬字节，控件只能在主线程碰。
 *       cg_net_read 是非阻塞的，所以用 cg_net_available() 先问一句，省一次注定返回 0 的 read。
 */
static int drain_conn(cg_widget_t *term, cg_net_t *n)
{
    unsigned char buf[RX_CHUNK];
    int           loops, got = 0, r;

    for (loops = 0; loops < RX_MAX_CHUNKS; loops++) {
        if (cg_net_available(n) <= 0) break;
        r = cg_net_read(n, buf, (int)sizeof(buf));
        if (r <= 0) break;
        cg_term_write(term, CG_TERM_RX, buf, r);      /* 收到多少灌多少，它会按 \n 切行 */
        s_biz.rx_window += r;
        s_biz.rx_total  += r;
        got += r;
    }
    return got;
}

/** @brief 曲线推一个点：这一秒收到的字节数，并按需自动扩 Y 轴上限 */
static void push_rate_point(cg_app_t *app)
{
    cg_widget_t *cht = cg_find(app, "cht_rate");

    if (!cht) {
        s_biz.rx_window = 0;
        return;
    }
    cg_chart_push(cht, 0, s_biz.rx_window);

    /* Y 轴自适应：只往上扩、不缩回 —— 缩回来会把刚才的尖峰压成一条贴底的线 */
    if (s_biz.rx_window > s_biz.chart_top) {
        int nice = ((s_biz.rx_window + CHART_TOP_MIN - 1) / CHART_TOP_MIN) * CHART_TOP_MIN;
        s_biz.chart_top = nice;
        cg_chart_set_range(cht, 0, nice);
    }
    s_biz.rx_window = 0;
}

/**
 * @brief 轮询回调：收新连接、取各条连接的数据、回收掉线的连接，并刷界面与曲线
 * @param user 挂进去的 cg_app_t（cg_set_timer 的第 4 个参数）
 * @return void
 * @note cg_set_timer 是一次性的，所以结尾要按需再挂一次，这才是"周期"。
 *       刚触发的那颗定时器已经被 cg_timer.c 回收了，所以这里先把自己的句柄清成 NULL。
 */
static void poll_tick(void *user)
{
    cg_app_t    *app  = (cg_app_t *)user;
    cg_widget_t *term = cg_find(app, "term_log");
    char        *line;
    int          i, k;

    s_biz.poll = NULL;                  /* 上一颗已经回收，别让 stop_poll 去碰悬空指针 */

    /* 1) 服务端：收新连接。accept 的 timeout 传 0 = 不等，没连接返回 NULL（正常） */
    if (s_biz.srv) {
        for (k = 0; k < ACCEPT_PER_TICK; k++) {
            cg_net_t *c = cg_net_accept(s_biz.srv, 0);
            if (!c) break;
            if (s_biz.n_peer >= MAX_CLIENTS) {
                term_sys(app, "连接数已满（上限 8），这个新连接被拒收。");
                cg_net_close(c);
                break;
            }
            s_biz.peers[s_biz.n_peer++] = c;
            line = cg_str_format("新连接：%s（现在共 %d 个）", cg_net_peer(c), s_biz.n_peer);
            if (line) {
                term_sys(app, line);
                cg_free(line);
            }
        }
    }

    /* 2) 客户端：先取干净，再看掉线（对端关闭后缓冲里可能还有尾巴，得先取走） */
    if (s_biz.cli) {
        drain_conn(term, s_biz.cli);
        if (!cg_net_connected(s_biz.cli) && cg_net_available(s_biz.cli) <= 0) {
            term_sys(app, "对端关闭了连接（cg_net_connected 变 0）。");
            cg_net_close(s_biz.cli);
            s_biz.cli = NULL;
        }
    }

    /* 3) 服务端：逐个连接取数据；取干净后仍 disconnected = 这人对端走了，回收句柄 */
    for (i = 0; i < s_biz.n_peer; ) {
        drain_conn(term, s_biz.peers[i]);
        if (!cg_net_connected(s_biz.peers[i]) && cg_net_available(s_biz.peers[i]) <= 0) {
            /* 对端字符串在 close 前取，close 之后句柄就没了 */
            line = cg_str_format("连接断开：%s（还剩 %d 个）", cg_net_peer(s_biz.peers[i]), s_biz.n_peer - 1);
            if (line) {
                term_sys(app, line);
                cg_free(line);
            }
            cg_net_close(s_biz.peers[i]);
            peer_remove(i);
            continue;                    /* 前移过来的那条还没看，下标不前进 */
        }
        i++;
    }

    refresh_ui(app);

    if (++s_biz.ticks >= TICKS_PER_SEC) {
        s_biz.ticks = 0;
        push_rate_point(app);
    }

    /* 还有要看的东西才再挂一颗；都没了就此停表（重连时再挂） */
    if (s_biz.srv || s_biz.cli || s_biz.n_peer > 0) {
        s_biz.poll = cg_set_timer(app, POLL_MS, poll_tick, app);
    }
}

/** @brief 挂上轮询定时器（已经在跑就不重复挂） */
static void start_poll(cg_app_t *app)
{
    if (!s_biz.poll) s_biz.poll = cg_set_timer(app, POLL_MS, poll_tick, app);
}

/** @brief 撤掉轮询定时器（NULL 安全，重复调用没关系） */
static void stop_poll(void)
{
    if (s_biz.poll) {
        cg_kill_timer(s_biz.poll);
        s_biz.poll = NULL;
    }
}

/* ------------------------------------------------------------ 连接 / 监听 / 发送 */

/**
 * @brief 以客户端身份连接，成功则开始轮询；失败把中文原因写进日志视图
 * @param app 所属应用
 * @return void
 * @note host 传 NULL / 空串 = 127.0.0.1；timeout_ms <= 0 按 3000 算（坏地址不会冻住界面）。
 */
static void do_connect(cg_app_t *app)
{
    cg_widget_t *edt  = cg_find(app, "edt_host");
    const char  *host = cg_widget_text(edt);
    char        *line;
    int          port = port_of(app);

    if (port <= 0 || port > 65535) {
        term_sys(app, "端口要在 1~65535 之间，先把它改对。");
        return;
    }

    s_biz.cli = cg_net_connect((host && *host) ? host : NULL, port, 3000);
    if (!s_biz.cli) {
        /* 连接失败时句柄是 NULL，所以只能问 NULL："刚刚那次连接为什么失败"。 */
        term_sys(app, cg_net_last_error(NULL));
        return;
    }

    s_biz.rx_window = 0;
    s_biz.ticks     = 0;
    line = cg_str_format("已连接 %s（目标 %s:%d）。", cg_net_peer(s_biz.cli),
                         (host && *host) ? host : "127.0.0.1", port);
    if (line) {
        term_sys(app, line);
        cg_free(line);
    }
    if (s_biz.ini[0]) {
        char buf[32];
        cg_ini_write(s_biz.ini, "网口", "地址", (host && *host) ? host : "127.0.0.1");
        snprintf(buf, sizeof(buf), "%d", port);
        cg_ini_write(s_biz.ini, "网口", "端口", buf);
    }
    refresh_ui(app);
    start_poll(app);
}

/**
 * @brief 以服务端身份监听端口，成功则开始轮询（accept 在时钟里做）
 * @param app 所属应用
 * @return void
 * @note 绑不上（端口被占用）返回 NULL，原因用 cg_net_last_error(NULL) 取。
 */
static void do_listen(cg_app_t *app)
{
    char *line;
    int   port = port_of(app);

    if (port <= 0 || port > 65535) {
        term_sys(app, "端口要在 1~65535 之间，先把它改对。");
        return;
    }

    s_biz.srv = cg_net_listen(port, 4);          /* backlog <= 0 会被当成 4 */
    if (!s_biz.srv) {
        term_sys(app, cg_net_last_error(NULL));  /* 端口被占用 / 权限不够… */
        return;
    }

    s_biz.rx_window = 0;
    s_biz.ticks     = 0;
    line = cg_str_format("正在监听 0.0.0.0:%d（backlog 4），等客户端连进来……", port);
    if (line) {
        term_sys(app, line);
        cg_free(line);
    }
    if (s_biz.ini[0]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", port);
        cg_ini_write(s_biz.ini, "网口", "端口", buf);
    }
    refresh_ui(app);
    start_poll(app);
}

/**
 * @brief 断开：撤轮询，关掉监听与全部连接，刷界面
 * @param app 所属应用
 * @return void
 * @note 顺序上先关连接再关监听；cg_net_close() 幂等且 NULL 安全，所以不用逐条判空。
 */
static void do_disconnect(cg_app_t *app)
{
    int i, had = (s_biz.srv != NULL) || (s_biz.cli != NULL) || (s_biz.n_peer > 0);

    stop_poll();
    cg_net_close(s_biz.cli);
    s_biz.cli = NULL;
    for (i = 0; i < s_biz.n_peer; i++) cg_net_close(s_biz.peers[i]);
    s_biz.n_peer = 0;
    cg_net_close(s_biz.srv);
    s_biz.srv = NULL;

    refresh_ui(app);
    if (had) term_sys(app, "已断开：监听与全部连接都已关闭。");
}

/**
 * @brief 把一段字节发给所有活着的链路（客户端那条 + 服务端收到的每条）
 * @param app 所属应用
 * @param data 要发的字节
 * @param len 字节数（必须 > 0）
 * @return void
 */
static void send_bytes(cg_app_t *app, const void *data, int len)
{
    cg_widget_t *term = cg_find(app, "term_log");
    int          i, sinks = 0;

    if (s_biz.cli) {
        if (cg_net_write(s_biz.cli, data, len) < 0) {
            term_sys(app, cg_net_last_error(s_biz.cli));
        } else {
            cg_term_write(term, CG_TERM_TX, data, len);
            s_biz.tx_total += len;
            sinks++;
        }
    }
    for (i = 0; i < s_biz.n_peer; i++) {
        if (cg_net_write(s_biz.peers[i], data, len) < 0) {
            term_sys(app, cg_net_last_error(s_biz.peers[i]));
            continue;
        }
        cg_term_write(term, CG_TERM_TX, data, len);
        s_biz.tx_total += len;
        sinks++;
    }

    if (sinks == 0) {
        if (s_biz.srv) term_sys(app, "还没有客户端连进来，暂时没有发送对象。");
        else           term_sys(app, "还没连上：先点「连接」或「监听」。");
    }
}

/**
 * @brief 发送输入框里的内容：按「HEX 发送」勾选决定原文还是十六进制
 * @param app 所属应用
 * @return void
 * @note 没有链路、输入为空、HEX 写错，都在这里给出人话提示，不让按钮"点了没反应"。
 */
static void send_current(cg_app_t *app)
{
    cg_widget_t *edt  = cg_find(app, "edt_send");
    const char  *text = cg_widget_text(edt);

    if (!text || !*text) {
        term_sys(app, "输入框是空的，没东西可发。");
        return;
    }
    if (!has_link()) {
        term_sys(app, s_biz.srv ? "还没人连进来，等客户端接入后再发。" : "还没连上：先点「连接」或「监听」。");
        return;
    }

    if (cg_widget_checked(cg_find(app, "chk_hex"))) {
        unsigned char bytes[512];
        int           n = hex_parse(text, bytes, (int)sizeof(bytes));

        if (n == -2) {
            term_sys(app, "HEX 数据太长了（一次最多 512 字节）。");
            return;
        }
        if (n < 0) {
            term_sys(app, "HEX 写错了：只允许 0-9 A-F 和分隔符，而且数字个数必须是偶数（像 1A 2B）。");
            return;
        }
        if (n == 0) {
            term_sys(app, "HEX 模式没解析出任何字节。");
            return;
        }
        send_bytes(app, bytes, n);
    } else {
        send_bytes(app, text, (int)strlen(text));
    }
    refresh_ui(app);
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 角色下拉框选择变化：记下来并提示当前角色在干什么
 * @param self 角色下拉框
 * @param ev select
 * @param user 未使用
 * @return void
 */
static void on_role_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    s_biz.role = cg_widget_value(self);
    if (has_link()) {
        term_sys(app, "角色改了，但当前链路还在用：要换角色请先点「断开」。");
        return;
    }
    term_sys(app, (s_biz.role == ROLE_SERVER)
                      ? "角色：服务端（监听本机端口，等别人连进来）"
                      : "角色：客户端（去连上面的地址和端口）");
    refresh_ui(app);
}

/**
 * @brief 「连接」按钮：以客户端身份连上去
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 自检（--selftest）会合成触发它，所以这里必须先确认真的开了窗 ——
 *       自检模式下绝不能去真连网。
 */
static void on_connect_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    if (has_link()) {
        term_sys(app, "已经在用了：先点「断开」再重连。");
        return;
    }
    if (s_biz.role == ROLE_SERVER) {
        term_sys(app, "当前角色是「服务端」：请点「监听」，不是「连接」。");
        return;
    }
    do_connect(app);
}

/**
 * @brief 「监听」按钮：以服务端身份监听端口
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_listen_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;          /* 自检别真的去 bind 端口 */

    if (has_link()) {
        term_sys(app, "已经在用了：先点「断开」再监听。");
        return;
    }
    if (s_biz.role == ROLE_CLIENT) {
        term_sys(app, "当前角色是「客户端」：请点「连接」，不是「监听」。");
        return;
    }
    do_listen(app);
}

/**
 * @brief 「断开」按钮：关掉监听与全部连接
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_stop_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_disconnect(cg_app_of(self));
}

/**
 * @brief 「清空」按钮：清空日志视图（累计 RX/TX 计数保留）
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_term_clear(cg_find(app, "term_log"));
    s_biz.rx_window = 0;
    s_biz.ticks     = 0;
    term_sys(app, "已清空日志视图（累计 RX/TX 计数保留）。");
}

/**
 * @brief 「HEX 发送」勾选变化：只改发送时的解析方式，顺手把当前输入试解析一遍
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_hex_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_widget_checked(self)) {
        term_sys(app, "HEX 发送：关。输入按原文（UTF-8）发出去。");
        return;
    }

    term_sys(app, "HEX 发送：开。输入像 1A 2B 2C 这样写，空格可有可无。");
    {
        const char *text = cg_widget_text(cg_find(app, "edt_send"));
        if (text && *text) {
            unsigned char bytes[512];
            int           n = hex_parse(text, bytes, (int)sizeof(bytes));
            char         *line;
            if (n < 0) {
                term_sys(app, "（当前输入解析不了：数字个数要是偶数，而且只允许 0-9 A-F）");
                return;
            }
            line = cg_str_format("（当前输入会发成 %d 个字节）", n);
            if (line) {
                term_sys(app, line);
                cg_free(line);
            }
        }
    }
}

/**
 * @brief 「发送」按钮
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_send_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    send_current(cg_app_of(self));
}

/**
 * @brief 发送输入框里按回车（等价于点「发送」）
 * @param self 输入框
 * @param ev submit
 * @param user 未使用
 * @return void
 */
static void on_send_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    send_current(cg_app_of(self));
}

/**
 * @brief 拖动分隔条松手：把新位置记进配置，下次启动照着恢复
 * @param self 分隔条控件
 * @param ev release
 * @param user 未使用
 * @return void
 */
static void on_split_release(cg_widget_t *self, cg_event_t ev, void *user)
{
    int pos;

    (void)ev;
    (void)user;

    pos = cg_splitter_pos(self);
    if (s_biz.ini[0] && pos >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", pos);
        cg_ini_write(s_biz.ini, "布局", "分隔条", buf);
    }
}

/**
 * @brief 窗口即将关闭：这里只记一笔（没有需要拦下的内容）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 未使用
 * @return void
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    cg_log(CG_LOG_INFO, "网口助手：窗口关闭");
}

/**
 * @brief 窗口已销毁：进程收尾 —— 撤轮询、关掉监听与全部连接
 * @param self 窗口伪控件
 * @param ev destroy
 * @param user 未使用
 * @return void
 */
static void on_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    int i;

    (void)self;
    (void)ev;
    (void)user;

    stop_poll();                     /* 定时器可能还在飞，先撤掉再收句柄 */
    cg_net_close(s_biz.cli);         /* 幂等 + NULL 安全，没连上时直接调也没事 */
    s_biz.cli = NULL;
    for (i = 0; i < s_biz.n_peer; i++) cg_net_close(s_biz.peers[i]);
    s_biz.n_peer = 0;
    cg_net_close(s_biz.srv);
    s_biz.srv = NULL;
    cg_log(CG_LOG_INFO, "网口助手退出：RX %lld / TX %lld 字节", s_biz.rx_total, s_biz.tx_total);
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面就绪：恢复上次的地址 / 端口 / 角色 / 分隔条，点亮初始状态
 * @param app 所属应用
 * @param user desc.ready_user，本示例没挂数据
 * @return void
 * @note **自检模式（--selftest）在这里直接返回**：自检会合成触发每个回调，
 *       要是在这里读配置、挂定时器去连网，CI 就卡住了。
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *cht;
    char        *dir, *ini;

    (void)user;

    if (!cg_app_has_window(app)) return;          /* --selftest：不连网、不读配置 */

    /* 配置文件放在 %LOCALAPPDATA%\netterm\netterm.ini */
    dir = cg_app_data_dir();
    ini = dir ? cg_path_join(dir, "netterm.ini") : NULL;
    if (ini) snprintf(s_biz.ini, sizeof(s_biz.ini), "%s", ini);
    cg_free(ini);
    cg_free(dir);

    cht = cg_find(app, "cht_rate");
    if (cht) cg_chart_set_range(cht, 0, CHART_TOP_MIN);
    s_biz.chart_top = CHART_TOP_MIN;

    s_biz.role = cg_widget_value(cg_find(app, "cmb_role"));
    if (s_biz.ini[0]) {
        char *host  = cg_ini_read(s_biz.ini, "网口", "地址", NULL);
        char *port  = cg_ini_read(s_biz.ini, "网口", "端口", NULL);
        char *role  = cg_ini_read(s_biz.ini, "网口", "角色", NULL);
        char *split = cg_ini_read(s_biz.ini, "布局", "分隔条", NULL);

        if (host && *host) cg_widget_set_text(cg_find(app, "edt_host"), host);
        if (port) {
            int p = 0;
            sscanf(port, "%d", &p);
            if (p > 0) cg_widget_set_value(cg_find(app, "spn_port"), p);
        }
        if (role) {
            int r = 0;
            sscanf(role, "%d", &r);
            if (r == ROLE_CLIENT || r == ROLE_SERVER) {
                cg_widget_set_value(cg_find(app, "cmb_role"), r);
                s_biz.role = r;
            }
        }
        if (split) {
            int pos = 0;
            sscanf(split, "%d", &pos);
            cg_splitter_set(cg_find(app, "sp_main"), pos);
        }
        cg_free(host);
        cg_free(port);
        cg_free(role);
        cg_free(split);
    }

    refresh_ui(app);
    cg_log(CG_LOG_INFO, "网口助手启动：角色 %s，配置 %s",
           s_biz.role == ROLE_SERVER ? "服务端" : "客户端", s_biz.ini[0] ? s_biz.ini : "(无)");
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 netterm.cgui 里） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_role_select",    on_role_select),
    CG_BIND("on_connect_click",  on_connect_click),
    CG_BIND("on_listen_click",   on_listen_click),
    CG_BIND("on_stop_click",     on_stop_click),
    CG_BIND("on_clear_click",    on_clear_click),
    CG_BIND("on_hex_change",     on_hex_change),
    CG_BIND("on_send_click",     on_send_click),
    CG_BIND("on_send_submit",    on_send_submit),
    CG_BIND("on_split_release",  on_split_release),
    CG_BIND("on_close",          on_close),
    CG_BIND("on_destroy",        on_destroy),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动去 exe 旁边找同名 .cgui */
    desc.on_ready = on_ready;       /* 用 on_ready 钩子而不是 app.events.ready：
                                       --selftest 也会走它，正好验证"自检不连网"那个守卫 */

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "netterm.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "netterm.cgui";
    return cg_app_run(&desc);
}
