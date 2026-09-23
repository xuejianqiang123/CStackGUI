/**
 * @file    cg_kit_str.c
 * @brief   kit 文本与编码：格式化、去空白、忽略大小写比较、替换、UTF-8<->GBK
 * @details 从 cg_kit.c 按子系统纯搬移拆出（公共 API 未变）。原 cg_kit.c 顶部的
 *          UTF-8/UTF-16 转换与 ASCII 小写化助手被 fs/ini/sys 多个文件共用，故提升为
 *          本文件的非 static 函数，声明见 cg_kit_internal.h。
 */
#include "cg_kit_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 内部助手 */

/** @brief UTF-8 -> UTF-16 堆串（调用方 free）；失败返回 NULL */
wchar_t *kit_w(const char *s)
{
    int      n;
    wchar_t *out;

    if (!s) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/** @brief UTF-16 -> UTF-8 堆串（调用方 free）；失败返回 NULL */
char *kit_u8(const wchar_t *ws)
{
    int   n;
    char *out;

    if (!ws) return NULL;
    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    out = (char *)malloc((size_t)n);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, n, NULL, NULL);
    return out;
}

/** @brief 忽略大小写的字符比较（只处理 ASCII，够用于键名/节名） */
char kit_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ------------------------------------------------------------ 文本与编码 */

/** @brief 拼字符串（printf 风格），返回堆内存 */
char *cg_str_format(const char *fmt, ...)
{
    va_list ap, ap2;
    int     n;
    char   *out;

    if (!fmt) return NULL;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return NULL; }
    out = (char *)malloc((size_t)n + 1);
    if (out) vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

/** @brief 去掉首尾空白，返回堆内存 */
char *cg_str_trim(const char *s)
{
    const char *b;
    const char *e;
    size_t      n;
    char       *out;

    if (!s) return NULL;
    b = s;
    while (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n') b++;
    e = b + strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    n = (size_t)(e - b);
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, b, n);
    out[n] = '\0';
    return out;
}

/** @brief 比较字符串（忽略英文大小写）：相等返回 1 */
int cg_str_equal(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (kit_lower(*a) != kit_lower(*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/** @brief 把 src 里所有 from 换成 to，返回堆内存 */
char *cg_str_replace(const char *src, const char *from, const char *to)
{
    size_t      fl, tl, sl, i, cap, len = 0;
    char       *out;
    const char *p;

    if (!src) return NULL;
    if (!from || !*from) return cg_str_dup(src);
    fl = strlen(from);
    tl = to ? strlen(to) : 0;
    sl = strlen(src);
    cap = sl * (tl > fl ? tl : 1) + 1;
    out = (char *)malloc(cap);
    if (!out) return NULL;
    for (i = 0; i < sl;) {
        if (i + fl <= sl && memcmp(src + i, from, fl) == 0) {
            if (len + tl + 1 > cap) break;
            memcpy(out + len, to ? to : "", tl);
            len += tl;
            i += fl;
        } else {
            if (len + 2 > cap) break;
            out[len++] = src[i++];
        }
    }
    out[len] = '\0';
    (void)p;
    return out;
}

/** @brief UTF-8 -> GBK */
char *cg_utf8_to_gbk(const char *utf8)
{
    wchar_t *w;
    int      n;
    char    *out;

    if (!utf8) return NULL;
    w = kit_w(utf8);
    if (!w) return NULL;
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) { free(w); return NULL; }
    out = (char *)malloc((size_t)n);
    if (out) WideCharToMultiByte(CP_ACP, 0, w, -1, out, n, NULL, NULL);
    free(w);
    return out;
}

/** @brief GBK -> UTF-8 */
char *cg_gbk_to_utf8(const char *gbk)
{
    wchar_t *w;
    int      n;
    char    *out;

    if (!gbk) return NULL;
    n = MultiByteToWideChar(CP_ACP, 0, gbk, -1, NULL, 0);
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_ACP, 0, gbk, -1, w, n);
    out = kit_u8(w);
    free(w);
    return out;
}
