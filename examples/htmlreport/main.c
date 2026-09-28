/**
 * @file    main.c
 * @brief   HTML 报告器：把 CSV 生成一份离线可看的 HTML 报告，并用网页控件 / 系统浏览器打开
 * @details 界面在 htmlreport.cgui 里；本文件只有行为。三件事：
 *
 *          1. **读数据**：`cg_open_file_dialog` 选一个 CSV（首行表头）。运行时的文件接口只有
 *             **整读**，没有分块 / 流式读 —— 所以先用 `cg_file_size` 量一遍，超 8 MB 只取前
 *             一段并明说；数据行封顶 5000 行（报告是给人看的，几千行的表没人翻）。
 *             有意留的简化：CSV **不处理引号转义、不处理字段内换行、不转码**。
 *
 *          2. **生成 HTML**：写进用户数据目录（`cg_app_data_dir` + `cg_path_join`，文件名带
 *             时间戳）。内容是内嵌 CSS，**不引外部资源、不含 JavaScript** —— 离线可看，
 *             网页控件降级成占位时也能直接拿别的浏览器打开。用**攒块写盘**（首块覆盖写、
 *             之后追加），不把整份 HTML 拼在内存里。
 *             本示例要教的坑：**单元格必须做 HTML 转义**（`&` `<` `>` `"`），否则用户数据里
 *             的一个 `<` 就能把页面结构搞坏。
 *
 *          3. **打开**：先 `cg_browser_navigate` 让报告在网页控件里显示；同时给一个
 *             「用系统默认浏览器打开」按钮（`cg_shell_open`）。网页控件是实验性的、起不来会
 *             降级成空占位，而运行时**没有**"控件是否真起来了"的查询接口，所以两个都提供。
 *
 *          内存全是文件级 static 数组，**不 malloc / 不 free**（唯一一处堆内存来自整读接口，
 *          拷完立刻还回去）。代价是 exe 的 BSS 多几 MB、一次只能开一份数据。
 *
 *          自检（--selftest）不开窗：就绪钩子里不读文件、不写 HTML；弹对话框 / 写盘 /
 *          navigate / 碰剪贴板之前一律先问有没有窗口。
 *
 *          编译：在项目根跑 build-app.bat htmlreport
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 上限与约定
 * 没有 malloc，就把上限摆在明面上（超了会明说，不会悄悄丢）。
 */
#define HR_MAX_BYTES  (8 * 1024 * 1024)  /**< 最多解析 8 MB，超了只取前一段 */
#define HR_MAX_ROWS   5000               /**< 数据行封顶：报告是给人看的 */
#define HR_MAX_COLS   32                 /**< 列数上限 */
#define HR_CELL_MAX   128                /**< 单元格截断长度（含结尾 0） */
#define HR_TITLE_MAX  48                 /**< 列名截断长度 */
#define HR_IO_CAP     65536              /**< 写盘攒块大小：满了就落盘 */
#define HR_CLIP_CAP   262144             /**< 剪贴板 HTML 上限：写剪贴板要整串 */

/* ------------------------------------------------------------ 业务状态 */

/** 一列的统计：非空格数 / 能当数字读的格数 / 合计与极值平均 */
typedef struct {
    int    nonempty; /**< 非空格数（空单元格不算 0，也不计数） */
    int    numeric;  /**< 能当数字读的格数 */
    double sum;      /**< 数值合计 */
    double min;      /**< 最小（numeric > 0 时有效） */
    double max;      /**< 最大（numeric > 0 时有效） */
    double avg;      /**< 平均（numeric > 0 时有效） */
} hr_col_t;

/** 行索引的一条：这一行在 s_window 里的偏移与长度（不含行尾换行） */
typedef struct {
    int off; /**< 行首字节偏移 */
    int len; /**< 行字节数 */
} hr_row_t;

