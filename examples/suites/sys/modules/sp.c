/**
 * @file    sp.c
 * @brief   syspanel —— 套件模块（原 examples\syspanel 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 sp_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 sp_bindings、原 on_ready 变成 sp_init、
 *          窗口事件改成直连转发 sp_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   示例 syspanel：系统信息与配置面板（只读为主，不接硬件）
 * @details 两个页签，各展示一类"界面之外"的基础能力：
 *
 *            系统信息页 —— 是否 64 位 / 系统版本 / 磁盘容量与占用 / 环境变量 /
 *                          进程存在性 / 串口枚举，全部只读；
 *            配置页     —— 读写 <用户数据目录>\syspanel.ini，用表格显示「节 / 键 / 值」，
 *                          支持读一项 / 写一项 / 全部重载，还能把表格内容送进剪贴板。
 *
 *          两条自检（--selftest）红线，本文件处处遵守：
 *            1. 就绪钩子里先问有没有窗口：不开窗就不写 INI、不枚举串口，只写一行日志说明跳过；
 *            2. 凡有副作用（写盘 / 写剪贴板）的回调也先问，免得自检留下垃圾数据。
 *
 * 界面在 syspanel.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat syspanel
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 内置示例配置 */

/** 内置示例配置：启动时若文件不存在就按这几项写默认值，重载时也照这份清单读回来 */
static const struct {
    const char *sec;   /**< 节名 */
    const char *key;   /**< 键名 */
    const char *def;   /**< 默认值 */
} k_cfg[] = {
    { "面板", "进程名",   "explorer.exe" },
    { "面板", "刷新间隔", "1000"         },
    { "串口", "端口",     "COM1"         },
    { "串口", "波特率",   "115200"       }
};

/** 内置示例配置的条目数 */
#define K_CFG_COUNT ((int)(sizeof k_cfg / sizeof k_cfg[0]))

/** 配置文件全路径：<用户数据目录>\syspanel.ini（就绪钩子里填一次） */
static char s_ini[1024];

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 往日志列表框追加一行，并镜像到 stdout 方便自检看时序
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char        buf[256];
    va_list     ap;
    cg_widget_t *lst = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (lst) {
        if (cg_widget_list_count(lst) > 400) cg_widget_list_clear(lst);   /* 别让它无限长 */
        cg_widget_list_add(lst, buf);
        cg_widget_list_scroll_bottom(lst);
    }
    printf("[log] %s\n", buf);
}

/**
 * @brief 把最近一次结果写进状态栏第 0 段
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void status_msg(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_status_set(cg_find(app, "sb_main"), 0, buf);
}

/**
 * @brief 把字节数格式化成好读的 "1.2 GB" / "345.1 MB" / "12 KB"
 * @param bytes 字节数
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void fmt_size(unsigned long long bytes, char *out, int cap)
{
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        snprintf(out, cap, "%.2f GB", (double)bytes / 1073741824.0);
    } else if (bytes >= 1024ull * 1024ull) {
        snprintf(out, cap, "%.1f MB", (double)bytes / 1048576.0);
    } else {
        snprintf(out, cap, "%llu KB", bytes / 1024ull);
    }
}

/* ------------------------------------------------------------ 系统信息页 */

/**
 * @brief 刷系统位数与可读版本串
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_system(cg_app_t *app)
{
    char *ver = cg_sys_version();

    cg_widget_set_text(cg_find(app, "lab_is64_v"),
                       cg_sys_is64() ? "是（64 位系统）" : "否（32 位系统）");
    cg_widget_set_text(cg_find(app, "lab_ver_v"), ver ? ver : "（读不到）");
    log_line(app, "系统：%s / %s",
             cg_sys_is64() ? "64 位" : "32 位", ver ? ver : "版本读不到");
    cg_free(ver);
}

/**
 * @brief 刷磁盘占用：对用户数据目录问剩余 / 总量，进度条显示已用百分比
 * @param app 应用
 * @return void 无返回值
 * @note 拿不到用户数据目录就退回当前目录；问不到容量时进度条与数字都写明原因
 */
