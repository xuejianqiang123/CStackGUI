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
/**
 * @brief 用指定字号建一个雅黑字体（负高 = 按 em 取，与被测代码同口径）
 * @param em_px 字号（像素，内部按其负值建字体）
 * @return HFONT 建好的字体；失败返回 NULL
 */
static HFONT mk_font(int em_px)
{
    return CreateFontW(-em_px, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

/** 屏幕上量这个字体的行高（独立实现，不经过被测代码） */
/**
 * @brief 屏幕上量这个字体的行高（独立实现，不经过被测代码）
 * @param f 待测量的字体
 * @return int 该字体的行高（像素）；拿不到 DC 或量失败返回 0
 */
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
/**
 * @brief 建一个 240x40 的单行输入框（不显示），挂上皮肤子类
 * @param app 控件所属的应用
 * @param w 回填的控件结构（原生句柄指向新建的输入框）
 * @return HWND 建好的输入框窗口；失败返回 NULL
 */
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
/**
 * @brief 问一次非客户区计算，返回让出来的上边距（客户区顶 - 窗口顶）
 * @param h 待查询的编辑框窗口
 * @return int 让出来的上边距（像素）
 */
static int query_pad(HWND h)
{
    RECT rc;

    SetRect(&rc, 0, 0, 240, 40);
    SendMessageW(h, WM_NCCALCSIZE, TRUE, (LPARAM)&rc);
    return rc.top;
}

/** 期望的上边距：边框 + (客户高 - 文字高)/2（与 cg_skin_edit.c 同一套整数算术） */
/**
 * @brief 期望的上边距：边框 + (客户高 - 文字高) / 2（与被测代码同一套整数算术）
 * @param text_h 文字行高（像素）
 * @return int 期望的上边距（像素）
 */
static int expect_pad(int text_h)
{
    return CG_EDIT_BORDER + (40 - 2 * CG_EDIT_BORDER - text_h) / 2;
}

/** @brief pad 必须按控件自己的字体算，且换字号后跟着重算 */
/**
 * @brief 上边距必须按控件自己的字体算，且换字号后跟着重算
 * @return void 无返回值
 */
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

/** @brief 多行编辑框滚到底：cg_edit_scroll_end 把插入点移到末尾（串口监视器要这个） */
/**
 * @brief 多行编辑框滚到底：把插入点移到末尾（串口监视器要这个）
 * @return void 无返回值
 */
static void test_edit_scroll_end(void)
{
    HWND        h;
    cg_widget_t w;
    int         len;
    DWORD       sel;

    memset(&w, 0, sizeof(w));
    w.type = "edit";

    cg_test_note("---- cg_edit_scroll_end：多行编辑框滚到末尾 ----");
    cg_edit_scroll_end(NULL);              /* 不崩 */
    cg_test_ok("NULL 不崩", 1);

    h = CreateWindowExW(0, L"EDIT", L"", WS_OVERLAPPEDWINDOW | ES_MULTILINE,
                       0, 0, 240, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) { cg_test_ok("建多行 EDIT 窗口成功", 0); return; }
    cg_test_ok("建多行 EDIT 窗口成功", 1);

    w.native = h;
    SetWindowTextW(h, L"line1\r\nline2\r\nline3");
    len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);

    cg_edit_scroll_end(&w);                /* 插入点应到末尾 */
    sel = (DWORD)SendMessageW(h, EM_GETSEL, 0, 0);
    cg_test_ok("插入点移到末尾（start==end==长度）",
               LOWORD(sel) == (WORD)len && HIWORD(sel) == (WORD)len);

    /* 非编辑框：空转、不崩 */
    w.type = "label";
    cg_edit_scroll_end(&w);
    cg_test_ok("非编辑框空转不崩", 1);

    DestroyWindow(h);
}

CG_TEST_SUITE("编辑框：滚到底", test_edit_scroll_end);

/* ================= 多行编辑框渲染：横贯实心线复现 =================
 * 用户报：串口日志灌进多行编辑框后，内容区里出现一条条横贯整行的实心线
 * （每两行文本下面一条，颜色和边框同色系）。这里的复现思路：
 * 建真多行 EDIT -> 灌日志 -> 滚到底 -> 强制画完 -> 逐行扫客户区像素，
 * 统计"非白像素横贯整行"的行（正常文本行字间有空隙，覆盖率到不了 80%）。 */

/** 像素算不算背景（白/近白） */
/**
 * @brief 像素算不算背景（白 / 近白）
 * @param c 待判定的颜色
 * @return int 1 = 是背景色；0 = 不是
 */
static int px_is_bg(COLORREF c)
{
    return GetRValue(c) > 235 && GetGValue(c) > 235 && GetBValue(c) > 235;
}

/**
 * @brief 扫客户区：返回横贯实心线的条数，并把最高覆盖率 / 颜色打出来（诊断用）
 * @param h 待扫描的编辑框窗口
 * @param tag 打印诊断信息时用的标签
 * @return int 横贯实心线的条数；窗口尺寸为 0 或拿不到 DC 时返回 -1
 * @note **关键**：先 BitBlt 把整窗抓到离屏位图一次，再扫位图 —— 否则对皮肤编辑框逐像素
 *       取色会触发皮肤反复重画（29 万次 × 重画），巨慢且让窗口"无响应"
 */
static int scan_solid_lines(HWND h, const char *tag)
{
    RECT rc;
    HDC  dc, mem;
    HBITMAP bmp;
    int  x0, x1, span, y, hit_max = 0, y_max = -1, lines = 0;

    GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return -1;
    dc = GetDC(h);
    if (!dc) return -1;
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    if (mem && bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        UpdateWindow(h);                       /* 先保证画完，再抓一次 */
        BitBlt(mem, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        x0 = 12;
        x1 = rc.right - 40;                     /* 避开左内边距与右侧滚动条 */
        span = x1 - x0;
        for (y = 4; y < rc.bottom - 4; y++) {
            int hit = 0, x;
            for (x = x0; x < x1; x++) {
                COLORREF c = GetPixel(mem, x, y);
                if (!px_is_bg(c)) hit++;
            }
            if (hit > hit_max) { hit_max = hit; y_max = y; }
            if (span > 0 && hit * 100 >= span * 80) lines++;
        }
        if (y_max >= 0) {                      /* 把那条线的颜色打出来，判断是皮肤还是 EDIT 自己 */
            int R = 0, G = 0, B = 0, n = 0, xx;
            RECT wr;
            GetWindowRect(h, &wr);
            for (xx = x0; xx < x1; xx += 37) {
                COLORREF c = GetPixel(mem, xx, y_max);
                if (!px_is_bg(c)) { R += GetRValue(c); G += GetGValue(c); B += GetBValue(c); n++; }
            }
            printf("        [%s] 最高整行覆盖率 %d%%（y=%d, RGB=%02X%02X%02X, winH=%d, rc=%dx%d）；实心线条数 %d\n",
                   tag, span > 0 ? hit_max * 100 / span : 0, y_max,
                   n ? R / n : 0, n ? G / n : 0, n ? B / n : 0,
                   wr.bottom - wr.top, rc.right, rc.bottom, lines);
        } else {
            printf("        [%s] 最高整行覆盖率 %d%%（y=%d, rc=%dx%d）；实心线条数 %d\n",
                   tag, span > 0 ? hit_max * 100 / span : 0, y_max, rc.right, rc.bottom, lines);
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(h, dc);
    return lines;
}

/** 建一个 800x400 的多行 EDIT（带滚动条）；skin=1 时挂皮肤子类 */
/**
 * @brief 建一个 800x400 的多行输入框（带滚动条）
 * @param app 控件所属的应用
 * @param w 回填的控件结构（原生句柄指向新建的输入框）
 * @param skin 非 0 时挂上皮肤子类
 * @return HWND 建好的多行输入框窗口；失败返回 NULL
 */
static HWND mk_edit_multi(cg_app_t *app, cg_widget_t *w, int skin)
{
    HWND h = CreateWindowW(L"EDIT", L"",
                           WS_POPUP | ES_MULTILINE | ES_LEFT | ES_AUTOVSCROLL | WS_VSCROLL,
                           40, 40, 800, 400, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) return NULL;
    memset(w, 0, sizeof(*w));
    w->native  = h;
    w->app     = app;
    w->style.bg = CG_COLOR_NONE;
    if (skin) cg_skin_apply_edit(w);
    return h;
}

/** 灌 n 行 ESP 风格日志（\r\n 结尾，和真机一致），滚到底并强制画完 */
/**
 * @brief 灌 n 行日志风格的文本（行尾换行，和真机一致），滚到底并强制画完
 * @param h 目标多行编辑框
 * @param lines 要灌的行数
 * @return void 无返回值
 */
static void fill_log_and_paint(HWND h, int lines)
{
    static wchar_t buf[16384];
    HFONT f = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, L"Consolas");
    int    i, n = 0, len;
    MSG    msg;

    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    for (i = 0; i < lines && n < 15000; i++) {
        wchar_t *p = buf + n;
        int left = 16384 - n;
        int k = swprintf(p, (size_t)left,
                         L"I (%d) esp_psram: Speed: 80MHz heap ok line %d\r\n", i * 7, i);
        if (k <= 0) break;
        n += k;
    }
    SetWindowTextW(h, buf);
    len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    UpdateWindow(h);
    while (PeekMessageW(&msg, h, 0, 0, PM_REMOVE)) {   /* 排队的 WM_PAINT 跑干净 */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(f);
}

/**
 * @brief 流式灌日志：一轮 = 追加一行 -> 全量重设文本 -> 滚到底 -> 画完
 * @param h 目标多行编辑框
 * @param rounds 总轮数
 * @param scan_every 每多少轮扫一次实心线
 * @return void 无返回值
 * @note 和串口示例的追加逻辑同一条路径：重设文本会把滚动清回顶部，再滚回底部 ——
 *       每轮都是"整屏重画 + 滚动"的组合拳
 */
static void stream_lines(HWND h, int rounds, int scan_every)
{
    static wchar_t buf[32768];
    int n = 0, i, bad = 0;
    MSG msg;

    for (i = 0; i < 6; i++)
        n += swprintf(buf + n, (size_t)(32768 - n), L"-- 欢迎第 %d 行\r\n", i);
    for (i = 0; i < rounds && n < 30000; i++) {
        int len;
        n += swprintf(buf + n, (size_t)(32768 - n),
                      L"I (%d) esp_psram: Speed: 80MHz heap ok line %d\r\n", i * 7, i);
        SetWindowTextW(h, buf);
        len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
        SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageW(h, EM_SCROLLCARET, 0, 0);
        UpdateWindow(h);
        while (PeekMessageW(&msg, h, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if ((i + 1) % scan_every == 0) {
            char tag[64];
            sprintf(tag, "round %d", i + 1);
            bad = scan_solid_lines(h, tag);
            if (bad > 0) break;            /* 复现了：别刷了，赶紧看 */
        }
    }
    cg_test_int("流式灌入后无实心线", bad, 0);
}

/** 复现/回归：多行编辑框灌日志滚动后，内容区不得出现横贯实心线 */
/**
 * @brief 复现 / 回归：多行编辑框灌日志滚动后，内容区不得出现横贯实心线
 * @return void 无返回值
 */
static void test_edit_multiline_paint(void)
{
    cg_app_t app;
    cg_widget_t w;
    HWND parent, h;
    int  bad;

    memset(&app, 0, sizeof(app));

    cg_test_note("---- 多行编辑框：灌日志 + 滚动后不应出现横贯实心线 ----");

    h = mk_edit_multi(&app, &w, 0);        /* 对照组：原生 EDIT（顶层弹窗） */
    if (!h) { cg_test_ok("建原生多行 EDIT 成功", 0); return; }
    fill_log_and_paint(h, 40);
    bad = scan_solid_lines(h, "native");
    cg_test_int("原生多行 EDIT 无实心线", bad, 0);
    DestroyWindow(h);

    h = mk_edit_multi(&app, &w, 1);        /* 实验组：挂皮肤子类 */
    if (!h) { cg_test_ok("建挂皮肤多行 EDIT 成功", 0); return; }
    fill_log_and_paint(h, 40);
    bad = scan_solid_lines(h, "skinned");
    cg_test_int("挂皮肤后仍无实心线", bad, 0);
    DestroyWindow(h);

    /* 流式：子窗口 + 反复灌入滚动（真实串口助手的用法） */
    parent = CreateWindowW(L"STATIC", L"parent", WS_POPUP, 30, 30, 900, 500,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    UpdateWindow(parent);
    h = CreateWindowW(L"EDIT", L"",
                      WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN |
                      ES_AUTOVSCROLL | WS_VSCROLL | ES_LEFT,
                      8, 8, 800, 400, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) { cg_test_ok("建子窗口多行 EDIT 成功", 0); DestroyWindow(parent); return; }
    cg_test_ok("建子窗口多行 EDIT 成功", 1);

    memset(&w, 0, sizeof(w));
    w.native  = h;
    w.app     = &app;
    w.style.bg = CG_COLOR_NONE;
    cg_skin_apply_edit(&w);                /* 挂上皮肤再流式（真机组合） */
    stream_lines(h, 60, 5);                 /* 每 5 轮扫一次，留足余量抓实心线 */
    cg_test_ok("流式（挂皮肤）跑完没崩", 1);
    DestroyWindow(h);

    DestroyWindow(parent);
    cg_test_ok("收尾没崩", 1);
}

CG_TEST_SUITE("编辑框：多行渲染实心线", test_edit_multiline_paint);
