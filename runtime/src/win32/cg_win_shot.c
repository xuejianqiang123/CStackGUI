/**
 * @file    cg_win_shot.c
 * @brief   把窗口或控件存成 PNG（工单取证、写文档配图）
 * @details 三个刻意的选择，都写在这里免得以后有人"顺手改回常规写法"：
 *
 *          **一、PrintWindow 优先，抓屏兜底。** `PrintWindow` 让窗口自己把内容画到内存 DC 上，
 *          所以窗口**被挡住、不在前台**也能拿到正确内容（抓屏那套拿到的是遮挡物的画面）。
 *          少数控件对 PrintWindow 不响应 ⇒ 失败时退回 `BitBlt` 抓屏，并**在日志里说明
 *          "这张是抓屏来的、可能被遮挡"** —— 宁可让人知道，也不给一张空图当证据。
 *
 *          **二、PNG 编码器 CLSID 写死，不去枚举。** `GdipSaveImageToFile` 要一个编码器 CLSID，
 *          常规做法是 `GetImageEncoders` 枚举一遍挑 `image/png`；但本项目对 GDI+ 是
 *          **按 ABI 手写声明**的（MinGW 的 gdiplusflat.h 在 C 下编不过，见 cg_win_internal.h），
 *          而 `ImageCodecInfo` 是 13 个字段的结构体 —— 手写它的布局一旦错一位就是内存踩踏。
 *          PNG 编码器的 CLSID 在 GDI+ 里是固定值（见 k_png_clsid），写死它就不用碰那个结构体：
 *          **少一份手写 ABI，少一份踩踏风险**。
 *
 *          **三、GDI+ 起不来就报失败。** 建窗口时 `GdiplusStartup` 失败（见 cg_win_window.c）时
 *          直接返回 0 —— 那种机器上 `image` 控件本来也只画占位框，如实报失败比给一张空图好。
 *
 *          文件路径走 UTF-8 → 宽字符，中文目录不会出错。
 */
#include "cg_win_internal.h"

#include <stdlib.h>
#include <string.h>

/** PNG 编码器的 CLSID（GDI+ 固定值：{557CF406-1A04-11D3-9A73-0000F81EF32E}）。
 *  按 GUID 的内存布局写（前三段小端），所以不是"照着字面念"的顺序。 */
static const unsigned char k_png_clsid[16] = {
    0x06, 0xF4, 0x7C, 0x55, 0x04, 0x1A, 0xD3, 0x11,
    0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E,
};

/** @brief 抓一个窗口（或控件）的像素并存成 PNG；成功 1，失败 0
 *  @param hwnd 目标窗口
 *  @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 */
static int shot_capture(HWND hwnd, const char *path)
{
    RECT     rc;
    int      cw, ch, ok = 0;
    HDC      win_dc = NULL, mem_dc = NULL;
    HBITMAP  bmp = NULL, old_bmp = NULL;
    void    *image = NULL;
    wchar_t *wpath;

    if (!hwnd || !path || !*path) return 0;
    if (!g_win_gdip_token) {
        cg_log(CG_LOG_WARN, "截图失败：这台机器上 GDI+ 没起来，存不了图");
        return 0;
    }
    if (!GetWindowRect(hwnd, &rc)) return 0;
    cw = rc.right - rc.left;
    ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) {
        cg_log(CG_LOG_WARN, "截图失败：窗口尺寸是 %dx%d", cw, ch);
        return 0;
    }

    wpath = cgw_wc_from_utf8(path);
    win_dc = GetWindowDC(hwnd);
    if (!wpath || !win_dc) goto done;
    mem_dc = CreateCompatibleDC(win_dc);
    if (!mem_dc) goto done;
    /* 必须用 CreateCompatibleBitmap（不是 CreateCompatibleBitmap 的 DC 版本）：
     * 8 位调色板位图存出来会丢色，而 DDB 才能被 GdipCreateBitmapFromHBITMAP 直接吃下。 */
    bmp = CreateCompatibleBitmap(win_dc, cw, ch);
    if (!bmp) goto done;
    old_bmp = (HBITMAP)SelectObject(mem_dc, bmp);
    if (!old_bmp) goto done;

    if (!PrintWindow(hwnd, mem_dc, 0)) {
        /* 少数控件不响应 PrintWindow：退回抓屏，并如实说明这张图可能被遮挡 */
        cg_log(CG_LOG_WARN, "PrintWindow 未成功，改用抓屏：这张图可能被其它窗口遮挡");
        if (!BitBlt(mem_dc, 0, 0, cw, ch, win_dc, 0, 0, SRCCOPY)) goto done;
    }
    if (GdipCreateBitmapFromHBITMAP(bmp, NULL, &image) != 0 || !image) {
        cg_log(CG_LOG_WARN, "截图失败：位图转 GDI+ 对象出错");
        goto done;
    }
    if (GdipSaveImageToFile(image, wpath, k_png_clsid, NULL) != 0) {
        cg_log(CG_LOG_WARN, "截图写文件失败：%s", path);
        goto done;
    }
    ok = 1;

done:
    if (image) GdipDisposeImage(image);
    if (old_bmp) SelectObject(mem_dc, old_bmp);
    if (bmp) DeleteObject(bmp);
    if (mem_dc) DeleteDC(mem_dc);
    if (win_dc) ReleaseDC(hwnd, win_dc);
    free(wpath);
    if (ok) cg_log(CG_LOG_INFO, "截图已存：%s（%dx%d）", path, cw, ch);
    return ok;
}

/** @brief 把整个窗口存成 PNG（含标题栏）；可用运行时 API 里最常用的那个 */
int cg_win_screenshot(cg_app_t *app, const char *path)
{
    if (!app || !app->hwnd) return 0;
    return shot_capture((HWND)app->hwnd, path);
}

/** @brief 把**一个控件**的原生窗口存成 PNG（给"这个框里到底显示成什么样"取证）。
 *  @note 日期时间这类由两个原生窗口拼成的控件，存的是主窗口那个。 */
int cg_widget_screenshot(cg_widget_t *w, const char *path)
{
    if (!w || !w->native) return 0;
    return shot_capture((HWND)w->native, path);
}
