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
 *               （reg.exe add/delete，删得干净）；扫出来的任意一条还能选中后真操作 ——
 *               禁用（cg_reg_write 写 LegacyDisable）、恢复（cg_reg_del 删该值）、
 *               备份并删除（reg.exe export 出 .reg 后用 cg_reg_del_key 整键删，先弹确认框）、
 *               还原本次改动（禁用过的删值、删过的 reg import 备份），并列出「曾改动」。
 *
 *          与原版的诚实差异（照实写，不在示例里装）：
 *            - 原版按钮带彩色图标：本工程按钮没有贴图能力，用文字 + 配色区分；
 *            - 原版的系统级开关（任务栏合并/内核隔离等）要写系统注册表值并支持
 *              删值/备份，本工程注册表接口只动 HKCU、不碰系统键（HKLM），
 *              示例也不碰 —— 所以成对开关落在自家沙箱上，模式一致、落点无害；
 *              删值 / 删键 / 备份的能力本身在「右键增强」页真用上了；
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

/** 右键扫描结果行数上限（表行与 s_rm_rows 一一对应，超出的行只显示、不可操作） */
#define K_RM_ROW_MAX 256

/** 本次会话右键改动记录上限（禁用 / 删键各占一条） */
#define K_RM_CHG_MAX 64

/** 一次批量操作（勾选后禁用 / 恢复 / 删除）的目标条数上限 */
#define K_RM_BATCH 128

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

/** 注册表根：用户区 = HKCU（自己和用户装的，改得动）；系统区 = HKLM（程序装的，改它要管理员） */
enum { RM_ROOT_USER = 0, RM_ROOT_SYS = 1 };
static const char *k_rm_roots[] = { "HKCU", "HKLM" };

/**
 * 扫描位置（页签，与 .cgui 里 rmtab0..rmtab5 同序）：base 是**类名**，
 * shell（程序装的菜单）与 shellex（DLL 扩展项）两类键都挂在它下面；
 * 每类都要在用户区和系统区各扫一遍 —— 参考实现按"右键点在哪"分页，正是这个口径。
 */
static const struct {
    const char *base;   /**< 类名，如 "*"、"Directory" */
    const char *label;  /**< 页签名 */
    const char *tab;    /**< .cgui 里页签按钮的 id */
} k_rm_areas[] = {
    { "*",                     "文件",        "rmtab0" },
    { "Directory",             "文件夹",      "rmtab1" },
    { "Directory\\Background", "文件夹空白处", "rmtab2" },
    { "DesktopBackground",     "桌面",        "rmtab3" },
    { "Drive",                 "磁盘",        "rmtab4" },
    { "AllFilesystemObjects",  "通用对象",    "rmtab5" }
};

/** 扫描位置数 */
#define K_RM_AREA_COUNT ((int)(sizeof k_rm_areas / sizeof k_rm_areas[0]))

/** 扫描结果里一项的状态分类：在用 / 已隐藏（有 LegacyDisable）/ 无命令（只有容器壳） */
enum { RM_ACTIVE = 0, RM_HIDDEN = 1, RM_NOCOMMAND = 2 };

/** 分组（参考实现的三组）：工具箱自己装的 / 程序装的 / DLL 扩展项（只显示不给动） */
enum { RM_G_OURS = 0, RM_G_PROG = 1, RM_G_EXT = 2 };
static const char *k_rm_groups[] = { "工具箱装的", "程序装的", "扩展项" };

/** 过滤按钮（与 .cgui 里 rmf0..rmf4 同序：全部 / 未禁用 / 程序装的 / 已禁用 / 扩展项） */
enum { RM_F_ALL = 0, RM_F_ON, RM_F_PROG, RM_F_OFF, RM_F_EXT, RM_F_COUNT };
static const char *k_rm_filter_ids[RM_F_COUNT] = { "rmf0", "rmf1", "rmf2", "rmf3", "rmf4" };

/**
 * 扫描结果行：一行 = 某个位置里的一条菜单（页签 + 过滤 + 搜索筛完才上表）。
 * 表格的 row-value 存本数组下标 —— 过滤后行号与本数组不再同序，选中行靠它翻回子键。
 */
static struct {
    char sub[620];   /**< 子键路径（含 Software\Classes），与扫描用的 path[600] 等宽，装不下宁可大不截断 */
    char title[300]; /**< 菜单标题（搜索按它匹配） */
    char cmd[512];   /**< 命令行（shell → command 子键；扩展项 → DLL 路径 / CLSID） */
    int  kind;       /**< RM_ACTIVE / RM_HIDDEN / RM_NOCOMMAND */
    int  root;       /**< RM_ROOT_USER / RM_ROOT_SYS */
    int  area;       /**< k_rm_areas 下标 */
    int  group;      /**< RM_G_* */
} s_rm_rows[K_RM_ROW_MAX];

/** 扫描到的全部行数（含没显示在当前页签上的） */
static int s_rm_row_n;

/** 行数到上限被截断（K_RM_ROW_MAX 满了）；日志里要说明"共 N 条"是截断值 */
static int s_rm_row_limit;

/** 每个位置扫到几条（页签按钮上的数字） */
static int s_rm_tab_n[K_RM_AREA_COUNT];

