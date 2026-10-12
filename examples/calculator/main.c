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
#include "cstackgui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ 常量与业务状态 */

/** 表达式长度上限（字节） */
#define CA_EXPR_MAX  512
/** 解析递归深度上限 */
#define CA_DEPTH_MAX 16
/** 位运算段数上限 */
#define CA_SEG_MAX   16
/** 位运算单段位宽上限 */
#define CA_SEG_W     64
/** HEX / 二进制文本缓冲 */
#define CA_HEX_MAX   4096
/** 通用输出缓冲 */
#define CA_OUT_MAX   1024
/** 状态栏描述缓冲 */
#define CA_LAST      160

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

/* ============================================================ 表达式求值 */

/** 函数 id（解析器按名字查表，求值时按 id 分支） */
enum {
    CA_F_SQRT = 1, CA_F_CBRT, CA_F_LN, CA_F_LOG, CA_F_LOG2, CA_F_EXP,
    CA_F_SIN, CA_F_COS, CA_F_TAN, CA_F_ASIN, CA_F_ACOS, CA_F_ATAN,
    CA_F_ABS, CA_F_FLOOR, CA_F_CEIL, CA_F_ROUND,
    CA_F_MIN, CA_F_MAX, CA_F_POW
};

/** 函数表：名字 + 参数个数 + id */
static const struct {
    const char *name;
    int         arity;
    int         id;
} k_funcs[] = {
    { "sqrt", 1, CA_F_SQRT }, { "cbrt", 1, CA_F_CBRT },
    { "ln",   1, CA_F_LN   }, { "log",  1, CA_F_LOG  },
    { "log2", 1, CA_F_LOG2 }, { "exp",  1, CA_F_EXP  },
    { "sin",  1, CA_F_SIN  }, { "cos",  1, CA_F_COS  },
    { "tan",  1, CA_F_TAN  }, { "asin", 1, CA_F_ASIN },
    { "acos", 1, CA_F_ACOS }, { "atan", 1, CA_F_ATAN },
    { "abs",  1, CA_F_ABS  }, { "floor",1, CA_F_FLOOR},
    { "ceil", 1, CA_F_CEIL }, { "round",1, CA_F_ROUND},
    { "min",  2, CA_F_MIN  }, { "max",  2, CA_F_MAX  },
    { "pow",  2, CA_F_POW  },
};

/** 解析器状态：串 + 位置 + 深度 + 错误串 */
typedef struct {
    const char *s;
    size_t      pos;
    int         depth;
    char        err[64];
} ca_ep_t;

/**
 * @brief 跳过空白（空格 / 制表 / 换行）
 * @param p 解析器
 * @return void 无返回值
 */
static void ep_skip(ca_ep_t *p)
{
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' || p->s[p->pos] == '\n' ||
           p->s[p->pos] == '\r')
        p->pos++;
}

/**
 * @brief 记一个错误（只记第一次，后者不覆盖）
 * @param p 解析器
 * @param msg 错误文字
 * @return void 无返回值
 */
static void ep_err(ca_ep_t *p, const char *msg)
{
    if (!p->err[0]) snprintf(p->err, sizeof(p->err), "%s", msg);
}

/**
 * @brief 读一个数：十进制 / 小数 / 0x 十六进制 / 0b 二进制
 * @param p 解析器
 * @param out 取值出口
 * @return int 1 = 成功读到；0 = 失败（错误已记账）
 */
