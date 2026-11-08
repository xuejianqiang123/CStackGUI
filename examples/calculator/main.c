/**
 * @file    main.c
 * @brief   计算器示例：四则运算、百分比、正负号，支持鼠标与键盘
 * @details 界面在 calculator.cgui，本文件是全部业务逻辑。没有拆模块——一个计算器的
 *          状态机一眼能读完，拆成模块反而要来回跳文件。
 *
 *          状态机只有三样东西：
 *            - acc      左操作数（已经确认下来、显示在屏幕上的那个数）
 *            - cur      当前操作数（按了 = 之后两者相同）
 *            - pend     待执行的运算符，0 表示"没有挂起的运算"
 *          `entering` 区分"用户正在敲数字"和"屏幕上是算出来的结果"：前者显示原始
 *          字符串（所以要保留 "12." 的小数点），后者走 fmt_num 格式化。
 *
 *          键盘：键码是 VK_* 的数值，业务侧不能引 windows.h，所以在下面列成枚举。
 *          键按下时主窗口有焦点就够了——这个窗口里的按钮不带焦点，点按钮不会把
 *          焦点抢走（参见 cg_win_event.c 的按钮分支），所以 keydown 只绑窗口。
 */
#include "cstackgui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_MAX     24    /**< 输入串缓冲区（15 位数字 + 负号 + 小数点 + 余量） */
#define ENTRY_DIGITS  13    /**< 最多输入多少位数字：再多 324px 的显示区就撑破了 */
#define EXPR_MAX      96    /**< 顶部表达式回显缓冲区 */

/** 用得到的虚拟键码。业务侧不许引 windows.h，这里按 VK_* 的取值抄一份。 */
enum {
    KEY_BACK      = 0x08,   /**< Backspace */
    KEY_ENTER     = 0x0D,   /**< 回车（主键盘与小键盘同值） */
    KEY_ESC       = 0x1B,   /**< Esc：全部清除 */
    KEY_PERCENT   = 0x25,   /**< %（Shift + 5） */
    KEY_DELETE    = 0x2E,   /**< Delete：也当退格用 */
    KEY_0         = 0x30,   /**< 主键盘 '0' */
    KEY_9         = 0x39,   /**< 主键盘 '9' */
    KEY_PAD0      = 0x60,   /**< 小键盘 '0' */
    KEY_PAD9      = 0x69,   /**< 小键盘 '9' */
    KEY_MUL       = 0x6A,   /**< 小键盘 '*' */
    KEY_ADD       = 0x6B,   /**< 小键盘 '+' */
    KEY_SUB       = 0x6D,   /**< 小键盘 '-' */
    KEY_DEC       = 0x6E,   /**< 小键盘 '.' */
    KEY_DIV       = 0x6F,   /**< 小键盘 '/' */
    KEY_OEM_PLUS  = 0xBB,   /**< '=' 键：不按 Shift 是等于，按住 Shift 是 '+' */
    KEY_OEM_MINUS = 0xBD,   /**< '-' */
    KEY_OEM_DOT   = 0xBE,   /**< '.' */
    KEY_OEM_SLASH = 0xBF,   /**< '/' */
    KEY_MOD_SHIFT = 0x02    /**< cg_event_keys() 里的 Shift 位 */
};

/** 顶层运算符的符号表：内部用 ASCII，显示用真正的数学符号 */
typedef struct {
    char        op;         /**< 内部码：'+' '-' '*' '/' */
    const char *sym;        /**< 显示符号："+" "−" "×" "÷" */
} op_sym_t;

static const op_sym_t g_op_syms[] = {
    { '+', "+" },
    { '-', "−" },
    { '*', "×" },
    { '/', "÷" },
};

