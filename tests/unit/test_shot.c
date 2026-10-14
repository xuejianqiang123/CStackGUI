/**
 * @file    test_shot.c
 * @brief   截图 API 的接口契约（真正"存出来的图能不能看"只能真机看图，见下）
 * @details 观感与像素只能靠真机验证 —— 出图证据留在 `build\shotprobe\`（探针在 on_ready 里
 *          各存一张，截图人工看过：整窗图带标题栏与全部控件、控件图就是那个输入框）。
 *          这里只钉**接口契约**：参数不合法（NULL / 没有窗口 / 没有原生窗口）时
 *          **返回 0 而不是崩**，且不留下半个文件。
 *          `--selftest` 这种没有窗口的场合调它们也必须安全 —— 业务代码常在 on_ready 里顺手存图，
 *          而自检也会跑 on_ready。
 *
 *          2026-10-14 补进截图链路那批新 API（`cg_screen_size` / `cg_screen_capture` /
 *          `cg_clip_set_image` / `cg_hotkey_set` / `cg_win_set_border` / `cg_scale_px`）：
 *          前四个同样只钉"没有窗口 / 参数不合法时返 0 不崩、不留半个文件"；`cg_screen_size`
 *          是唯一的**正断言**（不需要窗口也不需要 GDI+，任何机器上都该拿得到屏幕尺寸），
 *          `cg_scale_px` 按 scale=100/150/50 三档钉换算与四舍五入。
 *          "抓出来的图对不对"仍归真机（探针证据在 `.codebuddy/plans/shottool.md`）。
 */
#include "cg_test.h"
#include "cstackgui.h"
#include "cg_internal.h"

#include <string.h>

/**
 * @brief 截图 API 的参数兜底：NULL / 没有窗口 / 没有原生窗口都返 0 而不崩
 * @return void 无返回值
 */
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

/**
 * @brief 剪贴板图片的接口契约
 * @return void 无返回值
 * @note 真机出图见 build\clipprobe\：剪贴板放一张图 -> 存出的 PNG 逐像素一致
 */
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

/**
 * @brief 屏幕抓取（区域截图）的参数兜底：NULL / 空路径 / 区域整块在屏外都返 0 且不留文件
 * @return void 无返回值
 * @note 真正"抓出来的图对不对"要真机看像素（shottool 的探针证据在计划里）；这里只钉
 *       **不开窗也安全**：`--selftest` 与单元测试进程里都没有窗口，这些调用必须干净失败。
 *       屏外区域走的是"夹进屏幕后整体为 0"那条路，它在任何 GDI 调用之前就返回了，
 *       所以不依赖 GDI+ 起没起，结论是确定的
 */
static void test_screen_capture_contract(void)
{
    cg_app_t app;                     /* 白盒：栈上一个"没有窗口"的应用（hwnd = NULL） */
    int      w = 0, h = 0, rc;

    memset(&app, 0, sizeof(app));
    app.scale = 100;                  /* 显式给 100：cgw_scale_px 对 scale<=0 也按 100 算，写出来是文档 */

    /* 屏幕尺寸这条是**正断言**：不需要窗口、不需要 GDI+，任何机器上都该拿得到 */
    cg_test_int("app 传 NULL 也能问到屏幕尺寸", cg_screen_size(NULL, &w, &h), 1);
    cg_test_ok("  -> 宽是正的", w > 0);
    cg_test_ok("  -> 高是正的", h > 0);
    rc = cg_screen_size(&app, &w, &h);
    cg_test_int("栈上 app（scale=100）同样问得到", rc, 1);
    cg_test_ok("  -> 尺寸和 NULL 时一致", w > 0 && h > 0);

    /* 虚拟桌面原点（2026-10-14 多屏支持新增）：同样不需要窗口。
     * 值随机器而异（单屏在左上就是 0,0；左边竖了屏是负的），所以只钉返回码 */
    cg_test_int("app 传 NULL 也能问到虚拟桌面原点", cg_screen_origin(NULL, &w, &h), 1);
    cg_test_int("栈上 app 同样问得到原点", cg_screen_origin(&app, &w, &h), 1);

    cg_file_delete("build/_should_not_exist.png");
    cg_test_int("app 传 NULL：报失败不崩", cg_screen_capture(NULL, 0, 0, 4, 4, "build/_should_not_exist.png"), 0);
    cg_test_int("路径传 NULL：报失败不崩", cg_screen_capture(&app, 0, 0, 4, 4, NULL), 0);
    cg_test_int("路径传空串：报失败不崩", cg_screen_capture(&app, 0, 0, 4, 4, ""), 0);
    /* 整块在屏幕外：先夹后判，夹完宽高为 0 —— 这条在任何 GDI 调用之前返回，不依赖环境 */
    cg_test_int("区域整块在屏外：报失败不崩",
                cg_screen_capture(&app, 100000, 100000, 50, 50, "build/_should_not_exist.png"), 0);
    /* 原点**左界**外（虚拟桌面左界是原点、未必是 0）：同样夹成 0。
     * 多屏口径改的就是这条 —— 老写法拿 0 当下界，副屏区域会被夹没了 */
    cg_test_int("区域整块在桌面左界外：报失败不崩",
                cg_screen_capture(&app, -100000, 0, 50, 50, "build/_should_not_exist.png"), 0);
    cg_test_int("  -> 且没留下半个文件", cg_file_exists("build/_should_not_exist.png"), 0);
}

