/**
 * @file    lf.c
 * @brief   logfilter —— 套件模块（原 examples\logfilter 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 lf_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 lf_bindings、原 on_ready 变成 lf_init、
 *          窗口事件改成直连转发 lf_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   日志过滤器：打开文本日志 → 关键字过滤 → 终端 + 行号表 → 导出 / 复制
 * @details 界面在 logfilter.cgui 里；本文件只有行为和「启动时该干什么」。
 *
 *          这个示例专门把「文件怎么读」这条能力边界说清楚：
 *
 *          - 运行时的文件接口只有**整读**：一次把文件读成一块。**头文件里没有
 *            "按偏移读一块 / 流式读"的接口** —— 想分块读只能自己 include Windows 头，
 *            那违反本工程"业务只 include cstackgui.h"的铁律。所以这里走**整读 + 大小上限**：
 *            用问大小的接口先量一遍，超过 32 MB 就只取前 32 MB 并明确提示"请先切分" ——
 *            宁可少看一段，也不悄悄把几百 MB 读进内存。
 *            （运行时自己的摘要 / CRC 校验内部是分块读的，但那条路只吐摘要、拿不到字节。）
 *
 *          - 大缓冲一律是**文件级 static 数组**（工作窗口 32 MB / 攒块 64 KB /
 *            剪贴板 256 KB），业务代码不 malloc、不 free。这是示例的取舍：
 *            内存账先摆明、代码路径短。代价是 exe 的 BSS 多 32 MB，且一次只能开一份日志；
 *            真实项目要同时开多个，就该把这套收进结构体 + 堆分配。
 *            框架整读返回的那块内存拷完立刻还回去，工作窗口就是 static 那块。
 *
 *          - **没有正则**：运行时没有正则接口（也没有子串查找），所以两个关键字框的
 *            组合方式是**「与 / 或」**（勾选框）+ 反向开关；子串匹配是自写的，
 *            忽略大小写只对 ASCII 字母折叠。
 *
 *          自检（--selftest）不开窗：就绪钩子里不读文件、只写开场白；
 *          弹对话框 / 写盘 / 读大文件之前一律先问有没有窗口。
 *
 * 编译：在项目根跑 build-app.bat logfilter
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 常量 */

/** 整读上限：超过它只取前这么多字节（运行时没有分块读接口，见文件头） */
#define LF_MAX_BYTES   (32 * 1024 * 1024)
/** 行首预览最多取多少字节（正好是表格"内容"那一列） */
#define LF_PREVIEW     80
/** 终端最多回显多少行命中（再多只进表格 / 导出，免得终端刷到卡） */
#define LF_TERM_LINES  2000
/** 表格最多填多少行命中 */
#define LF_TABLE_ROWS  5000
/** 导出攒块缓冲大小；满了就写盘（首块覆盖写，之后追加） */
#define LF_IO_CAP      65536
/** 剪贴板文本上限（写剪贴板要整串，所以得先攒在 static 缓冲里） */
#define LF_CLIP_CAP    262144

/** 扫描结果的去向 */
#define LF_OUT_VIEW    0
#define LF_OUT_EXPORT  1
#define LF_OUT_CLIP    2

/* ------------------------------------------------------------ 业务状态 */

/** 一次过滤的条件：从控件读出来存好，扫描循环里就不再碰控件 */
typedef struct {
    char a[128];      /**< 关键字 A；空串 = 这一条不参与 */
    char b[128];      /**< 关键字 B；空串 = 这一条不参与 */
    int  ignore_case; /**< 非 0 = 忽略大小写（只对 ASCII 字母折叠） */
    int  use_or;      /**< 非 0 = A 或 B 命中即算；0 = 两个都命中才算 */
    int  invert;      /**< 非 0 = 反向：只保留**不**满足条件的行 */
} lf_cond_t;

