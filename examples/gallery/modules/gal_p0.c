/**
 * @file    gal_p0.c
 * @brief   gallery 页 0「基础闭环」实现：业务状态 → 控件显示 → 事件驱动改状态 的最小闭环
 */
#include "gal_p0.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 把一行状态文字写进页 0 的状态标签
 * @param app 所属应用
 * @param text 要显示的状态文字
 * @return void 无返回值
 */
static void set_status(cg_app_t *app, const char *text)
{
    cg_widget_t *lab = cg_find(app, "lab_hello_status");
    if (lab) cg_widget_set_text(lab, text);
}

/**
 * @brief 把业务等级推回界面，刷新等级文字与进度条
 * @param app 所属应用
 * @return void 无返回值
 */
static void refresh_level(cg_app_t *app)
{
    cg_widget_t *lab = cg_find(app, "lab_level");
    cg_widget_t *prg = cg_find(app, "prg_level");
    char buf[64];

    snprintf(buf, sizeof(buf), "等级 %d", gal_biz()->level);
    if (lab) cg_widget_set_text(lab, buf);
    if (prg) cg_widget_set_value(prg, gal_biz()->level);
}

/**
 * @brief 定时器到点后把等级回落到 40 并刷新界面
 * @param user 定时器传入的应用指针（不是挂在控件上的业务数据）
 * @return void 无返回值
 * @note 定时器回调：到点后在消息循环里执行，此时已跳出原事件栈，安全
 */
static void reset_level_later(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_widget_t *sld = cg_find(app, "sld_level");

    gal_biz()->level = 40;
    if (sld) cg_widget_set_value(sld, gal_biz()->level);
    refresh_level(app);
    gal_log_line(app, "2 秒后自动回落到等级 40");
}

/**
 * @brief 单击时累加点击次数并把姓名、VIP、等级写到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    char buf[160];

    (void)ev;
    if (!biz) biz = gal_biz();

    biz->click_count++;
    snprintf(buf, sizeof(buf), "单击：%s，VIP=%s，等级 %d（第 %d 次单击）",
             biz->name[0] ? biz->name : "(未填姓名)",
             biz->vip ? "是" : "否", biz->level, biz->click_count);
    set_status(app, buf);
    gal_log_line(app, "BTN 单击 #%d", biz->click_count);
}

/**
 * @brief 双击时把等级拉满到 100 并安排 2 秒后回落
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_ok_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char buf[160];

    (void)ev;
    (void)user;
    /* 双击时顺手把等级拉满，演示「事件驱动改状态」 */
    gal_biz()->level = 100;
    {
        cg_widget_t *sld = cg_find(app, "sld_level");
        if (sld) cg_widget_set_value(sld, gal_biz()->level);   /* 会触发 on_level_change */
    }
    snprintf(buf, sizeof(buf), "双击：等级已拉满到 %d", gal_biz()->level);
    set_status(app, buf);
    gal_log_line(app, "BTN 双击");

    /* 顺便演示 cg_set_timer：2 秒后自动回落到 40 */
    cg_set_timer(app, 2000, reset_level_later, app);
}

/**
 * @brief 重置按钮清零业务状态、等级回到 40 并同步界面
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_name");
    cg_widget_t *chk = cg_find(app, "chk_vip");
    cg_widget_t *sld = cg_find(app, "sld_level");

    (void)ev;
    (void)user;
    memset(gal_biz(), 0, sizeof(biz_t));
    gal_biz()->level = 40;

    if (edt) cg_widget_set_text(edt, "");     /* 界面 -> 会自动回灌 CHANGE，但值一样 */
    if (chk) cg_widget_set_checked(chk, 0);
    if (sld) cg_widget_set_value(sld, gal_biz()->level);
    refresh_level(app);
    set_status(app, "已重置");
    gal_log_line(app, "重置完成");
}

/**
 * @brief 退出按钮打印业务摘要后退出应用
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_quit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    gal_log_line(app, "退出");
    printf("business summary: clicks=%d vip=%d level=%d name=%s\n",
           gal_biz()->click_count, gal_biz()->vip, gal_biz()->level, gal_biz()->name);
    cg_quit(app);
}

/**
 * @brief 姓名框变动时把文本同步进业务状态并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_name_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    const char *text = cg_widget_text(self);

    (void)ev;
    (void)user;
    snprintf(gal_biz()->name, sizeof(gal_biz()->name), "%s", text ? text : "");
    set_status(app, gal_biz()->name[0] ? "姓名已记录" : "姓名已清空");
}

/**
 * @brief 姓名框回车提交时记一条日志并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_name_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    gal_log_line(app, "姓名回车提交: %s", cg_widget_text(self));
    set_status(app, "回车提交");
}

/**
 * @brief VIP 勾选变化时把勾选状态写进业务并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_vip_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    gal_biz()->vip = cg_widget_checked(self);
    gal_log_line(app, "VIP = %s", gal_biz()->vip ? "开" : "关");
    set_status(app, gal_biz()->vip ? "VIP 已开启" : "VIP 已关闭");
}

/**
 * @brief 等级滑块变化时把数值写进业务并刷新界面
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_level_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    gal_biz()->level = cg_widget_value(self);
    refresh_level(app);
    gal_log_line(app, "等级 -> %d", gal_biz()->level);
}

/**
 * @brief 页 0 的初始状态：挂业务数据、摆滑块初值、写启动日志
 * @param app 所属应用
 * @param biz 业务状态结构，非空，等级字段在此被置为 40
 * @return void 无返回值
 */
void gal_p0_ready(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *w;

    biz->level = 40;

    /* 业务数据挂到需要它的控件上，回调里就能直接拿到 */
    if ((w = cg_find(app, "btn_ok")) != NULL)  cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "chk_vip")) != NULL) cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "edt_name")) != NULL) cg_widget_set_user(w, biz);

    /* 用业务状态摆一次初始界面（.cgui 里给的是设计态默认值） */
    if ((w = cg_find(app, "sld_level")) != NULL) cg_widget_set_value(w, biz->level);
    refresh_level(app);
    set_status(app, "就绪：界面由 .cgui 描述，逻辑在 modules\\");

    gal_log_line(app, "启动完成，共 %d 个控件", cg_widget_count(app));
}

/** 页 0 的事件绑定表（右边这些名字写在 gallery.cgui 里） */
const cg_binding_t gal_p0_bindings[] = {
    CG_BIND("on_ok_click",      on_ok_click),
    CG_BIND("on_ok_dblclick",   on_ok_dblclick),
    CG_BIND("on_reset_click",   on_reset_click),
    CG_BIND("on_quit_click",    on_quit_click),
    CG_BIND("on_name_change",   on_name_change),
    CG_BIND("on_name_submit",   on_name_submit),
    CG_BIND("on_vip_change",    on_vip_change),
    CG_BIND("on_level_change",  on_level_change),
    CG_BIND_END
};
