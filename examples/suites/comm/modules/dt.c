/**
 * @file    dt.c
 * @brief   darkterm —— 套件模块（原 examples\darkterm 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 dt_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 dt_bindings、原 on_ready 变成 dt_init、
 *          窗口事件改成直连转发 dt_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   深色主题示例：app.dark 默认深色 + 顶层命名样式表 + cg_theme_set_dark 运行时切换
 * @details 这个示例只讲**深色**这一件事，所以界面本身不复杂：一个日志视图（只读多行 edit）
 *          + 几个典型控件（按钮 / 开关 / 进度条 / 圆环 / 状态灯 / 徽章 / 输入框）。
 *
 *          **一、深色是"默认"而不是"开关"。** `.cgui` 的 app 里写了 `"dark": true`，
 *          所以程序一打开就是深色。它走的不是"给每个控件刷一遍颜色"，而是换掉运行时那套
 *          **兜底配色**（窗口底色、自绘面板的"背后色"、没写颜色的文字色）—— 见
 *          docs/CGUI-FORMAT.md 的 1.1.1。想跟系统走就写 `"dark": "auto"`。
 *
 *          **二、配色写在顶层 `styles{}` 里，不散落在控件上。** 深色不是一个按键按下去
 *          全都自动变好看：不少控件不跟随主题，所以"为深色写的界面"必须**自己把颜色写清楚**，
 *          于是就需要一个地方集中放这套色（改一行就能统一调）：
 *
 *            - `panel`（深底 #1A191F + 浅字 #E8E2F0）：两个侧栏分组；
 *            - `oncard`（浅字 #CFC7DB）：深色面板上的标签；
 *            - `log`（深底 #0E1116 + 浅绿字 #CDE8C6）：中间那个日志视图；
 *            - `input`（深底浅字）：底部命令输入框；
 *            - `primary` / `ghost`（按钮填充色写死为深紫 / 深灰）：按钮的填充色**不跟随
 *              主题**，不写就是 M3 浅紫，深底上是一块亮紫；
 *            - `chip`（浅绿底 + 近黑字）：自绘徽章 —— "写了 bg/fg 就按自己的来"这条规则
 *              在深色下同样成立（写死就写死）。
 *
 *          控件那边三种引用形态都用到了：`"style": "panel"`（整份取用）、
 *          `"style": { "use": "panel", "bg": "#121117" }`（命名 + 局部覆盖，见日志分组）、
 *          以及纯内联对象（状态灯的亮色、进度条的填充色只有一个字段，不值得起名字）。
 *
 *          **三、运行时切换是本示例最值得看的地方**（bind 在右上角那个按钮上）：
 *            cg_theme_set_dark(app, !cg_theme_is_dark(app));
 *          它**立即重画**，语义与 `app.dark` 完全一致。切到浅色时你会看到：
 *            - 跟着变的：窗口底色，以及**没写 fg 的 label**（标题 / 状态行 / 底部两行说明）
 *              从浅字变深字；
 *            - 不跟着变的：写死了颜色的深色面板、日志视图、按钮、输入框、徽章。
 *          这不是 bug，是"深色只改默认值"的直接结果（CGUI-FORMAT 1.1.1 那条）。
 *
 *          **四、几处"看起来不对、其实是运行时边界"的地方，界面上都留了说明**：
 *            - `progress` 的**槽是皮肤里写死的浅色**（填充色能改：给 style.fg 就是填充色），
 *              深底下会看到一条浅槽；同族的 `progressring` 轨道用"背后色"，**反而跟随**；
 *            - `switch` / `checkbox` 的填充是 M3 紫（皮肤写死），只有标签文字跟着主题；
 *              **标签文字当天修好了一处**：原先 checkbox / radio 的标签是写死的深色
 *              （`cg_skin.c` 里与开关那处的常量差最后一位：0x1C1B1F / 0x1D1B20），深底上看不见 ——
 *              现在两处共用 `sk_default_text()`，按常量 grep 也搜不全的坑一并收口；
 *            - 本示例的日志视图**用自己写了颜色的只读多行 edit**，起因是当时 `terminal` 的
 *              **行色是给浅底写的**（RX 深绿 / TX 深蓝 / SYS 灰，深底上 RX/TX 对比度只有 2.6:1
 *              ⇒ 看不清）。**这一条当天也修了**：terminal 的行色现在分浅底 / 深底两套
 *              （`runtime/src/win32/cg_win_sync.c` 的 `term_col()`），深色下自动用亮色 ⇒
 *              **新工程可以直接用 terminal**。本示例的多行 edit 保留：它正好演示
 *              "想完全自己控制配色"的那一种写法；
 *            - `listbox` / `table` / `tree` / `tabs` / 下拉列表 / 状态栏是系统经典控件，
 *              公开接口改不动它们的主题 ⇒ 这个示例**一个都没用**，免得深色界面里摆一块白。
 *
 *          工程上的两点注意：
 *            1. 所有回调第一件事都是 `cg_app_has_window()`：`--selftest` 会**合成触发每个
 *               回调**，那时没有窗口、没有原生控件，碰界面 / 弹对话框就会把自检搞挂；
 *            2. 日志视图是普通多行 edit：文本得自己攒（这里用和 serialterm 同一套环形缓冲），
 *               追加后按需 `cg_edit_scroll_end()` 滚到底 —— 没有 terminal 那种"自动滚 + 按行
 *               着色 + 行数上限"的托管。
 *
 *          编译：在项目根跑 build-app.bat darkterm
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

/** 日志视图最多留多少字符（环形：超了从最老的地方丢） */
#define LOG_CAP 32000

