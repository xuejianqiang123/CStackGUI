/**
 * @file    ec.c
 * @brief   encconv —— 套件模块（原 examples\encconv 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 ec_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 ec_bindings、原 on_ready 变成 ec_init。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   编码与换行转换器：枚举目录 → 判定编码 → 预览 → GBK/UTF-8 互转 + 换行统一
 * @details 真实需求：一批 GBK 老文件要转成 UTF-8。界面在 encconv.cgui 里，本文件只有行为。
 *
 *          【这个工具的"价值"怎么让人一眼看到】（2026-09-28 按用户"好像没有体现出价值"的反馈改）
 *          光报"转换 2、输出 71 字节"这种元数据，价值是感觉不到的。真正的一幕是：
 *          **同一份字节，按 GBK 解读是正常中文，拿 UTF-8 打开就是乱码**。所以：
 *            * 「预览选中」（或双击表格）对疑似 GBK 的文件**两种解读都贴出来**（A 按 GBK 正确解、
 *              B 同一份字节按 UTF-8 强行解），差别当场可见；
 *            * 「造示例文件」一键在目录框那个目录里造 4 个文件（3 个真 GBK + 1 个纯 ASCII 对照），
 *              不用自己先找素材 —— 原先用户数据目录是空的，一开箱点「枚举文件」得到 0 个，
 *              整条流程没有可看的东西。
 *
 *          【第一步：先把运行时能力核实清楚，再决定怎么做】
 *
 *          - 编码转换（现成的两把刷子，都返回堆串、用完 cg_free，失败返回 NULL）：
 *              把 GBK 字节转成 UTF-8  /  把 UTF-8 字节转成 GBK
 *            入参必须是 **'\0' 结尾** 的串 —— 所以整读回来的字节要先拷进 static 缓冲、补一个 0。
 *            **没有**「这段字节是不是合法 UTF-8」的判定接口，也**没有**编码识别接口 ——
 *            校验函数 is_valid_utf8() 是本文件自写的，规则写在它上面的注释里。
 *
 *          - BOM：**没有**现成的 BOM 读写接口。`cg_file_read()` 会**自动去掉** UTF-8 BOM，
 *            而 `cg_file_read_bytes()` **一个字节都不动**（BOM 还在）—— 判定 BOM 只能用后者，
 *            自己判头三字节 EF BB BF；顺手判 UTF-16 的 FF FE / FE FF，判成「本工具不处理」。
 *
 *          - 文件与目录：整读（带长度出口）/ 整写 / 问大小 / 枚举（一次一个名字）/ 删文件 / 复制。
 *            **没有**「选目录」的通用对话框，也**没有**改名接口 —— 所以目录用输入框填路径，
 *            覆盖原文件只能直接写原名（因此默认不覆盖，另存新文件）。
 *
 *          【第二步：两条必须照实说的能力边界】
 *
 *          - 文件接口只有**整读**：没有分块 / 流式读。所以先量一遍大小，单文件超过 8 MB 就
 *            **只提示「跳过」** —— **绝不截断转换**：截一半的文件两边都不合法，是坏文件。
 *            一次最多 200 个文件，超了明说，不悄悄丢。
 *
 *          - **判定编码不可靠，本工具不装准**：判据只有一条 —— 先按严格 UTF-8 校验：合法（或带
 *            UTF-8 BOM）→ 判「UTF-8，无需转换」；不合法 → 判「疑似 GBK/ANSI」，作为候选。
 *            **这会误判**：纯 ASCII 谁都合法；GBK 内容偶尔恰好也是合法 UTF-8 序列。界面、日志、
 *            表格「结论」列都把这个不确定性讲出来，绝不假装识别得很准。
 *
 *          【参数约定】除了双击，也支持命令行直接跑一批（方便脚本化）：
 *              encconv.exe                  按 exe 同目录自动找 encconv.cgui
 *              encconv.exe xxx.cgui         用它当界面
 *              encconv.exe <目录>           界面自动找，启动后枚举该目录并**另存**转换一遍
 *              encconv.exe 任务单.ini       界面自动找，导入 [task] 节参数后枚举并**另存**转换一遍
 *           最后的自动转换**只另存新文件、绝不覆盖**（覆盖必须人工在界面上两步确认）：
 *           命令行 / 任务单把「覆盖原文件」勾上也只是填勾，自动跑仍停在两步确认的第一步。
 *
 *          【INI 任务单导入】把"转什么、怎么转"写成最朴素的 INI 交给工具（车间下发任务单、
 *          脚本批量生成的场景）：界面点「导入 INI 配置」，或命令行直接给 .ini。格式（[task] 节）：
 *              [task]
 *              dir=D:\data\old           要扫描的目录；空 = 保持目录框现状
 *              direction=gbk2utf8        gbk2utf8 | utf82gbk
 *              newline=keep              keep | crlf | lf
 *              bom=0                     0 | 1（只在 GBK→UTF-8 方向有意义）
 *              overwrite=0               0 | 1（真覆盖仍要两步确认，导入不绕过）
 *          读取用 cg_ini_read **逐键**读 —— 运行时没有枚举节/键的接口，所以任务单是
 *          键名固定的"表单"，不是任意 INI 的全量解析。缺项 = 保持界面当前值；非法值
 *          点名报出来并保持，绝不悄悄改。「造示例文件」会用 cg_ini_write 顺带造一份
 *          示例任务单 task.ini；「任务单」框空着时默认找「目录」下的 task.ini。
 *
 *          自检（--selftest）不开窗：就绪钩子里不读盘；枚举 / 读文件 / 写盘 / 弹框之前一律先问
 *          `cg_app_has_window()`，无窗口时整条 I/O 路径跳过。内存：整读回来的堆串拷进 static 缓冲
 *          后立刻 cg_free；业务代码不 malloc / free（代价是数据段多约 25 MB，一次只处理一个文件）。
 *
 * 编译：在项目根跑 build-app.bat encconv
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 上限与约定
 * 没有 malloc，就要把上限摆在明面上：数组尺寸全是编译期常量，超了都明确提示。
 */
#define ENCCONV_MAX_BYTES     (8 * 1024 * 1024)           /**< 单文件整读上限：超过只「跳过」，不截断 */
#define ENCCONV_OUT_MAX       (ENCCONV_MAX_BYTES * 3 + 8) /**< 输出缓冲：CRLF 翻倍、GBK→UTF-8 再 ×1.5 */
#define ENCCONV_MAX_FILES     200                         /**< 一次最多枚举 / 处理的文件数 */
#define ENCCONV_NAME_MAX      260                         /**< 文件名缓冲（含结尾 0） */
#define ENCCONV_DIR_MAX       1200                        /**< 目录路径缓冲 */
#define ENCCONV_PREVIEW_LINES 20                          /**< 预览只显示前多少行 */
#define ENCCONV_EXT_DESC      "*.txt/*.log/*.csv/*.md/*.ini/*.json/*.xml/*.cfg/*.c/*.h"

