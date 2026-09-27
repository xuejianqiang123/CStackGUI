/**
 * @file    main.c
 * @brief   Modbus 上位机示例：cg_net（Modbus/TCP）连从站 + cg_modbus_* 拼帧与解帧
 * @details 界面全在 modbus.cgui 里，这里只有行为。这个示例想讲清的是**工控上位机的主循环**：
 *
 *            「拼一帧 → 发出去 → 把回来的字节按协议切成一帧帧 → 解帧 → 落到界面」
 *
 *          四段里有三段是现场最容易写错的地方，这里都写对了：
 *
 *          1. **切帧不能靠"读一次 = 一帧"**。TCP 是字节流：一次 cg_net_read 拿到的可能
 *             是半帧、也可能是一帧半。Modbus/TCP 好在**头里有长度字段**（MBAP 的第 5、6 字节，
 *             含单元标识），所以正确的做法是把收到的字节攒着，攒够 `6 + 长度` 就切一帧出来，
 *             剩下的留给下一轮（见 drain_frames 的注释）。串口上的 Modbus/RTU 没有长度字段，
 *             得靠"帧间静默"或 CRC 反向定位 —— 那是另一个话题，本示例只做 TCP。
 *
 *          2. **请求要能被认出"没回来"**。上位机不能发完就当成功：这里记下"在等回应"和
 *             发出时刻，超过 REQ_TIMEOUT_MS 没收到就报一句超时（现场最常见的故障就是
 *             "网线掉了 / 从站忙"）。等回应期间不再发下一次，免得响应错位 ——
 *             这是 Modbus 主站的**事务**概念（一问一答），比"发了就不管"稳得多。
 *
 *          3. **异常响应要当正常结果处理**。从站回 `功能码|0x80 + 异常码` 是协议的一部分
 *             （非法地址、非法数据…），不是"通信坏了"。cg_modbus_parse() 已经替你剥掉 0x80，
 *             直接给原功能码 + 异常码 —— 日志里要能一眼看出"是地址写错了"还是"帧坏了"。
 *
 *          **这里没有"主站会话/自动重试"**：cg_modbus_* 是纯函数，连接与超时归本文件 ——
 *          框架不给"会持状态的主站"，因为轮询节奏、重试策略、多从站调度每个项目都不一样。
 *
 *          新手会踩的两点（和 netterm 示例同一套）：
 *            1. 自检模式（--selftest）会**合成触发每个回调**，所以凡是真连网的地方先问
 *               cg_app_has_window()，没有窗口就直接返回，别让 CI 卡在真连接上；
 *            2. 定时器回调**必须自己重挂**（cg_set_timer 是一次性的，见 cstackgui.h 的说明）。
 *
 *          自测（不需要真设备）：另开一个控制台跑 `node examples/modbus/mock-slave.mjs 1502`，
 *          然后点「连接」→「读一次」，或勾上「自动轮询」看表格在变。
 *
 *          编译：在项目根跑 build-app.bat modbus
 */
#include "cstackgui.h"

#include <stdio.h>
#include <stdlib.h>     /* getenv（演示模式的环境变量开关） */
#include <string.h>

/** 主循环周期（毫秒）：收数据与超时判断都挂在这一拍上 */
#define POLL_MS        50
/** 等回应超时（毫秒）：超过就当这次没回来 */
#define REQ_TIMEOUT_MS 1500
/** 收字节缓冲：够攒几帧；满了说明"取的速度跟不上"，直接丢最老的（宁可丢也不卡死） */
#define RX_CAP         8192
/** 一帧最长多少字节（MBAP 长度字段最大 255 + 6，留点余量） */
#define FRAME_MAX      300

/* ------------------------------------------------------------ 运行时状态
 *
 * 一个示例只跑一个连接，所以状态用文件级静态量 —— 不引"会话对象"这套东西，
 * 让读者一眼能看完（真项目里该把它们收进一个 struct）。
 */

static cg_net_t      *s_conn;            /* 只有一条连接（Modbus/TCP 客户端角色） */
static unsigned char  s_rx[RX_CAP];      /* 收字节攒在这里，够一帧就切走 */
static int            s_rxlen;

static unsigned       s_ms;              /* 自己数的毫秒（POLL_MS 一拍加一次） */
static int            s_auto;            /* 自动轮询开关（勾选框） */
static unsigned       s_next_poll;       /* 下一次自动轮询的时刻 */

