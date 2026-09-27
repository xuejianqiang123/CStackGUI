/**
 * @file    main.c
 * @brief   串口助手示例：串口组件（无界面控件 serial）+ "数据到达"事件的样板
 * @details 这是"跟板子说话"那类工具的样板：界面全在 serialterm.cgui 里，这里只有行为。
 *
 *          **功能上对标 XCOM（正点原子那款，现场用得最多）**，抄它好用的地方，其余用框架的强项补：
 *            - 抄：16 进制显示 / 时间戳 / 自动保存（日志三件套）、单条发送 + HEX + 发送新行、
 *                  **定时发送**、**多条发送（一行一条 + 循环）**、打开文件 / 发送文件、停止发送、
 *                  保存窗口 / 清除接收 / 收发计数；
 *            - 不抄：XCOM 的「协议传输」标签页是**厂商私有 IAP 协议**（地址/功能码/帧序列那套）。
 *                  通用件框架已经有了：`cg_crc16`（校验）+ `cg_modbus_*`（组帧解帧）+
 *                  `cg_frame_*`（切帧），要什么协议就组什么帧，不必绑在某一家的格式上；
 *            - **反超**：XCOM 是"读到什么就显示什么"，**不切帧**。这里用 `cg_frame_*` 的
 *                  「帧间静默」把字节流切成整帧，每帧单独一行并报长度 + CRC16 结论 ——
 *                  通信调试里最想要的那一眼。
 *
 *          **这个示例还想讲清楚"数据从哪来、落到哪去由谁决定"**：
 *            - 串口是 .cgui 里的一个**无界面控件**（type: "serial"，id: "ser_main"）：像易语言的
 *              串口组件 —— 画布上是个图标，运行时不建窗口，只带着端口 / 波特率这组参数；
 *            - 链路内部照旧是"读线程 + 64KB 环形缓冲"：读线程只搬字节、**绝不碰控件**，
 *              这是 Win32 的硬约束，不是风格偏好；
 *            - 运行时在主线程里发现缓冲有数据就派发 **arrive（数据到达）** 事件，回调拿到的 self
 *              就是串口组件本身 —— 数据落到哪个控件由这行代码决定。本示例一份数据三个去处：
 *              日志视图（terminal）、按帧切分（cg_frame_*）、自动保存（落盘），
 *              外加速率曲线（1 秒聚合一次）。
 *
 *          窗口拉伸时的**余量分配**（.cgui 的 grow 字段）：整份界面里只有 term_log 写了
 *          `grow`（权重 1）—— 它是唯一"该吃余量"的控件；其余控件与状态栏都是固定尺寸。
 *
 *          几个新手会踩的点，这里都写对了：
 *            1. cg_app_selftest 会**合成触发每个回调**（包括 arrive），所以回调第一件事就是判
 *               cg_widget_serial() 是不是 NULL —— 自检模式下链路不存在，直接返回；
 *               凡是开串口、弹对话框的地方也先问 cg_app_has_window()；
 *            2. 发送按「HEX 发送」勾选决定走原文还是十六进制，**转换只在一处**（prepare_send）：
 *               单条 / 多条 / 文件 / 演示模式共用它，不会出现"某条路径忘了加新行"这种错；
 *            3. 链路的开关、销毁全归运行时管（app 退出时自动收），业务代码不用自己兜底；
 *            4. 打开失败别只报"失败了"：cg_serial_last_error(NULL) 给的是中文原因
 *               （端口被占用 / 找不到 / 波特率不支持），直接写进日志视图。
 *
 *          自测（不需要真设备）：`serialterm.exe --demo` —— 不开串口，把几批合成字节灌进接收管线，
 *          并把"切出的帧 / CRC 结论 / 发送侧解析出的字节"打到 stdout（端到端要有能断言的文字）。
 *
 *          编译：在项目根跑 build-app.bat serialterm
 */
#include "cstackgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 聚合周期（毫秒）：每秒往曲线上推一个点、刷一次状态栏、顺手让切帧器收尾 */
#define AGG_MS         1000
/** 一拍最多取多少字节 */
#define RX_CHUNK       4096
/** 一次到达事件最多取几轮（防"数据一直比界面快"把事件拖久；剩下的下一拍再来） */
#define RX_MAX_CHUNKS  32
/** 曲线 Y 轴的最小上限，也是初始上限（和 .cgui 里 chart 的 max 对齐） */
#define CHART_TOP_MIN  256
/** 单次发送的字节上限（HEX 输入解出来 / 文本 / 文件都按它卡） */
#define SEND_MAX       4096
/** 「按帧显示」时，一帧最长这么大（超过就当噪声丢掉并计数） */
#define FRAME_MAX      1024
/** 自动保存时每行最多记多少字节（一行太长没法看） */
#define SAVE_LINE_MAX  512

/** prepare_send 的结果码 */
#define PREP_EMPTY    (-1)     /**< 没内容可发 */
#define PREP_BADHEX   (-2)     /**< HEX 写错了 */
#define PREP_TOOLONG  (-3)     /**< 超过 SEND_MAX */

/** 业务状态：曲线聚合量、累计计数、切帧器、发送调度（链路句柄在串口组件上，这里不存） */
typedef struct {
    char          ini[1200];      /**< 配置文件路径（记住上次的端口 / 波特率 / 分隔条） */
    int           n_ports;        /**< 上次枚举到几个串口（0 = 没有，别拿占位文字去开） */
    int           rx_window;      /**< 这一秒内收到的字节数 */
    int           chart_top;      /**< 曲线 Y 轴当前上限（只往上扩） */
    long long     rx_total;       /**< 累计收到字节（断连后还要显示，所以自己留一份） */
    long long     tx_total;       /**< 累计发出字节 */

    int           frame_on;       /**< 「按帧显示」开关 */
    cg_frame_t    fr;             /**< 切帧器（静默模式） */
    unsigned char frame_buf[FRAME_MAX * 2];   /**< 切帧器攒字节的缓冲 */
    unsigned long frame_no;       /**< 已经报出多少帧（日志里编号用） */

    int           autosave_on;    /**< 「自动保存」开关 */
    char          autosave_path[1200];

    cg_timer_t   *sched;          /**< 发送调度定时器（0 = 没在发） */
    int           sched_multi;    /**< 0 = 定时发单条；1 = 多条依次 */
    int           sched_loop;     /**< 多条是否循环 */
    int           sched_idx;      /**< 多条发到第几条 */
    int           sched_iv;       /**< 周期（毫秒） */
} biz_t;