#define ENC_NL_KEEP           0   /**< 换行：保持原样 */
#define ENC_NL_CRLF           1   /**< 换行：统一 CRLF */
#define ENC_NL_LF             2   /**< 换行：统一 LF */

#define ENC_V_UTF8            0   /**< 合法 UTF-8，无需转换 */
#define ENC_V_UTF8_BOM        1   /**< 带 UTF-8 BOM，本身即 UTF-8 */
#define ENC_V_GBK             2   /**< 不是合法 UTF-8，疑似 GBK/ANSI，候选转换 */
#define ENC_V_UTF16           3   /**< UTF-16 BOM，本工具不处理 */
#define ENC_V_BINARY          4   /**< 含 0x00，疑似二进制，跳过 */
#define ENC_V_TOO_BIG         5   /**< 超 8 MB 上限，跳过（不截断） */
#define ENC_V_READFAIL        6   /**< 读不到（不存在 / 被占用 / 是目录） */

/* ------------------------------------------------------------ 业务状态 */

/** 一个候选文件的：名字 + 大小 + 判定 */
typedef struct {
    char      name[ENCCONV_NAME_MAX]; /**< 相对目录的文件名 */
    long long size;                   /**< 字节数；问不到是 -1 */
    int       verdict;                /**< ENC_V_* 判定结果 */
} enc_file_t;

static enc_file_t     s_files[ENCCONV_MAX_FILES];  /**< 枚举结果表 */
static int            s_count;                     /**< 表里有多少个 */
static char           s_dir[ENCCONV_DIR_MAX];      /**< 当前目录（枚举那一刻记下） */
static unsigned char  s_in[ENCCONV_MAX_BYTES + 1]; /**< 整读拷贝缓冲（补 '\0'，供转换函数用） */
static unsigned char  s_out[ENCCONV_OUT_MAX];      /**< 转换输出缓冲（BOM + 换行统一后的字节） */
static int            s_done, s_skip, s_fail;      /**< 上次转换的统计：已转 / 跳过 / 失败 */
static int            s_confirmed;                 /**< 覆盖确认：两步走的第二下 */

static char s_boot_dir[ENCCONV_DIR_MAX]; /**< 命令行点名的目录：开窗后枚举并另存转换一遍 */
static char s_boot_ini[ENCCONV_DIR_MAX]; /**< 命令行点名的 INI 任务单：开窗后导入参数再跑一遍 */

/* ------------------------------------------------------------ 判定与文本小工具 */

/**
 * @brief 严格校验一段字节是不是合法 UTF-8（运行时没有这个接口，本文件自写）
 * @param s 待检查的字节
 * @param len s 的字节数（不以 '\0' 为界，按长度走；0x00 被视为合法 ASCII 字节）
 * @return int 1 = 整段都是合法 UTF-8；0 = 出现非法序列
 * @note 规则（RFC 3629）：1 字节 0x00..0x7F；多字节首字节 0xC2..0xF4，其后每字节必须落在
 *       0x80..0xBF；**拒绝过长编码**（0xC0/0xC1、0xE0 后的次字节 < 0xA0、0xF0 后的次字节 < 0x90）、
 *       **拒绝代理区**（0xED 后次字节 <= 0x9F 即 U+D800..U+DFFF）、**拒绝 >= U+10FFFF**
 *       （0xF4 后次字节 > 0x8F，以及 0xF5..0xFF 一律非法）。截断（续字节不够）也判非法
 */
static int is_valid_utf8(const unsigned char *s, int len)
{
    int i = 0;

    while (i < len) {
        unsigned char c = s[i];
        int           need, lo, hi, j;

        if (c < 0x80) { i++; continue; }                 /* ASCII：单字节 */
        if (c >= 0xC2 && c <= 0xDF)      { need = 1; lo = 0x80; hi = 0xBF; }
        else if (c == 0xE0)              { need = 2; lo = 0xA0; hi = 0xBF; }  /* 拒绝过长 */
        else if (c >= 0xE1 && c <= 0xEC) { need = 2; lo = 0x80; hi = 0xBF; }
        else if (c == 0xED)              { need = 2; lo = 0x80; hi = 0x9F; }  /* 拒绝代理区 */
        else if (c >= 0xEE && c <= 0xEF) { need = 2; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF0)              { need = 3; lo = 0x90; hi = 0xBF; }  /* 拒绝过长 */
        else if (c >= 0xF1 && c <= 0xF3) { need = 3; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF4)              { need = 3; lo = 0x80; hi = 0x8F; }  /* <= U+10FFFF */
        else return 0;                                                        /* 0xC0/0xC1/0xF5..0xFF */

        if (i + need >= len) return 0;                   /* 续字节不够：截断 */
        if (s[i + 1] < lo || s[i + 1] > hi) return 0;
        for (j = 2; j <= need; j++) {
            if (s[i + j] < 0x80 || s[i + j] > 0xBF) return 0;
        }
        i += need + 1;
    }
    return 1;
}

/**
 * @brief 判定一个文件最可能的编码（整读一遍字节，按字节判 BOM / 0x00 / UTF-8 合法性）
 * @param path 文件全路径（UTF-8）
 * @return int 判定结果 ENC_V_*
 * @note 这是**猜测**不是识别：合法 UTF-8 也可能是恰好合法的 GBK 字节，纯 ASCII 谁都合法。
 *       含 0x00 的按二进制跳过（否则字符串式转换会在第一个 0 处被截断）
 */
static int classify(const char *path)
{
    unsigned char *raw;
    long long      sz = cg_file_size(path);
    int            len = 0, i, verdict;

    if (sz < 0) return ENC_V_READFAIL;
    if (sz > ENCCONV_MAX_BYTES) return ENC_V_TOO_BIG;
    raw = cg_file_read_bytes(path, &len);                /* 不去 BOM，二进制一个字节都不动 */
    if (!raw) return ENC_V_READFAIL;

    if (len >= 2 && ((raw[0] == 0xFF && raw[1] == 0xFE) ||
                     (raw[0] == 0xFE && raw[1] == 0xFF))) {
        verdict = ENC_V_UTF16;
    } else if (len >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        verdict = ENC_V_UTF8_BOM;                        /* 有 BOM 基本就是 UTF-8 */
    } else {
        verdict = ENC_V_UTF8;
        for (i = 0; i < len; i++) {
            if (raw[i] == 0x00) { verdict = ENC_V_BINARY; break; }
        }
        if (verdict == ENC_V_UTF8 && !is_valid_utf8(raw, len)) verdict = ENC_V_GBK;
    }
    cg_free(raw);                                        /* 框架给的堆缓冲马上还回去 */
    return verdict;
}

/**
 * @brief 文件名扩展名要不要收（只收文本类，避免把子目录 / 无后缀文件带进来）
 * @param name 文件名（不含路径）
 * @return int 1 收、0 不收
 */
