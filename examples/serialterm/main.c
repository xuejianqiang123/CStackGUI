/**
 * @file    main.c
 * @brief   串口助手示例：cg_serial_* 的正确用法（读线程 + 时钟回调取数据）
 * @details 这是"跟板子说话"那类工具的样板：界面全在 serialterm.cgui 里，这里只有行为。
 *
 *          **线程模型是这个示例唯一想讲清楚的事，也是唯一容易出事的地方**：
 *            - cg_serial_open() 内部起一个读线程，它只干一件事 —— 把驱动里的字节搬进
 *              64KB 环形缓冲（满了丢最老的），**它绝不碰控件**；
 *            - 控件（HWND）只能在建它的那个线程里碰，这是 Win32 的硬约束，不是本项目的风格偏好；
 *            - 所以数据要"按时来取"：cg_set_timer(50) 定周期 → 时钟回调里 cg_serial_read()
 *              → cg_term_write() 灌进 terminal 控件 → 顺带刷状态栏与曲线。
 *
 *          界面上用到的控件（都在 .cgui 里声明）：
 *            combo ×2（端口 / 波特率）、button ×4、led（链路灯）、terminal（收发日志）、
 *            splitter（日志区 / 发送区之间，能上下拖）、group（发送区）、
 *            chart（每秒收到多少字节）、statusbar（状态 / RX / TX）。
 *
 *          几个新手会踩的点，这里都写对了：
 *            1. cg_app_selftest 会**合成触发每个回调**，所以凡是要开串口或弹对话框的地方，
 *               先问一句 cg_app_has_window()，自检模式下直接返回（否则 CI 会卡在弹窗或无端口报错上）；
 *            2. 发送按「HEX 发送」勾选决定走原文还是十六进制；转换是手写的 —— 不用 strtok
 *               （它会就地改原串、还要维护状态），而且"1A2B"这种没分隔符的也得算对；
 *            3. 断开与退出都要 cg_serial_close()，它**幂等且 NULL 安全**，多叫几次没关系；
 *            4. 打开失败别只报"失败了"：cg_serial_last_error(NULL) 给的是中文原因
 *               （端口被占用 / 找不到 / 波特率不支持），直接写进日志视图，这是可用性的关键。
 *
 *          编译：在项目根跑 build-app.bat serialterm
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** 轮询周期（毫秒）：50ms 足够跟手，也不会给界面添负担 */
#define POLL_MS        50
/** 多少拍算一秒（把"每秒收到多少字节"聚合成曲线上的一个点） */
#define TICKS_PER_SEC  (1000 / POLL_MS)
/** 一拍最多取多少字节 */
#define RX_CHUNK       4096
/** 一拍最多取几轮（防"数据一直比界面快"把这一拍拖久；剩下的下一拍再取） */
#define RX_MAX_CHUNKS  32
/** 曲线 Y 轴的最小上限，也是初始上限（和 .cgui 里 chart 的 max 对齐） */
#define CHART_TOP_MIN  256