static void refresh_disk(cg_app_t *app)
{
    char              *dir = cg_app_data_dir();
    const char        *probe = dir ? dir : ".";
    unsigned long long freeb = 0, totalb = 0;
    char               fbuf[32], tbuf[32], num[112];
    int                pct = 0;

    cg_widget_set_text(cg_find(app, "lab_disk_v"), probe);

    if (!cg_disk_space(probe, &freeb, &totalb)) {
        cg_widget_set_value(cg_find(app, "prg_disk"), 0);
        cg_widget_set_text(cg_find(app, "lab_disk_num"), "（问不到容量）");
        log_line(app, "磁盘：问不到 %s 的容量", probe);
        cg_free(dir);
        return;
    }

    if (totalb > 0) pct = (int)((totalb - freeb) * 100ull / totalb);
    fmt_size(freeb, fbuf, sizeof(fbuf));
    fmt_size(totalb, tbuf, sizeof(tbuf));
    snprintf(num, sizeof(num), "已用 %d%% · 剩余 %s / 共 %s", pct, fbuf, tbuf);

    cg_widget_set_value(cg_find(app, "prg_disk"), pct);
    cg_widget_set_text(cg_find(app, "lab_disk_num"), num);
    log_line(app, "磁盘 %s：占用 %d%%（剩余 %s / 共 %s）", probe, pct, fbuf, tbuf);
    cg_free(dir);
}

/**
 * @brief 刷环境变量：路径只显示前 60 字符，临时目录原样，CPU 个数按整数读
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_env(cg_app_t *app)
{
    cg_widget_t *lst  = cg_find(app, "lst_env");
    char        *path = cg_env("PATH");
    char        *temp = cg_env("TEMP");
    int          cpus = cg_env_int("NUMBER_OF_PROCESSORS", -1);

    if (lst) cg_widget_list_clear(lst);

    if (path) {
        char head[64];
        snprintf(head, sizeof(head), "%.60s", path);     /* 路径太长，只截前 60 个字符 */
        cg_widget_list_addf(lst, "PATH = %s%s", head, strlen(path) > 60 ? " …" : "");
    } else {
        cg_widget_list_add(lst, "PATH = （没有这个变量）");
    }
    cg_widget_list_addf(lst, "TEMP = %s", temp ? temp : "（没有这个变量）");
    cg_widget_list_addf(lst, "NUMBER_OF_PROCESSORS = %d", cpus);

    log_line(app, "环境变量：CPU %d 个，TEMP=%s", cpus, temp ? temp : "（无）");
    cg_free(path);
    cg_free(temp);
}

/**
 * @brief 刷进程存在性：查输入框里那个名字是否在跑，用状态灯 + 文字给结论
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_process(cg_app_t *app)
{
    cg_widget_t *led  = cg_find(app, "led_proc");
    const char  *name = cg_widget_text(cg_find(app, "edt_proc"));
    int          running;

    if (!name || !*name) {
        if (led) cg_led_set(led, 0);
        cg_widget_set_text(cg_find(app, "lab_proc_res"), "（请先填一个进程名）");
        return;
    }

    running = cg_process_running(name);
    if (led) {
        cg_led_set_color(led, running ? CG_RGB(0x1B, 0x6E, 0x33) : CG_RGB(0xC0, 0x30, 0x30));
        cg_led_set(led, running ? 1 : 0);
    }
    cg_widget_set_text(cg_find(app, "lab_proc_res"),
                       running ? "结论：正在运行" : "结论：没找到这个进程");
    log_line(app, "进程 %s：%s", name, running ? "在跑" : "没找到");
}

/**
 * @brief 刷串口列表：接口按换行分隔返回端口名，一行一个写进列表框
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_ports(cg_app_t *app)
{
    cg_widget_t *lst = cg_find(app, "lst_port");
    char         buf[1024];
    int          n, i = 0, count = 0;

    if (lst) cg_widget_list_clear(lst);
    n = cg_serial_list(buf, sizeof(buf));           /* 没有端口返回 0（不是错误） */
    if (n <= 0) {
        cg_widget_list_add(lst, "（没有发现串口）");
        log_line(app, "串口：一个也没枚举到");
        return;
    }

    while (i < n) {
        int j = i;
        while (j < n && buf[j] != '\n' && buf[j] != '\r') j++;
        if (j > i) {
            buf[j] = '\0';                          /* j <= n <= 缓冲上限，安全 */
            cg_widget_list_addf(lst, "%s", &buf[i]);
            count++;
        }
        i = j + 1;
    }
    log_line(app, "串口：枚举到 %d 个端口", count);
}

/* ------------------------------------------------------------ 配置页（INI） */

