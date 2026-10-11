/**
 * @file    main.c
 * @brief   示例 toolbox：工具箱启动器（多页签按钮墙 + 搜索 + 一键启动外部程序）
 * @details 参照开源项目 mxx1-toolbox（萌新工具箱）的功能面：
 *
 *            按钮墙   —— 标签页五个（搜索 / 常用 / 系统工具 / 命令行 / 网络），
 *                        按钮在 toolbox.cgui 里定义，命令在下面的 k_tools 表里登记；
 *            搜索     —— 顶部输入框即时过滤，结果进「搜索」页列表框，回车启动第一个匹配；
 *            一键启动 —— 统一回调按控件 id 查表，cg_run_process 启动外部程序；
 *            反馈     —— 状态栏成功/失败提示 + 底部日志列表框；
 *            最近使用 —— 内存记录最近 8 个，「常用」页双击再次启动。
 *
 *          与 mxx1-toolbox 的诚实差异（产品边界，照实写）：mxx1 的按钮纯 JSON 声明
 *          （tools.json），本工程没有"纯配置声明命令"的机制 —— 加一个工具 =
 *          toolbox.cgui 加一个按钮 + k_tools 表加一行，C 逻辑零改动。
 *
 *          自检（--selftest）红线，本文件处处遵守：
 *            1. 就绪钩子不启动任何程序、不读写盘；
 *            2. 凡有副作用（真启动外部程序）的回调先 cg_app_has_window()，
 *               自检下只查表对账，不真启动 —— 表查不到的按钮 id 会在日志里
 *               报「不同步」，自检输出里能 grep 到。
 *
 * 界面在 toolbox.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat toolbox
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 工具命令表 */

/** 最近使用记录条数上限 */
#define K_RECENT_MAX 8

/** 搜索结果条数上限（与「搜索」页列表框可见行数匹配，足够了） */
#define K_HIT_MAX    32

/**
 * 工具命令表：按钮 id -> 名称 + 命令行。
 * 加一个工具三步：toolbox.cgui 加一个 id 为 "tool_xxx" 的按钮 + 这张表加一行，完事。
 * console 类命令用 "cmd /k" 保持窗口可见，方便看清输出；全部是系统自带程序，无第三方依赖。
 */
static const struct {
    const char *id;   /**< 按钮 id（toolbox.cgui 里的控件 id） */
    const char *name; /**< 界面上显示的名字（也是搜索关键字） */
    const char *cmd;  /**< 启动的命令行（cg_run_process 原样透传） */
} k_tools[] = {
    /* 系统工具页 */
    { "tool_notepad",  "记事本",       "notepad"            },
    { "tool_calc",     "计算器",       "calc"               },
    { "tool_mspaint",  "画图",         "mspaint"            },
    { "tool_taskmgr",  "任务管理器",   "taskmgr"            },
    { "tool_regedit",  "注册表编辑器", "regedit"            },
    { "tool_devmgmt",  "设备管理器",   "mmc devmgmt.msc"    },
    { "tool_services", "服务",         "mmc services.msc"   },
    { "tool_control",  "控制面板",     "control"            },
    /* 命令行页 */
    { "tool_dir",        "当前目录",   "cmd /k dir"                },
    { "tool_ver",        "系统版本",   "cmd /k ver"                },
    { "tool_hostname",   "主机名",     "cmd /k hostname"           },
    { "tool_env",        "环境变量",   "cmd /k set"                },
    { "tool_tasklist",   "进程列表",   "cmd /k tasklist"           },
    { "tool_systeminfo", "系统信息",   "cmd /k systeminfo"         },
    { "tool_datetime",   "时间日期",   "cmd /k date /t & time /t"  },
    { "tool_cmdhelp",    "命令帮助",   "cmd /k help"               },
    /* 网络页 */
    { "tool_ipconfig", "IP 配置",   "cmd /k ipconfig /all"         },
    { "tool_ping",     "回环测试",   "cmd /k ping 127.0.0.1 -n 4"  },
    { "tool_ncpa",     "网络连接",   "control ncpa.cpl"             },
    { "tool_route",    "路由表",     "cmd /k route print"           },
    { "tool_netstat",  "端口占用",   "cmd /k netstat -ano"          },
    { "tool_dns",      "DNS 缓存",  "cmd /k ipconfig /displaydns"  }
};

/** 工具命令表条目数 */
#define K_TOOL_COUNT ((int)(sizeof k_tools / sizeof k_tools[0]))