/** 业务状态：回调里读，装载 / 生成里写 */
typedef struct {
    char      in_path[1024];                  /**< 当前 CSV 路径 */
    char      out_path[1024];                 /**< 生成的 HTML 路径 */
    long long file_size;                      /**< 问到的文件字节数 */
    int       win_len;                        /**< 工作窗口里有效字节数 */
    int       loaded;                         /**< 有没有成功打开过数据 */
    int       truncated;                      /**< 文件超 8 MB、只取了前一段 */
    int       row_capped;                     /**< 数据行超上限、后面没进报告 */
    int       rows;                           /**< 进报告的数据行数 */
    int       cols;                           /**< 列数 */
    char      col_title[HR_MAX_COLS][HR_TITLE_MAX]; /**< 表头拆出来的列名 */
    hr_col_t  col[HR_MAX_COLS];               /**< 每列的统计 */
    int       generated;                      /**< 报告有没有生成过 */
    char      gen_time[40];                   /**< 报告生成时间戳（写进 HTML 页眉） */
    int       sink;                           /**< 输出方向：0 = 写文件、1 = 攒进剪贴板缓冲 */
    int       io_used;                        /**< 攒块缓冲里攒了多少字节 */
    int       io_first;                       /**< 1 = 首块还没写（要覆盖写建文件） */
    int       io_failed;                      /**< 写盘过程中失败 */
    int       clip_len;                       /**< 剪贴板缓冲里攒了多少字节 */
    int       clip_cut;                       /**< 剪贴板内容被上限截断过 */
} hr_state_t;

static unsigned char s_window[HR_MAX_BYTES];       /**< CSV 内容的唯一工作窗口（static，见文件头取舍） */
static char          s_io[HR_IO_CAP];              /**< 写盘攒块缓冲 */
static char          s_clip[HR_CLIP_CAP];          /**< 剪贴板 HTML 缓冲 */
static char          s_cells[HR_MAX_COLS][HR_CELL_MAX]; /**< 解析一行的单元格缓冲，行间复用 */
static hr_row_t      s_rows[HR_MAX_ROWS];          /**< 行索引：偏移 + 长度，不拷行内容 */
static hr_state_t    s;

static char s_boot_csv[1024]; /**< 命令行点名的 CSV：有窗口时启动就读它并生成报告（见 main 的参数约定） */

/**
 * @brief 生成 HTML 报告并请网页控件加载它（前向声明；定义在下面的"事件回调"一节）
 * @param app 应用
 * @return void 无返回值
 * @note 就绪钩子与按钮回调都要用它，所以先声明 —— 少了它 gcc 会连着报
 *       "implicit declaration"与"conflicting types"；声明本身也要带注释块，
 *       否则注释门禁会把它算成一个"没注释的函数"
 */
static void hr_generate_and_show(cg_app_t *app);

/* ------------------------------------------------------------ 文本小工具 */

/**
 * @brief 截一段单元格文本：去掉首尾空白，超长时按字节截且不截出半个 UTF-8 汉字
 * @param dst 输出缓冲，函数写进以 '\0' 结尾的文本
 * @param cap dst 的字节数
 * @param src 源文本（不必以 '\0' 结尾）
 * @param len 源文本字节数
 * @return void 无返回值
 */
static void hr_trim_cell(char *dst, int cap, const char *src, int len)
{
    int b = 0, e = len, n;

    while (b < e && (src[b] == ' ' || src[b] == '\t')) b++;
    while (e > b && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r')) e--;
    n = e - b;
    if (n > cap - 1) n = cap - 1;
    if (n < 0) n = 0;
    memcpy(dst, src + b, (size_t)n);
    dst[n] = '\0';
    /* 尾巴落在 UTF-8 续字节（0x80..0xBF）上就往前退，免得显示 / 统计出现半个字 */
    while (n > 0 && (((unsigned char)dst[n - 1] & 0xC0) == 0x80)) dst[--n] = '\0';
}

/**
 * @brief 按逗号切一行 CSV 成若干单元格（不做引号转义）
 * @param line 行内容（不必以 '\0' 结尾）
 * @param len 行字节数
 * @param cells 出口：二维单元格缓冲，写满 max 格就只数不写
 * @param max 最多写几格，超出的只数不写
 * @return int 切出来的单元格个数（已夹到 max）
 * @note 形参就用二维数组、行宽由宏给：调用方直接传 s_cells，不必再记一个行宽参数。
 *       （早先为绕开注释门禁对二维数组形参的漏数，这里一度改成"扁平缓冲 + 行宽"；
 *        门禁的 paramName 修好后改回自然写法）
 */
static int hr_split_cells(const char *line, int len, char cells[][HR_CELL_MAX], int max)
{
    int i, start = 0, n = 0;

    for (i = 0; i <= len; i++) {
        if (i == len || line[i] == ',') {
            if (n < max) hr_trim_cell(cells[n], HR_CELL_MAX, line + start, i - start);
            n++;
            start = i + 1;
        }
    }
    return (n > max) ? max : n;
}

/**
 * @brief 判一段文本是不是一个数字（自己解析，不用库函数转数）
 * @param txt 以 '\0' 结尾的文本
 * @param out 出口：是数字时写进解析出的值；可传 NULL
 * @return int 1 是数字、0 不是（空串 / 带单位 / "1,234" 都不算）
 * @note 只认 [+-]?digits[.digits]，整串必须被吃掉 —— "12kg" 与 "1,234" 一律不当数字
 */
static int hr_is_number(const char *txt, double *out)
{
    const char *p = txt;
    double      v = 0.0, scale = 0.1;
    int         neg = 0, digits = 0;

    if (!p || !*p) return 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10.0 + (double)(*p - '0'); p++; digits++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { v += (double)(*p - '0') * scale; scale *= 0.1; p++; digits++; }
    }
    if (!digits || *p != '\0') return 0;
    if (out) *out = neg ? -v : v;
    return 1;
}