static biz_t s_biz;

/** --demo / CG_SERIALTERM_DEMO=1：不开真串口，灌合成数据并把结论打到 stdout（给 CI 用） */
static int s_demo;
/** --demo=COM4 / CG_SERIALTERM_DEMO=COM4：**真开这个口**，走一遍"界面选参数 → 打开 → 发 → 收" */
static char s_demo_port[64];

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 取日志视图控件
 * @param app 所属应用
 * @return cg_widget_t* terminal 控件；.cgui 里没有返回 NULL（cg_term_* 都 NULL 安全）
 */
static cg_widget_t *term_of(cg_app_t *app)
{
    return cg_find(app, "term_log");
}

/**
 * @brief 往日志视图写一条"程序自己记的"消息（行首 --，灰色）
 * @param app 所属应用
 * @param text 要写的中文（UTF-8）
 * @return void
 * @note 长度传 -1 就是"按字符串取长度"（cg_term_write 的约定）。
 */
static void sys_line(cg_app_t *app, const char *text)
{
    if (!text || !*text) return;
    cg_term_write(term_of(app), CG_TERM_SYS, text, -1);
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

/**
 * @brief 按配置里的文字恢复一个下拉（找不到 / 没配就保持 .cgui 的初值）
 * @param app 所属应用
 * @param id 控件 id
 * @param key 配置节 [串口] 里的键名
 * @return void
 * @note 帧格式那三个下拉也用它：板子换一次就得重选一次的话，谁都烦。
 */
static void combo_restore(cg_app_t *app, const char *id, const char *key)
{
    char *v;

    if (!s_biz.ini[0]) return;
    v = cg_ini_read(s_biz.ini, "串口", key, NULL);
    if (v && *v) {
        int idx = combo_find(cg_find(app, id), v);
        if (idx >= 0) cg_widget_set_value(cg_find(app, id), idx);
    }
    cg_free(v);
}

/** 原来这里有一段手写的 hex_parse()，serialterm 与 netterm 里各有一份、逐字相同 ——
 *  典型的"同一事实有两份"。现在它搬进了运行时：**cg_hex_decode()**（cg_kit 的「编码」一组）。
 *  语义一字未改：空格/制表/换行/逗号/连字符都当分隔符、"1A2B" 这种不写分隔的也算对、
 *  只剩半个字节或非法字符返回 -1、放不下返回 -2。 */

/* ------------------------------------------------------------ 界面刷新 */

/**
 * @brief 把累计收发的字节数、切帧统计刷到状态栏第 2~4 段
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
    cg_status_setf(sb, 3, "%lu 帧 / 丢 %lu B",
                   (unsigned long)cg_frame_frames(&s_biz.fr),
                   (unsigned long)cg_frame_dropped(&s_biz.fr));
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
        sys_line(app, "没枚举到任何串口。插上设备后点「刷新」再试。");
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

/* ------------------------------------------------------------ 按帧显示（XCOM 没有的那一条） */

/**
 * @brief 按「静默间隔」重建切帧器（勾选框 / 间隔一改就重建）
 * @param app 所属应用
 * @return void
 * @note 模式是**帧间静默**：隔 gap_ms 没有新字节就算一帧结束 —— 这正是 Modbus/RTU 的 t3.5。
 *       攒字节的缓冲由我们自己给（框架不 malloc）；一帧上限 FRAME_MAX，超了整帧丢掉并计数。
 */
static void framer_reset(cg_app_t *app)
{
    int gap = cg_widget_value(cg_find(app, "spn_gap"));

    if (gap < 1) gap = 1;
    if (cg_frame_init(&s_biz.fr, s_biz.frame_buf, (int)sizeof(s_biz.frame_buf),
                      CG_FRAME_SILENCE, gap) == 0) {
        cg_frame_limits(&s_biz.fr, 2, FRAME_MAX);
    }
}

/**
 * @brief 报一帧：长度、内容（最多 32 字节）、以及帧尾两字节是不是 CRC16
 * @param app 所属应用
 * @param frame 帧首地址
 * @param len 帧长
 * @return void
 * @note CRC16 那两行是**通用**的判据（Modbus/RTU 用的是同一个多项式）：能对上说明这帧
 *       大概率完整（静默切帧在噪声粘前面时切不出真正的帧，只能靠它认出来）。
 *       太短的帧不判 CRC，免得报一个没意义的"通过"。
 */
static void frame_report(cg_app_t *app, const unsigned char *frame, int len)
{
    char hex[3 * 32 + 8];
    char line[512];
    int  shown = len > 32 ? 32 : len;

    cg_hex_encode(frame, shown, " ", 1, hex, (int)sizeof(hex));
    s_biz.frame_no++;

    if (len < 3) {
        snprintf(line, sizeof(line), "帧 #%lu：%d 字节 [%s%s] （太短，不判 CRC）",
                 s_biz.frame_no, len, hex, len > shown ? " …" : "");
    } else {
        unsigned crc = cg_crc16(frame, len - 2);
        int      ok  = (((crc & 0xFFu) ^ frame[len - 2]) == 0) &&
                       ((((crc >> 8) & 0xFFu) ^ frame[len - 1]) == 0);

        snprintf(line, sizeof(line), "帧 #%lu：%d 字节 [%s%s]  CRC16 %s（算出 %02X %02X / 帧尾 %02X %02X）",
                 s_biz.frame_no, len, hex, len > shown ? " …" : "",
                 ok ? "通过" : "不符",
                 crc & 0xFFu, (crc >> 8) & 0xFFu, frame[len - 2], frame[len - 1]);
    }
    cg_term_write(term_of(app), CG_TERM_SYS, line, -1);
    if (s_demo) {                       /* 演示模式把同一行也打到 stdout：CI 要有能断言的文字 */
        printf("[demo] %s\n", line);
        fflush(stdout);
    }
}

/**
 * @brief 把切帧器里已经完整的帧全部取出来报掉
 * @param app 所属应用
 * @return void
 */
static void drain_frames(cg_app_t *app)
{
    unsigned char frame[FRAME_MAX];
    int           flen;

    if (!s_biz.frame_on) return;
    while ((flen = cg_frame_take(&s_biz.fr, frame, (int)sizeof(frame))) > 0) {
        frame_report(app, frame, flen);
    }
}

/* ------------------------------------------------------------ 自动保存（XCOM 的「自动保存」） */

/**
 * @brief 把一段收到的字节按十六进制文本追加落盘
 * @param app 所属应用
 * @param data 收到的字节
 * @param n 字节数
 * @return void
 * @note **为什么存十六进制文本**：字节精确（每字节两位，不会像文本那样把 0x00 / 0xFF 搅坏）、
 *       还能直接 grep —— 现场排查"这条命令到底返回了什么"时比二进制文件好用。
 *       写失败（磁盘满 / 被占用）就把开关关掉并说一声，别每来一包刷一行。
 */
static void autosave_append(cg_app_t *app, const unsigned char *data, int n)
{
    char  hex[3 * SAVE_LINE_MAX + 16];
    char  line[3 * SAVE_LINE_MAX + 128];
    char *ts;
    int   take = n > SAVE_LINE_MAX ? SAVE_LINE_MAX : n;

    if (!s_biz.autosave_on || !s_biz.autosave_path[0] || n <= 0) return;

    cg_hex_encode(data, take, " ", 1, hex, (int)sizeof(hex));
    ts = cg_time_now("[%H:%M:%S]");
    snprintf(line, sizeof(line), "%s RX %s%s\n", ts ? ts : "", hex, n > take ? " ..." : "");
    cg_free(ts);

    if (cg_file_append(s_biz.autosave_path, line) != 0) {
        s_biz.autosave_on = 0;
        cg_widget_set_checked(cg_find(app, "chk_autosave"), 0);
        sys_line(app, "自动保存写不进去了（路径不可写 / 磁盘满），已关掉。");
    }
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
 * @brief **收到一段字节之后的落点**（日志 / 按帧 / 自动保存）——一份数据三个去处
 * @param app 所属应用
 * @param data 字节
 * @param n 字节数
 * @param now_ms 收到时刻（cg_now_ms()）
 * @return void
 * @note 把"落点"从 on_arrive 里拆出来是有原因的：演示模式（--demo）不碰真串口，
 *       但要走**同一条管线** —— 不然演示就掩盖了真实流程。
 */
static void intake(cg_app_t *app, const unsigned char *data, int n, unsigned long now_ms)
{
    if (n <= 0) return;

    /* 去处 1：日志视图（十六进制 / 时间戳 / 方向着色全由控件管，这里只灌字节） */
    cg_term_write(term_of(app), CG_TERM_RX, data, n);

    /* 去处 2：按帧切分（XCOM 没有的那一条）*/
    if (s_biz.frame_on) {
        cg_frame_feed(&s_biz.fr, data, n, now_ms);
        drain_frames(app);              /* 静默到点的帧，喂新字节时会顺手收尾 */
    }

    /* 去处 3：自动保存 */
    autosave_append(app, data, n);

    s_biz.rx_window += n;
    s_biz.rx_total  += n;
}

/**
 * @brief **数据到达**：串口组件收到数据了，把缓冲取空、决定数据去哪
 * @param self 串口组件本身（.cgui 里的 ser_main）
 * @param ev arrive
 * @param user 未使用
 * @return void
 * @note **第一件事是判句柄 NULL**：自检模式会合成触发本回调，那时链路不存在。
 */
static void on_arrive(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t     *app = cg_app_of(self);
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
        intake(app, buf, n, cg_now_ms());
    }
    refresh_stats(app);
}

/**
 * @brief 每秒的聚合拍：推曲线点、让切帧器收尾、刷状态栏
 * @param user 挂进去的 cg_app_t
 * @return void
 * @note cg_set_timer 是一次性的，所以每次结束都要再挂一次，这才是"周期"
 *       （回调里重挂的定时器同一拍绝不会再触发 —— 那个死循环是 cg_timer.c 的
 *       2026-09 修复，别改回去）。
 *       **收尾就靠这一拍**：对端只发了一帧就不再发时，没有新字节来触发空档判定，
 *       只有时钟能让它出来（cg_frame_tick 的用处）。
 */
static void agg_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    push_rate_point(app);
    if (s_biz.frame_on) {
        cg_frame_tick(&s_biz.fr, cg_now_ms());
        drain_frames(app);
    }
    refresh_stats(app);
    cg_set_timer(app, AGG_MS, agg_tick, app);
}

