/**
 * @file    main.c
 * @brief   串口助手示例：串口组件（无界面控件 serial）+ "数据到达"事件的样板
 * @details 这是"跟板子说话"那类工具的样板：界面全在 serialterm.cgui 里，这里只有行为。
 *
 *          **这个示例想讲清楚的是"数据从哪来、落到哪去由谁决定"**：
 *            - 串口是 .cgui 里的一个**无界面控件**（type: "serial"，id: "ser_main"）：
 *              像易语言的串口组件 —— 画布上是个图标，运行时不建窗口，只带着
 *              端口 / 波特率 / 数据位 / 停止位 / 校验这组参数；
 *            - 链路内部照旧是"读线程 + 64KB 环形缓冲"：读线程只搬字节、**绝不碰控件**，
 *              这是 Win32 的硬约束，不是风格偏好；
 *            - 运行时在主线程里发现缓冲有数据就派发 **arrive（数据到达）** 事件，
 *              回调拿到的 self 就是串口组件本身 —— 数据落到哪个控件，由这行代码决定：
 *              本示例把同一份数据**同时**灌进 edt_recv（接收编辑框）和 chart（速率曲线），
 *              换个项目灌进 listbox / label / 自己的解析器，都是这个回调里的事；
 *            - 1 秒一颗的业务定时器只管"聚合"：把这一秒收到的字节数推上曲线、刷状态栏，
 *              它不碰数据（数据已经在 arrive 里取走了）。
 *
 *          窗口拉伸时的**余量分配**（.cgui 的 grow 字段）：整份界面里只有 edt_recv 写了
 *          `grow`（权重 1）—— 它是唯一"该吃余量"的控件：窗口拉高时接收编辑框变高（下面的
 *          分隔条 / 发送区 / 曲线跟着整体下移，状态栏仍贴着窗口底边），拉宽时它变宽；
 *          按钮、标签、曲线、状态栏都是固定尺寸，一律不写 grow。
 *
 *          几个新手会踩的点，这里都写对了：
 *            1. cg_app_selftest 会**合成触发每个回调**（包括 arrive），所以回调第一件事
 *               就是判 cg_widget_serial() 是不是 NULL —— 自检模式下链路不存在，直接返回；
 *               凡是开串口、弹对话框的地方也先问 cg_app_has_window()；
 *            2. 发送按「HEX 发送」勾选决定走原文还是十六进制；转换是手写的 —— 不用 strtok
 *               （它会就地改原串、还要维护状态），而且"1A2B"这种没分隔符的也得算对；
 *            3. 链路的开关、销毁全归运行时管（app 退出时自动收），业务代码不用自己兜底；
 *            4. 打开失败别只报"失败了"：cg_serial_last_error(NULL) 给的是中文原因
 *               （端口被占用 / 找不到 / 波特率不支持），直接写进日志视图，这是可用性的关键。
 *
 *          编译：在项目根跑 build-app.bat serialterm
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** 聚合周期（毫秒）：每秒往曲线上推一个点、刷一次状态栏。取数据不靠它 */
#define AGG_MS         1000
/** 一拍最多取多少字节 */
#define RX_CHUNK       4096
/** 一次到达事件最多取几轮（防"数据一直比界面快"把事件拖久；剩下的下一拍再来） */
#define RX_MAX_CHUNKS  32
/** 接收编辑框里最多留多少字符（环形：超了从最老的地方丢） */
#define RECV_CAP       65000
/** 曲线 Y 轴的最小上限，也是初始上限（和 .cgui 里 chart 的 max 对齐） */
#define CHART_TOP_MIN  256

/** 业务状态：曲线聚合量与累计计数（链路句柄在串口组件上，这里不存） */
typedef struct {
    char         ini[1200];   /**< 配置文件路径（记住上次的端口 / 波特率 / 分隔条） */
    int          n_ports;     /**< 上次枚举到几个串口（0 = 没有，别拿占位文字去开） */
    int          rx_window;   /**< 这一秒内收到的字节数 */
    int          chart_top;   /**< 曲线 Y 轴当前上限（只往上扩） */
    long long    rx_total;    /**< 累计收到字节（断连后还要显示，所以自己留一份） */
    long long    tx_total;    /**< 累计发出字节 */
    char         recv_buf[RECV_CAP + 1];  /**< 接收编辑框的内容（环形，超了丢最老） */
    int          recv_len;    /**< recv_buf 里当前有效长度 */
} biz_t;

