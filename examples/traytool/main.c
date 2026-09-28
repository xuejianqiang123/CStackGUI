/**
 * @file    main.c
 * @brief   traytool 示例：托盘常驻小工具 —— 托盘图标、关窗即最小化、全局快捷键、剪贴板、截图、深浅主题
 * @details 这个示例讲的是一类程序怎么活：**没有主窗口也一直在**。
 *          启动就建托盘图标；点窗口关闭只是藏进托盘（进程继续跑），
 *          真正退出走菜单动作 Ctrl+Shift+Q。
 *
 *          五条命令写在 traytool.cgui 的 actions 里（各带 accel 快捷键），
 *          C 侧**只有一个回调 on_action** 服务全部命令，靠 cg_action_name(self) 分辨是哪一个 ——
 *          "同一个 handler 服务多个动作"是这套 API 的常用姿势。
 *
 *          全程只 include cstackgui.h 与标准头：不碰 Windows 头，不用 malloc/free（只用 cg_free）。
 *          凡是需要窗口 / 托盘 / 对话框 / 截图的地方，一律先问 cg_app_has_window()，
 *          这样 --selftest（不开窗）也能跑干净。
 * 编译：在项目根跑 build-app.bat traytool
 * 自检：traytool.exe --selftest        （不开窗、不建托盘、不弹框）
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态 */

static struct {
    int visible;   /**< 1 = 窗口显示中、0 = 藏在托盘里 */
    int shots;     /**< 本次运行成功存下的 PNG 张数 */
} s_biz = { 1, 0 };

/* ------------------------------------------------------------ 小助手 */

/**
 * @brief 往日志列表框追加一行，同时刷状态栏第一段并写一份日志文件
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char         buf[512];
    va_list      va;
    cg_widget_t *lst = cg_find(app, "lst_log");
    cg_widget_t *sb  = cg_find(app, "sb_main");

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    if (lst) {
        cg_widget_list_add(lst, buf);            /* 追加一条 */
        cg_widget_list_scroll_bottom(lst);       /* 日志类控件的老规矩：新的始终看得见 */
    }
    if (sb) cg_status_set(sb, 0, buf);
    cg_log(CG_LOG_INFO, "%s", buf);              /* 用户报"点了没反应"时靠这份日志 */
}

/**
 * @brief 把「窗口状态 + 当前主题 + 截图张数」刷到标签与状态栏
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    cg_widget_t *lab = cg_find(app, "lab_state");
    cg_widget_t *sb  = cg_find(app, "sb_main");
    int          dark = cg_theme_is_dark(app);   /* 自检没有窗口也答得出 */

    if (lab) {
        char *t = cg_str_format("窗口：%s　主题：%s　截图：%d 张",
                                s_biz.visible ? "显示中" : "藏在托盘里",
                                dark ? "深色" : "浅色", s_biz.shots);
        /* 框架返回的是堆串，设文字只是拷一份（不接管所有权）—— 用完必须还 */
        cg_widget_set_text(lab, t ? t : "");
        cg_free(t);
    }
    if (sb) {
        cg_status_set(sb, 1, dark ? "主题：深色" : "主题：浅色");
        cg_status_setf(sb, 2, "截图 %d 张", s_biz.shots);
    }
}

/**
 * @brief 显示 / 隐藏主窗口（托盘左键单击与快捷键 Ctrl+Shift+H 共用这条路）
 * @param app 应用
 * @param show 1 = 显示并置顶、0 = 藏进托盘
 * @return void 无返回值
 */
static void show_window(cg_app_t *app, int show)
{
    if (!cg_app_has_window(app)) return;    /* 自检没有窗口，这一类调用一律不发 */
    cg_win_show(app, show);
    cg_win_set_topmost(app, show);          /* 从托盘叫回来时别被别的窗口盖着 */
    s_biz.visible = show;
    refresh_status(app);
    log_line(app, show ? "窗口已显示（并置顶）" : "窗口已隐藏到托盘，进程还在跑");
}

/**
 * @brief 弹一个托盘气泡（自检 / 没有托盘时静默跳过）
 * @param app 应用
 * @param title 气泡标题
 * @param text 气泡正文
 * @return void 无返回值
 */
static void tray_hint(cg_app_t *app, const char *title, const char *text)
{
    if (!cg_app_has_window(app)) return;
    cg_tray_balloon(app, title, text);
}

/**
 * @brief 深色 / 浅色来回切一次，并把结果写进日志
 * @param app 应用
 * @return void 无返回值
 * @note 只改**默认配色**：控件自己写了前景 / 底色的，仍按它自己的来
 */