/** 当前页签 / 过滤 / 搜索词 / 是否扫过（没扫过时点页签会先扫一次） */
static int  s_rm_tab;
static int  s_rm_filter = RM_F_ALL;
static char s_rm_search[64];
static int  s_rm_scanned;

/** 本次会话动过的右键项：禁用、或整键删除（记下备份 .reg 路径 + 根） */
static struct {
    char sub[620];   /**< 子键路径（含 Software\Classes，与 s_rm_rows[].sub 同宽） */
    char bak[1024];  /**< 删除前的备份 .reg 全路径；空串 = 只禁用了、没删键 */
    int  root;       /**< RM_ROOT_USER / RM_ROOT_SYS */
} s_rm_changed[K_RM_CHG_MAX];

/** 改动记录条数 */
static int s_rm_changed_n;

/** 备份 .reg 存放目录（数据目录；就绪钩子里填） */
static char s_rm_bakdir[1024];

/** 待确认删除的目标行（s_rm_rows 下标）；0 条 = 没有待确认的（确认框是模态的，弹之前先滤好） */
static int s_rm_pending_rows[K_RM_BATCH];
static int s_rm_pending_n;

/** 备份文件名去重用序号（同一秒内连删两项也不覆盖） */
static int s_rm_bakseq;

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
 * @brief 刷新「曾改动」提示行：列出本次会话动过的项（只列键名末段，太长就截）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_changed_sync(cg_app_t *app)
{
    char buf[768];
    int  i, off = 0, del = 0;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].bak[0]) del++;

    if (s_rm_changed_n == 0) {
        snprintf(buf, sizeof(buf), "曾改动：无");
    } else {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                        "曾改动 %d 项（删键 %d）：", s_rm_changed_n, del);
        for (i = 0; i < s_rm_changed_n && off < (int)sizeof(buf) - 64; i++) {
            const char *p = strrchr(s_rm_changed[i].sub, '\\');
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s%s%s",
                            s_rm_changed[i].root == RM_ROOT_SYS ? "[系]" : "",
                            p ? p + 1 : s_rm_changed[i].sub,
                            (i + 1 < s_rm_changed_n) ? "、" : "");
        }
    }
    cg_widget_set_text(cg_find(app, "lab_rm_changed"), buf);
}

/**
 * @brief rm_fill 的前向声明（rm_scan 里先用后定义，这里先占位）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_fill(cg_app_t *app);

/**
 * @brief 不区分 ASCII 大小写的子串查找（搜索框用；中文按原样比，不做折字）
 * @param s 被搜的串
 * @param needle 要找的子串；空串算命中
 * @return int 1 = 命中
 */
