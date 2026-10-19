/**
 * @file    fc.c
 * @brief   filecheck —— 套件模块（原 examples\filecheck 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 fc_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 fc_bindings、原 on_ready 变成 fc_init、
 *          窗口事件改成直连转发 fc_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   文件核对 / 镜像工具箱：比两个文件是否一致，不一致就指出差在哪
 * @details 面向固件与镜像发布的**纯离线**工具，不碰任何硬件：
 *
 *            A / B 两个文件槽    选中后立刻给出大小与 MD5 / SHA1 / SHA256 / SHA512 / CRC32
 *            「比对」            先比大小再比 SHA256，给出结论与状态灯（绿=一致 / 红=不一致 / 灭=无法比对）
 *            「找差异」          两文件都读得动且 ≤ 4 MB 时逐字节比：差异全部计数，表格只列前 200 条
 *            「复制报告」/「导出报告」  报告文本进剪贴板 / 写成 .txt；文件拖进窗口按 A、B 顺序落槽
 *
 *          摘要与校验一律走 cg_kit 的现成接口（cg_hash_file / cg_crc32_file），本文件
 *          **只 include cstackgui.h 与标准头**：没有 Windows 头，也不 malloc/free（只 cg_free 框架的堆串）。
 *
 * 界面在 filecheck.cgui 里；本文件只有业务与事件。
 * 编译：在项目根跑 build-app.bat filecheck        自检：filecheck.exe --selftest
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 常量 */

#define FC_PATH      1024              /**< 路径与配置缓冲上限 */
#define FC_MAX_DIFF  200               /**< 差异表格最多列出的条数（计数不设限） */
#define FC_MAX_BYTES (4 * 1024 * 1024) /**< 逐字节比对的文件大小上限：4 MB */
#define FC_REPORT    16384             /**< 报告缓冲上限 */
#define FC_LINE      512               /**< 单行格式化缓冲 */

/* ------------------------------------------------------------ 业务状态 */

/** 文件槽状态：空文件与打不开必须分清（前者能比大小，后者连摘要都算不出） */
typedef enum {
    SLOT_ST_NONE = 0,  /**< 还没选文件 */
    SLOT_ST_EMPTY,     /**< 选上了，但是 0 字节 */
    SLOT_ST_OK,        /**< 读得动，摘要已算好 */
    SLOT_ST_BAD        /**< 打不开：不存在 / 是目录 / 被占用 / 没权限 */
} slot_st_t;

/** 核对结论 */
typedef enum {
    V_NONE = 0, /**< 还没比对 */
    V_SAME,     /**< 一致 */
    V_DIFF,     /**< 不一致 */
    V_ERR       /**< 无法比对（槽是空的 / 文件打不开） */
} verdict_t;

/** 结论文字，下标就是 verdict_t */
static const char *const k_verdict[] = { "尚未比对", "一致", "不一致", "无法比对" };

/** 摘要名（报告里逐行输出），与 slot_t 里五个摘要字段一一对应 */
static const char *const k_algo[] = { "MD5   ", "SHA1  ", "SHA256", "SHA512", "CRC32 " };

/** 一个文件槽：路径 / 大小 / 状态 / 五种摘要串（空串 = 没算出来） */
typedef struct {
    char      path[FC_PATH];  /**< 完整路径，空串 = 还没选 */
    long long size;           /**< 字节数；SLOT_ST_BAD 时无意义 */
    slot_st_t st;             /**< 状态 */
    char      md5[33];        /**< MD5，32 字符小写十六进制 */
    char      sha1[41];       /**< SHA-1，40 字符 */
    char      sha256[65];     /**< SHA-256，64 字符 */
    char      sha512[129];    /**< SHA-512，128 字符 */
    char      crc32s[9];      /**< CRC32，8 位大写十六进制 */
} slot_t;

/** 逐字节比对的结果统计：count 是全量计数，不受"表格只列 200 条"的限制 */
typedef struct {
    int       count;  /**< 差异字节总数 */
    int       listed; /**< 已写进表格的条数（最多 FC_MAX_DIFF） */
    long long first;  /**< 第一处差异的偏移；没有差异时是 -1 */
} diff_stat_t;