static int ext_ok(const char *name)
{
    static const char *ok[] = { ".txt", ".log", ".csv", ".md", ".ini",
                                ".json", ".xml", ".cfg", ".c", ".h" };
    const char        *ext  = cg_path_ext(name);
    int                i;

    if (!ext || !ext[0]) return 0;
    for (i = 0; i < (int)(sizeof(ok) / sizeof(ok[0])); i++) {
        if (strcmp(ext, ok[i]) == 0) return 1;
    }
    return 0;
}

/**
 * @brief 把字节数写成人看的 KB / MB（负数写破折号）
 * @param bytes 字节数
 * @param out 输出缓冲，写进 '\0' 结尾的文本
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void human_size(long long bytes, char *out, int cap)
{
    if (bytes < 0)                 snprintf(out, (size_t)cap, "—");
    else if (bytes >= 1024 * 1024) snprintf(out, (size_t)cap, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)        snprintf(out, (size_t)cap, "%.1f KB", (double)bytes / 1024.0);
    else                           snprintf(out, (size_t)cap, "%lld B", bytes);
}

/**
 * @brief 判定结果转成显示的短标签或「结论」长句
 * @param v 判定结果 ENC_V_*
 * @param brief 非 0 返回短标签（表格「判定」列），0 返回长结论（表格「结论」列）
 * @return const char * 静态字符串，别 free
 */
static const char *enc_vtext(int v, int brief)
{
    if (brief) {
        switch (v) {
        case ENC_V_UTF8:     return "UTF-8";
        case ENC_V_UTF8_BOM: return "UTF-8(BOM)";
        case ENC_V_GBK:      return "疑似 GBK";
        case ENC_V_UTF16:    return "UTF-16";
        case ENC_V_BINARY:   return "疑似二进制";
        case ENC_V_TOO_BIG:  return "超上限";
        default:             return "读失败";
        }
    }
    switch (v) {
    case ENC_V_UTF8:     return "无需转换（已是合法 UTF-8；纯 ASCII 也算）";
    case ENC_V_UTF8_BOM: return "带 UTF-8 BOM，本身即 UTF-8";
    case ENC_V_GBK:      return "候选：疑似 GBK/ANSI（判定只是猜，可能误判）";
    case ENC_V_UTF16:    return "UTF-16 BOM，本工具不处理";
    case ENC_V_BINARY:   return "含 0x00，疑似二进制，跳过";
    case ENC_V_TOO_BIG:  return "超 8 MB 上限，跳过（不截断）";
    default:             return "读不到（不存在 / 被占用 / 是目录）";
    }
}

/**
 * @brief 按当前方向，判一个判定结果是不是本次要转换的候选
 * @param v 判定结果 ENC_V_*
 * @param gbk2utf8 非 0 = GBK → UTF-8；0 = UTF-8 → GBK
 * @return int 1 是候选、0 不是
 */
static int is_candidate(int v, int gbk2utf8)
{
    return gbk2utf8 ? (v == ENC_V_GBK) : (v == ENC_V_UTF8 || v == ENC_V_UTF8_BOM);
}

/**
 * @brief 往终端写一行（本示例唯一的日志出口）
 * @param app 应用
 * @param dir 方向：CG_TERM_RX 预览内容 / CG_TERM_TX 用户操作结果 / CG_TERM_SYS 程序说明
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void note(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_term_write(cg_find(app, "term_out"), dir, buf, -1);
}

/**
 * @brief 把枚举表里的第 idx 个写成表格的一行（文件名 / 大小 / 判定 / 结论）
 * @param tbl 文件列表控件
 * @param idx 枚举表里的下标
 * @return void 无返回值
 */
static void table_add(cg_widget_t *tbl, int idx)
{
    char        size[32];
    const char *cells[4];

    human_size(s_files[idx].size, size, sizeof(size));
    cells[0] = s_files[idx].name;
    cells[1] = size;
    cells[2] = enc_vtext(s_files[idx].verdict, 1);
    cells[3] = enc_vtext(s_files[idx].verdict, 0);
    cg_table_add_row(tbl, cells, 4);   /* 表格内部会拷贝文字，这几个缓冲马上可以失效 */
}

/**
 * @brief 刷新状态栏第 2 段：共 N 个 / 待转 M / 已转 K / 跳过 J / 失败 F
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    cg_widget_t *cbo = cg_find(app, "cbo_dir");
    int          gbk2utf8 = (cbo && cg_widget_value(cbo) == 0);
    int          i, pending = 0;

    for (i = 0; i < s_count; i++) {
        if (is_candidate(s_files[i].verdict, gbk2utf8)) pending++;
    }
    cg_status_setf(cg_find(app, "sb_main"), 2, "共 %d 个 / 待转 %d / 已转 %d / 跳过 %d / 失败 %d",
                   s_count, pending, s_done, s_skip, s_fail);
}

/**
 * @brief 算出前 maxlines 行（含行尾换行）一共多少字节，用来截预览
 * @param s 文本
 * @param len s 的字节数
 * @param maxlines 最多取几行
 * @return int 截断长度（字节）
 */
static int first_lines_len(const char *s, int len, int maxlines)
{
    int i, lines = 0;

    for (i = 0; i < len; i++) {
        if (s[i] == '\n') {
            lines++;
            if (lines >= maxlines) return i + 1;
        }
    }
    return len;
}

/* ------------------------------------------------------------ 枚举目录 */

/**
 * @brief 枚举目录里的文本类文件，逐个判定编码，填进表格
 * @param app 应用
 * @return void 无返回值
 * @note 用扩展名过滤（目录枚举的通配符只能给一个，所以先收全部再按扩展名筛）；
 *       每个文件都整读一遍做判定（没有编码识别接口，判定必须看字节）；子目录靠"是不是文件"排除
 */
