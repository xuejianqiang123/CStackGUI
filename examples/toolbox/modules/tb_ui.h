/**
 * @file    tb_ui.h
 * @brief   toolbox 底栏功能带：切页 / 搜索 / 日志窗 / 深浅主题 / 关于 / 检查更新
 */
#ifndef TB_UI_H
#define TB_UI_H

#include "cstackgui.h"

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_ui_bindings[];

#endif
