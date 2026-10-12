/**
 * @file    main.c
 * @brief   入门 02：输入与选择 —— 表单里的控件怎么读、怎么填
 * @details 一屏把「填表」要用的控件全摆出来，每个都配回调，结果只落在状态栏那一句：
 *
 *            edit        单行 / 多行 / 数值框（自动·手动）/ 六种边框
 *                        —— 文字用 cg_widget_text() 读；数值框用 cg_widget_value() 读
 *            richtext    多行富文本（RICHEDIT50W），和 edit 多行是两套控件
 *            datepicker  日期 / 时间，值就是 cg_widget_text()（format 决定写法）
 *            monthcal    月历：点一天发 select，选中的日期被写回 text
 *            checkbox    勾选：cg_widget_checked() 读，cg_widget_set_checked() 写
 *            radio       一组单选就是一个选择：互斥发生在「用户点击」时（程序改勾选要自己两两写）
 *            listbox     列表：cg_widget_value() 是下标，cg_widget_list_item() 才是文字
 *            combo       下拉：和 listbox 同一套下标语义
 *            group       容器：子控件的 rect 坐标**相对容器**，不是相对窗口
 *
 *          三个当「进阶技巧」讲的地方：
 *            1. on_form_change()：多个控件共用一个回调 + cg_widget_id(self) 分流 ——
 *               省代码的写法（六个边框输入框 + 容器里那四个控件都走它）；
 *            2. on_rad_group_change()：一组单选在业务上就是一个选择，三个 radio 共用一个回调，
 *               连分流都不用做 —— 直接读 cg_widget_checked() 拼出答案；
 *            3. 数值框的两种调节器：spin = 1 由运行时按 ±1 改值并夹到区间；
 *               spin = 2 只报方向，步长由业务自己乘（s_biz.step）。
 *
 *          还有一个"只有状态、没有事件"的角色：chk_locked 是 "enabled": false 的禁用控件 ——
 *          点不动、一个回调都不会发，但 cg_widget_checked() 照样读得到它的状态。
 *
 *          字段真相在 docs/CGUI-FORMAT.md 第 2 节，类型与事件表在第 4 节。
 *          容器与布局回看入门 01；滑块、进度、仪表这类"只被程序写"的控件走入门 03。
 *
 * 界面在 lesson02_inputs.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson02_inputs
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** @brief 业务状态：回调之间要记住的东西（本项目惯例：文件级 static 收在 s_biz 里） */
typedef struct {
    int step;   /**< 手动数值框的步长：运行时只报 ±1，乘多少是业务的事 */
} biz_t;

static biz_t s_biz;

/**
 * @brief 取控件文字，NULL 一律当空串
 * @param w 控件
 * @return const char* 永远不是 NULL，可以放心 strlen / printf
 */
static const char *txt(const cg_widget_t *w)
{
    const char *s = cg_widget_text(w);

    return s ? s : "";
}

/* 每个回调的最后一句都是 cg_status_setf(..., "sb_main", ...)：状态栏是本课唯一的结果出口。
 * 不去自己包一层 varargs 助手 —— 运行时的 cg_status_setf 就是干这个的。 */

/* ---------------------------------------- edit：能打字的地方 */

/**
 * @brief 单行输入框：改一个字就报一次
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_single_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 单行：\"%s\"（%d 字）", txt(self), (int)strlen(txt(self)));
}

/**
 * @brief 多行输入框：只报行数与字数，免得把状态栏撑爆
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_multi_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *s = txt(self);
    int         lines = 1;
    const char *p;

    (void)ev;
    (void)user;
    for (p = s; *p; p++) {
        if (*p == '\n') lines++;
    }
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 多行：%d 行 / %d 字（Enter 换行，滚动条是 scroll: 2 给的）",
                   lines, (int)strlen(s));
}

/**
 * @brief 数值框（自动）：值运行时已经改好并夹过，业务只管读
 * @param self 数值框
 * @param ev spin
 * @param user 未使用
 * @return 无
 */
static void on_edt_spin_auto_spin(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 数值框（自动）：%d，本次 %+d（越界由运行时夹）",
                   cg_widget_value(self), cg_event_delta(self));
}

/**
 * @brief 数值框（手动）：运行时只给方向，步长由业务自己定
 * @param self 数值框
 * @param ev spin
 * @param user 未使用
 * @return 无
 */
