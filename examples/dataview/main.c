/**
 * @file    main.c
 * @brief   数据浏览器：分页看大 CSV，几万行也不卡
 * @details 本示例只教一件事：**几万行数据不许一次塞进表格**。
 *          表格控件每加一行都要建行、算宽、重排；一次填几万行时看到的就是
 *          「界面白掉十几秒、内存再涨一截」，看着像卡死。做对了的样子是两件事：
 *
 *            1. **一次解析进内存索引**（index_build）：整份 CSV 只读一遍，把每一行的
 *               **偏移 + 长度**记进一张固定数组，**不复制行内容**。
 *               为什么存偏移不存字符串副本？行内容总量 ≈ 文件大小，再拷一份就是内存翻倍；
 *               而「偏移 + 长度」每行只要 8 字节，10 万行也就 800 KB。更要紧的是**只有一份真相**：
 *               排序、统计、导出都从同一份字节里现取，不会出现「两份数据不同步」。
 *            2. **分页 + 按需渲染**（render_page）：每页 200 行，只把**当前页**塞给表格，
 *               切页时先清空再填。排序也只排那张偏移数组 —— 换的是 8 字节的序号，
 *               不是搬字符串；排完回第 1 页（行序换了，留在原页看到的是完全另一批行）。
 *
 *          顺带钉住一条最容易误判的：**当前页 ≠ 全部行**。「统计」按钮把「当前页」与「全部行」
 *          并排算一遍，就是让「只看当前页就下结论」这个坑当场显形。
 *
 *          三条有意留的简化（诚实说明，不是漏做）：
 *            * CSV 只认逗号与换行：**不处理引号转义、不处理字段内换行**、不转码
 *              （别的程序另存的 ANSI/GBK 文件中文会乱码）；
 *            * 文件超过 16 MB 只解析前一段（先用问大小的接口量一遍），状态栏会写明「已截断」；
 *            * 内存全是固定数组 / static 缓冲，**没有任何 malloc/free**：行数 10 万、列数 32、
 *              单元格 128 字节这些上限写在下面的宏里，超了会明说，不会悄悄丢。
 *              唯一一处堆内存来自整读文件的接口，用完立刻还回去。
 *
 *          界面在 dataview.cgui 里；本文件只有行为。
 *          编译：在项目根跑 build-app.bat dataview
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>   /* qsort：只排序号数组，不分配内存 */
#include <string.h>

/* ------------------------------------------------------------ 上限与约定
 * 全是编译期常量：没有 malloc，就要把上限摆在明面上（超了会明说，见 s_truncated / s_row_capped）。
 */
#define CG_PAGE_ROWS   200                 /**< 每页行数：表格一次只装这么多行 */
#define CG_MAX_ROWS    100000              /**< 行索引上限：100000 × 8 字节 = 800 KB 静态数组 */
#define CG_MAX_COLS    32                  /**< 列数上限 */
#define CG_CELL_MAX    128                 /**< 单元格截断长度（显示 / 排序 / 统计都用它，含结尾 0） */
#define CG_TITLE_MAX   48                  /**< 列名截断长度 */
#define CG_LOAD_LIMIT  (16 * 1024 * 1024)  /**< 最多解析 16 MB，超了只取前一段并明说 */
#define CG_OUT_CHUNK   16384               /**< 导出攒块大小：满了就落盘，不整份驻留内存 */

/* ------------------------------------------------------------ 业务状态 */

/** 行索引的一条：这一行在 s_text 里的字节偏移与长度（不含行尾换行） */
typedef struct {
    int off;   /**< 行首字节偏移 */
    int len;   /**< 行字节数 */
} row_t;

/** 一列的数值统计结果 */
typedef struct {
    int    numeric;    /**< 能当数字读的行数 */
    int    nonnumeric; /**< 读不出数字的行数（空单元格也算） */
    double min;        /**< 最小值（numeric > 0 时有效） */
    double max;        /**< 最大值（numeric > 0 时有效） */
    double sum;        /**< 求和 */
    double avg;        /**< 平均（numeric > 0 时有效） */
} col_stat_t;

/* 已载入的文件与索引：只有 load_csv() 写，其余函数只读 */
static unsigned char *s_text;        /**< 整份文件内容，来自整读接口；换文件时还回去 */
static int            s_len;         /**< s_text 里实际解析的字节数（截断时小于文件大小） */
static long long      s_file_size;   /**< 文件真实大小（问出来的，用来说明有没有截断） */
static int            s_truncated;   /**< 1 = 文件超限，只解析了前一段 */
static int            s_skip_empty;  /**< 被跳过的空行数（只是统计口径，报告区会说明） */
static int            s_row_capped;  /**< 1 = 行数超过上限，后面的行没进索引 */
static int            s_loading;     /**< 1 = 正在装载：这时下拉框是程序改的，别当成用户操作 */

static row_t s_rows[CG_MAX_ROWS];    /**< 行索引：偏移 + 长度，不拷行内容 */
static int   s_row_count;            /**< 索引到的数据行数（不含表头） */

static int   s_header_off;                                  /**< 表头行的起始偏移 */
static int   s_header_len;                                  /**< 表头行的字节数 */
static char  s_col_title[CG_MAX_COLS][CG_TITLE_MAX];        /**< 表头拆出来的列名 */
static int   s_col_count;                                   /**< 列数（= 表头字段数，封顶上限） */
static int   s_built_cols;                                  /**< 已经给表格建过几列（框架只能加列） */

