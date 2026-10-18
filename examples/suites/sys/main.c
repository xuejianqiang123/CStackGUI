/**
 * @file    main.c
 * @brief   sys 套件入口：建窗口 + 拼绑定表 + 逐模块初始化 + 窗口事件路由
 * @details examples 合并方案（.codebuddy\plans\examples-consolidation.md）的 P3。
 *          原本 5 个独立工程（shottool / traytool / syspanel / envsnap / settings）
 *          合并成一个多页签套件 —— 逻辑完整保在 modules\（照抄价值零损失）。
 *
 *          **本套件特有的取舍：托盘是进程级单例，点击语义二选一**（shottool 与
 *          traytool 都建托盘，而一个进程只有一张图标、app.events.tray 只有一个槽）：
 *            托盘点击 → 只路由给 traytool（显隐切换 = 窗口化套件的通用默认语义）；
 *            shottool 的"托盘左键 = 截图"是它文档里明写的**兜底入口**，套件里它仍保留
 *            两个入口：面板按钮 + 全局热键 Ctrl+Alt+A（托盘里也按得到）—— 无能力损失。
 *          托盘图标与提示语由两个模块的 init 依次设置，按页签顺序后者（traytool）生效，
 *          与"点击语义归 traytool"一致。
 *
 *          加速键冲突（双方都有 Ctrl+Shift+Q 退出）由合并器先到先得去重：保留页签靠前
 *          的 shottool 那条，traytool 的退出仍可从菜单点（两边的退出职责相同：
 *          摘托盘图标 + cg_quit）。
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/sh.h"   /**< shottool —— 截图（框选/标注/贴图/GIF 录屏） */
#include "modules/tr.h"   /**< traytool —— 托盘（显隐/菜单/剪贴板/主题） */
#include "modules/sp.h"   /**< syspanel —— 系统面板 */
#include "modules/en.h"   /**< envsnap  —— 环境快照 */
#include "modules/se.h"   /**< settings —— 程序配置 */

/** 各模块的绑定表规模上限：五个工具合计 40 条，留足余量给新增模块 */
#define SUITE_BIND_MAX 256

/** 运行时要一张**连续**的绑定表（表尾 CG_BIND_END），这里在启动时把各模块的拼起来 */
static cg_binding_t g_bindings[SUITE_BIND_MAX];

/**
 * @brief 把一张以 CG_BIND_END 结尾的模块绑定表追加进总表
 * @param src 模块绑定表（不可为 NULL）
 * @param n 当前总表已用条数
 * @return int 追加后的新条数（不会越过 SUITE_BIND_MAX；超了就停下，自检会报出未登记回调）
 */
static int append_bindings(const cg_binding_t *src, int n)
{
    int i;

    for (i = 0; src[i].name && n < SUITE_BIND_MAX - 1; i++) g_bindings[n++] = src[i];
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;
    return n;
}

/**
 * @brief 窗口关闭：两个托盘工具的收尾（.cgui 的 app.events.close 指到这里）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 * @note 两边语义一致地"拦下关闭并藏进托盘"（cg_cancel_close + cg_win_show(0)），
 *       连续跑只是第二次气泡覆盖第一次，行为正确：**关窗 = 进托盘，进程继续**
 *       （这是两个工具共同的设计，退出走托盘菜单或 Ctrl+Shift+Q）。
 */
static void suite_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    sh_app_close(self, ev, user);
    tr_app_close(self, ev, user);
}

/**
 * @brief 界面就绪后：按页签顺序依次调各模块的初始化（各自设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 * @note shottool 先建托盘 + 注册全局热键，traytool 后建托盘（提示语覆盖为后者，
 *       与"托盘点击语义归 traytool"一致）；两者都有"没窗口就跳过"的守卫，
 *       --selftest 走不到托盘与热键初始化。
 */
static void suite_ready(cg_app_t *app, void *user)
{
    sh_init(app, user);
    tr_init(app, user);
    sp_init(app, user);
    en_init(app, user);
    se_init(app, user);
}

/**
 * @brief 入口：登记 .cgui、拼绑定表后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int            n = 0;

    memset(&desc, 0, sizeof(desc));
    n = append_bindings(sh_bindings, n);
    n = append_bindings(tr_bindings, n);
    n = append_bindings(sp_bindings, n);
    n = append_bindings(en_bindings, n);
    n = append_bindings(se_bindings, n);

    /* 窗口事件路由（.cgui 的 app.events 一律写 suite_<事件>）：
     * 只有两个工具都用的 close 需要真正的 wrapper；其余都是单消费者 ——
     * 直接把 suite_<事件> 绑到那个模块的直连转发函数上，少一层壳。 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_close",   suite_close);
    /* 托盘点击**只给 traytool**（取舍见文件头：shottool 保留按钮 + 全局热键两个入口） */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_tray",    tr_app_tray);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_keydown", sh_app_keydown);  /**< Esc/贴图返回等全局按键 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_size",    sh_app_size);     /**< 面板位置只在空闲态记录 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_move",    sh_app_move);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_show",    tr_app_show);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_hide",    tr_app_hide);
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "sys.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "sys.cgui";
    return cg_app_run(&desc);
}
