/**
 * @file    main.c
 * @brief   自定义帧协议示例：帧头 + 长度 + 命令 + 负载 + 校验的切帧、校验与解码，不接硬件
 * @details 已有的 `examples\modbus` 只讲 Modbus 这一种既有协议；"自己定一种帧、自己切帧与校验"
 *          这条路没有任何示例走过，本示例就补这一条。数据不来自串口 / 网口，而是三种合成来源：
 *          （a）粘贴十六进制文本进输入框；（b）「模拟一帧」按当前字段拼一帧填回输入框；
 *          （c）「模拟连发」用定时器每 300ms 造一批字节（**故意夹带噪声与断帧**）。
 *
 *          帧格式（简单到一眼能懂）：
 *              [0xAA 0x55][长度 L(1)][命令 cmd(1)][负载 N][校验(1)]
 *              L = 校验字节之前的字节数 = 4 + N  =>  负载 N = L - 4
 *              整帧长度 = L + 1；最小 L = 4（空负载，整帧 5 字节）
 *          校验三选一：累加和(低 8 位) / XOR / CRC16-Modbus(低字节在前)。
 *
 *          切帧**手写**（没有复用框架的通用切帧器），原因写在 drain_stream 的注释里。
 *          每一种异常都给**明确的结论文字**（"残帧：还差 3 字节"/"校验错：算出 0x4B，帧里是 0x4C"），
 *          不静默跳过 —— 这是通信调试里最想要的那一眼。
 *
 *          编译：在项目根跑 build-app.bat framedec
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

#define PH0            0xAA          /**< 帧头第 1 字节 */
#define PH1            0x55          /**< 帧头第 2 字节 */
#define FRAME_OVERHEAD 4             /**< 帧头 2 + 长度 1 + 命令 1（校验字节之前的固定开销） */
#define FRAME_MAX      300           /**< 一帧最长（含校验） */
#define PAYLOAD_MAX    (FRAME_MAX - FRAME_OVERHEAD - 1)   /**< 负载最长 295 */
#define STREAM_CAP     2048          /**< 攒帧缓冲（固定数组，见 s_biz.buf 注释） */
#define BURST_MS       300           /**< 连发周期 */
#define TBL_MAX_ROWS   200           /**< 帧表最多留这么多行，超了清空重来 */
#define HEX_MAX        (3 * FRAME_MAX + 8)

#define SUM_ADD8       0             /**< 与 .cgui 里「校验」下拉的项序号一致 */
#define SUM_XOR8       1
#define SUM_CRC16      2

#define CMD_TEMP       0x01
#define CMD_RANGE      0x02
#define CMD_VOLT       0x03

/** 切帧 / 统计状态。固定数组、不 malloc：示例的规模是定死的，堆分配只会多出一套所有权与失败路径；
 * 真项目里把它收进业务 struct 即可（本示例只有一个实例，故用文件级 static） */
typedef struct {
    unsigned char buf[STREAM_CAP];   /**< 攒帧缓冲（前移由 memmove 完成，量小无所谓） */
    int           len;               /**< 缓冲里现在有多少字节 */
    long long     rx_total;          /**< 累计收到字节 */
    unsigned long frames;            /**< 校验通过的整帧数 */
    unsigned long bad_sum;           /**< 校验错的帧数 */
    unsigned long frags;             /**< 报出的残帧次数 */
    unsigned long noise;             /**< 丢弃的噪声字节 */
    int           seg;               /**< 已消费字节数（= 下一帧在字节流里的偏移） */
    int           last_need;         /**< 上次报的残帧长度，用来去重不刷屏 */
    int           burst;             /**< 连发开关 */
    cg_timer_t   *timer;             /**< 连发定时器（NULL = 没在发） */
    unsigned      seq;               /**< 连发序号 */
} biz_t;

static biz_t s_biz;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 取日志视图控件
 * @param app 所属应用
 * @return cg_widget_t * 终端控件；.cgui 里没有就返回 NULL（终端相关接口都 NULL 安全）
 */
static cg_widget_t *term_of(cg_app_t *app)
{
    return cg_find(app, "term_log");
}

