/**
 * @file    tb_rmenu.h
 * @brief   toolbox 右键菜单管理：扫描 HKCU/HKLM 六位置 + 批量禁用/恢复/备份删除/还原
 */
#ifndef TB_RMENU_H
#define TB_RMENU_H

#include "cstackgui.h"

/** 工具箱自建右键菜单组数（复制文件路径 / 在此处打开终端） */
#define TB_RMENU_COUNT 2

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_rmenu_bindings[];

/**
 * @brief 自建菜单组数（底栏「本页 X 个」在右键页用它计数）
 * @return int 组数（固定 TB_RMENU_COUNT）
 */
int tb_rmenu_count(void);

/**
 * @brief 就绪钩子用：定备份 .reg 存放目录（数据目录；拿不到时留空，删键时用 "."）
 * @param dir 数据目录全路径；NULL 表示拿不到
 * @return void 无返回值
 */
void tb_rmenu_set_bakdir(const char *dir);

/**
 * @brief 就绪钩子用：读回自建菜单装撤状态（command 有值 = 已装）
 * @param read_reg 1=真读注册表；0=自检，按未装算
 * @return void 无返回值
 */
void tb_rmenu_load(int read_reg);

/**
 * @brief 同步自建菜单按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 */
void tb_rmenu_sync(cg_app_t *app);

#endif