/** 全部业务状态：事件回调需要时随时取（示例用文件级 static，真实项目也可以挂到控件 user 上） */
static struct {
    slot_t      a, b;               /**< 两个文件槽 */
    verdict_t   verdict;            /**< 最近一次结论 */
    diff_stat_t diff;               /**< 最近一次逐字节比对的统计 */
    char        cfg[FC_PATH + 200]; /**< 配置文件：<用户数据目录>\filecheck.ini */
    int         win_w, win_h;       /**< 最近一次窗口尺寸（size 事件里记，关闭时写盘） */
    char        report[FC_REPORT];  /**< 可导出的报告文本 */
    int         report_len;         /**< report 里已有的字节数 */
} s;

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 把字符串当十进制整数读（不引 stdlib.h）
 * @param str 待解析字符串，可有前导空白，读到第一个非数字就停；NULL 按空串处理
 * @return int 解析出的十进制值，一个数字都没有时是 0
 */
static int to_int(const char *str)
{
    int v = 0;

    if (!str) return 0;
    while (*str == ' ' || *str == '\t') str++;
    while (*str >= '0' && *str <= '9') v = v * 10 + (*str++ - '0');
    return v;
}

/**
 * @brief 字节数写成人看的文字（1024 -> "1.0 KB"、1258291 -> "1.2 MB"）
 * @param n 字节数；负数写成 "-"
 * @param out 输出缓冲
 * @param cap out 的字节数，至少 24
 * @return void 无返回值
 */
static void fmt_size(long long n, char *out, int cap)
{
    static const char *const unit[] = { "B", "KB", "MB", "GB" };
    long long                v = n;
    int                      i = 0, frac = 0;

    if (n < 0) { snprintf(out, cap, "-"); return; }
    while (v >= 1024 && i < 3) { frac = (int)((v % 1024) * 10 / 1024); v /= 1024; i++; }
    if (i == 0) snprintf(out, cap, "%lld B", n);
    else        snprintf(out, cap, "%lld.%d %s", v, frac, unit[i]);
}

/**
 * @brief 往日志终端写一行
 * @param app 应用
 * @param dir 方向：CG_TERM_SYS 程序说明 / CG_TERM_TX 本工具产出的结论
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void term_line(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[FC_LINE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_term_write(cg_find(app, "term_log"), dir, buf, -1);
}

/**
 * @brief 写状态栏第 part 段
 * @param app 应用
 * @param part 段号，0 起（0 = 最近一次操作、1 = A 槽、2 = B 槽）
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void status_line(cg_app_t *app, int part, const char *fmt, ...)
{
    char    buf[FC_LINE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_status_set(cg_find(app, "sb_main"), part, buf);
}

/* ------------------------------------------------------------ 文件槽 */

/**
 * @brief 把文件槽清回"还没选"（路径、大小、五种摘要一起清）
 * @param sl 目标文件槽
 * @return void 无返回值
 */
static void slot_reset(slot_t *sl)
{
    memset(sl, 0, sizeof(*sl));
    sl->st = SLOT_ST_NONE;
}

/**
 * @brief 这个槽里的文件能不能拿来比（空文件也算"能读"，只是没有内容）
 * @param sl 文件槽
 * @return int 1 能读（SLOT_ST_OK / SLOT_ST_EMPTY）、0 不能读
 */
static int slot_readable(const slot_t *sl)
{
    return sl && (sl->st == SLOT_ST_OK || sl->st == SLOT_ST_EMPTY);
}

/**
 * @brief 读文件算大小与五种摘要；空文件与打不开分开记（状态不同，界面提示也不同）
 * @param sl 目标文件槽，path 已填好；本函数改写 size / st / 五个摘要字段
 * @return void 无返回值
 * @details 故意不用读整个文件来探可读性：那会把整个镜像读进内存。先问大小（0 字节 = 空文件），
 *          再用摘要接口是否成功判断读不读得动 —— 摘要本来就是分块算的，大文件也不会把内存吃满。
 */