/** 计算器状态 */
typedef struct {
    double acc;                     /**< 左操作数（已确认） */
    double cur;                     /**< 当前操作数（未确认时等于显示值） */
    char   entry[ENTRY_MAX];        /**< 正在输入的原始串，如 "-12." */
    int    entering;                /**< 1 = 用户正在敲数字；0 = 屏幕上是结果 */
    int    have_dot;                /**< 当前输入里已出现小数点 */
    int    error;                   /**< 1 = 上一次运算出错（除以零），只认 AC 与数字 */
    char   pend;                    /**< 待执行运算符，0 = 无 */
    char   last_op;                 /**< 上一次按的运算符：连按 = 时复用 */
    double last_rhs;                /**< 上一次运算的右操作数 */
    char   expr[EXPR_MAX];          /**< 顶部那行回显 */
} calc_t;

static calc_t      g_calc;                      /**< 全局唯一状态 */
static cg_app_t   *g_app;                       /**< 就绪后才有 */
static cg_widget_t *g_lab_value;                /**< 主显示标签（缓存，省得每次 cg_find） */
static cg_widget_t *g_lab_expr;                 /**< 表达式标签 */

/**
 * @brief 把 double 格式化成显示用的字符串
 * @param v 要格式化的数
 * @param buf 输出缓冲区
 * @param n 缓冲区字节数
 * @return void 无返回值
 * @note 12 位有效数字是计算器的甜点：0.1 + 0.2 显示 "0.3" 而不是 0.30000000000000004，
 *       再大就退化成科学计数法（1e+16），保证不会撑破显示区。
 */
static void fmt_num(double v, char *buf, size_t n)
{
    if (v != v) {                               /* NaN：除以零走到这里 */
        snprintf(buf, n, "错误");
        return;
    }
    if (v == 0.0) {                             /* 顺手吃掉 -0 */
        snprintf(buf, n, "0");
        return;
    }
    if (v == floor(v) && fabs(v) < 1e15) {
        snprintf(buf, n, "%.0f", v);
        return;
    }
    snprintf(buf, n, "%.12g", v);
}

/**
 * @brief 取当前输入串的数值
 * @return double 输入串转换结果；空串按 0 处理
 */
static double entry_value(void)
{
    return g_calc.entry[0] ? strtod(g_calc.entry, NULL) : 0.0;
}

/**
 * @brief 取运算符的显示符号
 * @param op 内部运算符
 * @return const char* 符号（"×" 之类），认不出就返回 "?"
 */
static const char *op_symbol(char op)
{
    size_t i;

    for (i = 0; i < sizeof g_op_syms / sizeof g_op_syms[0]; i++) {
        if (g_op_syms[i].op == op) {
            return g_op_syms[i].sym;
        }
    }
    return "?";
}

/**
 * @brief 按当前状态刷新两个标签（唯一的"写界面"出口）
 * @return void 无返回值
 * @note 所有按键最终都落到这里：业务只改 g_calc，显示统一由这一处推出去，
 *       避免"某个分支忘了更新屏幕"这类半更新状态。
 */
static void refresh(void)
{
    char buf[ENTRY_MAX];

    if (!g_lab_value || !g_lab_expr) {
        return;                                 /* --selftest 里可能没有真窗口 */
    }
    if (g_calc.error) {
        cg_widget_set_text(g_lab_value, "不能除以零");
    } else if (g_calc.entering) {
        cg_widget_set_text(g_lab_value, g_calc.entry[0] ? g_calc.entry : "0");
    } else {
        fmt_num(g_calc.cur, buf, sizeof buf);
        cg_widget_set_text(g_lab_value, buf);
    }
    cg_widget_set_text(g_lab_expr, g_calc.expr);
}

/**
 * @brief 回到初始状态（AC / Esc）
 * @return void 无返回值
 */
static void calc_reset(void)
{
    memset(&g_calc, 0, sizeof g_calc);
    g_calc.cur = 0.0;
}

/**
 * @brief 执行一次四则运算
 * @param op 运算符：'+' '-' '*' '/'
 * @param a 左操作数
 * @param b 右操作数
 * @return double 结果；除以零返回 NaN
 * @note NaN 是"出错"的唯一信号：调用方看到它就进 error 态，不去猜别的。
 */
static double apply_op(char op, double a, double b)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return (b == 0.0) ? NAN : a / b;
    default:  return b;
    }
}

/**
 * @brief 进入出错态：清掉挂起的运算，屏幕显示原因
 * @return void 无返回值
 */
