/**
 * @file    main.c
 * @brief   comm 套件入口：建窗口 + 拼绑定表 + 命令行转发 + 逐模块初始化 + 窗口事件分发
 * @details examples 合并方案（.codebuddy\plans\examples-consolidation.md）的 P2。
 *          原本 5 个独立工程（serialterm / netterm / darkterm / modbus / framedec）
 *          合并成一个多页签套件，每页一个工具 —— 每个工具的逻辑完整保留在自己的
 *          模块里（modules\<缩写>.c），照抄价值零损失。
 *
 *          **命令行旗标的转发**：serialterm 与 modbus 都有 `--demo`（无硬件自测），
 *          各模块的 <缩写>_parse_cli 只比较**自己的**旗标，不认识的自然忽略 ——
 *          所以 main 把 argc/argv 原样转发给每个声明了 CLI 的模块即可，
 *          `comm.exe --demo` 两个模块同时进演示模式，语义正确。
 *          （argv[1] 开文件那类"分不清该给谁"的语句没有搬，转换器会报丢弃。）
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/st.h"   /**< serialterm —— 串口终端（帧定界/CRC/多格式） */
#include "modules/nt.h"   /**< netterm   —— TCP 助手 */
#include "modules/dt.h"   /**< darkterm  —— 终端控件全功能演示 */
#include "modules/mb.h"   /**< modbus    —— Modbus RTU/TCP */
#include "modules/fd.h"   /**< framedec  —— 帧解码器 */

/** 各模块的绑定表规模上限：五个工具合计 57 条，留足余量给新增模块 */
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

/* ------------------------------------------------ 窗口事件分发器（见 examples-consolidation.md）
 * 顺序 = 页签顺序。通信类工具的窗口事件里有**真清理**：杀定时器、撤轮询、关端口 ——
 * 漏掉任何一个都是"关了窗口还占着口子"。 */

/**
 * @brief 窗口关闭：逐个调各模块的收尾（.cgui 的 app.events.close 指到这里）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 */
static void suite_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    st_app_close(self, ev, user);   /**< 杀调度定时器 */
    nt_app_close(self, ev, user);
    mb_app_close(self, ev, user);   /**< 关 Modbus 连接 */
    fd_app_close(self, ev, user);   /**< 杀解码定时器 */
}

/**
 * @brief 窗口已销毁：进程级收尾（.cgui 的 app.events.destroy 指到这里）
 * @param self 窗口伪控件
 * @param ev destroy
 * @param user 业务数据（本套件未用）
 * @return void 无返回值
 * @note 与 close 的分工照原工程：close 是"窗口要关了"（可能被拦），destroy 是"确定没了"。
 */
static void suite_destroy(cg_widget_t *self, cg_event_t ev, void *user)
{
    st_app_destroy(self, ev, user);
    nt_app_destroy(self, ev, user);
    mb_app_destroy(self, ev, user);
    fd_app_destroy(self, ev, user);
}

/**
 * @brief 界面就绪后：依次调各模块的初始化（各自设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 */
static void suite_ready(cg_app_t *app, void *user)
{
    st_init(app, user);
    nt_init(app, user);
    dt_init(app, user);    /**< 原工程没有 on_ready：初值由转换器生成的 init 承载 */
    mb_init(app, user);
    fd_init(app, user);
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

    n = append_bindings(st_bindings, n);
    n = append_bindings(nt_bindings, n);
    n = append_bindings(dt_bindings, n);
    n = append_bindings(mb_bindings, n);
    n = append_bindings(fd_bindings, n);
    /* 窗口事件的分发器：.cgui 的 app.events 写的是这两个名字（按名派发只能接一个回调） */
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_close", suite_close);
    g_bindings[n++] = (cg_binding_t)CG_BIND("suite_destroy", suite_destroy);
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "comm.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "comm.cgui";
    return cg_app_run(&desc);
}