/**
 * @brief 往日志视图写一条"程序自己记的"消息（灰色那种）
 * @param app 所属应用
 * @param text UTF-8 文本
 * @return void 无返回值
 */
static void log_sys(cg_app_t *app, const char *text)
{
    if (!text || !*text) return;
    cg_term_write(term_of(app), CG_TERM_SYS, text, -1);
}

/**
 * @brief 读「校验类型」下拉，返回校验类型编号
 * @param app 所属应用
 * @return int 校验类型（越界兜底为累加和）
 */
static int ui_kind(cg_app_t *app)
{
    int i = cg_widget_value(cg_find(app, "cmb_sum"));

    return (i < 0 || i > 2) ? SUM_ADD8 : i;
}

/**
 * @brief 命令码转中文名（未知命令也照实说）
 * @param cmd 命令字节
 * @return const char * 命令名
 */
static const char *cmd_name(int cmd)
{
    switch (cmd) {
    case CMD_TEMP:  return "读温度";
    case CMD_RANGE: return "读量程";
    case CMD_VOLT:  return "读电压组";
    default:        return "未知命令";
    }
}

/**
 * @brief 算一个字节的校验值（覆盖 d 的前 n 个字节）
 * @param kind 校验类型编号
 * @param d 校验覆盖的数据（帧头 … 负载）
 * @param n 字节数
 * @return int 低 8 位有效的校验字节
 * @note CRC16 复用框架的现成实现（多项式 0xA001、初值 0xFFFF）；累加和与 XOR 没有现成函数，就地算。
 *       CRC16 是 16 位，而本帧只放 1 字节校验 ⇒ 只取低字节（低字节在前）
 */
static int sum_calc(int kind, const unsigned char *d, int n)
{
    int i, v = 0;

    if (kind == SUM_CRC16) return (int)(cg_crc16(d, n) & 0xFFu);
    for (i = 0; i < n; i++) {
        if (kind == SUM_XOR8) v ^= d[i];
        else                  v += d[i];
    }
    return v & 0xFF;
}

/**
 * @brief 按当前字段拼一帧
 * @param kind 校验类型编号
 * @param cmd 命令字节
 * @param pay 负载（可空）
 * @param n 负载长度（0..上限）
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return int 帧长（已写进 out）；-1 负载太长或 out 放不下
 */
static int build_frame(int kind, int cmd, const unsigned char *pay, int n,
                       unsigned char *out, int cap)
{
    int L, i;

    if (n < 0 || n > PAYLOAD_MAX) return -1;
    L = FRAME_OVERHEAD + n;                 /* 校验字节之前的字节数 */
    if (L + 1 > cap) return -1;
    out[0] = PH0;
    out[1] = PH1;
    out[2] = (unsigned char)L;
    out[3] = (unsigned char)cmd;
    for (i = 0; i < n; i++) out[4 + i] = pay[i];
    out[L] = (unsigned char)sum_calc(kind, out, L);
    return L + 1;
}

/**
 * @brief 往帧表加一行（序号 / 偏移 / 长度 / 命令 / 校验 / 结论）
 * @param app 所属应用
 * @param no 序号
 * @param off 该帧在字节流里的起始偏移
 * @param total 帧长
 * @param cmd 命令字节
 * @param sum 帧里那一个校验字节
 * @param verdict 结论文字（合法 / 校验错 / 等）
 * @return void 无返回值
 */
static void add_row(cg_app_t *app, unsigned long no, int off, int total,
                    int cmd, int sum, const char *verdict)
{
    cg_widget_t *tbl = cg_find(app, "tbl_frames");
    char         c0[16], c1[16], c2[16], c3[40], c4[16];
    const char  *cells[6];

    if (!tbl) return;
    if (cg_table_row_count(tbl) >= TBL_MAX_ROWS) cg_table_clear(tbl);   /* 别让表无限涨 */
    snprintf(c0, sizeof c0, "%lu", no);
    snprintf(c1, sizeof c1, "%d", off);
    snprintf(c2, sizeof c2, "%d", total);
    snprintf(c3, sizeof c3, "0x%02X %s", cmd, cmd_name(cmd));
    snprintf(c4, sizeof c4, "0x%02X", sum);
    cells[0] = c0;
    cells[1] = c1;
    cells[2] = c2;
    cells[3] = c3;
    cells[4] = c4;
    cells[5] = verdict;
    cg_table_add_row(tbl, cells, 6);
}

