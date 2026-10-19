/**
 * @file    cl.c
 * @brief   calculator —— 套件模块（原 examples\calculator 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 cl_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 cl_bindings、原 on_ready 变成 cl_init、
 *          窗口事件改成直连转发 cl_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 *
 *          模块拆分（2026-10-19）：递归下降表达式求值是纯逻辑零 UI 依赖，已剥离到 cl_expr.c，
 *          共享的定长缓冲上限与 ca_eval 声明在 cl_internal.h；本文件只留 UI 胶水。
 */

/**
 * @file    main.c
 * @brief   计算器：表达式求值 / 时间戳转换 / 进制转换 / 长度计算 / 字符串转换 / 位运算拼接
 * @details 六个功能分区与界面一一对应：
 *
 *            计算      递归下降表达式求值（四则 / 括号 / 幂 / 位运算 / 常用函数），「查看公式」列语法
 *            时间戳    unix 秒 ↔ "YYYY-MM-DD HH:MM:SS" 互转，生成当前时间戳并复制到剪贴板
 *            进制      原进制 → 被进制（2/8/10/16，支持 0x / 0b 前缀），两侧都可按位取反
 *            长度      字节数（UTF-8）/ 字符数 / 行数
 *            字符串    文本 ↔ HEX 互转（原地替换），可给 HEX 加 / 去空格
 *            位运算    位宽'值 列表拼接（首段最高位），输出 HEX 与按段分组的二进制
 *
 *          自检（--selftest）红线，本文件处处遵守：
 *            1. 就绪钩子预填全部输入框并跑通每条路径，自检合成事件时没有一条是空跑；
 *            2. 凡有副作用（剪贴板 / 弹窗）先 cg_app_has_window()，无窗一律跳过；
 *            3. 全程不弹窗、不写盘，--selftest 退出码即结果。
 *
 *          解析器安全口径：表达式长度 ≤ 512、递归深度 ≤ 16、位宽 ≤ 64、段数 ≤ 16，
 *          所有输出走定长缓冲 + snprintf，除零 / 未知符号 / 括号不配平都给友好错误串。
 *
 * 界面在 calculator.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat calculator
 * 自检：calculator.exe --selftest
 */
#include "cl_internal.h"   /* 子模块共享：定长缓冲上限 + ca_eval 声明 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ 业务状态 */

/** 业务状态：最近一次操作描述（状态栏第一段） */
typedef struct {
    char last[CA_LAST];
} ca_biz_t;

static ca_biz_t s_biz;

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 按 id 取控件（找不到返回 NULL，调用方一律容忍）
 * @param app 应用
 * @param id 控件 id
 * @return cg_widget_t* 控件或 NULL
 */
static cg_widget_t *w(cg_app_t *app, const char *id)
{
    return cg_find(app, id);
}

/**
 * @brief 往控件写文字（NULL 控件时静默跳过）
 * @param e 目标控件
 * @param text 文字
 * @return void 无返回值
 */
static void set_box(cg_widget_t *e, const char *text)
{
    if (e) cg_widget_set_text(e, text ? text : "");
}

/**
 * @brief 记下最近一次操作并刷新状态栏
 * @param app 应用
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void set_status(cg_app_t *app, const char *fmt, ...)
{
    va_list ap;
    char    buf[CA_LAST];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(s_biz.last, sizeof(s_biz.last), "%s", buf);
    cg_status_set(w(app, "sb_main"), 0, s_biz.last);
}

/* 表达式求值（递归下降解析器）已按职责剥离到 cl_expr.c；对外入口是 ca_eval（见 cl_internal.h）。 */

/**
 * @brief 「计算」：读计算式 → 求值 → 写结果框
 * @param app 应用
 * @return void 无返回值
 */