/** 业务状态：串口链路、轮询定时器、累计量、曲线聚合量 */
typedef struct {
    cg_serial_t *ser;         /**< 打开中的串口；NULL = 没连 */
    cg_timer_t  *poll;        /**< 轮询定时器（一次性的，"再挂一次"就是周期） */
    char         ini[1200];   /**< 配置文件路径（记住上次的端口 / 波特率 / 分隔条） */
    int          n_ports;     /**< 上次枚举到几个串口（0 = 没有，别拿占位文字去开） */
    int          ticks;       /**< 距曲线上一个点过了几拍 */
    int          rx_window;   /**< 这一秒内收到的字节数 */
    int          chart_top;   /**< 曲线 Y 轴当前上限（只往上扩） */
    long long    rx_total;    /**< 累计收到字节（断连后还要显示，所以自己留一份） */
    long long    tx_total;    /**< 累计发出字节 */
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
 * @brief 在下拉框里按文字找条目下标
 * @param w 下拉框控件
 * @param text 要匹配的文字（忽略英文大小写）
 * @return int 下标；找不到返回 -1
 */
static int combo_find(const cg_widget_t *w, const char *text)
{
    int i, n;

    if (!w || !text) return -1;
    n = cg_widget_list_count(w);
    for (i = 0; i < n; i++) {
        const char *item = cg_widget_list_item(w, i);
        if (item && cg_str_equal(item, text)) return i;
    }
    return -1;
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

/* ------------------------------------------------------------ 界面刷新 */

/**
 * @brief 把累计收发的字节数刷到状态栏第 2、3 段
 * @param app 所属应用
 * @return void
 * @note 连着的时候以句柄里的计数为准（只增不减）；断开后句柄已经释放，
 *       就继续显示断开前的数字，免得状态栏突然归零让人以为数据丢了。
 */
static void refresh_stats(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    if (s_biz.ser) {
        s_biz.rx_total = cg_serial_rx_bytes(s_biz.ser);
        s_biz.tx_total = cg_serial_tx_bytes(s_biz.ser);
    }
    cg_status_setf(sb, 1, "RX %lld B", s_biz.rx_total);
    cg_status_setf(sb, 2, "TX %lld B", s_biz.tx_total);
}

/**
 * @brief 把"连上 / 断开"这个状态刷到按钮文字、链路灯和状态栏第 1 段
 * @param app 所属应用
 * @param on 1 = 已连接，0 = 未连接
 * @return void
 */
static void set_connected(cg_app_t *app, int on)
{
    cg_widget_t *led = cg_find(app, "led_link");

    cg_widget_set_text(cg_find(app, "btn_connect"), on ? "关闭串口" : "打开串口");
    cg_widget_set_text(led, on ? "已连接" : "未连接");
    cg_led_set(led, on ? 1 : 0);           /* 0 灭 / 1 亮 / 2 闪 */
    cg_status_set(cg_find(app, "sb_main"), 0, on ? "已连接" : "未连接");
}

/* ------------------------------------------------------------ 端口枚举 */

/**
 * @brief 枚举本机串口，填进「端口」下拉框
 * @param app 所属应用
 * @return int 找到的端口个数（0 = 一个都没有，不是错误）
 * @note cg_serial_list 的返回是"写进 buf 的字节数"，内容是每行一个端口名
 *       （"COM3\nCOM7\n"），所以这里按 '\n' 切行；一个都没有时放一句占位文字，
 *       避免下拉框空着让人以为是界面坏了。
 */
static int fill_ports(cg_app_t *app)
{
    cg_widget_t *cmb = cg_find(app, "cmb_port");
    char         buf[4096];
    const char  *p;
    int          n, count = 0;

    n = cg_serial_list(buf, (int)sizeof(buf));
    cg_widget_list_clear(cmb);

    if (n <= 0) {
        cg_widget_list_add(cmb, "（没有可用串口）");
        cg_widget_set_value(cmb, 0);
        s_biz.n_ports = 0;
        term_sys(app, "没枚举到任何串口。插上设备后点「刷新」再试。");
        return 0;
    }

    p = buf;
    while (*p) {
        char   line[128];
        size_t len = 0;

        while (p[len] && p[len] != '\n' && len + 1 < sizeof(line)) len++;
        memcpy(line, p, len);
        line[len] = '\0';
        if (line[0]) {
            cg_widget_list_add(cmb, line);
            count++;
        }
        while (*p && *p != '\n') p++;       /* 跳过本行剩余部分 */
        if (*p == '\n') p++;
    }
    cg_widget_set_value(cmb, 0);
    s_biz.n_ports = count;
    return count;
}

/* ------------------------------------------------------------ 轮询（取数据）

 * 为什么要有这一段、为什么是这个形状，见文件头"线程模型"。
 */

/**
 * @brief 曲线推一个点：这一秒收到的字节数，并按需自动扩 Y 轴上限
 * @param app 所属应用
 * @return void
 */
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
 * @brief 轮询回调：把串口接收缓冲里的数据搬进终端，并刷新状态栏与曲线
 * @param user 挂进去的 cg_app_t（cg_set_timer 的第 4 个参数）
 * @return void
 * @note 它**不在读线程里**跑：读线程只往环形缓冲搬字节，控件只能在主线程碰。
 *       cg_set_timer 是一次性的，所以每次结束都要再挂一次，这才是"周期"。
 *       刚触发的那颗定时器已经被 cg_timer.c 回收了，所以这里先把自己的句柄清成 NULL。
 */
static void poll_tick(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term_log");
    unsigned char buf[RX_CHUNK];
    int           loops, n;

    s_biz.poll = NULL;                  /* 上一颗已经回收，别让 stop_poll 去碰悬空指针 */
    if (!s_biz.ser) return;             /* 中途断开了：就此停表，重连时再挂 */

    /* 把缓冲取空：cg_serial_available() 先问一句，省一次注定返回 0 的 read；
     * RX_MAX_CHUNKS 是"数据一直比界面快"时的保险，剩下的留给下一拍。 */
    for (loops = 0; loops < RX_MAX_CHUNKS; loops++) {
        if (cg_serial_available(s_biz.ser) <= 0) break;
        n = cg_serial_read(s_biz.ser, buf, (int)sizeof(buf));
        if (n <= 0) break;
        cg_term_write(term, CG_TERM_RX, buf, n);     /* 收到多少灌多少，它会按 \n 切行 */
        s_biz.rx_window += n;
    }

    refresh_stats(app);

    if (++s_biz.ticks >= TICKS_PER_SEC) {
        s_biz.ticks = 0;
        push_rate_point(app);
    }

    s_biz.poll = cg_set_timer(app, POLL_MS, poll_tick, app);
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

/* ------------------------------------------------------------ 连接 / 发送 */

/**
 * @brief 打开串口，成功则开始轮询；失败把中文原因写进日志视图
 * @param app 所属应用
 * @return void
 */
static void do_connect(cg_app_t *app)
{
    cg_widget_t *cmb_port = cg_find(app, "cmb_port");
    cg_widget_t *cmb_baud = cg_find(app, "cmb_baud");
    const char  *port = cg_widget_list_item(cmb_port, cg_widget_value(cmb_port));
    const char  *btext = cg_widget_list_item(cmb_baud, cg_widget_value(cmb_baud));
    int          baud = 0;

    if (btext) sscanf(btext, "%d", &baud);      /* 下拉框里存的是文字，转成数字 */

    if (s_biz.n_ports <= 0 || !port || !*port) {
        term_sys(app, "没有可用串口。插上设备后点「刷新」。");
        return;
    }
    if (baud <= 0) {
        term_sys(app, "波特率没选对，先在右边的下拉框里挑一个。");
        return;
    }

    s_biz.ser = cg_serial_open(port, baud);
    if (!s_biz.ser) {
        /* 打开失败时最要紧的是"为什么"。cg_serial_last_error(NULL) 给的就是最近一次
         * 打开失败的中文原因（端口被占用 / 找不到 / 波特率不支持…），别只报一句"失败了"。 */
        term_sys(app, cg_serial_last_error(NULL));
        cg_status_set(cg_find(app, "sb_main"), 0, "打开失败");
        return;
    }

    s_biz.rx_total  = 0;
    s_biz.tx_total  = 0;
    s_biz.rx_window = 0;
    s_biz.ticks     = 0;
    set_connected(app, 1);
    refresh_stats(app);
    {
        char *line = cg_str_format("已连接 %s @ %d（8N1）。每 %dms 取一次数据。", port, baud, POLL_MS);
        if (line) {
            term_sys(app, line);
            cg_free(line);
        }
    }
    if (s_biz.ini[0]) {
        cg_ini_write(s_biz.ini, "串口", "端口", port);
        cg_ini_write(s_biz.ini, "串口", "波特率", btext);
    }
    start_poll(app);
}

/**
 * @brief 断开串口：撤掉轮询、关掉链路、刷界面
 * @param app 所属应用
 * @return void
 * @note cg_serial_close() 会等读线程真的退出再释放缓冲，所以这里不需要自己判"还在不在"。
 */
static void do_disconnect(cg_app_t *app)
{
    stop_poll();
    cg_serial_close(s_biz.ser);
    s_biz.ser = NULL;
    set_connected(app, 0);
    refresh_stats(app);
    term_sys(app, "已断开串口。");
}

/**
 * @brief 发送输入框里的内容：按「HEX 发送」勾选决定原文还是十六进制
 * @param app 所属应用
 * @return void
 * @note 没有连接、输入为空、HEX 写错，都在这里给出人话提示，不让按钮"点了没反应"。
 */
static void send_current(cg_app_t *app)
{
    cg_widget_t *edt = cg_find(app, "edt_send");
    const char  *text = cg_widget_text(edt);

    if (!text || !*text) {
        term_sys(app, "输入框是空的，没东西可发。");
        return;
    }
    if (!s_biz.ser) {
        term_sys(app, "还没连上串口：先点「打开串口」。");
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
        if (cg_serial_write(s_biz.ser, bytes, n) < 0) {
            term_sys(app, cg_serial_last_error(s_biz.ser));
            return;
        }
        /* 日志里记一笔实际发出去的字节（原样按字节灌，终端会按 \n 切行） */
        cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, bytes, n);
    } else {
        int len = (int)strlen(text);

        if (cg_serial_write(s_biz.ser, text, len) < 0) {
            term_sys(app, cg_serial_last_error(s_biz.ser));
            return;
        }
        cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, text, len);
    }
    refresh_stats(app);
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 「打开串口 / 关闭串口」按钮：一个按钮管两件事
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 自检（--selftest）会合成触发它，所以这里必须先确认真的开了窗 ——
 *       自检模式下绝不能去碰硬件。
 */
static void on_connect_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    if (s_biz.ser) do_disconnect(app);
    else          do_connect(app);
}

/**
 * @brief 「刷新」按钮：重新枚举本机串口
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_refresh_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       n;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    n = fill_ports(app);
    if (n > 0) {
        char *line = cg_str_format("枚举到 %d 个串口。", n);
        if (line) {
            term_sys(app, line);
            cg_free(line);
        }
    }
}

/**
 * @brief 「清空」按钮：丢掉还没取走的接收数据，并清空日志视图
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

    cg_serial_flush_rx(s_biz.ser);           /* NULL 安全：没连上时是个空操作 */
    cg_term_clear(cg_find(app, "term_log"));
    s_biz.rx_window = 0;
    s_biz.ticks     = 0;
    term_sys(app, "已清空接收缓冲与日志视图。");
}