static void slot_fill_hashes(slot_t *sl)
{
    static const char *const algo[4] = { "md5", "sha1", "sha256", "sha512" };
    char       *h[4];
    unsigned    crc = 0;
    long long   sz;
    int         i;

    sl->size = 0;
    sl->st = SLOT_ST_NONE;
    sl->md5[0] = sl->sha1[0] = sl->sha256[0] = sl->sha512[0] = sl->crc32s[0] = '\0';
    if (!sl->path[0]) return;

    sz = cg_file_size(sl->path);
    if (sz < 0) { sl->st = SLOT_ST_BAD; return; }       /* 不存在 / 是目录 / 问不到 */
    sl->size = sz;
    if (sz == 0) { sl->st = SLOT_ST_EMPTY; return; }    /* 空文件：有明确状态，不算摘要 */

    for (i = 0; i < 4; i++) h[i] = cg_hash_file(sl->path, algo[i]);
    if (!h[0]) {                                        /* 打不开（占用 / 权限）：一个摘要都算不出 */
        for (i = 0; i < 4; i++) cg_free(h[i]);
        sl->st = SLOT_ST_BAD;
        return;
    }
    snprintf(sl->md5, sizeof(sl->md5), "%s", h[0]);
    snprintf(sl->sha1, sizeof(sl->sha1), "%s", h[1] ? h[1] : "");
    snprintf(sl->sha256, sizeof(sl->sha256), "%s", h[2] ? h[2] : "");
    snprintf(sl->sha512, sizeof(sl->sha512), "%s", h[3] ? h[3] : "");
    for (i = 0; i < 4; i++) cg_free(h[i]);
    if (cg_crc32_file(sl->path, &crc)) snprintf(sl->crc32s, sizeof(sl->crc32s), "%08X", crc);
    sl->st = SLOT_ST_OK;
}

/**
 * @brief 给文件槽装一个路径：算摘要、同步输入框、写日志、刷状态栏
 * @param app 应用
 * @param sl 目标文件槽
 * @param path 新路径；NULL 按空串处理（等于清空这个槽）
 * @param edt_id 要同步的路径输入框 id（"edt_a_path" / "edt_b_path"）
 * @return void 无返回值
 */
static void slot_set_path(cg_app_t *app, slot_t *sl, const char *path, const char *edt_id)
{
    cg_widget_t *edt;
    char         sz[32];

    snprintf(sl->path, sizeof(sl->path), "%s", path ? path : "");
    slot_fill_hashes(sl);

    edt = cg_find(app, edt_id);
    if (edt) cg_widget_set_text(edt, sl->path);

    fmt_size(sl->size, sz, sizeof(sz));
    if (sl->st == SLOT_ST_BAD)        term_line(app, CG_TERM_SYS, "打不开：%s", sl->path);
    else if (sl->st == SLOT_ST_EMPTY) term_line(app, CG_TERM_SYS, "空文件（0 字节）：%s", sl->path);
    else                              term_line(app, CG_TERM_SYS, "已选 %s（%s）", sl->path, sz);
    status_line(app, 0, "已选文件：%s", sl->path[0] ? sl->path : "(无)");
}

/**
 * @brief 弹「打开」对话框，把选中的文件装进 A 或 B 槽
 * @param app 应用
 * @param which 0 = A 槽、1 = B 槽
 * @return void 无返回值；自检（没有窗口）或用户取消时什么都不做
 */
static void pick_file(cg_app_t *app, int which)
{
    slot_t *sl = which ? &s.b : &s.a;
    char    path[FC_PATH], sz[32];

    if (!cg_app_has_window(app)) return;      /* --selftest：绝不用弹窗把 CI 卡住 */
    path[0] = '\0';
    if (!cg_open_file_dialog(cg_find(app, CG_WINDOW_ID), "选择要核对的文件",
                             "所有文件(*.*)|*.*", path, sizeof(path))) return;

    slot_set_path(app, sl, path, which ? "edt_b_path" : "edt_a_path");
    fmt_size(sl->size, sz, sizeof(sz));
    status_line(app, 1 + which, "%c：%s", which ? 'B' : 'A',
                sl->st == SLOT_ST_BAD ? "打不开" : sz);
}

/* ------------------------------------------------------------ 报告 */

/**
 * @brief 把一段文字拼进报告缓冲（超上限就截断，不越界）
 * @param text 要追加的文字，以 '\0' 结尾
 * @return void 无返回值
 */
static void report_append(const char *text)
{
    int n = (int)strlen(text);

    if (n > FC_REPORT - 1 - s.report_len) n = FC_REPORT - 1 - s.report_len;
    if (n <= 0) return;
    memcpy(s.report + s.report_len, text, (size_t)n);
    s.report_len += n;
    s.report[s.report_len] = '\0';
}