static biz_t s_biz;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 把一段文本追加进接收编辑框（edt_recv），超容量从最老处丢，并滚到底
 * @param app 所属应用
 * @param data 要追加的字节（当成文本，UTF-8）
 * @param n 字节数
 * @return void
 * @note 这就是"数据落到编辑框"的具体写法：自己维护一个环形缓冲，每次 set_text 整段重设。
 *       set_text 会把编辑框滚回顶部，所以后面跟一个 cg_edit_scroll_end 把新数据露出来。
 */
static void recv_append(cg_app_t *app, const char *data, int n)
{
    cg_widget_t *edt = cg_find(app, "edt_recv");

    if (n <= 0) return;
    /* 环形：装不下就丢最老的那截 */
    if (s_biz.recv_len + n > RECV_CAP) {
        int drop = s_biz.recv_len + n - RECV_CAP;
        if (drop >= s_biz.recv_len) {
            s_biz.recv_len = 0;
        } else {
            memmove(s_biz.recv_buf, s_biz.recv_buf + drop, (size_t)(s_biz.recv_len - drop));
            s_biz.recv_len -= drop;
        }
    }
    memcpy(s_biz.recv_buf + s_biz.recv_len, data, (size_t)n);
    s_biz.recv_len += n;
    s_biz.recv_buf[s_biz.recv_len] = '\0';

    if (edt) {
        cg_widget_set_text(edt, s_biz.recv_buf);
        cg_edit_scroll_end(edt);             /* set_text 会滚回顶部，这里救回来 */
    }
}

/**
 * @brief 往接收编辑框补一条"程序自己记的"系统消息（行首 --）
 * @param app 所属应用
 * @param text 要写的中文（UTF-8）
 * @return void
 */
static void log_line(cg_app_t *app, const char *text)
{
    char tmp[2048];
    int  n = snprintf(tmp, sizeof(tmp), "\n-- %s", text);

    if (n < 0) return;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    recv_append(app, tmp, n);
}

/**
 * @brief 取本应用的串口组件（.cgui 里的 ser_main）
 * @param app 所属应用
 * @return cg_widget_t* 串口组件；.cgui 里没写返回 NULL
 */
static cg_widget_t *ser_of(cg_app_t *app)
{
    return cg_find(app, "ser_main");
}

/**
 * @brief 当前是否连着（链路句柄在组件上，开着就算连着）
 * @param app 所属应用
 * @return int 1 = 已连接
 */
