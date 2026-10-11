/**
 * @file    main.c
 * @brief   示例 toolbox：工具箱启动器（对标 mxx1-toolbox 的功能面与界面形态）
 * @details 五个形态要素与原版一一对齐：
 *
 *            ① 8 个页签（常用/常用设置/我的工具/系统工具/清理优化/隐私设置/应用管理/右键增强）；
 *            ② 页内分组条 + 密集按钮墙（.cgui 命名样式 sect / install / remove）；
 *            ③ 成对「装上/撤掉」开关 —— 真实落点是本程序沙箱键
 *               HKCU\Software\CStackGUI\examples\toolbox，控制本工具箱自身行为
 *               （日志镜像 / 时间戳 / 自动切搜索页 / 记录最近使用 / 沙箱标记），
 *               真改真还原，配「查看曾改动 / 还原全部」；
 *            ④ 底栏功能带：「本页 X 个（共 Y 个）」+ 搜索/日志/设置/关于/检查更新
 *               （日志是非模态对话框，设置 = 深浅主题切换，均为真功能）；
 *            ⑤ 我的工具 = mytools.ini 槽位（名称/命令），**加工具不用重编译**；
 *            ⑥ 右键菜单管理：扫 HKCU 用户区五个区域（cg_reg_subkeys 枚举 + 逐项读
 *               MUIVerb / command / LegacyDisable），工具箱自己的两条菜单真装真撤
 *               （reg.exe add/delete，删得干净 —— 本工程注册表接口没有删值能力）。
 *
 *          与原版的诚实差异（照实写，不在示例里装）：
 *            - 原版按钮带彩色图标：本工程按钮没有贴图能力，用文字 + 配色区分；
 *            - 原版的系统级开关（任务栏合并/内核隔离等）要写系统注册表值并支持
 *              删值/备份，本工程注册表接口只能读写字符串值、没有删值与枚举，
 *              示例也不碰系统键 —— 所以成对开关落在自家沙箱上，模式一致、落点无害；
 *            - 原版 106+ 工具：示例收在 60+，够演示全部模式。
 *
 *          自检（--selftest）红线，本文件处处遵守：
 *            1. 就绪钩子不启动程序、不读盘不写盘（mytools / 开关状态在无窗时用默认值）；
 *            2. 凡有副作用（启动外部程序 / 写注册表 / 弹对话框）先 cg_app_has_window()；
 *            3. 「.cgui 按钮 id 查不到命令表」会在日志报「不同步」，自检输出可 grep。
 *
 * 界面在 toolbox.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat toolbox
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 常量 */

/** 最近使用记录条数上限 */
#define K_RECENT_MAX 8

/** 搜索结果条数上限 */
#define K_HIT_MAX    32

/** 我的工具槽位数（对应 mytools.ini 的 [t1]..[t8]） */
#define K_MY_MAX     8

/** 行为开关种数 */
#define K_OPT_MAX    5

/** 工具箱自建右键菜单组数 */
#define K_RMENU_COUNT 2

/** 注册表沙箱子键（相对 HKCU）——绝不写系统键 */
#define K_REG_SUBKEY "Software\\CStackGUI\\examples\\toolbox"

/** 我的工具配置文件名（放用户数据目录） */
#define K_MY_INI     "mytools.ini"

/** 页数（与 toolbox.cgui 的 items 一一对应） */
#define K_PAGE_MAX   8

/** 开关定义：注册表值名 + 界面名 + 接管的行为说明 */
static const struct {
    const char *val;   /**< 沙箱键里的值名 */
    const char *name;  /**< 界面上显示的名字 */
} k_opts[K_OPT_MAX] = {
    { "MirrorLog",      "日志镜像到控制台" },
    { "LogTimestamp",   "日志行首加时间戳" },
    { "AutoSearchPage", "输入关键字自动切到搜索页" },
    { "RecentOn",       "记录最近使用" },
    { "RmenuSandbox",   "右键增强沙箱标记（演示）" }
};

/** 开关当前状态（1=装上 0=撤掉），就绪钩子里从沙箱键读回 */
static int s_opt[K_OPT_MAX];

