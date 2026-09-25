/**
 * @file    cg_json.c
 * @brief   极简 JSON DOM 解析器实现（接口见 cg_json.h）
 * @details .cgui 是我们自己生成的，只需要一个不报行号、不占坑位的只读解析器；
 *          值节点用数组存 children，对象额外存一份 keys 与 items 一一对应
 */
#include "cg_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 工具 */

/** @brief 写一条带行号的解析错误到 errbuf（可传 NULL 静默） */
static void cj_err(char *errbuf, size_t errsz, const char *msg, size_t line)
{
    if (errbuf && errsz) {
        snprintf(errbuf, errsz, "%s (line %lu)", msg, (unsigned long)line);
    }
}

/* ------------------------------------------------------------ 游标 */

typedef struct {
    const char *p;
    const char *end;
    char       *errbuf;
    size_t      errsz;
    size_t      line;
    int         failed;
} parser_t;

/** @brief 按首字符分派解析任意 JSON 值；失败返回 NULL */
static cj_val_t *parse_value(parser_t *ps);

/** @brief 新建一个指定类型的值节点；失败返回 NULL */
static cj_val_t *val_new(cj_type_t t)
{
    cj_val_t *v = (cj_val_t *)calloc(1, sizeof(cj_val_t));
    if (v) v->type = t;
    return v;
}

/** @brief 跳过空白（换行要计数，报错时能给出行号） */
static void skip_ws(parser_t *ps)
{
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == '\n') { ps->line++; ps->p++; }
        else if (c == ' ' || c == '\t' || c == '\r') { ps->p++; }
        else break;
    }
}

/** @brief 记录第一次解析错误并返回 0（失败后不再覆盖，方便定位首个错误） */
static int fail(parser_t *ps, const char *msg)
{
    if (!ps->failed) {
        ps->failed = 1;
        cj_err(ps->errbuf, ps->errsz, msg, ps->line);
    }
    return 0;
}

/** @brief 保证动态缓冲装得下 need 个字节（倍增扩容；失败保持原状） */
static void buf_ensure(char **buf, size_t *len, size_t need, size_t *cap)
{
    if (*len + need + 1 > *cap) {
        size_t nc = (*cap ? *cap : 32);
        char *nb;
        while (nc < *len + need + 1) nc *= 2;
        nb = (char *)realloc(*buf, nc);
        if (!nb) return;
        *buf = nb;
        *cap = nc;
    }
}

/** 原始字节（UTF-8 文本里的一个字节）直接追加 */
static void byte_append(char **buf, size_t *len, size_t *cap, unsigned char b)
{
    buf_ensure(buf, len, 1, cap);
    if (!*buf) return;
    (*buf)[(*len)++] = (char)b;
    (*buf)[*len] = '\0';
}