static int rm_has_ci(const char *s, const char *needle)
{
    if (!needle || !*needle) return 1;
    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b) {
            int ca = (unsigned char)*a;
            int cb = (unsigned char)*b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/**
 * @brief 这条子键是不是本工具箱自己装的那两个（分组「工具箱装的」靠它认）
 * @param sub 子键路径（相对 Software\Classes）
 * @return int k_rmenu 下标 + 1；不是自己装的返回 0
 */
static int rm_is_ours(const char *sub)
{
    int i;
    for (i = 0; i < K_RMENU_COUNT; i++) {
        char want[600];
        snprintf(want, sizeof(want), "Software\\Classes\\%s", k_rmenu[i].key);
        if (_stricmp(sub, want) == 0) return i + 1;
    }
    return 0;
}

/**
 * @brief 扫一个「菜单集合键」（某位置下的 shell 或 shellex）的直接子键，逐条归类进 s_rm_rows
 * @param root_i RM_ROOT_USER / RM_ROOT_SYS（对应 "HKCU" / "HKLM"）
 * @param sub 集合键路径（相对 Software\Classes），如 *\\shell
 * @param area 位置下标（k_rm_areas）
 * @param base_group 该类键的默认分组：shell → RM_G_PROG，shellex → RM_G_EXT
 * @param ri 下一个可写行号
 * @return int 新的行号
 * @note 纯只读；shellex 那类只取名字与 DLL（参考实现的口径：只显示、不给动）。
 */
static int rm_scan_folder(int root_i, const char *sub, int area, int base_group, int ri)
{
    const char *root = k_rm_roots[root_i];
    char        buf[8192];
    int         n, i = 0;

    n = cg_reg_subkeys_ex(root, sub, buf, sizeof(buf));
    while (i < n && ri < K_RM_ROW_MAX) {
        int         j = i;
        char        path[600];
        char        cmdkey[768];
        char        title[300];
        char        cmd[512];
        char       *s1, *s2;
        int         kind;
        int         head;

        while (j < n && buf[j] != '\n') j++;
        /* 行边界截断：%s 会穿过 '\n' 分隔符把整个剩余 buf 拼进 path，
         * RegOpenKeyExW 打不开带换行的路径，command/标题会全体读空。
         * fallback 的键名同理，且必须取本行（推进 i 之前）而不是下一行。 */
        head = i;
        snprintf(path, sizeof(path), "%s\\%.*s", sub, j - head, &buf[head]);
        i = j + 1;
        if (base_group == RM_G_EXT) {
            char clsid[128];
            char cpath[768];
            int  is_guid;

            /* 扩展项：名字看 MUIVerb / 默认值，命令列给 DLL（能解析就带 CLSID 说明） */
            s1 = cg_reg_read_ex(root, path, "MUIVerb", NULL);
            s2 = cg_reg_read_ex(root, path, "", NULL);
            if (s1 && *s1)      snprintf(title, sizeof(title), "%s", s1);
            else if (s2 && *s2) snprintf(title, sizeof(title), "%s", s2);
            else                snprintf(title, sizeof(title), "%.*s", j - head, &buf[head]);
            cg_free(s1);
            cg_free(s2);
            snprintf(cmd, sizeof(cmd), "%.*s", j - head, &buf[head]);
            /* 默认值是 {GUID} 的（写法很常见）：把 GUID 变成"组件名 + DLL"再显示 */
            s1 = cg_reg_read_ex(root, path, "", NULL);
            is_guid = (s1 && s1[0] == '{');
            snprintf(clsid, sizeof(clsid), "%s", is_guid ? s1 : "");
            cg_free(s1);
            if (is_guid) {
                char *cname;
                char *dll;
                snprintf(cpath, sizeof(cpath), "CLSID\\%s", clsid);
                cname = cg_reg_read_ex("HKCR", cpath, "", NULL);
                if (cname && *cname && strlen(cname) < sizeof(title))
                    snprintf(title, sizeof(title), "%s", cname);
                cg_free(cname);
                snprintf(cpath, sizeof(cpath), "CLSID\\%s\\InprocServer32", clsid);
                dll = cg_reg_read_ex("HKCR", cpath, "", NULL);
                if (dll && *dll) snprintf(cmd, sizeof(cmd), "%s", dll);
                else             snprintf(cmd, sizeof(cmd), "%s", clsid);
                cg_free(dll);
            }
            kind = RM_ACTIVE;
        } else {
            char *legacy;

            /* shell 菜单：MUIVerb / 默认值 → 标题；command → 命令；LegacyDisable → 已隐藏 */
            s1 = cg_reg_read_ex(root, path, "MUIVerb", NULL);
            s2 = cg_reg_read_ex(root, path, "", NULL);
            if (s1 && *s1)      snprintf(title, sizeof(title), "%s", s1);
            else if (s2 && *s2) snprintf(title, sizeof(title), "%s", s2);
            else                snprintf(title, sizeof(title), "%.*s", j - head, &buf[head]);
            cg_free(s1);
            cg_free(s2);
            snprintf(cmdkey, sizeof(cmdkey), "%s\\command", path);
            s1 = cg_reg_read_ex(root, cmdkey, "", NULL);
            if (s1 && *s1) snprintf(cmd, sizeof(cmd), "%s", s1);
            else            cmd[0] = '\0';
            cg_free(s1);
            legacy = cg_reg_read_ex(root, path, "LegacyDisable", NULL);
            if (!cmd[0])      kind = RM_NOCOMMAND;   /* 只有容器壳，没 command */
            else if (legacy)  kind = RM_HIDDEN;      /* 空值存在 = 菜单被隐藏 */
            else              kind = RM_ACTIVE;
            cg_free(legacy);
        }

        snprintf(s_rm_rows[ri].sub,   sizeof(s_rm_rows[0].sub),   "%s", path);
        snprintf(s_rm_rows[ri].title, sizeof(s_rm_rows[0].title), "%s", title);
        snprintf(s_rm_rows[ri].cmd,   sizeof(s_rm_rows[0].cmd),   "%s", cmd);
        s_rm_rows[ri].kind  = kind;
        s_rm_rows[ri].root  = root_i;
        s_rm_rows[ri].area  = area;
        s_rm_rows[ri].group = (base_group != RM_G_EXT && rm_is_ours(path)) ? RM_G_OURS : base_group;
        ri++;
        s_rm_tab_n[area]++;
    }
    return ri;
}

/**
 * @brief 扫描六个位置（用户区 HKCU + 系统区 HKLM，含 shellex 扩展项），重建 s_rm_rows 并刷表
 * @param app 应用
 * @return void 无返回值
 * @note 纯只读；表格整表重建用 begin/end update 围住，不围就是逐行重建，几百条会连续闪。
 *       状态判定：command 有值且没有 LegacyDisable = 在用；有 LegacyDisable = 已禁用；
 *       读不到 command = 无命令。shellex 扩展项不看 LegacyDisable（那是 shell 键的开关）。
 */
static void rm_scan(cg_app_t *app)
{
    char key[600];
    int  a, r, ri = 0;

    s_rm_row_n = 0;
    s_rm_scanned = 1;
    s_rm_row_limit = 0;
    memset(s_rm_tab_n, 0, sizeof(s_rm_tab_n));

    for (a = 0; a < K_RM_AREA_COUNT && !s_rm_row_limit; a++) {
        for (r = 0; r < 2 && !s_rm_row_limit; r++) {
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\shell", k_rm_areas[a].base);
            ri = rm_scan_folder(r, key, a, RM_G_PROG, ri);
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\shellex\\ContextMenuHandlers",
                     k_rm_areas[a].base);
            ri = rm_scan_folder(r, key, a, RM_G_EXT, ri);
            if (ri >= K_RM_ROW_MAX) s_rm_row_limit = 1;
        }
    }
    s_rm_row_n = ri;
    rm_fill(app);
    rm_changed_sync(app);
    status_msg(app, "扫描完成：%d 条 · 位置「%s」%d 条", s_rm_row_n,
               k_rm_areas[s_rm_tab].label, s_rm_tab_n[s_rm_tab]);
    log_line(app, "扫描右键菜单（HKCU + HKLM，%d 个位置，含 shellex 扩展项）：共 %d 条%s",
             K_RM_AREA_COUNT, s_rm_row_n,
             s_rm_row_limit ? "（到上限被截断）" : "");
}