/**
 * 工具命令表：按钮 id -> 名称 + 命令行 + 页号。
 * 加一个工具两步：toolbox.cgui 加一个 id 为 "tool_xxx" 的按钮 + 这张表加一行，完事；
 * 我的工具例外：id 为 tool_my1..tool_my8，命令来自 mytools.ini，加工具连 C 都不碰。
 * console 类命令用 "cmd /k" 保持窗口可见；URI 类（ms-settings:/shell:）经 explorer 打开。
 */
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
    { "tool_charmap",  "字符映射表",     "charmap",              3 },
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
    { "tool_loc",      "位置",       "explorer ms-settings:privacy/location",       5 },
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
    /* —— 右键增强页（页 7）：入口类命令（菜单管理见 rmenu 段，不走命令表） —— */
    { "tool_fonts",    "字体目录",   "explorer shell:fonts",        7 },
    { "tool_recent",   "最近使用项目", "explorer shell:recent",     7 }
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

/** 我的工具槽位：名称 + 命令（就绪钩子里从 mytools.ini 读） */
static struct {
    char name[64];
    char cmd[256];
} s_my[K_MY_MAX];

/** 有槽位的工具数（「共 Y 个」计数用） */
static int s_my_n;

/** 我的工具配置文件全路径（就绪钩子里填一次） */
static char s_myini[1024];

/** 当前主题是否深色（按钮文字同步用） */
static int s_dark;

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 往日志列表框追加一行；镜像 stdout 与时间戳都受沙箱开关控制
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char        buf[256];
    char        line[300];
    va_list     ap;
    cg_widget_t *lst = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (s_opt[1]) {                                   /* LogTimestamp：行首加时间戳 */
        char *now = cg_time_now(NULL);
        snprintf(line, sizeof(line), "[%s] %s", now ? now : "--:--:--", buf);
        cg_free(now);
    } else {
        snprintf(line, sizeof(line), "%s", buf);
    }

    if (lst) {
        if (cg_widget_list_count(lst) > 400) cg_widget_list_clear(lst);
        cg_widget_list_add(lst, line);
        cg_widget_list_scroll_bottom(lst);
    }
    if (s_opt[0]) printf("[log] %s\n", line);         /* MirrorLog：镜像到控制台 */
}

/**
 * @brief 把最近一次结果写进状态栏第 1 段（第 0 段固定放计数）
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

    cg_status_set(cg_find(app, "sb_main"), 1, buf);
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

/* ------------------------------------------------------------ 计数 */

/**
 * @brief 刷新底栏「本页 X 个（共 Y 个）」：本页 = 该页命令表条数 + 我的工具槽位
 * @param app 应用
 * @return void 无返回值
 * @note 页 0（常用）按最近使用条数算，没有记录就显示共数
 */
static void page_count_sync(cg_app_t *app)
{
    int          page = cg_tabs_selected(cg_find(app, "tab_main"));
    int          n = 0, total = K_TOOL_COUNT - K_MY_MAX + s_my_n, i;
    char         buf[96];

    for (i = 0; i < K_TOOL_COUNT; i++)
        if (k_tools[i].page == page && strcmp(k_tools[i].cmd, "__my__") != 0) n++;
    if (page == 0) n = s_recent_n;
    if (page == 2) n = s_my_n;
    if (page == 7) n = K_RMENU_COUNT;   /* 右键页：本页的"工具"是自建菜单 */

    snprintf(buf, sizeof(buf), "本页 %d 个（共 %d 个）", n, total);
    cg_widget_set_text(cg_find(app, "lab_page"), buf);
}

/* ------------------------------------------------------------ 我的工具（INI 槽位） */

/**
 * @brief 重读 mytools.ini 并同步 8 个槽位按钮的文字与可用状态
 * @param app 应用
 * @param read_file 1=真读盘（有窗运行时）；0=自检，用默认值不动盘
 * @return void 无返回值
 */
