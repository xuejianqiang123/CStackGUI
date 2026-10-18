/**
 * @file    tb_tools.c
 * @brief   toolbox 工具命令表实现：按钮墙启动 + 最近使用 + 页计数 + 自检对账
 * @details 工具命令表：按钮 id -> 名称 + 命令行 + 页号。
 *          加一个工具两步：toolbox.cgui 加一个 id 为 "tool_xxx" 的按钮 + 这张表加一行，完事；
 *          我的工具例外：id 为 tool_my1..tool_my8，命令来自 mytools.ini，加工具连 C 都不碰。
 *          console 类命令用 "cmd /k" 保持窗口可见；URI 类（ms-settings:/shell:）经 explorer 打开。
 */
#include "tb_tools.h"

#include <stdio.h>
#include <string.h>

#include "tb_log.h"
#include "tb_mytools.h"
#include "tb_opts.h"
#include "tb_rmenu.h"

/** 最近使用记录条数上限 */
#define K_RECENT_MAX 8

/** 工具命令表：按钮 id -> 名称 + 命令行 + 页号（"__my__" 表示来自 mytools.ini 槽位） */
static const struct {
    const char *id;   /**< 按钮 id（toolbox.cgui 里的控件 id） */
    const char *name; /**< 界面上显示的名字（也是搜索关键字） */
    const char *cmd;  /**< 启动的命令行；"__my__" 表示来自 mytools.ini 槽位 */
    int         page; /**< 所在页（0 起，用于「本页 X 个」计数） */
} k_tools[] = {
    /* —— 常用设置页：常用入口（页 1） —— */
    { "tool_taskmgr",  "任务管理器",   "taskmgr",                   1 },
    { "tool_control",  "控制面板",     "control",                   1 },
    { "tool_env",      "环境变量",     "cmd /k set",                1 },
    { "tool_userpwd",  "用户账户",     "control userpasswords",     1 },
    { "tool_regedit",  "注册表编辑器", "regedit",                   1 },
    { "tool_cmd",      "命令提示符",   "cmd /k echo 命令提示符",    1 },
    { "tool_ps",       "PowerShell",   "powershell -NoExit",        1 },
    { "tool_calc",     "计算器",       "calc",                      1 },
    { "tool_notepad",  "记事本",       "notepad",                   1 },
    { "tool_mspaint",  "画图",         "mspaint",                   1 },
    { "tool_snip",     "截图工具",     "snippingtool",              1 },
    { "tool_magnify",  "放大镜",       "magnify",                   1 },
    /* —— 我的工具槽位（页 2）：命令在 mytools.ini —— */
    { "tool_my1", "", "__my__", 2 }, { "tool_my2", "", "__my__", 2 },
    { "tool_my3", "", "__my__", 2 }, { "tool_my4", "", "__my__", 2 },
    { "tool_my5", "", "__my__", 2 }, { "tool_my6", "", "__my__", 2 },
    { "tool_my7", "", "__my__", 2 }, { "tool_my8", "", "__my__", 2 },
    /* —— 系统工具页（页 3） —— */
    { "tool_devmgmt",  "设备管理器",     "mmc devmgmt.msc",      3 },
    { "tool_sound",    "声音",           "control mmsys.cpl",    3 },
    { "tool_printer",  "设备和打印机",   "control printers",     3 },
    { "tool_tasks",    "计划任务",       "mmc taskschd.msc",     3 },
    { "tool_diskmgmt", "磁盘管理",       "mmc diskmgmt.msc",     3 },
    { "tool_power",    "电源选项",       "control powercfg.cpl", 3 },
    { "tool_display",  "显示设置",       "control desk.cpl",     3 },
    { "tool_mouse",    "鼠标",           "control main.cpl",     3 },
    { "tool_services", "服务",           "mmc services.msc",     3 },
    { "tool_gpedit",   "组策略",         "mmc gpedit.msc",       3 },
    { "tool_appwiz",   "程序和功能",     "control appwiz.cpl",   3 },
    { "tool_firewall", "防火墙",         "control firewall.cpl", 3 },
    { "tool_sysdm",    "系统属性",       "control sysdm.cpl",    3 },
    { "tool_timedate", "日期和时间",     "control timedate.cpl", 3 },
    { "tool_msinfo",   "系统信息",       "msinfo32",             3 },
    { "tool_dxdiag",   "DirectX 诊断",   "dxdiag",               3 },
    { "tool_winver",   "系统版本",       "cmd /k ver",           3 },
    { "tool_resmon",   "资源监视器",     "resmon",               3 },
    { "tool_perfmon",  "性能监视器",     "perfmon",              3 },
    { "tool_eventvwr", "事件查看器",     "mmc eventvwr.msc",     3 },
    { "tool_charmap",  "字符映射表",     "charmap",             3 },
    /* —— 清理优化页（页 4）：全部"只打开 / 只查看"，不自动删 —— */
    { "tool_cleanmgr",  "磁盘清理",       "cleanmgr",                            4 },
    { "tool_dfrgui",    "碎片整理",       "dfrgui",                              4 },
    { "tool_storage",   "存储感知",       "explorer ms-settings:storagesense",   4 },
    { "tool_recycle",   "回收站",         "explorer shell:RecycleBinFolder",     4 },
    { "tool_temp",      "用户临时目录",   "cmd /c explorer %TEMP%",              4 },
    { "tool_wintemp",   "系统临时目录",   "cmd /k dir C:\\Windows\\Temp",        4 },
    { "tool_wucache",   "更新缓存目录",   "explorer C:\\Windows\\SoftwareDistribution\\Download", 4 },
    { "tool_flushdns",  "刷新 DNS 缓存",  "cmd /k ipconfig /flushdns",           4 },
    { "tool_sfc",       "系统文件校验",   "cmd /k sfc /verifyonly",              4 },
    { "tool_dism",      "映像完整性检查", "cmd /k DISM /Online /Cleanup-Image /CheckHealth", 4 },
    { "tool_rstrui",    "系统还原",       "rstrui",                              4 },
    { "tool_trouble",   "疑难解答",       "explorer ms-settings:troubleshoot",   4 },
    /* —— 隐私设置页（页 5）：直达设置页，不改任何设置 —— */
    { "tool_privacy",  "隐私总览",   "explorer ms-settings:privacy",                5 },
    { "tool_mic",      "麦克风",     "explorer ms-settings:privacy/microphone",     5 },
    { "tool_cam",      "摄像头",     "explorer ms-settings:privacy/webcam",         5 },
    { "tool_loc",      "位置",       "explorer ms-settings:privacy/location",        5 },
    { "tool_acthist",  "活动历史记录", "explorer ms-settings:privacy/activityhistory", 5 },
    { "tool_diag",     "诊断和反馈", "explorer ms-settings:privacy/diagnostics",    5 },
    { "tool_adid",     "广告 ID",    "explorer ms-settings:privacy/advertisingid",  5 },
    { "tool_speech",   "语音",       "explorer ms-settings:privacy/speech",         5 },
    /* —— 应用管理页（页 6） —— */
    { "tool_apps",     "应用和功能",   "explorer ms-settings:appsfeatures",  6 },
    { "tool_optfeat",  "可选功能",     "explorer ms-settings:optionalfeatures", 6 },
    { "tool_default",  "默认应用",     "explorer ms-settings:defaultapps",   6 },
    { "tool_startup",  "启动应用",     "explorer ms-settings:startupapps",   6 },
    { "tool_starupdir","启动文件夹",   "explorer shell:startup",             6 },
    { "tool_progs",    "程序安装目录", "explorer C:\\Program Files",         6 },
    { "tool_appsflr",  "应用文件夹",   "explorer shell:AppsFolder",          6 },
    { "tool_tasklist", "进程列表",     "cmd /k tasklist",                    6 },
    /* —— 右键增强页（页 7）：入口类命令（菜单管理见 tb_rmenu 模块，不走命令表） —— */
    { "tool_fonts",    "字体目录",   "explorer shell:fonts",        7 },
    { "tool_recent",   "最近使用项目", "explorer shell:recent",     7 }
};

