/**
 * @file    test_drop.c
 * @brief   文件拖放的路径清单 API（core 侧）
 * @details 拖放**真机那一半**是 win32 读 HDROP（cg_win_drop.c）—— 单测碰不到它
 *          （测试不 include cg_win_internal.h，按分层规矩只认公共头）。
 *          能测也必须测的是**清单本身**：填 / 取 / 越界 / 再填 / 清空 / 副本语义，
 *          以及那条生命周期约定 —— 后端在回调返回后必须 `cg_app_drop_put(app, NULL, 0)` 收口，
 *          漏了就是**每次拖放漏一块内存**，而这套件在 `--memcheck` 下会直接抓住。
 */
#include "cg_test.h"
#include "cstackgui.h"
#include "cg_internal.h"

#include <string.h>

/**
 * @brief 拖放路径清单：填 / 取 / 越界 / 再填 / 清空与副本语义
 * @return void 无返回值
 */
static void test_drop_list(void)
{
    cg_app_t    app;
    const char *files[3] = { "C:\\a.bin", "D:\\目录\\b.log", "\\\\server\\share\\c.txt" };
    char       *got;

    memset(&app, 0, sizeof(app));     /* 白盒：栈上一个应用（不需要窗口） */

    cg_test_int("没填之前是 0 个", cg_drop_count(&app), 0);
    cg_test_int("  -> 取第 0 个拿不到东西", cg_drop_file(&app, 0) == NULL, 1);

    cg_test_note("---- 填三个（含中文目录与 UNC 路径） ----");
    cg_app_drop_put(&app, files, 3);
    cg_test_int("填 3 个", cg_drop_count(&app), 3);
    got = cg_drop_file(&app, 0);
    cg_test_str("第 0 个", got, "C:\\a.bin");
    cg_free(got);
    got = cg_drop_file(&app, 1);
    cg_test_str("第 1 个（中文目录原样保留）", got, "D:\\目录\\b.log");
    cg_free(got);
    got = cg_drop_file(&app, 2);
    cg_test_str("第 2 个（UNC）", got, "\\\\server\\share\\c.txt");
    cg_free(got);

    cg_test_note("---- 越界 / 负数 / NULL app ----");
    cg_test_int("越界返回 NULL", cg_drop_file(&app, 3) == NULL, 1);
    cg_test_int("负数返回 NULL", cg_drop_file(&app, -1) == NULL, 1);
    cg_test_int("app 传 NULL：count 给 0", cg_drop_count(NULL), 0);
    cg_test_int("app 传 NULL：file 给 NULL", cg_drop_file(NULL, 0) == NULL, 1);

    cg_test_note("---- 再填一次是**换掉**而不是叠加 ----");
    cg_app_drop_put(&app, files, 1);
    cg_test_int("变成 1 个", cg_drop_count(&app), 1);
    cg_test_int("  原来那个第 1 个也跟着没了（不是叠加）", cg_drop_file(&app, 1) == NULL, 1);

    cg_test_note("---- 取到的是副本：改它不影响清单 ----");
    got = cg_drop_file(&app, 0);
    if (got) {
        got[0] = 'X';
        cg_free(got);
    }
    got = cg_drop_file(&app, 0);
    cg_test_str("再取一次仍是原值", got, "C:\\a.bin");
    cg_free(got);

    cg_test_note("---- 清空（回调返回后必须走这一步，否则每次拖放漏一块） ----");
    cg_app_drop_put(&app, NULL, 0);
    cg_test_int("清空后是 0 个", cg_drop_count(&app), 0);
    cg_test_int("  -> 取不到东西", cg_drop_file(&app, 0) == NULL, 1);

    cg_test_note("---- 边界：0 个 / app 为 NULL ----");
    cg_app_drop_put(&app, files, 0);
    cg_test_int("传 0 个 = 清空", cg_drop_count(&app), 0);
    cg_app_drop_put(NULL, files, 3);
    cg_test_int("app 为 NULL 时安全返回（不崩）", 1, 1);
}

CG_TEST_SUITE("拖放：路径清单的填 / 取 / 清", test_drop_list);
