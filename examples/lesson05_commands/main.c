/**
 * @file    main.c
 * @brief   入门 05 · 命令与图形 —— 菜单栏 / 工具栏 / 菜单按钮 / 取色 / 画板 / 外形框 / 图片 / 网页浏览
 * @details 这一课讲两件事，界面在 `lesson05_commands.cgui`：
 *          1. **命令从哪来**：同一个动作可以从三处发起 —— 菜单栏（`menu`）、工具栏（`toolbar`）、
 *             菜单按钮（`menubutton`）。本课刻意让三处都调**同一个** `do_command()`：
 *             一个动作只有一处实现，状态栏里会写明"这次是从哪儿点的"。
 *          2. **图形与媒体**：`canvas` 上用 `cg_canvas_*` 画图（命令会被录下来，重绘时按序回放）、
 *             `shape` 三种外形、`image` 放图、`colorpicker` 取色、`browser` 开网页。
 *
 *          本课覆盖的控件与关键 API：
 *            - `menu`        菜单栏：项写在 `nodes`（`"-"` = 分隔线，项上可带 `id`）；
 *                            点了哪一项用 `cg_menu_last_id(self)` 认（写死 id 比按文字判断稳）
 *            - `toolbar`     工具栏：`items` 是按钮文字；点中第几项用 `cg_widget_value(self)` 读（0 起）
 *            - `menubutton`  长得像按钮的菜单：选中项同样用 `cg_menu_last_id()`
 *            - `colorpicker` 当前颜色在 `value`（`0xRRGGBB`）；点它弹系统取色器，改完派发 `change`
 *            - `canvas`      `cg_canvas_clear/set_color/set_line_width/line/rect/ellipse`；
 *                            鼠标位置用 `cg_event_x/y()`（**相对窗口客户区**，要减掉控件自己的 x/y）
 *            - `shape`       `format` = `rect` / `roundrect` / `ellipse`，颜色走 `style.bg`
 *            - `image`       `src` 是图片路径；换图 `cg_widget_set_image()`
 *            - `browser`     实验性：靠系统 WebView2，`cg_browser_navigate(w, "网址或本地 html")`
 *
 *          想深入看哪一课：
 *            - 按钮、标签、链接、开关、徽章、容器  → 入门 01 · 基础与容器
 *            - 输入框、复选框、下拉框、日期、月历  → 入门 02 · 输入与选择
 *            - 滑块、进度条、圆环、仪表盘、状态灯  → 入门 03 · 数值与指示
 *            - 列表、树、表格、状态栏、终端、图表  → 入门 04 · 数据与日志
 *
 *          编译：在项目根跑 `build-app.bat lesson05_commands`
 *          自检：`examples\lesson05_commands\lesson05_commands.exe --selftest examples\lesson05_commands\lesson05_commands.cgui`
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 公用小工具 */

/**
 * @brief 往状态栏写一句话（本课所有回调的唯一出口）
 * @param app 所属应用（用 cg_app_of(self) 拿）
 * @param fmt 格式串，其后是它的参数
 * @return void 无返回值；超过 200 字节会被截断
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

/** 本课的业务状态：画笔颜色 + 已经画了几个图形 */
static unsigned s_ink    = 0x6750A4;
static int      s_shapes;

/* ------------------------------------------------------------ 命令表（本课的核心）

 * 菜单栏 / 工具栏 / 菜单按钮三处入口最后都调 do_command()：
 * 一个动作只有一处实现 —— 加一个命令只改这里，三处入口都不用动。 */

/** 本课支持的命令 */
typedef enum {
    CMD_NEW,      /**< 新建：清空画板 */
    CMD_CIRCLE,   /**< 画一个圆 */
    CMD_RECT,     /**< 画一个方 */
    CMD_CLEAR,    /**< 清空画板 */
    CMD_PAGE,     /**< 让 browser 载入示例页 */
    CMD_SIZE      /**< 把画板尺寸复制到剪贴板 */
} cmd_t;