static int   s_page;         /**< 当前页，0 起 */
static int   s_sort_col;     /**< 当前排序列；-1 = 原始顺序 */
static int   s_sort_desc;    /**< 1 = 降序 */
static int   s_sort_numeric; /**< 1 = 本次排序按数值比，0 = 按字典序 */

static char        s_cell[CG_MAX_COLS][CG_CELL_MAX]; /**< 渲染一行时的单元格缓冲，页内复用 */
static const char *s_cells[CG_MAX_COLS];             /**< 交给表格控件的指针数组 */

/* 报告区：一段段追加，最后一次写进只读输入框（报告是多行的，不能写一行覆盖一次） */
static char s_report[8192];  /**< 报告文本累积缓冲 */
static int  s_report_len;    /**< 已用字节数 */

/* 导出：攒块 + 追加，不把整份 CSV 留在内存里 */
static char      s_out_buf[CG_OUT_CHUNK]; /**< 攒块缓冲 */
static int       s_out_len;               /**< 缓冲里已攒多少字节 */
static char      s_out_path[1024];        /**< 导出目标路径 */
static int       s_out_first;             /**< 1 = 第一块还没落盘（要用覆盖写建文件） */
static int       s_out_rows;              /**< 已写出的数据行数 */

static char s_boot_csv[1024]; /**< 命令行点名的 CSV：有窗口时启动后就打开它（见 main 的参数约定） */

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 总页数（按当前索引算）
 * @return int 总页数；没有数据返回 0
 */
static int page_count(void)
{
    if (s_row_count <= 0) return 0;
    return (s_row_count + CG_PAGE_ROWS - 1) / CG_PAGE_ROWS;
}

/**
 * @brief 状态栏第 0 段：分页、规模，以及「数据被截断」的明示
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    cg_status_setf(cg_find(app, "sb_main"), 0, "第 %d / %d 页 · 共 %d 行 · %d 列%s",
                   page_count() ? s_page + 1 : 0, page_count(), s_row_count, s_col_count,
                   (s_truncated || s_row_capped) ? " · 已截断" : "");
}

/**
 * @brief 状态栏第 1 段：当前排序状态
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_sort_status(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    if (s_sort_col < 0) cg_status_set(sb, 1, "排序：原始顺序");
    else cg_status_setf(sb, 1, "排序：%s %s", s_col_title[s_sort_col], s_sort_desc ? "降序" : "升序");
}

/**
 * @brief 清空报告区（下一句 report 会重写）
 * @param app 应用
 * @return void 无返回值
 */
static void report_reset(cg_app_t *app)
{
    s_report_len = 0;
    s_report[0]  = '\0';
    cg_widget_set_text(cg_find(app, "edt_report"), "");
}

/**
 * @brief 往报告区追加一行，并整段重写只读输入框
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 * @note 报告是多行的：每句都整段重写才看得全（若每句只写自己那一行，界面上只剩最后一句）。
 *       同时在控制台镜像一行，自检时也能看到时序。
 */
static void report(cg_app_t *app, const char *fmt, ...)
{
    char   *line;
    va_list ap;
    int     room;

    if (s_report_len >= (int)sizeof(s_report) - 4) return;   /* 满了就不再追加 */
    room = (int)sizeof(s_report) - s_report_len - 4;

    line = s_report + s_report_len;
    va_start(ap, fmt);
    vsnprintf(line, (size_t)room, fmt, ap);
    va_end(ap);

    printf("[dataview] %s\n", line);

    s_report_len += (int)strlen(line);
    if (s_report_len < (int)sizeof(s_report) - 4) {          /* 补一行换行（报告区里是一行一句） */
        s_report[s_report_len++] = '\r';
        s_report[s_report_len++] = '\n';
        s_report[s_report_len]   = '\0';
    }
    cg_widget_set_text(cg_find(app, "edt_report"), s_report);
}

/**
 * @brief 把最近一次操作写进状态栏第 2 段
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void status_msg(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_status_set(cg_find(app, "sb_main"), 2, buf);
}

/**
 * @brief 把一段已经去掉首尾空白的文本当十进制数解析
 * @param s 文本，'\0' 结尾
 * @param out 解析结果出口；成功才写入
 * @return int 1 成功、0 不是数字
 * @note 不用库里的字符串转数：不引 locale，也不接受 "12abc" 这种半串 —— 排序与统计要的是「整串都是数」
 */
static int text_to_double(const char *s, double *out)
{
    double v = 0.0, frac = 0.0, scale = 0.1;
    int    neg = 0, digits = 0, exp = 0, eneg = 0, i = 0;

    if (!s || !*s) return 0;
    if (s[i] == '+' || s[i] == '-') { neg = (s[i] == '-'); i++; }
    for (; s[i] >= '0' && s[i] <= '9'; i++) { v = v * 10.0 + (double)(s[i] - '0'); digits++; }
    if (s[i] == '.') {
        i++;
        for (; s[i] >= '0' && s[i] <= '9'; i++) {
            frac  += (double)(s[i] - '0') * scale;
            scale *= 0.1;
            digits++;
        }
        v += frac;
    }
    if (digits == 0) return 0;
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        if (s[i] == '+' || s[i] == '-') { eneg = (s[i] == '-'); i++; }
        if (s[i] < '0' || s[i] > '9') return 0;
        for (; s[i] >= '0' && s[i] <= '9'; i++) exp = exp * 10 + (s[i] - '0');
        if (exp > 308) exp = 308;                       /* 别让 1e99999 空转 */
        while (exp-- > 0) v = eneg ? v / 10.0 : v * 10.0;
    }
    if (s[i] != '\0') return 0;
    if (out) *out = neg ? -v : v;
    return 1;
}