static unsigned       s_tid;             /* 事务号：每次请求 +1，用来对回应 */
static int            s_waiting;         /* 1 = 有请求在等回应 */
static unsigned       s_sent_ms;         /* 发出时刻（超时判断用） */

static int            s_ok;              /* 成功次数（状态栏） */
static int            s_err;             /* 失败次数（状态栏） */

/** --demo：启动就连本机模拟从站并开始轮询（给演示与 CI 用，省得人手点两下） */
static int            s_demo;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 往终端里写一条自己记的消息（连接状态、错误提示）
 * @param app 所属应用
 * @param text UTF-8 文本
 * @return void
 */
static void log_sys(cg_app_t *app, const char *text)
{
    cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS, text, -1);
}

/**
 * @brief 把一段字节以 HEX 写进终端（报文日志就靠它）
 * @param app 所属应用
 * @param dir CG_TERM_TX / CG_TERM_RX
 * @param data 字节
 * @param len 长度
 * @return void
 * @note 终端本身是 HEX 模式（cg_term_set_hex），所以这里再编一次也不会重复 ——
 *       但**自己编成文本**有个好处：日志存盘/复制出来就能直接对协议手册看。
 */
static void log_hex(cg_app_t *app, int dir, const void *data, int len)
{
    char buf[FRAME_MAX * 3 + 8];

    if (len <= 0 || len > FRAME_MAX) return;
    cg_hex_encode(data, len, " ", 1, buf, (int)sizeof(buf));
    cg_term_write(cg_find(app, "term_log"), dir, buf, -1);
}

/**
 * @brief 刷新状态栏与链路灯
 * @param app 所属应用
 * @return void
 */
static void refresh_ui(cg_app_t *app)
{
    cg_widget_t *led = cg_find(app, "led_link");
    cg_widget_t *sb  = cg_find(app, "sb_main");
    int          linked = (s_conn && cg_net_connected(s_conn)) ? 1 : 0;

    cg_widget_set_text(led, linked ? "已连接" : "未连接");
    cg_led_set(led, linked ? 1 : 0);
    cg_status_set(sb, 0, linked ? "已连接" : "未连接");
    cg_status_setf(sb, 1, "成功 %d", s_ok);
    cg_status_setf(sb, 2, "失败 %d", s_err);
    if (s_waiting) cg_status_setf(sb, 3, "等待 %ums", s_ms - s_sent_ms);
    else           cg_status_set(sb, 3, "等待 —");
}

/**
 * @brief 把界面上的参数读齐（改一处就够，回调里不再各读一遍）
 * @param app 所属应用
 * @param slave 从站号出口
 * @param func 功能码出口（3 / 4 / 1）
 * @param start 起始地址出口
 * @param count 个数出口
 * @return int 1 = 参数都能用；0 = 已经往日志里写了为什么不行
 */