/** @brief 把 \uXXXX（含代理对）编成 UTF-8 追加到 buf */
static void utf8_append(char **buf, size_t *len, size_t *cap, unsigned cp)
{
    unsigned char out[4];
    size_t n;
    if (cp < 0x80) { out[0] = (unsigned char)cp; n = 1; }
    else if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        out[0] = (unsigned char)(0xF0 | (cp >> 18));
        out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    buf_ensure(buf, len, n, cap);
    if (!*buf) return;
    memcpy(*buf + *len, out, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/** @brief 解析 4 位十六进制；成功写 *out 并返回 1 */
static int hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/**
 * @brief 解析一个 JSON 字符串（处理转义与 \uXXXX 代理对）
 * @param ps 解析游标
 * @return char* UTF-8 堆串（空串可能返回 NULL，由调用方兜底）；解析失败返回 NULL
 */
static char *parse_string_raw(parser_t *ps)
{
    char   *buf = NULL;
    size_t  len = 0, cap = 0;

    if (ps->p >= ps->end || *ps->p != '"') { fail(ps, "expected string"); return NULL; }
    ps->p++;
    while (ps->p < ps->end) {
        char c = *ps->p++;
        if (c == '"') return buf;                    /* 空串时返回 NULL，由调用方兜底 */
        if (c == '\\') {
            if (ps->p >= ps->end) break;
            char e = *ps->p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case '/': c = '/';  break;
            case '"': c = '"';  break;
            case '\\': c = '\\'; break;
            case 'u': {
                unsigned cp;
                if (ps->end - ps->p < 4 || !hex4(ps->p, &cp)) { fail(ps, "bad \\u escape"); return buf; }
                ps->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && ps->end - ps->p >= 6 &&
                    ps->p[0] == '\\' && ps->p[1] == 'u') {
                    unsigned lo;
                    if (hex4(ps->p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ps->p += 6;
                    }
                }
                utf8_append(&buf, &len, &cap, cp);
                continue;
            }
            default: fail(ps, "bad escape"); return buf;
            }
            utf8_append(&buf, &len, &cap, (unsigned char)c);
            continue;
        } else if ((unsigned char)c < 0x20) {
            fail(ps, "control character in string");
            return buf;
        }
        /* 普通字符：.cgui 本身是 UTF-8 文本，字节原样搬运，不能当码点再编码 */
        byte_append(&buf, &len, &cap, (unsigned char)c);
    }
    fail(ps, "unterminated string");
    return buf;
}

/** 解析后保证返回可用指针（空串返回 ""） */
static char *take_string(parser_t *ps)
{
    char *s = parse_string_raw(ps);
    if (!s) {
        if (ps->failed) return NULL;
        s = (char *)malloc(1);
        if (s) s[0] = '\0';
    }
    return s;
}

/** @brief 给数组/对象容器追加一个值（对象还要存 key）；OOM 返回 0 并释放入参，
 *  让调用方把整次解析判失败，而不是"静默丢 item 却返回成功" */
static int push_item(cj_val_t *container, char *key, cj_val_t *item)
{
    size_t n = container->count;
    cj_val_t **ni = (cj_val_t **)realloc(container->items, (n + 1) * sizeof(cj_val_t *));
    if (!ni) { free(key); cj_free(item); return 0; }
    container->items = ni;
    container->items[n] = item;
    if (container->type == CJ_OBJ) {
        char **nk = (char **)realloc(container->keys, (n + 1) * sizeof(char *));
        if (!nk) {
            /* items 已扩出第 n 槽但 count 还没加，items[n] 不会被 cj_free 释放，
             * 这里自己收掉 item，key 也一并释放，避免泄漏。 */
            container->items[n] = NULL;
            cj_free(item);
            free(key);
            return 0;
        }
        container->keys = nk;
        container->keys[n] = key;
    }
    container->count = n + 1;
    return 1;
}

/** @brief 解析对象（"key": value, ...）；失败返回 NULL */
static cj_val_t *parse_object(parser_t *ps)
{
    cj_val_t *obj = val_new(CJ_OBJ);
    if (!obj) { fail(ps, "out of memory"); return NULL; }
    ps->p++; /* '{' */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        skip_ws(ps);
        char *key = take_string(ps);
        if (!key) { cj_free(obj); return NULL; }
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') { free(key); fail(ps, "expected ':'"); cj_free(obj); return NULL; }
        ps->p++;
        cj_val_t *val = parse_value(ps);
        if (!val) { free(key); cj_free(obj); return NULL; }
        if (!push_item(obj, key, val)) {
            cj_free(obj);
            fail(ps, "out of memory");
            return NULL;
        }
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
        fail(ps, "expected ',' or '}'");
        cj_free(obj);
        return NULL;
    }
}