/**
 * @brief 从一条行索引里取出第 col 个单元格（去掉首尾空白）拷进 out
 * @param r 行索引（指向 s_rows 里的一条）
 * @param col 列号，0 起
 * @param out 输出缓冲，写进的是 '\0' 结尾的副本
 * @param cap out 的字节数
 * @return int 写入的字节数（不含结尾 0）；列号越界或参数非法返回 -1；空单元格返回 0
 * @note 超过 cap-1 的单元格会被截断（上限见单元格宏），超出部分不参与排序与统计
 */
static int cell_copy_at(const row_t *r, int col, char *out, int cap)
{
    int i = 0, c = 0, fs = 0, fe = 0, n;

    if (out && cap > 0) out[0] = '\0';
    if (!out || cap <= 0 || !r || !s_text || col < 0) return -1;

    for (;;) {
        int at_end = (i >= r->len);

        if (at_end || s_text[r->off + i] == ',') {
            if (c == col) { fe = i; break; }
            if (at_end) return -1;                      /* 这一行没有第 col 个字段 */
            c++;
            fs = i + 1;
        }
        i++;
    }
    while (fs < fe && (s_text[r->off + fs] == ' ' || s_text[r->off + fs] == '\t')) fs++;
    while (fe > fs && (s_text[r->off + fe - 1] == ' ' || s_text[r->off + fe - 1] == '\t')) fe--;

    n = fe - fs;
    if (n > cap - 1) n = cap - 1;
    if (n > 0) memcpy(out, s_text + r->off + fs, (size_t)n);
    out[n] = '\0';
    return n;
}

/**
 * @brief 按行号取单元格（上面那个的便利包装）
 * @param row 行号（s_rows 里的下标）
 * @param col 列号，0 起
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return int 写入的字节数；越界或参数非法返回 -1；空单元格返回 0
 */
static int cell_copy(int row, int col, char *out, int cap)
{
    if (row < 0 || row >= s_row_count) {
        if (out && cap > 0) out[0] = '\0';
        return -1;
    }
    return cell_copy_at(&s_rows[row], col, out, cap);
}

/**
 * @brief 把第 row 行摊成每列一个字符串指针（填 s_cell[] 与 s_cells[]）
 * @param row 行号（s_rows 里的下标）
 * @return void 无返回值
 * @note 表格控件加一行要的是「一串以 '\0' 结尾的字符串指针」，所以得有个地方按列持有缓冲。
 *       **缓冲是按列复用的，不是按行**：一列一个 —— 如果所有列都指向同一个缓冲，界面就会
 *       每列都显示最后一列的值（2026-09-28 真机截图抓到的 bug：整表全是 "ok"）。
 *       每列缓冲在下一行进来时被覆盖，这是安全的：加行那一步会把文字拷进控件自己那边
 */
static void row_into_slots(int row)
{
    int c;

    for (c = 0; c < s_col_count; c++) {
        if (cell_copy(row, c, s_cell[c], CG_CELL_MAX) < 0) s_cell[c][0] = '\0';
        s_cells[c] = s_cell[c];
    }
}

/* ------------------------------------------------------------ 解析：索引 + 列名 */

/**
 * @brief 把表头那一行拆成列名（写进列名表与列数）
 * @return void 无返回值
 * @note 只按逗号切；列名超长截断，空列名写成「列N」
 */
static void parse_header(void)
{
    row_t h;
    int   c;

    h.off = s_header_off;
    h.len = s_header_len;
    s_col_count = 0;
    for (c = 0; c < CG_MAX_COLS; c++) {
        int n = cell_copy_at(&h, c, s_col_title[c], CG_TITLE_MAX);

        if (n < 0) break;                       /* 越界 = 表头就这么多列 */
        if (n == 0) snprintf(s_col_title[c], CG_TITLE_MAX, "列%d", c + 1);
        s_col_count = c + 1;
    }
    if (s_col_count == 0) {                     /* 兜底：一列都没有时也给一列，界面不至于空着 */
        snprintf(s_col_title[0], CG_TITLE_MAX, "列1");
        s_col_count = 1;
    }
}

/**
 * @brief 一遍扫过文件字节：拆出表头、把每行的偏移 + 长度记进固定数组
 * @return void 无返回值
 * @note 只记偏移、不拷内容（为什么见文件头）；空行跳过并计数；超过行数上限就不再索引
 */
static void index_build(void)
{
    int i = 0, line_start = 0, line_no = 0;

    s_row_count  = 0;
    s_skip_empty = 0;
    s_row_capped = 0;
    s_header_off = 0;
    s_header_len = 0;

    while (i <= s_len) {
        if (i == s_len || s_text[i] == '\n') {
            int end = i;

            if (end > line_start && s_text[end - 1] == '\r') end--;   /* 吃掉 CRLF 里的回车 */
            if (line_no == 0) {
                s_header_off = line_start;                            /* 首行 = 表头 */
                s_header_len = end - line_start;
            } else if (end > line_start) {
                if (s_row_count < CG_MAX_ROWS) {
                    s_rows[s_row_count].off = line_start;
                    s_rows[s_row_count].len = end - line_start;
                    s_row_count++;
                } else {
                    s_row_capped = 1;                                 /* 到上限：不再索引，报出来 */
                }
            } else {
                s_skip_empty++;                                       /* 空行不占一行 */
            }
            line_no++;
            line_start = i + 1;
        }
        i++;
    }
}

