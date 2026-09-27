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

/* ------------------------------------------------------------ 编码：HEX / Base64 / URL
 *
 * 来历：`examples\serialterm` 与 `examples\netterm` 里各有一份**逐字相同**的手写
 * `hex_parse()`（连注释都一样）—— "同一事实有两份"正是本工程最忌讳的；而且业务想自己拼
 * 一段十六进制还得再写一遍 `%02X` 循环。所以两个方向连同 Base64 / URL 一起收进 kit，
 * `cg_hex_decode` 的语义与那两段手写实现**逐字对齐**，它们可以直接换过来。
 *
 * 形态：**调用方给缓冲**（与 cg_open_file_dialog / cg_serial_read 同一口径），
 * 不在这里 malloc。返回值统一是"写出的长度"（不含结尾 '\0'），
 * 失败 -1（参数或内容非法）、-2（解码类缓冲放不下）。
 */

static const char k_b64_tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** @brief 十六进制字符 -> 值；不是十六进制字符返回 -1 */
static int kit_hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 字节 -> 十六进制文本（sep 为 NULL/"" 时不加分隔） */
int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size)
{
    const unsigned char *p = (const unsigned char *)data;
    const char          *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int                  seplen = sep ? (int)strlen(sep) : 0;
    int                  per, i, n = 0;

    if ((!data && len > 0) || !out || out_size <= 0 || len < 0) return -1;
    per = 2 + seplen;
    /* 先做除法判断，避免 len*per 在超大 len 上溢出 */
    if (len > (out_size - 1) / per) return -1;
    for (i = 0; i < len; i++) {
        if (i > 0 && seplen > 0) {
            memcpy(out + n, sep, (size_t)seplen);
            n += seplen;
        }
        out[n++] = digits[p[i] >> 4];
        out[n++] = digits[p[i] & 0x0F];
    }
    out[n] = '\0';
    return n;
}

/** @brief 十六进制文本 -> 字节（分隔符随便写，大小写都认） */
int cg_hex_decode(const char *text, void *out, int out_size)
{
    unsigned char *o = (unsigned char *)out;
    int            hi = -1, n = 0;

    if (!out || out_size < 0) return -1;
    for (; text && *text; text++) {
        char c = *text;
        int  v;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '-') continue;
        v = kit_hex_val(c);
        if (v < 0) return -1;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= out_size) return -2;
            o[n++] = (unsigned char)((hi << 4) | v);
            hi = -1;
        }
    }
    return (hi >= 0) ? -1 : n;   /* 只剩半个字节 = 写错了 */
}

/** @brief Base64 编码（RFC 4648，带 '=' 填充） */
int cg_base64_encode(const void *data, int len, char *out, int out_size)
{
    const unsigned char *p = (const unsigned char *)data;
    int                  i, n = 0;

    if ((!data && len > 0) || !out || out_size <= 0 || len < 0) return -1;
    if (len > (out_size - 1) / 4 * 3) return -1;   /* 同上：先除掉再乘，防溢出 */

    for (i = 0; i + 2 < len; i += 3) {
        unsigned v = ((unsigned)p[i] << 16) | ((unsigned)p[i + 1] << 8) | (unsigned)p[i + 2];

        out[n++] = k_b64_tab[(v >> 18) & 0x3F];
        out[n++] = k_b64_tab[(v >> 12) & 0x3F];
        out[n++] = k_b64_tab[(v >> 6) & 0x3F];
        out[n++] = k_b64_tab[v & 0x3F];
    }
    if (i < len) {                                  /* 收尾 1 或 2 个字节 */
        unsigned v = (unsigned)p[i] << 16;

        if (i + 1 < len) v |= (unsigned)p[i + 1] << 8;
        out[n++] = k_b64_tab[(v >> 18) & 0x3F];
        out[n++] = k_b64_tab[(v >> 12) & 0x3F];
        out[n++] = (i + 1 < len) ? k_b64_tab[(v >> 6) & 0x3F] : '=';
        out[n++] = '=';
    }
    out[n] = '\0';
    return n;
}

/** @brief Base64 解码（带不带 '=' 填充都认，空白与换行忽略） */
int cg_base64_decode(const char *text, void *out, int out_size)
{
    unsigned char *o = (unsigned char *)out;
    int            n = 0, acc = 0, nbits = 0, pad = 0, chars = 0;

    if (!text || !out || out_size < 0) return -1;
    for (; *text; text++) {
        char c = *text;
        int  v = -1;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') {
            pad++;
            if (pad > 2) return -1;
            continue;
        }
        if (pad > 0) return -1;                     /* '=' 后面不许再有内容 */
        if (c >= 'A' && c <= 'Z')      v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+')             v = 62;
        else if (c == '/')             v = 63;
        if (v < 0) return -1;
        chars++;
        acc = (acc << 6) | v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (n >= out_size) return -2;
            o[n++] = (unsigned char)((acc >> nbits) & 0xFF);
        }
    }

    /* 合法性：填充时总长必须是 4 的倍数；不填充时不许出现 4n+1 这种不可能的长度；
     * 尾部攒下的位必须是 0（否则是被截断/改过的串，不是"多余的填充"）。 */
    if (pad > 0) {
        if ((chars + pad) % 4 != 0) return -1;
    } else if (chars % 4 == 1) {
        return -1;
    }
    if (nbits > 0 && (acc & ((1 << nbits) - 1)) != 0) return -1;
    return n;
}

