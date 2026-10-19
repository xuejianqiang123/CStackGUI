/**
 * @file    main.c
 * @brief   studio 工作台入口：建窗口 + 拼 16 个工具的绑定表 + 命令行转发 + 逐模块初始化 + 窗口事件分发
 * @details 把 comm / dev / text 三个套件再合并成**一个**多页签应用（每页一个工具，共 16 页）。
 *          原本 21 个独立工程 → 4 个领域套件 → 现在 comm/dev/text 三套件进一步并成 studio，
 *          每个工具的逻辑完整保留在自己的模块里（modules\<缩写>.c），照抄价值零损失。
 *
 *          **sys 套件（shottool / traytool / syspanel / envsnap / settings）有意不并入**：
 *          shottool 与 traytool 的 close 是"关窗 = 藏进托盘、进程继续"（无条件 cg_cancel_close
 *          + cg_win_show(0)），与 comm/dev/text 的"真关窗 + 清理"语义冲突 —— 硬并会造成
 *          "串口已断、界面还挂托盘"的坏状态，且自检抓不到（无窗口时那两个 close 会 early-return）。
 *          所以 sys 单独保留为一个套件。
 *
 *          **窗口事件的分发语义**（运行时 cg_win_main.c 的 WM_CLOSE）：
 *          close 与 closequery 在同一次 WM_CLOSE 里**先后全部派发**，之后才看 cg_cancel_close
 *          是否拦下。也就是说"某个工具拦下关闭"并不能阻止**其他工具**的 close 期清理照跑 ——
 *          这是运行时的既定行为，合并后必须如实面对。本套件里 notepad 的未保存拦截（cg_cancel_close）
 *          与 comm 工具的收尾（杀定时发送定时器）会同时发生；所幸那些收尾都是**幂等**的
 *          （先判状态再动），真正破坏性的资源（串口 / 网络链路）在 destroy / app 收尾才关，
 *          而 destroy 只在"确实关掉了"之后才触发 —— 拦下关闭时链路不受影响。
 *
 *          加速键冲突由合并器先到先得去重（text 的 np_save_as 与 sys 的 tr_shot 都用
 *          Ctrl+Shift+S；sys 未并入，故本套件内无冲突）。
 */
#include "cstackgui.h"

#include <string.h>

/* comm：通信（串口 / TCP / 深色终端 / Modbus / 帧解码） */
#include "modules/st.h"   /**< serialterm —— 串口终端（帧定界/CRC/多格式） */
#include "modules/nt.h"   /**< netterm   —— TCP 助手 */
#include "modules/dt.h"   /**< darkterm  —— 终端控件全功能演示 */
#include "modules/mb.h"   /**< modbus    —— Modbus RTU/TCP */
#include "modules/fd.h"   /**< framedec  —— 帧解码器 */
/* dev：开发工具（取色 / 画板 / HEX / 编码 / 计算器） */
#include "modules/pl.h"   /**< palette   —— 取色板 */
#include "modules/dp.h"   /**< drawpad   —— 画板 */
#include "modules/ht.h"   /**< hextool   —— HEX 工具 */
#include "modules/ec.h"   /**< encconv   —— 编码转换 */
#include "modules/cl.h"   /**< calculator—— 计算器 */
/* text：文本与数据（记事本 / 日志过滤 / 文件比对 / 数据视图 / 报告 / 采集） */
#include "modules/np.h"   /**< notepad    —— 记事本（菜单/脏标记/保存拦截） */
#include "modules/lf.h"   /**< logfilter  —— 日志过滤（关键字/与或/反向） */
#include "modules/fc.h"   /**< filecheck  —— 文件比对（大小/摘要/逐字节差异） */
#include "modules/dv.h"   /**< dataview   —— 数据视图（CSV/表格/图表） */
#include "modules/hr.h"   /**< htmlreport —— HTML 报告（CSV 转离线网页） */
#include "modules/dr.h"   /**< datarec    —— 数据记录（采集/落盘） */

/** 各模块绑定表合计上限：16 个工具的绑定 + 4 个窗口事件分发器，留足余量 */
#define SUITE_BIND_MAX 512

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

/* ------------------------------------------------ 窗口事件分发器
 * 一个窗口级事件只能接一个回调名，所以多消费者的事件都走"一个调全部"的分发器。
 * 顺序 = 页签顺序（comm → dev → text）；唯一的例外是 close：notepad 的未保存拦截
 * 排在最前，让它先弹"要不要保存"、先定夺这次关不关（见文件头的运行时语义说明）。 */