/** 业务状态：回调里读，装载 / 扫描里写 */
typedef struct {
    char          path[1024];        /**< 当前日志路径（导出文件头要写它） */
    char          export_path[1024]; /**< 导出目标路径（导出模式用） */
    long long     size;              /**< 问到的字节数 */
    int           win_len;           /**< 工作窗口里有效字节数 */
    int           loaded;            /**< 有没有成功打开过文件 */
    int           truncated;         /**< 超过上限、只取了前一段 */
    int           total_lines;       /**< 上一次扫描数出的总行数 */
    int           hits;              /**< 上一次扫描命中的行数 */
    unsigned long elapsed_ms;        /**< 上一次扫描耗时（毫秒） */
    int           shown_lines;       /**< 终端 / 表格实际写了几行命中 */
    int           exp_used;          /**< 攒块缓冲里攒了多少字节 */
    int           exp_first;         /**< 导出还没写过第一块（首块覆盖写） */
    int           exp_failed;        /**< 导出过程中写盘失败 */
    int           clip_len;          /**< 剪贴板缓冲里攒了多少字节 */
    int           clip_cut;          /**< 剪贴板内容被上限截断过 */
} lf_t;

static unsigned char s_window[LF_MAX_BYTES]; /**< 日志内容的唯一工作窗口（static，见文件头取舍） */
static char          s_io[LF_IO_CAP];        /**< 导出攒块缓冲 */
static char          s_clip[LF_CLIP_CAP];    /**< 剪贴板文本攒块缓冲 */
static lf_t          s_lf;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 截一段文本到缓冲里（按字节截，但不会截出半个 UTF-8 汉字）
 * @param dst 输出缓冲，函数写进以 '\0' 结尾的文本
 * @param cap dst 的字节数
 * @param src 源文本（不必以 '\0' 结尾）
 * @param len 源文本字节数；传 -1 表示按字符串长度取
 * @return void 无返回值
 */
static void lf_trunc_utf8(char *dst, int cap, const char *src, int len)
{
    if (cap <= 0 || !dst) return;
    if (len < 0) len = (int)strlen(src);
    if (len > cap - 1) len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    /* 尾巴落在 UTF-8 续字节（0x80..0xBF）上就往前退，免得比较 / 显示出现半个字 */
    while (len > 0 && (((unsigned char)dst[len - 1] & 0xC0) == 0x80)) dst[--len] = '\0';
}

/**
 * @brief 在定长的一段文本里找子串（自写：运行时没有子串查找与正则接口）
 * @param hay 被查找的文本（不必以 '\0' 结尾）
 * @param len hay 的字节数
 * @param needle 要找的子串，必须以 '\0' 结尾（空串视为不参与，返回 0）
 * @param ignore_case 非 0 = 忽略大小写（只折叠 ASCII 字母，中文不受影响）
 * @return int 1 找到、0 没找到或参数为空
 */
static int lf_contains(const char *hay, int len, const char *needle, int ignore_case)
{
    int nl = (int)strlen(needle);
    int i, j;

    if (nl <= 0 || nl > len) return 0;
    for (i = 0; i + nl <= len; i++) {
        for (j = 0; j < nl; j++) {
            unsigned char x = (unsigned char)hay[i + j];
            unsigned char y = (unsigned char)needle[j];

            if (ignore_case) {
                if (x >= 'A' && x <= 'Z') x = (unsigned char)(x - 'A' + 'a');
                if (y >= 'A' && y <= 'Z') y = (unsigned char)(y - 'A' + 'a');
            }
            if (x != y) break;
        }
        if (j == nl) return 1;
    }
    return 0;
}

/**
 * @brief 把两个关键字框 + 三个勾选框的当前值读成一份过滤条件
 * @param app 应用
 * @param c 条件出口，进来不必清零（函数自己清零）
 * @return void 无返回值
 */