/** 工具命令表条目数 */
#define K_TOOL_COUNT ((int)(sizeof k_tools / sizeof k_tools[0]))

/** 最近使用的工具下标，s_recent[0] 是最新的 */
static int s_recent[K_RECENT_MAX];

/** 最近使用条数 */
static int s_recent_n;

/**
 * @brief 命令表条目数（含 8 个我的工具槽位）
 * @return int 条目数
 */
int tb_tool_count(void)
{
    return K_TOOL_COUNT;
}

/**
 * @brief 内置工具数（命令表条数 - 我的工具槽位数）
 * @return int 内置工具个数
 */
int tb_tool_builtin_count(void)
{
    return K_TOOL_COUNT - TB_MY_MAX;
}

/**
 * @brief 按控件 id 在命令表里找工具
 * @param id 控件 id（形如 tool_notepad）
 * @return int 表内下标；没登记返回 -1
 */
int tb_tool_by_id(const char *id)
{
    int i;

    for (i = 0; i < K_TOOL_COUNT; i++)
        if (strcmp(k_tools[i].id, id) == 0) return i;
    return -1;
}

/**
 * @brief 这个下标是不是「我的工具」槽位（命令来自 mytools.ini）
 * @param idx 命令表下标
 * @return int 1=槽位 0=内置
 */