static void do_calc(cg_app_t *app)
{
    cg_widget_t *e = w(app, "edt_expr");
    cg_widget_t *r = w(app, "edt_result");
    const char  *expr = e ? cg_widget_text(e) : "";
    char         err[80];
    double       v = 0.0;

    if (ca_eval(expr, &v, err, (int)sizeof(err))) {
        char out[48];
        snprintf(out, sizeof(out), "%.12g", v);
        set_box(r, out);
        set_status(app, "计算完成：%s", out);
    } else {
        set_box(r, "");
        set_status(app, "计算失败：%s", err);
    }
}

/**
 * @brief 「查看公式」：把语法清单写进结果框（单行，'; ' 分隔）并进日志
 * @param app 应用
 * @return void 无返回值
 */
static void do_formula(cg_app_t *app)
{
    static const char *k_formula =
        "运算符 + - * / % ^ & | ^ ~ << >> （优先级同 C，~ 对整数）; "
        "常量 pi、e; 前缀 0x 十六进制、0b 二进制; "
        "函数 sqrt cbrt ln log log2 exp sin cos tan asin acos atan abs floor ceil round min max pow; "
        "例：(1+2)*3-4/2、0xff&0x0f、sqrt(2)、2^10";

    set_box(w(app, "edt_result"), k_formula);
    cg_log(CG_LOG_INFO, "公式语法：%s", k_formula);
    set_status(app, "已列出公式语法（结果框）");
}

/* ============================================================ 时间戳转换 */

/**
 * @brief 公历转天数（Howard Hinnant 算法，免 timegm 依赖）
 * @param y 年（如 2026）
 * @param m 月 1~12
 * @param d 日 1~31
 * @return long long 自 1970-01-01 起的天数
 */
