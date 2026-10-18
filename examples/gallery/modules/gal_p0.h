/**
 * @file    gal_p0.h
 * @brief   gallery 页 0「基础闭环」：分组框/输入/勾选/滑块/进度条/列表框/按钮联动
 */
#ifndef GAL_P0_H
#define GAL_P0_H

#include "cstackgui.h"

#include "gal_common.h"

/** 页 0 的事件绑定表（表尾 CG_BIND_END） */
extern const cg_binding_t gal_p0_bindings[];

/**
 * @brief 页 0 的初始状态：挂业务数据、摆滑块初值、写启动日志
 * @param app 所属应用
 * @param biz 业务状态结构，非空，等级字段在此被置为 40
 * @return void 无返回值
 */
void gal_p0_ready(cg_app_t *app, biz_t *biz);

#endif