static int ep_number(ca_ep_t *p, double *out)
{
    const char *s = p->s + p->pos;
    char       *end = NULL;
    double      v = 0.0;

    if ((s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
        unsigned long long u = strtoull(s + 2, &end, 16);
        if (end == s + 2) { ep_err(p, "0x 后面不是十六进制数"); return 0; }
        v = (double)u;
    } else if ((s[0] == '0') && (s[1] == 'b' || s[1] == 'B')) {
        unsigned long long u = strtoull(s + 2, &end, 2);
        if (end == s + 2) { ep_err(p, "0b 后面不是二进制数"); return 0; }
        v = (double)u;
    } else {
        v = strtod(s, &end);
        if (end == s) { ep_err(p, "这里应该是一个数"); return 0; }
    }
    p->pos += (size_t)(end - s);
    *out = v;
    return 1;
}

/**
 * @brief 加减层（最外层；前向声明：ep_atom 递归处理括号时要先用到）
 * @param p 解析器
 * @return double 表达式值；出错时返回 0（错误记在 p->err）
 */
static double ep_expr(ca_ep_t *p);

/**
 * @brief 原子：数 / 常量 / 函数调用 / 括号表达式
 * @param p 解析器
 * @param out 取值出口
 * @return int 1 = 成功；0 = 失败
 */
static int ep_atom(ca_ep_t *p, double *out)
{
    char name[16];
    int  nlen = 0;

    ep_skip(p);
    if (p->s[p->pos] == '(') {
        p->pos++;
        *out = ep_expr(p);
        if (p->err[0]) return 0;
        ep_skip(p);
        if (p->s[p->pos] != ')') { ep_err(p, "括号不配平：少 )"); return 0; }
        p->pos++;
        return 1;
    }
    /* 常量 pi */
    if ((p->s[p->pos] == 'p' || p->s[p->pos] == 'P') && p->s[p->pos + 1] == 'i') {
        p->pos += 2;
        *out = 3.14159265358979323846;
        return 1;
    }
    /* 常量 e（后面不再跟字母，避免吃掉 exp 之类函数名的头一个字母） */
    if (p->s[p->pos] == 'e' && !(p->s[p->pos + 1] >= 'a' && p->s[p->pos + 1] <= 'z') &&
        !(p->s[p->pos + 1] >= 'A' && p->s[p->pos + 1] <= 'Z')) {
        p->pos++;
        *out = 2.71828182845904523536;
        return 1;
    }
    /* 函数：小写字母开头的一段名字 */
    while ((p->s[p->pos] >= 'a' && p->s[p->pos] <= 'z') && nlen < (int)sizeof(name) - 1)
        name[nlen++] = p->s[p->pos++];
    if (nlen > 0) {
        int i, argc = 0;
        double args[2] = { 0.0, 0.0 };

        name[nlen] = '\0';
        for (i = 0; i < (int)(sizeof(k_funcs) / sizeof(k_funcs[0])); i++) {
            if (strcmp(k_funcs[i].name, name) == 0) break;
        }
        if (i >= (int)(sizeof(k_funcs) / sizeof(k_funcs[0]))) {
            char msg[64];
            snprintf(msg, sizeof(msg), "不认识的函数：%s", name);
            ep_err(p, msg);
            return 0;
        }
        ep_skip(p);
        if (p->s[p->pos] != '(') { ep_err(p, "函数后面要跟 ("); return 0; }
        p->pos++;
        for (argc = 0; argc < k_funcs[i].arity; argc++) {
            args[argc] = ep_expr(p);
            if (p->err[0]) return 0;
            ep_skip(p);
            if (argc + 1 < k_funcs[i].arity) {
                if (p->s[p->pos] != ',') { ep_err(p, "函数参数少个逗号"); return 0; }
                p->pos++;
            }
        }
        ep_skip(p);
        if (p->s[p->pos] != ')') { ep_err(p, "函数括号不配平"); return 0; }
        p->pos++;
        switch (k_funcs[i].id) {
        case CA_F_SQRT:  *out = sqrt(args[0]);  break;
        case CA_F_CBRT:  *out = cbrt(args[0]);  break;
        case CA_F_LN:    *out = log(args[0]);   break;
        case CA_F_LOG:   *out = log10(args[0]); break;
        case CA_F_LOG2:  *out = log2(args[0]);  break;
        case CA_F_EXP:   *out = exp(args[0]);   break;
        case CA_F_SIN:   *out = sin(args[0]);   break;
        case CA_F_COS:   *out = cos(args[0]);   break;
        case CA_F_TAN:   *out = tan(args[0]);   break;
        case CA_F_ASIN:  *out = asin(args[0]);  break;
        case CA_F_ACOS:  *out = acos(args[0]);  break;
        case CA_F_ATAN:  *out = atan(args[0]);  break;
        case CA_F_ABS:   *out = fabs(args[0]);  break;
        case CA_F_FLOOR: *out = floor(args[0]); break;
        case CA_F_CEIL:  *out = ceil(args[0]);  break;
        case CA_F_ROUND: *out = round(args[0]); break;
        case CA_F_MIN:   *out = args[0] < args[1] ? args[0] : args[1]; break;
        case CA_F_MAX:   *out = args[0] > args[1] ? args[0] : args[1]; break;
        case CA_F_POW:   *out = pow(args[0], args[1]); break;
        default:         *out = 0.0; break;
        }
        return 1;
    }
    return ep_number(p, out);
}

/**
 * @brief 幂运算：右结合，高于一元负号
 * @param p 解析器
 * @return double 值
 */
static double ep_power(ca_ep_t *p)
{
    double base;

    if (!ep_atom(p, &base)) return 0.0;
    ep_skip(p);
    if (p->s[p->pos] == '^') {
        double e;
        p->pos++;
        e = ep_power(p);                 /* 右结合：2^3^2 = 2^(3^2) */
        if (p->err[0]) return 0.0;
        return pow(base, e);
    }
    return base;
}

/**
 * @brief 一元运算：-x / ~x / +x（取反只对整数值有意义）
 * @param p 解析器
 * @return double 值
 */
static double ep_unary(ca_ep_t *p)
{
    char op;

    ep_skip(p);
    op = p->s[p->pos];
    if (op == '-' || op == '+' || op == '~') {
        double v;
        p->pos++;
        if (p->depth >= CA_DEPTH_MAX) { ep_err(p, "表达式套得太深"); return 0.0; }
        p->depth++;
        v = ep_unary(p);
        p->depth--;
        if (p->err[0]) return 0.0;
        if (op == '-') return -v;
        if (op == '+') return v;
        return (double)(~(long long)v);
    }
    return ep_power(p);
}

/**
 * @brief 位运算层：& | ^ << >>（操作数取整后按 64 位算，结果回 double）
 * @param p 解析器
 * @return double 值
 */
static double ep_bitor(ca_ep_t *p)
{
    double left = ep_unary(p);

    for (;;) {
        char op;

        if (p->err[0]) return 0.0;
        ep_skip(p);
        op = p->s[p->pos];
        if (op != '&' && op != '|' && op != '^' && op != '<' && op != '>') break;
        if ((op == '<' || op == '>') && p->s[p->pos + 1] != op) break;  /* 单个 < > 不是运算符 */
        p->pos += (op == '<' || op == '>') ? 2 : 1;
        {
            double right = ep_unary(p);
            long long a, b, r = 0;

            if (p->err[0]) return 0.0;
            a = (long long)left;
            b = (long long)right;
            switch (op) {
            case '&': r = a & b; break;
            case '|': r = a | b; break;
            case '^': r = a ^ b; break;
            case '<': if (b < 0 || b > 63) { ep_err(p, "移位量要在 0~63"); return 0.0; }
                      r = (long long)((unsigned long long)a << b); break;
            case '>': if (b < 0 || b > 63) { ep_err(p, "移位量要在 0~63"); return 0.0; }
                      r = (long long)((unsigned long long)a >> b); break;
            default: break;
            }
            left = (double)r;
        }
    }
    return left;
}

/**
 * @brief 乘除层：* / %（% 走 fmod）
 * @param p 解析器
 * @return double 值
 */
static double ep_term(ca_ep_t *p)
{
    double left = ep_bitor(p);

    for (;;) {
        char op;

        if (p->err[0]) return 0.0;
        ep_skip(p);
        op = p->s[p->pos];
        if (op != '*' && op != '/' && op != '%') break;
        p->pos++;
        {
            double right = ep_bitor(p);

            if (p->err[0]) return 0.0;
            if (op == '*') left *= right;
            else if (op == '/') {
                if (right == 0.0) { ep_err(p, "除数为 0"); return 0.0; }
                left /= right;
            } else {
                if (right == 0.0) { ep_err(p, "取余的除数为 0"); return 0.0; }
                left = fmod(left, right);
            }
        }
    }
    return left;
}

/**
 * @brief 加减层（最外层）
 * @param p 解析器
 * @return double 值
 */
static double ep_expr(ca_ep_t *p)
{
    double left = ep_term(p);

    for (;;) {
        char op;

        if (p->err[0]) return 0.0;
        ep_skip(p);
        op = p->s[p->pos];
        if (op != '+' && op != '-') break;
        p->pos++;
        {
            double right = ep_term(p);
            if (p->err[0]) return 0.0;
            left = (op == '+') ? left + right : left - right;
        }
    }
    return left;
}

/**
 * @brief 求值一个表达式串（入口）
 * @param expr 表达式（C 串）
 * @param out 结果出口
 * @param errbuf 错误串出口（成功时空串）
 * @param errcap errbuf 容量
 * @return int 1 = 成功；0 = 失败（errbuf 有原因）
 */
static int ca_eval(const char *expr, double *out, char *errbuf, int errcap)
{
    ca_ep_t p;

    errbuf[0] = '\0';
    if (!expr || !*expr) { snprintf(errbuf, (size_t)errcap, "计算式是空的"); return 0; }
    if (strlen(expr) > CA_EXPR_MAX) { snprintf(errbuf, (size_t)errcap, "表达式太长（>%d）", CA_EXPR_MAX); return 0; }
    memset(&p, 0, sizeof(p));
    p.s = expr;
    *out = ep_expr(&p);
    if (p.err[0]) { snprintf(errbuf, (size_t)errcap, "%s", p.err); return 0; }
    ep_skip(&p);
    if (p.s[p.pos] != '\0') { snprintf(errbuf, (size_t)errcap, "结尾有多余字符：%c", p.s[p.pos]); return 0; }
    if (isnan(*out) || isinf(*out)) { snprintf(errbuf, (size_t)errcap, "结果不是有限数"); return 0; }
    return 1;
}

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
static void on_ready(cg_app_t *app, void *user)
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

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_expr_change",    on_expr_change),
    CG_BIND("on_calc_click",     on_calc_click),
    CG_BIND("on_formula_click",  on_formula_click),
    CG_BIND("on_ts_change",      on_ts_change),
    CG_BIND("on_utc_change",     on_utc_change),
    CG_BIND("on_ts2utc_click",   on_ts2utc_click),
    CG_BIND("on_utc2ts_click",   on_utc2ts_click),
    CG_BIND("on_gen_ts_click",   on_gen_ts_click),
    CG_BIND("on_radix_change",   on_radix_change),
    CG_BIND("on_neg_click",      on_neg_click),
    CG_BIND("on_neg2_click",     on_neg2_click),
    CG_BIND("on_len_change",     on_len_change),
    CG_BIND("on_len_click",      on_len_click),
    CG_BIND("on_str2hex_click",  on_str2hex_click),
    CG_BIND("on_hex2str_click",  on_hex2str_click),
    CG_BIND("on_sp_click",       on_sp_click),
    CG_BIND("on_bits_change",    on_bits_change),
    CG_BIND("on_bits_click",     on_bits_click),
    CG_BIND("on_str_change",     on_str_change),
    CG_BIND("on_hex_change",     on_hex_change),
    CG_BIND("on_sp_change",      on_sp_change),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组；argv[1] 是 .cgui 路径，"--selftest" 则跑不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动在 exe 旁边找 .cgui */
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "calculator.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "calculator.cgui";
    cg_log(CG_LOG_INFO, "计算器启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
