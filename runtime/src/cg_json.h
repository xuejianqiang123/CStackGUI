/* cg_json.h -- 极简 JSON DOM 解析器（只读），专为 .cgui 设计。
 *
 * 支持：对象 / 数组 / 字符串（含 \uXXXX 与代理对）/ 数字 / true / false / null
 * 不支持：注释、尾随逗号（严格按 JSON 规范，方便用编辑器/工具生成）
 */
#ifndef CG_JSON_H
#define CG_JSON_H

#include <stddef.h>

typedef enum {
    CJ_NULL = 0,
    CJ_BOOL,
    CJ_NUM,
    CJ_STR,
    CJ_ARR,
    CJ_OBJ
} cj_type_t;

typedef struct cj_val {
    cj_type_t type;
    double    num;      /* CJ_NUM, CJ_BOOL(0/1) */
    char     *str;      /* CJ_STR */
    struct cj_val **items; /* CJ_ARR / CJ_OBJ 的值 */
    char    **keys;        /* CJ_OBJ 的键，与 items 一一对应 */
    size_t    count;       /* items 个数 */
} cj_val_t;

/** 解析一段 JSON 文本。失败返回 NULL 并写入 errbuf（可传 NULL）。
 *  成功返回的节点必须用 cj_free 释放。 */
cj_val_t *cj_parse(const char *text, char *errbuf, size_t errsz);
void      cj_free(cj_val_t *v);

/* 取值助手：类型不符一律返回默认值，调用方不用逐层判空 */
const cj_val_t *cj_get(const cj_val_t *obj, const char *key);
const char     *cj_str(const cj_val_t *v, const char *def);
double          cj_num(const cj_val_t *v, double def);
int             cj_int(const cj_val_t *v, int def);
int             cj_bool(const cj_val_t *v, int def);

/** 从 "#RRGGBB" / "#RGB" / 十进制 取颜色，支持 0xAARRGGBB 直传 */
unsigned cj_color(const cj_val_t *v, unsigned def);

/** 数组/对象长度 */
size_t cj_count(const cj_val_t *v);
const cj_val_t *cj_at(const cj_val_t *arr, size_t i);

#endif /* CG_JSON_H */
