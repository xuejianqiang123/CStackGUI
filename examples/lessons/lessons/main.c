/**
 * @file    main.c
 * @brief   教学六课套件入口：建窗口 + 拼绑定表 + 逐课初始化 + 窗口事件路由
 * @details examples 合并方案的延伸（2026-10-18 用户拍板：六课并成一个套件，12 → 7 个工程）。
 *          原本 6 个独立教学工程（lesson01_basics … lesson06_window）合并成一个多页签套件，
 *          每课一页签、逻辑完整保在 modules\（照抄价值零损失）。
 *
 *          **六课的 ready 是两套机制，合并后都保留**（这是与工具套件最大的不同）：
 *            - lesson02/04 用 `desc.on_ready`（→ l2_init / l4_init，在 suite_ready 里调）；
 *            - lesson01/03/05/06 用 `app.events.ready` 窗口事件（→ l1/l3/l5/l6_app_ready，
 *              由 suite_win_ready 分发器调）。两套签名不同、互不冲突，合并后各课初始化
 *              时机与原工程一致。
 *
 *          **lesson06 的窗口级配置**（tray / center / escClose / info）已透传到本套件窗口 ——
 *          它讲的就是窗口本身，托盘演示、cg_app_info 读回（product/build/copyright）都要真跑。
 *          窗口事件 8 个：ready / close / size / keydown / char / mousedown / idle / tray。
 *          close 与 ready 各需一个真分发器（多消费者）；其余单消费者直接绑模块转发函数。
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/l1.h"   /**< 入门 01 · 基础与容器 */
#include "modules/l2.h"   /**< 入门 02 · 输入与选择 */
#include "modules/l3.h"   /**< 入门 03 · 数值与指示 */
#include "modules/l4.h"   /**< 入门 04 · 数据与日志 */
#include "modules/l5.h"   /**< 入门 05 · 命令与图形 */
#include "modules/l6.h"   /**< 入门 06 · 主窗口与窗口事件 */

/** 各课绑定表规模上限：六课合计 86 条，留足余量 */
#define SUITE_BIND_MAX 256

/** 运行时要一张**连续**的绑定表（表尾 CG_BIND_END），这里在启动时把各课的拼起来 */
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
 * @brief desc.on_ready 分发器：依次调六课的 <pre>_init（各自设自己的控件初值）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 * @note lesson02/04 的初始化在这里（它们用 desc.on_ready）；lesson01/03/05/06 的 init
 *       是空壳（初始化在各自 app.events.ready 里，由 suite_win_ready 调）。顺序 = 页签顺序。
 */
static void suite_ready(cg_app_t *app, void *user)
{
    l1_init(app, user);
    l2_init(app, user);
    l3_init(app, user);
    l4_init(app, user);
    l5_init(app, user);
    l6_init(app, user);
}

/**
 * @brief 窗口 ready 事件分发器：调用了 app.events.ready 的四课（.cgui 的 app.events.ready 指到这里）
 * @param self 窗口伪控件
 * @param ev ready
 * @param user 业务数据（透传给各课 handler）
 * @return void 无返回值
 * @note lesson01/03/05/06 在 .cgui 里声明了 app.events.ready，各自往状态栏 / 日志写"就绪"。
 */
static void suite_win_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    l1_app_ready(self, ev, user);
    l3_app_ready(self, ev, user);
    l5_app_ready(self, ev, user);
    l6_app_ready(self, ev, user);
}

/**
 * @brief 窗口 close 事件分发器：lesson03 只记一笔、lesson06 可拦下关闭（.cgui 的 app.events.close 指到这里）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给各课 handler）
 * @return void 无返回值
 * @note 两课都要收到 close：lesson06 的"演示拦下这次关闭"不能因合并被吞掉。
 */
static void suite_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    l3_app_close(self, ev, user);
    l6_app_close(self, ev, user);
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
    n = append_bindings(l1_bindings, n);
    n = append_bindings(l2_bindings, n);
    n = append_bindings(l3_bindings, n);
    n = append_bindings(l4_bindings, n);
    n = append_bindings(l5_bindings, n);
    n = append_bindings(l6_bindings, n);

    /* 窗口事件路由（.cgui 的 app.events 一律写 suite_<事件>）：
     * ready 与 close 各有两个以上消费者，需要真分发器；
     * 其余（size/keydown/char/mousedown/idle/tray）只有 lesson06 用 ——
     * 直接把 suite_<事件> 绑到 l6 的直连转发函数上，少一层壳。 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_win_ready", suite_win_ready);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_close",     suite_close);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_size",      l6_app_size);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_keydown",   l6_app_keydown);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_char",      l6_app_char);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_mousedown", l6_app_mousedown);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_idle",      l6_app_idle);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_tray",      l6_app_tray);
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lessons.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "lessons.cgui";
    return cg_app_run(&desc);
}