static void scan_dir(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_files");
    const char  *dir = cg_widget_text(cg_find(app, "edt_dir"));
    char         name[ENCCONV_NAME_MAX];
    int          i = 0, capped = 0;

    cg_table_clear(tbl);
    s_count = 0;
    s_done = 0;
    s_skip = 0;
    s_fail = 0;
    s_confirmed = 0;
    snprintf(s_dir, sizeof(s_dir), "%.1000s", dir ? dir : "");
    if (!s_dir[0]) {
        note(app, CG_TERM_SYS, "先在「目录」里填一个路径。");
        refresh_status(app);
        return;
    }
    if (!cg_dir_exists(s_dir)) {
        note(app, CG_TERM_SYS, "目录不存在：%s", s_dir);
        refresh_status(app);
        return;
    }

    note(app, CG_TERM_SYS, "枚举 %s（只收文本类扩展名：%s）…", s_dir, ENCCONV_EXT_DESC);
    while (cg_dir_list(s_dir, "*", i, name, (int)sizeof(name))) {
        char     *full;
        long long sz;
        int       v;

        i++;
        if (!ext_ok(name)) continue;                     /* 扩展名不符：跳过（子目录也没有扩展名） */
        full = cg_path_join(s_dir, name);
        if (!full) continue;
        if (!cg_file_exists(full)) { cg_free(full); continue; }   /* 目录不算文件 */
        if (s_count >= ENCCONV_MAX_FILES) { capped = 1; cg_free(full); break; }
        sz = cg_file_size(full);
        v  = (sz < 0) ? ENC_V_READFAIL : classify(full);
        cg_free(full);

        snprintf(s_files[s_count].name, ENCCONV_NAME_MAX, "%s", name);
        s_files[s_count].size    = sz;
        s_files[s_count].verdict = v;
        s_count++;
        table_add(tbl, s_count - 1);
    }

    note(app, CG_TERM_TX, "枚举完成：%d 个文件%s。", s_count, capped ? "（已达 200 上限，其余未列出）" : "");
    if (capped) {
        note(app, CG_TERM_SYS, "文件数超过 %d 上限：只处理前 %d 个，不悄悄丢。",
             ENCCONV_MAX_FILES, ENCCONV_MAX_FILES);
    }
    note(app, CG_TERM_SYS, "「判定」只是猜测：合法 UTF-8 判它，不合法判「疑似 GBK」——纯 ASCII 谁都合法。");
    cg_status_setf(cg_find(app, "sb_main"), 0, "目录：%.200s · 上限 %d MB / %d 个",
                   s_dir, ENCCONV_MAX_BYTES / 1024 / 1024, ENCCONV_MAX_FILES);
    refresh_status(app);
}

/* ------------------------------------------------------------ 预览 */

/**
 * @brief 把表格里选中文件的前 20 行按「当前判定」试解后显示在终端
 * @param app 应用
 * @return void 无返回值
 * @note 明确标注这是按某编码试解的结果：乱码就是乱码，不掩盖。UTF-8 直接用原字节，
 *       疑似 GBK 才过一次转换；UTF-16 / 二进制 / 超上限只说明不预览
 */
static void preview_selected(cg_app_t *app)
{
    cg_widget_t   *tbl  = cg_find(app, "tbl_files");
    cg_widget_t   *term = cg_find(app, "term_out");
    unsigned char *raw;
    char          *full;
    int            row, len = 0;

    if (!cg_app_has_window(app)) {
        note(app, CG_TERM_SYS, "自检模式：不读盘预览。");
        return;
    }
    if (!tbl) return;
    row = cg_table_selected(tbl);
    if (row < 0 || row >= s_count) {
        note(app, CG_TERM_SYS, "先在左边列表里点一个文件。");
        return;
    }

    cg_term_clear(term);
    note(app, CG_TERM_SYS, "预览：%s（%s）—— 只显示前 %d 行；疑似 GBK 的会贴**两种解读**做对照",
         s_files[row].name, enc_vtext(s_files[row].verdict, 1), ENCCONV_PREVIEW_LINES);

    if (s_files[row].verdict == ENC_V_TOO_BIG) {
        note(app, CG_TERM_SYS, "文件超上限，未读取。");
        return;
    }
    if (s_files[row].verdict == ENC_V_UTF16 || s_files[row].verdict == ENC_V_BINARY ||
        s_files[row].verdict == ENC_V_READFAIL) {
        note(app, CG_TERM_SYS, "按文本预览没有意义（%s）。", enc_vtext(s_files[row].verdict, 0));
        return;
    }

    full = cg_path_join(s_dir, s_files[row].name);
    if (!full) {
        note(app, CG_TERM_SYS, "路径拼不出来。");
        return;
    }
    raw = cg_file_read_bytes(full, &len);
    cg_free(full);
    if (!raw) {
        note(app, CG_TERM_SYS, "读不进来。");
        return;
    }

    {
        int off = 0;

        if (s_files[row].verdict == ENC_V_UTF8_BOM && len >= 3 &&
            raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) off = 3;   /* 显示时略过 BOM */

        if (s_files[row].verdict == ENC_V_GBK) {
            char *txt;
            int   n = len - off;

            if (n > ENCCONV_MAX_BYTES) n = ENCCONV_MAX_BYTES;
            memcpy(s_in, raw + off, (size_t)n);
            s_in[n] = '\0';

            /* A：按 GBK 正确解读 —— 这就是"转换之后"的样子 */
            note(app, CG_TERM_TX, "【A 按 GBK 正确解读】（= 转换之后的内容）");
            txt = cg_gbk_to_utf8((const char *)s_in);   /* 试解：这一步对错都照实显示 */
            if (txt) {
                cg_term_write(term, CG_TERM_RX, txt, first_lines_len(txt, (int)strlen(txt), ENCCONV_PREVIEW_LINES));
                cg_free(txt);
            } else {
                note(app, CG_TERM_SYS, "按 GBK 解不出内容。");
            }

            /* B：**同一份字节**按 UTF-8 强行解读 —— 也就是"现在拿编辑器打开它看到的乱码"。
             * 这里故意一个字节都不转，直接把原始字节写进终端，让它按 UTF-8 解出来 */
            note(app, CG_TERM_SYS, "【B 同一份字节按 UTF-8 强行解读】（= 现在双击文件看到的乱码）");
            cg_term_write(term, CG_TERM_RX, raw + off,
                          first_lines_len((const char *)raw + off, n, ENCCONV_PREVIEW_LINES));
        } else {
            int cut = first_lines_len((const char *)raw + off, len - off, ENCCONV_PREVIEW_LINES);

            cg_term_write(term, CG_TERM_RX, raw + off, cut);   /* 合法 UTF-8：原字节直接显示 */
        }
    }
    cg_free(raw);
}

/**
 * @brief 在当前目录框里造几个典型 GBK 老文件（一键就有素材可演示）
 * @param app 应用
 * @return void 无返回值
 * @note 为什么需要它：本工具的价值是"把乱码变回正常中文"，可这要求手头先有一批 GBK 老文件 ——
 *       一开箱目录是空的，点「枚举文件」得到 0 个，价值就没机会展示（用户反馈）。造的 4 个文件
 *       对应真实工控场景：GBK+CRLF 的报警记录、GBK+LF 的参数表、GBK 的设备清单，外加一个
 *       **纯 ASCII 对照**（它会判"无需转换"，把"纯 ASCII 谁都合法"这条边界也一并演示）。
 *       源码里的文本是 UTF-8，落盘前用 cg_utf8_to_gbk 转成 GBK 字节 —— 所以造出来的是真 GBK 文件。
 *       同名文件**绝不覆盖**（撞上的可能是用户的真文件），跳过并报出来
 */
