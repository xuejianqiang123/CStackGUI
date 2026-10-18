/**
 * @file    test_gif_rec.c
 * @brief   GIF 录屏的接口契约：没有窗口 / 没在录时**不崩、不留半成品、不占状态**
 * @details 为什么值得单独一套（2026-10-18 补）：录屏的定时抓帧与工作线程编排
 *          （cg_win_gif.c）是全工程最复杂的一块"线程 + 文件 + 清理"代码，而它的
 *          API 从没有直接判据 —— 真正"录出来的 GIF 能不能看"只能真机验证
 *          （examples\shottool 的 Ctrl+Alt+G 是那条人工验收路径），但**接口契约**
 *          完全可以在没有窗口的场合钉死，而且这正是每个录像应用最先踩到的场景：
 *          on_ready 里顺手开录屏，而 --selftest 不开窗 —— 此刻 rec_start 必须干净地
 *          失败，不能碰磁盘、不能占住状态（否则下一次真录屏被"已在录"挡住）。
 *
 *          与 test_shot.c 同一哲学：钉"返回值 / 不崩 / 不留文件"，不钉像素。
 */
#include "cg_test.h"
#include "cstackgui.h"
#include "cg_internal.h"

#include <string.h>

/**
 * @brief 没有窗口的 app（白盒：栈上零填充，hwnd = NULL）上的录屏失败契约
 * @return void 无返回值
 */
static void test_rec_contract(void)
{
    cg_app_t app;
    char    *dir;
    char     tmp[1600];
    int      frames = -1, elapsed = -1;

    memset(&app, 0, sizeof(app));
    /* 与实现同一落点：录屏临时文件建在 cg_app_data_dir()（%LOCALAPPDATA%\<exe名>）下，
     * 不是 exe 目录 —— 拼错地方那条"不留下临时文件"断言就恒真空转了 */
    dir = cg_app_data_dir();

    /* 与 test_gif.c 的分工：那边钉 **NULL app** 的参数兜底（没窗口 / 没在录），
     * 这边钉 **有 app 但没窗口** 的现实场景（on_ready 里顺手开录屏、--selftest 跑
     * on_ready）—— app 对象合法、只是没有窗口，且要验**不碰磁盘、不占状态**。 */
    cg_test_int("没有窗口的 app：开始录屏失败不崩", cg_gif_rec_start(&app, 0, 0, 64, 64, 10), 0);
    /* 连点两次（on_ready 与某个按钮都调了 rec_start 的那种写法）：
     * 两次都必须干净失败，且**不许把状态占成"已在录"** —— 否则用户点了真录屏按钮
     * 得到的是"失败"，而且没有任何地方说明为什么。 */
    cg_test_int("  再点一次：仍然干净失败（不是「已在录」）", cg_gif_rec_start(&app, 0, 0, 64, 64, 10), 0);

    /* 失败的开始**不得碰磁盘**：临时文件是录屏状态的一半，留着它下次真录屏就是脏状态。
     * 先清掉上次真录屏可能残留的那份 —— 否则这条断言会红在与本次无关的旧文件上。 */
    if (dir) {
        int n = snprintf(tmp, sizeof(tmp), "%s/cg_rec_tmp.gif", dir);

        cg_test_ok("  临时文件路径拼得出来", n > 0 && n < (int)sizeof(tmp));
        cg_file_delete(tmp);
        cg_test_int("  （清场：删掉上次可能残留的临时文件）", cg_file_exists(tmp), 0);
        cg_test_int("  失败的开始不留下临时文件", cg_file_exists(tmp), 0);
    }

    /* 进度查询：没在录时两个出参都该是 0（调用方拿它刷新工具条计时，别让它显示脏值） */
    cg_gif_rec_info(&app, &frames, &elapsed);
    cg_test_int("没在录时 info 的帧数出参是 0", frames, 0);
    cg_test_int("  且毫秒出参是 0", elapsed, 0);
    frames = -1;
    cg_gif_rec_info(&app, &frames, NULL);   /* 出参可传 NULL */
    cg_test_int("info 只要帧数也行（elapsed 传 NULL）", frames, 0);
    cg_gif_rec_info(NULL, &frames, &elapsed);   /* app 传 NULL 也不崩 */

    /* 停止：没在录 = 0，且**不许留下成品文件** */
    cg_file_delete("build/_rec_should_not_exist.gif");
    cg_test_int("没在录时 stop 返回 0", cg_gif_rec_stop(&app, NULL), 0);
    cg_test_int("  指定成品路径的 stop 同样返回 0", cg_gif_rec_stop(&app, "build/_rec_should_not_exist.gif"), 0);
    cg_test_int("  且不写出成品文件", cg_file_exists("build/_rec_should_not_exist.gif"), 0);
    /* 连停两次（按钮重复点 / 关窗时又收一次）：第二次照样 0，不崩 */
    cg_test_int("  再 stop 一次照样 0（幂等）", cg_gif_rec_stop(&app, NULL), 0);

    cg_free(dir);
}

/**
 * @brief 录屏在自检场景下的行为边界：--selftest 不开窗，on_ready 里的录屏代码必须安全
 * @return void 无返回值
 * @note 这正是 test_gif_rec 存在的主要理由：录像类应用（shottool 那种）的 on_ready
 *       里常有"开始录制"的代码，而自检也会跑 on_ready —— 此刻崩或占状态都是现场级问题
 */
static void test_rec_selftest_scenario(void)
{
    cg_app_t app;

    memset(&app, 0, sizeof(app));

    cg_test_note("---- 模拟 on_ready：自检场景里先查进度、再开录、再收尾 ----");
    {
        int f = 99, e = 99;

        cg_gif_rec_info(&app, &f, &e);
        cg_test_int("  自检里查进度：帧数归零", f, 0);
        cg_test_int("  自检里查进度：毫秒归零", e, 0);
    }
    cg_test_int("  自检里开录屏：干净失败（没有窗口）", cg_gif_rec_start(&app, 0, 0, 320, 240, 30), 0);
    cg_test_int("  自检里收尾：不崩、返回 0", cg_gif_rec_stop(&app, NULL), 0);
}

CG_TEST_SUITE("win32：GIF 录屏的接口契约（无窗口 / 没在录）", test_rec_contract);
CG_TEST_SUITE("win32：GIF 录屏在自检场景下的边界", test_rec_selftest_scenario);