/**
 * @brief 让表格的列数对上新打开的文件
 * @param app 应用
 * @return void 无返回值
 * @note 框架只有「加一列」的接口，没有删列 / 改列名，所以列**只加不删**：
 *       第一次打开按表头建列；之后打开列数不同的文件只能往右加，左边旧列留在那里 ——
 *       报告区会把这件事明说，而不是假装列头还是对的
 */
static void ensure_columns(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_data");
    int          c;

    if (!tbl) return;
    for (c = s_built_cols; c < s_col_count; c++) cg_table_add_column(tbl, s_col_title[c], 0);
    if (s_col_count > s_built_cols) s_built_cols = s_col_count;
}

/**
 * @brief 把列名填进「排序列 / 统计列」两个下拉框
 * @param app 应用
 * @return void 无返回值
 * @note 装载期间把装载标记置 1：程序改下拉框的值也会派发选中事件，别被当成用户选列
 */
static void fill_col_combos(cg_app_t *app)
{
    cg_widget_t *cbo_sort = cg_find(app, "cbo_sort");
    cg_widget_t *cbo_stat = cg_find(app, "cbo_stat");
    int          c;

    cg_widget_list_clear(cbo_sort);
    cg_widget_list_clear(cbo_stat);
    for (c = 0; c < s_col_count; c++) {
        cg_widget_list_addf(cbo_sort, "%d. %s", c + 1, s_col_title[c]);
        cg_widget_list_addf(cbo_stat, "%d. %s", c + 1, s_col_title[c]);
    }
    if (s_col_count > 0) {
        cg_widget_set_value(cbo_sort, 0);
        cg_widget_set_value(cbo_stat, 0);
    }
}

/* ============================================================ 分页渲染 */

/**
 * @brief 只把当前页的行填进表格，并同步分页控件与状态栏
 * @param app 应用
 * @return void 无返回值
 * @note **这一段就是本示例存在的理由**：表格每加一行都要过一次消息循环 —— 一次把全部行
 *       （上限十万量级）填进界面，窗口会白屏、卡住、看着像崩了。所以索引建好后只渲染
 *       「当前页」这几百行，翻页时才换下一批，行数再多也是一页的活。
 * @note 全程用装载标记围住：清表 / 加行 / 回写页号都是「程序在改控件」，别让回调当用户操作再进来一次
 */
static void render_page(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_data");
    cg_widget_t *pg  = cg_find(app, "pg_main");
    int          keep_loading = s_loading;
    int          pages, first, last, i;

    if (!tbl) return;

    s_loading = 1;
    pages     = page_count();
    if (pages < 1) pages = 1;                        /* 没数据也算一页空页，页号才好夹 */

    if (s_page < 0) s_page = 0;
    if (s_page > pages - 1) s_page = pages - 1;      /* 行数变少 / 重排之后页号可能越界 */

    cg_table_clear(tbl);                             /* 表格里永远只有当前页这几行 */
    first = s_page * CG_PAGE_ROWS;
    last  = first + CG_PAGE_ROWS;
    if (last > s_row_count) last = s_row_count;

    for (i = first; i < last; i++) {
        row_into_slots(i);                           /* 第 i 行 → 每列一个缓冲 */
        cg_table_add_row(tbl, s_cells, s_col_count); /* 加行时控件把文字拷走，缓冲可以马上被覆盖 */
    }

    if (pg) {
        cg_widget_set_value_range(pg, 1, pages);     /* 分页控件：页数 = max - min + 1，最少 1 页 */
        cg_widget_set_value(pg, s_page + 1);         /* 控件里的页号 1 起，内部页号 0 起 */
    }

    s_loading = keep_loading;
    refresh_status(app);                             /* 状态栏里写的是真实总页数，0 页就写 0 页 */
}

/* 比较器只有 (const void *, const void *) 这一个签名 —— 没有「上下文」参数，也不能另传业务数据。
   当前排序列 / 方向 / 是否按数值比只能放在文件级 static 里让比较器自己读；三者只在 sort_by 里写、
   只在本函数里读，一眼看得全。 */
static int s_cmp_col;     /**< 比较器用的列号 */
static int s_cmp_desc;    /**< 比较器用的方向：1 降序 */
static int s_cmp_numeric; /**< 比较器用的口径：1 按数值比 */

/**
 * @brief 排序比较器：按当前列 / 方向 / 口径比两行
 * @param pa 指向 s_rows 里某个行索引的指针
 * @param pb 同上，另一行
 * @return int 负数 a 排前、0 相等、正数 b 排前（降序时整体取反）
 * @note 取单元格文字用的是两个 static 缓冲：单线程、顺序调用比较器，本次比较走完才轮到下一次，
 *       所以复用缓冲是安全的
 */