static void make_samples(cg_app_t *app)
{
    static const struct {
        const char *name;   /**< 文件名 */
        const char *text;   /**< 内容（UTF-8 源码，落盘前转 GBK） */
    } k_samples[] = {
        { "报警记录_2026-09-28.txt",
          "时间,设备,信息\r\n19:34:01,PLC-1,主轴过载报警\r\n19:35:22,PLC-2,急停按下\r\n19:36:10,PLC-1,恢复正常\r\n" },
        { "参数表.txt",
          "参数名,设定值,单位\n目标温度,185,度\n保温时间,45,分钟\n冷却方式,风冷,-\n" },
        { "设备清单.csv",
          "编号,名称,状态\r\n01,一号注塑机,运行\r\n02,二号注塑机,停机\r\n03,机械手,待机\r\n" },
        { "readme_ascii.txt",
          "plain ascii only - this one needs no conversion\r\n" },
    };
    cg_widget_t *edt = cg_find(app, "edt_dir");
    const char  *dir = edt ? cg_widget_text(edt) : NULL;
    char        *fallback = NULL;
    int          i, ok = 0, fail = 0;

    if (!dir || !*dir) {                     /* 目录框空着：先落到用户数据目录 */
        fallback = cg_app_data_dir();
        if (!fallback) {
            note(app, CG_TERM_SYS, "目录框空着，且取用户数据目录失败：先把目录填上。");
            return;
        }
        if (edt) cg_widget_set_text(edt, fallback);
        dir = fallback;
    }

    for (i = 0; i < (int)(sizeof(k_samples) / sizeof(k_samples[0])); i++) {
        char *gbk  = cg_utf8_to_gbk(k_samples[i].text);      /* UTF-8 源码 -> GBK 字节 */
        char *path = cg_path_join(dir, k_samples[i].name);

        if (path && cg_file_exists(path)) {      /* **绝不覆盖同名文件**：这按钮只是个"造素材"的入口，
                                                    撞上的很可能是用户的真文件（本示例转换时也从不覆盖） */
            fail++;
            note(app, CG_TERM_SYS, "  %s：已存在，跳过（不覆盖）。", k_samples[i].name);
        } else if (!gbk || !path || cg_file_write_bytes(path, gbk, (int)strlen(gbk)) != 0) {
            fail++;
            note(app, CG_TERM_SYS, "  %s：写不进去（目录可能不存在），跳过。", k_samples[i].name);
        } else {
            ok++;
            note(app, CG_TERM_TX, "  已造 %s（%d 字节，GBK）", k_samples[i].name, (int)strlen(gbk));
        }
        if (gbk)  cg_free(gbk);
        if (path) cg_free(path);
    }

    /* 顺带造一份示例任务单（cg_ini_write 逐键写：节不存在会自动补 [task]）。
     * dir 故意留空 —— 任务单跟数据同目录，导入时"dir 未填 = 保持目录框现状"，
     * 而「造示例文件」已经把目录框填好了，导入后直接就能枚举 */
    {
        char *task = cg_path_join(dir, "task.ini");

        if (task && cg_file_exists(task)) {
            note(app, CG_TERM_SYS, "  task.ini：已存在，跳过（不覆盖）。");
        } else if (!task ||
                   cg_ini_write(task, "task", "dir", "") != 0 ||
                   cg_ini_write(task, "task", "direction", "gbk2utf8") != 0 ||
                   cg_ini_write(task, "task", "newline", "keep") != 0 ||
                   cg_ini_write(task, "task", "bom", "0") != 0 ||
                   cg_ini_write(task, "task", "overwrite", "0") != 0) {
            note(app, CG_TERM_SYS, "  task.ini：写不进去，跳过。");
        } else {
            note(app, CG_TERM_TX, "  已造 task.ini（示例任务单：GBK→UTF-8，dir 留空 = 导入时保持目录框现状）");
        }
        if (task) cg_free(task);
    }

    note(app, CG_TERM_SYS, "造示例完成：%d 个成功、%d 个失败（目录：%s）", ok, fail, dir);
    note(app, CG_TERM_SYS, "  其中 readme_ascii.txt 是纯 ASCII 对照：它会判「无需转换」（谁都能读它）");
    note(app, CG_TERM_SYS, "  点「导入 INI 配置」（任务单框空着就找这份 task.ini）可一键把参数填好");
    if (fallback) cg_free(fallback);

    scan_dir(app);                            /* 顺手枚举一遍：接着双击一行就能看对照预览 */
}

/* ------------------------------------------------------------ INI 任务单导入 */

/**
 * @brief 两个串按 ASCII 折大小写后是否相等（任务单的枚举值判断用）
 * @param a 串一；NULL 安全
 * @param b 串二；NULL 安全
 * @return int 1 相等、0 不等
 * @note 不引 <ctype.h> / 平台专有 stricmp：与 ends_with_ci 同款手写，规则一目了然
 */