/** 搜索命中的工具下标（与 lst_search 行号一一对应） */
static int  s_hit[K_HIT_MAX];

/** 搜索命中条数 */
static int  s_hit_n;

/** 最近使用的工具下标，s_recent[0] 是最新的 */
static int  s_recent[K_RECENT_MAX];

/** 最近使用条数 */
static int  s_recent_n;

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
 * @brief 大小写不敏感地找子串（自己写，strcasestr 不是标准 C）
 * @param hay 长串
 * @param needle 子串
 * @return const char* 命中位置；没命中返回 NULL
 */
static const char *ci_str(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);

    if (nlen == 0) return hay;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            char a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == nlen) return hay;
    }
    return NULL;
}

/**
 * @brief 按控件 id 在命令表里找工具
 * @param id 控件 id（形如 tool_notepad）
 * @return int 表内下标；没登记返回 -1
 */
static int tool_by_id(const char *id)
{
    int i;

    for (i = 0; i < K_TOOL_COUNT; i++)
        if (strcmp(k_tools[i].id, id) == 0) return i;
    return -1;
}

/**
 * @brief 把一个工具记进最近使用（去重，最新排最前，超过 8 条挤掉最旧的）
 * @param idx 工具下标
 * @return void 无返回值
 */
static void recent_push(int idx)
{
    int i, j = -1;

    for (i = 0; i < s_recent_n; i++)
        if (s_recent[i] == idx) { j = i; break; }

    if (j >= 0) {                       /* 已在记录里：整体前移把它顶到第一位 */
        for (i = j; i > 0; i--) s_recent[i] = s_recent[i - 1];
        s_recent[0] = idx;
        return;
    }
    if (s_recent_n < K_RECENT_MAX) s_recent_n++;   /* 没在记录里：腾一个位置 */
    for (i = s_recent_n - 1; i > 0; i--) s_recent[i] = s_recent[i - 1];
    s_recent[0] = idx;
}

/**
 * @brief 把最近使用记录填进「常用」页列表框
 * @param app 应用
 * @return void 无返回值
 */
static void recent_reload(cg_app_t *app)
{
    cg_widget_t *lst = cg_find(app, "lst_recent");
    int         i;

    if (lst) cg_widget_list_clear(lst);
    for (i = 0; i < s_recent_n; i++) {
        if (lst) cg_widget_list_addf(lst, "%s    (%s)",
                                     k_tools[s_recent[i]].name,
                                     k_tools[s_recent[i]].cmd);
    }
    if (lst && s_recent_n == 0) cg_widget_list_add(lst, "（还没有记录）");
}

/**
 * @brief 真正启动一个工具：副作用入口，自检下只对账不启动
 * @param app 应用
 * @param idx 工具下标
 * @return void 无返回值
 */
static void launch_tool(cg_app_t *app, int idx)
{
    if (idx < 0 || idx >= K_TOOL_COUNT) return;

    if (!cg_app_has_window(app)) {
        /* 自检：不真启动，只证明"按钮 id -> 命令表"这一步对得上 */
        log_line(app, "自检模式：跳过启动 %s（%s）", k_tools[idx].name, k_tools[idx].cmd);
        return;
    }

    if (cg_run_process(k_tools[idx].cmd, 0) == 0) {
        recent_push(idx);
        recent_reload(app);
        status_msg(app, "已启动：%s", k_tools[idx].name);
        log_line(app, "启动 %s：%s", k_tools[idx].name, k_tools[idx].cmd);
    } else {
        status_msg(app, "启动失败：%s", k_tools[idx].name);
        log_line(app, "启动失败 %s：%s", k_tools[idx].name, k_tools[idx].cmd);
    }
}

/* ------------------------------------------------------------ 事件：按钮墙 */

/**
 * @brief 按钮墙统一回调：按控件 id 查命令表并启动；表里没登记的按钮会记一条「不同步」
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tool_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx = tool_by_id(id);

    (void)ev;
    (void)user;

    if (idx < 0) {
        /* .cgui 里加了按钮、k_tools 表忘了登记 —— 自检输出里 grep "不同步" 就能抓到 */
        log_line(app, "按钮 %s 与命令表不同步（表里没有它）", id ? id : "?");
        status_msg(app, "按钮 %s 未登记", id ? id : "?");
        return;
    }
    launch_tool(app, idx);
}

/* ------------------------------------------------------------ 事件：搜索 */

