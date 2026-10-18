/**
 * @file    gal_common.h
 * @brief   gallery 公共层：三页共用的业务状态 + 日志/状态栏回执 + 窗口与对话框事件
 */
#ifndef GAL_COMMON_H
#define GAL_COMMON_H

#include "cstackgui.h"

/** 事件名 -> C 函数 的登记表（右边这些名字写在 gallery.cgui 里），表尾 CG_BIND_END */
extern const cg_binding_t gal_common_bindings[];

/**
 * 三页共用的业务状态：字段不冲突，各页回调各用各的。
 * 实例由本模块持有，回调里经 gal_biz() 取（控件 user 上挂的也是它）。
 */
typedef struct {
    /* 页 0（基础闭环） */
    char  name[64];
    int   vip;
    int   level;
    int   click_count;
    /* 页 1（控件陈列） */
    int   notify;
    int   count;
    int   tree_added;   /**< 动态加过几个树节点 */
    int   rows_added;   /**< 动态加过几行表格 */
    int   dirty;        /**< 富文本改过还没"保存" */
    int   close_tried;  /**< 已经拦过一次关闭 */
} biz_t;

/**
 * @brief 取三页共用的业务状态实例（各页回调的兜底数据源）
 * @return biz_t* 永不为 NULL 的业务状态指针
 */
biz_t *gal_biz(void);

/**
 * @brief 把一行日志同时打印到控制台并追加到页 0 的日志列表框
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void gal_log_line(cg_app_t *app, const char *fmt, ...);

/**
 * @brief 把一句话同时写到状态栏第 2 段和 stdout
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void gal_status(cg_app_t *app, const char *fmt, ...);

#endif