/* ------------------------------------------------------------ 连接 */

/**
 * @brief 打开串口（走串口组件），失败把中文原因写进日志视图
 * @param app 所属应用
 * @return void
 */
static void do_connect(cg_app_t *app)
{
    cg_widget_t *ser        = ser_of(app);
    cg_widget_t *cmb_port   = cg_find(app, "cmb_port");
    cg_widget_t *cmb_baud   = cg_find(app, "cmb_baud");
    cg_widget_t *cmb_bits   = cg_find(app, "cmb_bits");
    cg_widget_t *cmb_stop   = cg_find(app, "cmb_stop");
    cg_widget_t *cmb_parity = cg_find(app, "cmb_parity");
    const char  *port  = cg_widget_list_item(cmb_port, cg_widget_value(cmb_port));
    const char  *btext = cg_widget_list_item(cmb_baud, cg_widget_value(cmb_baud));
    const char  *bitxt = cg_widget_list_item(cmb_bits, cg_widget_value(cmb_bits));
    const char  *sttxt = cg_widget_list_item(cmb_stop, cg_widget_value(cmb_stop));
    const char  *ptext = cg_widget_list_item(cmb_parity, cg_widget_value(cmb_parity));
    int          baud = 0, bits = 8, stop = 1, parity;
    char         fmt[16];

    /* 下拉框里存的是文字，逐个转成数字；校验直接取下标（「无/奇/偶」= 0/1/2） */
    if (btext) sscanf(btext, "%d", &baud);
    if (bitxt) sscanf(bitxt, "%d", &bits);
    if (sttxt) sscanf(sttxt, "%d", &stop);
    parity = cg_widget_value(cmb_parity);
    if (parity < 0 || parity > 2) parity = 0;
    snprintf(fmt, sizeof(fmt), "%d%s%d", bits, parity == 1 ? "O" : (parity == 2 ? "E" : "N"), stop);

    if (s_biz.n_ports <= 0 || !port || !*port) {
        sys_line(app, "没有可用串口。插上设备后点「刷新」。");
        return;
    }
    if (!ser) {
        sys_line(app, ".cgui 里没有串口组件（ser_main），这是界面文件的问题。");
        return;
    }
    if (baud <= 0) {
        sys_line(app, "波特率没选对，先在右边的下拉框里挑一个。");
        return;
    }

    /* 端口、波特率、帧格式都是界面上选的：**先写进组件再开链路**。
     * 帧格式走 cg_widget_serial_set_format()，底层就是 cg_serial_open_ex() ——
     * 7E1 这类老设备把三个下拉选成一样就能通（组件形态**支持全参数**）。 */
    cg_widget_serial_set_baud(ser, baud);
    cg_widget_serial_set_format(ser, bits, stop, parity);
    if (cg_widget_serial_open(ser, port) != 0) {
        /* 打开失败时最要紧的是"为什么"。cg_serial_last_error(NULL) 给的是最近一次
         * 打开失败的中文原因（端口被占用 / 找不到 / 参数不支持…），别只报一句"失败了"。 */
        sys_line(app, cg_serial_last_error(NULL));
        cg_status_set(cg_find(app, "sb_main"), 0, "打开失败");
        return;
    }

    s_biz.rx_window = 0;
    set_connected(app, 1);
    refresh_stats(app);
    {
        char *line = cg_str_format("已连接 %s @ %d（%s）。数据到达即取：日志 / 按帧 / 自动保存三个去处。",
                                   port, baud, fmt);
        if (line) {
            sys_line(app, line);
            cg_free(line);
        }
    }
    if (s_biz.ini[0]) {
        cg_ini_write(s_biz.ini, "串口", "端口", port);
        cg_ini_write(s_biz.ini, "串口", "波特率", btext);
        cg_ini_write(s_biz.ini, "串口", "数据位", bitxt);
        cg_ini_write(s_biz.ini, "串口", "停止位", sttxt);
        cg_ini_write(s_biz.ini, "串口", "校验", ptext);
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
    sys_line(app, "已断开串口。");
}

/* ------------------------------------------------------------ 发送（单条 / 定时 / 多条） */

/**
 * @brief 把「输入框里的文字」变成「要发出去的字节」——**全示例只有这一处做这个转换**
 * @param text 输入的原文
 * @param hex 1 = 按十六进制解析（cg_hex_decode 的语义）
 * @param newline 1 = 末尾追加 CRLF（拆成两个字节加在最后）
 * @param out 输出缓冲
 * @param cap 输出缓冲大小
 * @return int > 0 = 要发的字节数；PREP_* 负数 = 失败原因
 * @note 单条发送、定时发送、多条依次、演示模式都走它：路径只有一条，就不会出现
 *       "多条发送忘了加新行"这种只在某条路上才有的错。
 *       cap 要留出 2 字节给 CRLF（函数自己会卡，不会越界）。
 */
static int prepare_send(const char *text, int hex, int newline, unsigned char *out, int cap)
{
    int n = 0;

    if (!text || !*text || cap < 3) return PREP_EMPTY;

    if (hex) {
        n = cg_hex_decode(text, out, cap - 2);
        if (n == -1) return PREP_BADHEX;
        if (n == -2) return PREP_TOOLONG;
        if (n <= 0) return PREP_EMPTY;
    } else {
        int len = (int)strlen(text);

        if (len > cap - 2) return PREP_TOOLONG;
        memcpy(out, text, (size_t)len);
        n = len;
    }

    if (newline) {
        out[n++] = '\r';
        out[n++] = '\n';
    }
    return n;
}

/**
 * @brief 把一段字节发出去，并把"发出去了"记进日志视图
 * @param app 所属应用
 * @param bytes 要发的字节
 * @param n 字节数
 * @return int 0 = 已发出；-1 = 没连上或写失败（原因已写进日志）
 * @note 日志里记的是**原始字节**：显示成 HEX 还是文本由日志控件自己的模式决定，
 *       所以我们不用在这里拼一遍十六进制文本（少一处"两份事实"）。
 */
static int send_bytes(cg_app_t *app, const unsigned char *bytes, int n)
{
    cg_serial_t *ser = cg_widget_serial(ser_of(app));

    if (n <= 0) return -1;
    if (!ser) {
        sys_line(app, "还没连上串口：先点「打开串口」。");
        return -1;
    }
    if (cg_serial_write(ser, bytes, n) < 0) {
        sys_line(app, cg_serial_last_error(ser));
        return -1;
    }
    s_biz.tx_total += n;
    cg_term_write(term_of(app), CG_TERM_TX, bytes, n);
    refresh_stats(app);
    return 0;
}

/**
 * @brief 把一条文字按当前勾选（HEX / 新行）发出去
 * @param app 所属应用
 * @param text 输入的原文
 * @return int 0 = 发出；-1 = 没发（原因已写进日志）
 */
static int send_text(cg_app_t *app, const char *text)
{
    unsigned char bytes[SEND_MAX];
    int           n = prepare_send(text, cg_widget_checked(cg_find(app, "chk_hex")),
                                   cg_widget_checked(cg_find(app, "chk_newline")),
                                   bytes, (int)sizeof(bytes));

    if (n == PREP_EMPTY) {
        sys_line(app, "输入是空的，没东西可发。");
        return -1;
    }
    if (n == PREP_BADHEX) {
        sys_line(app, "HEX 写错了：只允许 0-9 A-F 和分隔符，而且数字个数必须是偶数（像 1A 2B）。");
        return -1;
    }
    if (n == PREP_TOOLONG) {
        sys_line(app, "这一条太长了（一次最多 4096 字节）。");
        return -1;
    }
    return send_bytes(app, bytes, n);
}

/**
 * @brief 发「单条发送」框里的那一行
 * @param app 所属应用
 * @return int 0 = 发出；-1 = 没发
 */
static int send_single(cg_app_t *app)
{
    return send_text(app, cg_widget_text(cg_find(app, "edt_send")));
}

/**
 * @brief 从「多条发送」框里取第 idx 条（跳过空行与 # 注释行）
 * @param app 所属应用
 * @param idx 第几条（0 起，按"非空非注释行"计数）
 * @param out 输出缓冲
 * @param cap 缓冲大小
 * @return int 1 = 取到；0 = 没有第 idx 条了
 */
static int multi_line_at(cg_app_t *app, int idx, char *out, int cap)
{
    const char *text = cg_widget_text(cg_find(app, "edt_multi"));
    const char *p;
    int         seen = 0;

    if (!text || !out || cap < 2) return 0;

    p = text;
    while (*p) {
        const char *e = p;
        int         len, s = 0, blank = 1;

        while (*e && *e != '\n' && *e != '\r') e++;
        len = (int)(e - p);
        while (s < len && (p[s] == ' ' || p[s] == '\t')) s++;
        if (s < len && p[s] != '#') blank = 0;

        if (!blank) {
            if (seen == idx) {
                int take = len - s;

                if (take > cap - 1) take = cap - 1;
                memcpy(out, p + s, (size_t)take);
                out[take] = '\0';
                return 1;
            }
            seen++;
        }
        p = e;
        while (*p == '\r' || *p == '\n') p++;
    }
    return 0;
}

/**
 * @brief 停掉发送调度（定时发送 / 多条轮发）
 * @param app 所属应用
 * @param how 说明文字（为空则不写日志）
 * @return void
 */
static void sched_stop(cg_app_t *app, const char *how)
{
    if (s_biz.sched) {
        cg_kill_timer(s_biz.sched);
        s_biz.sched = NULL;
    }
    if (how && *how) sys_line(app, how);
    /* 界面上的勾也要跟着落下来，不然"显示在发、其实没发" */
    cg_widget_set_checked(cg_find(app, "chk_timed"), 0);
}

/**
 * @brief 调度的一拍：发一条，然后按周期再挂下一拍
 * @param user 挂进去的 cg_app_t
 * @return void
 * @note 多条模式发到最后一条时：勾了「循环」就回到第一条，否则自己停掉并说一声。
 */
static void sched_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    s_biz.sched = NULL;                    /* 一次性定时器：这一颗已经用掉了 */

    if (s_biz.sched_multi) {
        char line[1600];

        if (!multi_line_at(app, s_biz.sched_idx, line, (int)sizeof(line))) {
            if (!s_biz.sched_loop) {
                sched_stop(app, "多条发送：发完了（要一直轮就勾「循环」）。");
                return;
            }
            s_biz.sched_idx = 0;
            if (!multi_line_at(app, 0, line, (int)sizeof(line))) {
                sched_stop(app, "多条发送：框里没有可发的行（空行与 # 注释会跳过）。");
                return;
            }
        }
        send_text(app, line);
        s_biz.sched_idx++;
    } else {
        send_single(app);
    }

    s_biz.sched = cg_set_timer(app, s_biz.sched_iv, sched_tick, app);
}