static void toggle_dark(cg_app_t *app)
{
    int dark = cg_theme_is_dark(app);

    if (!cg_theme_set_dark(app, !dark)) {          /* 1 = 深色、0 = 浅色；返回 0 = 失败 */
        log_line(app, "切主题失败：cg_theme_set_dark() 返回 0");
        return;
    }
    refresh_status(app);
    log_line(app, "主题已切到%s", dark ? "浅色" : "深色");
}

/**
 * @brief 把当前时间戳与一行运行统计写进剪贴板
 * @param app 应用
 * @return void 无返回值
 */
static void copy_time(cg_app_t *app)
{
    char *now = cg_time_now(NULL);   /* 格式传 NULL = "2026-09-27 12:00:00" 这种给人看的格式 */

    if (!now) {
        log_line(app, "取时间失败：cg_time_now() 返回 NULL");
        return;
    }
    {
        char *text = cg_str_format("[traytool] %s　窗口：%s　主题：%s　截图：%d 张",
                                   now, s_biz.visible ? "显示中" : "托盘里",
                                   cg_theme_is_dark(app) ? "深色" : "浅色", s_biz.shots);
        if (text && cg_clip_set_text(text) == 0) log_line(app, "已写入剪贴板：%s", text);
        else                                     log_line(app, "写剪贴板失败：cg_clip_set_text() 返回非 0");
        cg_free(text);
    }
    cg_free(now);
}

/**
 * @brief 把整个窗口（含标题栏）存成 PNG，落在用户数据目录里
 * @param app 应用
 * @return void 无返回值
 * @note 路径 = 用户数据目录 + shot_<时间戳>.png，目录由运行时保证已存在。
 *       失败只写日志、不弹框 —— 一个工具不该因为存图失败打断用户
 */
static void screenshot(cg_app_t *app)
{
    char *dir, *name, *path, *now;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式没有窗口，跳过截图");
        return;
    }
    dir = cg_app_data_dir();                    /* %LOCALAPPDATA%\<exe名>，自动建 */
    now = cg_time_now("%Y%m%d_%H%M%S");
    if (!dir || !now) {
        log_line(app, "截图失败：拿不到用户数据目录或时间");
        cg_free(dir);
        cg_free(now);
        return;
    }
    name = cg_str_format("shot_%s.png", now);
    path = name ? cg_path_join(dir, name) : NULL;
    if (!path || !cg_win_screenshot(app, path)) {
        log_line(app, "截图失败：cg_win_screenshot() 返回 0（GDI+ 起不来或写盘失败）");
    } else {
        s_biz.shots += 1;
        log_line(app, "已存图：%s", path);
    }
    cg_free(path);
    cg_free(name);
    cg_free(now);
    cg_free(dir);
    refresh_status(app);
}

/* ------------------------------------------------------------ 启动钩子 */

/**
 * @brief 启动钩子：建托盘图标、把初始状态画到界面上
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note **自检不开窗、也没有托盘**，所以这一段包在"有没有窗口"里 ——
 *       自检时去建托盘图标只会失败并弹框
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;

    if (cg_app_has_window(app)) {
        /* 图标路径传 NULL = 用窗口图标；悬停提示传 NULL = 用窗口标题 */
        cg_tray_set(app, NULL, "CStackGUI 托盘小工具：左键显示 / 隐藏，右键拉出窗口");
        log_line(app, "托盘图标已建好；关窗 = 最小化到托盘，退出走 Ctrl+Shift+Q");
        tray_hint(app, "traytool 已启动", "关掉窗口程序还在托盘里；真正退出请用菜单或 Ctrl+Shift+Q。");
    } else {
        log_line(app, "自检模式：跳过托盘与窗口相关的初始化");
    }
    refresh_status(app);
}

/* ------------------------------------------------------------ 窗口事件 */

/**
 * @brief 关闭前：不是退出，而是把窗口收进托盘
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 先拦下这次关闭、再自己隐藏窗口 —— 「关窗即最小化」的标准写法。
 *       **真正退出**只有两条路：菜单里的退出动作、或快捷键 Ctrl+Shift+Q
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_cancel_close(app);                    /* 只有关闭回调里调它才有意义 */
    if (!cg_app_has_window(app)) return;
    cg_win_show(app, 0);
    s_biz.visible = 0;
    refresh_status(app);
    log_line(app, "窗口被关到托盘里了（进程继续跑）");
    cg_tray_balloon(app, "还在后台待命",
                    "已最小化到托盘。左键点图标可再叫出来，退出请按 Ctrl+Shift+Q。");
}