static void mytools_reload(cg_app_t *app, int read_file)
{
    int i;

    memset(s_my, 0, sizeof(s_my));
    s_my_n = 0;
    if (read_file) {
        for (i = 0; i < K_MY_MAX; i++) {
            char sec[8];
            char *name, *cmd;
            snprintf(sec, sizeof(sec), "t%d", i + 1);
            name = cg_ini_read(s_myini, sec, "name", NULL);
            cmd  = cg_ini_read(s_myini, sec, "cmd", NULL);
            if (name && *name && cmd && *cmd) {
                snprintf(s_my[i].name, sizeof(s_my[i].name), "%s", name);
                snprintf(s_my[i].cmd,  sizeof(s_my[i].cmd),  "%s", cmd);
                s_my_n++;
            }
            cg_free(name);
            cg_free(cmd);
        }
    }

    for (i = 0; i < K_MY_MAX; i++) {
        cg_widget_t *btn = cg_find(app, k_tools[12 + i].id);   /* 表里槽位从 12 号起 */
        char        id[16];
        int         has = s_my[i].name[0] && s_my[i].cmd[0];

        snprintf(id, sizeof(id), "tool_my%d", i + 1);
        if (btn) {
            cg_widget_set_text(btn, has ? s_my[i].name : "空槽位");
            cg_widget_set_enabled(btn, has);
        }
    }
}

/* ------------------------------------------------------------ 成对开关（沙箱） */

/**
 * @brief 同步全部开关按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 * @note 装上/撤掉互斥禁用；说明 label 显示「已装上 / 未装」
 */
static void opt_sync(cg_app_t *app)
{
    static const char *on_id[K_OPT_MAX]  = { "opt0_on",  "opt1_on",  "opt2_on",  "opt3_on",  "opt4_on"  };
    static const char *off_id[K_OPT_MAX] = { "opt0_off", "opt1_off", "opt2_off", "opt3_off", "opt4_off" };
    static const char *lab_id[K_OPT_MAX] = { "lab_opt0", "lab_opt1", "lab_opt2", "lab_opt3", "lab_opt4" };
    int i;

    for (i = 0; i < K_OPT_MAX; i++) {
        char buf[128];
        cg_widget_t *on  = cg_find(app, on_id[i]);
        cg_widget_t *off = cg_find(app, off_id[i]);

        if (on)  cg_widget_set_enabled(on,  !s_opt[i]);
        if (off) cg_widget_set_enabled(off,  s_opt[i]);
        snprintf(buf, sizeof(buf), "%s —— %s", k_opts[i].name,
                 s_opt[i] ? "已装上" : "未装");
        cg_widget_set_text(cg_find(app, lab_id[i]), buf);
    }
}

/**
 * @brief 就绪钩子里从沙箱键读回全部开关状态
 * @param read_reg 1=真读注册表；0=自检，保持全关
 * @return void 无返回值
 */
static void opt_load(int read_reg)
{
    int i;

    for (i = 0; i < K_OPT_MAX; i++) {
        if (!read_reg) { s_opt[i] = 0; continue; }
        {
            char *v = cg_reg_read(K_REG_SUBKEY, k_opts[i].val, NULL);
            s_opt[i] = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
            cg_free(v);
        }
    }
}

/**
 * @brief 成对开关统一回调：按 id 尾缀 _on/_off 判方向，写沙箱键并同步界面
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_opt_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx, on;

    (void)ev;
    (void)user;

    if (!id || strlen(id) < 7) return;   /* opt0_on 是 7 字符 */
    idx = id[3] - '0';
    on  = (strcmp(id + 5, "on") == 0);   /* id 形如 opt3_on：id+4 是 "_on"，方向在 id+5 */
    if (idx < 0 || idx >= K_OPT_MAX) return;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过写沙箱键 %s=%d", k_opts[idx].val, on);
        s_opt[idx] = on;
        opt_sync(app);
        return;
    }

    if (cg_reg_write(K_REG_SUBKEY, k_opts[idx].val, on ? "1" : "") == 0) {
        s_opt[idx] = on;
        opt_sync(app);
        status_msg(app, "%s：%s", k_opts[idx].name, on ? "已装上" : "已撤掉");
        log_line(app, "开关 %s %s（写沙箱键）", k_opts[idx].val, on ? "=1" : "=空");
    } else {
        status_msg(app, "写沙箱键失败：%s", k_opts[idx].val);
    }
}

/**
 * @brief 「查看曾改动」：把沙箱键里 5 个开关的当前值弹出来看
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹框是副作用：自检下只写日志不弹
 */