/**
 * @brief 窗口关闭：逐个调各模块的收尾（.cgui 的 app.events.close 指到这里）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 * @note notepad 排第一：它是唯一会 cg_cancel_close 拦下关闭的模块（未保存拦截），
 *       先让它弹模态框定夺。其余模块的收尾是幂等记账（先判状态再动），照跑无害 ——
 *       运行时在所有 close 处理器跑完后才 honoring 拦截，见文件头说明。
 */
static void suite_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    np_app_close(self, ev, user);   /**< 未保存拦截（可能 cg_cancel_close）+ 写窗口尺寸 */
    st_app_close(self, ev, user);   /**< 杀定时发送定时器（幂等；串口链路在 destroy 才关） */
    nt_app_close(self, ev, user);
    mb_app_close(self, ev, user);   /**< 记账（Modbus 链路在 destroy/app 收尾才关） */
    fd_app_close(self, ev, user);   /**< 杀解码定时器 */
    ec_app_close(self, ev, user);
    lf_app_close(self, ev, user);
    fc_app_close(self, ev, user);   /**< 写自己的窗口尺寸配置 */
    hr_app_close(self, ev, user);
    dr_app_close(self, ev, user);
}

/**
 * @brief 窗口已销毁：comm 工具的进程级收尾（.cgui 的 app.events.destroy 指到这里）
 * @param self 窗口伪控件
 * @param ev destroy
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 * @note 与 close 的分工照原工程：close 是"窗口要关了"（可能被 notepad 拦下），
 *       destroy 是"确定没了" —— 串口 / Modbus 链路的真正关闭在这里，拦下关闭时不会触发。
 */
static void suite_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    st_app_destroy(self, ev, user);
    nt_app_destroy(self, ev, user);
    mb_app_destroy(self, ev, user);
    fd_app_destroy(self, ev, user);
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
 * @brief 界面就绪后：按页签顺序依次调各模块的初始化（各自设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 * @note 各模块的 init 互不相干（各页的控件都已建好，操作隐藏页控件是安全的）。
 */
static void suite_ready(cg_app_t *app, void *user)
{
    /* comm */
    st_init(app, user);
    nt_init(app, user);
    dt_init(app, user);    /**< 原工程没有 on_ready：初值由转换器生成的 init 承载 */
    mb_init(app, user);
    fd_init(app, user);
    /* dev */
    pl_init(app, user);
    dp_init(app, user);
    ht_init(app, user);
    ec_init(app, user);
    cl_init(app, user);
    /* text */
    np_init(app, user);
    lf_init(app, user);
    fc_init(app, user);
    dv_init(app, user);
    hr_init(app, user);
    dr_init(app, user);
}

/**
 * @brief 入口：登记 .cgui、转发命令行、拼绑定表后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑不开窗自检；
 *             声明了 CLI 的模块还会收到原样转发的 argv（各认各的旗标，如 --demo）
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int            n = 0;

    memset(&desc, 0, sizeof(desc));
    /* 命令行旗标：先转发给声明了 CLI 的模块（serialterm / modbus 的 --demo），
     * 让它们在 init 之前就把 s_demo 等状态备好（on_ready 会看它们）。 */
    st_parse_cli(argc, argv);
    mb_parse_cli(argc, argv);

    /* 绑定表按页签顺序拼：comm → dev → text */
    n = append_bindings(st_bindings, n);
    n = append_bindings(nt_bindings, n);
    n = append_bindings(dt_bindings, n);
    n = append_bindings(mb_bindings, n);
    n = append_bindings(fd_bindings, n);
    n = append_bindings(pl_bindings, n);
    n = append_bindings(dp_bindings, n);
    n = append_bindings(ht_bindings, n);
    n = append_bindings(ec_bindings, n);
    n = append_bindings(cl_bindings, n);
    n = append_bindings(np_bindings, n);
    n = append_bindings(lf_bindings, n);
    n = append_bindings(fc_bindings, n);
    n = append_bindings(dv_bindings, n);
    n = append_bindings(hr_bindings, n);
    n = append_bindings(dr_bindings, n);

    /* 窗口事件路由（.cgui 的 app.events 一律写 suite_<事件>）：
     * close/destroy/size 有多消费者，走分发器；drop 只有 filecheck 一个消费者，
     * 直接把 suite_drop 绑到它的直连转发函数上，少一层壳（与 sys/text 套件同口径）。 */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_close",   suite_close);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_destroy", suite_destroy);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_size",    suite_size);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_drop",    fc_app_drop);
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "studio.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "studio.cgui";
    return cg_app_run(&desc);
}