/**
 * @brief 起发送调度
 * @param app 所属应用
 * @param multi 0 = 定时发单条；1 = 多条依次
 * @param loop 多条是否循环
 * @return void
 */
static void sched_start(cg_app_t *app, int multi, int loop)
{
    int iv = cg_widget_value(cg_find(app, multi ? "spn_multi" : "spn_cycle"));

    if (iv < 50) iv = 50;

    if (s_biz.sched) {                     /* 改参数就重来：先停掉旧的 */
        cg_kill_timer(s_biz.sched);
        s_biz.sched = NULL;
    }
    s_biz.sched_multi = multi;
    s_biz.sched_loop  = loop;
    s_biz.sched_idx   = 0;
    s_biz.sched_iv    = iv;

    if (multi) {
        char line[1600];

        if (!multi_line_at(app, 0, line, (int)sizeof(line))) {
            sys_line(app, "「多条发送」框里没有可发的行（空行与 # 注释会跳过）。");
            return;
        }
    }
    sched_tick(app);                       /* 立刻发第一条，不等一个周期 */
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

    if (is_connected(app)) {
        sched_stop(app, NULL);                       /* 链路没了就别再轮着发 */
        do_disconnect(app);
    } else {
        do_connect(app);
    }
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
            sys_line(app, line);
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
    cg_term_clear(term_of(app));
    cg_frame_reset(&s_biz.fr);
    s_biz.frame_no  = 0;
    s_biz.rx_window = 0;
    sys_line(app, "已清空接收缓冲与日志视图。");
    refresh_stats(app);
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
    if (cg_term_save(term_of(app), path) != 0) {
        sys_line(app, "日志没写出去（路径不可写或被占用）。");
        return;
    }
    {
        char *line = cg_str_format("日志已存到 %s（不含方向标记，纯文本）", path);
        if (line) {
            sys_line(app, line);
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
        sys_line(app, "HEX 发送：关。输入按原文（UTF-8）发出去。");
        return;
    }

    sys_line(app, "HEX 发送：开。输入像 1A 2B 2C 这样写，空格可有可无。");
    {
        const char *text = cg_widget_text(cg_find(app, "edt_send"));
        if (text && *text) {
            unsigned char bytes[SEND_MAX];
            int           n = cg_hex_decode(text, bytes, (int)sizeof(bytes));
            char         *line;
            if (n < 0) {
                sys_line(app, "（当前输入解析不了：数字个数要是偶数，而且只允许 0-9 A-F）");
                return;
            }
            line = cg_str_format("（当前输入会发成 %d 个字节）", n);
            if (line) {
                sys_line(app, line);
                cg_free(line);
            }
        }
    }
}

/**
 * @brief 「发送新行」勾选变化：只是改解析时的行为，说一声就够
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_newline_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    sys_line(cg_app_of(self), cg_widget_checked(self)
                                   ? "发送新行：开。每次发送末尾追加 \\r\\n。"
                                   : "发送新行：关。");
}

/**
 * @brief 「16 进制显示」勾选变化：交给日志控件切显示模式
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_hexview_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       on  = cg_widget_checked(self);

    (void)ev;
    (void)user;
    cg_term_set_hex(term_of(app), on);
    sys_line(app, on ? "日志改为十六进制显示（只影响之后写入的行）。"
                     : "日志改为文本显示（只影响之后写入的行）。");
}

/**
 * @brief 「时间戳」勾选变化
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_stamp_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       on  = cg_widget_checked(self);

    (void)ev;
    (void)user;
    cg_term_set_timestamp(term_of(app), on);
    sys_line(app, on ? "时间戳：开（每行前面 [时:分:秒]）。" : "时间戳：关。");
}

/**
 * @brief 「按帧显示」勾选变化：开关切帧器
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_frame_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    s_biz.frame_on = cg_widget_checked(self);
    framer_reset(app);
    sys_line(app, s_biz.frame_on
                       ? "按帧显示：开。按「静默间隔」切帧，每帧报长度与 CRC16 结论。"
                       : "按帧显示：关。");
}

/**
 * @brief 「自动保存」勾选变化：开的时候先写一行头（时间 + 端口），好知道文件从哪开始
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_autosave_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    s_biz.autosave_on = cg_widget_checked(self);

    if (s_biz.autosave_on && s_biz.autosave_path[0]) {
        char *ts   = cg_time_now(NULL);
        char *line = cg_str_format("# 串口助手接收记录 %s\n", ts ? ts : "");
        cg_free(ts);
        if (line) {
            cg_file_append(s_biz.autosave_path, line);
            cg_free(line);
        }
    }
    sys_line(app, s_biz.autosave_on
                       ? "自动保存：开（十六进制文本追加落盘，字节精确、可 grep）。"
                       : "自动保存：关。");
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
    send_single(cg_app_of(self));
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
    send_single(cg_app_of(self));
}

/**
 * @brief 「定时发送」勾选变化：勾上就开始按周期发那一行
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_timed_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (cg_widget_checked(self)) {
        sched_start(app, 0, 0);
    } else {
        sched_stop(app, "定时发送：停。");
    }
}

/**
 * @brief 「依次发送」按钮：把「多条发送」框里每一行按顺序发一遍
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_sendall_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;
    sched_start(app, 1, cg_widget_checked(cg_find(app, "chk_loop")));
}

/**
 * @brief 「停止发送」按钮：停掉定时发送 / 多条轮发
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_stop_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    sched_stop(cg_app_of(self), "发送已停止。");
}

/**
 * @brief 「循环」勾选变化：正在轮发的话立刻按新开关走
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_loop_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    s_biz.sched_loop = cg_widget_checked(self);
    if (s_biz.sched && s_biz.sched_multi) {
        s_biz.sched_loop = cg_widget_checked(self);
        sys_line(app, s_biz.sched_loop ? "多条发送：发完自动从头轮。" : "多条发送：发完就停。");
    }
}

/**
 * @brief 「打开文件」按钮：把一个文本文件读进「多条发送」框（一行一条）
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_openfile_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    path[0] = '\0';
    if (!cg_open_file_dialog(self, "打开命令列表", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             path, sizeof(path))) {
        return;
    }
    {
        char *text = cg_file_read(path);

        if (!text) {
            sys_line(app, "这个文件读不了（不存在 / 被占用 / 太大）。");
            return;
        }
        cg_widget_set_text(cg_find(app, "edt_multi"), text);
        cg_free(text);
    }
    {
        char *line = cg_str_format("已把 %s 的内容读进「多条发送」（一行一条，点「依次发送」开始）。", path);
        if (line) {
            sys_line(app, line);
            cg_free(line);
        }
    }
}

/**
 * @brief 「发送文件」按钮：把文件内容原样发出去
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 读进来的是**文本**（cg_file_read 的语义）：文本命令文件完全够用；要发二进制固件那种
 *       得按字节读，那是另一个接口的事（记在计划里）。发送前会问一次，免得误触就发出去。
 */
static void on_sendfile_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    path[0] = '\0';
    if (!cg_open_file_dialog(self, "选择要发送的文件", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             path, sizeof(path))) {
        return;
    }
    {
        char *text = cg_file_read(path);
        int   len;

        if (!text) {
            sys_line(app, "这个文件读不了（不存在 / 被占用 / 太大）。");
            return;
        }
        len = (int)strlen(text);
        if (len <= 0) {
            sys_line(app, "文件是空的，没东西可发。");
            cg_free(text);
            return;
        }
        if (len > SEND_MAX) {
            len = SEND_MAX;
            sys_line(app, "文件较大，这次只发前 4096 字节。");
        }
        send_bytes(app, (const unsigned char *)text, len);
        cg_free(text);
    }
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
 * @brief 窗口即将关闭：把没停的发送停掉，记一笔
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
    if (s_biz.sched) {
        cg_kill_timer(s_biz.sched);
        s_biz.sched = NULL;
    }
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
    cg_log(CG_LOG_INFO, "串口助手退出：RX %lld / TX %lld 字节，切帧 %lu 个",
           s_biz.rx_total, s_biz.tx_total, cg_frame_frames(&s_biz.fr));
}