/** 业务状态：日志文本 + 自动滚动开关 */
typedef struct {
    int  autoscroll;              /**< 追加后是否滚到底（.cgui 里那个开关） */
    int  len;                     /**< log_buf 里当前有效长度 */
    char buf[LOG_CAP + 1];        /**< 日志视图的内容（环形，超了丢最老） */
} biz_t;

static biz_t s_biz;

/* ------------------------------------------------------------ 日志视图 */

/**
 * @brief 往日志视图追加一段文本（超容量从最老处丢），并按需滚到底
 * @param app 所属应用
 * @param data 要追加的字节（UTF-8）
 * @param n 字节数
 * @return void
 * @note 多行 edit 没有 terminal 那种"自动滚到底"：set_text 会把视图滚回顶部，
 *       所以后面必须跟一个 cg_edit_scroll_end 把新内容露出来（除非用户关了自动滚动）。
 */
static void log_append(cg_app_t *app, const char *data, int n)
{
    cg_widget_t *edt = cg_find(app, "edt_log");

    if (n <= 0) return;
    if (s_biz.len + n > LOG_CAP) {                  /* 环形：装不下就丢最老的那截 */
        int drop = s_biz.len + n - LOG_CAP;

        if (drop >= s_biz.len) {
            s_biz.len = 0;
        } else {
            memmove(s_biz.buf, s_biz.buf + drop, (size_t)(s_biz.len - drop));
            s_biz.len -= drop;
        }
    }
    memcpy(s_biz.buf + s_biz.len, data, (size_t)n);
    s_biz.len += n;
    s_biz.buf[s_biz.len] = '\0';

    if (edt) {
        cg_widget_set_text(edt, s_biz.buf);
        if (s_biz.autoscroll) cg_edit_scroll_end(edt);
    }
}

/**
 * @brief 追加一行日志（前面带方向记号，和 terminal 的 RX/TX/-- 一个观感）
 * @param app 所属应用
 * @param tag 行首记号（"RX " / "TX " / "-- "）
 * @param text 正文（UTF-8）
 * @return void
 */
static void log_line(cg_app_t *app, const char *tag, const char *text)
{
    char tmp[2048];
    int  n = snprintf(tmp, sizeof(tmp), "%s%s\n", tag, text);

    if (n < 0) return;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    log_append(app, tmp, n);
}

/* ------------------------------------------------------------ 主题 */

/**
 * @brief 把"当前是什么主题"刷到状态行与切换按钮上
 * @param app 所属应用
 * @return void
 * @note 界面文字要跟得上状态，否则按钮点完还写着"切换到浅色"，人会以为没生效。
 */
static void sync_theme_ui(cg_app_t *app)
{
    int dark = cg_theme_is_dark(app);

    cg_widget_set_text(cg_find(app, "btn_theme"),
                       dark ? "切换到浅色主题" : "切换到深色主题");
    cg_widget_set_text(cg_find(app, "lab_mode"),
                       dark ? "当前：深色（app.dark: true）"
                            : "当前：浅色（运行时用 cg_theme_set_dark 切的）");
}

/**
 * @brief 「切换主题」按钮：运行时在深色 / 浅色之间来回切
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 这是整个示例的重点。`cg_theme_set_dark()` 会立即重画：窗口底色、自绘面板的
 *       "背后色"、没写颜色控件的字色一起换；写死了 fg/bg 的控件**保持原样** ——
 *       那条边界就是本示例想让人看见的东西。
 */
static void on_theme_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app  = cg_app_of(self);
    int       dark;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;      /* --selftest：没有窗口，别动界面 */

    dark = !cg_theme_is_dark(app);
    cg_theme_set_dark(app, dark);             /* 1 = 深色，0 = 浅色；语义与 app.dark 一致 */
    sync_theme_ui(app);
    log_line(app, "-- ", dark ? "已切到深色：cg_theme_set_dark(app, 1)"
                              : "已切到浅色：cg_theme_set_dark(app, 0)，写死了颜色的控件不会跟着变。");
}