static void lf_read_cond(cg_app_t *app, lf_cond_t *c)
{
    const char *ta = cg_widget_text(cg_find(app, "edt_kw_a"));
    const char *tb = cg_widget_text(cg_find(app, "edt_kw_b"));

    memset(c, 0, sizeof(*c));
    lf_trunc_utf8(c->a, sizeof(c->a), ta ? ta : "", -1);
    lf_trunc_utf8(c->b, sizeof(c->b), tb ? tb : "", -1);
    c->ignore_case = cg_widget_checked(cg_find(app, "chk_case"));
    c->use_or      = cg_widget_checked(cg_find(app, "chk_or"));
    c->invert      = cg_widget_checked(cg_find(app, "chk_invert"));
}

/**
 * @brief 判一行命中不命中
 * @param c 过滤条件（空关键字不参与运算）
 * @param line 行内容（不必以 '\0' 结尾）
 * @param len line 的字节数
 * @return int 1 命中、0 不命中
 * @note 两个关键字都空时按"全部行命中"算；此时勾了反向就是 0 行 —— 这是有意的语义
 */
static int lf_match(const lf_cond_t *c, const char *line, int len)
{
    int a_on = (c->a[0] != '\0');
    int b_on = (c->b[0] != '\0');
    int base;

    if (!a_on && !b_on) {
        base = 1;
    } else if (c->use_or) {
        base = (a_on && lf_contains(line, len, c->a, c->ignore_case)) ||
               (b_on && lf_contains(line, len, c->b, c->ignore_case));
    } else {
        base = (!a_on || lf_contains(line, len, c->a, c->ignore_case)) &&
               (!b_on || lf_contains(line, len, c->b, c->ignore_case));
    }
    return c->invert ? !base : base;
}

/**
 * @brief 把条件拼成一句人话（写状态栏第二段 / 导出文件头）
 * @param c 过滤条件
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void lf_cond_text(const lf_cond_t *c, char *out, int cap)
{
    char full[512];
    int  any = (c->a[0] != '\0' || c->b[0] != '\0');

    /* 先拼进定长局部缓冲（关键字各限 100 字节，输出上限 ≈185，512 装得下），
     * 再按调用方的容量截一次。直接往 out 里 snprintf 的话 cap 是运行期值，
     * gcc 证明不了放得下，会一路报 -Wformat-truncation。 */
    snprintf(full, sizeof(full), "%s%.100s%s%.100s%s%s%s",
             any ? (c->use_or ? "或: " : "与: ") : "无关键字",
             c->a[0] ? c->a : "",
             (c->a[0] && c->b[0]) ? " / " : "",
             c->b[0] ? c->b : "",
             any ? "" : "（全部行）",
             c->invert ? "（反向）" : "",
             c->ignore_case ? "（忽略大小写）" : "");
    lf_trunc_utf8(out, cap, full, -1);
}

/**
 * @brief 把字节数写成人看的 KB / MB / GB
 * @param bytes 字节数
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void lf_human_size(long long bytes, char *out, int cap)
{
    if (bytes >= 1024LL * 1024 * 1024) {
        snprintf(out, (size_t)cap, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(out, (size_t)cap, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, (size_t)cap, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, (size_t)cap, "%lld B", bytes);
    }
}

/**
 * @brief 往结果终端写一行（本示例唯一的日志出口）
 * @param app 应用
 * @param dir 方向：CG_TERM_RX 命中行 / CG_TERM_TX 用户操作 / CG_TERM_SYS 程序说明
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void lf_note(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_term_write(cg_find(app, "term_out"), dir, buf, -1);
}

/* ------------------------------------------------------------ 导出（攒块 + 写盘） */

/**
 * @brief 把攒块缓冲里的内容写盘：首块覆盖写、之后追加
 * @return int 0 成功、-1 写盘失败
 * @note 攒块是为了不整份结果驻留内存；"第一块"这个状态必须记住，否则会把前面的内容覆盖掉
 */
