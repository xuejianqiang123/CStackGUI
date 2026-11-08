/**
 * @file    main.c
 * @brief   深色科技感计算器：上面输入公式、下面得到结果；另一块统计文本长度
 * @details 界面全在 calc.cgui 里（深海军蓝底 + 青蓝强调色），本文件只管业务：
 *            · **公式求值** —— 递归下降解析器，支持 + - × ÷ * /、括号、一元正负、
 *              百分号与小数；先乘除后加减，除数为 0 / 括号不配对 / 式子没写完
 *              都给中文原因，不依赖任何三方库；
 *            · **文本长度统计** —— 多行文本按 Unicode 码点算字符数、按 UTF-8 算字节数、
 *              按换行算行数，中文 1 字符 / 3 字节，边打字边刷新；
 *            · **同一套函数服务多个入口** —— 公式框的 change / submit 与「计算」
 *              按钮的 click 都落在 ca_compute，文本区同理落在 ca_len_show。
 *
 *          `--selftest` 走 cg_app_selftest：不开窗、把 .cgui 里登记的事件全合成
 *          触发一遍，用来确认事件名都在本文件登记过（CI 友好）。
 */
#include "cstackgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 草图里的控件 id：这几个是设计器拖出来的，原样保留，回画布继续微调时不会错位。
 * 业务里用下面这些宏起个可读的名字，两者不是两套控件。 */
#define W_FORMULA   "edit_mw7qrgnwau2i4b8p"      /* 公式输入框 */
#define W_RESULT    "edit_mw7qrgzzqmtmpo8j"      /* 公式结果（只读） */
#define W_TEXT      "edit_mw7qtjrbcstjxrno"      /* 多行文本输入框 */
#define W_LEN       "edit_mw7qvdmu16aknfaq"      /* 长度统计结果（只读） */

/** 公式与文本的最大读入长度：输入框本身不限长，这里只截够算的一段 */
#define CA_BUF_MAX   512
/** 结果 / 错误提示的输出缓冲 */
#define CA_OUT_MAX   128
/** 中文错误原因的缓冲 */
#define CA_ERR_MAX    64

/** 表达式解析器的游标：当前扫描位置 + 第一次出错的中文原因 */
typedef struct {
    const char *p;              /* 指向归一化后的 ASCII 表达式 */
    char        err[CA_ERR_MAX];/* 空串 = 还没出错 */
} ca_parser_t;

/** 文本长度统计的三项结果 */
typedef struct {
    unsigned long chars;        /* Unicode 码点数（中文算 1） */
    unsigned long bytes;        /* UTF-8 字节数（中文算 3） */
    unsigned long lines;        /* 行数，空文本算 0 行 */
} ca_len_t;

/** 全角 / 数学符号 -> ASCII 的对照表；ascii 为 0 表示该字符直接丢掉 */
typedef struct {
    const char *sym;            /* UTF-8 字面量，字节数由 strlen 自己算 */
    char        ascii;
} ca_sym_t;

/** 符号表：× 是 2 字节、− 是 3 字节，交给 strlen 比手写长度可靠 */
static const ca_sym_t k_syms[] = {
    { "×", '*' }, { "÷", '/' }, { "∗", '*' }, { "＊", '*' }, { "／", '/' },
    { "−", '-' }, { "－", '-' }, { "—", '-' }, { "–", '-' },
    { "＋", '+' }, { "（", '(' }, { "）", ')' }, { "％", '%' },
    { "　", 0 }
};

/**
 * @brief 解析加减表达式（前向声明：括号里的子表达式要回头调它）
 * @param ps 解析器游标
 * @return double 表达式的值；解析失败返回 0 且 ps->err 已写好原因
 */
static double ca_expr(ca_parser_t *ps);

/**
 * @brief 记下第一次出错的中文原因（后续解析直接短路，不被后面的错误覆盖）
 * @param ps 解析器游标
 * @param why 中文原因
 * @return void 无返回值
 */
static void ca_fail(ca_parser_t *ps, const char *why)
{
    if (!ps->err[0]) snprintf(ps->err, sizeof(ps->err), "%s", why);
}

/**
 * @brief 跳过表达式里的空格
 * @param ps 解析器游标
 * @return void 无返回值
 */
