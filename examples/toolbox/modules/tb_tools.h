/**
 * @file    tb_tools.h
 * @brief   toolbox 工具命令表：按钮墙启动 + 最近使用 + 页计数 + 自检对账
 */
#ifndef TB_TOOLS_H
#define TB_TOOLS_H

#include "cstackgui.h"

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t tb_tools_bindings[];

/**
 * @brief 命令表条目数（含 8 个我的工具槽位）
 * @return int 条目数
 */
int tb_tool_count(void);

/**
 * @brief 内置工具数（命令表条数 - 我的工具槽位数）
 * @return int 内置工具个数
 */
int tb_tool_builtin_count(void);

/**
 * @brief 按控件 id 在命令表里找工具
 * @param id 控件 id（形如 tool_notepad）
 * @return int 表内下标；没登记返回 -1
 */
int tb_tool_by_id(const char *id);

/**
 * @brief 这个下标是不是「我的工具」槽位（命令来自 mytools.ini）
 * @param idx 命令表下标
 * @return int 1=槽位 0=内置
 */
int tb_tool_is_my(int idx);

/**
 * @brief 取工具的显示名（槽位工具返回 mytools.ini 里的名字，空槽位返回空串）
 * @param idx 命令表下标
 * @return const char* 名称，永不为 NULL
 */
const char *tb_tool_name(int idx);

/**
 * @brief 取工具的启动命令（槽位工具返回 mytools.ini 里的命令，空槽位返回空串）
 * @param idx 命令表下标
 * @return const char* 命令行，永不为 NULL
 */
const char *tb_tool_cmd(int idx);

/**
 * @brief 真正启动一个工具：副作用入口，自检下只对账不启动
 * @param app 应用
 * @param idx 工具下标
 * @return void 无返回值
 */
void tb_launch_tool(cg_app_t *app, int idx);

/**
 * @brief 刷新底栏「本页 X 个（共 Y 个）」计数
 * @param app 应用
 * @return void 无返回值
 * @note 页 0（常用）按最近使用条数算；页 2（我的工具）按槽位数；页 7（右键）按自建菜单数
 */
void tb_page_count_sync(cg_app_t *app);

/**
 * @brief 就绪钩子用：装载最近使用列表（默认空，填一行「还没有记录」）
 * @param app 应用
 * @return void 无返回值
 */
void tb_tools_load(cg_app_t *app);

/**
 * @brief 自检用：命令表 id 重复对账（只打日志，不动任何外部状态）
 * @param app 应用
 * @return void 无返回值
 */
void tb_tools_audit(cg_app_t *app);

#endif