static int read_params(cg_app_t *app, int *slave, int *func, int *start, int *count)
{
    static const int k_funcs[3] = { 3, 4, 1 };      /* 与 .cgui 里 combo 的 items 一一对应 */
    int idx = cg_widget_value(cg_find(app, "cmb_func"));

    *slave = cg_widget_value(cg_find(app, "spn_slave"));
    *start = cg_widget_value(cg_find(app, "spn_addr"));
    *count = cg_widget_value(cg_find(app, "spn_count"));
    *func  = (idx >= 0 && idx < 3) ? k_funcs[idx] : 3;

    if (*slave < 1 || *slave > 247) {
        log_sys(app, "从站号要在 1~247 之间。");
        return 0;
    }
    if (*count < 1 || (*func == 1 ? *count > 2000 : *count > 125)) {
        log_sys(app, "数量超出规范上限（寄存器 125 个 / 线圈 2000 个）。");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ 发请求 */

/**
 * @brief 拼一帧读请求、发出去，并记下"在等回应"
 * @param app 所属应用
 * @return void
 * @note 等回应期间**不再发新的**：Modbus 是一问一答，抢着发会让响应错位，
 *       到时候"表里的值对不上地址"这种错最难查。
 */
static void do_read(cg_app_t *app)
{
    int           slave, func, start, count;
    unsigned char frame[16];
    int           n;

    if (!s_conn || !cg_net_connected(s_conn)) {
        log_sys(app, "还没连上：先点「连接」。（本机自测先跑 node examples/modbus/mock-slave.mjs 1502）");
        return;
    }
    if (s_waiting) return;                        /* 上一次还没回来，别插队 */
    if (!read_params(app, &slave, &func, &start, &count)) return;

    s_tid++;
    n = cg_modbus_build_read(CG_MODBUS_TCP, (int)s_tid, slave, func, start, count,
                             frame, (int)sizeof(frame));
    if (n < 0) {
        log_sys(app, "拼帧失败（参数不合法），这次没发。");
        s_err++;
        return;
    }

    log_hex(app, CG_TERM_TX, frame, n);
    if (cg_net_write(s_conn, frame, n) < 0) {
        log_sys(app, cg_net_last_error(s_conn));
        s_err++;
        return;
    }
    s_waiting = 1;
    s_sent_ms = s_ms;
    refresh_ui(app);
}

/* ------------------------------------------------------------ 收与解帧 */

/**
 * @brief 把一次读响应的值填进寄存器表
 * @param app 所属应用
 * @param r 解析结果
 * @param start 这次请求的起始地址（响应里没有它，得自己记着）
 * @param func 这次请求的功能码
 * @return void
 */
static void fill_table(cg_app_t *app, const cg_modbus_rsp_t *r, int start, int func)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regs");
    int          i;

    cg_table_clear(tbl);
    for (i = 0; i < r->nvalues; i++) {
        char addr[32], dec[32], hex[32];
        const char *cells[3];

        if (func == 1) snprintf(addr, sizeof(addr), "线圈 %d", start + i);
        else           snprintf(addr, sizeof(addr), "%d", start + i);

        if (func == 1) snprintf(dec, sizeof(dec), "%s", r->values[i] ? "ON" : "OFF");
        else           snprintf(dec, sizeof(dec), "%u", (unsigned)r->values[i]);

        /* 十六进制用框架的进制转换（大写、与数据手册写法一致） */
        {
            char tmp[24];

            if (cg_int_to_base((long long)r->values[i], 16, tmp, (int)sizeof(tmp)) > 0)
                snprintf(hex, sizeof(hex), "0x%s", tmp);
            else
                snprintf(hex, sizeof(hex), "0x%X", (unsigned)r->values[i]);
        }

        cells[0] = addr;
        cells[1] = dec;
        cells[2] = hex;
        cg_table_add_row(tbl, cells, 3);
    }
}

/**
 * @brief 处理切出来的一整帧
 * @param app 所属应用
 * @param frame 帧首地址
 * @param len 帧长
 * @return void
 */
static void handle_frame(cg_app_t *app, const unsigned char *frame, int len)
{
    cg_modbus_rsp_t r;
    int             ret;
    int             start = cg_widget_value(cg_find(app, "spn_addr"));
    int             func  = 3;
    static const int k_funcs[3] = { 3, 4, 1 };
    int             idx   = cg_widget_value(cg_find(app, "cmb_func"));

    if (idx >= 0 && idx < 3) func = k_funcs[idx];

    ret = cg_modbus_parse(CG_MODBUS_TCP, frame, len, &r);
    if (ret != 0) {
        /* -1 太短 / -3 格式不对（TCP 没有 CRC，所以不会出现 -2）。分得细是为了日志能说清。 */
        log_sys(app, ret == -1 ? "响应帧太短，丢掉这一帧。"
                               : "响应帧格式不对（功能码或长度跟协议对不上），丢掉这一帧。");
        s_err++;
        s_waiting = 0;
        refresh_ui(app);
        return;
    }

    if (r.exception) {
        /* 异常响应是**从站的正常回答**：不是通信坏了，是这次请求它不认（地址/数据不对）。
         * 异常码翻成人话再进日志 —— 现场一眼能看出"是我地址写错了"还是"链路坏了"。 */
        char msg[128];

        snprintf(msg, sizeof(msg), "从站报异常码 %d（%s）", r.exception,
                 r.exception == 1 ? "非法功能码" :
                 r.exception == 2 ? "非法数据地址" :
                 r.exception == 3 ? "非法数据值" :
                 r.exception == 4 ? "从站设备故障" : "其他");
        log_sys(app, msg);
        s_err++;
    } else {
        fill_table(app, &r, start, func);
        s_ok++;
    }

    s_waiting = 0;
    refresh_ui(app);
}

/**
 * @brief 从接收缓冲取字节，并按 MBAP 的长度字段切成整帧
 * @param app 所属应用
 * @return void
 * @details **切帧**是这个文件最该看的一段：TCP 是字节流，一次 read 拿到的可能不是一帧。
 *          攒着 + 按头里的长度字段切，是这个协议下最省事也最不会错的做法。
 */
static void drain_frames(cg_app_t *app)
{
    for (;;) {
        int n = cg_net_read(s_conn, s_rx + s_rxlen, RX_CAP - s_rxlen);

        if (n <= 0) break;
        s_rxlen += n;
    }

    while (s_rxlen >= 7) {
        int need = 6 + ((s_rx[4] << 8) | s_rx[5]);      /* MBAP：长度字段含单元标识 */

        if (need < 8 || need > FRAME_MAX) {
            /* 头就不像 Modbus（长度字段离谱）：丢掉缓冲，别拿垃圾当帧去解 */
            log_sys(app, "收到的东西不像 Modbus/TCP（长度字段离谱），已清空接收缓冲。");
            s_err++;
            s_rxlen = 0;
            refresh_ui(app);
            return;
        }
        if (s_rxlen < need) break;                       /* 还没攒够一帧，等下一轮 */

        log_hex(app, CG_TERM_RX, s_rx, need);
        handle_frame(app, s_rx, need);

        memmove(s_rx, s_rx + need, (size_t)(s_rxlen - need));
        s_rxlen -= need;
    }

    if (s_rxlen == RX_CAP) {                             /* 缓冲满还没切出一帧：没法更糟了 */
        log_sys(app, "接收缓冲满了还凑不出完整的一帧，丢掉重来。");
        s_err++;
        s_rxlen = 0;
    }
}

/* ------------------------------------------------------------ 主循环 */

/**
 * @brief 定时器回调：取数据、判超时、按需发起轮询
 * @param user 挂定时器时传进来的 cg_app_t*（cg_set_timer 的回调只收这一个参数）
 * @return void
 * @note 定时器是**一次性**的：每拍最后都要重新挂，否则只会跑一次（框架就这么设计，
 *       见 cstackgui.h 里 cg_set_timer 的说明）。
 */
static void poll_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    s_ms += POLL_MS;

    if (s_conn && !cg_net_connected(s_conn)) {
        log_sys(app, "对端把连接关了（cg_net_connected 变 0）。");
        cg_net_close(s_conn);
        s_conn   = NULL;
        s_waiting = 0;
        s_rxlen  = 0;
        refresh_ui(app);
    }
    if (s_conn) drain_frames(app);

    if (s_waiting && (unsigned)(s_ms - s_sent_ms) > REQ_TIMEOUT_MS) {
        log_sys(app, "等回应超时：从站没回（网线掉了 / 从站忙 / 地址被防火墙挡了）。");
        s_err++;
        s_waiting = 0;
        refresh_ui(app);
    }

    if (s_auto && s_conn && !s_waiting && s_ms >= s_next_poll) {
        int iv = cg_widget_value(cg_find(app, "spn_iv"));

        if (iv < 50) iv = 50;
        do_read(app);
        s_next_poll = s_ms + (unsigned)iv;
    } else if (s_waiting) {
        refresh_ui(app);                                 /* 状态栏里的"等了多少毫秒"要动 */
    }

    cg_set_timer(app, POLL_MS, poll_tick, app);          /* 重挂：下一拍 */
}

