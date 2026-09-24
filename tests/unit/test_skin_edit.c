/**
 * @file    test_skin_edit.c
 * @brief   输入框皮肤（cg_skin_edit.c）的单元测试：单行文字垂直居中
 * @details 垂直居中靠 WM_NCCALCSIZE 让出上边距，pad = 边框 + (客户高 - 文字高)/2。
 *          曾经的坑：量文字高度时拿 GetDC 的 DC 直接量 —— 那个 DC 里选的是系统默认
 *          字体（16px 的 System 位图字体），不是控件设置的雅黑，量小了 pad 就算大，
 *          文字整条往下坠。这里建一个真 EDIT 挂上皮肤子类，用**控件字体**独立量一遍
 *          期望高度，断言 pad 与之一致；再换一次字号，断言 pad 跟着重算
 *          （WM_SETFONT 的"字高变了才重排"路径）。
 */
#include "cg_test.h"
#include "cg_internal.h"
#include "cg_skin.h"

#include <windows.h>
#include <stdio.h>

/** 用指定字号建一个雅黑字体（负高 = 按 em 取，和 cgw_font_for 同口径） */
static HFONT mk_font(int em_px)
{
    return CreateFontW(-em_px, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

/** 屏幕上量这个字体的行高（独立实现，不经过被测代码） */
static int font_height(HFONT f)
{
    HDC        dc = GetDC(NULL);
    HFONT      of;
    TEXTMETRICW tm;
    int th = 0;

    if (!dc) return 0;
    of = (HFONT)SelectObject(dc, f);
    if (GetTextMetricsW(dc, &tm)) th = tm.tmHeight;
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    return th;
}

/** 建一个 240x40 的单行 EDIT（不显示），挂上皮肤子类；失败返回 NULL */
static HWND mk_edit(cg_app_t *app, cg_widget_t *w)
{
    HWND h = CreateWindowW(L"EDIT", L"", WS_POPUP | ES_LEFT,
                           0, 0, 240, 40, NULL, NULL, GetModuleHandleW(NULL), NULL);

    if (!h) return NULL;
    memset(w, 0, sizeof(*w));
    w->native = h;
    w->app    = app;
    w->style.bg = CG_COLOR_NONE;       /* 走系统底色分支 */
    cg_skin_apply_edit(w);             /* 挂子类 + 初始布局 */
    return h;
}

/** 问一次 WM_NCCALCSIZE，返回让出来的上边距（客户区顶 - 窗口顶） */
static int query_pad(HWND h)
{
    RECT rc;

    SetRect(&rc, 0, 0, 240, 40);
    SendMessageW(h, WM_NCCALCSIZE, TRUE, (LPARAM)&rc);
    return rc.top;
}

/** 期望的上边距：边框 + (客户高 - 文字高)/2（与 cg_skin_edit.c 同一套整数算术） */
static int expect_pad(int text_h)
{
    return CG_EDIT_BORDER + (40 - 2 * CG_EDIT_BORDER - text_h) / 2;
}

/** @brief pad 必须按控件自己的字体算，且换字号后跟着重算 */
static void test_edit_vcenter(void)
{
    cg_app_t    app;
    cg_widget_t w;
    HWND        h;
    HFONT       f1, f2;
    int         pad1, pad2, th1, th2;

    memset(&app, 0, sizeof(app));      /* scale=0 -> apply 里按 100 处理 */

    cg_test_note("---- 单行输入框的垂直居中 ----");
    h = mk_edit(&app, &w);
    if (!h) { cg_test_ok("建 EDIT 窗口成功", 0); return; }
    cg_test_ok("建 EDIT 窗口成功", 1);

    f1 = mk_font(13);                  /* 相当于 10pt @96dpi（serialterm 的发送框） */
    cg_test_ok("建字体成功", f1 != NULL);
    if (!f1) { DestroyWindow(h); return; }

    SendMessageW(h, WM_SETFONT, (WPARAM)f1, TRUE);
    th1 = font_height(f1);
    pad1 = query_pad(h);
    printf("        10pt 雅黑行高 %dpx，实际 pad %dpx（期望 %d）\n", th1, pad1, expect_pad(th1));
    cg_test_int("pad 按控件自己的字体算（量错系统字体就会偏大、文字下沉）",
                pad1, expect_pad(th1));

    cg_test_note("---- 换字号：pad 要跟着重算 ----");
    f2 = mk_font(20);
    if (!f2) { cg_test_ok("建大号字体成功", 0); DeleteObject(f1); DestroyWindow(h); return; }
    SendMessageW(h, WM_SETFONT, (WPARAM)f2, TRUE);
    th2  = font_height(f2);
    pad2 = query_pad(h);
    cg_test_int("换大字号后 pad 变小（文字仍在正中）", pad2, expect_pad(th2));
    cg_test_ok("两个字号算出的 pad 确实不同（否则断言没区分度）", pad1 != pad2);

    DeleteObject(f2);
    DeleteObject(f1);
    DestroyWindow(h);                  /* WM_NCDESTROY 里子类自己摘、自己释放 */
    cg_test_ok("收尾没崩", 1);
}

CG_TEST_SUITE("输入框皮肤：单行垂直居中", test_edit_vcenter);
