/**
 * @file    tb_log.h
 * @brief   toolbox 公共小工具：日志列表回执 + 状态栏回执（各模块共用）
 */
#ifndef TB_LOG_H
#define TB_LOG_H

#include "cstackgui.h"

/**
 * @brief 往日志列表框追加一行；镜像 stdout 与时间戳都受沙箱开关控制
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 * @note 依赖沙箱开关状态（LogTimestamp / MirrorLog），经 tb_opt_get 读取
 */
void tb_log_line(cg_app_t *app, const char *fmt, ...);

/**
 * @brief 把最近一次结果写进状态栏第 1 段（第 0 段固定放计数）
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void tb_status_msg(cg_app_t *app, const char *fmt, ...);

#endif