/**
 * @brief 往结果终端写一行（本示例唯一的日志出口）
 * @param app 应用
 * @param dir 方向：CG_TERM_RX 收到 / CG_TERM_TX 用户操作 / CG_TERM_SYS 程序说明
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void hr_note(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_term_write(cg_find(app, "term_log"), dir, buf, -1);
}

/* ------------------------------------------------------------ 输出层（文件攒块 / 剪贴板共用一套写法） */

/**
 * @brief 把攒块缓冲写盘：首块覆盖写、之后追加
 * @return int 0 成功、-1 写盘失败
 * @note 首块必须记住状态，否则会把前面写好的内容覆盖掉
 */
static int hr_flush(void)
{
    int rc;

    if (s.io_used <= 0) return 0;
    s_io[s.io_used] = '\0';
    rc = s.io_first ? cg_file_write(s.out_path, s_io) : cg_file_append(s.out_path, s_io);
    if (rc != 0) { s.io_failed = 1; return -1; }
    s.io_first = 0;
    s.io_used  = 0;
    return 0;
}

/**
 * @brief 往当前输出追加一段字节：写文件时进攒块缓冲（快满先落盘），剪贴板模式直接进内存
 * @param data 要写的字节
 * @param len 字节数
 * @return int 0 成功、-1 写盘失败或单条超缓冲
 */
static int hr_emit(const char *data, int len)
{
    if (len <= 0) return 0;
    if (s.sink == 1) {                                    /* 剪贴板模式：整串必须驻留内存 */
        if (s.clip_len + len > (int)sizeof(s_clip) - 1) { s.clip_cut = 1; return 0; }
        memcpy(s_clip + s.clip_len, data, (size_t)len);
        s.clip_len += len;
        return 0;
    }
    if (len > (int)sizeof(s_io) - 1) return -1;           /* 单条超攒块：纯防御 */
    if (s.io_used + len > (int)sizeof(s_io) - 32) {
        if (hr_flush() != 0) return -1;
    }
    memcpy(s_io + s.io_used, data, (size_t)len);
    s.io_used += len;
    return 0;
}

/**
 * @brief 追加一段以 '\0' 结尾的文本
 * @param text 文本；NULL 当空串
 * @return void 无返回值
 */
static void hr_out_str(const char *text)
{
    if (text) (void)hr_emit(text, (int)strlen(text));
}

/**
 * @brief 按 printf 风格拼一段再追加（截到 512 字节，够用）
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void hr_out_fmt(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hr_out_str(buf);
}

/**
 * @brief 追加一段**做过 HTML 转义**的文本（单元格 / 列名 / 路径都要过它）
 * @param text 原始文本；NULL 当空串
 * @return void 无返回值
 * @note `&` `<` `>` `"` 四个必须转 —— 少一个，用户数据里的 `<` 就能把页面结构搞坏
 */
static void hr_out_escaped(const char *text)
{
    const char *p;

    if (!text) return;
    for (p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;

        if (c == '&')       hr_out_str("&amp;");
        else if (c == '<')  hr_out_str("&lt;");
        else if (c == '>')  hr_out_str("&gt;");
        else if (c == '"')  hr_out_str("&quot;");
        else                (void)hr_emit((const char *)&c, 1);
    }
}

/**
 * @brief 这一列该不该当"数字列"（表头与单元格右对齐用）
 * @param c 列号
 * @return int 1 数字列（数值格数占一半以上）、0 不是
 */
static int hr_col_is_num(int c)
{
    if (s.rows <= 0 || c < 0 || c >= s.cols) return 0;
    return (s.col[c].numeric * 2 >= s.rows);
}

/* ------------------------------------------------------------ HTML 各段 */

