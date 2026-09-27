/**
 * @file    test_shot.c
 * @brief   截图 API 的接口契约（真正"存出来的图能不能看"只能真机看图，见下）
 * @details 观感与像素只能靠真机验证 —— 出图证据留在 `build\shotprobe\`（探针在 on_ready 里
 *          各存一张，截图人工看过：整窗图带标题栏与全部控件、控件图就是那个输入框）。
 *          这里只钉**接口契约**：参数不合法（NULL / 没有窗口 / 没有原生窗口）时
 *          **返回 0 而不是崩**，且不留下半个文件。
 *          `--selftest` 这种没有窗口的场合调它们也必须安全 —— 业务代码常在 on_ready 里顺手存图，
 *          而自检也会跑 on_ready。
 */
#include "cg_test.h"
#include "cstackgui.h"
#include "cg_internal.h"

#include <string.h>

static void test_shot_contract(void)
{
    cg_app_t app;                     /* 白盒：栈上一个"没有窗口"的应用（hwnd = NULL） */
    int      rc;

    memset(&app, 0, sizeof(app));

    cg_test_int("窗口句柄为 NULL 的 app：报失败不崩", cg_win_screenshot(&app, "x.png"), 0);
    cg_test_int("传 NULL app：报失败不崩", cg_win_screenshot(NULL, "x.png"), 0);
    cg_test_int("路径传 NULL：报失败不崩", cg_win_screenshot(&app, NULL), 0);
    cg_test_int("路径传空串：报失败不崩", cg_win_screenshot(&app, ""), 0);

    cg_test_int("控件为 NULL：报失败不崩", cg_widget_screenshot(NULL, "x.png"), 0);

    /* 控件对象存在但还没建原生窗口（--selftest 前、或 menu/timer 这类无窗口控件） */
    {
        cg_widget_t w;

        memset(&w, 0, sizeof(w));
        rc = cg_widget_screenshot(&w, "x.png");
        cg_test_int("控件没有原生窗口：报失败不崩", rc, 0);
    }
}

/** @brief 剪贴板图片的接口契约（真机出图见 build\clipprobe\：剪贴板放一张图 → 存出的 PNG 逐像素一致） */
static void test_clip_image_contract(void)
{
    cg_file_delete("build/_should_not_exist.png");

    cg_test_int("路径传 NULL：报失败不崩", cg_clip_get_image(NULL), 0);
    cg_test_int("路径传空串：报失败不崩", cg_clip_get_image(""), 0);
    /* --selftest 里 GDI+ 还没起（它在建窗口时初始化）⇒ 连剪贴板都不去碰就返回 0。
     * 这也顺带证明它**不会**在没准备好时误开剪贴板、更不会留下半个文件。 */
    cg_test_int("GDI+ 没起来时：报失败不崩", cg_clip_get_image("build/_should_not_exist.png"), 0);
    cg_test_int("  -> 且没留下半个文件", cg_file_exists("build/_should_not_exist.png"), 0);
}

CG_TEST_SUITE("截图：参数不合法时的接口契约", test_shot_contract);
CG_TEST_SUITE("剪贴板图片：参数不合法时的接口契约", test_clip_image_contract);
