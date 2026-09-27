/**
 * @file    cg_json.h
 * @brief   极简 JSON DOM 解析器（只读），专为 .cgui 设计
 * @details 支持：对象 / 数组 / 字符串（含 \uXXXX 与代理对）/ 数字 / true / false / null
 *          不支持：注释、尾随逗号（严格按 JSON 规范，方便用编辑器/工具生成）
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

/**
 * @brief 解析一段 JSON 文本
 * @param text JSON 文本（UTF-8，内部会复制出需要的字符串）
 * @param errbuf 失败时写错误信息的缓冲；可传 NULL（不关心原因）
 * @param errsz errbuf 的字节数
 * @return cj_val_t* 文档树的根，必须用 cj_free 释放；失败返回 NULL
 */
cj_val_t *cj_parse(const char *text, char *errbuf, size_t errsz);
/**
 * @brief 释放一整棵解析出来的文档树
 * @param v cj_parse 返回的根；NULL 安全
 * @return void 无返回值（递归释放节点、字符串与键）
 */
void      cj_free(cj_val_t *v);

/* 取值助手：类型不符一律返回默认值，调用方不用逐层判空 */
/**
 * @brief 取对象里某个键的值
 * @param obj 对象节点
 * @param key 键名
 * @return const cj_val_t* 值的节点；键不存在或 obj 不是对象返回 NULL
 */
const cj_val_t *cj_get(const cj_val_t *obj, const char *key);
/**
 * @brief 取字符串值
 * @param v 节点
 * @param def 类型不符时用它的顶上
 * @return const char* 节点内部的串，别 free；不是字符串返回 def
 */
const char     *cj_str(const cj_val_t *v, const char *def);
/**
 * @brief 取浮点值
 * @param v 节点
 * @param def 类型不符时返回它
 * @return double 数值；不是数字返回 def
 */
double          cj_num(const cj_val_t *v, double def);
/**
 * @brief 取整数值（四舍五入到 int）
 * @param v 节点
 * @param def 类型不符时返回它
 * @return int 数值；不是数字返回 def
 */
int             cj_int(const cj_val_t *v, int def);
/**
 * @brief 取布尔值
 * @param v 节点
 * @param def 类型不符时返回它
 * @return int 1 真 / 0 假；不是布尔返回 def
 */
int             cj_bool(const cj_val_t *v, int def);

/**
 * @brief 取颜色值
 * @param v 节点
 * @param def 类型不符时返回它
 * @return unsigned 0xRRGGBB；支持 "#RRGGBB" / "#RGB" / 十进制，以及 0xAARRGGBB 直传
 */
unsigned cj_color(const cj_val_t *v, unsigned def);

/**
 * @brief 取数组 / 对象的长度
 * @param v 节点
 * @return size_t 元素个数；不是数组 / 对象返回 0
 */
size_t cj_count(const cj_val_t *v);
/**
 * @brief 取数组 / 对象的第 i 个元素
 * @param arr 数组或对象节点
 * @param i 下标，0 起
 * @return const cj_val_t* 元素节点（对象时是第 i 个**值**）；越界返回 NULL
 */
const cj_val_t *cj_at(const cj_val_t *arr, size_t i);

/**
 * @brief 把值回序列化成紧凑 JSON 文本（无空白）
 * @param v 节点；NULL 时返回 "null" 的副本
 * @return char * malloc 串，调用方 free；失败返回 NULL
 */
char *cj_print_unformatted(const cj_val_t *v);

#endif /* CG_JSON_H */