static void on_btn_reg_view_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     buf[512];
    int      i, off = 0;

    (void)ev;
    (void)user;

    off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                    "沙箱键 HKCU\\%s\n\n", K_REG_SUBKEY);
    for (i = 0; i < K_OPT_MAX && off < (int)sizeof(buf) - 64; i++) {
        char *v = cg_app_has_window(app) ? cg_reg_read(K_REG_SUBKEY, k_opts[i].val, NULL) : NULL;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%-16s = %s\n",
                        k_opts[i].val, (v && *v) ? v : "(空)");
        cg_free(v);
    }

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过弹「查看曾改动」");
        return;
    }
    cg_message_box(self, buf, "曾改动（沙箱）", 0);
}

/**
 * @brief 「还原全部开关」：5 个值全写空串（本工程没有删值接口，值空 = 未设）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_reg_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int      i, ok = 1;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过写沙箱键（还原全部）");
        return;
    }
    for (i = 0; i < K_OPT_MAX; i++)
        if (cg_reg_write(K_REG_SUBKEY, k_opts[i].val, "") != 0) ok = 0;

    memset(s_opt, 0, sizeof(s_opt));
    opt_sync(app);
    status_msg(app, ok ? "已还原全部开关" : "部分值还原失败");
    log_line(app, ok ? "沙箱键全部还原（写空串）" : "沙箱键还原失败");
}

/* ------------------------------------------------------------ 右键菜单管理 */

/** 工具箱自建右键菜单：键（相对 HKCU\Software\Classes）+ 菜单标题（默认值）+ command */
static const struct {
    const char *key;   /**< 菜单项键 */
    const char *title; /**< 菜单标题 */
    const char *cmd;   /**< command 默认值 */
} k_rmenu[2] = {
    { "*\\shell\\CStackGUI.CopyPath",
      "复制文件路径",
      "cmd.exe /c echo %1 | clip" },
    { "Directory\\Background\\shell\\CStackGUI.Terminal",
      "在此处打开终端",
      "cmd.exe /k cd /d \"%V\"" }
};

/** 自建菜单当前是否已装（就绪钩子里读 command 判定） */
static int s_rm_installed[2];

/** 扫描目标区域（相对 HKCU\Software\Classes；原版五张区里本工程够得着的全部） */
static const struct {
    const char *key;   /**< 区键 */
    const char *label; /**< 区名 */
} k_rm_areas[] = {
    { "*\\shell",                     "文件" },
    { "Directory\\shell",             "文件夹" },
    { "Directory\\Background\\shell", "空白处" },
    { "Drive\\shell",                 "磁盘" },
    { "AllFilesystemObjects\\shell",  "通用对象" }
};

/** 扫描区域数 */
#define K_RM_AREA_COUNT ((int)(sizeof k_rm_areas / sizeof k_rm_areas[0]))

/**
 * @brief 同步自建菜单按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 */
static void rm_sync(cg_app_t *app)
{
    static const char *on_id[2]  = { "rm0_on",  "rm1_on"  };
    static const char *off_id[2] = { "rm0_off", "rm1_off" };
    static const char *lab_id[2] = { "lab_rm0", "lab_rm1" };
    int i;

    for (i = 0; i < K_RMENU_COUNT; i++) {
        char        buf[128];
        cg_widget_t *on  = cg_find(app, on_id[i]);
        cg_widget_t *off = cg_find(app, off_id[i]);

        if (on)  cg_widget_set_enabled(on,  !s_rm_installed[i]);
        if (off) cg_widget_set_enabled(off,  s_rm_installed[i]);
        snprintf(buf, sizeof(buf), "「%s」—— %s", k_rmenu[i].title,
                 s_rm_installed[i] ? "已装上" : "未装");
        cg_widget_set_text(cg_find(app, lab_id[i]), buf);
    }
}

/**
 * @brief 就绪钩子里读回自建菜单状态：command 有值 = 已装
 * @param read_reg 1=真读注册表；0=自检，按未装算
 * @return void 无返回值
 */
static void rm_load(int read_reg)
{
    int i;

    for (i = 0; i < K_RMENU_COUNT; i++) {
        s_rm_installed[i] = 0;
        if (!read_reg) continue;
        {
            char key[256];
            char *cmd;
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\command", k_rmenu[i].key);
            cmd = cg_reg_read(key, "", NULL);
            s_rm_installed[i] = (cmd && *cmd) ? 1 : 0;
            cg_free(cmd);
        }
    }
}

/**
 * @brief 装上 / 撤掉本工具箱自己的右键菜单：reg.exe add/delete，删得干净
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写注册表是副作用：自检下只记日志不执行。id 形如 rm0_on / rm0_off，
 *       方向用 strstr 认 —— 教训：别用固定偏移猜（opt 那次就是偏移错一位）。
 */