/* ------------------------------------------------------------ 回调 */

/**
 * @brief 「连接」按钮 / 再点一次断开
 * @param self 按钮
 * @param ev 事件
 * @param user 用不到
 * @return void
 */
static void on_connect(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_host");
    int          port = cg_widget_value(cg_find(app, "spn_port"));

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) return;        /* 自检模式别真连网 */

    if (s_conn) {
        cg_net_close(s_conn);
        s_conn    = NULL;
        s_waiting = 0;
        s_rxlen   = 0;
        log_sys(app, "已断开。");
        cg_widget_set_text(self, "连接");
        refresh_ui(app);
        return;
    }
    if (port <= 0 || port > 65535) {
        log_sys(app, "端口要在 1~65535 之间。");
        return;
    }

    s_conn = cg_net_connect(cg_widget_text(edt), port, 3000);
    if (!s_conn) {
        log_sys(app, cg_net_last_error(NULL));
        s_err++;
        refresh_ui(app);
        return;
    }
    s_rxlen   = 0;
    s_waiting = 0;
    {
        char msg[192];

        snprintf(msg, sizeof(msg),
                 "已连接到 %s:%d。Modbus/TCP 的帧头是 7 字节 MBAP（事务号/协议/长度/单元），**没有 CRC**。",
                 cg_widget_text(edt), port);
        log_sys(app, msg);
    }
    cg_widget_set_text(self, "断开");
    refresh_ui(app);
}