/** 画板尺寸（画布画不下第 21 个图形时回到左上角继续） */
#define BOARD_W 400
#define BOARD_H 300

/**
 * @brief 清空画板并复位计数
 * @param app 所属应用
 * @return void 无返回值
 * @note `cg_canvas_clear()` 清掉"绘制命令记录"，之后重绘不会再把旧图形画回来。
 */
static void board_clear(cg_app_t *app)
{
    cg_canvas_clear(cg_find(app, "cv_board"));
    s_shapes = 0;
}

/**
 * @brief 画一个圆（第几个图形决定位置，横向排满一行换下一行）
 * @param app 所属应用
 * @param from 这次是从哪儿发起的（"菜单" / "工具栏" / "按钮"），只用于状态栏文案
 * @return void 无返回值
 * @note `cg_canvas_set_color()` 设的是**后续**图形的颜色，所以改画笔颜色只影响新画的。
 */
static void draw_circle(cg_app_t *app, const char *from)
{
    cg_widget_t *cv = cg_find(app, "cv_board");
    int          x, y;

    s_shapes += 1;
    x = 40 + ((s_shapes - 1) % 5) * 62;
    y = 40 + ((s_shapes - 1) / 5) * 62;
    cg_canvas_set_color(cv, s_ink);
    cg_canvas_set_line_width(cv, 3);
    cg_canvas_ellipse(cv, x, y, 56, 56, 0);          /* fill=0 描边 */
    say(app, "%s → 画圆：第 %d 个（颜色 #%06X）", from, s_shapes, s_ink);
}

/**
 * @brief 画一个方
 * @param app 所属应用
 * @param from 入口名（只用于状态栏文案）
 * @return void 无返回值
 */
static void draw_rect(cg_app_t *app, const char *from)
{
    cg_widget_t *cv = cg_find(app, "cv_board");
    int          x, y;

    s_shapes += 1;
    x = 40 + ((s_shapes - 1) % 5) * 62;
    y = 40 + ((s_shapes - 1) / 5) * 62;
    cg_canvas_set_color(cv, s_ink);
    cg_canvas_set_line_width(cv, 3);
    cg_canvas_rect(cv, x, y, 52, 52, 0);
    say(app, "%s → 画方：第 %d 个（颜色 #%06X）", from, s_shapes, s_ink);
}

/**
 * @brief 让 browser 控件打开一个"程序自己写的"网页
 * @param app 所属应用
 * @return void 无返回值
 * @note 这是 browser 最实用的用法：不需要联网 —— 先 `cg_file_write()` 把 html 写到
 *       用户数据目录（`cg_app_data_dir()`，每次开机都在、又不会污染程序目录），
 *       再 `cg_browser_navigate()` 打开这个本地路径。
 */
static void load_demo_page(cg_app_t *app)
{
    static const char k_html[] =
        "<!doctype html><meta charset=\"utf-8\"><title>示例页</title>"
        "<style>body{margin:0;font:14px/1.7 'Microsoft YaHei UI',sans-serif;color:#1d1b20}"
        "h1{margin:0;padding:16px 20px;font-size:16px;background:#6750a4;color:#fff}"
        "p{margin:16px 20px}</style>"
        "<h1>这一页是示例程序自己写的</h1>"
        "<p>先用 cg_file_write() 存到用户数据目录，<br>"
        "再用 cg_browser_navigate() 让浏览器控件打开它。</p>";
    char *dir  = cg_app_data_dir();
    char *path = dir ? cg_path_join(dir, "lesson05_demo.html") : NULL;

    /* `--selftest` 是"不开窗自己跑一遍回调"：它会把每个回调都合成触发一次，
     * 所以回调里**别留下真实副作用**（写文件、弹窗、连设备）。要区分就问它有没有窗口。 */
    if (!cg_app_has_window(app)) {
        say(app, "自检模式：跳过写示例页与打开网页");
        free(path);
        free(dir);
        return;
    }
    if (path && cg_file_write(path, k_html) == 0) {
        cg_browser_navigate(cg_find(app, "br_page"), path);
        say(app, "已让 browser 打开：%s", path);
    } else {
        say(app, "写示例页失败：检查用户数据目录能不能写");
    }
    free(path);
    free(dir);
}

