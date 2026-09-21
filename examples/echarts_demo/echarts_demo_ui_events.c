/**
 * @file    echarts_demo_ui_events.c
 * @brief   事件回调实现：业务逻辑写在这里
 * @details 这份文件是「你的代码」，重新导出不会覆盖它（画布导出时同名文件会跳过）。
 *          每个回调都能拿到：
 *          self 触发事件的那个控件，用 cg_widget_text/cg_widget_checked/cg_widget_value 读它的状态
 *          ev   事件类型（一个函数只挂一个事件时可以直接忽略）
 *          user cg_widget_set_user() 挂上去的业务数据
 *          拿别的控件用 cg_find(cg_app_of(self), "控件id")；改界面用 cg_widget_set_* 系列。
 */
#include "echarts_demo_ui_events.h"

#include <stdio.h>

/* 画布上还没有绑定事件。在画布右侧「事件」页签里填上 C 函数名，再重新导出。 */