static void on_edt_spin_manual_spin(cg_widget_t *self, cg_event_t ev, void *user)
{
    int delta = cg_event_delta(self) * s_biz.step;

    (void)ev;
    (void)user;
    if (delta == 0) delta = s_biz.step;     /* 自检合成的事件没有方向，就当加一步 */
    cg_widget_set_value(self, cg_widget_value(self) + delta);
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 数值框（手动）：%d（步长 %d，运行时只管报 ±1）",
                   cg_widget_value(self), s_biz.step);
}

/**
 * @brief 字节输入框失焦：夹取与清空都发生在这一下，回调里读到的是处理后的文字
 * @param self 输入框
 * @param ev lostfocus
 * @param user 未使用
 * @return 无
 */
static void on_edt_byte_blur(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 字节：失焦后是 \"%s\"（超过 0~255 夹回，不是数字就清空）", txt(self));
}

/**
 * @brief 整数输入框：按键阶段就挡住了非法字符，回调里只可能拿到"像数字"的文字
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_int_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 整数：\"%s\"（真要当数字用，建数值框走 cg_widget_value 更省事）",
                   txt(self));
}

/**
 * @brief 日期时间输入框：形状对不上时，失焦会被清空（"没填"是合法状态，不弹窗）
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_dt_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 日期时间：\"%s\"", txt(self));
}

/**
 * @brief 大小写转换：Windows 在写入文本时也会转，所以初始的 abc 进来就显示成 ABC
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_upper_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 转大写：\"%s\"（conv: 2，连初始 text 一起转）", txt(self));
}

/**
 * @brief 限长输入框：第 9 个字符根本敲不进去，回调里也就永远看不到
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_edt_short_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "edit 限长：\"%s\"（%d / 8 字，多一个字都进不来）",
                   txt(self), (int)strlen(txt(self)));
}

/* ---------------------------------------- richtext / datepicker / monthcal：日期与富文本 */

/**
 * @brief 富文本变了：和 edit 多行一样读文字，只是控件换成了 RICHEDIT50W
 * @param self 富文本控件
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_rt_note_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "richtext：%d 字（多行富文本，能选中、能复制）", (int)strlen(txt(self)));
}

/**
 * @brief 日期选择变了：值就是 text，不用另找 API
 * @param self 日期选择框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_dtp_date_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "datepicker（date）：%s", txt(self));
}

/**
 * @brief 时间选择变了：同一个控件的 format 换成 time
 * @param self 时间选择框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_dtp_time_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "datepicker（time）：%s", txt(self));
}

/**
 * @brief 月历点了某天：选中日期被写回 text，格式固定 YYYY-MM-DD
 * @param self 月历
 * @param ev select
 * @param user 未使用
 * @return 无
 */
static void on_cal_pick_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "monthcal：选中的日期是 %s", txt(self));
}

/* ---------------------------------------- checkbox / radio：勾选 */

/**
 * @brief 复选框变了：读勾选用 cg_widget_checked
 * @param self 复选框
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_chk_vip_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "checkbox：%s", cg_widget_checked(self) ? "已勾选" : "已取消");
}

/**
 * @brief 一组三个单选钮共用一个回调：一组单选在业务上就是"一个选择"
 * @param self 触发的单选钮（点哪一个都进这里）
 * @param ev change
 * @param user 未使用
 * @return 无
 * @note 和 on_form_change 的"id 分流"不是一回事：这里连分流都不用做 —— 三个按钮问的是
 *       同一个问题（你是谁），所以直接读 cg_widget_checked() 拼出结果就行。
 *       点的是哪一个不在 self 上体现，只体现在"谁现在是选中的"。
 */
static void on_rad_group_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *who = "（都没选）";

    (void)ev;
    (void)user;
    if (cg_widget_checked(cg_find(app, "rad_stu"))) {
        who = "学生";
    } else if (cg_widget_checked(cg_find(app, "rad_emp"))) {
        who = "职员";
    } else if (cg_widget_checked(cg_find(app, "rad_other"))) {
        who = "其他";
    }
    cg_status_setf(cg_find(app, "sb_main"), 0,
                   "radio 单选：现在选中的是「%s」（一组共用一个回调）", who);
}

/* ---------------------------------------- listbox / combo：列表与下拉 */

/**
 * @brief 列表选中项变了：value 是下标，文字要另取
 * @param self 列表框
 * @param ev select
 * @param user 未使用
 * @return 无
 */