static void on_rmenu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx;
    int         on;
    char        keypath[384];
    char        cmdline[768];

    (void)ev;
    (void)user;

    if (!id || strlen(id) < 6) return;
    idx = id[2] - '0';
    on  = (strstr(id, "_on") != NULL);
    if (idx < 0 || idx >= K_RMENU_COUNT) return;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过 reg.exe（%s：%s）",
                 k_rmenu[idx].title, on ? "装上" : "撤掉");
        s_rm_installed[idx] = on;
        rm_sync(app);
        return;
    }

    snprintf(keypath, sizeof(keypath), "HKCU\\Software\\Classes\\%s", k_rmenu[idx].key);
    if (on) {
        snprintf(cmdline, sizeof(cmdline),
                 "reg add \"%s\" /ve /d \"%s\" /f", keypath, k_rmenu[idx].title);
        if (cg_run_process(cmdline, 1) != 0) {
            status_msg(app, "reg add 失败（标题）");
            log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
        snprintf(cmdline, sizeof(cmdline),
                 "reg add \"%s\\command\" /ve /d \"%s\" /f", keypath, k_rmenu[idx].cmd);
        if (cg_run_process(cmdline, 1) != 0) {
            status_msg(app, "reg add 失败（命令）");
            log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
    } else {
        snprintf(cmdline, sizeof(cmdline), "reg delete \"%s\" /f", keypath);
        if (cg_run_process(cmdline, 1) != 0) {
            status_msg(app, "reg delete 失败");
            log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
    }

    s_rm_installed[idx] = on;
    rm_sync(app);
    status_msg(app, "%s：%s", k_rmenu[idx].title, on ? "已装上" : "已撤掉");
    log_line(app, "右键菜单「%s」%s（reg.exe）", k_rmenu[idx].title, on ? "装上" : "撤掉");
}

/**
 * @brief 「扫描右键菜单」：五个区域枚举子键，逐项读 MUIVerb / command / LegacyDisable
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 纯只读，但按自检红线（不开窗不碰注册表）照样跳过。
 *       状态判定：command 有值且没有 LegacyDisable = 在用；有 LegacyDisable = 已隐藏；
 *       读不到 command = 无命令（多为 shellex 残键，原版里那类是 DLL 扩展项，本页只读不删）。
 */
static void on_btn_regmenu_scan_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    char         buf[8192];
    char         stat[128];
    int          a, total = 0, active = 0, hidden = 0, nocommand = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过扫描右键菜单（不碰注册表）");
        return;
    }
    if (tbl) cg_table_clear(tbl);

    for (a = 0; a < K_RM_AREA_COUNT; a++) {
        char key[256];
        int  n, i = 0;

        snprintf(key, sizeof(key), "Software\\Classes\\%s", k_rm_areas[a].key);
        n = cg_reg_subkeys(key, buf, sizeof(buf));
        while (i < n) {
            int  j = i;
            char sub[512];
            char cmdkey[600];
            char state[24];
            char *title, *def, *cmd, *legacy;
            const char *cells[4];

            while (j < n && buf[j] != '\n') j++;
            buf[j] = '\0';                        /* buf[i..j) 是一个子键名 */

            snprintf(sub, sizeof(sub), "%s\\%s", key, &buf[i]);
            title  = cg_reg_read(sub, "MUIVerb", NULL);          /* 菜单标题优先 MUIVerb */
            def    = cg_reg_read(sub, "", NULL);                 /* 没有就用默认值 */
            snprintf(cmdkey, sizeof(cmdkey), "%s\\command", sub);
            cmd    = cg_reg_read(cmdkey, "", NULL);              /* 命令在 command 子键的默认值 */
            legacy = cg_reg_read(sub, "LegacyDisable", NULL);    /* 空值存在 = 菜单被隐藏 */

            if (!title || !*title) {
                cg_free(title);
                title = (def && *def) ? def : NULL;
                def = NULL;
            }
            if (!title || !*title) {
                cg_free(title);
                title = cg_str_dup(&buf[i]);
            }

            if (!cmd || !*cmd) {
                snprintf(state, sizeof(state), "无命令");
                nocommand++;
            } else if (legacy) {
                snprintf(state, sizeof(state), "已隐藏");
                hidden++;
            } else {
                snprintf(state, sizeof(state), "在用");
                active++;
            }
            total++;

            cells[0] = k_rm_areas[a].label;
            cells[1] = title;
            cells[2] = (cmd && *cmd) ? cmd : "—";
            cells[3] = state;
            if (tbl) cg_table_add_row(tbl, cells, 4);

            cg_free(title);
            cg_free(def);
            cg_free(cmd);
            cg_free(legacy);
            i = j + 1;
        }
    }

    snprintf(stat, sizeof(stat), "共 %d 条 · 在用 %d · 已隐藏 %d · 无命令 %d",
             total, active, hidden, nocommand);
    cg_widget_set_text(cg_find(app, "lab_rm_stat"), stat);
    status_msg(app, "扫描完成：%s", stat);
    log_line(app, "扫描右键菜单（HKCU 用户区）：%s", stat);
}

