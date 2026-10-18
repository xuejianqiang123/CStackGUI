/**
 * @file    l3.c
 * @brief   lesson03_values —— 套件模块（原 examples\lesson03_values 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 l3_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 l3_bindings、原 on_ready 变成 l3_init、
 *          窗口事件改成直连转发 l3_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   入门 03（数值与指示）：把数字显示出来
 * @details 这一课只讲一件事：**值只有一份，显示方式可以有很多种**。
 *          界面里那个滑块是"值"的入口，进度条 / 圆环 / 仪表盘 / 数字标签都只是它的显示器。
 *
 *          本课控件清单（每个控件的关键 API 一句话）：
 *            slider        能读能改：读 cg_widget_value()，改 cg_widget_set_value()；
 *                          change 在拖动中连续发，release 只在松手时发一次
 *            progress      只被程序写：cg_widget_set_value(w, 0~100) 就是刷新，没有事件
 *            progressring  同上，同一个值画成一圈
 *            meter         纯显示：cg_meter_set(w, v) 给值，format 决定 270° 弧 / 半圆
 *            led           纯显示：cg_led_set(w, 0 灭 / 1 亮 / 2 闪)，闪烁靠 period，不用开时钟
 *            rate          能点的评分：星数 = max - min，当前值用 cg_widget_value() 读
 *            pagination    能点的分页：页数 = max - min + 1，当前值 = 当前页
 *            button        操作脚手架：一个「复位为 60」、一个「切换灯态」，
 *                          右下那个是 `"enabled": false` 的禁用态：它只会灰掉、点不动，
 *                          连 click 事件都不会发（禁用不是"看着像"，是 EnableWindow(FALSE)）
 *            label         说明文字 + 数字显示（纯显示，一个事件都没有）
 *
 *          三条要记住的规矩：
 *            1. 读值一律 cg_widget_value()、写值一律 cg_widget_set_value()，不要在回调里
 *               自己攒一份"影子变量"当真相 —— 控件的 value 就是真相。
 *            2. **程序改值不会触发 change 回调**：cg_widget_set_value() 只做"重绘 + 绑定传播"，
 *               不派发事件。所以「复位为 60」和 on_ready 里改完值，必须手动把几种显示刷一遍
 *               （见 show_level()）。
 *               界面里留了一组对比，读者一眼能分清两条路（规范 2.8）：
 *                 lab_level      C 拼串："60 / 100" —— 要拼字符串只能自己写 C；
 *                 lab_sld_mirror .cgui 里 "bind": { "src": "sld_level" } —— **一行 C 都没有**，
 *                                值自己跟着滑块走，但它只会**整份抄值**（抄出来是 "60"），
 *                                拼不出 "60 / 100" —— 这就是 bind 与手写 C 的分工。
 *            3. 窗口本身也是个控件（CG_WINDOW_ID）：窗口事件写在 .cgui 的 app.events 里，
 *               和控件事件共用同一张 CG_BIND 表。本课只用 ready / close 两个。
 *
 *          想深入看哪一课：键码 / 坐标 / 滚轮这些事件参数见窗口事件专题；表格与日志见入门 04；
 *          菜单 / 画板 / 媒体见入门 05。
 *
 * 界面在 lesson03_values.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson03_values
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int level;   /**< 当前等级：sld_level 的值，0~100（只做记录，真相在控件里） */
    int led;     /**< 状态灯：0 灭 / 1 亮 / 2 闪 */
} biz_t;

static biz_t s_biz;

/* ---------------------------------------- 一个值，四种显示 */

/**
 * @brief 把一个等级值刷到全部"指示类"控件上（本课的核心函数）
 * @param app 应用
 * @param level 新的等级值，运行时按各控件的 min/max 夹取
 * @return 无
 * @note 进度条 / 圆环 / 仪表盘都是**只被程序写**的控件：调一次 set 就是刷新，不用手动重绘。
 */
static void show_level(cg_app_t *app, int level)
{
    char buf[64];

    s_biz.level = level;

    cg_widget_set_value(cg_find(app, "l3_prg_level"), level);    /* 进度条：横着一条 */
    cg_widget_set_value(cg_find(app, "l3_ring_level"), level);   /* 圆环：同一个值画成一圈 */
    cg_meter_set(cg_find(app, "l3_mtr_arc"), level);             /* 仪表盘：270° 弧 */
    cg_meter_set(cg_find(app, "l3_mtr_half"), level);            /* 仪表盘：半圆（format = half） */

    /* C 这条路得自己拼串：lab_level 显示成 "60 / 100"。
     * 旁边那个 lab_sld_mirror 一行 C 都不用写 —— .cgui 里 bind 到 sld_level，值自己跟过来；
     * 但它只会整份抄值（抄出来就是 "60"），所以"要拼字符串"的活仍然得走 C。 */
    snprintf(buf, sizeof(buf), "%d / 100", level);
    cg_widget_set_text(cg_find(app, "l3_lab_level"), buf);       /* 数字本体：标签没有事件，只能主动写它 */
}

/* ---------------------------------------- slider：值的入口 */

/**
 * @brief 滑块拖动中（change）
 * @param self 滑块
 * @param ev change —— 值一变就发，拖动时会连续发好多次
 * @param user 未使用
 * @return 无
 */
static void on_sld_level_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       v   = cg_widget_value(self);   /* 读值的唯一入口 */

    (void)ev;
    (void)user;

    show_level(app, v);
    cg_status_setf(cg_find(app, "l3_sb_main"), 0, "拖动中：等级 %d / 100", v);
}