static void ca_skip(ca_parser_t *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

/**
 * @brief 解析一个「因子」：数字 / 括号子表达式 / 一元正负号，数字后允许跟 %
 * @param ps 解析器游标
 * @return double 该因子的值；解析失败返回 0 且 ps->err 已写好原因
 * @note 50% 折成 0.5，所以 200 × 15% 得 30；百分号只跟在数字后面，
 *       不参与加减运算，避免和取模混淆。
 */
static double ca_factor(ca_parser_t *ps)
{
    char  *end = NULL;
    double v;

    ca_skip(ps);
    if (ps->err[0]) return 0.0;
    if (*ps->p == '+') { ps->p++; return ca_factor(ps); }
    if (*ps->p == '-') { ps->p++; return -ca_factor(ps); }
    if (*ps->p == '(') {
        ps->p++;
        v = ca_expr(ps);
        if (ps->err[0]) return 0.0;
        ca_skip(ps);
        if (*ps->p != ')') { ca_fail(ps, "括号没有配对"); return 0.0; }
        ps->p++;
        return v;
    }
    v = strtod(ps->p, &end);
    if (!end || end == ps->p) { ca_fail(ps, "这里应该是一个数字"); return 0.0; }
    ps->p = end;
    if (*ps->p == '%') { ps->p++; v /= 100.0; }
    return v;
}

/**
 * @brief 解析乘除项：factor (('*' | '/') factor)*
 * @param ps 解析器游标
 * @return double 该项的值；解析失败返回 0 且 ps->err 已写好原因
 */
static double ca_term(ca_parser_t *ps)
{
    double v = ca_factor(ps);

    for (;;) {
        char   op;
        double rhs;

        ca_skip(ps);
        if (ps->err[0]) return 0.0;
        op = *ps->p;
        if (op != '*' && op != '/') return v;
        ps->p++;
        rhs = ca_factor(ps);
        if (ps->err[0]) return 0.0;
        if (op == '/' && rhs == 0.0) { ca_fail(ps, "除数不能为 0"); return 0.0; }
        v = (op == '*') ? v * rhs : v / rhs;
    }
}

/**
 * @brief 解析加减表达式：term (('+' | '-') term)*
 * @param ps 解析器游标
 * @return double 表达式的值；解析失败返回 0 且 ps->err 已写好原因
 * @note 括号里的子表达式也走这里；遇到 ')' 时因为不是 +/- 自然返回，
 *       由 ca_factor 负责收掉右括号。
 */
static double ca_expr(ca_parser_t *ps)
{
    double v = ca_term(ps);

    for (;;) {
        char   op;
        double rhs;

        ca_skip(ps);
        if (ps->err[0]) return 0.0;
        op = *ps->p;
        if (op != '+' && op != '-') return v;
        ps->p++;
        rhs = ca_term(ps);
        if (ps->err[0]) return 0.0;
        v = (op == '+') ? v + rhs : v - rhs;
    }
}

/**
 * @brief 把人写的公式归一化成纯 ASCII，再交给解析器
 * @param src 输入框原文（UTF-8）
 * @param out 归一化结果
 * @param cap out 缓冲字节数
 * @param err 出错时的中文原因
 * @param errcap err 缓冲字节数
 * @return int 1 = 归一化成功；0 = 失败（原因已写进 err）
 * @note 把 × ÷ − － （ ） ＋ 这些全角符号换成 ASCII，顺手丢掉空格、制表符
 *       和千分位逗号；其余非 ASCII 字符一律明确拒绝，不悄悄按 0 算。
 */
static int ca_normalize(const char *src, char *out, size_t cap,
                        char *err, size_t errcap)
{
    size_t i = 0;
    size_t o = 0;

    for (i = 0; src[i];) {
        unsigned char c = (unsigned char)src[i];
        size_t        j;
        int           hit = 0;

        if (o + 8 >= cap) { snprintf(err, errcap, "公式太长了"); return 0; }
        if (c < 0x80) {
            char ch = (char)c;
            if (ch == ' ' || ch == '\t' || ch == ',') { i++; continue; }
            if (!strchr("0123456789.+-*/()%eE", ch)) {
                snprintf(err, errcap, "公式里有认不出的字符");
                return 0;
            }
            out[o++] = ch;
            i++;
            continue;
        }
        /* 非 ASCII：查符号表，长度一律 strlen，不手写 UTF-8 字节数 */
        for (j = 0; j < sizeof(k_syms) / sizeof(k_syms[0]); j++) {
            size_t n = strlen(k_syms[j].sym);
            if (strncmp(src + i, k_syms[j].sym, n) == 0) {
                if (k_syms[j].ascii) out[o++] = k_syms[j].ascii;
                i += n;
                hit = 1;
                break;
            }
        }
        if (!hit) {
            snprintf(err, errcap, "公式里有认不出的字符");
            return 0;
        }
    }
    out[o] = '\0';
    return 1;
}

/**
 * @brief 求值入口：归一化 -> 递归下降解析 -> 收尾检查
 * @param src 输入框原文
 * @param out 求值结果输出
 * @param err 出错时的中文原因输出
 * @param errcap err 缓冲字节数
 * @return int 1 = 成功；0 = 失败（原因已写进 err）
 * @note 解析完还有剩字符，说明式子中间断了或右括号多了；这里按剩余字符
 *       给「括号没有配对」或「认不出的字符」，比统一报「表达式错误」好排查。
 */
static int ca_eval(const char *src, double *out, char *err, size_t errcap)
{
    char         norm[CA_BUF_MAX];
    ca_parser_t  ps;
    double       v;

    if (!ca_normalize(src, norm, sizeof(norm), err, errcap)) return 0;
    ps.p = norm;
    ps.err[0] = '\0';
    ca_skip(&ps);
    if (!*ps.p) { snprintf(err, errcap, "还没有输入公式"); return 0; }

    v = ca_expr(&ps);
    if (ps.err[0]) { snprintf(err, errcap, "%s", ps.err); return 0; }
    ca_skip(&ps);
    if (*ps.p) {
        if (*ps.p == ')') snprintf(err, errcap, "括号没有配对");
        else              snprintf(err, errcap, "公式里有认不出的字符");
        return 0;
    }
    if (v != v || v > 1e308 || v < -1e308) {
        snprintf(err, errcap, "结果超出范围");
        return 0;
    }
    *out = v;
    return 1;
}

/**
 * @brief 把计算结果格式化成简短字符串
 * @param v 待格式化的值
 * @param out 输出缓冲
 * @param cap 输出缓冲字节数
 * @return void 无返回值
 * @note 整数不带小数点（33 而不是 33.000000），小数最多 10 位有效数字；
 *       -0 归一成 0，免得结果显示成 -0。
 */
static void ca_num_text(double v, char *out, size_t cap)
{
    if (v != v) { snprintf(out, cap, "错误"); return; }
    if (v == 0.0) v = 0.0;
    if (v > -1e15 && v < 1e15 && v == (double)(long long)v) {
        snprintf(out, cap, "%lld", (long long)v);
    } else {
        snprintf(out, cap, "%.10g", v);
    }
}

/**
 * @brief 统计一段 UTF-8 文本的长度
 * @param s 文本（UTF-8，可含 \r\n 换行）；NULL 当空文本
 * @param out 三项统计结果输出
 * @return void 无返回值
 * @note 码点数 = 非 0b10xxxxxx 续字节的个数，所以中文按 1 字符 / 3 字节；
 *       行数按换行分段，\r\n 只算一次，空文本算 0 行、只有一行字算 1 行。
 */
static void ca_count(const char *s, ca_len_t *out)
{
    size_t        i;
    unsigned long nl = 0;

    out->chars = 0;
    out->bytes = 0;
    out->lines = 0;
    if (!s) return;
    out->bytes = (unsigned long)strlen(s);
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];

        if ((c & 0xC0u) != 0x80u) out->chars++;
        if (c == '\r') {
            nl++;
            if (s[i + 1] == '\n') i++;
        } else if (c == '\n') {
            nl++;
        }
    }
    if (out->bytes > 0) out->lines = nl + 1;
}

