/**
 * @file    hr.h
 * @brief   htmlreport 套件模块接口
 */
#ifndef SUITE_HR_H
#define SUITE_HR_H

#include "cstackgui.h"

/** htmlreport 的事件绑定表（表尾 CG_BIND_END）；事件名带 hr_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t hr_bindings[];

/**
 * @brief htmlreport 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void hr_init(cg_app_t *app, void *user);

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void hr_app_close(cg_widget_t *self, cg_event_t ev, void *user);

#endif
