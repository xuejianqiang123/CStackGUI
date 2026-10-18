/**
 * @file    l6.c
 * @brief   lesson06_window —— 套件模块（原 examples\lesson06_window 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 l6_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 l6_bindings、原 on_ready 变成 l6_init、
 *          窗口事件改成直连转发 l6_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   入门 06 · 主窗口与窗口事件 —— 窗口配置（app.*）/ 窗口事件（app.events）/ 程序信息（app.info）
 * @details 前五课都在讲"控件"，这一课讲**窗口本身**。界面在 `lesson06_window.cgui`：
 *          1. **窗口配置**：`.cgui` 的 `app` 对象 —— 标题、尺寸、底色、是否居中、边框样式、
 *             总在最前、任务栏、Esc 关窗、托盘图标、窗口图标、鼠标指针……**一行 C 都不用写**。
 *          2. **程序信息**：`app.info` 里的字段会被编译进 exe 的**版本信息**（右键属性 → 详细信息
 *             能看到），运行时用 `cg_app_info(app, "product")` 读回来显示 —— 本课顶部那一排就是。
 *          3. **窗口事件**：窗口本身也是一个"控件"（伪控件 id 固定 `__window`），
 *             它的事件写在 `app.events` 里，和控件事件共用同一套 `CG_BIND` 机制。
 *
 *          窗口支持的事件（对齐「启动窗口」的事件表，共 26 个，别忘了 drop）：
 *            ready / close / closequery / destroy / move / size / activate / deactivate /
 *            firstactivate / idle / tray / show / hide / mousedown / mouseup / rdown / rup /
 *            mousemove / dblclick / gotfocus / lostfocus / keydown / keyup / char / wheel
 *          （本课演示其中 8 个：ready、close、size、keydown、char、mousedown、idle、tray）
 *
 *          窗口级 API：
 *            `cg_win_center` / `cg_win_set_pos` / `cg_win_set_size` / `cg_win_show` /
 *            `cg_win_set_topmost` / `cg_win_set_state` / `cg_win_set_icon` / `cg_cancel_close` /
 *            `cg_app_info` / `cg_tray_action` / `cg_set_timer` / `cg_app_has_window`
 *
 *          想深入看哪一课：
 *            - 按钮、标签、容器、状态栏            → 入门 01 · 基础与容器
 *            - 输入框、复选、下拉、日期、月历        → 入门 02 · 输入与选择
 *            - 滑块、进度条、圆环、仪表盘、状态灯    → 入门 03 · 数值与指示
 *            - 标签页、树、表格、终端、图表          → 入门 04 · 数据与日志
 *            - 菜单、工具栏、画板、图片、网页        → 入门 05 · 命令与图形
 *
 *          编译：在项目根跑 `build-app.bat lesson06_window`
 *          自检：`examples\lesson06_window\lesson06_window.exe --selftest examples\lesson06_window\lesson06_window.cgui`
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 事件流水 */

static int s_idle;      /**< idle 事件触发过多少次（消息队列空着就发，很频繁） */
static int s_lines;     /**< 已经往列表里写过多少行 */

/**
 * @brief 往"事件流水"列表里追加一行，同时更新状态栏中段
 * @param app 所属应用
 * @param fmt 格式串，其后是它的参数
 * @return void 无返回值
 * @note 列表是"只增不减"的窗口事件流水：滚到最后一行用 `cg_widget_list_scroll_bottom()`，
 *       否则新行会加在下面看不见的地方。
 */
static void logev(cg_app_t *app, const char *fmt, ...)
{
    char    buf[200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    s_lines += 1;
    cg_widget_list_addf(cg_find(app, "l6_lst_evt"), "%02d  %s", s_lines, buf);
    cg_widget_list_scroll_bottom(cg_find(app, "l6_lst_evt"));
    cg_status_set(cg_find(app, "l6_sb_main"), 1, buf);
}

/* ------------------------------------------------------------ 窗口事件 */

/**
 * @brief 窗口就绪（控件都建好、还没显示）
 * @param self 窗口伪控件（`cg_app_of(self)` 拿应用）
 * @param ev ready
 * @param user 未使用
 * @return void 无返回值
 * @note 读程序信息就放在这里：`cg_app_info()` 的键见 `docs\CGUI-FORMAT.md` 第 1.2 节，
 *       取不到返回 `NULL`。这些值来自 `.cgui` 的 `app.info`，编译时落进 exe 版本信息。
 */
static void on_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      line[200];

    (void)ev;
    (void)user;

    snprintf(line, sizeof(line), "程序名称：%s", cg_app_info(app, "product"));
    cg_widget_set_text(cg_find(app, "l6_lab_info_product"), line);
    snprintf(line, sizeof(line), "版本：%s（构建 %s）",
             cg_app_info(app, "version"), cg_app_info(app, "build"));
    cg_widget_set_text(cg_find(app, "l6_lab_info_version"), line);
    snprintf(line, sizeof(line), "作者：%s", cg_app_info(app, "author"));
    cg_widget_set_text(cg_find(app, "l6_lab_info_author"), line);
    snprintf(line, sizeof(line), "单位：%s   %s",
             cg_app_info(app, "company"), cg_app_info(app, "copyright"));
    cg_widget_set_text(cg_find(app, "l6_lab_info_company"), line);

    logev(app, "ready：窗口与控件都建好了（这里适合做启动初始化）");
    logev(app, "提示：拖窗口边框会发 size，敲键盘会发 keydown / char，点客户区会发 mousedown");
}