static int lf_export_flush(void)
{
    int rc;

    if (s_lf.exp_used <= 0) return 0;
    s_io[s_lf.exp_used] = '\0';
    rc = s_lf.exp_first ? cg_file_write(s_lf.export_path, s_io)
                        : cg_file_append(s_lf.export_path, s_io);
    if (rc != 0) return -1;
    s_lf.exp_first = 0;
    s_lf.exp_used = 0;
    return 0;
}

/**
 * @brief 往导出攒块缓冲追加一段文本，快满时先写盘
 * @param text 要追加的文本，必须以 '\0' 结尾
 * @param len text 的字节数
 * @return int 0 成功、-1 写盘失败或单条就超缓冲
 */
static int lf_io_append(const char *text, int len)
{
    if (len <= 0) return 0;
    if (len > (int)sizeof(s_io) - 1) return -1;         /* 单条超缓冲：纯防御 */
    if (s_lf.exp_used + len > (int)sizeof(s_io) - 32) {
        if (lf_export_flush() != 0) return -1;
    }
    memcpy(s_io + s_lf.exp_used, text, (size_t)len);
    s_lf.exp_used += len;
    return 0;
}

/**
 * @brief 导出开始：把文件头（源文件 / 时间 / 条件）攒进缓冲
 * @param c 本次过滤条件（写进文件头，方便日后核对）
 * @return int 0 成功、-1 缓冲异常
 */
static int lf_export_begin(const lf_cond_t *c)
{
    char *now = cg_time_now(NULL);       /* 堆串，用完还回去 */
    char  head[512];
    char  cond[256];

    lf_cond_text(c, cond, (int)sizeof(cond));
    /* 路径与条件都限长：head 只有 512 字节，不限长 gcc 会报 -Wformat-truncation */
    /* 三个 %s 都要限长：head 只有 512 字节，按"路径 240 + 条件 120 + 时间 40 + 前后缀"
     * 算起来才稳定装得下（这是实测出来的：限到 300/150 时 gcc 仍报截断） */
    snprintf(head, sizeof(head),
             "# CStackGUI 日志过滤器 · 导出\n# 源文件: %.240s\n# 条件: %.120s\n# 导出时间: %.40s\n",
             s_lf.path[0] ? s_lf.path : "(未打开文件)", cond, now ? now : "?");
    cg_free(now);
    s_lf.exp_used = 0;
    s_lf.exp_first = 1;
    s_lf.exp_failed = 0;
    return lf_io_append(head, (int)strlen(head));
}

/**
 * @brief 追加一条命中行（行号 + 制表符 + 内容）
 * @param lineno 行号（1 起）
 * @param prev 行内容预览（已截到预览上限）
 * @return int 0 成功、-1 写盘失败
 */
static int lf_export_line(int lineno, const char *prev)
{
    char buf[LF_PREVIEW + 40];

    snprintf(buf, sizeof(buf), "%d\t%s\n", lineno, prev);
    return lf_io_append(buf, (int)strlen(buf));
}

/**
 * @brief 导出收尾：追加一行统计
 * @param hits 命中行数
 * @param total 总行数
 * @param ms 耗时毫秒
 * @return int 0 成功、-1 缓冲异常
 */
static int lf_export_tail(int hits, int total, unsigned long ms)
{
    char buf[160];

    snprintf(buf, sizeof(buf), "# 命中 %d 行 / 共 %d 行 / 耗时 %lu ms\n", hits, total, ms);
    return lf_io_append(buf, (int)strlen(buf));
}

/**
 * @brief 往表格加一行：行号 + 内容预览
 * @param tbl 行号表控件
 * @param lineno 行号（1 起）
 * @param prev 行内容预览
 * @return void 无返回值
 */
static void lf_table_row(cg_widget_t *tbl, int lineno, const char *prev)
{
    char        no[24];
    const char *cells[2];

    snprintf(no, sizeof(no), "%d", lineno);
    cells[0] = no;
    cells[1] = prev;
    cg_table_add_row(tbl, cells, 2);     /* 表格内部会拷贝文字，这两个栈缓冲可以马上失效 */
}

