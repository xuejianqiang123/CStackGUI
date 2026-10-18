/**
 * @file    sh.h
 * @brief   shottool 套件模块接口
 */
#ifndef SUITE_SH_H
#define SUITE_SH_H

#include "cstackgui.h"

/** shottool 的事件绑定表（表尾 CG_BIND_END）；事件名带 sh_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t sh_bindings[];

/**
 * @brief shottool 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void sh_init(cg_app_t *app, void *user);

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void sh_app_close(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 tray 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev tray
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void sh_app_tray(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 keydown 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void sh_app_keydown(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 size 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void sh_app_size(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 move 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev move
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void sh_app_move(cg_widget_t *self, cg_event_t ev, void *user);

#endif
