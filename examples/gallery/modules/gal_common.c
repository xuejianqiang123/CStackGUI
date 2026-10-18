/**
 * @file    gal_common.c
 * @brief   gallery 公共层实现：业务状态实例 + 日志/状态栏回执 + 窗口关闭拦截 + 对话框
 * @details 对话框是**独立顶层窗口**：cg_dialog_open 模态打开（返回时才有结果），
 *          cg_dialog_open_nonmodal 非模态打开（立即返回）。关闭一律 cg_dialog_close(self, result)。
 */
#include "gal_common.h"

#include <stdarg.h>
#include <stdio.h>

/** 三页共用的业务状态实例（就绪钩子里作为 ready_user 传给各控件的 user） */
static biz_t s_biz;

/**
 * @brief 取三页共用的业务状态实例（各页回调的兜底数据源）
 * @return biz_t* 永不为 NULL 的业务状态指针
 */
biz_t *gal_biz(void)
{
    return &s_biz;
}

/**
 * @brief 把一行日志同时打印到控制台并追加到页 0 的日志列表框
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void gal_log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    cg_widget_t *log = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[log] %s\n", buf);
    if (log) cg_widget_list_add(log, buf);
}

/**
 * @brief 把一句话同时写到状态栏第 2 段和 stdout
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void gal_status(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;
    cg_widget_t *sb = cg_find(app, "sb_main");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[gallery] %s\n", buf);
    if (sb) cg_status_set(sb, 1, buf);
}

/* ------------------------------------------------------------ 窗口级事件 */

/**
 * @brief 顶层标签页切换时把当前页号写到状态栏
 * @param self 该标签页控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_pages_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    gal_status(app, "顶层切到第 %d 页", cg_tabs_selected(self) + 1);
}

/**
 * @brief 窗口关闭前拦一次：富文本有未保存改动时取消本次关闭
 * @param self 窗口控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 * @note 第一次关闭被拦下并提示，再点一次才真正退出。
 *       窗口自身也是个"控件"（id 是 __window），所以回调签名和别的事件一样。
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    cg_widget_t *sb = cg_find(app, "sb_main");
    (void)ev;

    if (biz && biz->dirty && !biz->close_tried) {
        biz->close_tried = 1;
        printf("[gallery] 富文本有未保存的改动，已拦下这次关闭（再点一次就退出）\n");
        if (sb) cg_status_set(sb, 1, "有未保存的改动：再点一次关闭才退出");
        cg_cancel_close(app);      /* 这次先不关 */
        return;
    }
    if (sb) cg_status_set(sb, 1, "关闭中…");
    printf("[gallery] 窗口关闭\n");
}

/* ------------------------------------------------------------ 多窗体（对话框） */

/**
 * @brief 对话框里的"确定"：带结果 1 关闭
 * @param self 触发事件的控件（对话框里的按钮）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
static void on_dlg_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}

/**
 * @brief 对话框里的"取消"：带结果 0 关闭
 * @param self 触发事件的控件（对话框里的按钮）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note Esc 与右上角关闭钮走的也是这条 result=0 的路，业务不必各接一遍。
 */
static void on_dlg_cancel(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 0);
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 gallery.cgui 里） */
const cg_binding_t gal_common_bindings[] = {
    /* 窗口级 */
    CG_BIND("on_close",         on_close),
    /* 顶层标签页 */
    CG_BIND("on_pages_change",  on_pages_change),
    /* ---- 多窗体（对话框）---- */
    CG_BIND("on_dlg_ok",        on_dlg_ok),
    CG_BIND("on_dlg_cancel",    on_dlg_cancel),
    CG_BIND_END
};