/**
 * @brief 首次运行时把内置默认配置写进配置文件
 * @param app 应用
 * @return void 无返回值
 */
static void write_defaults(cg_app_t *app)
{
    int i;

    for (i = 0; i < K_CFG_COUNT; i++)
        cg_ini_write(s_ini, k_cfg[i].sec, k_cfg[i].key, k_cfg[i].def);
    log_line(app, "首次运行：已写入 %d 项默认配置", K_CFG_COUNT);
}

/**
 * @brief 把内置清单里的几项从文件读回来填进表格，并逐条写日志
 * @param app 应用
 * @return void 无返回值
 */
static void reload_table(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_cfg");
    int          i;

    if (tbl) cg_table_clear(tbl);
    for (i = 0; i < K_CFG_COUNT; i++) {
        const char *cells[3];
        char       *v = cg_ini_read(s_ini, k_cfg[i].sec, k_cfg[i].key, NULL);

        cells[0] = k_cfg[i].sec;
        cells[1] = k_cfg[i].key;
        cells[2] = v ? v : "（没有）";
        if (tbl) cg_table_add_row(tbl, cells, 3);
        log_line(app, "读回 [%s] %s = %s", cells[0], cells[1], cells[2]);

        cg_free(v);
    }
    log_line(app, "表格重载：%d 行", tbl ? cg_table_row_count(tbl) : 0);
}

/**
 * @brief 「读一项」：用节 / 键两个输入框读一个键，值填进「值」输入框
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_cfg_read_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *sec = cg_widget_text(cg_find(app, "edt_sec"));
    const char *key = cg_widget_text(cg_find(app, "edt_key"));
    char       *v;

    (void)ev;
    (void)user;

    if (!sec || !*sec || !key || !*key) {
        status_msg(app, "节 / 键不能为空");
        log_line(app, "读一项失败：节或键为空");
        return;
    }

    v = cg_ini_read(s_ini, sec, key, NULL);
    cg_widget_set_text(cg_find(app, "edt_val"), v ? v : "");
    log_line(app, "读 [%s] %s = %s", sec, key, v ? v : "（没有）");
    status_msg(app, "读到 [%s] %s", sec, key);
    cg_free(v);
}

/**
 * @brief 「写一项」：把「值」写进 [节] 键，然后重载表格
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写盘是副作用：自检下直接跳过（仍写一行日志，回调照样算命中）
 */
static void on_btn_cfg_write_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *sec;
    const char *key;
    const char *val;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过写配置（不真写盘）");
        return;
    }
    if (!s_ini[0]) {
        status_msg(app, "配置文件路径不可用");
        return;
    }

    sec = cg_widget_text(cg_find(app, "edt_sec"));
    key = cg_widget_text(cg_find(app, "edt_key"));
    val = cg_widget_text(cg_find(app, "edt_val"));
    if (!sec || !*sec || !key || !*key) {
        status_msg(app, "节 / 键不能为空");
        log_line(app, "写一项失败：节或键为空");
        return;
    }

    if (cg_ini_write(s_ini, sec, key, val ? val : "") == 0) {
        log_line(app, "写入 [%s] %s = %s", sec, key, val ? val : "");
        status_msg(app, "已写入 [%s] %s", sec, key);
        reload_table(app);
    } else {
        log_line(app, "写入失败：[%s] %s", sec, key);
        status_msg(app, "写入失败");
    }
}

/**
 * @brief 「全部重载」：清表格后把文件里的几项重新读回来
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_cfg_reload_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "tbl_cfg");

    (void)ev;
    (void)user;

    reload_table(app);
    status_msg(app, "已重载 %d 行", tbl ? cg_table_row_count(tbl) : 0);
}

/**
 * @brief 「复制到剪贴板」：把表格内容拼成每行一条，送进剪贴板
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写剪贴板是副作用：自检下跳过，别把 CI 那台机器的剪贴板糊掉
 */
static void on_btn_cfg_copy_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "tbl_cfg");
    char         text[2048];
    int          rows, i, off = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过写剪贴板");
        return;
    }

    text[0] = '\0';
    rows = tbl ? cg_table_row_count(tbl) : 0;
    for (i = 0; i < rows && off < (int)sizeof(text) - 80; i++) {
        const char *a = cg_table_cell(tbl, i, 0);
        const char *b = cg_table_cell(tbl, i, 1);
        const char *c = cg_table_cell(tbl, i, 2);
        off += snprintf(text + off, sizeof(text) - (size_t)off, "[%s] %s = %s\r\n",
                        a ? a : "", b ? b : "", c ? c : "");
    }

    if (cg_clip_set_text(text) == 0) {
        log_line(app, "已把 %d 行配置复制到剪贴板", rows);
        status_msg(app, "已复制 %d 行到剪贴板", rows);
    } else {
        log_line(app, "写剪贴板失败");
        status_msg(app, "写剪贴板失败");
    }
}