/**
 * @brief 重新过滤：按输入框内容在 名称/命令 里做大小写不敏感的子串匹配
 * @param app 应用
 * @return void 无返回值
 */
static void search_apply(cg_app_t *app)
{
    cg_widget_t *lst = cg_find(app, "lst_search");
    const char  *q   = cg_widget_text(cg_find(app, "edt_search"));
    int         i;

    s_hit_n = 0;
    if (lst) cg_widget_list_clear(lst);
    if (!q || !*q) {
        if (lst) cg_widget_list_addf(lst, "共 %d 个工具，输入关键字过滤", K_TOOL_COUNT);
        return;
    }

    for (i = 0; i < K_TOOL_COUNT && s_hit_n < K_HIT_MAX; i++) {
        if (ci_str(k_tools[i].name, q) || ci_str(k_tools[i].cmd, q)) {
            s_hit[s_hit_n++] = i;
            if (lst) cg_widget_list_addf(lst, "%s    (%s)",
                                         k_tools[i].name, k_tools[i].cmd);
        }
    }
    if (lst && s_hit_n == 0) cg_widget_list_add(lst, "（没有匹配的工具）");

    /* 敲第一个字时自动切到「搜索」页，像 mxx1 的搜索直达结果 */
    cg_tabs_select(cg_find(app, "tab_main"), 0);
}

/**
 * @brief 搜索输入框内容变化：重过滤
 * @param self 搜索输入框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_search_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    search_apply(cg_app_of(self));
}

/**
 * @brief 搜索框回车：有匹配就启动第一个
 * @param self 搜索输入框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_search_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (s_hit_n == 0) {
        status_msg(app, "没有匹配的工具");
        return;
    }
    launch_tool(app, s_hit[0]);
}

/**
 * @brief 搜索结果双击：启动双击的那一行
 * @param self 搜索结果列表框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_lst_search_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_widget_value(self);

    (void)ev;
    (void)user;

    if (row >= 0 && row < s_hit_n) launch_tool(cg_app_of(self), s_hit[row]);
}

/* ------------------------------------------------------------ 事件：常用页 */

/**
 * @brief 最近使用双击：再次启动那一条
 * @param self 最近使用列表框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_lst_recent_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_widget_value(self);

    (void)ev;
    (void)user;

    if (row >= 0 && row < s_recent_n) launch_tool(cg_app_of(self), s_recent[row]);
}

/**
 * @brief 清空最近使用记录
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_recent_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    s_recent_n = 0;
    recent_reload(app);
    status_msg(app, "已清空最近使用");
    log_line(app, "清空最近使用记录");
}

/* ------------------------------------------------------------ 事件：页签 */

/**
 * @brief 切页签：状态栏显示当前页名
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

    status_msg(app, "当前页：%s", name ? name : "?");
    log_line(app, "切到第 %d 页：%s", page + 1, name ? name : "?");
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面建好：预填搜索页提示、重排最近使用；自检下只做表对账
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 就绪钩子不启动任何程序、不读写盘（自检红线）
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;

    search_apply(app);          /* 空关键字：显示"共 N 个工具"的提示行 */
    recent_reload(app);

    if (!cg_app_has_window(app)) {
        /* 自检：把命令表全过一遍，证明每个 id 都能查到（反面由 on_tool_click 的"不同步"兜） */
        int i;
        for (i = 0; i < K_TOOL_COUNT; i++) {
            if (tool_by_id(k_tools[i].id) != i)
                log_line(app, "命令表异常：第 %d 项 id 重复", i);
        }
        log_line(app, "自检模式：命令表 %d 项对账完成，不启动任何程序", K_TOOL_COUNT);
        return;
    }

    status_msg(app, "就绪：点按钮或搜索后回车即可启动");
    log_line(app, "工具箱就绪：%d 个工具 / 5 个页签", K_TOOL_COUNT);
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_tool_click",              on_tool_click),
    CG_BIND("on_edt_search_change",       on_edt_search_change),
    CG_BIND("on_edt_search_submit",       on_edt_search_submit),
    CG_BIND("on_lst_search_dblclick",     on_lst_search_dblclick),
    CG_BIND("on_lst_recent_dblclick",     on_lst_recent_dblclick),
    CG_BIND("on_btn_recent_clear_click",  on_btn_recent_clear_click),
    CG_BIND("on_tab_main_select",         on_tab_main_select),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "toolbox.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "toolbox.cgui";
    return cg_app_run(&desc);
}