/** @brief 解析数组（value, ...）；失败返回 NULL */
static cj_val_t *parse_array(parser_t *ps)
{
    cj_val_t *arr = val_new(CJ_ARR);
    if (!arr) { fail(ps, "out of memory"); return NULL; }
    ps->p++; /* '[' */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        cj_val_t *val = parse_value(ps);
        if (!val) { cj_free(arr); return NULL; }
        if (!push_item(arr, NULL, val)) {
            cj_free(arr);
            fail(ps, "out of memory");
            return NULL;
        }
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
        fail(ps, "expected ',' or ']'");
        cj_free(arr);
        return NULL;
    }
}

/** @brief 解析数字字面量（整型/浮点/指数），strtod 语义 */
static cj_val_t *parse_number(parser_t *ps)
{
    char  tmp[64];
    size_t n = 0;
    cj_val_t *v;
    while (ps->p < ps->end && n < sizeof(tmp) - 1) {
        char c = *ps->p;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
            c == 'e' || c == 'E') {
            tmp[n++] = c;
            ps->p++;
        } else break;
    }
    tmp[n] = '\0';
    v = val_new(CJ_NUM);
    if (!v) { fail(ps, "out of memory"); return NULL; }
    v->num = strtod(tmp, NULL);
    return v;
}

/** @brief 前缀匹配一个字面量（true/false/null），命中则推进游标 */
static int lit(parser_t *ps, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) >= n && strncmp(ps->p, word, n) == 0) {
        ps->p += n;
        return 1;
    }
    return 0;
}

/** @brief 按首字符分派解析任意 JSON 值；失败返回 NULL */
static cj_val_t *parse_value(parser_t *ps)
{
    skip_ws(ps);
    if (ps->p >= ps->end) { fail(ps, "unexpected end of input"); return NULL; }
    switch (*ps->p) {
    case '{': return parse_object(ps);
    case '[': return parse_array(ps);
    case '"': {
        char *s = take_string(ps);
        cj_val_t *v;
        if (!s) return NULL;
        v = val_new(CJ_STR);
        if (!v) { free(s); fail(ps, "out of memory"); return NULL; }
        v->str = s;
        return v;
    }
    case 't': case 'f': {
        cj_val_t *v = val_new(CJ_BOOL);
        if (!v) { fail(ps, "out of memory"); return NULL; }
        if (lit(ps, "true")) v->num = 1;
        else if (lit(ps, "false")) v->num = 0;
        else { cj_free(v); fail(ps, "bad literal"); return NULL; }
        return v;
    }
    case 'n': {
        cj_val_t *v;
        if (!lit(ps, "null")) { fail(ps, "bad literal"); return NULL; }
        v = val_new(CJ_NULL);
        return v;
    }
    default:
        if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) return parse_number(ps);
        fail(ps, "unexpected character");
        return NULL;
    }
}

/* ------------------------------------------------------------ 序列化（把解析后的对象重新变成字符串） */

static void raw_append(char **buf, size_t *len, size_t *cap, const char *s)
{
    while (*s) byte_append(buf, len, cap, (unsigned char)*s++);
}

static void emit_str(const cj_val_t *v, char **buf, size_t *len, size_t *cap)
{
    static const char hexd[] = "0123456789abcdef";
    const char *s = v->str ? v->str : "";
    byte_append(buf, len, cap, '"');
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, c); }
        else if (c == '\n') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, 'n'); }
        else if (c == '\t') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, 't'); }
        else if (c == '\r') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, 'r'); }
        else if (c == '\b') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, 'b'); }
        else if (c == '\f') { byte_append(buf, len, cap, '\\'); byte_append(buf, len, cap, 'f'); }
        else if (c < 0x20) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "\\u%c%c%c%c",
                     hexd[(c >> 4) & 0xF], hexd[c & 0xF], '0', '0');
            raw_append(buf, len, cap, tmp);
        } else byte_append(buf, len, cap, c);
    }
    byte_append(buf, len, cap, '"');
}