/* ------------------------------------------------------------ 演示模式（--demo） */

/** 真链路演示的收尾汇总（定义在下面，这里先声明：real_demo 挂定时器要用） */
static void demo_report(void *user);

/**
 * @brief 真链路演示（--demo=COM4）：把界面下拉设成 7E1，走**用户点「打开串口」那条路**，发一帧，2 秒后汇总
 * @param app 所属应用
 * @return void
 * @note 为什么把格式设成 7E1 而不是默认的 8N1：这条链上"最容易写错又被忽略"的就是帧格式 ——
 *       7E1 走通了，说明下拉开的值真的经 `cg_widget_serial_set_format()` 送到了驱动
 *       （串口日志里会出现「已打开（115200 7E1）」）。TX/RXD 短接的话，这里还能看到回环收到的字节。
 */
static void real_demo(cg_app_t *app)
{
    cg_widget_t *cmb_port = cg_find(app, "cmb_port");
    int          i, idx = -1;

    printf("[demo] 真链路：目标 %s（下拉设成 7E1，再走「打开串口」那条路）\n", s_demo_port);
    for (i = 0; i < cg_widget_list_count(cmb_port); i++) {
        const char *it = cg_widget_list_item(cmb_port, i);

        if (it && cg_str_equal(it, s_demo_port)) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        cg_widget_list_add(cmb_port, s_demo_port);          /* 枚举里没有也认：直接塞进去试 */
        idx = cg_widget_list_count(cmb_port) - 1;
    }
    cg_widget_set_value(cmb_port, idx);
    cg_widget_set_value(cg_find(app, "cmb_bits"), 1);       /* "7" */
    cg_widget_set_value(cg_find(app, "cmb_stop"), 0);       /* "1" */
    cg_widget_set_value(cg_find(app, "cmb_parity"), 2);     /* "偶" */
    cg_widget_set_checked(cg_find(app, "chk_autosave"), 1); /* 收到的字节落盘，好读回来核对 */
    s_biz.autosave_on = 1;

    do_connect(app);                                        /* ← 用户点「打开串口」走的就是它 */
    printf("[demo] 打开结果：%s\n", is_connected(app) ? "已连接" : "没连上（原因见上面那行 -- ）");
    if (is_connected(app)) {
        cg_widget_set_checked(cg_find(app, "chk_hex"), 1);
        cg_widget_set_text(cg_find(app, "edt_send"), "01 03 00 6B 00 03 74 17");
        send_single(app);
        printf("[demo] 已发出 8 字节，TX 累计 %lld\n", s_biz.tx_total);
    }
    printf("[demo] 自动保存文件：%s\n", s_biz.autosave_path);
    fflush(stdout);
    cg_set_timer(app, 2000, demo_report, app);              /* 2 秒后汇总（收字节要靠消息循环跑起来） */
}