/**
 * @brief 用户要关闭窗口：勾了"拦下这次关闭"就拦一次
 * @param self 窗口伪控件
 * @param ev close
 * @param user 未使用
 * @return void 无返回值
 * @note `cg_cancel_close(app)` 是唯一能拦下关闭的调用；**弹框前先问 `cg_app_has_window()`**，
 *       否则 `--selftest`（不开窗）会被消息框卡住 —— 这是本框架最容易踩的一条。
 */
static void on_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    logev(app, "close：用户点了关闭");

    if (!cg_widget_checked(cg_find(app, "l6_chk_ask"))) return;   /* 没勾"演示拦截"就正常退出 */
    if (!cg_app_has_window(app)) return;                       /* 自检模式：绝不弹框 */

    cg_message_box(self,
                   "已拦下这次关闭：cg_cancel_close() 生效。\n"
                   "取消勾选「演示：拦下这次关闭」再关窗口就能退出。",
                   "关闭被拦下", 0);
    cg_cancel_close(app);
    logev(app, "close → cg_cancel_close()：这次不关");
}

/**
 * @brief 窗口尺寸变了（客户区尺寸）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 未使用
 * @return void 无返回值
 * @note 对 `size` 事件，`cg_event_x()` 是**宽**、`cg_event_y()` 是**高**（逻辑像素）。
 */
static void on_win_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       w   = cg_event_x(self);
    int       h   = cg_event_y(self);

    (void)ev;
    (void)user;

    cg_status_setf(cg_find(app, "l6_sb_main"), 0, "窗口 %d×%d", w, h);
    logev(app, "size：客户区变成 %d×%d（没有布局引擎，控件不会自己重排）", w, h);
}

/**
 * @brief 窗口里按下按键
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 未使用
 * @return void 无返回值
 * @note `cg_event_key()` 是虚拟键码（`VK_*`，比如 F1 = 0x70）；
 *       `cg_event_keys()` 是功能键位或：1 = Ctrl、2 = Shift、4 = Alt。
 */
static void on_win_keydown(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app  = cg_app_of(self);
    int       vk   = cg_event_key(self);
    int       keys = cg_event_keys(self);

    (void)ev;
    (void)user;

    logev(app, "keydown：键码 0x%02X%s%s%s", vk,
          (keys & 1) ? " +Ctrl" : "", (keys & 2) ? " +Shift" : "", (keys & 4) ? " +Alt" : "");
}

/**
 * @brief 窗口里输入了可打印字符
 * @param self 窗口伪控件
 * @param ev char
 * @param user 未使用
 * @return void 无返回值
 * @note 中文输入法上屏也走 `char`，字符码同样用 `cg_event_key()` 取（这里是 Unicode 码点）。
 */
static void on_win_char(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       ch  = cg_event_key(self);

    (void)ev;
    (void)user;

    if (ch >= 0x20 && ch < 0x7F) logev(app, "char：输入字符 '%c'", ch);
    else                          logev(app, "char：输入字符 U+%04X", ch);
}

/**
 * @brief 在窗口客户区按下鼠标
 * @param self 窗口伪控件
 * @param ev mousedown
 * @param user 未使用
 * @return void 无返回值
 * @note `cg_event_x/y()` 给的是**相对窗口客户区**的坐标（控件上的鼠标事件要减掉控件自己的位置，
 *       窗口事件本身就是客户区坐标，直接用）。
 */
static void on_win_mousedown(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    logev(app, "mousedown：客户区 (%d, %d)", cg_event_x(self), cg_event_y(self));
}

/**
 * @brief 消息队列空着（空闲）
 * @param self 窗口伪控件
 * @param ev idle
 * @param user 未使用
 * @return void 无返回值
 * @note **别在这里做耗时活**：它触发得非常频繁（队列一空就来），做重活会把界面拖死。
 *       这里只数个数、每 50 次刷一下状态栏；长时间的后台活应该用 `cg_set_timer()` 定周期做。
 */
static void on_app_idle(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    s_idle += 1;
    if (s_idle % 50 == 0) cg_status_setf(cg_find(app, "l6_sb_main"), 2, "空闲 %d 次", s_idle);
}