/**
 * @brief 把负载按当前字节序解成 int16 / uint16 / 十六进制，写一句结论
 * @param app 所属应用
 * @param cmd 命令字节
 * @param pay 负载首地址
 * @param n 负载长度
 * @param out 结论输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 * @note 字节序由「字节序」下拉决定（小端 / 大端）；未知命令只给十六进制
 */
static void decode_payload(cg_app_t *app, int cmd, const unsigned char *pay, int n,
                           char *out, int cap)
{
    char hex[3 * PAYLOAD_MAX + 8];
    int  be = (cg_widget_value(cg_find(app, "cmb_end")) == 1);
    int  i, p = 0;

    hex[0] = '\0';
    if (n > 0) cg_hex_encode(pay, n, " ", 1, hex, (int)sizeof hex);

    if (cmd == CMD_TEMP || cmd == CMD_RANGE) {
        if (n < 2) {
            p += snprintf(out + p, (size_t)(cap - p), "cmd 0x%02X 要 2 字节负载，只给了 %d", cmd, n);
        } else {
            int u = cg_bytes_get_u16(pay, n, 0, be);

            if (cmd == CMD_TEMP) {
                long long v = cg_to_signed((unsigned long long)(u & 0xFFFF), 16);

                p += snprintf(out + p, (size_t)(cap - p), "int16 %lld", v);
            } else {
                p += snprintf(out + p, (size_t)(cap - p), "uint16 %u", (unsigned)u);
            }
        }
    } else if (cmd == CMD_VOLT && n >= 2) {
        p += snprintf(out + p, (size_t)(cap - p), "int16[%d]", n / 2);
        for (i = 0; i + 1 < n && p < cap - 16; i += 2) {
            int       u = cg_bytes_get_u16(pay, n, i, be);
            long long v = cg_to_signed((unsigned long long)(u & 0xFFFF), 16);

            p += snprintf(out + p, (size_t)(cap - p), " %lld", v);
        }
    } else {
        p += snprintf(out + p, (size_t)(cap - p), "未定义命令，只看十六进制");
    }
    /* 十六进制那一串最长有 3 × 负载 个字符（远超结论缓冲），所以限长到 180 并先确认装得下：
     * 精度必须是**常量** gcc 才认（用 "%.*s" 传运行期精度一样会报 -Wformat-truncation）。
     * 结论列里放不下整串没关系：完整十六进制在右边的日志里逐帧都打了 */
    if (n > 0 && p + 184 < cap) snprintf(out + p, (size_t)(cap - p), "  [%.180s]", hex);
}

/**
 * @brief 把统计量刷到状态栏（六段）
 * @param app 所属应用
 * @return void 无返回值
 * @note 必须定义在第一个调用者（handle_frame）之前 —— 放到后面会被当成隐式声明
 */
static void refresh_status(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    if (!sb) return;
    cg_status_set(sb, 0, s_biz.burst ? "连发中" : "就绪");
    cg_status_setf(sb, 1, "收 %lld B", s_biz.rx_total);
    cg_status_setf(sb, 2, "成帧 %lu", s_biz.frames);
    cg_status_setf(sb, 3, "校验错 %lu", s_biz.bad_sum);
    cg_status_setf(sb, 4, "残帧 %lu", s_biz.frags);
    cg_status_setf(sb, 5, "丢 %lu B", s_biz.noise);
}

/**
 * @brief 处理切出的一整帧：验校验、填表、解码、写日志
 * @param app 所属应用
 * @param fr 帧首地址
 * @param total 帧长
 * @param off 该帧在字节流里的起始偏移
 * @param kind 校验类型编号
 * @return void 无返回值
 */