/**
 * @brief 滑块松手（release）
 * @param self 滑块
 * @param ev release —— 整段拖动只发一次，适合做"拖完了再算"的事
 * @param user 未使用
 * @return 无
 */
static void on_sld_level_release(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;

    cg_status_setf(cg_find(cg_app_of(self), "l3_sb_main"), 0,
                   "松手：等级定为 %d（release 只发这一次）", cg_widget_value(self));
}

/* ---------------------------------------- button：程序改值要自己刷新 */

/**
 * @brief 「复位为 60」：演示"程序改值不触发 change 回调"
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return 无
 */
static void on_btn_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *sld = cg_find(app, "l3_sld_level");

    (void)ev;
    (void)user;

    /* set_value 改的是值本身：会重绘、也会把绑定到它的控件一起带走，
     * 但**不会**派发 change 事件 —— 所以下面必须手动再刷一遍显示。 */
    cg_widget_set_value(sld, 60);
    show_level(app, cg_widget_value(sld));

    cg_status_set(cg_find(app, "l3_sb_main"), 0, "程序把值写回 60：这一步没有触发 change 回调");
}

/* ---------------------------------------- led：三态状态灯 */

/**
 * @brief 「切换灯态」：0 灭 → 1 亮 → 2 闪 循环
 * @param self 按钮
 * @param ev click
 * @param user 未使用
 * @return 无
 */
static void on_btn_led_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    static const char *k_led_text[3] = { "灭", "亮", "闪" };
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *led = cg_find(app, "l3_led_state");

    (void)ev;
    (void)user;

    s_biz.led = (cg_led_state(led) + 1) % 3;          /* 先读回当前状态，再往前推一格 */
    cg_led_set(led, s_biz.led);                       /* 2 = 闪：闪烁由控件自己按 period 重绘 */
    cg_widget_set_text(led, k_led_text[s_biz.led]);   /* 灯右边那句说明文字 */

    cg_status_setf(cg_find(app, "l3_sb_main"), 0, "状态灯：%s（period = %d 毫秒）",
                   k_led_text[s_biz.led], cg_widget_period(led));
}

/* ---------------------------------------- rate / pagination：能点的"数值" */

/**
 * @brief 评分变了：星数由 max - min 决定，当前值就是 cg_widget_value()
 * @param self 评分控件
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_rate_star_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;

    cg_status_setf(cg_find(cg_app_of(self), "l3_sb_main"), 0,
                   "评分：%d 星（.cgui 里 max - min = 5）", cg_widget_value(self));
}

/**
 * @brief 翻页了：页数由 max - min + 1 决定，当前值就是当前页
 * @param self 分页控件
 * @param ev change
 * @param user 未使用
 * @return 无
 */
static void on_pg_page_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;

    cg_status_setf(cg_find(cg_app_of(self), "l3_sb_main"), 0,
                   "翻到第 %d 页（.cgui 里 max - min + 1 = 8 页）", cg_widget_value(self));
}

/* ---------------------------------------- 窗口事件（app.events） */

/**
 * @brief 界面建好、还没显示（ready）
 * @param self 窗口伪控件（id = CG_WINDOW_ID）
 * @param ev ready
 * @param user 未使用
 * @return 无
 * @note 窗口事件和控件事件是同一套机制：.cgui 里写函数名，C 侧用 CG_BIND 登记。
 */
static void on_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    /* 按滑块的初值把四种显示对齐一次。.cgui 里每个控件的初值也都写成 60 了，
     * 这一步是"程序改值要自己刷显示"最稳的写法，也是启动初始化的标准位置。 */
    show_level(app, cg_widget_value(cg_find(app, "l3_sld_level")));
    cg_status_set(cg_find(app, "l3_sb_main"), 0, "就绪：拖一下滑块，看其它四个控件跟不跟");
    cg_log(CG_LOG_INFO, "lesson03 界面就绪：等级初值 %d", s_biz.level);
}

/**
 * @brief 用户点了关闭（close）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 未使用
 * @return 无
 * @note 在 close 回调里调 cg_cancel_close(app) 就能拦下这次关闭（入门 05 用过）。本课不拦，只记一笔。
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;

    cg_log(CG_LOG_INFO, "窗口关闭：最后的等级是 %d", s_biz.level);
}

const cg_binding_t l3_bindings[] = {
    CG_BIND("l3_on_sld_level_change",  on_sld_level_change),
    CG_BIND("l3_on_sld_level_release", on_sld_level_release),
    CG_BIND("l3_on_btn_reset_click",   on_btn_reset_click),
    CG_BIND("l3_on_btn_led_click",     on_btn_led_click),
    CG_BIND("l3_on_rate_star_change",  on_rate_star_change),
    CG_BIND("l3_on_pg_page_change",    on_pg_page_change),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */

/**
 * @brief lesson03_values 套件模块初始化（原工程没有 on_ready；初值原在 main() 里，由转换器搬来）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void l3_init(cg_app_t *app, void *user)
{
    (void)app;
    (void)user;
    /* 原 main() 里的工具级初值（转换器搬来：丢了会与 .cgui 的初始状态不一致） */


}

/**
 * @brief 窗口事件 ready 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev ready
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l3_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_ready(self, ev, user);
}

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l3_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_close(self, ev, user);
}