static int row_cmp(const void *pa, const void *pb)
{
    static char ba[CG_CELL_MAX];
    static char bb[CG_CELL_MAX];
    double      va = 0.0, vb = 0.0;
    int         r;

    cell_copy_at((const row_t *)pa, s_cmp_col, ba, sizeof ba);
    cell_copy_at((const row_t *)pb, s_cmp_col, bb, sizeof bb);

    if (s_cmp_numeric && text_to_double(ba, &va) && text_to_double(bb, &vb))
        r = (va < vb) ? -1 : ((va > vb) ? 1 : 0);
    else
        r = strcmp(ba, bb);            /* 空串排在字典序最前，这是简化后的可接受行为 */

    return s_cmp_desc ? -r : r;
}

/**
 * @brief 按某一列排序：只换行索引里的序号，正文一个字节都不动
 * @param app 应用
 * @param col 列号，0 起；越界 / 没数据时只提示不做事
 * @return void 无返回值
 * @note 先均匀抽几行探这一列是不是「整体数值」：只要有一格解不出数字（空单元格、「12 kg」这类
 *       混合文本）整列就退回字典序 —— 于是「序号 / 金额」按数值大小排，「型号 / 备注」仍按字典序。
 *       排序交换的是行索引（偏移 + 长度），原始字节不动，十万行也只是十万次小结构交换。
 *       排完回到第 1 页再渲染，否则用户看到的是「另一批行」还顶着原来的页号。
 */
static void sort_by(cg_app_t *app, int col)
{
    char   buf[CG_CELL_MAX];
    double v;
    int    probe, step, i, numeric = 1;

    if (col < 0 || col >= s_col_count) { status_msg(app, "请先在「排序列」里选一列"); return; }
    if (s_row_count <= 0) { status_msg(app, "还没有数据，先打开一个 CSV"); return; }

    step = (s_row_count > 8) ? (s_row_count / 8) : 1;   /* 均匀抽 8 行探口径 */
    for (i = 0, probe = 0; i < s_row_count && probe < 8; i += step, probe++) {
        cell_copy_at(&s_rows[i], col, buf, sizeof buf);
        if (!text_to_double(buf, &v)) { numeric = 0; break; }
    }

    if (col != s_sort_col) s_sort_desc = 0;      /* 换了一列：方向回到升序 */

    s_cmp_col     = col;
    s_cmp_desc    = s_sort_desc ? 1 : 0;
    s_cmp_numeric = numeric;

    if (s_row_count > 1) qsort(s_rows, (size_t)s_row_count, sizeof(s_rows[0]), row_cmp);

    s_sort_col     = col;
    s_sort_numeric = numeric;
    s_page         = 0;
    render_page(app);
    refresh_sort_status(app);
    status_msg(app, "按「%s」排序，%d 行（%s）", s_col_title[col], s_row_count,
               numeric ? "数值序" : "字典序");
}

/**
 * @brief 对某一列做统计：当前页一组、全部行一组，并排写进报告
 * @param app 应用
 * @param col 列号，0 起；越界 / 没数据时只提示
 * @return void 无返回值
 * @note **只看当前页会得出不同结论**：当前页只是整份数据的一小段，这一页恰好没有负数 / 恰好都是
 *       同一个值时，最小、最大、平均都会给出与全量完全不同的判断。所以两组并排列出来，让「页」
 *       与「全量」的差别一眼可见。空单元格与解不出数的行都计进「非数值」（它们不是 0）
 */
static void do_stats(cg_app_t *app, int col)
{
    col_stat_t all;
    col_stat_t pg;
    char       buf[CG_CELL_MAX];
    double     v;
    int        i, first, last, all_n = 0, pg_n = 0;

    if (col < 0 || col >= s_col_count) { status_msg(app, "请先在「统计列」里选一列"); return; }
    if (s_row_count <= 0) { status_msg(app, "还没有数据，先打开一个 CSV"); return; }

    memset(&all, 0, sizeof all);
    memset(&pg, 0, sizeof pg);
    first = s_page * CG_PAGE_ROWS;
    if (first > s_row_count) first = s_row_count;
    last = first + CG_PAGE_ROWS;
    if (last > s_row_count) last = s_row_count;

    for (i = 0; i < s_row_count; i++) {
        int in_page = (i >= first && i < last);

        cell_copy_at(&s_rows[i], col, buf, sizeof buf);
        if (!text_to_double(buf, &v)) {          /* 空单元格也算非数值 */
            all.nonnumeric++;
            if (in_page) pg.nonnumeric++;
            continue;
        }
        if (all_n == 0 || v < all.min) all.min = v;
        if (all_n == 0 || v > all.max) all.max = v;
        all.sum += v;
        all_n++;

        if (in_page) {
            if (pg_n == 0 || v < pg.min) pg.min = v;
            if (pg_n == 0 || v > pg.max) pg.max = v;
            pg.sum += v;
            pg_n++;
        }
    }
    all.avg = all_n ? (all.sum / all_n) : 0.0;
    pg.avg  = pg_n  ? (pg.sum / pg_n)   : 0.0;

    report_reset(app);
    report(app, "列「%s」统计 —— 第 %d 页（第 %d~%d 行）与全部 %d 行",
           s_col_title[col], s_page + 1, s_row_count ? first + 1 : 0, last, s_row_count);
    report(app, "%-10s %14s %14s", "项目", "当前页", "全部行");
    report(app, "%-10s %14d %14d", "数值行", pg_n, all_n);
    report(app, "%-10s %14d %14d", "非数值", pg.nonnumeric, all.nonnumeric);
    report(app, "%-10s %14.4f %14.4f", "最小", pg.min, all.min);
    report(app, "%-10s %14.4f %14.4f", "最大", pg.max, all.max);
    report(app, "%-10s %14.4f %14.4f", "合计", pg.sum, all.sum);
    report(app, "%-10s %14.4f %14.4f", "平均", pg.avg, all.avg);
    report(app, "只看当前页会得出不同结论：");
    report(app, "  当前页只有 %d 行，它的最小 / 最大 / 平均都只是这一页的；", last - first);
    report(app, "  右边那列「全部行」才是整份 CSV 的真实分布。");

    status_msg(app, "已统计「%s」：当前页 %d 行 / 全部 %d 行", s_col_title[col], last - first, s_row_count);
}

