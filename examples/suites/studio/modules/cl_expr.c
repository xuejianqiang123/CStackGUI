/**
 * @file    cl_expr.c
 * @brief   calculator 的递归下降表达式求值（纯逻辑：四则 / 括号 / 幂 / 位运算 / 常用函数）
 * @details 从 cl.c（原 1192 行单体）按职责剥离出来：本文件零 UI 依赖，只吃字符串出数值，
 *          因此可单独读、单独推理。对外只导出 ca_eval（声明见 cl_internal.h），
 *          其余 ep_* 一律 static —— 编译单元隔离，合并进同一次链接不冲突。
 *
 *          解析器安全口径：表达式长度 ≤ 512、递归深度 ≤ 16，所有错误走定长 errbuf + snprintf，
 *          除零 / 未知符号 / 括号不配平都给友好错误串。
 *
 *          语法优先级（自低到高）：加减 → 乘除 → 位运算(& | ^ << >>) → 一元(- + ~) → 幂(^，右结合) → 原子。
 */
#include "cl_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 函数表与解析器状态 */

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

/* ------------------------------------------------------------ 词法与原子 */

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

/* ------------------------------------------------------------ 优先级各层 */

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

/* ------------------------------------------------------------ 对外入口 */

/**
 * @brief 求值一个表达式串（入口）
 * @param expr 表达式（C 串）
 * @param out 结果出口
 * @param errbuf 错误串出口（成功时空串）
 * @param errcap errbuf 容量
 * @return int 1 = 成功；0 = 失败（errbuf 有原因）
 */
int ca_eval(const char *expr, double *out, char *errbuf, int errcap)
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