/**
 * @brief 这一行在当前视图里吗（页签 + 过滤 + 搜索三个条件都过才算）
 * @param i s_rm_rows 下标
 * @return int 1 = 显示
 */
static int rm_match(int i)
{
    if (s_rm_rows[i].area != s_rm_tab) return 0;
    switch (s_rm_filter) {
    case RM_F_ON:   if (s_rm_rows[i].kind == RM_HIDDEN) return 0; break;
    case RM_F_OFF:  if (s_rm_rows[i].kind != RM_HIDDEN) return 0; break;
    case RM_F_PROG: if (s_rm_rows[i].group != RM_G_PROG) return 0; break;
    case RM_F_EXT:  if (s_rm_rows[i].group != RM_G_EXT) return 0; break;
    default: break;
    }
    return rm_has_ci(s_rm_rows[i].title, s_rm_search);
}

/**
 * @brief 刷新页签数字、过滤按钮的「当前」标记、统计行与批量按钮文案（只动文字，不重建表）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_stat_sync(cg_app_t *app)
{
    cg_widget_t  *tbl = cg_find(app, "tbl_regmenu");
    char          buf[420];
    int           rows = tbl ? cg_table_row_count(tbl) : 0;
    int           i, chk = 0, on = 0, off = 0, noc = 0, ext = 0, sys = 0;
    static const char *flab[RM_F_COUNT] = { "全部", "未禁用", "程序装的", "已禁用", "扩展项" };

    for (i = 0; i < rows; i++) {
        int di = cg_table_row_value(tbl, i);
        if (cg_table_checked(tbl, i)) chk++;
        if (di < 0 || di >= s_rm_row_n) continue;
        if (s_rm_rows[di].root == RM_ROOT_SYS) sys++;
        if (s_rm_rows[di].group == RM_G_EXT) ext++;
        else if (s_rm_rows[di].kind == RM_HIDDEN) off++;
        else if (s_rm_rows[di].kind == RM_NOCOMMAND) noc++;
        else on++;
    }

    for (i = 0; i < K_RM_AREA_COUNT; i++) {
        cg_widget_t *w = cg_find(app, k_rm_areas[i].tab);
        snprintf(buf, sizeof(buf), "%s %d", k_rm_areas[i].label, s_rm_tab_n[i]);
        if (w) cg_widget_set_text(w, buf);
    }
    for (i = 0; i < RM_F_COUNT; i++) {
        cg_widget_t *w = cg_find(app, k_rm_filter_ids[i]);
        snprintf(buf, sizeof(buf), "%s%s", (i == s_rm_filter) ? "✓ " : "", flab[i]);
        if (w) cg_widget_set_text(w, buf);
    }

    if (!s_rm_scanned) {
        snprintf(buf, sizeof(buf), "（还没有扫描：点上面「扫描右键菜单」才读注册表）");
    } else {
        snprintf(buf, sizeof(buf),
                 "已勾选 %d 项 · 共 %d 条（在用 %d / 已禁用 %d / 无命令 %d / 扩展项 %d；"
                 "其中 %d 条在系统区，改它要管理员）· %s",
                 chk, rows, on, off, noc, ext, sys, k_rm_areas[s_rm_tab].label);
    }
    cg_widget_set_text(cg_find(app, "lab_rm_stat"), buf);

    if (chk > 0) {
        snprintf(buf, sizeof(buf), "禁用选中的 %d 项", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_disable"), buf);
        snprintf(buf, sizeof(buf), "恢复选中的 %d 项", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_enable"), buf);
        snprintf(buf, sizeof(buf), "备份并删除选中的 %d 项…", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_backup_del"), buf);
    } else {
        cg_widget_set_text(cg_find(app, "btn_rm_disable"), "禁用选中项");
        cg_widget_set_text(cg_find(app, "btn_rm_enable"), "恢复选中项");
        cg_widget_set_text(cg_find(app, "btn_rm_backup_del"), "备份并删除选中…");
    }
}

/**
 * @brief 按当前页签 / 过滤 / 搜索重建扫描结果表，然后刷新统计
 * @param app 应用
 * @return void 无返回值
 * @note 表行的 row-value 存 s_rm_rows 下标：过滤后两者不再同序，操作按钮靠它翻回子键。
 *       重建过程不要动勾选状态 —— 用户点勾选框只会触发 rm_stat_sync，不会走进来。
 */
