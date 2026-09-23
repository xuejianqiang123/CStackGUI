/**
 * @file    cg_widget_data.c
 * @brief   路标：控件内容 API 已按控件族拆成七个文件，这里不再留任何实现
 * @details 2026-09-24 把原来"一个文件塞 7 组控件"的内容按控件族拆开，各文件职责：
 *            - cg_widget_list.c   列表类条目（listbox/combo 等）+ 状态栏 / 图片 / 菜单 /
 *                                 标题·退出·文档转储 / 悬停提示·时钟 / 动态追加控件 cg_widget_add
 *            - cg_widget_tree.c   树；并定义各控件族共用的类型守卫 want_type()（提升进 cg_internal.h）
 *            - cg_widget_table.c  表格（行列、勾选/数值/图标附加数组的增长）
 *            - cg_widget_tabs.c   标签页
 *            - cg_widget_split.c  分隔条（拖动重排的纯几何运算）
 *            - cg_widget_term.c   终端 / 日志视图
 *            - cg_widget_gauge.c  图表（chart）+ 状态灯（led）+ 仪表盘（meter）
 *          **别往这里加实现** —— 新控件 API 请落到上面某个文件，或按同样口径新开一个
 *          cg_widget_<族>.c（文件名必须全局唯一，理由见 tools/check-layers.mjs 第 5 条）；
 *          跨文件共用的内部 helper 声明放进 cg_internal.h。
 */
#include "cg_internal.h"
