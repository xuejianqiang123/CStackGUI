/**
 * @file    cg_win_shot.c
 * @brief   把窗口 / 控件 / 剪贴板图片存成 PNG（工单取证、写文档配图）
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

/**
 * @brief 把一个位图存成 PNG（截图与剪贴板图片共用这一条路）
 * @param bmp 待保存的位图；只读使用，不接管所有权
 * @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 * @return int 1 = 成功；0 = 参数为空 / GDI+ 没起来 / 转换或写盘失败（都写日志）
 */
static int shot_save_png(HBITMAP bmp, const char *path)
{
    void    *image = NULL;
    wchar_t *wpath;
    int      ok = 0;

    if (!bmp || !path || !*path) return 0;
    if (!g_win_gdip_token) {
        cg_log(CG_LOG_WARN, "存图失败：这台机器上 GDI+ 没起来");
        return 0;
    }
    wpath = cgw_wc_from_utf8(path);
    if (!wpath) return 0;

    if (GdipCreateBitmapFromHBITMAP(bmp, NULL, &image) != 0 || !image) {
        cg_log(CG_LOG_WARN, "存图失败：位图转 GDI+ 对象出错");
    } else if (GdipSaveImageToFile(image, wpath, k_png_clsid, NULL) != 0) {
        cg_log(CG_LOG_WARN, "存图失败：写文件出错：%s", path);
    } else {
        ok = 1;
    }
    if (image) GdipDisposeImage(image);
    free(wpath);
    return ok;
}

/**
 * @brief 抓一个窗口（或控件）的像素并存成 PNG
 * @param hwnd 目标窗口（含非客户区）
 * @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 * @return int 1 = 成功；0 = 参数为空 / 尺寸非法 / 取 DC 失败 / 存盘失败
 */
static int shot_capture(HWND hwnd, const char *path)
{
    RECT    rc;
    int     cw, ch, ok = 0;
    HDC     win_dc = NULL, mem_dc = NULL;
    HBITMAP bmp = NULL, old_bmp = NULL;

    if (!hwnd || !path || !*path) return 0;
    if (!GetWindowRect(hwnd, &rc)) return 0;
    cw = rc.right - rc.left;
    ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) {
        cg_log(CG_LOG_WARN, "截图失败：窗口尺寸是 %dx%d", cw, ch);
        return 0;
    }

    win_dc = GetWindowDC(hwnd);
    if (!win_dc) return 0;
    mem_dc = CreateCompatibleDC(win_dc);
    if (!mem_dc) goto done;
    /* 必须用 CreateCompatibleBitmap（不是 DC 版本的 CreateCompatibleBitmap 位图）：
     * 8 位调色板位图存出来会丢色，DDB 才能被 GdipCreateBitmapFromHBITMAP 直接吃下。 */
    bmp = CreateCompatibleBitmap(win_dc, cw, ch);
    if (!bmp) goto done;
    old_bmp = (HBITMAP)SelectObject(mem_dc, bmp);
    if (!old_bmp) goto done;

    if (!PrintWindow(hwnd, mem_dc, 0)) {
        /* 少数控件不响应 PrintWindow：退回抓屏，并如实说明这张图可能被遮挡 */
        cg_log(CG_LOG_WARN, "PrintWindow 未成功，改用抓屏：这张图可能被其它窗口遮挡");
        if (!BitBlt(mem_dc, 0, 0, cw, ch, win_dc, 0, 0, SRCCOPY)) goto done;
    }
    ok = shot_save_png(bmp, path);

done:
    if (old_bmp) SelectObject(mem_dc, old_bmp);
    if (bmp) DeleteObject(bmp);
    if (mem_dc) DeleteDC(mem_dc);
    if (win_dc) ReleaseDC(hwnd, win_dc);
    if (ok) cg_log(CG_LOG_INFO, "截图已存：%s（%dx%d）", path, cw, ch);
    return ok;
}

/**
 * @brief 把整个窗口存成 PNG（含标题栏）
 * @param app 所属应用；取其主窗口
 * @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 * @return int 1 = 成功；0 = app 或主窗口为空 / 抓图存盘失败
 */