static void rm_fill(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int          i;

    if (tbl) cg_table_begin_update(tbl);
    if (tbl) cg_table_clear(tbl);
    for (i = 0; i < s_rm_row_n; i++) {
        char        title[300];
        char        cmd[600];
        char        state[64];
        const char *cells[4];

        if (!rm_match(i)) continue;
        snprintf(title, sizeof(title), "%s", s_rm_rows[i].title);
        snprintf(cmd, sizeof(cmd), "%s", s_rm_rows[i].cmd[0] ? s_rm_rows[i].cmd : "—");
        if (s_rm_rows[i].group == RM_G_EXT) {
            snprintf(state, sizeof(state), "扩展项·%s", s_rm_rows[i].root == RM_ROOT_SYS
                     ? "系统区" : "用户区");
        } else {
            const char *k = s_rm_rows[i].kind == RM_HIDDEN ? "已禁用"
                          : s_rm_rows[i].kind == RM_NOCOMMAND ? "无命令" : "在用";
            snprintf(state, sizeof(state), "%s·%s", k,
                     s_rm_rows[i].root == RM_ROOT_SYS ? "系统区" : "用户区");
        }
        cells[0] = title;
        cells[1] = k_rm_groups[s_rm_rows[i].group];
        cells[2] = cmd;
        cells[3] = state;
        if (tbl) {
            int r = cg_table_add_row(tbl, cells, 4);
            if (r >= 0) cg_table_set_row_value(tbl, r, i);
        }
    }
    if (tbl) cg_table_end_update(tbl);
    rm_stat_sync(app);
}

/**
 * @brief 「扫描右键菜单」按钮：按自检红线先挡掉无窗（不开窗不碰注册表）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_regmenu_scan_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过扫描右键菜单（不碰注册表）");
        return;
    }
    rm_scan(app);
}

/**
 * @brief 位置页签：切到某个位置（没扫过就先扫一次 —— 点页签就是「我要看这个位置」）
 * @param self 被点的页签按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_tab_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         i;

    (void)ev;
    (void)user;
    for (i = 0; i < K_RM_AREA_COUNT; i++)
        if (id && strcmp(id, k_rm_areas[i].tab) == 0) break;
    if (i >= K_RM_AREA_COUNT) return;
    s_rm_tab = i;
    if (!s_rm_scanned) {
        if (!cg_app_has_window(app)) {
            log_line(app, "自检模式：跳过扫描右键菜单（不碰注册表）");
            return;
        }
        rm_scan(app);
    } else {
        rm_fill(app);
    }
}

/**
 * @brief 过滤按钮：全部 / 未禁用 / 程序装的 / 已禁用 / 扩展项
 * @param self 被点的过滤按钮（rmf0..rmf4）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_filter_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         i;

    (void)ev;
    (void)user;
    for (i = 0; i < RM_F_COUNT; i++)
        if (id && strcmp(id, k_rm_filter_ids[i]) == 0) break;
    if (i >= RM_F_COUNT) return;
    s_rm_filter = i;
    rm_fill(app);
}

/**
 * @brief 搜索框：按标题子串过滤（不区分 ASCII 大小写，中文按原样比）
 * @param self 搜索框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 没扫过就只记下词，等扫描 —— 不因为打字就去碰注册表。
 */
static void on_rm_search_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    const char  *t   = cg_widget_text(self);

    (void)ev;
    (void)user;
    snprintf(s_rm_search, sizeof(s_rm_search), "%s", t ? t : "");
    if (s_rm_scanned) rm_fill(app);
}

/**
 * @brief 勾选变化：只刷统计行与批量按钮文案，**不**重建表 —— 重建会把勾选清掉
 * @param self 表格
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    rm_stat_sync(app);
}

/**
 * @brief 取当前选中的扫描结果行
 * @param app 应用
 * @return int 行下标；没选中或超出可操作范围返回 -1
 */
static int rm_sel_row(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int          r   = tbl ? cg_table_selected(tbl) : -1;
    int          di;

    if (!tbl || r < 0 || r >= cg_table_row_count(tbl)) return -1;
    di = cg_table_row_value(tbl, r);     /* 表行 → s_rm_rows 下标（页签/过滤/搜索筛过就不同序） */
    return (di >= 0 && di < s_rm_row_n) ? di : -1;
}

/**
 * @brief 收集本次操作的目标行：勾选的行优先，一条没勾就退化成「当前选中的那一行」
 * @param app 应用
 * @param out 出口数组（存 s_rm_rows 下标）
 * @param cap 出口容量
 * @return int 目标条数；0 = 没有可操作的目标
 */
static int rm_target_rows(cg_app_t *app, int *out, int cap)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int         n = 0;
    int         i, sel;

    if (tbl) {
        for (i = 0; i < cg_table_row_count(tbl) && n < cap; i++) {
            int di;
            if (!cg_table_checked(tbl, i)) continue;
            di = cg_table_row_value(tbl, i);
            if (di >= 0 && di < s_rm_row_n) out[n++] = di;
        }
    }
    if (n > 0) return n;

    sel = rm_sel_row(app);
    if (sel >= 0 && n < cap) out[n++] = sel;
    return n;
}

/**
 * @brief 这一行动不动得；不能动时给出用户看得懂的理由
 * @param di s_rm_rows 下标
 * @param admin 当前进程是否已提权（调用方查一次 token，别每行都问）
 * @return const char *NULL = 可以动；否则就是拒绝理由（扩展项只显示不给动 / 系统区要管理员）
 */
