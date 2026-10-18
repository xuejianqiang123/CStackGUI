/**
 * @file    pl.h
 * @brief   palette 套件模块接口
 */
#ifndef SUITE_PL_H
#define SUITE_PL_H

#include "cstackgui.h"

/** palette 的事件绑定表（表尾 CG_BIND_END）；事件名带 pl_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t pl_bindings[];

/**
 * @brief palette 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void pl_init(cg_app_t *app, void *user);

#endif
