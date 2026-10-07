/**
 * @file    cg_win_drop.c
 * @brief   文件拖放：把资源管理器里的文件拖到窗口上 -> 派发窗口级 `drop` 事件
 * @details 用老接口 `WM_DROPFILES`（`DragAcceptFiles` 注册，建窗口时开），**不用 OLE 拖放**
 *          那套（要写一个 IDropTarget、注册、还要处理跨进程数据对象）—— 文件拖放这个场景
 *          老接口更省且够用；将来要支持"拖文本 / 拖图片进来"再上 OLE 也不迟。
 *
 *          **路径的生命周期是刻意的**：取完路径填进 app 的清单（cg_app_drop_put）→ 派发
 *          `drop` 事件 → 回调返回后**立刻清空**。这样业务代码不必自己管这块内存，
 *          代价是路径只在回调期间有效（文档里写明了"要留着就在回调里拷走"）。
 *          实现里先把 HDROP 全读成 UTF-8 再派发，就是为了不在回调里还持着系统的句柄过久。
 */
#include "cg_win_internal.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief 处理一次 WM_DROPFILES：取路径 -> 填清单 -> 派发 drop 事件 -> 清清单
 * @param app 目标应用；为 NULL 时直接返回
 * @param hdrop 消息带进来的 HDROP；**所有权归系统**，调用方负责 DragFinish，本函数只读
 * @return void 无返回值（清单只在 drop 回调期间有效，回调返回后立刻清掉）
 */
void cgw_win_drop(cg_app_t *app, HDROP hdrop)
{
    UINT   n, i, taken = 0;
    char **files;

    if (!app || !hdrop) return;

    n = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);     /* 个数：第二个参数传 -1 */
    if (n == 0) {
        cg_log(CG_LOG_WARN, "拖进来的东西里没有文件名（不是文件、或来源没给内容）");
        return;
    }
    files = (char **)calloc(n, sizeof(char *));
    if (!files) return;

    for (i = 0; i < n; i++) {
        UINT     len = DragQueryFileW(hdrop, i, NULL, 0);   /* 不含结尾 0 的长度 */
        wchar_t *w;
        char    *u8;

        if (len == 0) continue;
        w = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
        if (!w) continue;
        if (DragQueryFileW(hdrop, i, w, len + 1) == 0) {
            free(w);
            continue;
        }
        u8 = cgw_utf8_from_wcs(w);
        free(w);
        if (!u8) continue;
        files[taken++] = u8;
    }

    if (taken == 0) {
        cg_log(CG_LOG_WARN, "拖进来的 %u 项里没有一项能读成路径", (unsigned)n);
        free(files);
        return;
    }

    cg_app_drop_put(app, (const char *const *)files, (int)taken);
    for (i = 0; i < taken; i++) free(files[i]);
    free(files);

    cg_log(CG_LOG_INFO, "文件拖入：%d 个（第一个：%s）", cg_drop_count(app), app->drop_files[0]);

    /* 派发期间清单有效；回调返回后立刻收掉（业务要留就自己拷） */
    cgw_win_emit(app->window, CG_EVENT_DROP);
    cg_app_drop_put(app, NULL, 0);
}