/** 内嵌 CSS：深色标题栏 + 汇总卡 + 覆盖度小条 + 斑马纹表格 + 数字右对齐；不引任何外部资源 */
static const char *k_css =
    "*{box-sizing:border-box}\n"
    "body{margin:0;background:#fafafc;color:#1b1b1f;"
    "font:14px/1.55 \"Segoe UI\",system-ui,\"Microsoft YaHei\",sans-serif}\n"
    "header.bar{background:#1f2430;color:#fff;padding:18px 24px}\n"
    "header.bar h1{margin:0;font-size:20px;font-weight:600}\n"
    "header.bar .meta{margin-top:6px;font-size:12px;color:#b9c0cf}\n"
    "main{padding:20px 24px 36px}\n"
    ".cards{display:flex;flex-wrap:wrap;gap:12px;margin:0 0 8px}\n"
    ".card{flex:1 1 170px;background:#fff;border:1px solid #e3e3ea;border-radius:10px;padding:12px 14px}\n"
    ".card .k{font-size:12px;color:#6b6b76}\n"
    ".card .v{margin-top:4px;font-size:20px;font-weight:600;font-variant-numeric:tabular-nums}\n"
    "h2{font-size:15px;margin:24px 0 10px;color:#333}\n"
    ".row{display:flex;align-items:center;gap:10px;margin:7px 0}\n"
    ".nm{width:190px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#6b6b76}\n"
    ".track{flex:1;height:12px;background:#eceef4;border-radius:6px;overflow:hidden}\n"
    ".fill{height:100%;background:#3b6ef5}\n"
    ".pct{width:58px;text-align:right;color:#6b6b76;font-variant-numeric:tabular-nums}\n"
    "table{border-collapse:collapse;width:100%;background:#fff;font-size:13px}\n"
    "thead th{background:#1f2430;color:#fff;text-align:left;padding:8px 10px}\n"
    "tbody td{padding:6px 10px;border-bottom:1px solid #e3e3ea}\n"
    "tbody tr:nth-child(even){background:#f4f5f9}\n"
    "th.n,td.n{text-align:right;font-variant-numeric:tabular-nums}\n"
    "footer{margin-top:22px;color:#6b6b76;font-size:12px;line-height:1.8}\n";

/**
 * @brief 写 HTML 开头：DOCTYPE / head / 内嵌 CSS / 深色标题栏
 * @return void 无返回值
 */
static void hr_html_head(void)
{
    hr_out_str("<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n");
    hr_out_str("<title>CSV 数据报告</title>\n<style>\n");
    hr_out_str(k_css);
    hr_out_str("</style>\n</head>\n<body>\n");
    hr_out_str("<header class=\"bar\"><h1>CSV 数据报告</h1>\n<div class=\"meta\">数据源：");
    hr_out_escaped(cg_path_name(s.in_path));
    hr_out_str(" ｜ 生成时间：");
    hr_out_escaped(s.gen_time[0] ? s.gen_time : "(未记录)");
    hr_out_fmt(" ｜ 规模：%d 行 × %d 列</div></header>\n<main>\n", s.rows, s.cols);
}

/**
 * @brief 写汇总卡片：行数 / 列数 / 各数值列的合计与平均
 * @return void 无返回值
 */
static void hr_html_cards(void)
{
    int c;

    hr_out_str("<section class=\"cards\">\n");
    hr_out_fmt("<div class=\"card\"><div class=\"k\">数据行</div><div class=\"v\">%d</div></div>\n", s.rows);
    hr_out_fmt("<div class=\"card\"><div class=\"k\">列数</div><div class=\"v\">%d</div></div>\n", s.cols);
    for (c = 0; c < s.cols; c++) {
        if (s.col[c].numeric <= 0) continue;
        hr_out_str("<div class=\"card\"><div class=\"k\">");
        hr_out_escaped(s.col_title[c]);
        hr_out_str(" · 合计</div><div class=\"v\">");
        hr_out_fmt("%.2f", s.col[c].sum);
        hr_out_str("</div><div class=\"k\">平均 ");
        hr_out_fmt("%.2f", s.col[c].avg);
        hr_out_fmt(" ｜ 数值 %d 格</div></div>\n", s.col[c].numeric);
    }
    hr_out_str("</section>\n");
}

/**
 * @brief 写每列一张"覆盖度小条"：纯 <div> 百分比宽度画的横向条
 * @return void 无返回值
 */
