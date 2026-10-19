/**
 * @file    ec_internal.h
 * @brief   encconv 套件各子模块之间的共享接口（业务代码不应 include）
 * @details ec.c 曾是一个 1152 行的单体，现按职责拆成 ec_detect.c（编码判定与文本工具，
 *          纯逻辑零 UI 依赖）与 ec.c（UI 胶水：目录枚举 / 表格 / 预览 / 转换落盘 / 生命周期）。
 *          本头文件集中放两样东西：
 *            1. 编码判定的结果码与容量常量（各子模块共用）；
 *            2. 跨子模块调用的内部函数声明——**只给 encconv 这一页用**，
 *               不对外暴露、也不进 cstackgui.h。
 *          分层规矩：子模块之间只允许通过本头文件互相调用，不要 include 对方的 .c；
 *          业务代码只认 cstackgui.h。
 */
#ifndef EC_INTERNAL_H
#define EC_INTERNAL_H

#include "cstackgui.h"

/* ------------------------------------------------------------ 容量与判定结果码 */

/** 单文件整读上限（字节）：超过只"跳过"，不截断 */
#define ENCCONV_MAX_BYTES     (8 * 1024 * 1024)

/** 换行：保持原样 / 统一 CRLF / 统一 LF */
#define ENC_NL_KEEP           0
#define ENC_NL_CRLF           1
#define ENC_NL_LF             2

/** 编码判定结果（ec_detect.c 产出、ec.c 显示与筛选） */
#define ENC_V_UTF8            0   /**< 合法 UTF-8，无需转换 */
#define ENC_V_UTF8_BOM        1   /**< 带 UTF-8 BOM，本身即 UTF-8 */
#define ENC_V_GBK             2   /**< 不是合法 UTF-8，疑似 GBK/ANSI，候选转换 */
#define ENC_V_UTF16           3   /**< UTF-16 BOM，本工具不处理 */
#define ENC_V_BINARY          4   /**< 含 0x00，疑似二进制，跳过 */
#define ENC_V_TOO_BIG         5   /**< 超 8 MB 上限，跳过（不截断） */
#define ENC_V_READFAIL        6   /**< 读不到（不存在 / 被占用 / 是目录） */

/* ---------------- ec_detect.c：编码判定与文本工具 ---------------- */

/**
 * @brief 判定一个文件最可能的编码（整读一遍字节，按字节判 BOM / 0x00 / UTF-8 合法性）
 * @param path 文件全路径（UTF-8）
 * @return int 判定结果 ENC_V_*
 * @note 这是**猜测**不是识别：合法 UTF-8 也可能是恰好合法的 GBK 字节，纯 ASCII 谁都合法。
 */
int ec_classify(const char *path);

/**
 * @brief 文件名扩展名要不要收（只收文本类，避免把子目录 / 无后缀文件带进来）
 * @param name 文件名（不含路径）
 * @return int 1 收、0 不收
 */
int ec_ext_ok(const char *name);

/**
 * @brief 把字节数写成人看的 KB / MB（负数写破折号）
 * @param bytes 字节数
 * @param out 输出缓冲，写进 '\0' 结尾的文本
 * @param cap out 的字节数
 * @return void 无返回值
 */
void ec_human_size(long long bytes, char *out, int cap);

/**
 * @brief 判定结果转成显示的短标签或「结论」长句
 * @param v 判定结果 ENC_V_*
 * @param brief 非 0 返回短标签（表格「判定」列），0 返回长结论（表格「结论」列）
 * @return const char * 静态字符串，别 free
 */
const char *ec_vtext(int v, int brief);

/**
 * @brief 按当前方向，判一个判定结果是不是本次要转换的候选
 * @param v 判定结果 ENC_V_*
 * @param gbk2utf8 非 0 = GBK → UTF-8；0 = UTF-8 → GBK
 * @return int 1 是候选、0 不是
 */
int ec_is_candidate(int v, int gbk2utf8);

/**
 * @brief 算出前 maxlines 行（含行尾换行）一共多少字节，用来截预览
 * @param s 文本
 * @param len s 的字节数
 * @param maxlines 最多取几行
 * @return int 截断长度（字节）
 */
int ec_first_lines_len(const char *s, int len, int maxlines);

#endif