static void emit_value(const cj_val_t *v, char **buf, size_t *len, size_t *cap)
{
    char tmp[32];
    if (!v) { raw_append(buf, len, cap, "null"); return; }
    switch (v->type) {
    case CJ_NULL:  raw_append(buf, len, cap, "null"); break;
    case CJ_BOOL:  raw_append(buf, len, cap, v->num ? "true" : "false"); break;
    case CJ_NUM: {
        snprintf(tmp, sizeof(tmp), "%.17g", v->num);
        raw_append(buf, len, cap, tmp); break;
    }
    case CJ_STR:   emit_str(v, buf, len, cap); break;
    case CJ_ARR: {
        byte_append(buf, len, cap, '[');
        for (size_t i = 0; i < v->count; i++) {
            if (i) byte_append(buf, len, cap, ',');
            emit_value(v->items[i], buf, len, cap);
        }
        byte_append(buf, len, cap, ']'); break;
    }
    case CJ_OBJ: {
        byte_append(buf, len, cap, '{');
        for (size_t i = 0; i < v->count; i++) {
            if (i) byte_append(buf, len, cap, ',');
            cj_val_t keyv; memset(&keyv, 0, sizeof(keyv));
            keyv.type = CJ_STR; keyv.str = v->keys[i];
            emit_str(&keyv, buf, len, cap);
            byte_append(buf, len, cap, ':');
            emit_value(v->items[i], buf, len, cap);
        }
        byte_append(buf, len, cap, '}'); break;
    }
    }
}

char *cj_print_unformatted(const cj_val_t *v)
{
    char *buf = NULL; size_t len = 0, cap = 0;
    emit_value(v, &buf, &len, &cap);
    if (!buf) { buf = (char *)malloc(1); if (buf) buf[0] = '\0'; }
    return buf;
}

/* ------------------------------------------------------------ 公共入口 */

/**
 * @brief 解析一段 JSON 文本
 * @param text UTF-8 原文（自动跳过 BOM）
 * @param errbuf 解析失败时写错误信息，可传 NULL
 * @param errsz errbuf 容量
 * @return cj_val_t* 根节点（调用方 cj_free）；失败返回 NULL
 */
cj_val_t *cj_parse(const char *text, char *errbuf, size_t errsz)
{
    parser_t ps;
    cj_val_t *root;

    if (!text) { cj_err(errbuf, errsz, "empty input", 0); return NULL; }
    memset(&ps, 0, sizeof(ps));
    ps.p = text;
    ps.end = text + strlen(text);
    ps.errbuf = errbuf;
    ps.errsz = errsz;
    ps.line = 1;

    /* 跳过 UTF-8 BOM，编辑器保存的 .cgui 常带 */
    if ((size_t)(ps.end - ps.p) >= 3 &&
        (unsigned char)ps.p[0] == 0xEF && (unsigned char)ps.p[1] == 0xBB &&
        (unsigned char)ps.p[2] == 0xBF) {
        ps.p += 3;
    }

    root = parse_value(&ps);
    if (!root) return NULL;
    skip_ws(&ps);
    if (ps.p != ps.end) { cj_free(root); cj_err(errbuf, errsz, "trailing garbage", ps.line); return NULL; }
    return root;
}

/** @brief 递归释放值节点（含子项与 key） */
void cj_free(cj_val_t *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->count; i++) {
        if (v->items) cj_free(v->items[i]);
        if (v->keys && v->keys[i]) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->str);
    free(v);
}

/** @brief 取对象的子值；类型不对/键不存在返回 NULL */
const cj_val_t *cj_get(const cj_val_t *obj, const char *key)
{
    if (!obj || obj->type != CJ_OBJ) return NULL;
    for (size_t i = 0; i < obj->count; i++) {
        if (obj->keys && obj->keys[i] && strcmp(obj->keys[i], key) == 0) return obj->items[i];
    }
    return NULL;
}

/** @brief 取字符串；类型不符返回 def（调用方不用判空） */
const char *cj_str(const cj_val_t *v, const char *def)
{
    if (!v) return def;
    if (v->type == CJ_STR) return v->str ? v->str : "";
    return def;
}