/**
 * @brief 按当前两个槽与最近一次结论，重新拼出可导出的报告文本
 * @return void 无返回值
 */
static void build_report(void)
{
    char line[FC_LINE], sz[32];
    int  i, j;

    s.report_len = 0;
    s.report[0]  = '\0';
    report_append("文件核对报告\r\n====================\r\n");

    for (i = 0; i < 2; i++) {
        const slot_t *sl = i ? &s.b : &s.a;
        const char   *hv[5];

        fmt_size(sl->size, sz, sizeof(sz));
        snprintf(line, sizeof(line), "[%c] %.500s\r\n", i ? 'B' : 'A',
                 sl->path[0] ? sl->path : "(未选择)");
        report_append(line);
        if (sl->st == SLOT_ST_NONE)  { report_append("    状态：未选择\r\n"); continue; }
        if (sl->st == SLOT_ST_BAD)   { report_append("    状态：打不开\r\n"); continue; }
        if (sl->st == SLOT_ST_EMPTY) { report_append("    状态：空文件（0 字节）\r\n"); continue; }

        snprintf(line, sizeof(line), "    大小：%s（%lld 字节）\r\n", sz, sl->size);
        report_append(line);
        hv[0] = sl->md5; hv[1] = sl->sha1; hv[2] = sl->sha256;
        hv[3] = sl->sha512; hv[4] = sl->crc32s;
        for (j = 0; j < 5; j++) {
            snprintf(line, sizeof(line), "    %s: %s\r\n", k_algo[j], hv[j]);
            report_append(line);
        }
    }

    snprintf(line, sizeof(line), "结论：%s\r\n", k_verdict[s.verdict]);
    report_append(line);
    if (s.diff.first >= 0 || s.diff.count > 0) {
        snprintf(line, sizeof(line), "逐字节差异：%d 处，表内列出 %d 条，首处偏移 0x%X\r\n",
                 s.diff.count, s.diff.listed, (unsigned)s.diff.first);
        report_append(line);
    }
}

/* ------------------------------------------------------------ 核对与差异 */

/**
 * @brief 把结论写进标签与状态灯（一致=绿灯、不一致=红灯、无法比对/未比对=灭）
 * @param app 应用
 * @param v 要展示的结论
 * @return void 无返回值
 */
static void show_verdict(cg_app_t *app, verdict_t v)
{
    cg_widget_t *led = cg_find(app, "led_verdict");
    cg_widget_t *lab = cg_find(app, "lab_verdict");

    s.verdict = v;
    if (lab) cg_widget_set_text(lab, k_verdict[v]);
    if (!led) return;
    if (v == V_NONE || v == V_ERR) {
        cg_led_set(led, 0);                    /* 灭：说明"没结果"，不是"有问题" */
    } else {
        cg_led_set_color(led, v == V_SAME ? CG_RGB(0x2E, 0xB0, 0x4F) : CG_RGB(0xD3, 0x2F, 0x2F));
        cg_led_set(led, 1);
    }
}

/**
 * @brief 比两个文件：先大小、后 SHA256，给出一致 / 不一致 / 无法比对
 * @param app 应用
 * @return void 无返回值
 */
static void do_compare(cg_app_t *app)
{
    verdict_t v = V_ERR;

    if (!slot_readable(&s.a) || !slot_readable(&s.b)) {
        term_line(app, CG_TERM_SYS, "两个文件槽都要先选到能打开的文件。");
    } else if (s.a.size != s.b.size) {
        v = V_DIFF;
        term_line(app, CG_TERM_TX, "大小不同：%lld 字节 / %lld 字节", s.a.size, s.b.size);
    } else if (s.a.st == SLOT_ST_EMPTY) {
        v = V_SAME;
        term_line(app, CG_TERM_TX, "两个都是空文件（0 字节），视为一致。");
    } else {
        v = (strcmp(s.a.sha256, s.b.sha256) == 0) ? V_SAME : V_DIFF;
        term_line(app, CG_TERM_TX, "SHA256 %s", k_verdict[v]);
        if (v == V_DIFF) term_line(app, CG_TERM_TX, "MD5 A=%s  B=%s", s.a.md5, s.b.md5);
    }
    show_verdict(app, v);
    build_report();
    status_line(app, 0, "比对：%s", k_verdict[v]);
}