/**
 * @brief 执行一条命令（菜单 / 工具栏 / 菜单按钮 / 按钮 四类入口共用）
 * @param app 所属应用
 * @param cmd 命令
 * @param from 入口名（"菜单" / "工具栏" / "菜单按钮" / "按钮"），写进状态栏便于对照
 * @return void 无返回值
 */
static void do_command(cg_app_t *app, cmd_t cmd, const char *from)
{
    switch (cmd) {
    case CMD_NEW:
        board_clear(app);
        say(app, "%s → 新建：画板已清空", from);
        break;
    case CMD_CIRCLE:
        draw_circle(app, from);
        break;
    case CMD_RECT:
        draw_rect(app, from);
        break;
    case CMD_CLEAR:
        board_clear(app);
        say(app, "%s → 清空：画板和计数都归零", from);
        break;
    case CMD_PAGE:
        load_demo_page(app);
        break;
    case CMD_SIZE: {
        char size[32];
        /* 控件的实际尺寸从控件上问，别把 .cgui 里的数字再抄一遍 */
        snprintf(size, sizeof(size), "%d x %d",
                 cg_widget_w(cg_find(app, "cv_board")),
                 cg_widget_h(cg_find(app, "cv_board")));
        cg_clip_set_text(size);
        say(app, "%s → 画板尺寸 %s 已复制到剪贴板", from, size);
        break;
    }
    }
}

/* ------------------------------------------------------------ 三处命令入口 */

/**
 * @brief 菜单栏被点
 * @param self 菜单控件
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 点了哪一项用 `cg_menu_last_id(self)` 认 —— 菜单项的 `id` 写在 `.cgui` 里；
 *       没写 id 的项只能用 `cg_menu_last_text()` 按文字判断，改文案就会失效。
 */
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_menu_last_id(self);

    (void)ev;
    (void)user;
    if (!id) return;
    if      (strcmp(id, "m_new") == 0)    do_command(cg_app_of(self), CMD_NEW,    "菜单");
    else if (strcmp(id, "m_circle") == 0) do_command(cg_app_of(self), CMD_CIRCLE, "菜单");
    else if (strcmp(id, "m_rect") == 0)   do_command(cg_app_of(self), CMD_RECT,   "菜单");
    else if (strcmp(id, "m_clear") == 0)  do_command(cg_app_of(self), CMD_CLEAR,  "菜单");
    else if (strcmp(id, "m_page") == 0)   do_command(cg_app_of(self), CMD_PAGE,   "菜单");
}

/**
 * @brief 工具栏被点
 * @param self 工具栏控件
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note 工具栏没有"项 id"这一说：点中第几项读 `cg_widget_value(self)`（0 起，和 `items` 顺序一致）。
 */
static void on_tb_cmd_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    int idx = cg_widget_value(self);

    (void)ev;
    (void)user;
    switch (idx) {
    case 0: do_command(cg_app_of(self), CMD_NEW,    "工具栏"); break;
    case 1: do_command(cg_app_of(self), CMD_CIRCLE, "工具栏"); break;
    case 2: do_command(cg_app_of(self), CMD_RECT,   "工具栏"); break;
    case 3: do_command(cg_app_of(self), CMD_CLEAR,  "工具栏"); break;
    default: break;                       /* -1：点在空白处 */
    }
}

