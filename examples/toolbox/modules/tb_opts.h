/**
 * @file    tb_opts.h
 * @brief   toolbox 成对开关（沙箱）：5 个行为开关真读写 HKCU 沙箱键，配查看/还原
 */
#ifndef TB_OPTS_H
#define TB_OPTS_H

#include "cstackgui.h"

/** 开关种数（0=日志镜像 1=时间戳 2=自动切搜索页 3=记录最近使用 4=右键沙箱标记） */
#define TB_OPT_COUNT 5

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_opts_bindings[];

/**
 * @brief 读一个开关的当前状态
 * @param idx 开关下标（0 起，TB_OPT_COUNT 内）；越界按 0（关）返回
 * @return int 1=装上 0=撤掉
 */
int tb_opt_get(int idx);

/**
 * @brief 就绪钩子用：从沙箱键读回全部开关状态
 * @param read_reg 1=真读注册表；0=自检，保持全关
 * @return void 无返回值
 */
void tb_opts_load(int read_reg);

/**
 * @brief 同步全部开关按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 * @note 装上/撤掉互斥禁用；说明 label 显示「已装上 / 未装」
 */
void tb_opts_sync(cg_app_t *app);

#endif