/**
 * @brief 逐字节找差异：差异全部统计，表格只列前 200 条（行数据存偏移）
 * @param app 应用
 * @return void 无返回值
 * @details 只在两文件都读得动、且都不超过 4 MB 时才逐字节比；超限时提示改为比摘要，
 *          不硬读进内存 —— 镜像动辄几十上百 MB，摘要本来就是为这种场景准备的。
 */
static void do_diff(cg_app_t *app)
{
    cg_widget_t   *tbl  = cg_find(app, "tbl_diff");
    cg_widget_t   *hint = cg_find(app, "lab_diff_hint");
    unsigned char *ba, *bb;
    const char    *cells[3];
    char           fld[3][24], sz[32];
    int            la = 0, lb = 0, n, i, row;

    s.diff.count = 0; s.diff.listed = 0; s.diff.first = -1;
    if (tbl) cg_table_clear(tbl);

    if (!slot_readable(&s.a) || !slot_readable(&s.b)) {
        if (hint) cg_widget_set_text(hint, "两个文件槽都要先选到能打开的文件");
        term_line(app, CG_TERM_SYS, "找差异需要两个能打开的文件。");
        return;
    }
    if (s.a.size > FC_MAX_BYTES || s.b.size > FC_MAX_BYTES) {
        fmt_size(s.a.size > s.b.size ? s.a.size : s.b.size, sz, sizeof(sz));
        if (hint) cg_widget_set_text(hint, "文件超过 4 MB，改为比摘要");
        term_line(app, CG_TERM_SYS, "最大 %s，超过 4 MB 上限：不逐字节比，请点「比对」看摘要。", sz);
        return;
    }

    ba = cg_file_read_bytes(s.a.path, &la);
    bb = cg_file_read_bytes(s.b.path, &lb);
    if (!ba || !bb) {
        cg_free(ba); cg_free(bb);
        if (hint) cg_widget_set_text(hint, "读文件失败，无法找差异");
        term_line(app, CG_TERM_SYS, "读文件失败，无法逐字节比对。");
        return;
    }

    n = (la > lb) ? la : lb;
    for (i = 0; i < n; i++) {
        int xa = (i < la) ? ba[i] : -1;      /* -1 = 这个文件已经到头（多出来的部分） */
        int xb = (i < lb) ? bb[i] : -1;

        if (xa == xb) continue;
        s.diff.count++;
        if (s.diff.first < 0) s.diff.first = i;
        if (!tbl || s.diff.listed >= FC_MAX_DIFF) continue;
        snprintf(fld[0], sizeof(fld[0]), "0x%X", i);
        if (xa < 0) strcpy(fld[1], "--"); else snprintf(fld[1], sizeof(fld[1]), "%02X", xa);
        if (xb < 0) strcpy(fld[2], "--"); else snprintf(fld[2], sizeof(fld[2]), "%02X", xb);
        cells[0] = fld[0]; cells[1] = fld[1]; cells[2] = fld[2];
        row = cg_table_add_row(tbl, cells, 3);
        if (row >= 0) cg_table_set_row_value(tbl, row, i);   /* 行数据存偏移，select 时取用 */
        s.diff.listed++;
    }
    cg_free(ba);
    cg_free(bb);

    if (s.diff.count == 0) {
        if (hint) cg_widget_set_text(hint, "逐字节相同，没有差异");
        show_verdict(app, V_SAME);
    } else {
        char *t = cg_str_format("差异 %d 处（首处偏移 0x%X），表内列出 %d 条",
                                s.diff.count, (unsigned)s.diff.first, s.diff.listed);
        if (hint && t) cg_widget_set_text(hint, t);
        cg_free(t);
        show_verdict(app, V_DIFF);
    }
    build_report();
    status_line(app, 0, "找差异：%d 处不同", s.diff.count);
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 「选 A」按钮：弹打开对话框，选中的文件装进 A 槽
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_pick_a_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    pick_file(cg_app_of(self), 0);
}