/**
 * @brief 按 id 读控件文字，取不到时返回空串
 * @param app 当前应用
 * @param id 控件 id
 * @return const char* 控件自己的字符串，别 free
 */
static const char *ca_get(cg_app_t *app, const char *id)
{
    cg_widget_t *w = cg_find(app, id);
    const char *s = w ? cg_widget_text(w) : NULL;

    return s ? s : "";
}

/**
 * @brief 按 id 写控件文字，找不到就跳过
 * @param app 当前应用
 * @param id 控件 id
 * @param text 要显示的文字
 * @return void 无返回值
 * @note 自检（--selftest）也在无窗口模式下跑，容错让回调不必分「有没有界面」两套写法。
 */
static void ca_set(cg_app_t *app, const char *id, const char *text)
{
    cg_widget_t *w = cg_find(app, id);

    if (w) cg_widget_set_text(w, text);
}

/**
 * @brief 区分「还没敲完」和「确实算不了」两类错误
 * @param err 中文错误原因
 * @return int 1 = 硬错误（预览时就该提示）；0 = 式子没写完（留白，别闪提示）
 * @note 空公式、末尾挂着运算符、左括号还没闭合都属于输入过程中的正常中间态；
 *       除数为 0、认不出的字符、结果溢出则是用户真需要马上知道的问题。
 */
static int ca_hard_error(const char *err)
{
    return strcmp(err, "还没有输入公式") != 0 &&
           strcmp(err, "这里应该是一个数字") != 0 &&
           strcmp(err, "括号没有配对") != 0;
}

