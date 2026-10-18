/**
 * @file    gal_p1.h
 * @brief   gallery 页 1「控件陈列」：菜单/工具栏/开关/微调/日期/树/表格/富文本等逐个演示
 */
#ifndef GAL_P1_H
#define GAL_P1_H

#include "cstackgui.h"

#include "gal_common.h"

/** 页 1 的事件绑定表（表尾 CG_BIND_END） */
extern const cg_binding_t gal_p1_bindings[];

/**
 * @brief 页 1 的初始状态：挂业务数据、联动圆环、给树 / 表格补内容
 * @param app 所属应用
 * @param biz 业务状态结构，可为 NULL；非空时把开关置开、数量置 5
 * @return void 无返回值
 */
void gal_p1_ready(cg_app_t *app, biz_t *biz);

#endif