/**
 * @brief 菜单按钮里的项被点
 * @param self 菜单按钮控件
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_mbtn_more_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_menu_last_id(self);

    (void)ev;
    (void)user;
    if (!id) return;
    if (strcmp(id, "m_page") == 0) do_command(cg_app_of(self), CMD_PAGE, "菜单按钮");
    else if (strcmp(id, "m_size") == 0) do_command(cg_app_of(self), CMD_SIZE, "菜单按钮");
}

/* ------------------------------------------------------------ 控件回调 */

/**
 * @brief 窗口事件：就绪
 * @param self 窗口伪控件
 * @param ev ready
 * @param user 未使用
 * @return void 无返回值
 */
static void on_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    /* 命令也能被程序发起：启动时先画两个图形，省得画板是空的（和用户点菜单是同一条路径） */
    draw_circle(app, "启动");
    draw_rect(app, "启动");
    say(app, "命令可以从菜单栏、工具栏、菜单按钮发起 —— 三处都走同一个实现。");
}

/**
 * @brief 画笔颜色变了
 * @param self 取色控件
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 * @note 颜色是非区间值（`0xRRGGBB`），所以只读 `cg_widget_value()`，
 *       **不要**用 `cg_widget_set_value()` 去写它（那条路会按 min/max 截断）。
 */
static void on_cp_ink_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_ink = (unsigned)cg_widget_value(self) & 0xFFFFFFu;
    say(cg_app_of(self), "画笔颜色改成 #%06X（只影响之后画的图形）", s_ink);
}

/**
 * @brief 「画圆」按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_draw_circle_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_command(cg_app_of(self), CMD_CIRCLE, "按钮");
}

/**
 * @brief 「画方」按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_draw_rect_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_command(cg_app_of(self), CMD_RECT, "按钮");
}

/**
 * @brief 「清空」按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_draw_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_command(cg_app_of(self), CMD_CLEAR, "按钮");
}

/**
 * @brief 在画板上点了一下：在点到的位置画一个小圆点
 * @param self 画板控件
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 * @note **坐标要减掉控件自己的位置**：`cg_event_x/y()` 给的是相对**窗口客户区**的坐标，
 *       而 `cg_canvas_*` 要的是相对控件左上角的坐标（差值用 `cg_widget_x/y()` 拿）。
 *       这个"点一下画一个点"的用法，做个取点工具 / 标注小工具正好。
 */
static void on_cv_board_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    int x = cg_event_x(self) - cg_widget_x(self);
    int y = cg_event_y(self) - cg_widget_y(self);

    (void)ev;
    (void)user;
    cg_canvas_set_color(self, s_ink);
    cg_canvas_ellipse(self, x - 4, y - 4, 8, 8, 1);      /* fill=1 实心 */
    say(cg_app_of(self), "在画板 (%d, %d) 点了一个点（颜色 #%06X）", x, y, s_ink);
}

/**
 * @brief 「载入示例页」按钮被点
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_br_load_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    do_command(cg_app_of(self), CMD_PAGE, "按钮");
}

/* ------------------------------------------------------------ 绑定表与入口 */

/** `.cgui` 里出现的函数名 -> C 函数；漏一个 `--selftest` 会报"未登记" */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_app_ready",         on_app_ready),
    CG_BIND("on_menu_click",        on_menu_click),
    CG_BIND("on_tb_cmd_click",      on_tb_cmd_click),
    CG_BIND("on_mbtn_more_click",   on_mbtn_more_click),
    CG_BIND("on_cp_ink_change",     on_cp_ink_change),
    CG_BIND("on_btn_draw_circle_click", on_btn_draw_circle_click),
    CG_BIND("on_btn_draw_rect_click",   on_btn_draw_rect_click),
    CG_BIND("on_btn_draw_clear_click",  on_btn_draw_clear_click),
    CG_BIND("on_cv_board_click",    on_cv_board_click),
    CG_BIND("on_btn_br_load_click", on_btn_br_load_click),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson05_commands.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson05_commands.cgui";
    return cg_app_run(&desc);
}
