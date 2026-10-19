/**
 * @file    ec_detect.c
 * @brief   encconv 的编码判定与文本工具（纯逻辑：UTF-8 校验 / 编码猜测 / 扩展名筛选 / 尺寸与标签格式化）
 * @details 从 ec.c（原 1152 行单体）按职责剥离出来：本文件零 UI 依赖，只吃路径/字节出判定，
 *          因此可单独读、单独推理。对外只导出 ec_classify / ec_ext_ok / ec_human_size /
 *          ec_vtext / ec_is_candidate / ec_first_lines_len（声明见 ec_internal.h），
 *          其余 is_valid_utf8 保持 static —— 编译单元隔离，合并进同一次链接不冲突。
 *
 *          判定口径：**判定编码不可靠，本工具不装准**——先按严格 UTF-8 校验：合法（或带 BOM）
 *          判「UTF-8，无需转换」；不合法 → 判「疑似 GBK/ANSI」作为候选。这会误判：纯 ASCII 谁都
 *          合法；GBK 内容偶尔恰好也是合法 UTF-8 序列。界面/日志/表格/结论列都把这个不确定性讲出来。
 */
#include "ec_internal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ UTF-8 校验 */

/**
 * @brief 严格校验一段字节是不是合法 UTF-8（运行时没有这个接口，本文件自写）
 * @param s 待检查的字节
 * @param len s 的字节数（不以 '\0' 为界，按长度走；0x00 被视为合法 ASCII 字节）
 * @return int 1 = 整段都是合法 UTF-8；0 = 出现非法序列
 * @note 规则（RFC 3629）：1 字节 0x00..0x7F；多字节首字节 0xC2..0xF4，其后每字节必须落在
 *       0x80..0xBF；并拒绝过长编码与代理区（E0 的第二字节 ≥ 0xA0、ED 的 ≤ 0x9F、
 *       F0 的 ≥ 0x90、F4 的 ≤ 0x8F），把范围卡在 ≤ U+10FFFF。
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

/* ------------------------------------------------------------ 判定与文本工具 */

/**
 * @brief 判定一个文件最可能的编码（整读一遍字节，按字节判 BOM / 0x00 / UTF-8 合法性）
 * @param path 文件全路径（UTF-8）
 * @return int 判定结果 ENC_V_*
 * @note 这是**猜测**不是识别：合法 UTF-8 也可能是恰好合法的 GBK 字节，纯 ASCII 谁都合法。
 *       含 0x00 的按二进制跳过（否则字符串式转换会在第一个 0 处被截断）。
 */
int ec_classify(const char *path)
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
int ec_ext_ok(const char *name)
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
void ec_human_size(long long bytes, char *out, int cap)
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
const char *ec_vtext(int v, int brief)
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
int ec_is_candidate(int v, int gbk2utf8)
{
    return gbk2utf8 ? (v == ENC_V_GBK) : (v == ENC_V_UTF8 || v == ENC_V_UTF8_BOM);
}

/**
 * @brief 算出前 maxlines 行（含行尾换行）一共多少字节，用来截预览
 * @param s 文本
 * @param len s 的字节数
 * @param maxlines 最多取几行
 * @return int 截断长度（字节）
 */
int ec_first_lines_len(const char *s, int len, int maxlines)
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