/**
 * @brief 往剪贴板缓冲追加一条命中行（带行号）
 * @param lineno 行号（1 起）
 * @param prev 行内容预览
 * @return void 无返回值
 */
static void lf_clip_line(int lineno, const char *prev)
{
    char buf[LF_PREVIEW + 40];
    int  n;

    snprintf(buf, sizeof(buf), "%6d  %s\n", lineno, prev);
    n = (int)strlen(buf);
    if (s_lf.clip_len + n >= (int)sizeof(s_clip) - 1) {
        s_lf.clip_cut = 1;               /* 超上限：后面的行丢掉，但"说得出丢了" */
        return;
    }
    memcpy(s_clip + s_lf.clip_len, buf, (size_t)n);
    s_lf.clip_len += n;
}

/* ------------------------------------------------------------ 扫描引擎 */

/**
 * @brief 按当前条件扫一遍工作窗口，把命中行送去终端 + 表格 / 导出文件 / 剪贴板
 * @param app 应用
 * @param mode 去向：显示 / 导出 / 剪贴板
 * @return void 无返回值
 * @note 行是按换行现场切的（末尾回车丢掉），不预先建行索引表：重滤时重扫一遍即可，
 *       省一张几十万项的偏移数组 —— 这也是 static 缓冲区能压到三块的原因
 */
static void lf_scan(cg_app_t *app, int mode)
{
    cg_widget_t  *term = cg_find(app, "term_out");
    cg_widget_t  *tbl  = cg_find(app, "tbl_hits");
    cg_widget_t  *prg  = cg_find(app, "prg_work");
    cg_widget_t  *sb   = cg_find(app, "sb_main");
    lf_cond_t     cond;
    unsigned long t0 = cg_now_ms();
    int           pos = 0, lineno = 0, hits = 0, shown = 0;

    lf_read_cond(app, &cond);

    if (mode == LF_OUT_VIEW) {
        if (prg) cg_widget_set_value(prg, 2);            /* 阶段 2：过滤中 */
        cg_table_clear(tbl);
        cg_term_clear(term);
    } else if (mode == LF_OUT_EXPORT) {
        if (lf_export_begin(&cond) != 0) s_lf.exp_failed = 1;
    } else {
        s_lf.clip_len = 0;
        s_lf.clip_cut = 0;
        s_clip[0] = '\0';
    }

    /* 命中行的表格填法用**批量更新**围起来：表格每加一行都会整表重建，命中几百行时
     * 不围起来就是几百次重建 —— 界面上看着一直在闪（与 dataview 同一个坑）。
     * 只围"查看"这条路：导出 / 剪贴板两条路不碰表格 */
    cg_table_begin_update(tbl);
    while (pos < s_lf.win_len) {
        char prev[LF_PREVIEW + 8];
        int  start = pos, len;

        while (pos < s_lf.win_len && s_window[pos] != '\n') pos++;
        len = pos - start;
        if (len > 0 && s_window[start + len - 1] == '\r') len--;   /* CRLF 只留内容 */
        if (pos < s_lf.win_len) pos++;                             /* 吃掉换行符 */
        lineno++;

        if (!lf_match(&cond, (const char *)s_window + start, len)) continue;
        lf_trunc_utf8(prev, sizeof(prev), (const char *)s_window + start, len);
        hits++;

        if (mode == LF_OUT_VIEW) {
            if (shown < LF_TERM_LINES) lf_note(app, CG_TERM_RX, "行 %d: %s", lineno, prev);
            if (shown < LF_TABLE_ROWS) lf_table_row(tbl, lineno, prev);
            shown++;
        } else if (mode == LF_OUT_EXPORT) {
            if (lf_export_line(lineno, prev) != 0) {       /* 写盘失败就停，宁可少写不写坏 */
                s_lf.exp_failed = 1;
                break;
            }
        } else {
            lf_clip_line(lineno, prev);
        }
    }

    cg_table_end_update(tbl);     /* 与上面的 begin 成对：到这里才重建一次表格 */

    s_lf.total_lines = lineno;
    s_lf.hits        = hits;
    s_lf.shown_lines = shown;
    s_lf.elapsed_ms  = cg_now_ms() - t0;

    if (mode == LF_OUT_VIEW) {
        char human[32], cond_txt[256];

        if (prg) cg_widget_set_value(prg, 3);              /* 阶段 3：完成 */
        lf_human_size(s_lf.size, human, sizeof(human));
        lf_cond_text(&cond, cond_txt, (int)sizeof(cond_txt));
        if (sb) {
            cg_status_setf(sb, 0, "%s · %s%s",
                           s_lf.path[0] ? cg_path_name(s_lf.path) : "（没打开文件）",
                           human, s_lf.truncated ? "（已截断到 32 MB）" : "");
            cg_status_setf(sb, 1, "条件：%s", cond_txt);
            cg_status_setf(sb, 2, "命中 %d 行 / 共 %d 行 / 耗时 %lu ms",
                           hits, lineno, s_lf.elapsed_ms);
        }
        if (shown > LF_TERM_LINES) {
            lf_note(app, CG_TERM_SYS, "（终端只回显前 %d 行命中，其余 %d 行见上面的行号表 / 导出文件）",
                    LF_TERM_LINES, shown - LF_TERM_LINES);
        }
    } else if (mode == LF_OUT_CLIP) {
        s_clip[s_lf.clip_len < LF_CLIP_CAP ? s_lf.clip_len : LF_CLIP_CAP - 1] = '\0';
    } else if (!s_lf.exp_failed) {
        (void)lf_export_tail(hits, lineno, s_lf.elapsed_ms);
    }
}