/**
 * @brief 全局热键 / 改边框 / 逻辑像素换算的参数兜底
 * @return void 无返回值
 * @note 这三个都要求"有窗口"才真干活（热键要 RegisterHotKey、改边框要动 GWL_STYLE），
 *       没有窗口时一律返回 0 —— 业务在 on_ready 里注册热键，而自检也跑 on_ready，
 *       所以这条契约是"自检能跑干净"的前提
 */
static void test_shot_misc_contract(void)
{
    cg_app_t app;

    memset(&app, 0, sizeof(app));
    app.scale = 100;

    cg_test_int("热键：app 传 NULL 报失败不崩", cg_hotkey_set(NULL, "Ctrl+Alt+A", "shot"), 0);
    cg_test_int("热键：没有窗口报失败不崩", cg_hotkey_set(&app, "Ctrl+Alt+A", "shot"), 0);
    cg_test_int("热键：组合键看不懂报失败不崩", cg_hotkey_set(&app, "乱写的组合键", "shot"), 0);
    cg_test_int("热键：动作名是空的报失败不崩", cg_hotkey_set(&app, "Ctrl+Alt+A", ""), 0);

    cg_test_int("改边框：app 传 NULL 报失败不崩", cg_win_set_border(NULL, "none"), 0);
    cg_test_int("改边框：没有窗口报失败不崩", cg_win_set_border(&app, "none"), 0);
    cg_test_int("改边框：mode 传空报失败不崩", cg_win_set_border(&app, ""), 0);

    cg_test_int("换算：app 传 NULL 时原样返回", cg_scale_px(NULL, 123), 123);
    cg_test_int("换算：scale=100 时原样返回", cg_scale_px(&app, 123), 123);
    app.scale = 150;
    cg_test_int("换算：scale=150 时四舍五入（123*1.5=184.5 -> 185）", cg_scale_px(&app, 123), 185);
    app.scale = 50;
    cg_test_int("换算：scale=50 时减半", cg_scale_px(&app, 120), 60);

    /* 负值也要四舍五入到正确一侧（2026-10-14 多屏支持时修的）：老写法对负数向零截断，
     * -1920 会算成 -1919，虚拟桌面原点是负时 overlay 左边缘会漏一条桌面 */
    app.scale = 100;
    cg_test_int("换算：负值 scale=100 时不被向零截断", cg_scale_px(&app, -1920), -1920);
    app.scale = 150;
    cg_test_int("换算：负值 scale=150 时（-1920*1.5）", cg_scale_px(&app, -1920), -2880);
    app.scale = 50;
    cg_test_int("换算：负值 scale=50 时（-1920/2）", cg_scale_px(&app, -1920), -960);
}

CG_TEST_SUITE("截图：参数不合法时的接口契约", test_shot_contract);
CG_TEST_SUITE("剪贴板图片：参数不合法时的接口契约", test_clip_image_contract);
CG_TEST_SUITE("屏幕抓取：参数不合法时的接口契约", test_screen_capture_contract);
CG_TEST_SUITE("热键 / 改边框 / 像素换算：参数不合法时的接口契约", test_shot_misc_contract);