static void hr_html_dists(void)
{
    int c;

    hr_out_str("<h2>各列非空覆盖度</h2>\n<section class=\"dists\">\n");
    for (c = 0; c < s.cols; c++) {
        int pct = 0;

        if (s.rows > 0) pct = (int)((long long)s.col[c].nonempty * 100 / s.rows);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        hr_out_str("<div class=\"row\"><span class=\"nm\">");
        hr_out_escaped(s.col_title[c]);
        hr_out_str("</span><div class=\"track\"><div class=\"fill\" style=\"width:");
        hr_out_fmt("%d", pct);
        hr_out_str("%\"></div></div><span class=\"pct\">");
        hr_out_fmt("%d", pct);
        hr_out_str("%</span></div>\n");
    }
    hr_out_str("</section>\n");
}

/**
 * @brief 写数据表：按行输出，每个单元格都过 HTML 转义
 * @return void 无返回值
 */
static void hr_html_table(void)
{
    int r, c;

    hr_out_str("<h2>数据表</h2>\n<table>\n<thead><tr>");
    for (c = 0; c < s.cols; c++) {
        hr_out_str(hr_col_is_num(c) ? "<th class=\"n\">" : "<th>");
        hr_out_escaped(s.col_title[c]);
        hr_out_str("</th>");
    }
    hr_out_str("</tr></thead>\n<tbody>\n");
    for (r = 0; r < s.rows; r++) {
        int n = hr_split_cells((const char *)s_window + s_rows[r].off, s_rows[r].len,
                               s_cells, HR_MAX_COLS);

        hr_out_str("<tr>");
        for (c = 0; c < s.cols; c++) {
            hr_out_str(hr_col_is_num(c) ? "<td class=\"n\">" : "<td>");
            if (c < n) hr_out_escaped(s_cells[c]);
            hr_out_str("</td>");
        }
        hr_out_str("</tr>\n");
    }
    hr_out_str("</tbody>\n</table>\n");
}

/**
 * @brief 写页脚与收尾：把这次报告的三条简化摆在明面上
 * @return void 无返回值
 */
static void hr_html_tail(void)
{
    hr_out_str("<footer>");
    if (s.truncated) hr_out_str("源文件超过 8 MB：本报告只用了前 8 MB。<br>");
    if (s.row_capped) hr_out_fmt("数据行超过上限：本报告只收了前 %d 行。<br>", HR_MAX_ROWS);
    hr_out_str("CSV 按逗号与换行切分：不处理引号转义、字段内换行与编码转换。<br>");
    hr_out_str("单元格已做 HTML 转义（&amp; &lt; &gt; &quot;），用户数据不会破坏页面结构。<br>");
    hr_out_str("本页不引用外部资源、不含 JavaScript，离线可看。");
    hr_out_str("</footer>\n</main>\n</body>\n</html>\n");
}

/**
 * @brief 生成整份 HTML
 * @param sink 输出方向：0 = 攒块写进输出路径、1 = 攒进剪贴板缓冲
 * @return int 0 成功、-1 写盘失败
 * @note 两条路共用同一套写法：生成代码只管 hr_out_*，落盘 / 进内存由输出层决定
 */
static int hr_generate(int sink)
{
    s.sink = sink;
    s.io_failed = 0;
    if (sink == 1) {
        s.clip_len = 0;
        s.clip_cut = 0;
        s_clip[0]  = '\0';
    } else {
        s.io_used  = 0;
        s.io_first = 1;
    }
    hr_html_head();
    hr_html_cards();
    hr_html_dists();
    hr_html_table();
    hr_html_tail();
    if (sink == 1) {
        s_clip[s.clip_len] = '\0';
        return 0;
    }
    return hr_flush();
}

/* ------------------------------------------------------------ 装载数据 */

/**
 * @brief 打开一个 CSV：先问大小，超上限只取前一段，再整读进工作窗口并建行索引 / 算统计
 * @param app 应用
 * @param path 文件路径（UTF-8）
 * @return void 无返回值（成功与否都通过终端 / 状态栏说话）
 * @note 这里的"整读"是能力边界不是偷懒：运行时没有按偏移 / 流式读的接口，所以尺寸保护
 *       只能在读之前用问大小的接口拦住；行内容不复制，索引只存偏移 + 长度
 */