/* ------------------------------------------------------------ 页签与刷新 */

/**
 * @brief 切页签：日志记一行，状态栏显示当前页名
 * @param self 标签页控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tab_main_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         page = cg_tabs_selected(self);
    const char *name = cg_widget_list_item(self, page);   /* 页签文字也是"列表项" */

    (void)ev;
    (void)user;

    log_line(app, "切到第 %d 页：%s", page + 1, name ? name : "?");
    status_msg(app, "当前页：%s", name ? name : "?");
}

/**
 * @brief 「检查」进程：只重算进程存在性（名字来自输入框）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_proc_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    const char *name;

    (void)ev;
    (void)user;

    refresh_process(app);
    name = cg_widget_text(cg_find(app, "edt_proc"));
    status_msg(app, "已检查进程 %s", name ? name : "");
}

/**
 * @brief 在进程名输入框里按回车 = 点「检查」
 * @param self 进程名输入框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_proc_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    on_btn_proc_click(self, CG_EVENT_CLICK, NULL);
}

/**
 * @brief 「刷新」：重算系统 / 磁盘 / 环境 / 进程 / 串口，并把本次刷新时间写进状态栏
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_refresh_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *sb  = cg_find(app, "sb_main");
    char        *now;

    (void)ev;
    (void)user;

    refresh_system(app);
    refresh_disk(app);
    refresh_env(app);
    refresh_process(app);
    refresh_ports(app);

    cg_status_set(sb, 1, "系统信息");
    now = cg_time_now(NULL);                       /* 传 NULL 用给人看的默认格式 */
    cg_status_setf(sb, 2, "刷新：%s", now ? now : "?");
    status_msg(app, "系统信息已刷新");
    log_line(app, "刷新完成，时间 %s", now ? now : "?");
    cg_free(now);
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面建好：定下配置文件路径；开窗时写默认值并全量刷新，自检时只写一行日志
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 */
void sp_init(cg_app_t *app, void *user)
{
    char *dir;

    (void)user;

    /* 配置文件固定为 <用户数据目录>\syspanel.ini；拿不到就退回裸文件名 */
    dir = cg_app_data_dir();
    if (dir) {
        char *full = cg_path_join(dir, "syspanel.ini");
        snprintf(s_ini, sizeof(s_ini), "%s", full ? full : "syspanel.ini");
        cg_free(full);
        cg_free(dir);
    } else {
        snprintf(s_ini, sizeof(s_ini), "syspanel.ini");
    }
    cg_widget_set_text(cg_find(app, "lab_cfgpath_v"), s_ini);

    if (!cg_app_has_window(app)) {
        /* 自检：不开窗就不写配置、不枚举串口，只写一行日志说明跳过 */
        log_line(app, "自检模式：跳过配置写入与串口枚举（不写盘、不枚举）");
        return;
    }

    if (!cg_file_exists(s_ini)) write_defaults(app);

    reload_table(app);
    refresh_system(app);
    refresh_disk(app);
    refresh_env(app);
    refresh_process(app);
    refresh_ports(app);
    status_msg(app, "就绪");
    log_line(app, "面板就绪：两个页签（系统信息 / 配置）");
}

const cg_binding_t sp_bindings[] = {
    CG_BIND("sp_on_tab_main_select",      on_tab_main_select),
    CG_BIND("sp_on_btn_proc_click",       on_btn_proc_click),
    CG_BIND("sp_on_edt_proc_submit",      on_edt_proc_submit),
    CG_BIND("sp_on_btn_refresh_click",    on_btn_refresh_click),
    CG_BIND("sp_on_btn_cfg_read_click",   on_btn_cfg_read_click),
    CG_BIND("sp_on_btn_cfg_write_click",  on_btn_cfg_write_click),
    CG_BIND("sp_on_btn_cfg_reload_click", on_btn_cfg_reload_click),
    CG_BIND("sp_on_btn_cfg_copy_click",   on_btn_cfg_copy_click),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