static void handle_frame(cg_app_t *app, const unsigned char *fr, int total, int off, int kind)
{
    int  L = total - 1;                 /* 校验覆盖 L 个字节 */
    int  want = sum_calc(kind, fr, L);
    int  got  = fr[L];
    int  cmd  = fr[3];
    char msg[1024];                     /* 要装下"完整十六进制 + 解码结论"，256 装不下长负载 */
    char verdict[192];
    char hex[HEX_MAX];

    if (want != got) {                  /* 校验错：说清算出多少、帧里是多少 */
        s_biz.bad_sum++;
        snprintf(verdict, sizeof verdict, "校验错：算出 0x%02X，帧里是 0x%02X", want, got);
        add_row(app, s_biz.frames + s_biz.bad_sum, off, total, cmd, got, verdict);
        snprintf(msg, sizeof msg, "帧（偏移 %d 长 %d cmd 0x%02X）：%s", off, total, cmd, verdict);
        log_sys(app, msg);
        refresh_status(app);
        return;
    }

    decode_payload(app, cmd, fr + 4, L - 4, verdict, (int)sizeof verdict);
    s_biz.frames++;
    add_row(app, s_biz.frames, off, total, cmd, got, verdict);

    cg_hex_encode(fr, total, " ", 1, hex, (int)sizeof hex);
    /* 两个 %s 都限长：verdict 最长 191、hex 最长 907，不限长 gcc 证不出 msg 装得下 */
    snprintf(msg, sizeof msg, "帧 #%lu 偏移 %d 长 %d cmd 0x%02X 校验 0x%02X：%.150s = [%.500s]",
             s_biz.frames, off, total, cmd, got, verdict, hex);
    log_sys(app, msg);
    refresh_status(app);
}

/**
 * @brief 从攒帧缓冲里切帧：找帧头、按长度收帧，逐类异常给明确结论
 * @param app 所属应用
 * @param kind 校验类型编号
 * @return void 无返回值
 * @note **为什么手写、不用框架的通用切帧器**：
 *       1) 本协议的长度字段 L 在**帧头之后、命令之前**，语义是"校验字节之前的字节数"，
 *          即 帧总长 = L + 1；而通用切帧器的"长度字段"模式按"帧总长 = 长度值 + 附加量"解释，
 *          且该模式的附加量必须为正 —— **表达不了 +1 这种偏移为 1 的情形**，
 *          也对不上"长度字节排在命令之前"这一布局；
 *       2) 本示例要的是逐类**文字结论**（残帧还差几字节 / 校验错 / 长度越界 / 噪声丢弃），
 *          通用切帧器只给"丢了多少字节"的计数，报不出这些；
 *       3) 固定缓冲 + 手写前移，省得为一份定死规模的示例再引入一处状态语义差异
 */
static void drain_stream(cg_app_t *app, int kind)
{
    for (;;) {
        int i, idx = -1, L, total;

        if (s_biz.len <= 0) return;

        for (i = 0; i + 1 < s_biz.len; i++) {
            if (s_biz.buf[i] == PH0 && s_biz.buf[i + 1] == PH1) { idx = i; break; }
        }
        if (idx < 0) {                              /* 没有帧头：除末字节外都当噪声丢 */
            int drop = (s_biz.len > 1) ? s_biz.len - 1 : 0;
            char m[96];

            if (drop > 0) {
                snprintf(m, sizeof m, "丢弃 %d 字节噪声（未见帧头 AA 55）", drop);
                log_sys(app, m);
                s_biz.noise += (unsigned long)drop;
                s_biz.seg   += drop;
                memmove(s_biz.buf, s_biz.buf + drop, (size_t)(s_biz.len - drop));
                s_biz.len   -= drop;
            }
            return;
        }
        if (idx > 0) {                              /* 帧头前有杂字节 */
            char m[96];

            snprintf(m, sizeof m, "丢弃 %d 字节噪声（帧头 AA 55 之前）", idx);
            log_sys(app, m);
            s_biz.noise += (unsigned long)idx;
            s_biz.seg   += idx;
            memmove(s_biz.buf, s_biz.buf + idx, (size_t)(s_biz.len - idx));
            s_biz.len   -= idx;
        }
        if (s_biz.len < 3) return;                  /* 还差长度字节，等下批 */

        L     = s_biz.buf[2];
        total = L + 1;
        if (L < FRAME_OVERHEAD || total > FRAME_MAX) {
            char m[128];

            snprintf(m, sizeof m, "长度字段 %d 不合法（合法 %d..%d），丢 1 字节重新找帧头",
                     L, FRAME_OVERHEAD, FRAME_MAX - 1);
            log_sys(app, m);
            s_biz.noise += 1;
            s_biz.seg   += 1;
            memmove(s_biz.buf, s_biz.buf + 1, (size_t)(s_biz.len - 1));
            s_biz.len   -= 1;
            continue;
        }
        if (s_biz.len < total) {                    /* 残帧：说清还差几字节 */
            if (s_biz.last_need != total) {
                char m[128];

                snprintf(m, sizeof m, "残帧：还差 %d 字节（本帧 %d，现攒 %d）",
                         total - s_biz.len, total, s_biz.len);
                log_sys(app, m);
                s_biz.frags++;
                s_biz.last_need = total;
            }
            refresh_status(app);
            return;
        }

        handle_frame(app, s_biz.buf, total, s_biz.seg, kind);   /* 整帧到了 */
        s_biz.last_need = 0;
        s_biz.seg += total;
        memmove(s_biz.buf, s_biz.buf + total, (size_t)(s_biz.len - total));
        s_biz.len -= total;
    }
}