static void hr_load(cg_app_t *app, const char *path)
{
    unsigned char *raw;
    long long      size;
    int            len = 0, take, pos, start, llen, r = 0, c, n, i;

    size = cg_file_size(path);
    if (size < 0) {
        hr_note(app, CG_TERM_SYS, "读不到文件大小：%s（不存在 / 是目录 / 被占用）", path);
        return;
    }
    memset(&s, 0, sizeof(s));
    snprintf(s.in_path, sizeof(s.in_path), "%s", path);
    s.file_size = size;
    s.truncated = (size > (long long)HR_MAX_BYTES);

    raw = cg_file_read_bytes(path, &len);        /* 整读：返回堆缓冲，用完还回去 */
    if (!raw) {
        hr_note(app, CG_TERM_SYS, "读不进来：%s", path);
        return;
    }
    take = (len > HR_MAX_BYTES) ? HR_MAX_BYTES : len;
    if (take < 0) take = 0;
    memcpy(s_window, raw, (size_t)take);
    cg_free(raw);
    s.win_len = take;
    s.loaded  = 1;
    if (s.truncated) {
        hr_note(app, CG_TERM_SYS, "文件 %lld 字节超过 8 MB 上限：只取前 8 MB 参与报告。", size);
    }

    pos = 0;
    if (take >= 3 && s_window[0] == 0xEF && s_window[1] == 0xBB && s_window[2] == 0xBF) pos = 3;

    /* 表头行：首行拆出来的就是列名。先拆进单元格缓冲、再截进列名缓冲 ——
     * 两个缓冲的"行宽"不一样（单元格 128，列名 48），混着传会直接报指针类型不兼容 */
    start = pos;
    while (pos < take && s_window[pos] != '\n') pos++;
    llen = pos - start;
    if (llen > 0 && s_window[start + llen - 1] == '\r') llen--;
    n = hr_split_cells((const char *)s_window + start, llen, s_cells, HR_MAX_COLS);
    s.cols = (n < 1) ? 1 : n;
    for (c = 0; c < s.cols; c++) {
        if (s_cells[c][0] == '\0') snprintf(s.col_title[c], HR_TITLE_MAX, "列%d", c + 1);
        else snprintf(s.col_title[c], HR_TITLE_MAX, "%.*s", HR_TITLE_MAX - 1, s_cells[c]);
    }
    if (pos < take) pos++;

    /* 数据行：只建偏移索引 + 顺手算统计（空单元格不算 0，也不计数） */
    while (pos < take) {
        if (r >= HR_MAX_ROWS) { s.row_capped = 1; break; }
        start = pos;
        while (pos < take && s_window[pos] != '\n') pos++;
        llen = pos - start;
        if (llen > 0 && s_window[start + llen - 1] == '\r') llen--;
        if (pos < take) pos++;
        if (llen <= 0) continue;                 /* 空行不进报告 */
        s_rows[r].off = start;
        s_rows[r].len = llen;
        r++;
        n = hr_split_cells((const char *)s_window + start, llen, s_cells, HR_MAX_COLS);
        for (c = 0; c < s.cols; c++) {
            double v = 0.0;

            if (c >= n || s_cells[c][0] == '\0') continue;
            s.col[c].nonempty++;
            if (hr_is_number(s_cells[c], &v)) {
                s.col[c].numeric++;
                s.col[c].sum += v;
                if (s.col[c].numeric == 1 || v < s.col[c].min) s.col[c].min = v;
                if (s.col[c].numeric == 1 || v > s.col[c].max) s.col[c].max = v;
            }
        }
    }
    s.rows = r;
    for (i = 0; i < s.cols; i++) {
        if (s.col[i].numeric > 0) s.col[i].avg = s.col[i].sum / (double)s.col[i].numeric;
    }

    hr_note(app, CG_TERM_TX, "已解析 %s：%d 列 × %d 行%s%s",
            cg_path_name(path), s.cols, s.rows,
            s.truncated ? "（超 8 MB，只用前一段）" : "",
            s.row_capped ? "（行数超 5000，只取前 5000 行）" : "");
    hr_note(app, CG_TERM_SYS, "CSV 按逗号与换行切分：不处理引号转义、字段内换行与转码。");
    cg_status_set(cg_find(app, "sb_main"), 0, "数据已就绪");
    cg_status_setf(cg_find(app, "sb_main"), 1, "%d 列 × %d 行%s%s",
                   s.cols, s.rows, s.truncated ? "（截断 8 MB）" : "",
                   s.row_capped ? "（截断 5000 行）" : "");
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 启动钩子：只摆开场白（自检不开窗也会走到这里，所以绝不读文件、绝不写 HTML）
 * @param app 应用
 * @param user 就绪钩子的业务数据，本示例没用
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    (void)user;
    cg_status_set(sb, 0, "等待数据");
    cg_status_set(sb, 1, "0 列 × 0 行");
    cg_status_set(sb, 2, "报告：还没生成");
    hr_note(app, CG_TERM_SYS, "HTML 报告器就绪：点「打开 CSV...」选一个首行是表头的 CSV（上限 8 MB / 5000 行）。");
    hr_note(app, CG_TERM_SYS, "报告写进用户数据目录，纯 HTML + 内嵌 CSS，不引外部资源、不含 JavaScript，离线可看。");
    hr_note(app, CG_TERM_SYS, "预览区是 browser 控件（实验性）：起不来会降级成空占位，那时点「用系统默认浏览器打开」。");
    cg_log(CG_LOG_INFO, "HTML 报告器启动");

    /* 命令行点了名的 CSV：有窗口就顺手读进去、生成报告并预览（自检不开窗，也就不碰盘） */
    if (s_boot_csv[0] && cg_app_has_window(app)) {
        hr_note(app, CG_TERM_TX, "命令行点了名，直接处理：%s", s_boot_csv);
        hr_load(app, s_boot_csv);
        if (s.loaded) hr_generate_and_show(app);
    }
}

