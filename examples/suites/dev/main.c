/**
 * @file    main.c
 * @brief   dev 套件入口：只做"建窗口 + 拼绑定表 + 逐模块初始化"，功能全在 modules\ 里
 * @details 这是 examples 合并方案（.codebuddy\plans\examples-consolidation.md）的 P0 试点。
 *          原本 5 个独立工程（palette / drawpad / hextool / encconv / calculator）合并成
 *          一个多页签套件，每页一个工具 —— 而**每个工具的逻辑完整保留在自己的模块里**
 *          （modules\<缩写>.c），照抄价值零损失：想抄 HEX 工具就打开 ht.c。
 *
 *          分层契约：
 *            main.c   仅入口：登记 .cgui、拼各模块绑定表、依次调各模块 init
 *            modules\  各工具的完整逻辑（回调仍是 static，编译单元隔离，函数名未改）
 *            dev.cgui   由 tools\suite-merge.mjs 生成（一个 tabs，每页一个原工程的界面）
 *          main.c 不含任何工具语义 —— 加一个工具就是"加一个模块 + 改一次 .cgui 清单"。
 */
#include "cstackgui.h"

#include <string.h>

#include "modules/pl.h"   /**< palette   —— M3 调色板 */
#include "modules/dp.h"   /**< drawpad   —— 画板（截图当底、标注当命令） */
#include "modules/ht.h"   /**< hextool   —— HEX 与文本互转、CRC 与摘要 */
#include "modules/ec.h"   /**< encconv   —— 编码转换（UTF-8 / GBK / Base64 / URL） */
#include "modules/cl.h"   /**< calculator —— 计算器（表达式求值） */

/** 各模块的绑定表规模上限：五个工具合计 41 条，留足余量给新增模块 */
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
 * @brief 界面就绪后：依次调各模块的初始化（各自设自己的控件）
 * @param app 当前应用
 * @param user 用户数据（本套件未用）
 * @return void 无返回值
 * @note 五个模块的 init 互不相干（各页的控件都已建好，操作隐藏页控件是安全的），
 *       所以不需要"页签切换才初始化"——那会引入模块间的状态机耦合。
 *       页签切换由 .cgui 的 tabs 自理，本文件不参与。
 */
static void suite_ready(cg_app_t *app, void *user)
{
    pl_init(app, user);
    dp_init(app, user);
    ht_init(app, user);
    ec_init(app, user);
    cl_init(app, user);
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
    n = append_bindings(pl_bindings, n);
    n = append_bindings(dp_bindings, n);
    n = append_bindings(ht_bindings, n);
    n = append_bindings(ec_bindings, n);
    n = append_bindings(cl_bindings, n);

    desc.bindings         = g_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = suite_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "dev.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "dev.cgui";
    return cg_app_run(&desc);
}
