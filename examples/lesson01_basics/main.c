/**
 * @file    main.c
 * @brief   入门 01 · 基础与容器 —— 按钮 / 图标按钮 / 标签 / 链接 / 开关 / 徽章 / 标签片 / 分割线 + 三种容器
 * @details 这一课把"搭界面最基础的那批控件"一次摆齐，界面在 `lesson01_basics.cgui`：
 *            - **左边一列是逐个演示**：每行"类型名 + 一个控件"，点一下看状态栏写什么；
 *            - **右边是容器组合**：`group` / `box` / `card` 里各放几个控件，
 *              演示"容器里的坐标是相对容器左上角的"，不是相对窗口。
 *
 *          本课覆盖的控件与关键 API：
 *            - `button`      能点的：`click` 事件；`enabled: false` 的禁用态点不动
 *            - `iconbutton`  只放一张图（`src`），点击同样是 `click`
 *            - `label`       纯显示文字，`cg_widget_set_text()` 改它
 *            - `link`        蓝色下划线的可点文字；要真跳转就在回调里 `cg_shell_open(url)`
 *            - `switch`      开/关两态，读 `cg_widget_checked()`（不是 `value`）
 *            - `badge` / `tag`  小圆片与圆角标签片；`badge` 没有事件
 *            - `separator`   一条横线，纯分隔，没有事件
 *            - `group` / `box` / `card`  三种容器：子控件的 `rect` 相对容器左上角
 *
 *          本工程贯穿五课的写法（后面每一课都是这一套）：
 *            - `.cgui` 里存 **id 与事件函数名**，C 侧用 `CG_BIND` 把函数名和函数对上；
 *              名字写错不报错，但 `--selftest` 会揪出来；
 *            - 回调签名固定 `void f(cg_widget_t *self, cg_event_t ev, void *user)`；
 *            - 本课所有回调只往状态栏 `sb_main` 写一句 —— 这一课不涉及业务逻辑，看的就是"谁被点了"。
 *
 *          想深入看哪一课：
 *            - 输入框、复选框、单选钮、下拉框、日期、月历  → 入门 02 · 输入与选择
 *            - 滑块、进度条、圆环进度、仪表盘、状态灯        → 入门 03 · 数值与指示
 *            - 列表、树、表格、状态栏、终端、图表、分隔条    → 入门 04 · 数据与日志
 *            - 菜单、工具栏、图片、画板、颜色、网页浏览      → 入门 05 · 命令与图形
 *
 *          编译：在项目根跑 `build-app.bat lesson01_basics`
 *          自检：`examples\lesson01_basics\lesson01_basics.exe --selftest examples\lesson01_basics\lesson01_basics.cgui`
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 公用小工具 */

/**
 * @brief 往状态栏写一句话（本课所有回调的唯一出口）
 * @param app 所属应用（用 cg_app_of(self) 拿）
 * @param fmt 格式串，其后是它的参数
 * @return void 无返回值；超过 200 字节会被截断
 * @note 为什么统一走状态栏：把一个回调的"效果"集中在一处，界面截图和文章里都对得上，
 *       不会出现"点了没反应"的错觉。
 */
static void say(cg_app_t *app, const char *fmt, ...)
{
    char    buf[200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cg_status_set(cg_find(app, "sb_main"), 0, buf);
}

/** 主按钮被点的次数（演示"业务状态放哪"：文件级 static 就够） */
static int s_clicks;

/* ------------------------------------------------------------ 窗口（ready） */

/**
 * @brief 窗口事件：就绪
 * @param self 窗口伪控件（不是普通控件，id 是 CG_WINDOW_ID）
 * @param ev ready
 * @param user 未使用
 * @return void 无返回值
 * @note 窗口本身也是一个"控件"：它的事件写在 `.cgui` 的 `app.events` 里，
 *       和控件事件共用同一套 CG_BIND 机制。窗口能报的事件比控件多（关闭、移动、大小、托盘…）。
 */
static void on_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "窗口准备好了：左边逐个试控件，右边看容器里的控件怎么排。");
}

/* ------------------------------------------------------------ button 按钮 */

/**
 * @brief 主按钮被点：数一下点了第几次
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note `cg_widget_text(self)` 读按钮上的文字；改文字用 `cg_widget_set_text()`。
 *       按钮上是"程序改文字"还是"用户点击"，回调里也能看出来：点击会走到这里，
 *       程序改文字不会（本工程的规矩：程序改控件不触发回调）。
 */
