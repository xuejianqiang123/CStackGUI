/**
 * @file    main.c
 * @brief   示例 toolbox 入口：拼绑定表 + 界面就绪分发（行为按功能拆在 modules\ 下）
 * @details 工具箱启动器（对标 mxx1-toolbox 的功能面与界面形态），形态要素：
 *
 *            ① 8 个页签（常用/常用设置/我的工具/系统工具/清理优化/隐私设置/应用管理/右键增强）；
 *            ② 页内分组条 + 密集按钮墙（.cgui 命名样式 sect / install / remove）；
 *            ③ 成对「装上/撤掉」开关 —— 真实落点是本程序沙箱键
 *               HKCU\Software\CStackGUI\examples\toolbox，真改真还原（tb_opts 模块）；
 *            ④ 底栏功能带：「本页 X 个（共 Y 个）」+ 搜索/日志/设置/关于/检查更新（tb_ui 模块）；
 *            ⑤ 我的工具 = mytools.ini 槽位，**加工具不用重编译**（tb_mytools 模块）；
 *            ⑥ 右键菜单管理：扫 HKCU 用户区 + HKLM 系统区六个位置，批量禁用 / 备份删除 /
 *               还原本次改动（tb_rmenu 模块）。
 *
 *          模块划分（每个模块自带绑定表 <模块>_bindings，main 只负责拼成一张总表）：
 *            modules\tb_log.c     日志列表 / 状态栏回执（各模块共用）
 *            modules\tb_opts.c    成对开关（沙箱注册表键）
 *            modules\tb_tools.c   工具命令表 + 启动 + 最近使用 + 页计数 + 自检对账
 *            modules\tb_search.c  全局搜索（名称/命令双字段过滤）
 *            modules\tb_mytools.c 我的工具（mytools.ini 槽位）
 *            modules\tb_rmenu.c   右键菜单管理（扫描 / 批量操作 / 备份还原）
 *            modules\tb_ui.c      底栏功能带（切页 / 日志窗 / 主题 / 关于）
 *
 *          自检（--selftest）红线，各模块处处遵守：
 *            1. 就绪钩子不启动程序、不读盘不写盘（mytools / 开关状态在无窗时用默认值）；
 *            2. 凡有副作用（启动外部程序 / 写注册表 / 弹对话框）先 cg_app_has_window()；
 *            3. 「.cgui 按钮 id 查不到命令表」会在日志报「不同步」，自检输出可 grep。
 *
 *          界面在 toolbox.cgui 里；本文件只是入口。
 *          编译：在项目根跑 build-app.bat toolbox
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/tb_log.h"     /**< 日志 / 状态栏回执（各模块共用） */
#include "modules/tb_opts.h"    /**< 成对开关（沙箱注册表键） */
#include "modules/tb_tools.h"   /**< 工具命令表 + 启动 + 最近使用 + 页计数 */
#include "modules/tb_search.h"  /**< 全局搜索 */
#include "modules/tb_mytools.h" /**< 我的工具（mytools.ini 槽位） */
#include "modules/tb_rmenu.h"   /**< 右键菜单管理 */
#include "modules/tb_ui.h"      /**< 底栏功能带 */

/** 页数（与 toolbox.cgui 的 items 一一对应） */
#define K_PAGE_MAX 8

/** 各模块的绑定表规模上限：31 条，留足余量给新增模块 */
#define TB_BIND_MAX 64

/** 运行时要一张**连续**的绑定表（表尾 CG_BIND_END），这里在启动时把各模块的拼起来 */
static cg_binding_t g_bindings[TB_BIND_MAX];

/**
 * @brief 把一张以 CG_BIND_END 结尾的模块绑定表追加进总表
 * @param src 模块绑定表（不可为 NULL）
 * @param n 当前总表已用条数
 * @return int 追加后的新条数（不会越过 TB_BIND_MAX；超了就停下，自检会报出未登记回调）
 */
static int append_bindings(const cg_binding_t *src, int n)
{
    int i;

    for (i = 0; src[i].name && n < TB_BIND_MAX - 1; i++) g_bindings[n++] = src[i];
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;
    return n;
}

/**
 * @brief 界面建好：定各模块的数据目录路径、读回状态、刷界面；自检下全部用默认值
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

    /* 数据目录一次取齐：mytools.ini 与右键菜单的 .reg 备份都放那儿 */
    dir = cg_app_data_dir();
    tb_mytools_set_path(dir);
    tb_rmenu_set_bakdir(dir);
    cg_free(dir);

    tb_opts_load(has_win);
    tb_rmenu_load(has_win);
    tb_mytools_reload(app, has_win);
    tb_tools_load(app);
    tb_opts_sync(app);
    tb_rmenu_sync(app);
    tb_search_apply(app);          /* 空关键字：显示"共 N 个工具"的提示行 */
    tb_page_count_sync(app);

    if (!has_win) {
        /* 自检：把命令表全过一遍，反面由 on_tool_click 的「不同步」兜底 */
        tb_tools_audit(app);
        return;
    }

    tb_status_msg(app, "就绪：点按钮、搜索回车、或先在常用设置里装开关");
    tb_log_line(app, "工具箱就绪：%d 页 / %d 个内置工具 / %d 个我的工具",
                K_PAGE_MAX, tb_tool_builtin_count(), tb_mytools_count());
}

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int           n = 0;

    n = append_bindings(tb_tools_bindings, n);
    n = append_bindings(tb_search_bindings, n);
    n = append_bindings(tb_mytools_bindings, n);
    n = append_bindings(tb_opts_bindings, n);
    n = append_bindings(tb_rmenu_bindings, n);
    n = append_bindings(tb_ui_bindings, n);

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "toolbox.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "toolbox.cgui";
    return cg_app_run(&desc);
}
