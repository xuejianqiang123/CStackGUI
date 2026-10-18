/**
 * @file    gal_p2.h
 * @brief   gallery 页 2「新特性」：时钟/对话框/画板/取色/表格勾选多选/评分/分页/焦点/图标列表
 */
#ifndef GAL_P2_H
#define GAL_P2_H

#include "cstackgui.h"

#include "gal_common.h"

/** 页 2 的事件绑定表（表尾 CG_BIND_END） */
extern const cg_binding_t gal_p2_bindings[];

/**
 * @brief 页 2 的初始状态：给画板打底稿、给表格挂行数值与初始勾选、补图标列表数据
 * @param app 所属应用
 * @param user 就绪钩子的业务数据（本回调不用）
 * @return void 无返回值
 */
void gal_p2_ready(cg_app_t *app, void *user);

#endif