/**
 * @brief 把一批字节喂进攒帧缓冲并试着切帧（三种来源共用这一处）
 * @param app 所属应用
 * @param data 字节
 * @param n 字节数
 * @param kind 校验类型编号
 * @return void 无返回值
 * @note 缓冲满时丢最老的字节并计入噪声 —— 宁可丢也不卡死（固定数组，没有"扩容"这条路）
 */
static void ingest(cg_app_t *app, const unsigned char *data, int n, int kind)
{
    char hex[3 * 64 + 8];
    char m[256];
    int  take;

    if (n <= 0) return;
    s_biz.rx_total += n;

    take = (n > 64) ? 64 : n;
    cg_hex_encode(data, take, " ", 1, hex, (int)sizeof hex);
    snprintf(m, sizeof m, "收到 %d 字节：%s%s", n, hex, (n > take) ? " …" : "");
    log_sys(app, m);

    if (n > STREAM_CAP) {                       /* 比缓冲还大：只留最后 STREAM_CAP 字节 */
        s_biz.noise += (unsigned long)(n - STREAM_CAP);
        data += (n - STREAM_CAP);
        n     = STREAM_CAP;
    }
    if (s_biz.len + n > STREAM_CAP) {           /* 溢出：丢最老的 */
        int drop = s_biz.len + n - STREAM_CAP;

        log_sys(app, "攒帧缓冲满了，丢掉最老的字节（取的速度跟不上）。");
        s_biz.noise += (unsigned long)drop;
        s_biz.seg   += drop;
        memmove(s_biz.buf, s_biz.buf + drop, (size_t)(s_biz.len - drop));
        s_biz.len -= drop;
    }
    memcpy(s_biz.buf + s_biz.len, data, (size_t)n);
    s_biz.len += n;
    drain_stream(app, kind);
    refresh_status(app);
}

/* ------------------------------------------------------------ 三种喂数据来源 */

/**
 * @brief （a）把输入框里的十六进制文本喂进去
 * @param app 所属应用
 * @return void 无返回值
 * @note 十六进制解析的语义正好合"带空格 / 换行 / 逗号都能忍"：不写分隔的 "AA55" 也认；
 *       只剩半个字节或非法字符返回 -1，放不下返回 -2
 */
static void feed_input(cg_app_t *app)
{
    const char   *text = cg_widget_text(cg_find(app, "edt_hex"));
    unsigned char bytes[STREAM_CAP];
    int           n;

    if (!text || !*text) { log_sys(app, "输入框是空的，没东西可切。"); return; }
    n = cg_hex_decode(text, bytes, (int)sizeof bytes);
    if (n == -1) { log_sys(app, "十六进制写错了：只允许 0-9 A-F 与空格 / 换行 / 逗号分隔，且数字个数要是偶数。"); return; }
    if (n == -2) { log_sys(app, "这一批太长（最多 2048 字节）。"); return; }
    if (n <= 0)  { log_sys(app, "解析出来 0 个字节。"); return; }
    ingest(app, bytes, n, ui_kind(app));
}