/** @brief URL 编码（百分号；space_plus 非 0 时空格编成 '+'） */
int cg_url_encode(const char *text, int space_plus, char *out, int out_size)
{
    static const char k_hexu[] = "0123456789ABCDEF";
    int               n = 0;

    if (!text || !out || out_size <= 0) return -1;
    if ((int)strlen(text) > (out_size - 1) / 3) return -1;   /* 最坏每个字节 3 个字符 */
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;                    /* 不编码集 = RFC 3986 的 unreserved */
        } else if (c == ' ' && space_plus) {
            out[n++] = '+';
        } else {
            out[n++] = '%';
            out[n++] = k_hexu[c >> 4];
            out[n++] = k_hexu[c & 0x0F];
        }
    }
    out[n] = '\0';
    return n;
}

/** @brief URL 解码（'%XX' 与 '+'） */
int cg_url_decode(const char *text, char *out, int out_size)
{
    int n = 0;

    if (!text || !out || out_size <= 0) return -1;
    for (; *text; text++) {
        int v;

        if (*text == '+') {
            v = ' ';
        } else if (*text == '%') {
            int h1 = kit_hex_val(text[1]);         /* text[1] 是 '\0' 时返回 -1，天然挡住越界 */
            int h2 = kit_hex_val(text[2]);

            if (h1 < 0 || h2 < 0) return -1;
            v = (h1 << 4) | h2;
            text += 2;
        } else {
            v = (unsigned char)*text;              /* 按字节原样透传（≥0x80 的中文照样对） */
        }
        if (n >= out_size - 1) return -2;          /* 留一个位置给结尾的 0 */
        out[n++] = (char)v;
    }
    out[n] = '\0';
    return n;
}

/* ------------------------------------------------------------ 进制转换与补码
 *
 * 来历：精易模块的 `进制_` 一族（14 条）不只是"2/8/16 转十进制"，还有
 * `进制_十六进制转有符号的十进制` 与 `进制_有符号的十进制转十六进制` —— 后者正是嵌入式与
 * 工控的日常：报文里给你的是 `0xFFF6`，而协议说的是"12 位有符号"，它其实是 -10。
 * 所以这一组不是"进制换算"，是"寄存器值怎么在人眼前和帧里来回表示"。
 *
 * 输出一律大写（数据手册、寄存器表都这么写）；要小写十六进制用 cg_hex_encode()。
 */

/** @brief 整数 -> 任意进制文本（2..36），负数带前导 '-' */
int cg_int_to_base(long long v, int base, char *out, int out_size)
{
    static const char   k_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char                tmp[72];
    unsigned long long  u;
    int                 n = 0, neg = 0, i;

    if (!out || out_size <= 0 || base < 2 || base > 36) return -1;
    if (v < 0) {
        neg = 1;
        u = 0ULL - (unsigned long long)v;   /* 这样写才能正确取到 LLONG_MIN 的绝对值 */
    } else {
        u = (unsigned long long)v;
    }
    do {
        tmp[n++] = k_digits[u % (unsigned)base];
        u /= (unsigned)base;
    } while (u && n < (int)sizeof(tmp));
    if (neg) tmp[n++] = '-';
    if (n + 1 > out_size) return -1;
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];   /* 上面是倒着攒的，这里翻过来 */
    out[n] = '\0';
    return n;
}

/** @brief 任意进制文本 -> 整数（整串必须合法，溢出算失败） */
int cg_base_to_int(const char *s, int base, long long *out)
{
    unsigned long long acc = 0;
    int                neg = 0, any = 0;

    if (!s || base < 2 || base > 36) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    /* 与进制相符的 0x / 0b / 0o 前缀：调试工具里到处是 `0x1A` 这种写法，不该让用户自己剪 */
    if (s[0] == '0' && s[1]) {
        int c = s[1] | 0x20;
        if ((c == 'x' && base == 16) || (c == 'b' && base == 2) || (c == 'o' && base == 8)) s += 2;
    }
    for (; *s; s++) {
        int c = (unsigned char)*s;
        int d;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            const char *t = s;                        /* 只允许**尾部**空白： */
            while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') t++;
            if (*t) return 0;                         /* "1A 2B" 是两段，不拼成一段 */
            break;
        }
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else return 0;
        if (d >= base) return 0;
        if (acc > (0xFFFFFFFFFFFFFFFFULL - (unsigned)d) / (unsigned)base) return 0;   /* 溢出 */
        acc = acc * (unsigned)base + (unsigned)d;
        any = 1;
    }
    if (!any) return 0;                               /* 空串 / 只有符号 */
    if (neg) {
        if (acc > 0x8000000000000000ULL) return 0;    /* 比 LLONG_MIN 还小 */
        acc = 0ULL - acc;
    } else if (acc > 0x7FFFFFFFFFFFFFFFULL) {
        return 0;
    }
    if (out) *out = (long long)acc;
    return 1;
}

/** @brief 进制互转（字符串进、字符串出） */
int cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size)
{
    long long v;

    if (!out || out_size <= 0) return -1;
    out[0] = '\0';
    if (!cg_base_to_int(s, from_base, &v)) return -1;
    return cg_int_to_base(v, to_base, out, out_size);
}

/** @brief 把低 bits 位按补码解释成有符号数 */
long long cg_to_signed(unsigned long long v, int bits)
{
    unsigned long long mask, sign;

    if (bits <= 0 || bits >= 64) return (long long)v;
    mask = (1ULL << bits) - 1;
    sign = 1ULL << (bits - 1);
    v &= mask;
    return (long long)((v & sign) ? (v | ~mask) : v);
}

/** @brief 把有符号数写成 bits 位补码（只保留低 bits 位） */
unsigned long long cg_from_signed(long long v, int bits)
{
    if (bits <= 0 || bits >= 64) return (unsigned long long)v;
    return (unsigned long long)v & ((1ULL << bits) - 1);
}