static int equal_ci(const char *a, const char *b)
{
    int i;

    if (!a || !b) return 0;
    if (strlen(a) != strlen(b)) return 0;
    for (i = 0; a[i]; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];

        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

/**
 * @brief 把 INI 任务单（[task] 节）里的转换参数逐键读出、填进界面控件
 * @param app 应用：找控件、往终端写报告
 * @param path 任务单 INI 的路径
 * @return int 0 = 已导入（含部分键非法/缺省的情况）；-1 = 文件不存在，什么都没动
 * @note 读取用 cg_ini_read 逐键读 —— 运行时**没有**枚举节/键的接口，所以任务单是键名固定的
 *       "表单"（dir / direction / newline / bom / overwrite），不是任意 INI 的全量解析。
 *       行为约定：缺项 = 保持界面当前值；非法值点名报出来并保持当前值，绝不悄悄改。
 *       导入只填参数**不执行转换**：执行仍走「开始转换」，覆盖的两步确认也不被导入绕过
 */
static int import_task_ini(cg_app_t *app, const char *path)
{
    cg_widget_t *cbo_dir = cg_find(app, "cbo_dir");
    cg_widget_t *cbo_nl  = cg_find(app, "cbo_nl");
    char        *v;
    int          bad = 0;

    if (!path || !path[0] || !cg_file_exists(path)) {
        note(app, CG_TERM_SYS, "任务单不存在：%s", (path && path[0]) ? path : "（路径空）");
        return -1;
    }

    note(app, CG_TERM_TX, "导入 INI 任务单：%s", path);

    /* dir：空 = 保持目录框现状（任务单往往和数据放一起，目录让使用者自己选） */
    v = cg_ini_read(path, "task", "dir", "");
    if (v && v[0]) {
        cg_widget_set_text(cg_find(app, "edt_dir"), v);
        note(app, CG_TERM_SYS, "  dir = %s", v);
    } else {
        note(app, CG_TERM_SYS, "  dir 未填，保持目录框现状。");
    }
    cg_free(v);

    /* direction：gbk2utf8 / utf82gbk（大小写不敏感） */
    v = cg_ini_read(path, "task", "direction", "");
    if (equal_ci(v, "gbk2utf8")) {
        cg_widget_set_value(cbo_dir, 0);
    } else if (equal_ci(v, "utf82gbk")) {
        cg_widget_set_value(cbo_dir, 1);
    } else {
        note(app, CG_TERM_SYS, "  direction = %s（非法：收 gbk2utf8 / utf82gbk），保持当前方向。",
             (v && v[0]) ? v : "未填");
        bad += (v && v[0]) ? 1 : 0;
    }
    cg_free(v);

    /* newline：keep / crlf / lf */
    v = cg_ini_read(path, "task", "newline", "");
    if      (equal_ci(v, "keep")) cg_widget_set_value(cbo_nl, ENC_NL_KEEP);
    else if (equal_ci(v, "crlf")) cg_widget_set_value(cbo_nl, ENC_NL_CRLF);
    else if (equal_ci(v, "lf"))   cg_widget_set_value(cbo_nl, ENC_NL_LF);
    else {
        note(app, CG_TERM_SYS, "  newline = %s（非法：收 keep / crlf / lf），保持当前换行。",
             (v && v[0]) ? v : "未填");
        bad += (v && v[0]) ? 1 : 0;
    }
    cg_free(v);

    /* bom / overwrite：0 或 1；覆盖只填勾，真覆盖仍要两步确认 */
    v = cg_ini_read(path, "task", "bom", "");
    if      (equal_ci(v, "0")) cg_widget_set_checked(cg_find(app, "chk_bom"), 0);
    else if (equal_ci(v, "1")) cg_widget_set_checked(cg_find(app, "chk_bom"), 1);
    else {
        note(app, CG_TERM_SYS, "  bom = %s（非法：收 0 / 1），保持当前勾选。",
             (v && v[0]) ? v : "未填");
        bad += (v && v[0]) ? 1 : 0;
    }
    cg_free(v);

    v = cg_ini_read(path, "task", "overwrite", "");
    if      (equal_ci(v, "0")) cg_widget_set_checked(cg_find(app, "chk_overwrite"), 0);
    else if (equal_ci(v, "1")) cg_widget_set_checked(cg_find(app, "chk_overwrite"), 1);
    else {
        note(app, CG_TERM_SYS, "  overwrite = %s（非法：收 0 / 1），保持当前勾选。",
             (v && v[0]) ? v : "未填");
        bad += (v && v[0]) ? 1 : 0;
    }
    cg_free(v);

    note(app, CG_TERM_TX, "导入完成%s。下一步点「枚举文件」→「开始转换」（导入只填参数，不替你转换）。",
         bad ? "（有非法键已按当前值保持）" : "");
    return 0;
}

/**
 * @brief 「导入 INI 配置」按钮：按任务单框的路径导入；框空着就找目录下的 task.ini
 * @param app 应用
 * @return void 无返回值
 * @note 默认路径跟数据放一起（车间习惯：任务单和数据同一个目录），找「目录」框目录下的
 *       task.ini；目录也没填就明说，不瞎猜。导入成功顺手枚举一遍，接着就能看表
 */
static void import_clicked(cg_app_t *app)
{
    cg_widget_t *edt = cg_find(app, "edt_ini");
    const char  *p   = edt ? cg_widget_text(edt) : NULL;
    char        *fallback = NULL;
    int          rc;

    if (!p || !p[0]) {                           /* 任务单框空着：找「目录」下的 task.ini */
        const char *dir = cg_widget_text(cg_find(app, "edt_dir"));

        if (!dir || !dir[0]) {
            note(app, CG_TERM_SYS, "任务单路径空着，目录框也空着：先填一个（任务单跟数据放一起时，空着也行）。");
            return;
        }
        fallback = cg_path_join(dir, "task.ini");
        p = fallback;
        note(app, CG_TERM_SYS, "任务单框空着，默认找目录下的 task.ini：%s", p);
    }

    rc = import_task_ini(app, p);
    if (fallback) cg_free(fallback);
    if (rc == 0) scan_dir(app);                  /* 目录键若是新的，枚举表跟着刷新 */
}

/* ------------------------------------------------------------ 转换与落盘 */

/**
 * @brief 把第 idx 个文件读进来、按当前方向转换、统一换行、按需加 BOM，结果写进输出缓冲
 * @param app 应用：取方向 / 换行 / BOM 等界面状态，也用来往终端写原因
 * @param idx 枚举表里的下标，必须在 [0, s_count) 内
 * @param out_len 长度出口：成功时写进"写出的字节数"，失败时不动
 * @return int 成功返回写出的字节数（>= 0）、失败返回 -1 并把原因写进终端
 * @note 读进来先拷进静态输入缓冲并补 '\0'（转换接口要 '\0' 结尾的串）；源是 UTF-8 BOM 就先剥掉
 *       三字节再转；转换返回 NULL 当失败报出来（不产空文件）；换行统一放在转换之后做（只动字节、
 *       不碰字符集），CRLF 与孤立的 CR 都归成所选风格；BOM 只在 GBK→UTF-8 且勾选时写，
 *       顺序 BOM → 正文；输出超过上限直接报错，绝不截断
 */
static int build_output(cg_app_t *app, int idx, int *out_len)
{
    const enc_file_t *f;
    unsigned char    *raw;
    char             *full;
    char             *conv;
    const char       *src;
    int               dir, nl, rd, n, i, k, o, cap;

    if (idx < 0 || idx >= s_count) return -1;
    f = &s_files[idx];

    if (f->size > ENCCONV_MAX_BYTES) {                     /* 只跳过，不截断 */
        note(app, CG_TERM_SYS, "  %s：%lld 字节，超过 %d 上限，跳过。",
             f->name, f->size, ENCCONV_MAX_BYTES);
        return -1;
    }

    full = cg_path_join(s_dir, f->name);                   /* 用完整路径去读 */
    if (!full) {
        note(app, CG_TERM_SYS, "  %s：路径拼不出来，跳过。", f->name);
        return -1;
    }
    rd  = 0;
    raw = cg_file_read_bytes(full, &rd);                   /* 不去 BOM，自己剥 */
    cg_free(full);
    if (!raw) {
        note(app, CG_TERM_SYS, "  %s：读不到（文件被删 / 没权限）。", f->name);
        return -1;
    }
    if (rd > (int)sizeof s_in - 1) {                       /* 枚举后又长大了，双保险 */
        cg_free(raw);
        note(app, CG_TERM_SYS, "  %s：%d 字节，超过读缓冲，跳过。", f->name, rd);
        return -1;
    }
    memcpy(s_in, raw, (size_t)rd);                         /* 拷走 + 补 '\0' */
    s_in[rd] = '\0';
    cg_free(raw);

    dir = cg_widget_value(cg_find(app, "cbo_dir"));
    nl  = cg_widget_value(cg_find(app, "cbo_nl"));

    src = (const char *)s_in;
    if (f->verdict == ENC_V_UTF8_BOM) src = (const char *)s_in + 3;   /* 转换前先把 BOM 剥掉 */

    conv = (dir == 0) ? cg_gbk_to_utf8(src) : cg_utf8_to_gbk(src);
    if (!conv) {                                           /* NULL 当失败报出来 */
        note(app, CG_TERM_SYS, "  %s：%s 转换失败（源里有不合法的字节）。",
             f->name, (dir == 0) ? "GBK→UTF-8" : "UTF-8→GBK");
        return -1;
    }

    n   = (int)strlen(conv);
    o   = 0;
    cap = ENCCONV_OUT_MAX;

    /* BOM 只在 GBK→UTF-8 且勾了「加 UTF-8 BOM」时写，顺序 BOM → 正文 */
    if (dir == 0 && cg_widget_checked(cg_find(app, "chk_bom"))) {
        if (cap - o < 3) {
            cg_free(conv);
            note(app, CG_TERM_SYS, "  %s：输出超过 %d 字节上限，放不下（不截断）。",
                 f->name, ENCCONV_OUT_MAX);
            return -1;
        }
        s_out[o++] = (unsigned char)0xEF;
        s_out[o++] = (unsigned char)0xBB;
        s_out[o++] = (unsigned char)0xBF;
    }

    /* 换行统一在转换之后做：CRLF 与孤立的 CR 都归成所选风格 */
    for (i = 0; i < n; i++) {
        const char *em;
        int         el;

        if (conv[i] == '\r') {
            int crlf = (i + 1 < n && conv[i + 1] == '\n');

            if (crlf) i++;
            if (nl == ENC_NL_CRLF)    { em = "\r\n"; el = 2; }
            else if (nl == ENC_NL_LF) { em = "\n";   el = 1; }
            else                      { em = crlf ? "\r\n" : "\r"; el = crlf ? 2 : 1; }
        } else if (conv[i] == '\n') {
            if (nl == ENC_NL_CRLF)    { em = "\r\n"; el = 2; }
            else                      { em = "\n";   el = 1; }
        } else {
            em = conv + i;
            el = 1;
        }

        if (o + el > cap) {                                /* 放不下就报，不截断 */
            cg_free(conv);
            note(app, CG_TERM_SYS, "  %s：输出超过 %d 字节上限，放不下（不截断）。",
                 f->name, ENCCONV_OUT_MAX);
            return -1;
        }
        for (k = 0; k < el; k++) s_out[o++] = (unsigned char)em[k];
    }

    cg_free(conv);
    if (out_len) *out_len = o;
    return o;
}

/**
 * @brief 另存时的新文件名：换掉原扩展名再加 .utf8.txt / .gbk.txt
 * @param name 原文件名（相对目录的名字）
 * @param dir 0 = GBK→UTF-8、1 = UTF-8→GBK
 * @return char * 堆串（cg_str_format 出的），调用方 cg_free；失败返回 NULL
 * @note 早先是直接追加，于是得到 `参数表.txt.utf8.txt` 这种双后缀 —— 观感差、也没必要；
 *       现在换成 `参数表.utf8.txt`。没有扩展名（或以点开头的隐藏名）就退化成追加
 */
static char *out_name(const char *name, int dir)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    const char *suf = (dir == 1) ? ".gbk.txt" : ".utf8.txt";

    if (!name) return NULL;
    if (dot && dot != name) return cg_str_format("%.*s%s", (int)(dot - name), name, suf);
    return cg_str_format("%s%s", name, suf);
}