/* ------------------------------------------------------------ 最近使用 */

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

    if (lst) cg_widget_list_clear(lst);
    for (i = 0; i < s_recent_n; i++) {
        int         idx = s_recent[i];
        const char *name = (strcmp(k_tools[idx].cmd, "__my__") == 0)
                           ? s_my[k_tools[idx].id[7] - '1'].name : k_tools[idx].name;
        if (lst) cg_widget_list_addf(lst, "%s    (%s)", name,
                                     strcmp(k_tools[idx].cmd, "__my__") == 0
                                     ? s_my[k_tools[idx].id[7] - '1'].cmd : k_tools[idx].cmd);
    }
    if (lst && s_recent_n == 0) cg_widget_list_add(lst, "（还没有记录）");
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 真正启动一个工具：副作用入口，自检下只对账不启动
 * @param app 应用
 * @param idx 工具下标
 * @return void 无返回值
 */
static void launch_tool(cg_app_t *app, int idx)
{
    const char *name, *cmd;

    if (idx < 0 || idx >= K_TOOL_COUNT) return;
    if (strcmp(k_tools[idx].cmd, "__my__") == 0) {
        int slot = k_tools[idx].id[7] - '1';
        if (slot < 0 || slot >= K_MY_MAX || !s_my[slot].cmd[0]) {
            status_msg(app, "空槽位：先编辑 mytools.ini");
            return;
        }
        name = s_my[slot].name;
        cmd  = s_my[slot].cmd;
    } else {
        name = k_tools[idx].name;
        cmd  = k_tools[idx].cmd;
    }

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过启动 %s（%s）", name, cmd);
        return;
    }

    if (cg_run_process(cmd, 0) == 0) {
        if (s_opt[3]) {                               /* RecentOn：受开关控制 */
            recent_push(idx);
            recent_reload(app);
            page_count_sync(app);
        }
        status_msg(app, "已启动：%s", name);
        log_line(app, "启动 %s：%s", name, cmd);
    } else {
        status_msg(app, "启动失败：%s", name);
        log_line(app, "启动失败 %s：%s", name, cmd);
    }
}

/* ------------------------------------------------------------ 事件：按钮墙 */

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
    int         idx = tool_by_id(id);

    (void)ev;
    (void)user;

    if (idx < 0) {
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
        if (lst) cg_widget_list_addf(lst, "共 %d 个工具，输入关键字过滤",
                                     K_TOOL_COUNT - K_MY_MAX + s_my_n);
        return;
    }

    for (i = 0; i < K_TOOL_COUNT && s_hit_n < K_HIT_MAX; i++) {
        const char *name = (strcmp(k_tools[i].cmd, "__my__") == 0)
                           ? s_my[k_tools[i].id[7] - '1'].name : k_tools[i].name;
        const char *cmd  = (strcmp(k_tools[i].cmd, "__my__") == 0)
                           ? s_my[k_tools[i].id[7] - '1'].cmd : k_tools[i].cmd;
        if (!name[0]) continue;                       /* 没配的 mytools 槽位不参加搜索 */
        if (ci_str(name, q) || ci_str(cmd, q)) {
            s_hit[s_hit_n++] = i;
            if (lst) cg_widget_list_addf(lst, "%s    (%s)", name, cmd);
        }
    }
    if (lst && s_hit_n == 0) cg_widget_list_add(lst, "（没有匹配的工具）");

    if (s_opt[2]) cg_tabs_select(cg_find(app, "tab_main"), 0);   /* AutoSearchPage */
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
    page_count_sync(app);
    status_msg(app, "已清空最近使用");
    log_line(app, "清空最近使用记录");
}

