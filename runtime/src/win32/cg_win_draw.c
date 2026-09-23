/**
 * @file    cg_win_draw.c
 * @brief   路标：9 个自绘窗口类已拆到本目录的四个文件，本文件只剩文件头
 * @details ① 各类现在住哪（按控件找文件）：
 *             cg_win_draw_canvas.c —— image / 圆环进度（ring）/ 进度条（progress）/ 画板（canvas）
 *             cg_win_draw_panel.c  —— 面板（group/box/card/shape）/ 分隔条（splitter）
 *             cg_win_draw_chart.c  —— 折线 / 柱状图（chart）
 *             cg_win_draw_gauge.c  —— 状态灯（led）/ 仪表盘（meter）
 *          ② 留着这个空壳，是为了让旧引用（注释/文档里写的 "cg_win_draw.c"）落在一个有解释的
 *             地方 —— **不要再往里加代码**，新代码请加到上面四个文件里。
 *          跨模块接口（cgw_register_* / cgw_PanelProc）见 cg_win_internal.h。
 */
#include "cg_win_internal.h"