/**
 * @brief 「打开 CSV...」：弹通用对话框 → 读数据
 * @param self 被点的按钮（同时当通用对话框的 parent）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 自检不开窗：弹框与读盘之前先问有没有窗口
 */
static void on_btn_open_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        hr_note(app, CG_TERM_SYS, "自检模式：跳过「打开文件」对话框与读盘。");
        return;
    }
    path[0] = '\0';
    if (cg_open_file_dialog(self, "打开 CSV 数据",
                            "CSV 文件(*.csv)|*.csv|文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                            path, sizeof(path))) {
        hr_load(app, path);
    }
}

/**
 * @brief 「生成并预览」按钮：转给生成流程
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_generate_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    hr_generate_and_show(cg_app_of(self));
}

/**
 * @brief 生成 HTML 报告并请网页控件加载它（按钮与"命令行点名"两条路共用）
 * @param app 应用
 * @return void 无返回值
 * @note 自检 / CI 不开窗：写盘与导航都不做（先问有没有窗口）
 */
static void hr_generate_and_show(cg_app_t *app)
{
    cg_widget_t *sb  = cg_find(app, "sb_main");
    cg_widget_t *web = cg_find(app, "web_view");
    char        *dir, *ts, *full;
    char         fname[80];

    if (!s.loaded) {
        hr_note(app, CG_TERM_SYS, "还没打开数据：先点「打开 CSV...」选一个。");
        return;
    }
    if (!cg_app_has_window(app)) {
        hr_note(app, CG_TERM_SYS, "自检模式：跳过生成 HTML 与写盘。");
        return;
    }

    dir = cg_app_data_dir();                     /* 堆串，用完还回去 */
    ts  = cg_time_now("%Y%m%d-%H%M%S");          /* 文件名带时间戳，一次一份不互相覆盖 */
    snprintf(fname, sizeof(fname), "csv-report-%s.html", ts ? ts : "now");
    full = dir ? cg_path_join(dir, fname) : NULL;
    if (!full) {
        hr_note(app, CG_TERM_SYS, "拼不出输出路径（用户数据目录取不到）。");
        cg_free(dir);
        cg_free(ts);
        return;
    }
    snprintf(s.out_path, sizeof(s.out_path), "%s", full);
    snprintf(s.gen_time, sizeof(s.gen_time), "%s", ts ? ts : "?");
    cg_free(full);
    cg_free(dir);
    cg_free(ts);

    if (hr_generate(0) != 0 || s.io_failed) {
        hr_note(app, CG_TERM_SYS, "写报告失败：%s（没权限 / 磁盘满 / 路径被占用）", s.out_path);
        cg_status_set(sb, 0, "生成失败");
        return;
    }
    s.generated = 1;
    hr_note(app, CG_TERM_TX, "已生成报告：%s", s.out_path);

    if (web) cg_browser_navigate(web, s.out_path);   /* 有窗口就在这里请求控件加载 */
    hr_note(app, CG_TERM_SYS, "已请求网页控件加载；若预览区没内容（控件没起来会降级成空占位），点「用系统默认浏览器打开」。");
    cg_status_set(sb, 0, "报告已生成");
    cg_status_set(sb, 2, s.out_path);
    cg_log(CG_LOG_INFO, "生成 HTML 报告：%s（%d 行 × %d 列）", s.out_path, s.rows, s.cols);
}