/**
 * @brief 读公式框 -> 求值 -> 把结果（或错误原因）写进结果框
 * @param app 当前应用
 * @param strict 1 = 正式求值，任何错误都给中文原因；0 = 边输入边预览，只提示硬错误
 * @return void 无返回值
 * @note 预览模式不闪「式子没写完」：敲到「12 +」时结果留白；但「除数不能为 0」
 *       这类硬错误当场就显示，用户不必多点一次按钮才知道错在哪。
 *       这里**不**调 cg_widget_set_colors 改字色 —— edit 的 fg 同时被自绘层拿去
 *       画描边，改了会冒出一圈白边；配色统一交给 .cgui 的 styles.result。
 */
static void ca_compute(cg_app_t *app, int strict)
{
    char   src[CA_BUF_MAX];
    char   txt[CA_OUT_MAX];
    char   err[CA_ERR_MAX];
    double v = 0.0;

    snprintf(src, sizeof(src), "%s", ca_get(app, W_FORMULA));
    err[0] = '\0';
    if (ca_eval(src, &v, err, sizeof(err))) {
        ca_num_text(v, txt, sizeof(txt));
        ca_set(app, W_RESULT, txt);
    } else if (strict || ca_hard_error(err)) {
        ca_set(app, W_RESULT, err);
    } else {
        ca_set(app, W_RESULT, "");
    }
}

/**
 * @brief 读多行文本框 -> 统计字符 / 字节 / 行数 -> 写进统计结果框
 * @param app 当前应用
 * @return void 无返回值
 * @note 输入框的 change 和「计算」按钮的 click 都调它，实时刷新与手动重算
 *       走的是同一条路径，不存在两套算法对不上的问题。
 */
static void ca_len_show(cg_app_t *app)
{
    ca_len_t n;
    char     txt[CA_OUT_MAX];

    ca_count(ca_get(app, W_TEXT), &n);
    snprintf(txt, sizeof(txt), "字符 %lu · UTF-8 字节 %lu · 行数 %lu",
             n.chars, n.bytes, n.lines);
    ca_set(app, W_LEN, txt);
}

/**
 * @brief 公式框 change：边输入边预览结果，式子没写完不出错提示
 * @param self 公式输入框
 * @param ev change
 * @param user 业务数据（本示例未用）
 * @return void 无返回值
 */
static void on_formula_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    ca_compute(cg_app_of(self), 0);
}

/**
 * @brief 正式求值：公式框按回车（submit）或点「计算」按钮都走这里
 * @param self 触发事件的控件（可能是 edit，也可能是 button）
 * @param ev submit / click
 * @param user 业务数据（本示例未用）
 * @return void 无返回值
 * @note 同一函数绑两类事件，靠 cg_app_of(self) 拿应用而不假设 self 是谁，
 *       加一个触发入口不用再加一个回调。
 */
static void on_calc(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    ca_compute(cg_app_of(self), 1);
}

/**
 * @brief 多行文本框 change：文本一变就重新统计长度
 * @param self 多行文本输入框
 * @param ev change
 * @param user 业务数据（本示例未用）
 * @return void 无返回值
 */
static void on_len_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    ca_len_show(cg_app_of(self));
}

/**
 * @brief 手动统计：多行框按 Esc（submit）或点「计算」按钮都走这里
 * @param self 触发事件的控件
 * @param ev submit / click
 * @param user 业务数据（本示例未用）
 * @return void 无返回值
 */
static void on_len(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    ca_len_show(cg_app_of(self));
}

/**
 * @brief 界面就绪钩子：按 .cgui 里的预填文字算出首屏结果，再把焦点交给公式框
 * @param app 当前应用
 * @param user 业务数据（本示例未用）
 * @return void 无返回值
 * @note 用 desc.on_ready 而不是 app.events.ready：on_ready 在第一次 WM_SIZE
 *       之后触发，此时改文字不会被窗口初始化打回去。
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *w = cg_find(app, W_FORMULA);

    (void)user;
    ca_compute(app, 1);
    ca_len_show(app);
    if (w) cg_widget_focus(w);
}

/** 事件名 -> C 函数：.cgui 里写的名字必须全在这里登记（--selftest 会查） */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_formula_change", on_formula_change),
    CG_BIND("on_calc",           on_calc),
    CG_BIND("on_len_change",     on_len_change),
    CG_BIND("on_len",            on_len),
    CG_BIND_END
};

/**
 * @brief 程序入口：登记界面文件与绑定表，按参数走自检或正常运行
 * @param argc 命令行参数个数
 * @param argv 命令行参数：argv[1] 可以是 --selftest 或 .cgui 路径
 * @return int 进程退出码，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动去 exe 旁边找 .cgui */
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "calc.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "calc.cgui";
    return cg_app_run(&desc);
}
