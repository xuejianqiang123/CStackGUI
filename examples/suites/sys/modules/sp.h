/**
 * @file    sp.h
 * @brief   syspanel 套件模块接口
 */
#ifndef SUITE_SP_H
#define SUITE_SP_H

#include "cstackgui.h"

/** syspanel 的事件绑定表（表尾 CG_BIND_END）；事件名带 sp_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t sp_bindings[];

/**
 * @brief syspanel 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void sp_init(cg_app_t *app, void *user);

#endif