/**
 * @brief （b）按当前校验 / 命令 / 负载拼一帧，把结果十六进制填回输入框
 * @param app 所属应用
 * @return void 无返回值
 * @note 只填框不喂：让用户先看一眼再点「喂入」，好对照帧结构
 */
static void mock_frame(cg_app_t *app)
{
    unsigned char pay[PAYLOAD_MAX];
    unsigned char frame[FRAME_MAX];
    char          hex[HEX_MAX];
    const char   *pt = cg_widget_text(cg_find(app, "edt_pay"));
    int           cmd = cg_widget_value(cg_find(app, "spn_cmd"));
    int           pn = 0, fn;

    if (pt && *pt) {
        pn = cg_hex_decode(pt, pay, (int)sizeof pay);
        if (pn < 0) { log_sys(app, "负载写错了（照「0A 0B」这样写）。"); return; }
    }
    fn = build_frame(ui_kind(app), cmd & 0xFF, pay, pn, frame, (int)sizeof frame);
    if (fn < 0) { log_sys(app, "这一帧拼不出来（负载太长）。"); return; }
    cg_hex_encode(frame, fn, " ", 1, hex, (int)sizeof hex);
    cg_widget_set_text(cg_find(app, "edt_hex"), hex);
    log_sys(app, "已按当前校验 / 命令 / 负载生成一帧，填进输入框；点「喂入」看它被切出来。");
}

/**
 * @brief （c）连发的一拍：造一批字节喂进去（故意夹带噪声与断帧）
 * @param user 挂定时器时传进来的应用指针
 * @return void 无返回值
 * @note 每拍 = 2 字节噪声 + 一帧合法帧 + 一段 5 字节的断帧（声明长度 9 却只给 5 字节）。
 *       目的是演示切帧的健壮性：噪声被丢、合法帧被切出、残缺帧报"残帧"
 */
static void burst_tick(void *user)
{
    cg_app_t     *app = (cg_app_t *)user;
    unsigned char pay[PAYLOAD_MAX];
    unsigned char frame[FRAME_MAX];
    unsigned char chunk[FRAME_MAX + 8];
    const char   *pt = cg_widget_text(cg_find(app, "edt_pay"));
    int           cmd = cg_widget_value(cg_find(app, "spn_cmd"));
    int           pn = 0, fn, m = 0;

    if (!cg_app_has_window(app)) return;            /* --selftest：绝不启动连发 */
    s_biz.seq++;
    if (pt && *pt) {
        pn = cg_hex_decode(pt, pay, (int)sizeof pay);
        if (pn < 0) pn = 0;
    }
    chunk[m++] = (unsigned char)(0x10 + (s_biz.seq & 0x0F));   /* 噪声 1 */
    chunk[m++] = (unsigned char)0x7E;                          /* 噪声 2 */
    fn = build_frame(ui_kind(app), cmd & 0xFF, pay, pn, frame, (int)sizeof frame);
    if (fn > 0) {
        memcpy(chunk + m, frame, (size_t)fn);
        m += fn;
    }
    chunk[m++] = PH0;                              /* 断帧：声明长度 9 却只给 5 字节 */
    chunk[m++] = PH1;
    chunk[m++] = 0x09;
    chunk[m++] = (unsigned char)CMD_VOLT;
    chunk[m++] = 0x01;

    ingest(app, chunk, m, ui_kind(app));
    s_biz.timer = cg_set_timer(app, BURST_MS, burst_tick, app);   /* 定时器一次性：重挂下一拍 */
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 「喂入」按钮 / 输入框回车
 * @param self 触发的控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_feed(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    feed_input(cg_app_of(self));
}

/**
 * @brief 「模拟一帧」按钮
 * @param self 按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_mock(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    mock_frame(cg_app_of(self));
}

/**
 * @brief 「模拟连发」按钮：开 / 关定时器（只在真窗口下能开）
 * @param self 按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note --selftest 会合成触发这个回调，所以先问有没有窗口：没有窗口就什么都不做，
 *       绝不启动定时器（否则 CI 会空转）
 */
static void on_burst(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    if (s_biz.timer) {
        cg_kill_timer(s_biz.timer);
        s_biz.timer = NULL;
        s_biz.burst = 0;
        cg_widget_set_text(self, "模拟连发");
        log_sys(app, "连发已停。");
    } else {
        s_biz.burst = 1;
        cg_widget_set_text(self, "停止连发");
        log_sys(app, "连发开始：每 300ms 一批（噪声 + 一帧合法帧 + 一段断帧）。");
        s_biz.timer = cg_set_timer(app, BURST_MS, burst_tick, app);
    }
    refresh_status(app);
}

/**
 * @brief 「清空 / 重置」按钮：丢攒帧、清统计、清帧表、清日志
 * @param self 按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_reset(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    s_biz.len = 0;
    s_biz.seg = 0;
    s_biz.rx_total = 0;
    s_biz.frames = 0;
    s_biz.bad_sum = 0;
    s_biz.frags = 0;
    s_biz.noise = 0;
    s_biz.last_need = 0;
    cg_table_clear(cg_find(app, "tbl_frames"));
    cg_term_clear(term_of(app));
    log_sys(app, "已清空攒帧缓冲、统计与帧表。");
    refresh_status(app);
}

/**
 * @brief 校验类型 / 字节序 / 命令 / 负载任一变了：只说一句，不重建状态
 * @param self 触发的控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 校验类型改了**不**影响已切出的帧，之后的帧按新校验算，所以这里不重置缓冲
 */
static void on_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    refresh_status(cg_app_of(self));
}