int tb_tool_is_my(int idx)
{
    return (idx >= 0 && idx < K_TOOL_COUNT) && strcmp(k_tools[idx].cmd, "__my__") == 0;
}

/**
 * @brief 取工具的显示名（槽位工具返回 mytools.ini 里的名字，空槽位返回空串）
 * @param idx 命令表下标
 * @return const char* 名称，永不为 NULL
 */
const char *tb_tool_name(int idx)
{
    if (idx < 0 || idx >= K_TOOL_COUNT) return "";
    if (tb_tool_is_my(idx)) return tb_my_slot_name(k_tools[idx].id[7] - '1');
    return k_tools[idx].name;
}

/**
 * @brief 取工具的启动命令（槽位工具返回 mytools.ini 里的命令，空槽位返回空串）
 * @param idx 命令表下标
 * @return const char* 命令行，永不为 NULL
 */
const char *tb_tool_cmd(int idx)
{
    if (idx < 0 || idx >= K_TOOL_COUNT) return "";
    if (tb_tool_is_my(idx)) return tb_my_slot_cmd(k_tools[idx].id[7] - '1');
    return k_tools[idx].cmd;
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

    if (j >= 0) {
        for (i = j; i > 0; i--) s_recent[i] = s_recent[i - 1];
        s_recent[0] = idx;
        return;
    }
    if (s_recent_n < K_RECENT_MAX) s_recent_n++;
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

    if (lst) cg_iconlist_clear(lst);
    for (i = 0; i < s_recent_n; i++) {
        /* 行 = 工具图标 + 主标题（名字）+ 副标题（命令行）；双击行为不变（on_lst_recent_dblclick） */
        if (lst) cg_iconlist_add(lst, "assets/recent.png",
                                 tb_tool_name(s_recent[i]), tb_tool_cmd(s_recent[i]), NULL);
    }
    if (lst && s_recent_n == 0)
        cg_iconlist_add(lst, NULL, "（还没有记录）", NULL, NULL);
}

/**
 * @brief 刷新底栏「本页 X 个（共 Y 个）」计数
 * @param app 应用
 * @return void 无返回值
 * @note 页 0（常用）按最近使用条数算；页 2（我的工具）按槽位数；页 7（右键）按自建菜单数
 */
