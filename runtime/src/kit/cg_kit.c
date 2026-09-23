/**
 * @file    cg_kit.c
 * @brief   基础能力库（cg_kit）：路标 —— 实现已按子系统拆走，本文件不再放代码
 * @details 分层说明：core 不认识 Win32，win32 是界面后端，skin 是外观层；本层是
 *          **平台服务**（跟界面无关的那一半 Windows API）。业务代码只认 cstackgui.h，
 *          所以这些能力必须由运行时提供 —— 否则用户做个"读配置、写日志、转编码"的小工具
 *          就得自己 include windows.h，把"只 include cstackgui.h"的约定破掉。
 *          约定：返回 char* 的一律是 malloc 出来的，调用方用 cg_free() 释放。
 *          里面刻意只用到处都有的 Win32 API（不用 RegGetValue 这种 Vista+ 才有的），
 *          并且不依赖 POSIX 的 strtok_r/_stricmp，免得 -std=c11 下漏声明。
 *
 *          2026-09-23 把原先塞在这一个文件里的四个子系统纯搬移拆开（公共 API 未变，
 *          共享助手见 cg_kit_internal.h）。找代码请去对应的子文件，别往这里加实现：
 *            cg_kit_fs.c    文件 / 目录 / 路径：读写枚举、拼接分解、程序目录与数据目录
 *            cg_kit_ini.c   INI 配置读写
 *            cg_kit_str.c   文本与编码（cg_str_*）
 *            cg_kit_sys.c   剪贴板 / 外部程序 / 时间 / 注册表
 */
#include "cg_kit_internal.h"
