/**
 * @file    cg_kit_internal.h
 * @brief   kit 层内部共享声明：拆分后跨文件复用的字符转换与 ASCII 小写化助手
 * @details kit 层原先集中在 cg_kit.c，2026-09-23 按子系统拆成
 *          cg_kit_fs.c / cg_kit_ini.c / cg_kit_str.c / cg_kit_sys.c 后，
 *          原本文件内的 static 助手被多个文件共用，只能提升到本头文件。
 *          这里只放 kit 内部的东西；公共 API 的声明一律在 cstackgui.h。
 */
#ifndef CG_KIT_INTERNAL_H
#define CG_KIT_INTERNAL_H

/* 与拆分前的 cg_kit.c 保持一致（各调用都显式走 Win32 的 W 系列 API） */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"

#include <windows.h>

/**
 * @brief UTF-8 字符串转成 UTF-16 堆串
 * @param s UTF-8 源串，可为 NULL
 * @return wchar_t* 新分配的宽串，调用方 free；s 为 NULL 或失败返回 NULL
 */
wchar_t *kit_w(const char *s);

/**
 * @brief UTF-16 字符串转成 UTF-8 堆串
 * @param ws UTF-16 源串，可为 NULL
 * @return char* 新分配的 UTF-8 串，调用方 free；ws 为 NULL 或失败返回 NULL
 */
char *kit_u8(const wchar_t *ws);

/**
 * @brief 把 ASCII 大写字母转小写（键名/节名的忽略大小写比较用）
 * @param c 待转换的字符
 * @return char 小写化后的字符；非 A-Z 原样返回
 */
char kit_lower(char c);

#endif /* CG_KIT_INTERNAL_H */