/* ------------------------------------------------------------ 打开文件 */

/**
 * @brief 打开一个日志文件：先问大小，超上限只取前一段，再整读进工作窗口
 * @param app 应用
 * @param path 文件路径（UTF-8）
 * @return void 无返回值（成功与否都通过终端 / 状态栏说话）
 * @note 这里的"整读"是能力边界不是偷懒：运行时没有按偏移 / 流式读的接口（见文件头），
 *       所以尺寸保护只能在读之前用问大小的接口拦住
 */
static void lf_load(cg_app_t *app, const char *path)
{
    cg_widget_t   *prg = cg_find(app, "prg_work");
    unsigned char *raw;
    long long      size = cg_file_size(path);
    int            len = 0, take;

    if (size < 0) {
        lf_note(app, CG_TERM_SYS, "读不到文件大小：%s（不存在、是目录或被占用）", path);
        return;
    }
    snprintf(s_lf.path, sizeof(s_lf.path), "%s", path);
    s_lf.size      = size;
    s_lf.loaded    = 0;
    s_lf.win_len   = 0;
    s_lf.truncated = 0;
    if (prg) cg_widget_set_value(prg, 0);                 /* 阶段 0：准备读取 */

    if (size > LF_MAX_BYTES) {
        s_lf.truncated = 1;
        lf_note(app, CG_TERM_SYS,
                "文件 %lld 字节超过 32 MB 上限：只取前 32 MB 参与过滤。"
                "要处理这么大的日志，请先切成几段再看。", size);
    }

    raw = cg_file_read_bytes(path, &len);                 /* 整读：返回堆缓冲，用完还回去 */
    if (!raw) {
        lf_note(app, CG_TERM_SYS, "读不进来：%s", path);
        return;
    }
    take = len;
    if (take > LF_MAX_BYTES) take = LF_MAX_BYTES;         /* 尺寸与读到的长度对不上时再兜一刀 */
    memcpy(s_window, raw, (size_t)take);
    cg_free(raw);                                         /* 框架那块立刻还回去，工作窗口是 static 的 */
    s_lf.win_len = take;
    s_lf.loaded  = 1;
    if (prg) cg_widget_set_value(prg, 1);                 /* 阶段 1：已读取 */

    {
        char  human[32];
        char *txt;

        lf_human_size(size, human, sizeof(human));
        lf_note(app, CG_TERM_TX, "已打开 %s（%s，参与过滤 %d 字节%s）",
                cg_path_name(path), human, take, s_lf.truncated ? "，已截断" : "");
        txt = cg_str_format("文件：%s（%s%s）", cg_path_name(path), human,
                            s_lf.truncated ? "，已截断到前 32 MB" : "");
        cg_widget_set_text(cg_find(app, "lab_size"), txt ? txt : "");
        cg_free(txt);                                     /* 设文字只拷贝，堆串得自己还 */
    }
    lf_scan(app, LF_OUT_VIEW);
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 启动钩子：只摆开场白（自检不开窗也会走到这里，所以绝不读文件、绝不弹框）
 * @param app 应用
 * @param user 就绪钩子的业务数据，本示例没用
 * @return void 无返回值
 */
void lf_init(cg_app_t *app, void *user)
{
    cg_widget_t *prg = cg_find(app, "prg_work");
    cg_widget_t *sb  = cg_find(app, "sb_main");

    (void)user;
    if (prg) {
        cg_widget_set_value_range(prg, 0, 3);
        cg_widget_set_value(prg, 0);
    }
    cg_status_set(sb, 0, "还没打开文件");
    cg_status_set(sb, 1, "条件：无关键字（全部行）");
    cg_status_set(sb, 2, "命中 0 行 / 共 0 行 / 0 ms");

    lf_note(app, CG_TERM_SYS, "日志过滤器就绪：点「打开日志文件」选一个文本日志（上限 32 MB）。");
    lf_note(app, CG_TERM_SYS, "运行时的文件接口只有整读，没有分块 / 流式读 —— 所以先问大小、超限只取前一段。");
    lf_note(app, CG_TERM_SYS, "两个关键字按「与 / 或」组合；没有正则接口，所以不做正则勾选框。");
    cg_log(CG_LOG_INFO, "日志过滤器启动");
}

/**
 * @brief 「打开日志文件」：弹通用对话框 → 读文件 → 过滤
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 自检模式不开窗：弹框前先问有没有窗口，免得 CI 被弹窗卡住
 */
static void on_btn_open_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        lf_note(app, CG_TERM_SYS, "自检模式：跳过「打开文件」对话框与读盘。");
        return;
    }
    path[0] = '\0';
    if (cg_open_file_dialog(self, "打开日志文件",
                            "日志文件(*.log)|*.log|文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                            path, sizeof(path))) {
        lf_load(app, path);
    }
}