/**
 * @brief 托盘图标被点
 * @param self 窗口伪控件
 * @param ev tray
 * @param user 未使用
 * @return void 无返回值
 * @note 托盘图标由 `.cgui` 的 `app.tray: true` 建出来（用窗口图标）。
 *       `cg_tray_action()` 分辨动作：1 = 单击 / 2 = 双击 / 3 = 右键。
 */
static void on_app_tray(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *act = "?";

    (void)ev;
    (void)user;

    switch (cg_tray_action(self)) {
    case 1: act = "单击"; break;
    case 2: act = "双击"; break;
    case 3: act = "右键"; break;
    default: break;
    }
    logev(app, "tray：托盘图标被%s（下面可以自己弹菜单、或者 cg_win_show 拉回窗口）", act);
}

/* ------------------------------------------------------------ 窗口操作按钮 */

/**
 * @brief 「关于」按钮：把程序信息拼成文本弹出来
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_about_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      text[400];

    (void)ev;
    (void)user;

    snprintf(text, sizeof(text),
             "%s\n版本 %s（构建 %s）\n%s\n%s\n联系：%s",
             cg_app_info(app, "product"), cg_app_info(app, "version"),
             cg_app_info(app, "build"), cg_app_info(app, "author"),
             cg_app_info(app, "copyright"), cg_app_info(app, "email"));

    if (!cg_app_has_window(app)) return;         /* 自检时不弹框 */
    cg_message_box(self, text, "关于", 0);       /* 0 = 信息图标（1 警告 / 2 错误 / 3 问号） */
    logev(app, "关于：程序信息来自 .cgui 的 app.info（编译进 exe 版本信息）");
}

/**
 * @brief 「窗口居中」按钮：按当前显示器工作区居中
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_center_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_win_center(app);
    logev(app, "cg_win_center()：已按当前显示器工作区居中");
}

/**
 * @brief 「总在最前」按钮：开关置顶
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 开关状态记在 `user` 上（`cg_widget_set_user()` 挂的业务数据），按钮文字跟着改。
 */
static void on_btn_topmost_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       on  = (int)(long long)user;

    (void)ev;

    on = !on;
    cg_win_set_topmost(app, on);
    cg_widget_set_text(self, on ? "总在最前：开" : "总在最前：关");
    cg_widget_set_user(self, (void *)(long long)on);
    logev(app, "cg_win_set_topmost(%d)：和 .cgui 的 app.topmost 字段等效", on);
}

/**
 * @brief 1 秒到点：把窗口重新显示出来（定时器回调）
 * @param user 挂进去的应用指针
 * @return void 无返回值
 */
static void show_again(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    cg_win_show(app, 1);
    logev(app, "show：窗口又被显示出来了");
}

/**
 * @brief 「隐藏 1 秒再显示」按钮：演示显示 / 隐藏事件
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 * @note 定时器是**一次性**的：到点触发后就回收了，不会自动重挂 —— 周期性任务要自己重挂
 */
static void on_btn_hide_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    logev(app, "hide：窗口马上隐藏，1 秒后由 cg_set_timer 拉回来");
    cg_win_show(app, 0);
    cg_set_timer(app, 1000, show_again, app);
}

/* ------------------------------------------------------------ 绑定表与入口 */

/** `.cgui` 里出现的函数名 -> C 函数；漏一个 `--selftest` 会报"未登记" */
const cg_binding_t l6_bindings[] = {
    CG_BIND("l6_on_btn_about_click",  on_btn_about_click),
    CG_BIND("l6_on_btn_center_click", on_btn_center_click),
    CG_BIND("l6_on_btn_topmost_click", on_btn_topmost_click),
    CG_BIND("l6_on_btn_hide_click",   on_btn_hide_click),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */

/**
 * @brief lesson06_window 套件模块初始化（原工程没有 on_ready；初值原在 main() 里，由转换器搬来）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void l6_init(cg_app_t *app, void *user)
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
void l6_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_app_ready(self, ev, user);
}

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_app_close(self, ev, user);
}

/**
 * @brief 窗口事件 size 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_win_size(self, ev, user);
}

/**
 * @brief 窗口事件 keydown 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_keydown(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_win_keydown(self, ev, user);
}

/**
 * @brief 窗口事件 char 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev char
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_char(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_win_char(self, ev, user);
}

/**
 * @brief 窗口事件 mousedown 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev mousedown
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_mousedown(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_win_mousedown(self, ev, user);
}

/**
 * @brief 窗口事件 idle 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev idle
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_idle(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_app_idle(self, ev, user);
}

/**
 * @brief 窗口事件 tray 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev tray
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void l6_app_tray(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_app_tray(self, ev, user);
}