/**
 * @brief 「读一次」按钮 / 切换功能码时也读一次
 * @param self 触发它的控件
 * @param ev 事件
 * @param user 用不到
 * @return void
 */
static void on_read(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_read(cg_app_of(self));
}

/**
 * @brief 「自动轮询」勾选框
 * @param self 勾选框
 * @param ev 事件
 * @param user 用不到
 * @return void
 */
static void on_auto(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    s_auto = cg_widget_checked(self);
    if (s_auto) s_next_poll = 0;                 /* 立刻读第一次 */
    log_sys(app, s_auto ? "开始自动轮询。" : "停止自动轮询。");
}

/**
 * @brief 窗口关闭前：把连接收干净（不留句柄）
 * @param self 窗口
 * @param ev 事件
 * @param user 用不到
 * @return void
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    if (s_conn) {
        cg_net_close(s_conn);
        s_conn = NULL;
    }
}

/**
 * @brief 退出前：同样把句柄放掉（cg_net_close 幂等且 NULL 安全）
 * @param self 窗口
 * @param ev 事件
 * @param user 用不到
 * @return void
 */
static void on_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_close(self, ev, user);
}

/**
 * @brief 建表头、把终端切到 HEX 模式、起主循环定时器
 * @param app 所属应用
 * @param user 用不到（on_ready 的签名固定两参数）
 * @return void
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;

    cg_widget_t *tbl = cg_find(app, "tbl_regs");

    /* 列在代码里建（.cgui 只描述控件本身）：改列宽/加列不用动界面文件 */
    cg_table_add_column(tbl, "地址", 140);
    cg_table_add_column(tbl, "值", 120);
    cg_table_add_column(tbl, "十六进制", 0);

    /* **不要**打开终端自己的 HEX 模式：本示例是把帧**先编成 hex 文本**再写进去的
     * （见 log_hex），两个机制一起上就会**编两遍** —— 表现是日志里出现
     * `20 36 42`（正是字符 " 6B" 的 hex），一眼看不出来但全是错的（2026-09-27 截图抓到的）。
     * 要 hex 只有一条路：要么让终端编（直接 cg_term_write 原始字节），要么自己编（本示例）。
     * 自己编的好处是**存盘/复制出来直接能对协议手册看**。 */
    log_sys(app, "Modbus/TCP 上位机就绪。自测：node examples/modbus/mock-slave.mjs 1502");

    if (s_demo) {
        /* 演示模式（--demo 或环境变量 CG_MODBUS_DEMO=1）：等价于"用户点了连接、勾上自动轮询"。
         * 走的是**同一批函数**，不另开一条代码路径 —— 演示模式不该掩盖真实流程。 */
        log_sys(app, "演示模式：自动连接本机模拟从站并开始轮询。");
        s_auto      = 1;
        s_next_poll = 0;
        on_connect(cg_find(app, "btn_conn"), 0, NULL);
    }

    refresh_ui(app);
    cg_set_timer(app, POLL_MS, poll_tick, app);
}

/* ------------------------------------------------------------ 入口 */

/** 事件名 -> C 函数 的登记表（右边这些名字写在 modbus.cgui 里） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_connect", on_connect),
    CG_BIND("on_read",    on_read),
    CG_BIND("on_auto",    on_auto),
    CG_BIND("on_close",   on_close),
    CG_BIND("on_destroy", on_destroy),
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
    int           i;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动去 exe 旁边找同名 .cgui */
    desc.on_ready         = on_ready;

    /* 演示模式：命令行给 --demo，或者设环境变量 CG_MODBUS_DEMO=1。
     * 为什么两种都认：命令行参数最自然，但 `tools\shot.ps1`（截图/CI 用）只会把 .cgui
     * 路径传给 exe、没法再塞一个开关，所以留了环境变量这条 —— 环境变量会被子进程继承。 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) s_demo = 1;
    }
    {
        const char *env = getenv("CG_MODBUS_DEMO");

        if (env && *env && strcmp(env, "0") != 0) s_demo = 1;
    }

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "modbus.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "modbus.cgui";
    return cg_app_run(&desc);
}