int cg_win_screenshot(cg_app_t *app, const char *path)
{
    if (!app || !app->hwnd) return 0;
    return shot_capture((HWND)app->hwnd, path);
}

/**
 * @brief 把**一个控件**的原生窗口存成 PNG（"这个框里到底显示成什么样"取证用）
 * @param w 目标控件；存它的主原生窗口
 * @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 * @return int 1 = 成功；0 = 控件无原生窗口 / 抓图存盘失败
 * @note 日期时间这类由两个原生窗口拼成的控件，存的是主窗口那个
 */
int cg_widget_screenshot(cg_widget_t *w, const char *path)
{
    if (!w || !w->native) return 0;
    return shot_capture((HWND)w->native, path);
}

/**
 * @brief 位图深度与调色板：算出"像素数据"在设备无关位图里从第几字节开始
 * @param h 位图信息头（可能是 V5 头）；只读，不接管所有权
 * @return const void * 指向像素数据首字节的指针；指向 h 所在缓冲内部，调用方不得释放
 * @note 剪贴板给的是**打包形式**（信息头 + 调色板 + 位），位偏移要按头大小与位深自己算
 */
static const void *dib_bits(const BITMAPINFOHEADER *h)
{
    const unsigned char *p = (const unsigned char *)h;
    size_t               off = h->biSize;

    if (h->biBitCount <= 8) off += (size_t)(1u << h->biBitCount) * sizeof(RGBQUAD);
    else if (h->biCompression == BI_BITFIELDS) off += 3 * sizeof(DWORD);
    return p + off;
}

/** @brief 把剪贴板里的**图片**存成 PNG（把示波器/相机截的图直接落到调试记录里）
 *  @param path UTF-8 输出路径（扩展名随意，编码固定 PNG）
 *  @return int 1 = 成功；0 = 剪贴板里没有图片 / 打开剪贴板失败 / 存盘失败（都写日志）
 *  @note 优先 `CF_BITMAP`（系统会把它转成**本进程可用**的位图句柄，最省），
 *        没有就取 `CF_DIB` 自己拼一张。两条路都必须**在 CloseClipboard 之前**把像素复制出来 ——
 *        剪贴板只保证句柄在剪贴板打开期间有效，关掉之后再用就是悬空句柄。
 *        `CF_DIB` 里带的 alpha 会被丢掉（CreateDIBitmap 不保留），截图粘贴本来也是不透明的。 */
int cg_clip_get_image(const char *path)
{
    HBITMAP copy = NULL;
    int     ok = 0;

    if (!path || !*path) return 0;
    if (!g_win_gdip_token) {
        cg_log(CG_LOG_WARN, "取剪贴板图片失败：这台机器上 GDI+ 没起来");
        return 0;
    }
    if (!OpenClipboard(NULL)) {
        cg_log(CG_LOG_WARN, "取剪贴板图片失败：打不开剪贴板（可能被别的程序占着）");
        return 0;
    }

    {
        HBITMAP hb = (HBITMAP)GetClipboardData(CF_BITMAP);

        if (hb) copy = (HBITMAP)CopyImage(hb, IMAGE_BITMAP, 0, 0, 0);
    }
    if (!copy) {
        HGLOBAL h = GetClipboardData(CF_DIB);

        if (h) {
            const BITMAPINFOHEADER *bi = (const BITMAPINFOHEADER *)GlobalLock(h);

            if (bi) {
                HDC dc = GetDC(NULL);      /* 只当调色板参考，CreateDIBitmap 不依赖它持有 */
                copy = CreateDIBitmap(dc, bi, CBM_INIT, dib_bits(bi),
                                      (const BITMAPINFO *)bi, DIB_RGB_COLORS);
                ReleaseDC(NULL, dc);
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();      /* 像素已经复制出来了，句柄不再需要 */

    if (!copy) {
        cg_log(CG_LOG_WARN, "剪贴板里没有图片（先截个图或复制一张图再试）");
        return 0;
    }
    ok = shot_save_png(copy, path);
    DeleteObject(copy);
    if (ok) cg_log(CG_LOG_INFO, "剪贴板图片已存：%s", path);
    return ok;
}
