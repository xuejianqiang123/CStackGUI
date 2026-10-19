/**
 * @file    cl_internal.h
 * @brief   calculator 套件各子模块之间的共享接口（业务代码不应 include）
 * @details cl.c 曾是一个 1192 行的单体，现按职责拆成 cl_expr.c（递归下降表达式求值，
 *          纯逻辑零 UI 依赖）与 cl.c（UI 胶水：控件读写 / do_* / on_* / 生命周期）。
 *          本头文件集中放两样东西：
 *            1. 模块级的定长缓冲与安全上限常量（各子模块共用）；
 *            2. 跨子模块调用的内部函数声明——**只给 calculator 这一页用**，
 *               不对外暴露、也不进 cstackgui.h。
 *          分层规矩：子模块之间只允许通过本头文件互相调用，不要 include 对方的 .c；
 *          业务代码只认 cstackgui.h。
 */
#ifndef CL_INTERNAL_H
#define CL_INTERNAL_H

#include "cstackgui.h"

/* ------------------------------------------------------------ 常量与安全上限 */

/** 表达式长度上限（字节） */
#define CA_EXPR_MAX  512
/** 解析递归深度上限 */
#define CA_DEPTH_MAX 16
/** 位运算段数上限 */
#define CA_SEG_MAX   16
/** 位运算单段位宽上限 */
#define CA_SEG_W     64
/** HEX / 二进制文本缓冲 */
#define CA_HEX_MAX   4096
/** 通用输出缓冲 */
#define CA_OUT_MAX   1024
/** 状态栏描述缓冲 */
#define CA_LAST      160

/* ---------------- cl_expr.c：递归下降表达式求值 ---------------- */

/**
 * @brief 求值一个表达式串（四则 / 括号 / 幂 / 位运算 / 常用函数）
 * @param expr 表达式（C 串）
 * @param out 结果出口
 * @param errbuf 错误串出口（成功时空串）
 * @param errcap errbuf 容量
 * @return int 1 = 成功；0 = 失败（errbuf 有原因）
 */
int ca_eval(const char *expr, double *out, char *errbuf, int errcap);

#endif