/**
 * @brief 打开用户数据目录（mytools.ini 所在处）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_open_data_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     *dir = cg_app_data_dir();

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过打开数据目录");
        cg_free(dir);
        return;
    }
    if (dir && cg_run_process(dir, 0) == 0) {
        status_msg(app, "已打开数据目录");
        log_line(app, "打开数据目录 %s", dir);
    } else {
        status_msg(app, "打开数据目录失败");
    }
    cg_free(dir);
}

/* ------------------------------------------------------------ 事件：我的工具 */

/**
 * @brief 「重新载入」：重读 mytools.ini 并刷新槽位按钮
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_my_reload_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过重读 mytools.ini");
        return;
    }
    mytools_reload(app, 1);
    page_count_sync(app);
    status_msg(app, "已载入 %d 个我的工具", s_my_n);
    log_line(app, "重新载入 mytools.ini：%d 个槽位生效", s_my_n);
}

/**
 * @brief 「编辑 mytools.ini」：没有就先写一份带注释的模板，再用记事本打开
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_my_edit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      cmdline[1200];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过编辑 mytools.ini");
        return;
    }
    if (!cg_file_exists(s_myini)) {
        static const char *tpl =
            "[t1]\nname=示例：显示系统版本\ncmd=cmd /k ver\n\n"
            "[t2]\nname=\ncmd=\n\n"
            "; 在 [t3]..[t8] 里照抄两行，改掉 name 和 cmd 即可加工具；空槽位灰着。\n";
        cg_file_write_bytes(s_myini, tpl, (int)strlen(tpl));
        log_line(app, "已生成模板 %s", s_myini);
    }
    snprintf(cmdline, sizeof(cmdline), "notepad \"%s\"", s_myini);
    if (cg_run_process(cmdline, 0) == 0) {
        status_msg(app, "已打开 mytools.ini");
        log_line(app, "编辑 %s", s_myini);
    } else {
        status_msg(app, "打开编辑器失败");
    }
}

/* ------------------------------------------------------------ 事件：底栏 */

/**
 * @brief 切页签：状态栏显示当前页名 + 刷新「本页 X 个」
 * @param self 标签页控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tab_main_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         page = cg_tabs_selected(self);
    const char *name = cg_widget_list_item(self, page);

    (void)ev;
    (void)user;

    page_count_sync(app);
    status_msg(app, "当前页：%s", name ? name : "?");
    log_line(app, "切到第 %d 页：%s", page + 1, name ? name : "?");
}

/**
 * @brief 底栏「搜索」：切到搜索页并把焦点交给输入框
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_search_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    cg_tabs_select(cg_find(app, "tab_main"), 0);
    cg_widget_focus(cg_find(app, "edt_search"));
}

/**
 * @brief 底栏「日志」：开非模态日志对话框（同一时间只允许一个对话框）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹窗是副作用：自检下只写日志
 */
static void on_btn_log_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过弹日志对话框");
        return;
    }
    if (cg_dialog_open_nonmodal(cg_find(app, "dlg_log")) == 1) {
        status_msg(app, "日志窗口已打开");
        log_line(app, "打开日志对话框");
    } else {
        status_msg(app, "已有一个对话框开着");
    }
}