/**
 * @brief 遍历枚举表，把「按当前方向算候选」的文件逐个转换并落盘，边做边报进度
 * @param app 应用
 * @return void 无返回值
 * @note 默认另存新文件（换掉扩展名再加 .utf8.txt / .gbk.txt：参数表.txt → 参数表.utf8.txt）；
 *       勾了「覆盖原文件」要两步确认，第一次点击只提示，第二次才真写。
 *       本框架**没有改名接口**，所以覆盖就是直接写原名
 */
static void convert_all(cg_app_t *app)
{
    cg_widget_t *prg = cg_find(app, "prg_main");
    int          dir, nl, overwrite, i;

    if (s_count <= 0) {
        note(app, CG_TERM_SYS, "还没有枚举到文件，先点「枚举文件」。");
        return;
    }

    dir       = cg_widget_value(cg_find(app, "cbo_dir"));
    nl        = cg_widget_value(cg_find(app, "cbo_nl"));
    overwrite = cg_widget_checked(cg_find(app, "chk_overwrite"));

    s_done = 0;
    s_skip = 0;
    s_fail = 0;

    if (dir == 1 && cg_widget_checked(cg_find(app, "chk_bom"))) {
        note(app, CG_TERM_SYS, "提示：UTF-8→GBK 方向不写 BOM，「写 UTF-8 BOM」已忽略。");
    }

    if (overwrite) {
        if (!s_confirmed) {                                /* 第一步：只确认 */
            note(app, CG_TERM_SYS,
                 "「覆盖原文件」会改写原文件、不可撤销；本框架没有改名接口，覆盖就是直接写原名。");
            note(app, CG_TERM_SYS, "再点一次「开始转换」才开始真覆盖。");
            s_confirmed = 1;
            return;
        }
        s_confirmed = 0;                                   /* 真覆盖了，下次重新确认 */
    } else {
        s_confirmed = 0;
    }

    note(app, CG_TERM_TX, "开始转换：%d 个文件，方向 %s，换行 %s%s。",
         s_count, dir == 0 ? "GBK→UTF-8" : "UTF-8→GBK",
         nl == ENC_NL_CRLF ? "统一 CRLF" : (nl == ENC_NL_LF ? "统一 LF" : "保持原样"),
         overwrite ? "，覆盖原文件" : "，另存新文件");

    if (prg) cg_widget_set_value(prg, 0);

    for (i = 0; i < s_count; i++) {
        enc_file_t *f = &s_files[i];
        int         outn = 0;

        if (prg) cg_widget_set_value(prg, (i * 100) / s_count);  /* 按文件个数进步 */

        /* 注意：方向下拉的值 0 就是「GBK→UTF-8」，而 is_candidate 第二个参数是**布尔**语义
         * （非 0 = GBK→UTF-8），所以这里必须写 dir == 0。直接传 dir 会把候选判反 ——
         * 2026-09-28 真机实测踩过：已经是 UTF-8 的文件全被转了一遍（产出双重编码的垃圾），
         * 真正要转的 GBK 文件反而被跳过。索引与布尔长得像，但语义差一个整数 */
        if (!is_candidate(f->verdict, dir == 0)) {
            s_skip++;
            note(app, CG_TERM_SYS, "%s：按当前方向不是候选，跳过。", f->name);
            continue;
        }

        if (build_output(app, i, &outn) < 0) {             /* 原因已由 build_output 写清 */
            s_fail++;
            continue;
        }

        if (overwrite) {
            char *src = cg_path_join(s_dir, f->name);

            if (!src) {
                s_fail++;
                note(app, CG_TERM_SYS, "%s：路径拼不出来，跳过。", f->name);
                continue;
            }
            if (cg_file_write_bytes(src, s_out, outn) != 0) {
                s_fail++;
                note(app, CG_TERM_SYS, "%s → %s / 输入 %lld 字节 → 输出 %d 字节 / 写盘失败。",
                     f->name, f->name, f->size, outn);
            } else {
                s_done++;
                note(app, CG_TERM_TX, "%s → %s / 输入 %lld 字节 → 输出 %d 字节 / 已覆盖。",
                     f->name, f->name, f->size, outn);
            }
            cg_free(src);
        } else {
            char *newname = out_name(f->name, dir);
            char *dst     = newname ? cg_path_join(s_dir, newname) : NULL;

            if (newname) cg_free(newname);
            if (!dst) {
                s_fail++;
                note(app, CG_TERM_SYS, "%s：输出名拼不出来，跳过。", f->name);
                continue;
            }
            if (cg_file_write_bytes(dst, s_out, outn) != 0) {
                s_fail++;
                note(app, CG_TERM_SYS, "%s → %s / 输入 %lld 字节 → 输出 %d 字节 / 写盘失败。",
                     f->name, cg_path_name(dst), f->size, outn);
            } else {
                s_done++;
                note(app, CG_TERM_TX, "%s → %s / 输入 %lld 字节 → 输出 %d 字节 / 已写出。",
                     f->name, cg_path_name(dst), f->size, outn);
            }
            cg_free(dst);
        }
    }

    if (prg) cg_widget_set_value(prg, 100);
    note(app, CG_TERM_TX, "完成：转换 %d、跳过 %d、失败 %d（共 %d）。",
         s_done, s_skip, s_fail, s_count);
    refresh_status(app);
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 按钮点击的总入口：靠控件 id 分辨点的是哪一个按钮
 * @param self 被点的按钮
 * @param ev 事件类型（这里总是单击）
 * @param user 业务数据，未用
 * @return void 无返回值
 * @note 「枚举 / 预览 / 转换」都有真实 I/O，自检不开窗时只写一行提示就返回
 */
static void on_btn_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);

    (void)ev;
    (void)user;
    if (!id) return;

    if (strcmp(id, "btn_datadir") == 0) {                  /* 纯填框，不读盘 */
        char *dir = cg_app_data_dir();

        if (dir) {
            cg_widget_set_text(cg_find(app, "edt_dir"), dir);
            note(app, CG_TERM_TX, "目录框已填成用户数据目录：%s", dir);
            cg_free(dir);
        } else {
            note(app, CG_TERM_SYS, "取用户数据目录失败。");
        }
        return;
    }

    if (!cg_app_has_window(app)) {                         /* 自检不开窗：不发真 I/O */
        note(app, CG_TERM_SYS, "自检模式：跳过「%s」（不真读盘）。",
             strcmp(id, "btn_scan") == 0     ? "枚举"
           : strcmp(id, "btn_preview") == 0  ? "预览"
           : strcmp(id, "btn_samples") == 0  ? "造示例文件"
           : strcmp(id, "btn_import") == 0   ? "导入任务单" : "转换");
        return;
    }

    if (strcmp(id, "btn_samples") == 0) { make_samples(app);     return; }
    if (strcmp(id, "btn_scan") == 0)    { scan_dir(app);         return; }
    if (strcmp(id, "btn_preview") == 0) { preview_selected(app); return; }
    if (strcmp(id, "btn_import") == 0)  { import_clicked(app);   return; }
    if (strcmp(id, "btn_convert") == 0) { convert_all(app);      return; }
}