/**
 * @brief 「保存日志」按钮：把日志视图存成文本文件
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_save_log_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;      /* 自检别弹"另存为" */

    path[0] = '\0';
    if (!cg_save_file_dialog(self, "保存日志", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             "txt", path, sizeof(path))) {
        return;
    }
    if (cg_term_save(cg_find(app, "term_log"), path) != 0) {
        term_sys(app, "日志没写出去（路径不可写或被占用）。");
        return;
    }
    {
        char *line = cg_str_format("日志已存到 %s", path);
        if (line) {
            term_sys(app, line);
            cg_free(line);
        }
    }
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
    cg_log(CG_LOG_INFO, "串口助手：窗口关闭");
}

/**
 * @brief 窗口已销毁：进程收尾 —— 撤轮询、关串口
 * @param self 窗口伪控件
 * @param ev destroy
 * @param user 未使用
 * @return void
 */
static void on_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;

    stop_poll();                     /* 定时器可能还在飞，先撤掉再收串口 */
    cg_serial_close(s_biz.ser);      /* 幂等 + NULL 安全，没连上时直接调也没事 */
    s_biz.ser = NULL;
    cg_log(CG_LOG_INFO, "串口助手退出：RX %lld / TX %lld 字节", s_biz.rx_total, s_biz.tx_total);
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面就绪：枚举端口、恢复上次的配置、点亮初始状态
 * @param app 所属应用
 * @param user desc.ready_user，本示例没挂数据
 * @return void
 * @note **自检模式（--selftest）在这里直接返回**：自检会合成触发每个回调，
 *       要是在这里枚举端口、挂定时器、弹对话框，CI 就卡住了。
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *cht;
    char        *dir, *ini;

    (void)user;

    if (!cg_app_has_window(app)) return;          /* --selftest：不开串口、不弹框 */

    /* 配置文件放在 %LOCALAPPDATA%\serialterm\serialterm.ini */
    dir = cg_app_data_dir();
    ini = dir ? cg_path_join(dir, "serialterm.ini") : NULL;
    if (ini) snprintf(s_biz.ini, sizeof(s_biz.ini), "%s", ini);
    cg_free(ini);
    cg_free(dir);

    cht = cg_find(app, "cht_rate");
    if (cht) cg_chart_set_range(cht, 0, CHART_TOP_MIN);
    s_biz.chart_top = CHART_TOP_MIN;

    /* 枚举串口；上次用的那个还在的话，帮用户选回来 */
    if (fill_ports(app) > 0 && s_biz.ini[0]) {
        char *last = cg_ini_read(s_biz.ini, "串口", "端口", NULL);
        if (last && *last) {
            int idx = combo_find(cg_find(app, "cmb_port"), last);
            if (idx >= 0) cg_widget_set_value(cg_find(app, "cmb_port"), idx);
        }
        cg_free(last);
    }
    if (s_biz.ini[0]) {
        char *lastbaud = cg_ini_read(s_biz.ini, "串口", "波特率", NULL);
        char *split = cg_ini_read(s_biz.ini, "布局", "分隔条", NULL);
        if (lastbaud) {
            int idx = combo_find(cg_find(app, "cmb_baud"), lastbaud);
            if (idx >= 0) cg_widget_set_value(cg_find(app, "cmb_baud"), idx);
        }
        if (split) {
            int pos = 0;
            sscanf(split, "%d", &pos);
            cg_splitter_set(cg_find(app, "sp_main"), pos);
        }
        cg_free(lastbaud);
        cg_free(split);
    }

    set_connected(app, 0);
    refresh_stats(app);
    cg_log(CG_LOG_INFO, "串口助手启动：枚举到 %d 个串口，配置 %s",
           s_biz.n_ports, s_biz.ini[0] ? s_biz.ini : "(无)");
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 serialterm.cgui 里） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_connect_click",  on_connect_click),
    CG_BIND("on_refresh_click",  on_refresh_click),
    CG_BIND("on_clear_click",    on_clear_click),
    CG_BIND("on_save_log_click", on_save_log_click),
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
                                       --selftest 也会走它，正好验证"自检不开串口"那个守卫 */

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "serialterm.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "serialterm.cgui";
    return cg_app_run(&desc);
}
