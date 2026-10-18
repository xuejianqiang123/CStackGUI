/**
 * @file    tb_search.h
 * @brief   toolbox 全局搜索：按名称/命令双字段过滤工具，回车或双击启动
 */
#ifndef TB_SEARCH_H
#define TB_SEARCH_H

#include "cstackgui.h"

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_search_bindings[];

/**
 * @brief 按当前搜索框内容重过滤结果列表（就绪钩子里用空关键字跑一次，显示"共 N 个"）
 * @param app 应用
 * @return void 无返回值
 */
void tb_search_apply(cg_app_t *app);

#endif