/* ------------------------------------------------------------ 日志视图的按钮 */

/**
 * @brief 「清空日志」按钮：丢掉缓冲与视图内容
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    s_biz.len    = 0;
    s_biz.buf[0] = '\0';
    cg_widget_set_text(cg_find(app, "edt_log"), "");
    log_line(app, "-- ", "日志已清空（缓冲与视图一起丢）。");
}

/**
 * @brief 「追加几行」按钮：往日志视图里塞几行演示内容
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 多行 edit 只有一个文字色（styles.log 的浅绿），所以 RX / TX 只靠行首记号区分 ——
 *       要按方向上色就得用 terminal（它的行色现在分浅底 / 深底两套，深色下自动用亮色，见文件头说明）。
 */
static void on_fill_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    log_line(app, "RX ", "OK 19200 8N1");
    log_line(app, "TX ", "AT+VERSION");
    log_line(app, "-- ", "上面两行按 RX（收到）/ TX（发出）记，靠行首记号区分方向。");
}

/**
 * @brief 「自动滚动到底」开关：追加日志后要不要滚到最新一行
 * @param self 开关控件
 * @param ev change
 * @param user 未使用
 * @return void
 */
static void on_scroll_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    s_biz.autoscroll = cg_widget_checked(self);
    log_line(app, "-- ", s_biz.autoscroll ? "自动滚动：开（新内容出现时滚到底）。"
                                          : "自动滚动：关（新内容还在，只是视图不跟着滚）。");
}

/**
 * @brief 「保存日志」按钮：把日志视图存成 UTF-8 文本文件
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 * @note 自检模式下绝不能弹"另存为"，所以先问 cg_app_has_window()。
 */
static void on_save_log_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    path[0] = '\0';
    if (!cg_save_file_dialog(self, "保存日志", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             "txt", path, sizeof(path))) {
        return;
    }
    if (cg_file_write(path, s_biz.buf) != 0) {
        log_line(app, "-- ", "日志没写出去（路径不可写或被占用）。");
        return;
    }
    {
        char *line = cg_str_format("日志（%d 字符）已存到 %s", s_biz.len, path);

        if (line) {
            log_line(app, "-- ", line);
            cg_free(line);
        }
    }
}

/* ------------------------------------------------------------ 命令输入 */

/**
 * @brief 发送输入框里的内容：按 TX 记一行
 * @param app 所属应用
 * @return void
 * @note 这里**不发真数据** —— 示例只演示深色，命令只是回显，免得没接硬件时让人以为坏了。
 */
static void do_send(cg_app_t *app)
{
    cg_widget_t *edt  = cg_find(app, "edt_cmd");
    const char  *text = cg_widget_text(edt);

    if (!text || !*text) {
        log_line(app, "-- ", "输入框是空的，没东西可发。");
        return;
    }
    log_line(app, "TX ", text);
    cg_widget_set_text(edt, "");              /* text 已经用完了，再清空，避免悬空 */
}

/**
 * @brief 「发送」按钮
 * @param self 触发的按钮
 * @param ev click
 * @param user 未使用
 * @return void
 */
static void on_send_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;
    do_send(app);
}

/**
 * @brief 输入框里按回车（等价于点「发送」）
 * @param self 输入框
 * @param ev submit
 * @param user 未使用
 * @return void
 */
static void on_cmd_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;
    do_send(app);
}

/* ------------------------------------------------------------ 启动 */

/** 事件名 -> C 函数 的登记表（右边这些名字写在 darkterm.cgui 里） */
const cg_binding_t dt_bindings[] = {
    CG_BIND("dt_on_theme_click",   on_theme_click),
    CG_BIND("dt_on_clear_click",   on_clear_click),
    CG_BIND("dt_on_fill_click",    on_fill_click),
    CG_BIND("dt_on_scroll_change", on_scroll_change),
    CG_BIND("dt_on_save_log_click", on_save_log_click),
    CG_BIND("dt_on_send_click",    on_send_click),
    CG_BIND("dt_on_cmd_submit",    on_cmd_submit),
    CG_BIND_END
};

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */

/**
 * @brief darkterm 套件模块初始化（原工程没有 on_ready；初值原在 main() 里，由转换器搬来）
 * @param app 当前应用
 * @param user 用户数据（本套件未用，透传自入口）
 * @return void 无返回值
 */
void dt_init(cg_app_t *app, void *user)
{
    (void)app;
    (void)user;
    /* 原 main() 里的工具级初值（转换器搬来：丢了会与 .cgui 的初始状态不一致） */
    /* 双击 exe 也能跑：自动去 exe 旁边找同名 .cgui */

    s_biz.autoscroll = 1;

}