static void on_lst_skill_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int         idx = cg_widget_value(self);
    const char *item = cg_widget_list_item(self, idx);

    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "listbox：第 %d 项「%s」（%d 项可选）",
                   idx, item ? item : "（未选）", cg_widget_list_count(self));
}

/**
 * @brief 下拉框选中项变了：下标语义和 listbox 完全一样
 * @param self 下拉框
 * @param ev select
 * @param user 未使用
 * @return 无
 */
static void on_cmb_city_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int         idx = cg_widget_value(self);
    const char *item = cg_widget_list_item(self, idx);

    (void)ev;
    (void)user;
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "combo：第 %d 项「%s」", idx, item ? item : "（未选）");
}

/* ---------------------------------------- on_form_change：多个控件共用一个回调（进阶技巧） */

/**
 * @brief 多控件共用一个回调，靠 cg_widget_id(self) 分流
 * @param self 触发的控件（id 用来判断是谁）
 * @param ev change
 * @param user 未使用
 * @return 无
 * @note 这是「省代码的写法」，文章里当进阶技巧讲：六个边框输入框 + 容器里那四个控件，
 *       行为都是"改了就报一句"，与其写十个几乎一样的函数，不如一个回调 + 一张 id 表。
 *       它和「每个主控件一个专用回调」不冲突：主控件用专用回调（名字自带语义，
 *       找 bug 时一眼知道是谁），这种纯展示、行为一致的控件才共用。
 *       代价是 id 必须写清楚（edt_b3 而不是 edt3），因为 id 就是这里的分流依据。
 */
static void on_form_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_widget_id(self);
    cg_app_t   *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!id) return;

    if (cg_str_equal(id, "edt_grp_name")) {
        cg_status_setf(cg_find(app, "sb_main"), 0,
                       "容器里的 edit：\"%s\"（坐标相对 group）", txt(self));
    } else if (cg_str_equal(id, "chk_grp_vip")) {
        cg_status_setf(cg_find(app, "sb_main"), 0,
                       "容器里的 checkbox：%s", cg_widget_checked(self) ? "已勾选" : "已取消");
    } else if (cg_str_equal(id, "rad_grp_a") || cg_str_equal(id, "rad_grp_b")) {
        /* 同一个容器里的两个 radio 也是一组互斥，和窗口上那三个互不干扰 */
        cg_status_setf(cg_find(app, "sb_main"), 0,
                       "容器里的 radio：选中「%s」", txt(self));
    } else {
        /* edt_b1~edt_b6：只演示边框长相，改了文字就报一句 */
        cg_status_setf(cg_find(app, "sb_main"), 0,
                       "border 输入框 #%s：\"%s\"", id, txt(self));
    }
}

/* ---------------------------------------- 生命周期 */

/**
 * @brief 界面建好之后调一次：填业务初值，并给状态栏一句话
 * @param app 应用
 * @param user 未使用
 * @return 无
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;
    s_biz.step = 5;     /* 手动数值框的步长：自动方式拿不到这个能力，只能业务自己乘 */
    cg_status_set(cg_find(app, "sb_main"), 0, "改一改上面的控件，这里就会多一句结果");
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_edt_single_change",      on_edt_single_change),
    CG_BIND("on_edt_multi_change",       on_edt_multi_change),
    CG_BIND("on_edt_spin_auto_spin",     on_edt_spin_auto_spin),
    CG_BIND("on_edt_spin_manual_spin",   on_edt_spin_manual_spin),
    CG_BIND("on_edt_byte_blur",          on_edt_byte_blur),
    CG_BIND("on_edt_int_change",         on_edt_int_change),
    CG_BIND("on_edt_dt_change",          on_edt_dt_change),
    CG_BIND("on_edt_upper_change",       on_edt_upper_change),
    CG_BIND("on_edt_short_change",       on_edt_short_change),
    CG_BIND("on_rt_note_change",         on_rt_note_change),
    CG_BIND("on_dtp_date_change",        on_dtp_date_change),
    CG_BIND("on_dtp_time_change",        on_dtp_time_change),
    CG_BIND("on_cal_pick_select",        on_cal_pick_select),
    CG_BIND("on_chk_vip_change",         on_chk_vip_change),
    CG_BIND("on_rad_group_change",       on_rad_group_change),
    CG_BIND("on_lst_skill_select",       on_lst_skill_select),
    CG_BIND("on_cmb_city_select",        on_cmb_city_select),
    CG_BIND("on_form_change",            on_form_change),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson02_inputs.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson02_inputs.cgui";
    return cg_app_run(&desc);
}