/**
 * @brief 往导出缓冲里追加一段文字，攒满一块就落盘
 * @param text 要写出的文字（按字节追加，调用方负责结尾的换行）
 * @return int 0 成功、-1 写盘失败
 * @note 首块用覆盖写（建文件），其后每块用追加；缓冲留几格给结尾，攒到上限就冲一次
 */
static int out_put(const char *text)
{
    int i;

    for (i = 0; text[i]; i++) {
        if (s_out_len >= CG_OUT_CHUNK - 1) {
            s_out_buf[s_out_len] = '\0';
            if (s_out_first) {
                if (cg_file_write(s_out_path, s_out_buf) != 0) return -1;
                s_out_first = 0;
            } else if (cg_file_append(s_out_path, s_out_buf) != 0) {
                return -1;
            }
            s_out_len = 0;
        }
        s_out_buf[s_out_len++] = text[i];
    }
    return 0;
}

/**
 * @brief 把当前页或者全部行导出成 CSV
 * @param app 应用
 * @param only_page 非 0 只导出当前页，0 导出全部行
 * @return void 无返回值
 * @note 第一行写列名当表头（下面「列只加不删」留下的空列也会照写，位置对得上）。
 *       **CSV 是简化写法**：字段直接拼逗号，不处理引号转义、字段里不换行 —— 单元格里有逗号时
 *       这份 CSV 会被解读成多一列，这是明写在报告里的取舍。
 *       攒块写盘：每攒够一块落一次，不用把整份数据拼成一个大字符串。无窗口（自检）时整条路跳过
 */
static void export_rows(cg_app_t *app, int only_page)
{
    cg_widget_t *parent = cg_find(app, "btn_export_all");
    char         buf[CG_CELL_MAX];
    char         path[1024];
    int          first, last, i, c, ok = 1;

    if (s_row_count <= 0 || s_col_count <= 0) { status_msg(app, "还没有数据，先打开一个 CSV"); return; }

    if (!cg_app_has_window(app)) {                  /* 自检不开窗：不弹对话框，这条路整个跳过 */
        status_msg(app, "自检模式：不弹保存对话框，导出跳过");
        return;
    }

    path[0] = '\0';
    if (!cg_save_file_dialog(parent, only_page ? "导出当前页" : "导出全部行",
                             "CSV 文件(*.csv)|*.csv|所有文件(*.*)|*.*", "csv",
                             path, sizeof path)) {
        return;                                     /* 用户取消：什么都不做 */
    }

    first = only_page ? (s_page * CG_PAGE_ROWS) : 0;
    last  = only_page ? (first + CG_PAGE_ROWS) : s_row_count;
    if (first > s_row_count) first = s_row_count;
    if (last > s_row_count) last = s_row_count;

    snprintf(s_out_path, sizeof s_out_path, "%s", path);
    s_out_len   = 0;
    s_out_first = 1;
    s_out_rows  = 0;

    if (out_put("\xEF\xBB\xBF") != 0) ok = 0;       /* UTF-8 BOM：不加的话 Excel 会把中文读成乱码 */

    for (c = 0; ok && c < s_col_count; c++) {       /* 表头 */
        if (c && out_put(",") != 0) break;
        if (out_put(s_col_title[c]) != 0) break;
    }
    if (ok && c == s_col_count && out_put("\r\n") != 0) ok = 0;

    for (i = first; ok && i < last; i++) {
        for (c = 0; c < s_col_count; c++) {
            if (c && out_put(",") != 0) { ok = 0; break; }
            cell_copy_at(&s_rows[i], c, buf, sizeof buf);
            if (out_put(buf) != 0) { ok = 0; break; }
        }
        if (!ok || out_put("\r\n") != 0) ok = 0;
        else s_out_rows++;
    }

    if (ok) {                                       /* 冲尾巴：还没写过盘的用覆盖写，建出文件 */
        s_out_buf[s_out_len] = '\0';
        if (s_out_first) {
            if (cg_file_write(s_out_path, s_out_buf) != 0) ok = 0;
        } else if (cg_file_append(s_out_path, s_out_buf) != 0) {
            ok = 0;
        }
        s_out_first = 0;
    }

    if (ok) {
        status_msg(app, "已导出 %d 行到 %s", s_out_rows, cg_path_name(path));
        report(app, "导出：%s（%d 行%s）", path, s_out_rows, only_page ? "，当前页" : "，全部行");
    } else {
        status_msg(app, "导出失败：%s", path);
        report(app, "导出失败：%s（磁盘满了 / 路径不可写？）", path);
    }
}