static void enter_error(void)
{
    g_calc.error    = 1;
    g_calc.entering = 0;
    g_calc.pend     = 0;
    g_calc.last_op  = 0;
    g_calc.expr[0]  = '\0';
    refresh();
}

/**
 * @brief 按了一个数字键
 * @param d 0-9
 * @return void 无返回值
 */
static void press_digit(int d)
{
    size_t len;

    if (g_calc.error) {
        calc_reset();                           /* 出错后按数字：从头开始 */
    }
    if (!g_calc.entering) {
        g_calc.entering = 1;
        g_calc.have_dot = 0;
        g_calc.entry[0] = '\0';
    }
    len = strlen(g_calc.entry);
    if (len == 0) {
        g_calc.entry[0] = (char)('0' + d);
        g_calc.entry[1] = '\0';
    } else if (len == 1 && g_calc.entry[0] == '0' && !g_calc.have_dot) {
        g_calc.entry[0] = (char)('0' + d);       /* "0" 后面接数字 → 替换前导零 */
    } else if (len < ENTRY_DIGITS) {
        g_calc.entry[len]     = (char)('0' + d);
        g_calc.entry[len + 1] = '\0';
    }
    refresh();
}

/**
 * @brief 按了小数点
 * @return void 无返回值
 * @note 已经在输入且已有小数点时什么都不做；还没输入就先补个 "0."。
 */
static void press_dot(void)
{
    size_t len;

    if (g_calc.error) {
        calc_reset();
    }
    if (!g_calc.entering) {
        g_calc.entering = 1;
        g_calc.have_dot = 0;
        g_calc.entry[0] = '0';
        g_calc.entry[1] = '\0';
    }
    if (g_calc.have_dot) {
        return;
    }
    len = strlen(g_calc.entry);
    if (len + 2 <= ENTRY_MAX) {
        g_calc.entry[len]     = '.';
        g_calc.entry[len + 1] = '\0';
        g_calc.have_dot       = 1;
    }
    refresh();
}

/**
 * @brief 按了运算符：先把前一个挂起的运算算掉，再挂上新的
 * @param op 运算符：'+' '-' '*' '/'
 * @return void 无返回值
 * @note 没输入右操作数就连按运算符时只换符号（3 + 之后按 ×，变成 3 ×），
 *       不会把 3 加两遍——这是和手算一致的行为。
 */
static void press_op(char op)
{
    double r;
    char   a[ENTRY_MAX];

    if (g_calc.error) {
        return;
    }
    if (g_calc.pend) {
        if (g_calc.entering) {
            r = apply_op(g_calc.pend, g_calc.acc, entry_value());
            if (r != r) {
                enter_error();
                return;
            }
            g_calc.acc = r;
        }
    } else {
        g_calc.acc = g_calc.entering ? entry_value() : g_calc.cur;
    }
    g_calc.cur      = g_calc.acc;
    g_calc.entering = 0;
    g_calc.have_dot = 0;
    g_calc.entry[0] = '\0';
    g_calc.pend     = op;
    fmt_num(g_calc.acc, a, sizeof a);
    snprintf(g_calc.expr, sizeof g_calc.expr, "%s %s", a, op_symbol(op));
    refresh();
}

/**
 * @brief 按了等于：算掉挂起的运算；没有挂起就重复上一次运算
 * @return void 无返回值
 */
