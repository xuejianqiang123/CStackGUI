/* cg_json.c -- 极简 JSON DOM 解析器实现 */
#include "cg_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 工具 */

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

static cj_val_t *parse_value(parser_t *ps);

static cj_val_t *val_new(cj_type_t t)
{
    cj_val_t *v = (cj_val_t *)calloc(1, sizeof(cj_val_t));
    if (v) v->type = t;
    return v;
}

static void skip_ws(parser_t *ps)
{
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == '\n') { ps->line++; ps->p++; }
        else if (c == ' ' || c == '\t' || c == '\r') { ps->p++; }
        else break;
    }
}

static int fail(parser_t *ps, const char *msg)
{
    if (!ps->failed) {
        ps->failed = 1;
        cj_err(ps->errbuf, ps->errsz, msg, ps->line);
    }
    return 0;
}

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

/* 把 \uXXXX（含代理对）编成 UTF-8 追加到 buf */
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

static void push_item(cj_val_t *container, char *key, cj_val_t *item)
{
    size_t n = container->count;
    cj_val_t **ni = (cj_val_t **)realloc(container->items, (n + 1) * sizeof(cj_val_t *));
    if (!ni) return;
    container->items = ni;
    container->items[n] = item;
    if (container->type == CJ_OBJ) {
        char **nk = (char **)realloc(container->keys, (n + 1) * sizeof(char *));
        if (!nk) return;
        container->keys = nk;
        container->keys[n] = key;
    }
    container->count = n + 1;
}

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
        push_item(obj, key, val);
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
        fail(ps, "expected ',' or '}'");
        cj_free(obj);
        return NULL;
    }
}

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
        push_item(arr, NULL, val);
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
        fail(ps, "expected ',' or ']'");
        cj_free(arr);
        return NULL;
    }
}

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

static int lit(parser_t *ps, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) >= n && strncmp(ps->p, word, n) == 0) {
        ps->p += n;
        return 1;
    }
    return 0;
}

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

/* ------------------------------------------------------------ 公共入口 */

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

const cj_val_t *cj_get(const cj_val_t *obj, const char *key)
{
    if (!obj || obj->type != CJ_OBJ) return NULL;
    for (size_t i = 0; i < obj->count; i++) {
        if (obj->keys && obj->keys[i] && strcmp(obj->keys[i], key) == 0) return obj->items[i];
    }
    return NULL;
}

const char *cj_str(const cj_val_t *v, const char *def)
{
    if (!v) return def;
    if (v->type == CJ_STR) return v->str ? v->str : "";
    return def;
}

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

int cj_int(const cj_val_t *v, int def)
{
    double d = cj_num(v, def);
    return (int)(d < 0 ? d - 0.5 : d + 0.5);
}

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
        unsigned a = 0;
        if (sscanf(s, "%2x%2x%2x%2x", &r, &g, &b, &a) == 4) return ((a << 24) | (r << 16) | (g << 8) | b);
    }
    return def;
}

size_t cj_count(const cj_val_t *v)
{
    return (v && (v->type == CJ_ARR || v->type == CJ_OBJ)) ? v->count : 0;
}

const cj_val_t *cj_at(const cj_val_t *arr, size_t i)
{
    if (!arr || arr->type != CJ_ARR || i >= arr->count) return NULL;
    return arr->items[i];
}