/**
 * @brief 载入一份 CSV：量大小 → 读字节 → 建索引 → 解析表头 → 建列 → 填下拉 → 渲染第 1 页
 * @param app 应用
 * @param path CSV 文件路径（UTF-8）
 * @return void 无返回值
 * @note 换文件前先把上一份的堆内存还掉 —— 整读出来的那份是框架给的堆串，由业务负责还。
 *       超过上限的只索引前面一段；末行若是半行就砍到最后一个换行处，宁可少一行也不把半行当数据
 * @note 两条限制明写在报告里：**CSV 是简化解析**（不处理引号转义、字段内不换行）、
 *       **表格的列只加不删**（新文件列更少时旧列留着、显示为空）
 */
static void load_csv(cg_app_t *app, const char *path)
{
    long long sz;
    int       keep;

    if (s_text) {                                   /* 换文件：先还上一份 */
        cg_free(s_text);
        s_text = NULL;
    }
    s_len       = 0;
    s_row_count = 0;
    s_truncated = 0;

    sz          = cg_file_size(path);
    s_file_size = sz;
    s_truncated = (sz > CG_LOAD_LIMIT) ? 1 : 0;

    s_text = (unsigned char *)cg_file_read_bytes(path, &s_len);
    if (!s_text || s_len <= 0) {
        if (s_text) { cg_free(s_text); s_text = NULL; }
        s_len = 0;
        status_msg(app, "打不开或空文件：%s", path);
        render_page(app);                           /* 表格清空、分页归到 1 页空页 */
        return;
    }

    if (s_truncated && s_len > CG_LOAD_LIMIT) {
        s_len = CG_LOAD_LIMIT;
        keep  = s_len;
        while (keep > 0 && s_text[keep - 1] != '\n') keep--;
        if (keep > 0) s_len = keep;
    }

    index_build();                                  /* 一趟扫完，把行跨度填进索引数组 */
    parse_header();
    ensure_columns(app);                            /* 列只加不删 */

    s_loading = 1;
    fill_col_combos(app);                           /* 灌列名会派发选中事件，挡一下 */
    s_loading = 0;

    s_page         = 0;
    s_sort_col     = -1;
    s_sort_desc    = 0;
    s_sort_numeric = 0;

    render_page(app);

    report_reset(app);
    report(app, "已载入 %s", path);
    report(app, "  大小 %lld 字节%s；索引到 %d 行、%d 列。",
           s_file_size, s_truncated ? "（超过索引上限，只读了前一段）" : "",
           s_row_count, s_col_count);
    if (s_skip_empty) report(app, "  跳过的空行：%d 行（空行不占一页，也不进统计）", s_skip_empty);
    if (s_row_capped) report(app, "  行数超过上限 %d：后面的行没有进索引（状态栏标了「已截断」）", CG_MAX_ROWS);
    report(app, "两条限制：CSV 解析是简化版 —— 不处理引号转义、字段内不换行；");
    report(app, "          表格的列只加不删 —— 换成列更少的文件时，多出来的列会留着（显示为空）。");
}

/* ============================================================ 事件回调 */

/**
 * @brief 「打开 CSV」：弹打开对话框，选中的文件交给装载
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 自检（无窗口）下不弹对话框：只提示一句就返回，保证这条路径也被跑到
 */
static void on_btn_open_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        status_msg(app, "自检模式：不弹打开对话框，跳过");
        return;
    }

    path[0] = '\0';
    if (!cg_open_file_dialog(self, "打开 CSV",
                             "CSV / 文本(*.csv;*.txt)|*.csv;*.txt|所有文件(*.*)|*.*",
                             path, sizeof path)) {
        return;                                     /* 用户取消 */
    }
    load_csv(app, path);
}

/**
 * @brief 两个导出按钮共用一个回调：按控件 id 分辨「当前页」还是「全部行」
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 两个按钮做的是同一件事，只差一个范围 —— 用 id 分辨即可，不必写两份一样的代码
 */
static void on_btn_export_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_widget_id(self);

    (void)ev;
    (void)user;

    export_rows(cg_app_of(self), (id && strcmp(id, "btn_export_all") == 0) ? 0 : 1);
}

/**
 * @brief 「统计」：统计「统计列」下拉里选中的那一列
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 列号现取现用（不另存状态）；越界与没数据的提示都由统计函数自己说
 */
static void on_btn_stats_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cbo = cg_find(app, "cbo_stat");
    int          col = cbo ? cg_widget_value(cbo) : -1;

    (void)ev;
    (void)user;

    do_stats(app, col);
}

/**
 * @brief 下拉选好了：排序列直接重排，统计列只提示（等用户点「统计」）
 * @param self 被改的下拉框
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note **装载标记保护**：程序自己灌列名时（先清空再逐条加）也会派发选中事件，
 *       不挡一下会变成「载入文件的同时按每一列排一遍序」—— 十万行的文件要卡住好几秒
 */
static void on_cbo_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx;

    (void)ev;
    (void)user;

    if (s_loading) return;
    if (!id) return;

    idx = cg_widget_value(self);
    if (strcmp(id, "cbo_sort") == 0) {
        s_sort_desc = 0;                            /* 换列从升序开始；同一列换方向请双击表格 */
        sort_by(app, idx);
    } else if (strcmp(id, "cbo_stat") == 0) {
        if (idx >= 0 && idx < s_col_count)
            status_msg(app, "统计列：%s（点「统计」看当前页与全部行的对比）", s_col_title[idx]);
    }
}

