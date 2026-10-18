/**
 * @file    tb_mytools.h
 * @brief   toolbox 我的工具：mytools.ini 槽位（名称/命令），加工具不用重编译
 */
#ifndef TB_MYTOOLS_H
#define TB_MYTOOLS_H

#include "cstackgui.h"

/** 我的工具槽位数（对应 mytools.ini 的 [t1]..[t8]） */
#define TB_MY_MAX 8

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_mytools_bindings[];

/**
 * @brief 就绪钩子用：定 mytools.ini 全路径（数据目录下；无数据目录时用相对名）
 * @param path 数据目录全路径；NULL 表示拿不到数据目录
 * @return void 无返回值
 */
void tb_mytools_set_path(const char *path);

/**
 * @brief 重读 mytools.ini 并同步 8 个槽位按钮的文字与可用状态
 * @param app 应用
 * @param read_file 1=真读盘（有窗运行时）；0=自检，用默认值不动盘
 * @return void 无返回值
 */
void tb_mytools_reload(cg_app_t *app, int read_file);

/**
 * @brief 有配置的槽位数（「共 Y 个」计数用）
 * @return int 槽位数（0..TB_MY_MAX）
 */
int tb_mytools_count(void);

/**
 * @brief 取某个槽位的名称（空槽位或越界返回空串）
 * @param slot 槽位下标（0 起）
 * @return const char* 名称，永不为 NULL
 */
const char *tb_my_slot_name(int slot);

/**
 * @brief 取某个槽位的启动命令（空槽位或越界返回空串）
 * @param slot 槽位下标（0 起）
 * @return const char* 命令行，永不为 NULL
 */
const char *tb_my_slot_cmd(int slot);

#endif
