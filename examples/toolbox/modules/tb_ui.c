/**
 * @file    tb_ui.c
 * @brief   toolbox 底栏功能带实现：切页计数 / 搜索聚焦 / 非模态日志窗 / 深浅主题 / 关于 / 更新
 */
#include "tb_ui.h"

#include <stdio.h>

#include "tb_log.h"
#include "tb_tools.h"

/** 当前主题是否深色（按钮文字同步用） */
static int s_dark;

/**
 * @brief 切页签：状态栏显示当前页名 + 刷新「本页 X 个」
 * @param self 标签页控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tab_main_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         page = cg_tabs_selected(self);
    const char *name = cg_widget_list_item(self, page);

    (void)ev;
    (void)user;

    tb_page_count_sync(app);
    tb_status_msg(app, "当前页：%s", name ? name : "?");
    tb_log_line(app, "切到第 %d 页：%s", page + 1, name ? name : "?");
}

/**
 * @brief 底栏「搜索」：切到搜索页并把焦点交给输入框
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_search_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_tabs_select(cg_find(app, "tab_main"), 0);
    cg_widget_focus(cg_find(app, "edt_search"));
}

/**
 * @brief 底栏「日志」：开非模态日志对话框（同一时间只允许一个对话框）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹窗是副作用：自检下只写日志
 */
static void on_btn_log_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过弹日志对话框");
        return;
    }
    if (cg_dialog_open_nonmodal(cg_find(app, "dlg_log")) == 1) {
        tb_status_msg(app, "日志窗口已打开");
        tb_log_line(app, "打开日志对话框");
    } else {
        tb_status_msg(app, "已有一个对话框开着");
    }
}

/**
 * @brief 日志对话框「清空」
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_log_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;

    cg_widget_list_clear(cg_find(cg_app_of(self), "lst_log"));
    tb_log_line(cg_app_of(self), "日志已清空");
}

/**
 * @brief 底栏「设置」：切换深浅主题并同步按钮文字（真功能，原版同款）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_theme_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过切主题");
        return;
    }
    s_dark = !s_dark;
    cg_theme_set_dark(app, s_dark);
    cg_widget_set_text(self, s_dark ? "切换到浅色" : "切换到深色");
    tb_status_msg(app, s_dark ? "已切到深色主题" : "已切到浅色主题");
    tb_log_line(app, "主题切换：%s", s_dark ? "深色" : "浅色");
}

/**
 * @brief 底栏「关于」：弹本工具箱的程序信息
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_about_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过弹「关于」");
        return;
    }
    cg_message_box(self,
                   "CStackGUI 工具箱启动器 1.1.0\n"
                   "功能面对标 mxx1-toolbox：按钮墙 / 搜索 / 成对开关 / 最近使用 / 我的工具\n"
                   "界面在 toolbox.cgui，行为在 modules\\ 下各模块（入口 main.c）；"
                   "我的工具改 mytools.ini 即可，不用重编译。",
                   "关于", 0);
}

/**
 * @brief 底栏「检查更新」：演示版不做联网检查，照实说明
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_update_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过弹「检查更新」");
        return;
    }
    cg_message_box(self,
                   "当前版本 1.1.0.0，已是最新。\n（示例不做联网检查，这行文案是照实写的）",
                   "检查更新", 0);
    tb_log_line(app, "检查更新：演示版不联网");
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_ui_bindings[] = {
    CG_BIND("on_tab_main_select",     on_tab_main_select),
    CG_BIND("on_btn_search_click",    on_btn_search_click),
    CG_BIND("on_btn_log_click",       on_btn_log_click),
    CG_BIND("on_btn_log_clear_click", on_btn_log_clear_click),
    CG_BIND("on_btn_theme_click",     on_btn_theme_click),
    CG_BIND("on_btn_about_click",     on_btn_about_click),
    CG_BIND("on_btn_update_click",    on_btn_update_click),
    CG_BIND_END
};