/**
 * @brief 窗口即将关闭：停掉连发
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    if (s_biz.timer) {
        cg_kill_timer(s_biz.timer);
        s_biz.timer = NULL;
        s_biz.burst = 0;
    }
}

/**
 * @brief 窗口已销毁：记一笔总计
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    cg_log(CG_LOG_INFO, "帧协议解析器退出：收 %lld B / 成帧 %lu / 校验错 %lu / 残帧 %lu / 丢 %lu B",
           s_biz.rx_total, s_biz.frames, s_biz.bad_sum, s_biz.frags, s_biz.noise);
}

/**
 * @brief 界面就绪：建表头 + 写开场白（不启动定时器、不弹框、不碰硬件）
 * @param app 所属应用
 * @param user 启动描述里挂的业务数据，本示例没挂
 * @return void 无返回值
 * @note 连发定时器**只能**由用户点「模拟连发」触发（那个回调里已守有没有窗口）；
 *       这里只做两件静态、无害的事：建表头、写一句开场白
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *tbl = cg_find(app, "tbl_frames");

    (void)user;
    if (tbl) {
        cg_table_add_column(tbl, "序号", 56);
        cg_table_add_column(tbl, "偏移", 56);
        cg_table_add_column(tbl, "长度", 56);
        cg_table_add_column(tbl, "命令", 110);
        cg_table_add_column(tbl, "校验", 64);
        cg_table_add_column(tbl, "结论", 0);
    }
    log_sys(app, "帧协议解析器就绪。帧：[AA 55][L][cmd][负载 L-4][校验 1]；整帧 = L + 1 字节。");
    log_sys(app, "校验三选一：累加和(低 8 位) / XOR / CRC16-Modbus(低字节在前)；负载按字节序解 int16 / uint16。");
    refresh_status(app);

    /* 真窗口下顺手把输入框里那帧样本喂一次：一打开就能在帧表与日志里看到
     * "切帧 → 校验 → 解码"的完整效果（自检不开窗，那边由合成的「喂入」点击走同一条路） */
    if (cg_app_has_window(app)) feed_input(app);
}

/* ------------------------------------------------------------ 绑定表 */

/** 事件名 → C 函数 的登记表（左边这些名字写在 framedec.cgui 里） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_feed",    on_feed),
    CG_BIND("on_mock",    on_mock),
    CG_BIND("on_burst",   on_burst),
    CG_BIND("on_reset",   on_reset),
    CG_BIND("on_change",  on_change),
    CG_BIND("on_close",   on_close),
    CG_BIND("on_destroy", on_destroy),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动去 exe 旁边找同名 .cgui */
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "framedec.cgui";
        return cg_app_selftest(&desc);          /* 不开窗：不启动定时器、不弹框 */
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "framedec.cgui";
    return cg_app_run(&desc);
}