static void press_eq(void)
{
    double rhs;
    double r;
    char   a[ENTRY_MAX];
    char   b[ENTRY_MAX];

    if (g_calc.error) {
        return;
    }
    if (g_calc.pend) {
        rhs = g_calc.entering ? entry_value() : g_calc.cur;
        fmt_num(g_calc.acc, a, sizeof a);
        fmt_num(rhs, b, sizeof b);
        snprintf(g_calc.expr, sizeof g_calc.expr, "%s %s %s =", a, op_symbol(g_calc.pend), b);
        r = apply_op(g_calc.pend, g_calc.acc, rhs);
        if (r != r) {
            enter_error();
            return;
        }
        g_calc.last_op  = g_calc.pend;
        g_calc.last_rhs = rhs;
        g_calc.pend     = 0;
        g_calc.acc      = r;
        g_calc.cur      = r;
    } else if (g_calc.last_op) {
        rhs = g_calc.entering ? entry_value() : g_calc.cur;
        fmt_num(rhs, a, sizeof a);
        fmt_num(g_calc.last_rhs, b, sizeof b);
        snprintf(g_calc.expr, sizeof g_calc.expr, "%s %s %s =", a, op_symbol(g_calc.last_op), b);
        r = apply_op(g_calc.last_op, rhs, g_calc.last_rhs);
        if (r != r) {
            enter_error();
            return;
        }
        g_calc.acc = r;
        g_calc.cur = r;
    } else {
        g_calc.cur = g_calc.entering ? entry_value() : g_calc.cur;
    }
    g_calc.entering = 0;
    g_calc.have_dot = 0;
    g_calc.entry[0] = '\0';
    refresh();
}

/**
 * @brief 退格：删掉刚输入的最后一位
 * @return void 无返回值
 * @note 只对"正在输入"有效——屏幕上是算出来的结果时，退格不该改动它。
 */
static void press_back(void)
{
    size_t len;

    if (g_calc.error) {
        calc_reset();
        refresh();
        return;
    }
    if (!g_calc.entering) {
        return;
    }
    len = strlen(g_calc.entry);
    if (len > 0) {
        g_calc.entry[len - 1] = '\0';
    }
    if (g_calc.entry[0] == '\0') {
        /* 删空了就留在"0"上，别跳回上一个结果——那会让人以为自己删错了 */
        g_calc.entry[0] = '0';
        g_calc.entry[1] = '\0';
        g_calc.have_dot = 0;
    } else if (strchr(g_calc.entry, '.') == NULL) {
        g_calc.have_dot = 0;
    }
    refresh();
}

/**
 * @brief 正负号：翻转当前数的符号
 * @return void 无返回值
 */
static void press_sign(void)
{
    if (g_calc.error) {
        return;
    }
    if (g_calc.entering) {
        size_t len = strlen(g_calc.entry);

        if (g_calc.entry[0] == '-') {
            memmove(g_calc.entry, g_calc.entry + 1, len);       /* 连同结尾的 '\0' 一起前移 */
        } else if (len > 0 && len + 2 <= ENTRY_MAX) {
            memmove(g_calc.entry + 1, g_calc.entry, len + 1);
            g_calc.entry[0] = '-';
        }
    } else {
        g_calc.cur = -g_calc.cur;
        if (g_calc.pend == 0) {
            g_calc.acc = g_calc.cur;
        }
    }
    refresh();
}

/**
 * @brief 百分比：当前数除以 100
 * @return void 无返回值
 * @note 不做"加上百分之几"的隐式计算——这里只做明确的 ÷100，
 *       算 200 + 10% 请自己写 200 + 20。
 */
static void press_pct(void)
{
    double v;

    if (g_calc.error) {
        return;
    }
    v = g_calc.entering ? entry_value() : g_calc.cur;
    v /= 100.0;
    g_calc.cur      = v;
    g_calc.entering = 0;
    g_calc.have_dot = 0;
    g_calc.entry[0] = '\0';
    if (g_calc.pend == 0) {
        g_calc.acc = v;
    }
    refresh();
}

/**
 * @brief 键盘上的一个动作分发到具体的按键处理
 * @param key 虚拟键码（见上面的枚举）
 * @param mods 功能键位：1=Ctrl 2=Shift 4=Alt
 * @return int 1 = 认得的键；0 = 不管
 */