/**
 * @brief 「选 B」按钮：弹打开对话框，选中的文件装进 B 槽
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_pick_b_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    pick_file(cg_app_of(self), 1);
}

/**
 * @brief 「开始比对」按钮：比大小与摘要，给出结论与状态灯
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_compare_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_compare(cg_app_of(self));
}

/**
 * @brief 「找差异」按钮：逐字节比对并把前若干处差异填进表格
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_diff_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_diff(cg_app_of(self));
}

/**
 * @brief 「复制报告」按钮：把拼好的报告送进剪贴板
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_copy_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    build_report();
    if (cg_clip_set_text(s.report) == 0) {
        term_line(app, CG_TERM_TX, "报告已复制到剪贴板（%d 字节）", s.report_len);
    } else {
        term_line(app, CG_TERM_SYS, "复制失败：剪贴板打不开");
    }
}

/**
 * @brief 「导出报告」按钮：弹保存对话框把报告写成文本；没有窗口时落到用户数据目录
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 弹框前先问有没有窗口：自检不开窗，改写用户数据目录，保证这条路径也被跑到
 */
static void on_export_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[FC_PATH];

    (void)ev;
    (void)user;
    build_report();
    path[0] = '\0';

    if (cg_app_has_window(app)) {
        if (!cg_save_file_dialog(self, "导出报告", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                                 "txt", path, sizeof(path))) {
            return;                         /* 用户取消：什么都不做 */
        }
    } else {
        char *dir  = cg_app_data_dir();     /* 用户数据目录，会自动建 */
        char *full = dir ? cg_path_join(dir, "filecheck.txt") : NULL;

        cg_free(dir);
        if (!full) return;
        snprintf(path, sizeof(path), "%s", full);
        cg_free(full);
    }

    if (cg_file_write(path, s.report) == 0) {
        term_line(app, CG_TERM_TX, "报告已导出到 %s（%d 字节）", cg_path_name(path), s.report_len);
    } else {
        term_line(app, CG_TERM_SYS, "导出失败：%s", path);
    }
}

/**
 * @brief 「清空」按钮：两个槽、差异表、日志、结论、状态栏一起归零
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    slot_reset(&s.a);
    slot_reset(&s.b);
    cg_table_clear(cg_find(app, "tbl_diff"));
    cg_term_clear(cg_find(app, "term_log"));
    s.diff.count = 0; s.diff.listed = 0; s.diff.first = -1;
    show_verdict(app, V_NONE);
    status_line(app, 0, "就绪：请选择 A / B 两个文件");
    status_line(app, 1, "A：未选");
    status_line(app, 2, "B：未选");
    term_line(app, CG_TERM_SYS, "已清空：两个槽、差异表、日志都归零");
}

/**
 * @brief 差异表选中行：读出三列文字与行上绑的偏移，写一行日志
 * @param self 差异表控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_tbl_diff_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    int         row = cg_table_selected(self);
    const char *c0;
    const char *c1;
    const char *c2;
    int         off;

    (void)ev;
    (void)user;
    if (row < 0) {
        term_line(app, CG_TERM_SYS, "差异表没有选中行（cg_table_selected = -1）");
        return;
    }
    c0  = cg_table_cell(self, row, 0);
    c1  = cg_table_cell(self, row, 1);
    c2  = cg_table_cell(self, row, 2);
    off = cg_table_row_value(self, row);
    term_line(app, CG_TERM_TX, "差异第 %d 行：%s | %s | %s（偏移 %d）",
              row, c0 ? c0 : "", c1 ? c1 : "", c2 ? c2 : "", off);
}

/**
 * @brief 分隔条松手：记一行当前分隔条位置
 * @param self 分隔条控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 真实项目在这里把位置写进 INI，下次启动用设置接口恢复
 */
static void on_sp_main_release(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    term_line(cg_app_of(self), CG_TERM_SYS, "分隔条 y=%d（真实项目会把位置写进 INI）",
              cg_splitter_pos(self));
}

/**
 * @brief 文件拖进窗口：前两个分别装进 A、B 槽并随即比对一次
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 取到的路径是副本（堆串），装完槽立刻 cg_free 掉
 */
static void on_files_drop(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       n   = cg_drop_count(app);
    char     *p;

    (void)ev;
    (void)user;
    if (n <= 0) {
        term_line(app, CG_TERM_SYS, "没有拖进任何文件");
        return;
    }
    p = cg_drop_file(app, 0);
    if (p) {
        slot_set_path(app, &s.a, p, "edt_a_path");
        cg_free(p);
    }
    if (n >= 2) {
        p = cg_drop_file(app, 1);
        if (p) {
            slot_set_path(app, &s.b, p, "edt_b_path");
            cg_free(p);
        }
    }
    term_line(app, CG_TERM_TX, "拖入 %d 个文件：前两个装进 A / B 槽", n);
    do_compare(app);
}