/**
 * @brief 真链路演示的收尾汇总：RX 收了多少、切了几帧、自动保存文件建了没有
 * @param user 挂进去的 cg_app_t
 * @return void
 */
static void demo_report(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    static int round;

    round++;
    printf("[demo] 第 %d 次汇总（%d 秒）：RX %lld 字节 / 切帧 %lu 个 / 自动保存 %s\n",
           round, round * 2, s_biz.rx_total, (unsigned long)cg_frame_frames(&s_biz.fr),
           cg_file_exists(s_biz.autosave_path) ? "文件已生成" : "还没生成（没收到字节就不会建）");
    if (s_biz.rx_total == 0 && round == 1) {
        printf("[demo] 这一秒还没有字节：对端没发 / TXD 与 RXD 没短接都很正常 —— 再看两轮\n");
    }
    fflush(stdout);
    if (round < 3) cg_set_timer(app, 2000, demo_report, app);   /* 多报两轮：慢启动的板子 3 秒后才吐数据 */
}

/**
 * @brief 不开真串口，把几批合成字节灌进**同一条接收管线**，并把结论打到 stdout
 * @param app 所属应用
 * @return void
 * @note 为什么要它：端到端验证要有**能断言的文字**（截图只能给人看）。走的是 intake()，
 *       也就是 on_arrive 用的那个函数 —— 演示只能证明这条链，不能掩盖它。
 *       发送侧同理：调 prepare_send()（真正发包时用的那一个）把解析结果打出来。
 */