/**
 * @brief 托盘图标被点：按托盘动作码分派
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 动作码：1 = 左键单击、2 = 左键双击、3 = 右键单击。
 *       右键**不会**自动弹菜单（框架没有"在光标处弹菜单"的接口），
 *       所以这里把窗口叫回前台，让用户走菜单栏
 */
static void on_tray(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       act;

    (void)ev;
    (void)user;

    act = cg_tray_action(self);
    if (act == 1) {                 /* 左键单击：显示 / 隐藏来回切 */
        show_window(app, !s_biz.visible);
    } else if (act == 2) {          /* 左键双击：强行叫回来并置顶 */
        show_window(app, 1);
        tray_hint(app, "traytool", "窗口叫回来了，随时可以再关到托盘。");
    } else if (act == 3) {          /* 右键：拉出窗口走菜单 */
        show_window(app, 1);
        log_line(app, "右键托盘：把窗口叫到前台，请从菜单栏操作命令");
    }
}

/**
 * @brief 窗口被显示：同步状态（从托盘回来时也走这里）
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_show(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_biz.visible = 1;
    refresh_status(cg_app_of(self));
}

/**
 * @brief 窗口被隐藏：同步状态
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_hide(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_biz.visible = 0;
    refresh_status(cg_app_of(self));
}

/* ------------------------------------------------------------ 菜单回调 */

/**
 * @brief 菜单点击：处理**没走动作**的那几项（动作项的点击不会到这里）
 * @param self 菜单控件
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 * @note 自检会合成一次点击，那时没有"刚点的项"（取回来是 NULL），开头就返回
 */
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_menu_last_id(self);

    (void)ev;
    (void)user;
    if (!id) return;

    if (strcmp(id, "m_clear") == 0) {
        cg_widget_t *lst = cg_find(app, "lst_log");
        if (lst) cg_widget_list_clear(lst);
        log_line(app, "日志已清空");
    } else if (strcmp(id, "m_balloon") == 0) {
        tray_hint(app, "托盘气泡", "这条是托盘气泡接口弹出来的。");
        log_line(app, "弹了一个托盘气泡（自检下跳过）");
    } else if (strcmp(id, "m_about") == 0) {
        char *text = cg_str_format("%s\n版本 %s\n\n%s",
                                   cg_app_info(app, "product"),
                                   cg_app_info(app, "version"),
                                   cg_app_info(app, "desc"));
        log_line(app, "关于：%s", text ? text : "");   /* 日志里留一份，自检下也不弹框 */
        if (cg_app_has_window(app)) cg_message_box(self, text, "关于", 0);   /* 0 = 信息图标 */
        cg_free(text);
    }
}

/* ------------------------------------------------------------ 动作回调
 * 签名与事件回调完全一样，但有两个固定值：self 恒为**主窗口伪控件**（不是菜单项、不是按钮），
 * 事件恒为 click。所以拿应用用 cg_app_of(self)、拿控件用 cg_find()；
 * 一个回调挂了多个动作时，用 cg_action_name(self) 问是哪一个。
 */

/**
 * @brief 动作回调：**一个 handler 服务五条命令**，靠"最近激活的动作名"分派
 * @param self 主窗口伪控件
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_action(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    const char *name = cg_action_name(self);

    (void)ev;
    (void)user;
    if (!name) return;                              /* 自检里拿不到动作名时直接走开 */

    if (strcmp(name, "toggle_win") == 0)        show_window(app, !s_biz.visible);
    else if (strcmp(name, "toggle_dark") == 0)  toggle_dark(app);
    else if (strcmp(name, "copy_time") == 0)    copy_time(app);
    else if (strcmp(name, "shot") == 0)         screenshot(app);
    else if (strcmp(name, "quit") == 0) {
        log_line(app, "退出：先摘掉托盘图标，再关消息循环");
        if (cg_app_has_window(app)) cg_tray_remove(app);   /* 少这一句，托盘里会留一个死图标 */
        cg_quit(app);
    }
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_close",      on_close),
    CG_BIND("on_tray",       on_tray),
    CG_BIND("on_show",       on_show),
    CG_BIND("on_hide",       on_hide),
    CG_BIND("on_menu_click", on_menu_click),
    CG_BIND("on_action",     on_action),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int           selftest = (argc > 1 && strcmp(argv[1], "--selftest") == 0);

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.cgui_path        = (argc > (selftest ? 2 : 1))
                                ? argv[selftest ? 2 : 1]
                                : "traytool.cgui";
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动在 exe 旁边找 .cgui */
    desc.on_ready         = on_ready;

    if (selftest) return cg_app_selftest(&desc);

    cg_log(CG_LOG_INFO, "traytool 启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