static const char *rm_block_reason(int di, int admin)
{
    if (di < 0 || di >= s_rm_row_n) return "行号无效（请重新扫描）";
    if (s_rm_rows[di].group == RM_G_EXT)
        return "扩展项（shellex DLL 注册）只显示不给动";
    if (s_rm_rows[di].root == RM_ROOT_SYS && !admin)
        return "系统区（HKLM）要管理员身份：以管理员重新运行本工具再改";
    return NULL;
}

/**
 * @brief 把一条改动记进 s_rm_changed（同一根下同一子键不重复记）
 * @param root RM_ROOT_USER / RM_ROOT_SYS（还原时按根删值 / 导备份）
 * @param sub 子键路径（相对根，含 Software\Classes）
 * @param bak 删除前的备份 .reg 路径；传 NULL 表示只禁用、没删键
 * @return void 无返回值
 */
static void rm_changed_add(int root, const char *sub, const char *bak)
{
    int i;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].root == root && strcmp(s_rm_changed[i].sub, sub) == 0) {
            if (bak && *bak) snprintf(s_rm_changed[i].bak, sizeof(s_rm_changed[0].bak), "%s", bak);
            return;
        }
    if (s_rm_changed_n >= K_RM_CHG_MAX) return;
    snprintf(s_rm_changed[s_rm_changed_n].sub, sizeof(s_rm_changed[0].sub), "%s", sub);
    snprintf(s_rm_changed[s_rm_changed_n].bak, sizeof(s_rm_changed[0].bak), "%s", bak ? bak : "");
    s_rm_changed[s_rm_changed_n].root = root;
    s_rm_changed_n++;
}

/**
 * @brief 把一条改动从 s_rm_changed 去掉（单项恢复成功之后）
 * @param root 记录时的根（同一子键名在 HKCU/HKLM 各算一条，别删串了）
 * @param sub 子键路径（相对根，含 Software\Classes）
 * @return void 无返回值
 */
static void rm_changed_remove(int root, const char *sub)
{
    int i;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].root == root && strcmp(s_rm_changed[i].sub, sub) == 0) {
            s_rm_changed[i] = s_rm_changed[s_rm_changed_n - 1];
            s_rm_changed_n--;
            return;
        }
}

/**
 * @brief 对一个扫描结果行做「导出备份 + 整键删除」
 * @param app 应用
 * @param r 扫描结果行下标
 * @return int 0 = 备份并删除成功；-1 = 中止（备份没成功或删键失败）
 * @note 备份先成功才敢删 —— reg export 没过就中止，绝不出现"删了没备份"。
 *       reg.exe export 落在数据目录，文件名带时间戳 + 序号，同秒连删也不互相覆盖。
 *       根按行自己记的（HKCU/HKLM）走；批量删由调用方在循环后统一重扫一次。
 */
static int rm_delete_row(cg_app_t *app, int r)
{
    const char *root = k_rm_roots[s_rm_rows[r].root];
    char  rel[1200];
    char  name[96];
    char *ts, *bak = NULL;
    char  cmd[2600];
    int   attempt;

    ts = cg_time_now("rmenu-%Y%m%d-%H%M%S");
    for (attempt = 0; attempt < 100; attempt++) {
        snprintf(name, sizeof(name), "%s-%d.reg", ts ? ts : "rmenu-backup", s_rm_bakseq++);
        bak = cg_path_join(s_rm_bakdir[0] ? s_rm_bakdir : ".", name);
        if (bak && !cg_file_exists(bak)) break;
        cg_free(bak);
        bak = NULL;
    }
    cg_free(ts);
    if (!bak) {
        status_msg(app, "备份失败：拿不到可用的备份文件名");
        log_line(app, "备份失败：文件名冲突或路径不可用");
        return -1;
    }

    snprintf(rel, sizeof(rel), "%s\\%s", root, s_rm_rows[r].sub);
    snprintf(cmd, sizeof(cmd), "reg export \"%s\" \"%s\" /y", rel, bak);
    if (cg_run_process(cmd, 1) != 0) {
        status_msg(app, "备份失败：reg export 没成功，已中止删除");
        log_line(app, "reg export 失败：%s", cmd);
        cg_free(bak);
        return -1;
    }

    if (cg_reg_del_key_ex(root, s_rm_rows[r].sub) != 0) {
        status_msg(app, "删除失败（备份仍留在 %s）", cg_path_name(bak));
        log_line(app, "删键失败（备份已留）：%s\\%s", root, s_rm_rows[r].sub);
        rm_changed_add(s_rm_rows[r].root, s_rm_rows[r].sub, bak);
        cg_free(bak);
        rm_changed_sync(app);
        return -1;
    }

    rm_changed_add(s_rm_rows[r].root, s_rm_rows[r].sub, bak);
    log_line(app, "已删除右键菜单项并备份：%s\\%s -> %s", root, s_rm_rows[r].sub, bak);
    status_msg(app, "已删除并备份到 %s", cg_path_name(bak));
    cg_free(bak);
    return 0;
}

/**
 * @brief 「禁用选中（隐藏）」：给目标项写 LegacyDisable 空值
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写空串 = 值「存在」= 菜单项被隐藏；恢复要删掉这个值（见 on_rm_enable_click）。
 *       目标 = 勾选的行（批量），一条没勾就退化成选中的那一行；
 *       扩展项、系统区未提权的行按条跳过并写日志，绝不静默失败。
 */
