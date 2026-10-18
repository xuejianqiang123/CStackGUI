/**
 * @file    main.c
 * @brief   text 套件入口：只做"建窗口 + 拼绑定表 + 逐模块初始化 + 窗口事件分发"
 * @details examples 合并方案（.codebuddy\plans\examples-consolidation.md）的 P1。
 *          原本 6 个独立工程（notepad / logfilter / filecheck / dataview / htmlreport /
 *          datarec）合并成一个多页签套件，每页一个工具 —— 每个工具的逻辑完整保留在
 *          自己的模块里（modules\<缩写>.c），照抄价值零损失。
 *
 *          **窗口事件为什么要有分发器**（dev 套件只有 encconv 用窗口事件，P1 首次撞上）：
 *          运行时把 .cgui 的 app.events.close 存进窗口伪控件的一个槽（root->ev[close]），
 *          一个槽只能接一个回调名。6 个工具里 5 个都要 close —— "首工具胜出"会把其余
 *          工具的窗口职责静默吞掉：notepad 的未保存拦截（cg_cancel_close）、filecheck 的
 *          写窗口尺寸、logfilter/htmlreport 的收尾记账全没了。
 *          正解：.cgui 的 app.events 一律写 suite_<事件>，本文件的分发器按序调各模块的
 *          <缩写>_app_<事件> 直连转发（模块的窗口 handler 不再按名派发，绑定表里那条
 *          由转换器删掉，不留死绑定）。
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/np.h"   /**< notepad    —— 记事本（菜单/脏标记/保存拦截） */
#include "modules/lf.h"   /**< logfilter  —— 日志过滤（关键字/与或/反向） */
#include "modules/fc.h"   /**< filecheck  —— 文件比对（大小/摘要/逐字节差异） */
#include "modules/dv.h"   /**< dataview   —— 数据视图（CSV/表格/图表） */
#include "modules/hr.h"   /**< htmlreport —— HTML 报告（CSV 转离线网页） */
#include "modules/dr.h"   /**< datarec    —— 数据记录（采集/落盘） */

/** 各模块的绑定表规模上限：六个工具合计 39 条，留足余量给新增模块 */
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

/* ------------------------------------------------ 窗口事件分发器（见文件头说明）
 * 顺序 = 页签顺序。close 里 notepad 排第一：它有"未保存就拦下关闭"的语义
 * （cg_cancel_close），先让它把关；其余模块的 close 是记账与状态复位，多跑一次无害
 * （哪怕这次被拦下了，下次关闭再跑一遍也一样）。 */

/**
 * @brief 窗口关闭：逐个调各模块的收尾（.cgui 的 app.events.close 指到这里）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 */
static void suite_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    np_app_close(self, ev, user);   /**< 未保存拦截（可能 cg_cancel_close）+ 写窗口尺寸 */
    lf_app_close(self, ev, user);
    fc_app_close(self, ev, user);   /**< 写自己的窗口尺寸配置 */
    hr_app_close(self, ev, user);
    dr_app_close(self, ev, user);
}

/**
 * @brief 窗口尺寸变化：记事本与文件比对各自记下尺寸（关窗时写进各自的配置）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 */
static void suite_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    np_app_size(self, ev, user);
    fc_app_size(self, ev, user);
}

/**
 * @brief 文件拖放到窗口：只有 filecheck 认（"直接拖两个文件进来比"是它的入口）
 * @param self 窗口伪控件
 * @param ev drop
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 */
static void suite_drop(cg_widget_t *self, cg_event_t ev, void *user)
{
    fc_app_drop(self, ev, user);
}

/**
 * @brief 界面就绪后：依次调各模块的初始化（各自设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 * @note 各模块的 init 互不相干（各页的控件都已建好，操作隐藏页控件是安全的）。
 */
static void suite_ready(cg_app_t *app, void *user)
{
    np_init(app, user);
    lf_init(app, user);
    fc_init(app, user);
    dv_init(app, user);
    hr_init(app, user);
    dr_init(app, user);
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
    n = append_bindings(np_bindings, n);
    n = append_bindings(lf_bindings, n);
    n = append_bindings(fc_bindings, n);
    n = append_bindings(dv_bindings, n);
    n = append_bindings(hr_bindings, n);
    n = append_bindings(dr_bindings, n);
    /* 窗口事件的分发器：.cgui 的 app.events 写的是这三个名字（suite_close/size/drop），
     * 按名派发只能接一个回调，所以这里注册"一个调全部"的那一个。 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_close", suite_close);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_size", suite_size);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_drop", suite_drop);
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "text.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "text.cgui";
    return cg_app_run(&desc);
}
