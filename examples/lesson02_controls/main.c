/**
 * @file    main.c
 * @brief   入门 02：常用输入控件怎么「读」、怎么「写」
 * @details 入门 01 只用了编辑框和按钮。这一个例子把表单里最常用的六种控件凑齐，
 *          每种都走「读一遍（显示到汇总栏）+ 写一遍（重置按钮）」：
 *
 *            编辑框 edit      -> cg_widget_text()
 *            数字微调 spinner -> cg_widget_value()      （值就是数字）
 *            复选框 checkbox  -> cg_widget_checked()
 *            单选钮 radio     -> cg_widget_checked()    （互斥只在"点"的时候自动做）
 *            下拉框 combo     -> cg_widget_value() = 第几项，配合 cg_widget_list_item() 取文字
 *            滑块 slider      -> cg_widget_value()      （范围由 min/max 决定）
 *
 *          顺带演示两件工程上的事：
 *            - 容器（group / box / card / tabs）里的子控件，rect 坐标是**相对容器**的；
 *            - 六种控件的 change / select 事件全指向同一个回调 on_form_change —— 事件的名字
 *              可以共用，反正逻辑就是"有任何一个变了就重算一遍汇总"。
 *
 * 界面在 lesson02_controls.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson02_controls
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/* 「重置」要写回去的默认值：和 .cgui 里写的初始值保持一致 */
#define DEF_AGE    24
#define DEF_LEVEL  3

/** @brief 读一遍全部控件，更新下面的汇总标签、完成度进度条和状态栏 */
static void collect(cg_app_t *app)
{
    cg_widget_t *edt = cg_find(app, "edt_name");
    cg_widget_t *spn = cg_find(app, "spn_age");
    cg_widget_t *chk = cg_find(app, "chk_vip");
    cg_widget_t *rad = cg_find(app, "rad_male");     /* 两个单选钮，看第一个就知道性别 */
    cg_widget_t *cmb = cg_find(app, "cmb_city");
    cg_widget_t *sld = cg_find(app, "sld_level");
    cg_widget_t *prg = cg_find(app, "prg_done");
    const char  *name = cg_widget_text(edt);
    int          city_idx = cg_widget_value(cmb);    /* 下拉框：存的是下标，不是文字 */
    const char  *city = cg_widget_list_item(cmb, city_idx);
    int          done = 0;
    char         buf[512];

    /* 完成度：填了哪几样就加几分（进度条是"只被程序写"的控件，没有事件） */
    if (name && *name)                 done += 34;
    if (cg_widget_value(spn) > 0)      done += 22;
    if (city && *city)                 done += 22;
    if (cg_widget_value(sld) > 0)      done += 22;

    snprintf(buf, sizeof(buf),
             "姓名：%s\n年龄：%d\n会员：%s\n性别：%s\n城市：%s（下拉框第 %d 项）\n满意度：%d",
             (name && *name) ? name : "（空）",
             cg_widget_value(spn),
             cg_widget_checked(chk) ? "是" : "否",
             cg_widget_checked(rad) ? "男" : "女",
             (city && *city) ? city : "（未选）",
             city_idx,
             cg_widget_value(sld));
    cg_widget_set_text(cg_find(app, "lab_summary"), buf);
    cg_widget_set_value(prg, done);
    cg_status_setf(cg_find(app, "sb_main"), 0, "资料完成度 %d%%", done);
}

/**
 * @brief 任何一个输入控件变了都走这里
 * @param self 触发的控件
 * @param ev change / select
 * @param user 未使用
 */
static void on_form_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    collect(cg_app_of(self));
}

/**
 * @brief 「重置」：把每种控件的"写"方法过一遍
 * @param self 重置按钮
 * @param ev click
 * @param user 未使用
 */
static void on_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_widget_set_text(cg_find(app, "edt_name"), "");              /* 编辑框：改文字 */
    cg_widget_set_value(cg_find(app, "spn_age"), DEF_AGE);         /* 微调：改数值（自动夹到 1..120） */
    cg_widget_set_checked(cg_find(app, "chk_vip"), 0);             /* 复选：改勾选 */
    /* 单选钮注意：**程序改勾选不会自动把兄弟取消掉**（互斥发生在用户点击时）。
     * 所以两个都要显式写一遍，否则会出现"两个都选中"。 */
    cg_widget_set_checked(cg_find(app, "rad_male"), 1);
    cg_widget_set_checked(cg_find(app, "rad_female"), 0);
    cg_widget_set_value(cg_find(app, "cmb_city"), 0);              /* 下拉框：改选中第几项 */
    cg_widget_set_value(cg_find(app, "sld_level"), DEF_LEVEL);     /* 滑块：改位置 */

    collect(app);
    cg_log(CG_LOG_INFO, "表单已重置");
}

/** @brief 界面建好之后调一次：先算一遍，让汇总栏一进来就有内容 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;
    collect(app);
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_form_change", on_form_change),
    CG_BIND("on_reset_click", on_reset_click),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson02_controls.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson02_controls.cgui";
    return cg_app_run(&desc);
}