void tb_page_count_sync(cg_app_t *app)
{
    int          page = cg_tabs_selected(cg_find(app, "tab_main"));
    int          n = 0, total = tb_tool_builtin_count() + tb_mytools_count(), i;
    char         buf[96];

    for (i = 0; i < K_TOOL_COUNT; i++)
        if (k_tools[i].page == page && !tb_tool_is_my(i)) n++;
    if (page == 0) n = s_recent_n;
    if (page == 2) n = tb_mytools_count();
    if (page == 7) n = tb_rmenu_count();   /* 右键页：本页的"工具"是自建菜单 */

    snprintf(buf, sizeof(buf), "本页 %d 个（共 %d 个）", n, total);
    cg_widget_set_text(cg_find(app, "lab_page"), buf);
}

/**
 * @brief 就绪钩子用：装载最近使用列表（默认空，填一行「还没有记录」）
 * @param app 应用
 * @return void 无返回值
 */
void tb_tools_load(cg_app_t *app)
{
    recent_reload(app);
}

/**
 * @brief 自检用：命令表 id 重复对账（只打日志，不动任何外部状态）
 * @param app 应用
 * @return void 无返回值
 */
void tb_tools_audit(cg_app_t *app)
{
    int i, j;

    for (i = 0; i < K_TOOL_COUNT; i++) {
        if (tb_tool_is_my(i)) continue;
        for (j = i + 1; j < K_TOOL_COUNT; j++)
            if (strcmp(k_tools[i].id, k_tools[j].id) == 0)
                tb_log_line(app, "命令表异常：id %s 重复", k_tools[i].id);
    }
    tb_log_line(app, "自检模式：命令表 %d 项对账完成，不启动任何程序", K_TOOL_COUNT);
}

/**
 * @brief 真正启动一个工具：副作用入口，自检下只对账不启动
 * @param app 应用
 * @param idx 工具下标
 * @return void 无返回值
 */
void tb_launch_tool(cg_app_t *app, int idx)
{
    const char *name, *cmd;

    if (idx < 0 || idx >= K_TOOL_COUNT) return;
    name = tb_tool_name(idx);
    cmd  = tb_tool_cmd(idx);
    if (tb_tool_is_my(idx) && !cmd[0]) {
        tb_status_msg(app, "空槽位：先编辑 mytools.ini");
        return;
    }

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过启动 %s（%s）", name, cmd);
        return;
    }

    if (cg_run_process(cmd, 0) == 0) {
        if (tb_opt_get(3)) {                               /* RecentOn：受开关控制 */
            recent_push(idx);
            recent_reload(app);
            tb_page_count_sync(app);
        }
        tb_status_msg(app, "已启动：%s", name);
        tb_log_line(app, "启动 %s：%s", name, cmd);
    } else {
        tb_status_msg(app, "启动失败：%s", name);
        tb_log_line(app, "启动失败 %s：%s", name, cmd);
    }
}

/**
 * @brief 按钮墙统一回调：按控件 id 查命令表并启动；表里没登记的按钮记「不同步」
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tool_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx = tb_tool_by_id(id);

    (void)ev;
    (void)user;

    if (idx < 0) {
        tb_log_line(app, "按钮 %s 与命令表不同步（表里没有它）", id ? id : "?");
        tb_status_msg(app, "按钮 %s 未登记", id ? id : "?");
        return;
    }
    tb_launch_tool(app, idx);
}

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

    if (row >= 0 && row < s_recent_n) tb_launch_tool(cg_app_of(self), s_recent[row]);
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
    tb_page_count_sync(app);
    tb_status_msg(app, "已清空最近使用");
    tb_log_line(app, "清空最近使用记录");
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_tools_bindings[] = {
    CG_BIND("on_tool_click",             on_tool_click),
    CG_BIND("on_lst_recent_dblclick",    on_lst_recent_dblclick),
    CG_BIND("on_btn_recent_clear_click", on_btn_recent_clear_click),
    CG_BIND_END
};