/**
 * @brief 「开始过滤」：按当前条件重扫一遍（文件内容还在工作窗口里，不用重新读盘）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_filter_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!s_lf.loaded) {
        lf_note(app, CG_TERM_SYS, "还没打开文件：先点「打开日志文件」选一个。");
        return;
    }
    lf_scan(app, LF_OUT_VIEW);
    lf_note(app, CG_TERM_SYS, "过滤完成：命中 %d / %d 行，耗时 %lu ms。",
            s_lf.hits, s_lf.total_lines, s_lf.elapsed_ms);
}

/**
 * @brief 条件控件（两个关键字框 + 三个勾选框）共用的变化回调：条件一变就重滤
 * @param self 触发变化的控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 自检会合成变化事件：那时没打开文件，这里什么都不做（不做 I/O、不弹框）
 */
static void on_cond_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    if (!s_lf.loaded) return;
    lf_scan(cg_app_of(self), LF_OUT_VIEW);
}

/**
 * @brief 「清空视图」：清终端与行号表，文件内容留着（改条件可以立刻重滤）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    cg_term_clear(cg_find(app, "term_out"));
    cg_table_clear(cg_find(app, "tbl_hits"));
    cg_widget_set_value(cg_find(app, "prg_work"), 0);
    s_lf.hits = 0;
    s_lf.shown_lines = 0;
    cg_status_set(cg_find(app, "sb_main"), 2, "视图已清空（文件内容还在，改条件可直接重滤）");
    lf_note(app, CG_TERM_SYS, "视图已清空。");
}

/**
 * @brief 「命中内容复制到剪贴板」：把命中行（带行号）攒成一段文本放进剪贴板
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_copy_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!s_lf.loaded) {
        lf_note(app, CG_TERM_SYS, "还没打开文件，没有可复制的内容。");
        return;
    }
    if (!cg_app_has_window(app)) {
        lf_note(app, CG_TERM_SYS, "自检模式：不碰剪贴板。");
        return;
    }
    lf_scan(app, LF_OUT_CLIP);
    if (s_lf.clip_len <= 0) {
        lf_note(app, CG_TERM_SYS, "没有命中行，剪贴板未改动。");
        return;
    }
    if (cg_clip_set_text(s_clip) == 0) {
        lf_note(app, CG_TERM_TX, "已复制 %d 行到剪贴板（%d 字节%s）",
                s_lf.hits, s_lf.clip_len, s_lf.clip_cut ? "，超上限被截断" : "");
    } else {
        lf_note(app, CG_TERM_SYS, "写剪贴板失败。");
    }
}

/**
 * @brief 「导出结果」：弹保存对话框 → 把命中行写成 UTF-8 文本（攒块 + 追加，不整份驻留内存）
 * @param self 被点的按钮（同时当通用对话框的 parent）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 自检 / CI 不开窗：先问有没有窗口 —— 弹框与写盘都不做
 */