/**
 * @brief 窗口尺寸变了：先记在内存里（关闭时才写盘，别在拖动中反复写）
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 尺寸事件里 x = 宽、y = 高
 */
static void on_window_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s.win_w = cg_event_x(self);
    s.win_h = cg_event_y(self);
}

/**
 * @brief 关闭前：把窗口尺寸写进配置文件指向的 INI
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 只在配置文件路径存在、且尺寸有效时才写
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    if (s.cfg[0] && s.win_w > 0 && s.win_h > 0) {
        char *w = cg_str_format("%d", s.win_w);
        char *h = cg_str_format("%d", s.win_h);

        if (w && h) {
            cg_ini_write(s.cfg, "窗口", "宽", w);
            cg_ini_write(s.cfg, "窗口", "高", h);
            cg_log(CG_LOG_INFO, "窗口尺寸 %dx%d 已写入 %s", s.win_w, s.win_h, s.cfg);
        }
        cg_free(w);
        cg_free(h);
    }
}

/**
 * @brief 界面就绪：定位配置文件；有窗口时恢复上次的尺寸与两个文件并比一次
 * @param app 应用
 * @param user 就绪钩子的业务数据，本示例不用
 * @return void 无返回值
 * @note 自检（无窗口）下不恢复、不比对，只写开场白
 */
void fc_init(cg_app_t *app, void *user)
{
    char *dir;
    char *cfg;

    (void)user;
    dir = cg_app_data_dir();                        /* 用户数据目录，会自动建 */
    cfg = dir ? cg_path_join(dir, "filecheck.ini") : NULL;
    if (cfg) snprintf(s.cfg, sizeof(s.cfg), "%s", cfg);
    cg_free(cfg);
    cg_free(dir);

    if (cg_app_has_window(app) && s.cfg[0]) {
        char *w  = cg_ini_read(s.cfg, "窗口", "宽", NULL);
        char *h  = cg_ini_read(s.cfg, "窗口", "高", NULL);
        char *pa = cg_ini_read(s.cfg, "文件", "A", NULL);
        char *pb = cg_ini_read(s.cfg, "文件", "B", NULL);

        s.win_w = to_int(w);
        s.win_h = to_int(h);
        if (s.win_w > 0 && s.win_h > 0) cg_win_set_size(app, s.win_w, s.win_h);
        if (pa && *pa && cg_file_exists(pa)) slot_set_path(app, &s.a, pa, "edt_a_path");
        if (pb && *pb && cg_file_exists(pb)) slot_set_path(app, &s.b, pb, "edt_b_path");
        cg_free(w);
        cg_free(h);
        cg_free(pa);
        cg_free(pb);

        if (s.a.path[0] && s.b.path[0]) do_compare(app);   /* 上次的两个文件还在，随即比一次 */
    }

    term_line(app, CG_TERM_SYS, "文件核对工具就绪：左右各选一个文件，点「开始比对」。");
    term_line(app, CG_TERM_SYS, "也可以把文件直接拖进窗口：前两个自动落到 A / B 槽。");
    term_line(app, CG_TERM_SYS, "比完看差异表；「复制报告」进剪贴板，「导出报告」存成文本文件。");
    show_verdict(app, V_NONE);
}

/* ------------------------------------------------------------ 绑定表 */

const cg_binding_t fc_bindings[] = {
    CG_BIND("fc_on_pick_a_click",    on_pick_a_click),
    CG_BIND("fc_on_pick_b_click",    on_pick_b_click),
    CG_BIND("fc_on_compare_click",   on_compare_click),
    CG_BIND("fc_on_diff_click",      on_diff_click),
    CG_BIND("fc_on_copy_click",      on_copy_click),
    CG_BIND("fc_on_export_click",    on_export_click),
    CG_BIND("fc_on_clear_click",     on_clear_click),
    CG_BIND("fc_on_tbl_diff_select", on_tbl_diff_select),
    CG_BIND("fc_on_sp_main_release", on_sp_main_release),
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
void fc_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_close(self, ev, user);
}

/**
 * @brief 窗口事件 size 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void fc_app_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_window_size(self, ev, user);
}

/**
 * @brief 窗口事件 drop 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev drop
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void fc_app_drop(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_files_drop(self, ev, user);
}