static void on_btn_primary_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_clicks += 1;
    say(cg_app_of(self), "主按钮被点了第 %d 次（文字是「%s」）", s_clicks, cg_widget_text(self));
}

/* ------------------------------------------------------------ iconbutton 图标按钮 */

/**
 * @brief 图标按钮被点
 * @param self 被点的图标按钮（内容来自 `.cgui` 的 `src`）
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 图标按钮和普通按钮是两回事：它没有文字只有图，适合"工具条上那颗方按钮"这种场景。
 *       图片路径写在 `src`，相对 `.cgui` 所在目录；换图时运行时会自动释放旧位图。
 */
static void on_icb_run_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "图标按钮被点了（%s）", cg_widget_id(self));
}

/* ------------------------------------------------------------ link 链接 */

/**
 * @brief 链接被点
 * @param self 被点的链接
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 想真跳转就一行 `cg_shell_open("https://…")`（用系统默认浏览器打开）。
 *       本示例**故意不指向外部网址** —— 教程里的链接最容易烂掉，读者点到一个 404 只会困惑。
 */
static void on_lnk_docs_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "链接「%s」被点了：要跳转就在这里调 cg_shell_open(url)", cg_widget_text(self));
}

/* ------------------------------------------------------------ switch 开关 */

/**
 * @brief 开关被切换
 * @param self 开关控件
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 * @note 开关的状态读 `cg_widget_checked()`（**不是** `value`）；程序改状态用
 *       `cg_widget_set_checked()`，同样不会触发这个回调。
 */
static void on_sw_notify_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "开关现在「%s」", cg_widget_checked(self) ? "开" : "关");
}

/* ------------------------------------------------------------ tag 标签片 */

/**
 * @brief 标签片被点
 * @param self 被点的标签片
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 标签片长得像"关键词"，比徽章长、能点；常用来做筛选词的按钮。
 */
static void on_tg_demo_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "标签片「%s」被点了", cg_widget_text(self));
}

/* ------------------------------------------------------------ 容器里的控件 */

/**
 * @brief 分组框里的按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 它和窗口里的按钮**没有任何区别** —— 唯一的差别是 `.cgui` 里的 rect 是
 *       相对分组框左上角算的。容器自己不带事件。
 */
static void on_btn_in_grp_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "组内按钮被点了：它的坐标 (16,60) 是相对分组框的");
}

/**
 * @brief 容器块里的开关被切换
 * @param self 开关控件
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_sw_in_box_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "容器块里的开关：%s", cg_widget_checked(self) ? "开" : "关");
}

/**
 * @brief 卡片里的标签片被点
 * @param self 被点的标签片
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tg_in_card_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "卡片里的标签片被点了");
}

/**
 * @brief 卡片里的链接被点
 * @param self 被点的链接
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_lnk_in_card_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "卡片里的链接被点了");
}

/**
 * @brief 卡片里的按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 容器**可以嵌套**（卡片里再放分组框也行）；嵌套时坐标逐层相对。
 */
static void on_btn_in_card_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say(cg_app_of(self), "卡片里的按钮被点了");
}

/* ------------------------------------------------------------ 绑定表与入口 */

/** `.cgui` 里出现的函数名 -> C 函数；漏一个 `--selftest` 会报"未登记" */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_app_ready",       on_app_ready),
    CG_BIND("on_btn_primary_click", on_btn_primary_click),
    CG_BIND("on_icb_run_click",   on_icb_run_click),
    CG_BIND("on_lnk_docs_click",  on_lnk_docs_click),
    CG_BIND("on_sw_notify_change", on_sw_notify_change),
    CG_BIND("on_tg_demo_click",   on_tg_demo_click),
    CG_BIND("on_btn_in_grp_click", on_btn_in_grp_click),
    CG_BIND("on_sw_in_box_change", on_sw_in_box_change),
    CG_BIND("on_tg_in_card_click", on_tg_in_card_click),
    CG_BIND("on_lnk_in_card_click", on_lnk_in_card_click),
    CG_BIND("on_btn_in_card_click", on_btn_in_card_click),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson01_basics.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson01_basics.cgui";
    return cg_app_run(&desc);
}