static void on_btn_export_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!s_lf.loaded) {
        lf_note(app, CG_TERM_SYS, "还没打开文件，没有可导出的内容。");
        return;
    }
    if (!cg_app_has_window(app)) {
        lf_note(app, CG_TERM_SYS, "自检模式：跳过「保存文件」对话框与写盘。");
        return;
    }
    path[0] = '\0';
    if (!cg_save_file_dialog(self, "导出过滤结果", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             "txt", path, sizeof(path))) {
        return;                                          /* 用户取消 */
    }
    snprintf(s_lf.export_path, sizeof(s_lf.export_path), "%s", path);
    lf_scan(app, LF_OUT_EXPORT);
    if (lf_export_flush() != 0 || s_lf.exp_failed) {
        lf_note(app, CG_TERM_SYS, "导出失败（可能没权限 / 磁盘满 / 路径被占用）：%s", path);
        cg_log(CG_LOG_ERROR, "导出失败：%s", path);
        return;
    }
    lf_note(app, CG_TERM_TX, "已导出 %d 行到 %s（共 %d 行，耗时 %lu ms）",
            s_lf.hits, cg_path_name(path), s_lf.total_lines, s_lf.elapsed_ms);
    cg_log(CG_LOG_INFO, "导出 %d 行到 %s", s_lf.hits, path);
}

/**
 * @brief 关闭前：记一条日志，把工作窗口标记成"已释放"
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
    cg_log(CG_LOG_INFO, "日志过滤器关闭：%s（命中 %d / %d 行）",
           s_lf.path[0] ? s_lf.path : "(没打开过文件)", s_lf.hits, s_lf.total_lines);
    s_lf.loaded = 0;
    s_lf.win_len = 0;
}

/* ------------------------------------------------------------ 绑定表 */

const cg_binding_t lf_bindings[] = {
    CG_BIND("lf_on_btn_open_click",   on_btn_open_click),
    CG_BIND("lf_on_btn_filter_click", on_btn_filter_click),
    CG_BIND("lf_on_btn_clear_click",  on_btn_clear_click),
    CG_BIND("lf_on_btn_copy_click",   on_btn_copy_click),
    CG_BIND("lf_on_btn_export_click", on_btn_export_click),
    CG_BIND("lf_on_cond_change",      on_cond_change),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void lf_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_close(self, ev, user);
}