static void on_rm_disable_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       tg[K_RM_BATCH];
    int       n, i, admin, ok = 0, skip = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过禁用右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    for (i = 0; i < n; i++) {
        int         di = tg[i];
        const char *why = rm_block_reason(di, admin);

        if (why) {
            skip++;
            log_line(app, "跳过禁用 %s\\%s：%s", k_rm_roots[s_rm_rows[di].root],
                     s_rm_rows[di].sub, why);
            continue;
        }
        if (s_rm_rows[di].kind == RM_NOCOMMAND) {
            skip++;
            log_line(app, "跳过禁用 %s\\%s：没有命令（多为 shellex 残键），禁用没有意义",
                     k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        if (s_rm_rows[di].kind == RM_HIDDEN) { skip++; continue; }   /* 已经是隐藏的，不用重复写 */
        if (cg_reg_write_ex(k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub,
                            "LegacyDisable", "") != 0) {
            fail++;
            log_line(app, "禁用失败：%s\\%s", k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        rm_changed_add(s_rm_rows[di].root, s_rm_rows[di].sub, NULL);
        log_line(app, "已隐藏右键菜单项：%s\\%s", k_rm_roots[s_rm_rows[di].root],
                 s_rm_rows[di].sub);
        ok++;
    }

    if (ok == 0 && fail == 0) {
        status_msg(app, "没有可禁用的项（%d 项跳过：已隐藏 / 扩展项 / 系统区未提权，见日志）", skip);
        return;
    }
    rm_scan(app);
    if (fail) status_msg(app, "禁用完成：%d 项成功，%d 项失败，%d 项跳过（详见日志）", ok, fail, skip);
    else status_msg(app, "已隐藏 %d 项（写了 LegacyDisable，可用「恢复选中」还原；%d 项跳过）",
                    ok, skip);
}

/**
 * @brief 「恢复选中」：删掉目标项的 LegacyDisable 值，菜单项重新出现
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 目标口径与 on_rm_disable_click 相同：勾选优先，没勾选就用选中行。
 */
static void on_rm_enable_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       tg[K_RM_BATCH];
    int       n, i, admin, ok = 0, skip = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过恢复右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    for (i = 0; i < n; i++) {
        int         di = tg[i];
        const char *why = rm_block_reason(di, admin);

        if (why) {
            skip++;
            log_line(app, "跳过恢复 %s\\%s：%s", k_rm_roots[s_rm_rows[di].root],
                     s_rm_rows[di].sub, why);
            continue;
        }
        if (s_rm_rows[di].kind != RM_HIDDEN) { skip++; continue; }   /* 没被隐藏的不用恢复 */
        if (cg_reg_del_ex(k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub,
                          "LegacyDisable") != 0) {
            fail++;
            log_line(app, "恢复失败：%s\\%s", k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        rm_changed_remove(s_rm_rows[di].root, s_rm_rows[di].sub);
        log_line(app, "已恢复右键菜单项：%s\\%s", k_rm_roots[s_rm_rows[di].root],
                 s_rm_rows[di].sub);
        ok++;
    }

    if (ok == 0 && fail == 0) {
        status_msg(app, "没有可恢复的项（%d 项跳过：不是隐藏状态 / 扩展项 / 系统区未提权）", skip);
        return;
    }
    rm_scan(app);
    if (fail) status_msg(app, "恢复完成：%d 项成功，%d 项失败，%d 项跳过（详见日志）", ok, fail, skip);
    else status_msg(app, "已恢复 %d 项（删掉了 LegacyDisable；%d 项跳过）", ok, skip);
}

/**
 * @brief 「备份并删除选中」：先弹模态确认框，确认后才导出备份 + 整键删除
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 破坏性操作用 cg_dialog_open 做真确认（cg_message_box 没有返回值，
 *       拿不到"用户点了哪个"）。待删行放 s_rm_pending_rows，确认框回调只负责关框。
 *       不能动的行（扩展项 / 系统区未提权）在**弹框之前**就滤掉并写日志 ——
 *       弹了框才跳过，用户会以为全删了。
 */
static void on_rm_backup_del_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *dlg;
    char         txt[1600];
    char         list[900];
    char         extra[160];
    int          tg[K_RM_BATCH];
    int          n, i, admin, off = 0, skip = 0, done = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过删除右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    s_rm_pending_n = 0;
    for (i = 0; i < n; i++) {
        const char *why = rm_block_reason(tg[i], admin);
        if (why) {
            skip++;
            log_line(app, "跳过删除 %s\\%s：%s", k_rm_roots[s_rm_rows[tg[i]].root],
                     s_rm_rows[tg[i]].sub, why);
            continue;
        }
        s_rm_pending_rows[s_rm_pending_n++] = tg[i];
    }
    if (s_rm_pending_n == 0) {
        status_msg(app, "没有可删除的项（%d 项跳过：扩展项只显示不给动 / 系统区未提权）", skip);
        return;
    }

    /* 确认文案最多列 4 条，长路径也要写得下（snprintf 超限时返回"本应写入"的长度，别直接加） */
    list[0] = 0;
    for (i = 0; i < s_rm_pending_n && i < 4; i++) {
        int    di = s_rm_pending_rows[i];
        size_t left = sizeof(list) - (size_t)off;
        int    w;

        if (left < 100) break;
        w = snprintf(list + off, left, "%s\\%s\r\n",
                     k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
        if (w < 0) break;
        off += (w < (int)left) ? w : (int)left - 1;
    }
    if (s_rm_pending_n > 4) {
        size_t left = sizeof(list) - (size_t)off;
        if (left >= 40) snprintf(list + off, left, "……等共 %d 项\r\n", s_rm_pending_n);
    }
    if (skip) snprintf(extra, sizeof(extra),
                       "\r\n\r\n另有 %d 项不能动，已跳过（扩展项只显示不给动 / 系统区未提权，见日志）。", skip);
    else extra[0] = 0;

    snprintf(txt, sizeof(txt),
             "将整键删除这 %d 个右键菜单项（逐个先导出 .reg 备份到数据目录）：\r\n\r\n%s\r\n"
             "之后可用「还原本次改动」或直接双击备份 .reg 文件导回。%s",
             s_rm_pending_n, list, extra);
    dlg = cg_find(app, "dlg_rm_confirm");
    if (dlg) cg_widget_set_text(cg_find(app, "edt_rmc_text"), txt);

    if (cg_dialog_open(dlg) != 1) {
        s_rm_pending_n = 0;
        log_line(app, "删除右键菜单项：用户取消");
        status_msg(app, "已取消删除");
        return;
    }

    for (i = 0; i < s_rm_pending_n; i++)
        if (rm_delete_row(app, s_rm_pending_rows[i]) == 0) done++;
        else fail++;
    s_rm_pending_n = 0;

    rm_scan(app);
    if (fail) status_msg(app, "已删除 %d 项，%d 项失败（备份都留着，详见日志）", done, fail);
    else status_msg(app, "已删除并备份 %d 项（可用「还原本次改动」导回）", done);
}

/**
 * @brief 「还原本次改动」：禁用过的删掉 LegacyDisable、删过的从备份 .reg 导回
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 逐项处理，成功的从改动记录里剔除；失败的留在记录里（提示与「曾改动」都对得上）。
 */
static void on_rm_restore_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       i, keep = 0, done = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过还原右键改动（不碰注册表）");
        return;
    }
    if (s_rm_changed_n == 0) {
        status_msg(app, "本次会话还没有改动过右键菜单");
        return;
    }

    for (i = 0; i < s_rm_changed_n; i++) {
        int         good;
        const char *root = k_rm_roots[s_rm_changed[i].root];

        if (s_rm_changed[i].bak[0]) {
            char cmd[1400];
            snprintf(cmd, sizeof(cmd), "reg import \"%s\"", s_rm_changed[i].bak);
            good = (cg_run_process(cmd, 1) == 0);
            if (!good) log_line(app, "reg import 失败：%s", cmd);
        } else {
            good = (cg_reg_del_ex(root, s_rm_changed[i].sub, "LegacyDisable") == 0);
            if (!good) log_line(app, "取消隐藏失败：%s\\%s", root, s_rm_changed[i].sub);
        }
        if (good) done++;
        else { fail++; s_rm_changed[keep++] = s_rm_changed[i]; }   /* 只留没还原成功的 */
    }
    s_rm_changed_n = keep;

    log_line(app, "还原本次右键改动：成功 %d / 失败 %d", done, fail);
    rm_scan(app);
    /* 两条分支各自带自己的占位符：写成「三目 + 两个实参」时编译器查不了非字面量的格式串，
     * 以后改文案很容易留一个多余的 %d 而没人发现 */
    if (fail == 0) status_msg(app, "已还原本次改动（%d 项）", done);
    else status_msg(app, "还原了 %d 项，%d 项失败（仍记在「曾改动」里）", done, fail);
}

/**
 * @brief 删除确认框的「备份并删除」按钮：关框并带回结果 1
 * @param self 确认框里的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rmc_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}

/**
 * @brief 删除确认框的「取消」按钮：关框并带回结果 0
 * @param self 确认框里的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rmc_cancel_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 0);
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
        /* 右键菜单备份 .reg 也放数据目录（那儿一定存在，reg export 才写得进） */
        snprintf(s_rm_bakdir, sizeof(s_rm_bakdir), "%s", dir);
        cg_free(dir);
    } else {
        snprintf(s_myini, sizeof(s_myini), K_MY_INI);
        s_rm_bakdir[0] = '\0';
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
    CG_BIND("on_rm_tab_click",           on_rm_tab_click),
    CG_BIND("on_rm_filter_click",        on_rm_filter_click),
    CG_BIND("on_rm_search_change",       on_rm_search_change),
    CG_BIND("on_rm_check",               on_rm_check),
    CG_BIND("on_rm_disable_click",       on_rm_disable_click),
    CG_BIND("on_rm_enable_click",        on_rm_enable_click),
    CG_BIND("on_rm_backup_del_click",    on_rm_backup_del_click),
    CG_BIND("on_rm_restore_click",       on_rm_restore_click),
    CG_BIND("on_rmc_ok_click",           on_rmc_ok_click),
    CG_BIND("on_rmc_cancel_click",       on_rmc_cancel_click),
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