static int is_connected(cg_app_t *app)
{
    return cg_widget_serial(ser_of(app)) != NULL;
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

/* 原来这里有一段手写的 hex_parse()，serialterm 与 netterm 里各有一份、逐字相同 ——
 * 典型的"同一事实有两份"。现在它搬进了运行时：**cg_hex_decode()**（cg_kit 的「编码」一组，
 * 见画布「支持库」页签）。语义一字未改：空格/制表/换行/逗号/连字符都当分隔符、
 * "1A2B" 这种不写分隔的也算对、只剩半个字节或非法字符返回 -1、放不下返回 -2。 */

/* ------------------------------------------------------------ 界面刷新 */

/**
 * @brief 把累计收发的字节数刷到状态栏第 2、3 段
 * @param app 所属应用
 * @return void
 * @note 累计量是业务自己记的（arrive / 发送时往里加）：链路断了句柄就没了，
 *       状态栏不能因此突然归零，让人以为数据丢了。
 */
static void refresh_stats(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

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
        log_line(app, "没枚举到任何串口。插上设备后点「刷新」再试。");
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

/* ------------------------------------------------------------ 数据到达（落点在这里定） */

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
 * @brief **数据到达**：串口组件收到数据了，把缓冲取空、决定数据去哪
 * @param self 串口组件本身（.cgui 里的 ser_main）
 * @param ev arrive
 * @param user 未使用
 * @return void
 * @note 这就是"用户自己决定数据传输到哪个控件"的地方：同一份数据，想灌几个控件
 *       就灌几个 —— 本示例同时喂 edt_recv（接收编辑框）和 rx_window（速率曲线）。
 *       **第一件事是判句柄 NULL**：自检模式会合成触发本回调，那时链路不存在。
 */
static void on_arrive(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t     *app  = cg_app_of(self);
    cg_serial_t  *ser;
    unsigned char buf[RX_CHUNK];
    int           loops, n;

    (void)ev;
    (void)user;

    ser = cg_widget_serial(self);
    if (!ser) return;                       /* 自检合成触发：链路不存在，直接走 */

    for (loops = 0; loops < RX_MAX_CHUNKS; loops++) {
        if (cg_serial_available(ser) <= 0) break;
        n = cg_serial_read(ser, buf, (int)sizeof(buf));
        if (n <= 0) break;
        /* ---- 数据的落点：一份数据，两个去处 ---- */
        recv_append(app, (const char *)buf, n);     /* 去处 1：接收编辑框（当作文本灌进去） */
        s_biz.rx_window += n;                      /* 去处 2：速率曲线（1 秒聚一次） */
        s_biz.rx_total  += n;
    }
    refresh_stats(app);
}

/**
 * @brief 每秒的聚合拍：推曲线点、刷状态栏（它不取数据，数据 arrive 已经取走了）
 * @param user 挂进去的 cg_app_t
 * @return void
 * @note cg_set_timer 是一次性的，所以每次结束都要再挂一次，这才是"周期"
 *       （回调里重挂的定时器同一拍绝不会再触发 —— 那个死循环是 cg_timer.c 的
 *       2026-09 修复，别改回去）。
 */
static void agg_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    push_rate_point(app);
    refresh_stats(app);
    cg_set_timer(app, AGG_MS, agg_tick, app);
}

/* ------------------------------------------------------------ 连接 / 发送 */

/**
 * @brief 打开串口（走串口组件），失败把中文原因写进日志视图
 * @param app 所属应用
 * @return void
 */
static void do_connect(cg_app_t *app)
{
    cg_widget_t *ser      = ser_of(app);
    cg_widget_t *cmb_port = cg_find(app, "cmb_port");
    cg_widget_t *cmb_baud = cg_find(app, "cmb_baud");
    const char  *port = cg_widget_list_item(cmb_port, cg_widget_value(cmb_port));
    const char  *btext = cg_widget_list_item(cmb_baud, cg_widget_value(cmb_baud));
    int          baud = 0;

    if (btext) sscanf(btext, "%d", &baud);      /* 下拉框里存的是文字，转成数字 */

    if (s_biz.n_ports <= 0 || !port || !*port) {
        log_line(app, "没有可用串口。插上设备后点「刷新」。");
        return;
    }
    if (!ser) {
        log_line(app, ".cgui 里没有串口组件（ser_main），这是界面文件的问题。");
        return;
    }
    if (baud <= 0) {
        log_line(app, "波特率没选对，先在右边的下拉框里挑一个。");
        return;
    }

    /* 波特率是界面上选的，先写进组件再开链路；端口也交给组件记着 */
    cg_widget_serial_set_baud(ser, baud);
    if (cg_widget_serial_open(ser, port) != 0) {
        /* 打开失败时最要紧的是"为什么"。cg_serial_last_error(NULL) 给的是最近一次
         * 打开失败的中文原因（端口被占用 / 找不到 / 波特率不支持…），别只报一句"失败了"。 */
        log_line(app, cg_serial_last_error(NULL));
        cg_status_set(cg_find(app, "sb_main"), 0, "打开失败");
        return;
    }

    s_biz.rx_window = 0;
    set_connected(app, 1);
    refresh_stats(app);
    {
        char *line = cg_str_format("已连接 %s @ %d（8N1）。数据到达即取，灌进接收编辑框。",
                                   port, baud);
        if (line) {
            log_line(app, line);
            cg_free(line);
        }
    }
    if (s_biz.ini[0]) {
        cg_ini_write(s_biz.ini, "串口", "端口", port);
        cg_ini_write(s_biz.ini, "串口", "波特率", btext);
    }
}

/**
 * @brief 断开串口：关掉组件上的链路、刷界面
 * @param app 所属应用
 * @return void
 */
static void do_disconnect(cg_app_t *app)
{
    cg_widget_serial_close(ser_of(app));
    set_connected(app, 0);
    refresh_stats(app);
    log_line(app, "已断开串口。");
}

/**
 * @brief 把"已发出"的内容记进接收编辑框（行首 >>）
 * @param app 所属应用
 * @param show 要显示的文字（文本模式就是原文；HEX 模式是已经拼好的 01 02 这样的串）
 * @param n show 的长度
 * @return void
 */
static void log_sent(cg_app_t *app, const char *show, int n)
{
    char tmp[1200];
    int  k = snprintf(tmp, sizeof(tmp), "\n>> %.*s", n, show);

    if (k < 0) return;
    if (k > (int)sizeof(tmp) - 1) k = (int)sizeof(tmp) - 1;
    recv_append(app, tmp, k);
}

/**
 * @brief 发送输入框里的内容：按「HEX 发送」勾选决定原文还是十六进制
 * @param app 所属应用
 * @return void
 * @note 没有连接、输入为空、HEX 写错，都在这里给出人话提示，不让按钮"点了没反应"。
 */
static void send_current(cg_app_t *app)
{
    cg_serial_t  *ser = cg_widget_serial(ser_of(app));
    cg_widget_t  *edt = cg_find(app, "edt_send");
    const char   *text = cg_widget_text(edt);

    if (!text || !*text) {
        log_line(app, "输入框是空的，没东西可发。");
        return;
    }
    if (!ser) {
        log_line(app, "还没连上串口：先点「打开串口」。");
        return;
    }

    if (cg_widget_checked(cg_find(app, "chk_hex"))) {
        unsigned char bytes[512];
        int           n = cg_hex_decode(text, bytes, (int)sizeof(bytes));

        if (n == -2) {
            log_line(app, "HEX 数据太长了（一次最多 512 字节）。");
            return;
        }
        if (n < 0) {
            log_line(app, "HEX 写错了：只允许 0-9 A-F 和分隔符，而且数字个数必须是偶数（像 1A 2B）。");
            return;
        }
        if (n == 0) {
            log_line(app, "HEX 模式没解析出任何字节。");
            return;
        }
        if (cg_serial_write(ser, bytes, n) < 0) {
            log_line(app, cg_serial_last_error(ser));
            return;
        }
        s_biz.tx_total += n;
        {                                   /* 记一笔实际发出的字节（按 HEX 显示） */
            char hex[1200];
            int  hi = 0, cap = (int)sizeof(hex) - 1, i;
            for (i = 0; i < n && hi < cap - 3; i++)
                hi += snprintf(hex + hi, (size_t)(cap - hi), "%02X ", bytes[i]);
            log_sent(app, hex, hi);
        }
    } else {
        int len = (int)strlen(text);

        if (cg_serial_write(ser, text, len) < 0) {
            log_line(app, cg_serial_last_error(ser));
            return;
        }
        s_biz.tx_total += len;
        log_sent(app, text, len);
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

    if (is_connected(app)) do_disconnect(app);
    else                   do_connect(app);
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
            log_line(app, line);
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

    cg_serial_flush_rx(cg_widget_serial(ser_of(app)));   /* NULL 安全：没连上时空操作 */
    s_biz.recv_len = 0;
    s_biz.recv_buf[0] = '\0';
    cg_widget_set_text(cg_find(app, "edt_recv"), "");    /* 清空接收编辑框 */
    s_biz.rx_window = 0;
    log_line(app, "已清空接收缓冲与编辑框。");
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
    {
        const char *txt = cg_widget_text(cg_find(app, "edt_recv"));

        if (cg_file_write(path, txt ? txt : "") != 0) {
            log_line(app, "日志没写出去（路径不可写或被占用）。");
            return;
        }
    }
    {
        char *line = cg_str_format("日志已存到 %s", path);
        if (line) {
            log_line(app, line);
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
        log_line(app, "HEX 发送：关。输入按原文（UTF-8）发出去。");
        return;
    }

    log_line(app, "HEX 发送：开。输入像 1A 2B 2C 这样写，空格可有可无。");
    {
        const char *text = cg_widget_text(cg_find(app, "edt_send"));
        if (text && *text) {
            unsigned char bytes[512];
            int           n = cg_hex_decode(text, bytes, (int)sizeof(bytes));
            char         *line;
            if (n < 0) {
                log_line(app, "（当前输入解析不了：数字个数要是偶数，而且只允许 0-9 A-F）");
                return;
            }
            line = cg_str_format("（当前输入会发成 %d 个字节）", n);
            if (line) {
                log_line(app, line);
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
 * @brief 窗口即将关闭：这里只记一笔（链路由运行时收，业务不用兜底）
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
 * @brief 窗口已销毁：只记账本（串口链路运行时在 app 收尾时自动关，这里不碰）
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
    cg_log(CG_LOG_INFO, "串口助手退出：RX %lld / TX %lld 字节", s_biz.rx_total, s_biz.tx_total);
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面就绪：枚举端口、恢复上次的配置、点亮初始状态、挂上聚合拍
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
    cg_set_timer(app, AGG_MS, agg_tick, app);     /* 聚合拍（取数据不靠它，见 on_arrive） */
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
    CG_BIND("on_arrive",         on_arrive),
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