static long long ca_days_from_civil(int y, int m, int d)
{
    int      era;
    unsigned yoe, doy, doe;
    const unsigned mm = (unsigned)m;

    y -= mm <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153u * (mm + (mm > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + (unsigned)d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (long long)era * 146097 + (long long)doe - 719468;
}

/**
 * @brief 时间戳（unix 秒）→ UTC 文本 "YYYY-MM-DD HH:MM:SS"
 * @param ts unix 秒
 * @param out 输出缓冲
 * @param cap 容量
 * @return int 1 = 成功；0 = 超出可表示范围
 */
static int ca_ts2utc(long long ts, char *out, int cap)
{
    time_t     t = (time_t)ts;
    struct tm *g = gmtime(&t);

    if (!g) { out[0] = '\0'; return 0; }
    strftime(out, (size_t)cap, "%Y-%m-%d %H:%M:%S", g);
    return 1;
}

/**
 * @brief UTC 文本 → 时间戳（容忍 'T' 分隔）
 * @param s "YYYY-MM-DD HH:MM:SS"（或 T 分隔）
 * @param out 时间戳出口
 * @return int 1 = 成功；0 = 格式或范围不对
 */
static int ca_utc2ts(const char *s, long long *out)
{
    int y, mo, d, h, mi, se;

    while (*s == ' ') s++;
    if (sscanf(s, "%d-%d-%d%*[ T]%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || se < 0 || se > 59)
        return 0;
    if (y < 1900 || y > 3000) return 0;
    *out = ca_days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + se;
    return 1;
}

/**
 * @brief 左框时间戳 → 右框 UTC（编辑即转）
 * @param app 应用
 * @return void 无返回值
 */
static void do_ts2utc(cg_app_t *app)
{
    cg_widget_t *e = w(app, "edt_ts");
    const char  *t = e ? cg_widget_text(e) : "";
    char        *end = NULL;
    long long    ts;

    if (!t || !*t) { set_box(w(app, "edt_utc"), ""); set_status(app, "时间戳框是空的"); return; }
    ts = strtoll(t, &end, 10);
    if (!end || *end) { set_status(app, "时间戳不是整数"); return; }
    {
        char buf[32];
        if (!ca_ts2utc(ts, buf, (int)sizeof(buf))) { set_status(app, "时间戳超出可表示范围"); return; }
        set_box(w(app, "edt_utc"), buf);
        set_status(app, "时间戳 %lld -> %s", ts, buf);
    }
}

/**
 * @brief 右框 UTC → 左框时间戳（编辑即转）
 * @param app 应用
 * @return void 无返回值
 */
static void do_utc2ts(cg_app_t *app)
{
    cg_widget_t *e = w(app, "edt_utc");
    const char  *t = e ? cg_widget_text(e) : "";
    long long    ts = 0;

    if (!t || !*t) { set_box(w(app, "edt_ts"), ""); set_status(app, "UTC 框是空的"); return; }
    if (!ca_utc2ts(t, &ts)) { set_status(app, "UTC 格式应为 YYYY-MM-DD HH:MM:SS"); return; }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", ts);
        set_box(w(app, "edt_ts"), buf);
        set_status(app, "UTC %s -> 时间戳 %lld", t, ts);
    }
}

/**
 * @brief 「生成时间戳」：当前 unix 秒写左框、同步 UTC，并复制到剪贴板（有窗才复制）
 * @param app 应用
 * @return void 无返回值
 */
static void do_gen_ts(cg_app_t *app)
{
    long long ts = (long long)time(NULL);
    char      buf[32];

    snprintf(buf, sizeof(buf), "%lld", ts);
    set_box(w(app, "edt_ts"), buf);
    do_ts2utc(app);
    if (cg_app_has_window(app)) {
        if (cg_clip_set_text(buf) == 0) set_status(app, "已生成 %lld 并复制到剪贴板", ts);
        else set_status(app, "已生成 %lld（剪贴板写入失败）", ts);
    } else {
        set_status(app, "已生成 %lld", ts);
    }
}

/* ============================================================ 进制转换 */

/**
 * @brief 取原进制单选组的当前基数
 * @param app 应用
 * @return int 2 / 8 / 10 / 16（默认 10）
 */
static int ca_src_base(cg_app_t *app)
{
    if (cg_widget_checked(w(app, "rad_s2")))  return 2;
    if (cg_widget_checked(w(app, "rad_s8")))  return 8;
    if (cg_widget_checked(w(app, "rad_s16"))) return 16;
    return 10;
}

/**
 * @brief 取被进制单选组的当前基数
 * @param app 应用
 * @return int 2 / 8 / 10 / 16（默认 10）
 */
static int ca_dst_base(cg_app_t *app)
{
    if (cg_widget_checked(w(app, "rad_d2")))  return 2;
    if (cg_widget_checked(w(app, "rad_d8")))  return 8;
    if (cg_widget_checked(w(app, "rad_d16"))) return 16;
    return 10;
}

/**
 * @brief 按指定进制解析无符号整数（容忍 0x / 0b 前缀，无视传入 base）
 * @param s 文本
 * @param base 期望进制（仅用于无前缀时的 strtoull）
 * @param out 取值出口
 * @return int 1 = 成功；0 = 不是合法整数
 */
static int ca_parse_ull(const char *s, int base, unsigned long long *out)
{
    char *end = NULL;

    while (*s == ' ') s++;
    if (!*s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = strtoull(s + 2, &end, 16);
        return (end != s + 2 && end && *end == '\0');
    }
    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        const char        *q = s + 2;
        unsigned long long v = 0;

        if (!*q) return 0;
        for (; *q; q++) {
            if (*q != '0' && *q != '1') return 0;
            v = (v << 1) | (unsigned long long)(*q - '0');
        }
        *out = v;
        return 1;
    }
    *out = strtoull(s, &end, base);
    return (end != s && end && *end == '\0');
}

/**
 * @brief 按指定进制格式化无符号整数（十六进制大写）
 * @param v 值
 * @param base 2 / 8 / 10 / 16
 * @param out 输出缓冲
 * @param cap 容量
 * @return void 无返回值
 */
static void ca_fmt_ull(unsigned long long v, int base, char *out, int cap)
{
    if (base == 10)      snprintf(out, (size_t)cap, "%llu", v);
    else if (base == 16) snprintf(out, (size_t)cap, "%llX", v);
    else if (base == 8)  snprintf(out, (size_t)cap, "%llo", v);
    else {
        char tmp[65];
        int  n = 0, k = 0;

        if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
        while (v) { tmp[n++] = (char)((v & 1u) + '0'); v >>= 1; }
        while (n) out[k++] = tmp[--n];
        out[k] = '\0';
    }
}

/**
 * @brief 进制转换：左框按原进制解析 → 右框按被进制输出
 * @param app 应用
 * @return void 无返回值
 */
static void do_convert(cg_app_t *app)
{
    const char        *t = cg_widget_text(w(app, "edt_conv"));
    int                sb = ca_src_base(app), db = ca_dst_base(app);
    unsigned long long v = 0;
    char               out[80];

    if (!t || !*t) { set_box(w(app, "edt_conv_res"), ""); return; }
    if (!ca_parse_ull(t, sb, &v)) {
        set_box(w(app, "edt_conv_res"), "");
        set_status(app, "转换数字不是合法的 %d 进制整数", sb);
        return;
    }
    ca_fmt_ull(v, db, out, (int)sizeof(out));
    set_box(w(app, "edt_conv_res"), out);
    set_status(app, "%d 进制 -> %d 进制：%s", sb, db, out);
}

/**
 * @brief 对某个框里的整数按位取反，并按给定进制写回该框
 * @param app 应用
 * @param box_id 框 id
 * @param base 该框当前进制
 * @param what 状态栏描述（"转换数字" / "转换结果"）
 * @return void 无返回值
 */
static void do_negate(cg_app_t *app, const char *box_id, int base, const char *what)
{
    cg_widget_t       *e = w(app, box_id);
    const char        *t = e ? cg_widget_text(e) : "";
    unsigned long long v = 0;
    char               out[80];

    if (!t || !*t) { set_status(app, "%s是空的，没得取反", what); return; }
    if (!ca_parse_ull(t, base, &v)) { set_status(app, "%s不是合法整数", what); return; }
    v = ~v;
    ca_fmt_ull(v, base, out, (int)sizeof(out));
    set_box(e, out);
    set_status(app, "%s已按位取反：%s", what, out);
}

/* ============================================================ 长度计算 */

/**
 * @brief 长度计算：字节数（UTF-8）/ 字符数（码点）/ 行数 → 输出框
 * @param app 应用
 * @return void 无返回值
 */
static void do_len(cg_app_t *app)
{
    const char *body = cg_widget_text(w(app, "edt_len_body"));
    size_t      bytes = body ? strlen(body) : 0;
    unsigned    chars = 0, lines = 0, nl = 0, i;
    char        out[96];

    for (i = 0; i < bytes; i++)
        if (((unsigned char)body[i] & 0xC0u) != 0x80u) chars++;   /* 非 UTF-8 续字节 = 新码点 */
    for (i = 0; i < bytes; i++)
        if (body[i] == '\n') nl++;
    lines = (bytes == 0) ? 0 : (unsigned)(nl + (body[bytes - 1] != '\n'));
    snprintf(out, sizeof(out), "%u 字节 · %u 字符 · %u 行", (unsigned)bytes, chars, lines);
    set_box(w(app, "edt_len_out"), out);
    set_status(app, "长度：%s", out);
}

/* ============================================================ 字符串转换 */

/**
 * @brief 把二进制缓冲过滤成可安全显示的 C 串（控制字符与 \0 显示为点）
 * @param raw 二进制
 * @param n 长度
 * @param out 输出缓冲
 * @param cap 容量
 * @return void 无返回值
 */
static void ca_printable(const unsigned char *raw, int n, char *out, int cap)
{
    int i, k = 0;

    for (i = 0; i < n && k < cap - 1; i++) {
        unsigned char c = raw[i];
        out[k++] = (c >= 0x20 && c != 0x7F) ? (char)c : '.';
    }
    out[k] = '\0';
}

/**
 * @brief 「STR转HEX」：文本 → 大写十六进制（空格分隔），原地写回
 * @param app 应用
 * @return void 无返回值
 */
static void do_str2hex(cg_app_t *app)
{
    cg_widget_t *e = w(app, "edt_str");
    const char  *t = e ? cg_widget_text(e) : "";
    static char  hex[CA_HEX_MAX];

    if (!t || !*t) { set_status(app, "文本是空的"); return; }
    if (cg_hex_encode(t, (int)strlen(t), " ", 1, hex, (int)sizeof(hex)) < 0) {
        set_status(app, "文本太长，转不下");
        return;
    }
    set_box(e, hex);
    set_status(app, "文本 -> HEX（%d 字节）", (int)strlen(t));
}

/**
 * @brief 「HEX转STR」：十六进制 → 文本（不可打印字符显示为点），原地写回
 * @param app 应用
 * @return void 无返回值
 */
static void do_hex2str(cg_app_t *app)
{
    cg_widget_t      *e = w(app, "edt_hex");
    const char       *t = e ? cg_widget_text(e) : "";
    static unsigned char raw[CA_HEX_MAX / 2];
    static char          txt[CA_HEX_MAX / 2];
    int                   n;

    if (!t || !*t) { set_status(app, "HEX 是空的"); return; }
    n = cg_hex_decode(t, raw, (int)sizeof(raw));
    if (n < 0) { set_status(app, "含非十六进制字符（空格 / 逗号可分隔）"); return; }
    if (n == 0) { set_status(app, "没有解出字节"); return; }
    ca_printable(raw, n, txt, (int)sizeof(txt));
    set_box(e, txt);
    set_status(app, "HEX -> 文本（%d 字节）", n);
}

/**
 * @brief 「转换」：按复选框给 HEX 加空格 / 去空格（先解码校验再格式化）
 * @param app 应用
 * @return void 无返回值
 */
static void do_sp(cg_app_t *app)
{
    cg_widget_t      *e = w(app, "edt_sp");
    const char       *t = e ? cg_widget_text(e) : "";
    int               spaced = cg_widget_checked(w(app, "chk_sp"));
    static unsigned char raw[CA_HEX_MAX / 2];
    static char          hex[CA_HEX_MAX];
    int                   n;

    if (!t || !*t) { set_status(app, "待处理的 HEX 是空的"); return; }
    n = cg_hex_decode(t, raw, (int)sizeof(raw));
    if (n < 0) { set_status(app, "含非十六进制字符"); return; }
    if (cg_hex_encode(raw, n, spaced ? " " : "", 1, hex, (int)sizeof(hex)) < 0) {
        set_status(app, "HEX 太长，处理不下");
        return;
    }
    set_box(e, hex);
    set_status(app, spaced ? "HEX 已加空格（%d 字节）" : "HEX 已去空格（%d 字节）", n);
}

/* ============================================================ 位运算拼接 */

/** 位运算段：位宽 + 值 */
typedef struct {
    int width;
    unsigned long long value;
} ca_seg_t;

/**
 * @brief 把一段的值格式化成左补零到指定位宽的二进制
 * @param v 值
 * @param width 位宽
 * @param out 输出缓冲（≥ width+1）
 * @return void 无返回值
 */
static void ca_seg_bin(unsigned long long v, int width, char *out)
{
    int i;

    for (i = 0; i < width; i++)
        out[i] = (char)(((v >> (width - 1 - i)) & 1u) + '0');
    out[width] = '\0';
}

/**
 * @brief 「转换」：解析 位宽'值 列表，首段最高位拼接，输出 HEX 与分段二进制
 * @param app 应用
 * @return void 无返回值
 */
static void do_bits(cg_app_t *app)
{
    cg_widget_t *e = w(app, "edt_bits");
    const char  *s = e ? cg_widget_text(e) : "";
    ca_seg_t     segs[CA_SEG_MAX];
    int          nseg = 0, i;
    unsigned long long acc = 0, used = 0;
    char         hex[24], out[CA_OUT_MAX], res[CA_OUT_MAX + 40];
    size_t       off = 0;

    if (!s || !*s) { set_box(w(app, "edt_bits_res"), ""); set_status(app, "位运算输入是空的"); return; }
    while (*s) {
        char       *end = NULL;
        int         width;
        long        wv;
        const char *q;

        while (*s == ' ' || *s == ',' || *s == '\t') s++;
        if (!*s) break;
        if (nseg >= CA_SEG_MAX) { set_status(app, "段数超过 %d", CA_SEG_MAX); return; }
        wv = strtol(s, &end, 10);
        if (end == s || wv < 1 || wv > CA_SEG_W) {
            set_status(app, "第 %d 段位宽不对（1~%d）", nseg + 1, CA_SEG_W);
            return;
        }
        width = (int)wv;
        q = end;
        if (*q != '\'') { set_status(app, "第 %d 段少个 '", nseg + 1); return; }
        q++;
        {
            unsigned long long v = 0;
            int                ok = ca_parse_ull(q, 10, &v);
            const char        *p2 = q;

            if (!ok) {
                set_status(app, "第 %d 段的值不是合法整数", nseg + 1);
                return;
            }
            while (*p2 && *p2 != ',' && *p2 != ' ') p2++;
            if (width < 64 && (v >> width) != 0) {
                set_status(app, "第 %d 段的值超出 %d 位", nseg + 1, width);
                return;
            }
            segs[nseg].width = width;
            segs[nseg].value = v;
            nseg++;
            acc = (acc << width) | v;
            used += (unsigned long long)width;
            if (used > CA_SEG_W) { set_status(app, "总位宽超过 %d", CA_SEG_W); return; }
            s = p2;
        }
    }
    if (nseg == 0) { set_status(app, "没有解析出段"); return; }
    snprintf(hex, sizeof(hex), "0x%llX", acc);
    out[0] = '\0';
    for (i = 0; i < nseg; i++) {
        char b[CA_SEG_W + 1];
        int  wlen;

        ca_seg_bin(segs[i].value, segs[i].width, b);
        wlen = snprintf(out + off, sizeof(out) - off, "%s%s", i ? " " : "", b);
        if (wlen < 0 || (size_t)wlen >= sizeof(out) - off) break;
        off += (size_t)wlen;
    }
    snprintf(res, sizeof(res), "%s · %s", hex, out);
    set_box(w(app, "edt_bits_res"), res);
    set_status(app, "拼接完成：%llu 位 · %s", used, hex);
}

/* ============================================================ 回调 */

/**
 * @brief 计算式编辑即算
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_expr_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_calc(cg_app_of(self));
}

/**
 * @brief 「计算」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_calc_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_calc(cg_app_of(self));
}

/**
 * @brief 「查看公式」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_formula_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_formula(cg_app_of(self));
}

/**
 * @brief 时间戳编辑即转 UTC
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_ts_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_ts2utc(cg_app_of(self));
}

/**
 * @brief UTC 编辑即转时间戳
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_utc_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_utc2ts(cg_app_of(self));
}

/**
 * @brief 「时间戳->UTC」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_ts2utc_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_ts2utc(cg_app_of(self));
}

/**
 * @brief 「UTC->时间戳」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_utc2ts_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_utc2ts(cg_app_of(self));
}

/**
 * @brief 「点击生成时间戳(自动复)」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_gen_ts_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_gen_ts(cg_app_of(self));
}

/**
 * @brief 进制单选或转换数字变化：立即转换
 * @param self 控件
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_radix_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_convert(cg_app_of(self));
}

/**
 * @brief 「取反」（转换数字侧）
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_neg_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_negate(cg_app_of(self), "edt_conv", ca_src_base(cg_app_of(self)), "转换数字");
}

/**
 * @brief 「取反」（转换结果侧）
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_neg2_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_negate(cg_app_of(self), "edt_conv_res", ca_dst_base(cg_app_of(self)), "转换结果");
}

/**
 * @brief 长度计算输入变化即算
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_len_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_len(cg_app_of(self));
}

/**
 * @brief 「计算」（长度）
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_len_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_len(cg_app_of(self));
}

/**
 * @brief 「STR转HEX」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_str2hex_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_str2hex(cg_app_of(self));
}

/**
 * @brief 「HEX转STR」按钮
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_hex2str_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_hex2str(cg_app_of(self));
}

/**
 * @brief 「转换」（HEX 加 / 去空格）
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_sp_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_sp(cg_app_of(self));
}

/**
 * @brief 位运算输入变化即转
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_bits_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_bits(cg_app_of(self));
}

/**
 * @brief 「转换」（位运算）
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_bits_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_bits(cg_app_of(self));
}

/**
 * @brief 文本编辑框变化：只提示，不即时转换（原地写回会触发 change 递归）
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_str_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_status(cg_app_of(self), "点「STR转HEX」把文本编成十六进制");
}

/**
 * @brief HEX 编辑框变化：只提示，不即时转换（原地写回会触发 change 递归）
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_hex_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_status(cg_app_of(self), "点「HEX转STR」把十六进制解成文本");
}

/**
 * @brief 待处理 HEX 编辑框变化：只提示
 * @param self 编辑框
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_sp_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_status(cg_app_of(self), "点「转换」按复选框给 HEX 加 / 去空格");
}

/* ============================================================ 生命周期 */

/**
 * @brief 界面就绪钩子：预填全部输入框并跑通每条路径
 * @param app 应用
 * @param user 未使用
 * @return void 无返回值
 * @note 预填样例让 --selftest 合成的每个回调都跑得到真实数据，且全程不弹窗
 */
void cl_init(cg_app_t *app, void *user)
{
    (void)user;

    set_box(w(app, "edt_expr"), "(1+2)*3-4/2");
    do_calc(app);

    set_box(w(app, "edt_ts"), "1762886400");
    do_ts2utc(app);

    set_box(w(app, "edt_conv"), "255");
    do_convert(app);

    set_box(w(app, "edt_len_body"), "Hello, 计算器\n第二行");
    do_len(app);

    set_box(w(app, "edt_str"), "Hi, 计算器");
    set_box(w(app, "edt_sp"), "48692C20");
    do_sp(app);

    set_box(w(app, "edt_bits"), "29'h0, 1'h1, 1'h0, 1'h0");
    do_bits(app);

    set_status(app, "就绪：六个分区都可以用了");
    cg_log(CG_LOG_INFO, "计算器就绪（界面 calculator.cgui）");
}

const cg_binding_t cl_bindings[] = {
    CG_BIND("cl_on_expr_change",    on_expr_change),
    CG_BIND("cl_on_calc_click",     on_calc_click),
    CG_BIND("cl_on_formula_click",  on_formula_click),
    CG_BIND("cl_on_ts_change",      on_ts_change),
    CG_BIND("cl_on_utc_change",     on_utc_change),
    CG_BIND("cl_on_ts2utc_click",   on_ts2utc_click),
    CG_BIND("cl_on_utc2ts_click",   on_utc2ts_click),
    CG_BIND("cl_on_gen_ts_click",   on_gen_ts_click),
    CG_BIND("cl_on_radix_change",   on_radix_change),
    CG_BIND("cl_on_neg_click",      on_neg_click),
    CG_BIND("cl_on_neg2_click",     on_neg2_click),
    CG_BIND("cl_on_len_change",     on_len_change),
    CG_BIND("cl_on_len_click",      on_len_click),
    CG_BIND("cl_on_str2hex_click",  on_str2hex_click),
    CG_BIND("cl_on_hex2str_click",  on_hex2str_click),
    CG_BIND("cl_on_sp_click",       on_sp_click),
    CG_BIND("cl_on_bits_change",    on_bits_change),
    CG_BIND("cl_on_bits_click",     on_bits_click),
    CG_BIND("cl_on_str_change",     on_str_change),
    CG_BIND("cl_on_hex_change",     on_hex_change),
    CG_BIND("cl_on_sp_change",      on_sp_change),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组；argv[1] 是 .cgui 路径，"--selftest" 则跑不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