/** @brief 取数字（BOOL 视为 0/1，字符串尝试 strtod）；类型不符返回 def */
double cj_num(const cj_val_t *v, double def)
{
    if (!v) return def;
    if (v->type == CJ_NUM || v->type == CJ_BOOL) return v->num;
    if (v->type == CJ_STR && v->str) {
        char *endp = NULL;
        double d = strtod(v->str, &endp);
        if (endp && endp != v->str) return d;
    }
    return def;
}

/** @brief 取整数（四舍五入）；类型不符返回 def */
int cj_int(const cj_val_t *v, int def)
{
    double d = cj_num(v, def);
    return (int)(d < 0 ? d - 0.5 : d + 0.5);
}

/** @brief 取布尔（还认字符串 "true"/"1"/"false"/"0"）；类型不符返回 def */
int cj_bool(const cj_val_t *v, int def)
{
    if (!v) return def;
    if (v->type == CJ_BOOL || v->type == CJ_NUM) return v->num != 0;
    if (v->type == CJ_STR && v->str) {
        if (!strcmp(v->str, "true") || !strcmp(v->str, "1")) return 1;
        if (!strcmp(v->str, "false") || !strcmp(v->str, "0")) return 0;
    }
    return def;
}

/**
 * @brief 从 "#RRGGBB" / "#RGB" / 十进制取颜色，支持 0xAARRGGBB 直传
 * @param v JSON 值；def 类型不符时的默认色
 * @return unsigned 0xRRGGBB（8 位写法返回 0xAARRGGBB 原样）；不符返回 def
 */
unsigned cj_color(const cj_val_t *v, unsigned def)
{
    if (!v) return def;
    if (v->type == CJ_NUM || v->type == CJ_BOOL) return (unsigned)((long long)v->num & 0xFFFFFFFFu);
    if (v->type != CJ_STR || !v->str) return def;

    const char *s = v->str;
    if (*s == '#') s++;
    size_t n = strlen(s);
    unsigned r = 0, g = 0, b = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return def;
    }
    if (n == 3) {
        /* 展开 #abc -> #aabbcc */
        unsigned v1 = 0;
        for (int i = 0; i < 3; i++) {
            char c = s[i];
            unsigned d = (c >= '0' && c <= '9') ? (unsigned)(c - '0')
                      : (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10)
                      : (unsigned)(c - 'A' + 10);
            v1 = (v1 << 8) | (d << 4) | d;
        }
        return v1 & 0xFFFFFFu;
    }
    if (n == 6) {
        if (sscanf(s, "%2x%2x%2x", &r, &g, &b) == 3) return ((r << 16) | (g << 8) | b) & 0xFFFFFFu;
    }
    if (n == 8) {
        /* 8 位写法是 #AARRGGBB（见 docs\CGUI-FORMAT.md 的 style.fg/bg）：
         * 第一对是透明度，不是红色分量 —— 顺序写反的话 rgba 会整体错位，
         * 而且不报错，只是颜色看起来不对（tests\unit\test_json.c 钉住了这个约定）。 */
        unsigned a = 0;
        if (sscanf(s, "%2x%2x%2x%2x", &a, &r, &g, &b) == 4) return ((a << 24) | (r << 16) | (g << 8) | b);
    }
    return def;
}

/**
 * @brief 数组/对象长度
 * @param v JSON 值
 * @return size_t 元素个数；其它类型返回 0
 */
size_t cj_count(const cj_val_t *v)
{
    return (v && (v->type == CJ_ARR || v->type == CJ_OBJ)) ? v->count : 0;
}

/** @brief 取数组第 i 个元素；越界/类型不对返回 NULL */
const cj_val_t *cj_at(const cj_val_t *arr, size_t i)
{
    if (!arr || arr->type != CJ_ARR || i >= arr->count) return NULL;
    return arr->items[i];
}