static int dispatch_key(int key, int mods)
{
    char ch = 0;

    if (key >= KEY_0 && key <= KEY_9) {
        press_digit(key - KEY_0);
        return 1;
    }
    if (key >= KEY_PAD0 && key <= KEY_PAD9) {
        press_digit(key - KEY_PAD0);
        return 1;
    }
    switch (key) {
    case KEY_ENTER:      press_eq();    return 1;
    case KEY_ESC:        calc_reset();  refresh(); return 1;
    case KEY_BACK:
    case KEY_DELETE:     press_back();  return 1;
    case KEY_PERCENT:    press_pct();   return 1;
    case KEY_MUL:        ch = '*'; break;
    case KEY_ADD:        ch = '+'; break;
    case KEY_SUB:        ch = '-'; break;
    case KEY_DIV:
    case KEY_OEM_SLASH:  ch = '/'; break;
    case KEY_OEM_MINUS:  ch = '-'; break;
    case KEY_OEM_DOT:
    case KEY_DEC:        press_dot();   return 1;
    case KEY_OEM_PLUS:
        if (mods & KEY_MOD_SHIFT) {
            ch = '+';                   /* Shift + '=' 是加号 */
        } else {
            press_eq();                 /* 单按 '=' 是等于 */
            return 1;
        }
        break;
    default:
        return 0;
    }
    press_op(ch);
    return 1;
}

/**
 * @brief 键盘按下：把键码翻译成按键动作
 * @param self 窗口伪控件（keydown 绑在窗口上）
 * @param ev keydown
 * @param user 未使用
 * @return void 无返回值
 * @note 主窗口有焦点就能收到；点按钮不会把焦点抢走（按钮分支不 SetFocus），
 *       所以这里绑一次就够，不用给 20 个按钮各绑一遍。
 */
static void on_win_keydown(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    dispatch_key(cg_event_key(self), cg_event_keys(self));
}

/**
 * @brief 按键被点：按 id 分发，不用给 20 个按钮写 20 个回调
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_pad_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_widget_id(self);

    (void)ev;
    (void)user;
    if (!id) {
        return;
    }
    /* btn_0..btn_9 先按前缀认，剩下的按名字认 */
    if (strncmp(id, "btn_", 4) == 0 && id[4] >= '0' && id[4] <= '9' && id[5] == '\0') {
        press_digit(id[4] - '0');
        return;
    }
    if (strcmp(id, "btn_ac") == 0)   { calc_reset();   refresh(); return; }
    if (strcmp(id, "btn_back") == 0) { press_back();   return; }
    if (strcmp(id, "btn_pct") == 0)  { press_pct();    return; }
    if (strcmp(id, "btn_sign") == 0) { press_sign();   return; }
    if (strcmp(id, "btn_dot") == 0)  { press_dot();    return; }
    if (strcmp(id, "btn_eq") == 0)   { press_eq();     return; }
    if (strcmp(id, "btn_add") == 0)  { press_op('+');  return; }
    if (strcmp(id, "btn_sub") == 0)  { press_op('-');  return; }
    if (strcmp(id, "btn_mul") == 0)  { press_op('*');  return; }
    if (strcmp(id, "btn_div") == 0)  { press_op('/');  return; }
}

/**
 * @brief 窗口就绪：接上状态、缓存两个显示标签、画初值
 * @param self 窗口伪控件（id 是 CG_WINDOW_ID）
 * @param ev ready
 * @param user 未使用
 * @return void 无返回值
 */
static void on_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    g_app       = cg_app_of(self);
    g_lab_value = cg_find(g_app, "lab_value");
    g_lab_expr  = cg_find(g_app, "lab_expr");
    calc_reset();
    refresh();
}

/** 绑定表：.cgui 里写的每个事件名都要在这里登记，否则自检会报"找不到回调" */
static const cg_binding_t g_bindings[] = {
    CG_BIND("on_app_ready",   on_app_ready),
    CG_BIND("on_win_keydown", on_win_keydown),
    CG_BIND("on_pad_click",   on_pad_click),
    CG_BIND_END
};

/**
 * @brief 程序入口
 * @param argc 参数个数
 * @param argv argv[1] 可以是 .cgui 路径，或 --selftest
 * @return int 0 = 正常退出；非 0 = 自检失败
 * @note --selftest 走运行时自检（真实窗口 + 合成事件），用于门禁。
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "calculator.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "calculator.cgui";

    printf("计算器示例：鼠标点按键，或直接用键盘输入（数字 / + - * / / Enter / Backspace / Esc）\n");
    return cg_app_run(&desc);
}