/**
 * @brief 日志对话框「清空」
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_log_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;

    cg_widget_list_clear(cg_find(cg_app_of(self), "lst_log"));
    log_line(cg_app_of(self), "日志已清空");
}

/**
 * @brief 底栏「设置」：切换深浅主题并同步按钮文字（真功能，原版同款）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_theme_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过切主题");
        return;
    }
    s_dark = !s_dark;
    cg_theme_set_dark(app, s_dark);
    cg_widget_set_text(self, s_dark ? "切换到浅色" : "切换到深色");
    status_msg(app, s_dark ? "已切到深色主题" : "已切到浅色主题");
    log_line(app, "主题切换：%s", s_dark ? "深色" : "浅色");
}

/**
 * @brief 底栏「关于」：弹本工具箱的程序信息
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_about_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过弹「关于」");
        return;
    }
    cg_message_box(self,
                   "CStackGUI 工具箱启动器 1.1.0\n"
                   "功能面对标 mxx1-toolbox：按钮墙 / 搜索 / 成对开关 / 最近使用 / 我的工具\n"
                   "界面在 toolbox.cgui，行为在 main.c；我的工具改 mytools.ini 即可，不用重编译。",
                   "关于", 0);
}

/**
 * @brief 底栏「检查更新」：演示版不做联网检查，照实说明
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_update_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过弹「检查更新」");
        return;
    }
    cg_message_box(self,
                   "当前版本 1.1.0.0，已是最新。\n（示例不做联网检查，这行文案是照实写的）",
                   "检查更新", 0);
    log_line(app, "检查更新：演示版不联网");
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面建好：定 mytools.ini 路径、读开关与槽位、刷计数；自检下全部用默认值
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 就绪钩子不启动任何程序；读写盘/注册表只在有窗时做（自检红线）
 */
static void on_ready(cg_app_t *app, void *user)
{
    char *dir;
    int   has_win = cg_app_has_window(app);

    (void)user;

    dir = cg_app_data_dir();
    if (dir) {
        char *full = cg_path_join(dir, K_MY_INI);
        snprintf(s_myini, sizeof(s_myini), "%s", full ? full : K_MY_INI);
        cg_free(full);
        cg_free(dir);
    } else {
        snprintf(s_myini, sizeof(s_myini), K_MY_INI);
    }

    opt_load(has_win);
    rm_load(has_win);
    mytools_reload(app, has_win);
    recent_reload(app);
    opt_sync(app);
    rm_sync(app);
    search_apply(app);          /* 空关键字：显示"共 N 个工具"的提示行 */
    page_count_sync(app);

    if (!has_win) {
        /* 自检：把命令表全过一遍，反面由 on_tool_click 的「不同步」兜底 */
        int i, j;
        for (i = 0; i < K_TOOL_COUNT; i++) {
            if (strcmp(k_tools[i].cmd, "__my__") == 0) continue;
            for (j = i + 1; j < K_TOOL_COUNT; j++)
                if (strcmp(k_tools[i].id, k_tools[j].id) == 0)
                    log_line(app, "命令表异常：id %s 重复", k_tools[i].id);
        }
        log_line(app, "自检模式：命令表 %d 项对账完成，不启动任何程序", K_TOOL_COUNT);
        return;
    }

    status_msg(app, "就绪：点按钮、搜索回车、或先在常用设置里装开关");
    log_line(app, "工具箱就绪：%d 页 / %d 个内置工具 / %d 个我的工具",
             K_PAGE_MAX, K_TOOL_COUNT - K_MY_MAX, s_my_n);
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_tool_click",             on_tool_click),
    CG_BIND("on_edt_search_change",      on_edt_search_change),
    CG_BIND("on_edt_search_submit",      on_edt_search_submit),
    CG_BIND("on_lst_search_dblclick",    on_lst_search_dblclick),
    CG_BIND("on_lst_recent_dblclick",    on_lst_recent_dblclick),
    CG_BIND("on_btn_recent_clear_click", on_btn_recent_clear_click),
    CG_BIND("on_btn_open_data_click",    on_btn_open_data_click),
    CG_BIND("on_opt_click",              on_opt_click),
    CG_BIND("on_rmenu_click",            on_rmenu_click),
    CG_BIND("on_btn_regmenu_scan_click", on_btn_regmenu_scan_click),
    CG_BIND("on_btn_reg_view_click",     on_btn_reg_view_click),
    CG_BIND("on_btn_reg_reset_click",    on_btn_reg_reset_click),
    CG_BIND("on_btn_my_reload_click",    on_btn_my_reload_click),
    CG_BIND("on_btn_my_edit_click",      on_btn_my_edit_click),
    CG_BIND("on_tab_main_select",        on_tab_main_select),
    CG_BIND("on_btn_search_click",       on_btn_search_click),
    CG_BIND("on_btn_log_click",          on_btn_log_click),
    CG_BIND("on_btn_log_clear_click",    on_btn_log_clear_click),
    CG_BIND("on_btn_theme_click",        on_btn_theme_click),
    CG_BIND("on_btn_about_click",        on_btn_about_click),
    CG_BIND("on_btn_update_click",       on_btn_update_click),
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