/**
 * @brief 翻页：分页控件的当前值就是页号（1 起），换页只重渲染当前页
 * @param self 分页控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 翻页是**唯一**会改表格内容的入口：每次只填当前页那几百行，所以十万行的文件翻页也是
 *       「一页的活」，不会卡。装载期间程序正在回写页号，那不是用户翻页，直接返回
 */
static void on_page_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       page;

    (void)ev;
    (void)user;

    if (s_loading) return;

    page   = cg_widget_value(self);
    s_page = (page > 0) ? (page - 1) : 0;           /* 控件里 1 起，内部 0 起 */
    render_page(app);                               /* 里面会再夹一次页号 */
}

/**
 * @brief 排序的触发入口：本版表格没有「点了第几列表头」事件，改挂在双击表格上
 * @param self 表格控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 通读 .cgui 格式文档，`table` 只有 select / dblclick / check / gotfocus / lostfocus，
 *       没有「表头点击」这一项，所以排序的触发方式选了**双击表格行**：按「排序列」下拉里选中的
 *       那一列排，同一列再双击一次换升 / 降序
 */
static void on_table_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cbo = cg_find(app, "cbo_sort");
    int          col;

    (void)ev;
    (void)user;

    if (s_row_count <= 0) { status_msg(app, "还没有数据，先打开一个 CSV"); return; }

    col = cbo ? cg_widget_value(cbo) : -1;
    if (col < 0 || col >= s_col_count) {
        status_msg(app, "先在「排序列」里选一列，再双击表格排序");
        return;
    }

    s_sort_desc = (col == s_sort_col) ? !s_sort_desc : 0;   /* 同列双击：翻方向 */
    sort_by(app, col);
}

/**
 * @brief 界面就绪：只摆开场白与初始状态，**不读任何文件**
 * @param app 应用
 * @param user 就绪钩子的业务数据，本示例不用
 * @return void 无返回值
 * @note 大文件是用户点「打开 CSV」之后才读的：启动就读会让窗口半天不出来，自检（无窗口）下更是
 *       完全没有读的必要。总页数按 0 页起，分页控件最小 1 页
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;

    s_page         = 0;
    s_sort_col     = -1;
    s_sort_desc    = 0;
    s_sort_numeric = 0;
    s_loading      = 0;
    s_row_count    = 0;
    s_col_count    = 0;
    s_built_cols   = 0;
    s_file_size    = 0;
    s_truncated    = 0;

    report_reset(app);
    report(app, "请打开一个 CSV");
    report(app, "  打开后按页读：表格里只放当前页的 %d 行，翻页才换一批；", CG_PAGE_ROWS);
    report(app, "  单文件最多索引 %d KB、%d 行、%d 列，单元格最多 %d 字符。",
           CG_LOAD_LIMIT / 1024, CG_MAX_ROWS, CG_MAX_COLS, CG_CELL_MAX);
    report(app, "  选中「排序列」即排序，双击表格可在升 / 降序之间切换。");

    render_page(app);                               /* 空表 + 总页数 0 */

    /* 命令行点了名的 CSV：有窗口就顺手打开（自检不开窗，也就不用读盘） */
    if (s_boot_csv[0] && cg_app_has_window(app)) {
        report(app, "命令行点了名，直接打开：%s", s_boot_csv);
        load_csv(app, s_boot_csv);
    }
}

/**
 * @brief 判断字符串是不是以某个后缀结尾（不区分 ASCII 大小写）
 * @param s 待检查的字符串；NULL 安全
 * @param suffix 后缀，如 ".cgui"
 * @return int 1 是、0 否
 * @note 自己按字节折大小写，不引 <ctype.h>：这里只是给命令行参数分个类。
 *       必须定义在 main 之前 —— 放在后面会被当成隐式声明
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

/** .cgui 里写的事件名 → 本文件的回调；名字对不上会在自检里直接报出来 */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_btn_open_click",   on_btn_open_click),
    CG_BIND("on_btn_export_click", on_btn_export_click),   /* 两个导出按钮共用 */
    CG_BIND("on_btn_stats_click",  on_btn_stats_click),
    CG_BIND("on_cbo_select",       on_cbo_select),         /* 两个下拉共用 */
    CG_BIND("on_page_change",      on_page_change),
    CG_BIND("on_table_dblclick",   on_table_dblclick),     /* 挂在表格的双击上 */
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
        desc.cgui_path = (argc > 2) ? argv[2] : "dataview.cgui";
        return cg_app_selftest(&desc);
    }

    /* 参数约定（三种都支持：方便双击，也方便命令行直接看某个文件）：
     *   不写参数            → 按 exe 同目录自动找 dataview.cgui
     *   写 xxx.cgui         → 用它当界面；再给第二个参数就是启动要打开的 CSV
     *   写 xxx.csv / 其它   → 当数据文件：界面按同目录自动找，启动后直接打开它 */
    desc.cgui_path = "dataview.cgui";
    if (argc > 1 && ends_with_ci(argv[1], ".cgui")) {
        desc.cgui_path = argv[1];
        if (argc > 2) snprintf(s_boot_csv, sizeof s_boot_csv, "%s", argv[2]);
    } else if (argc > 1) {
        snprintf(s_boot_csv, sizeof s_boot_csv, "%s", argv[1]);
    }

    cg_log(CG_LOG_INFO, "数据浏览器启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