static void run_demo(cg_app_t *app)
{
    /* Modbus/RTU：读保持寄存器 0x006B 起 3 个（01 03 00 6B 00 03 + CRC 74 17） */
    static const unsigned char k_req[8] = { 0x01, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x74, 0x17 };
    /* 一帧噪声（长度字段离谱那种）：应当被丢掉并计数 */
    static const unsigned char k_junk[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    unsigned long              t = 1000;
    unsigned long              f0, d0;

    s_biz.frame_on = 1;
    framer_reset(app);
    f0 = cg_frame_frames(&s_biz.fr);
    d0 = cg_frame_dropped(&s_biz.fr);

    if (s_demo_port[0]) {                       /* 给了真口：只做真链路那一段 */
        real_demo(app);
        return;
    }

    printf("[demo] 喂 8 字节的 Modbus/RTU 帧，碎成 3+3+2（每块隔 1ms，小于静默间隔）\n");
    intake(app, k_req, 3, t);
    intake(app, k_req + 3, 3, t + 1);
    intake(app, k_req + 6, 2, t + 2);
    printf("[demo] 碎着喂完还没有整帧：%d（要有 0 = 没有整帧）\n",
           (int)(cg_frame_frames(&s_biz.fr) - f0));

    cg_frame_tick(&s_biz.fr, t + 2 + 100);      /* 静默到点：整帧出来 */
    drain_frames(app);
    printf("[demo] 静默到点后切出 %d 帧\n", (int)(cg_frame_frames(&s_biz.fr) - f0));

    printf("[demo] 喂 1 个字节的零头（低于 min_len = 2）：应当被丢掉并计数\n");
    intake(app, k_junk, 1, t + 200);
    cg_frame_tick(&s_biz.fr, t + 400);
    drain_frames(app);
    printf("[demo] 切出 %d 帧 / 丢 %d 字节（那个零头要记在 dropped 里）\n",
           (int)(cg_frame_frames(&s_biz.fr) - f0),
           (int)(cg_frame_dropped(&s_biz.fr) - d0));

    printf("[demo] 再喂 5 字节噪声：静默切帧**分不出**它和真帧（物理限制，不是缺陷，已写进头文件）\n");
    printf("[demo]           所以必须配 CRC —— 上面那行「帧 #2 … CRC16 不符」就是它被认出来的样子\n");
    intake(app, k_junk, 5, t + 600);
    cg_frame_tick(&s_biz.fr, t + 800);
    drain_frames(app);
    printf("[demo] 一共切出 %d 帧（CRC 不符的那帧就是噪声）\n",
           (int)(cg_frame_frames(&s_biz.fr) - f0));

    /* 帧格式那三个下拉有没有接上（只打印解析结果，不真开串口） */
    {
        cg_widget_t *cb = cg_find(app, "cmb_bits");
        cg_widget_t *cs = cg_find(app, "cmb_stop");
        cg_widget_t *cp = cg_find(app, "cmb_parity");
        const char  *bt = cg_widget_list_item(cb, cg_widget_value(cb));
        const char  *st = cg_widget_list_item(cs, cg_widget_value(cs));
        const char  *pt = cg_widget_list_item(cp, cg_widget_value(cp));

        printf("[demo] 界面上的帧格式下拉：数据位=%s 停止位=%s 校验=%s"
               "（打开串口时交给 cg_widget_serial_set_format → cg_serial_open_ex）\n",
               bt ? bt : "-", st ? st : "-", pt ? pt : "-");
    }

    /* 发送侧：解析（不真发，因为演示模式不开串口） */
    {
        unsigned char out[64];
        char          hex[3 * 64];
        int           n = prepare_send("01 03 00 00 00 0A", 1, 1, out, (int)sizeof(out));

        if (n > 0) {
            cg_hex_encode(out, n, " ", 1, hex, (int)sizeof(hex));
            printf("[demo] HEX 输入 \"01 03 00 00 00 0A\" + 发送新行 -> %d 字节：%s\n", n, hex);
        }
        n = prepare_send("AT", 0, 1, out, (int)sizeof(out));
        if (n > 0) {
            cg_hex_encode(out, n, " ", 1, hex, (int)sizeof(hex));
            printf("[demo] 文本输入 \"AT\" + 发送新行 -> %d 字节：%s\n", n, hex);
        }
        printf("[demo] HEX 写错时返回 %d（负数 = 拒绝发送，不会发半截）\n",
               prepare_send("1G", 1, 0, out, (int)sizeof(out)));
    }
    printf("[demo] 演示结束（窗口可以继续用；这一模式不开串口）\n");
    fflush(stdout);
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
    cg_widget_t *term;
    char        *dir, *ini;

    (void)user;

    if (!cg_app_has_window(app)) return;          /* --selftest：不开串口、不弹框 */

    /* 配置文件放在 %LOCALAPPDATA%\serialterm\serialterm.ini */
    dir = cg_app_data_dir();
    ini = dir ? cg_path_join(dir, "serialterm.ini") : NULL;
    if (ini) snprintf(s_biz.ini, sizeof(s_biz.ini), "%s", ini);
    if (dir) snprintf(s_biz.autosave_path, sizeof(s_biz.autosave_path), "%s\\rx_log.txt", dir);
    cg_free(ini);
    cg_free(dir);

    cht = cg_find(app, "cht_rate");
    if (cht) cg_chart_set_range(cht, 0, CHART_TOP_MIN);
    s_biz.chart_top = CHART_TOP_MIN;

    /* 日志控件的初值跟着 .cgui 里的勾选走（时间戳那一项 .cgui 写的是 checked: true） */
    term = term_of(app);
    cg_term_set_timestamp(term, cg_widget_checked(cg_find(app, "chk_stamp")));
    cg_term_set_hex(term, cg_widget_checked(cg_find(app, "chk_hexview")));
    framer_reset(app);
    s_biz.frame_on = cg_widget_checked(cg_find(app, "chk_frame"));

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
    /* 帧格式三个下拉：记住上次选的（7E1 那种设备只需配一次） */
    combo_restore(app, "cmb_bits", "数据位");
    combo_restore(app, "cmb_stop", "停止位");
    combo_restore(app, "cmb_parity", "校验");

    set_connected(app, 0);
    refresh_stats(app);
    cg_set_timer(app, AGG_MS, agg_tick, app);     /* 聚合拍（取数据不靠它，见 on_arrive） */
    cg_log(CG_LOG_INFO, "串口助手启动：枚举到 %d 个串口，配置 %s",
           s_biz.n_ports, s_biz.ini[0] ? s_biz.ini : "(无)");

    if (s_demo) run_demo(app);
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 serialterm.cgui 里） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_connect_click",   on_connect_click),
    CG_BIND("on_refresh_click",   on_refresh_click),
    CG_BIND("on_clear_click",     on_clear_click),
    CG_BIND("on_save_log_click",  on_save_log_click),
    CG_BIND("on_hex_change",      on_hex_change),
    CG_BIND("on_newline_change",  on_newline_change),
    CG_BIND("on_hexview_change",  on_hexview_change),
    CG_BIND("on_stamp_change",    on_stamp_change),
    CG_BIND("on_frame_change",    on_frame_change),
    CG_BIND("on_autosave_change", on_autosave_change),
    CG_BIND("on_send_click",      on_send_click),
    CG_BIND("on_send_submit",     on_send_submit),
    CG_BIND("on_timed_change",    on_timed_change),
    CG_BIND("on_sendall_click",   on_sendall_click),
    CG_BIND("on_stop_click",      on_stop_click),
    CG_BIND("on_loop_change",     on_loop_change),
    CG_BIND("on_openfile_click",  on_openfile_click),
    CG_BIND("on_sendfile_click",  on_sendfile_click),
    CG_BIND("on_split_release",   on_split_release),
    CG_BIND("on_arrive",          on_arrive),
    CG_BIND("on_close",           on_close),
    CG_BIND("on_destroy",         on_destroy),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检 / 演示 / 正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest / --demo）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int           i;

    memset(&desc, 0, sizeof(desc));

    /* 演示模式：命令行给 --demo，或者设环境变量 CG_SERIALTERM_DEMO=1。
     * 两种都认的理由同 examples/modbus：tools\shot.ps1（截图 / CI 用）只传 .cgui 路径，
     * 没法再塞一个开关，而环境变量会被子进程继承。
     * **后面跟端口名就是真链路模式**（`--demo=COM4` / `CG_SERIALTERM_DEMO=COM4`）：
     * 不灌合成数据，真开那个口跑一遍"选参数 → 打开 → 发 → 收"，给真机验证与现场自检用。 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) {
            s_demo = 1;
        } else if (strncmp(argv[i], "--demo=", 7) == 0) {
            s_demo = 1;
            snprintf(s_demo_port, sizeof(s_demo_port), "%s", argv[i] + 7);
        }
    }
    {
        const char *env = getenv("CG_SERIALTERM_DEMO");

        if (env && *env && strcmp(env, "0") != 0) {
            s_demo = 1;
            if (strcmp(env, "1") != 0) {        /* 值不是 1/0 就是端口名 */
                snprintf(s_demo_port, sizeof(s_demo_port), "%s", env);
            }
        }
    }

    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动去 exe 旁边找同名 .cgui */
    desc.on_ready = on_ready;       /* 用 on_ready 钩子而不是 app.events.ready：
                                       --selftest 也会走它，正好验证"自检不开串口"那个守卫 */

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "serialterm.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "serialterm.cgui";
    return cg_app_run(&desc);
}