/**
 * @brief 表格双击：预览选中那行的文件开头几行
 * @param self 被双击的表格控件
 * @param ev 事件类型（这里总是双击）
 * @param user 业务数据，未用
 * @return void 无返回值
 */
static void on_preview(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    preview_selected(cg_app_of(self));
}

/**
 * @brief 关窗前记一行日志：枚举了多少个、转了多少
 * @param self 窗口伪控件
 * @param ev 事件类型（关闭）
 * @param user 业务数据，未用
 * @return void 无返回值
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    cg_log(CG_LOG_INFO, "编码转换工具关闭：枚举 %d 个，转换 %d，跳过 %d，失败 %d。",
           s_count, s_done, s_skip, s_fail);
}

/**
 * @brief 界面就绪：只摆开场白（自检不开窗也会走到这里，所以绝不读盘、绝不写盘）
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 命令行点名了目录时，这里会枚举并**另存**转换一遍 —— 只在真窗口下做，
 *       而且一律"另存新文件"，绝不覆盖（覆盖必须人工两步确认）
 */
void ec_init(cg_app_t *app, void *user)
{
    (void)user;

    cg_status_set(cg_find(app, "sb_main"), 0, "还没枚举目录");
    cg_status_set(cg_find(app, "sb_main"), 1, "方向：GBK → UTF-8 · 换行：保持原样 · BOM：不写");
    refresh_status(app);

    note(app, CG_TERM_SYS, "编码与换行转换器就绪。单文件上限 8 MB、一次最多 200 个；超限一律跳过，不截断。");
    note(app, CG_TERM_SYS, "判定只是猜测：先按严格 UTF-8 校验，不合法才判「疑似 GBK」——纯 ASCII 谁都合法。");
    note(app, CG_TERM_SYS, "默认另存新文件（原名 + .utf8.txt / .gbk.txt）；覆盖原文件需要在界面上两步确认。");
    note(app, CG_TERM_SYS, "转换参数可写成 INI 任务单（[task] 节）再「导入 INI 配置」，或命令行直接给 task.ini。");

    if (s_boot_dir[0] && cg_app_has_window(app)) {
        note(app, CG_TERM_TX, "命令行点名了目录，直接枚举并另存转换：%s", s_boot_dir);
        cg_widget_set_text(cg_find(app, "edt_dir"), s_boot_dir);
        scan_dir(app);
        convert_all(app);
    } else if (s_boot_ini[0] && cg_app_has_window(app)) {
        note(app, CG_TERM_TX, "命令行点名了任务单，导入参数后枚举并另存转换：%s", s_boot_ini);
        cg_widget_set_text(cg_find(app, "edt_ini"), s_boot_ini);
        if (import_task_ini(app, s_boot_ini) == 0) {
            scan_dir(app);
            convert_all(app);          /* 只另存：任务单里 overwrite=1 也只填勾，仍停在两步确认 */
        }
    }
}

/**
 * @brief 判断字符串是不是以某个后缀结尾（不区分 ASCII 大小写）
 * @param s 待检查的字符串；NULL 安全
 * @param suffix 后缀，如 ".cgui"
 * @return int 1 是、0 否
 * @note 自己按字节折大小写，不引 <ctype.h>：这里只是给命令行参数分个类
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

const cg_binding_t ec_bindings[] = {
    CG_BIND("ec_on_btn_click", on_btn_click),
    CG_BIND("ec_on_preview",   on_preview),
    CG_BIND("ec_on_close",     on_close),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径 / .ini 任务单 / 要处理的目录，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