/**
 * @brief 「用系统默认浏览器打开」：把报告文件交给系统默认程序
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 网页控件是实验性的、没有"是否起来了"的查询接口，所以这条兜底必须有
 */
static void on_btn_sysopen_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       rc;

    (void)ev;
    (void)user;
    if (!s.generated) {
        hr_note(app, CG_TERM_SYS, "还没生成报告：先点「生成并预览」。");
        return;
    }
    if (!cg_app_has_window(app)) {
        hr_note(app, CG_TERM_SYS, "自检模式：不启动外部程序。");
        return;
    }
    rc = cg_shell_open(s.out_path);
    if (rc == 0) {
        hr_note(app, CG_TERM_TX, "已交给系统默认浏览器：%s", s.out_path);
    } else {
        hr_note(app, CG_TERM_SYS, "调不动系统默认程序（rc=%d）：请手动打开 %s", rc, s.out_path);
    }
}

/**
 * @brief 「复制 HTML 源码」：把完整 HTML 攒进剪贴板缓冲（上限 256 KB）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 走的是生成器的"内存出口"，和写盘共用同一套写法
 */
static void on_btn_copy_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!s.loaded) {
        hr_note(app, CG_TERM_SYS, "还没打开数据，没有可复制的 HTML。");
        return;
    }
    if (!cg_app_has_window(app)) {
        hr_note(app, CG_TERM_SYS, "自检模式：不碰剪贴板。");
        return;
    }
    (void)hr_generate(1);
    if (s.clip_len <= 0) {
        hr_note(app, CG_TERM_SYS, "HTML 生成失败，剪贴板未改动。");
        return;
    }
    if (cg_clip_set_text(s_clip) == 0) {
        hr_note(app, CG_TERM_TX, "已复制 HTML 源码到剪贴板（%d 字节%s）",
                s.clip_len, s.clip_cut ? "，超 256 KB 被截断" : "");
    } else {
        hr_note(app, CG_TERM_SYS, "写剪贴板失败。");
    }
}

/**
 * @brief 关闭前：记一条日志
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
    cg_log(CG_LOG_INFO, "HTML 报告器关闭：%s（%d 行 × %d 列，报告 %s）",
           s.in_path[0] ? s.in_path : "(没打开过数据)", s.rows, s.cols,
           s.generated ? s.out_path : "(没生成)");
    s.loaded = 0;
    s.win_len = 0;
}

/**
 * @brief 判断字符串是不是以某个后缀结尾（不区分 ASCII 大小写）
 * @param s 待检查的字符串；NULL 安全
 * @param suffix 后缀，如 ".cgui"
 * @return int 1 是、0 否
 * @note 自己按字节折大小写，不引 <ctype.h>：这里只是给命令行参数分个类。
 *       必须定义在 main 之前 —— 放到后面会被当成隐式声明
 */
static int ends_with_ci(const char *s, const char *suffix)
{
    int ls, lf, i;

    if (!s || !suffix) return 0;
    ls = (int)strlen(s);
    lf = (int)strlen(suffix);
    if (lf <= 0 || lf > ls) return 0;
    for (i = 0; i < lf; i++) {
        unsigned char a = (unsigned char)s[ls - lf + i];
        unsigned char b = (unsigned char)suffix[i];

        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_close",              on_close),
    CG_BIND("on_btn_open_click",     on_btn_open_click),
    CG_BIND("on_btn_generate_click", on_btn_generate_click),
    CG_BIND("on_btn_sysopen_click",  on_btn_sysopen_click),
    CG_BIND("on_btn_copy_click",     on_btn_copy_click),
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

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动在 exe 旁边找同名 .cgui */
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "htmlreport.cgui";
        return cg_app_selftest(&desc);
    }

    /* 参数约定（与 examples\dataview 一致，三种都支持）：
     *   不写参数            → 按 exe 同目录自动找 htmlreport.cgui
     *   写 xxx.cgui         → 用它当界面；再给第二个参数就是启动要处理的数据文件
     *   写 xxx.csv / 其它   → 当数据文件：界面按同目录自动找，启动后读它、生成报告并预览 */
    desc.cgui_path = "htmlreport.cgui";
    if (argc > 1 && ends_with_ci(argv[1], ".cgui")) {
        desc.cgui_path = argv[1];
        if (argc > 2) snprintf(s_boot_csv, sizeof s_boot_csv, "%s", argv[2]);
    } else if (argc > 1) {
        snprintf(s_boot_csv, sizeof s_boot_csv, "%s", argv[1]);
    }

    cg_log(CG_LOG_INFO, "HTML 报告器启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
