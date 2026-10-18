/**
 * @file    l6.h
 * @brief   lesson06_window 套件模块接口
 */
#ifndef SUITE_L6_H
#define SUITE_L6_H

#include "cstackgui.h"

/** lesson06_window 的事件绑定表（表尾 CG_BIND_END）；事件名带 l6_ 前缀，与 .cgui 的 events 对应 */
extern const cg_binding_t l6_bindings[];

/**
 * @brief lesson06_window 套件模块初始化（原工程的 on_ready：窗口就绪后设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void l6_init(cg_app_t *app, void *user);

/**
 * @brief 窗口事件 ready 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev ready
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_ready(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_close(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 size 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_size(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 keydown 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_keydown(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 char 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev char
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_char(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 mousedown 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev mousedown
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_mousedown(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 idle 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev idle
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_idle(cg_widget_t *self, cg_event_t ev, void *user);

/**
 * @brief 窗口事件 tray 的套件转发入口（main 的分发器直接调；不进绑定表）
 * @param self 窗口伪控件
 * @param ev tray
 * @param user 业务数据（透传给模块内部的原 handler）
 * @return void 无返回值
 */
void l6_app_tray(cg_widget_t *self, cg_event_t ev, void *user);

#endif
