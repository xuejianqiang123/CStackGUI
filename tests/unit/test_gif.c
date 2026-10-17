/**
 * @file    test_gif.c
 * @brief   GIF 编码器的单测：字节级结构 + **用 GDI+ 当独立解码器**逐帧比像素
 * @details 编码器自己说"对"不算对 —— 所以这里让 GDI+ 来读：合成几帧 RGB、
 *          写成 GIF、再用 GDI+ 打开，逐帧取像素与测试自己维护的"期望画布"比。
 *          合成色取**少数几个纯净色**（黑 / 白 / 红 / 蓝）：首帧自适应调色板（中位切分，
 *          2026-10-17 起）会给每种颜色分到独立的盒，代表色 = 5-5-5 桶中心 —— 于是
 *          逐通道比对用容差 4（桶中心到真值的最大距离），其余结构断言照旧逐字节。
 *
 *          还做两件事：
 *          1. **走一遍顶层块**（扩展 / 图像描述符 / 尾符），数帧数、取最后一帧的
 *             left/top/w/h —— 用来验证脏矩形优化真的只写了变化那一块；
 *          2. 参数兜底：NULL / 非法尺寸 / 空路径都不崩、不留半成品文件。
 *
 *          GDI+ 的声明按 ABI 手写：运行时的 cg_win_internal.h 里也有一份，但那是给
 *          "存 PNG"用的（没有多帧读取那几个）；测试只读不写，自己声明一份免得改运行时。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ============================================================ GDI+（测试内自绘声明） */

/** GpStatus（GDI+ 的状态码，Ok = 0） */
typedef int gp_status;
/** GUID（FrameDimension 用） */
typedef struct { unsigned int d1; unsigned short d2, d3; unsigned char d4[8]; } gp_guid;
/** GdiplusStartup 的输入 */
typedef struct {
    unsigned int GdiplusVersion;
    void        *DebugEventCallback;
    int          SuppressBackgroundThread;
    int          SuppressExternalCodecs;
} gp_startup;

__declspec(dllimport) gp_status WINAPI GdiplusStartup(ULONG_PTR *token, const gp_startup *in, void *out);
__declspec(dllimport) void     WINAPI GdiplusShutdown(ULONG_PTR token);
__declspec(dllimport) gp_status WINAPI GdipCreateBitmapFromFile(const WCHAR *name, void **bmp);
__declspec(dllimport) gp_status WINAPI GdipCreateHBITMAPFromBitmap(void *bmp, HBITMAP *hbm, unsigned int back);
__declspec(dllimport) gp_status WINAPI GdipDisposeImage(void *img);
__declspec(dllimport) gp_status WINAPI GdipImageSelectActiveFrame(void *img, const gp_guid *dim, unsigned int i);
/* 取帧数（gdiplus 导出的真名是 GdipImageGetFrameCount，不是 GdipGetFrameCount） */
__declspec(dllimport) gp_status WINAPI GdipImageGetFrameCount(void *img, const gp_guid *dim, unsigned int *n);

/**
 * @brief FrameDimensionTime：GDI+ 多帧动画用的时间维度 GUID
 * @details 权威值取自 .NET 的 `[System.Drawing.Imaging.FrameDimension]::Time`：
 *          **{6aedbd6d-3fb5-418a-83a6-7f45229dc872}**，字节序（ToByteArray）为
 *          `6D BD ED 6A | B5 3F | 8A 41 | 83 A6 7F 45 22 9D C8 72`。
 *          **这里必须逐字节对**：后 8 个字节最容易从记忆里编错，写错了
 *          `GdipImageSelectActiveFrame` 只会回一个含糊的 Win32Error(7)，
 *          看不出是 GUID 不对（2026-10-14 实测踩到，前 8 字节对、后 8 字节错）。
 */
static const gp_guid k_dim_time = {
    0x6AEDBD6Du, 0x3FB5u, 0x418Au, { 0x83u, 0xA6u, 0x7Fu, 0x45u, 0x22u, 0x9Du, 0xC8u, 0x72u }
};

/**
 * @brief 起停 GDI+ 的小帮手：测试里起一次、用完关一次
 */
typedef struct { ULONG_PTR token; int ok; } gp_ctx;

/**
 * @brief 启动 GDI+（测试进程默认没起过它）
 * @param ctx 出口：令牌与成败
 * @return void 无返回值（失败只是 ctx->ok = 0，调用方据此跳过解码断言）
 */
static void gp_on(gp_ctx *ctx)
{
    gp_startup in;

    memset(&in, 0, sizeof(in));
    in.GdiplusVersion = 1;
    ctx->token = 0;
    ctx->ok = (GdiplusStartup(&ctx->token, &in, NULL) == 0);
}

/**
 * @brief 关掉 GDI+
 * @param ctx gp_on 起来的上下文
 * @return void 无返回值
 */
static void gp_off(gp_ctx *ctx)
{
    if (ctx->ok) GdiplusShutdown(ctx->token);
    ctx->ok = 0;
}

/* ============================================================ 顶层块扫描 */

/** 扫描结果：帧数 + 最后一帧图像描述符的位置尺寸 */
typedef struct {
    int count;
    int last_left, last_top, last_w, last_h;
    int gct_entries;                 /**< 全局色表项数（0 = 没有色表） */
    int loop_ext;                    /**< 1 = 有 Netscape 循环扩展 */
} gif_scan_t;

/**
 * @brief 走一遍 GIF 的顶层块，拿帧数与最后一帧的位置尺寸
 * @param path 文件路径
 * @param out 出口（成功时才填）
 * @return int 1 = 结构完整走通；0 = 文件读不到 / 不是 GIF89a / 块结构不认识
 */
static int gif_scan(const char *path, gif_scan_t *out)
{
    unsigned char *b;
    int            len, i;

    memset(out, 0, sizeof(*out));
    b = (unsigned char *)cg_file_read_bytes(path, &len);
    if (!b || len < 13) { cg_free(b); return 0; }
    if (memcmp(b, "GIF89a", 6) != 0) { cg_free(b); return 0; }

    if (b[10] & 0x80) out->gct_entries = 1 << ((b[10] & 0x07) + 1);
    i = 13 + (out->gct_entries ? 3 * out->gct_entries : 0);

    while (i < len) {
        if (b[i] == 0x3B) break;                                   /* 尾符 */
        if (b[i] == 0x21) {                                        /* 扩展：21 <label> <子块...> 00 */
            if (i + 1 >= len || b[i + 1] == 0xFF) out->loop_ext = 1;
            i += 2;
            while (i < len && b[i]) i += b[i] + 1;
            i++;
        } else if (b[i] == 0x2C) {                                 /* 图像描述符 */
            int packed;

            if (i + 9 > len) { cg_free(b); return 0; }
            out->count++;
            out->last_left = b[i + 1] | (b[i + 2] << 8);
            out->last_top  = b[i + 3] | (b[i + 4] << 8);
            out->last_w    = b[i + 5] | (b[i + 6] << 8);
            out->last_h    = b[i + 7] | (b[i + 8] << 8);
            packed = b[i + 9];
            i += 10;
            if (packed & 0x80) i += 3 * (1 << ((packed & 0x07) + 1));   /* 本地色表 */
            i++;                                                   /* LZW 最小码长 */
            while (i < len && b[i]) i += b[i] + 1;                 /* 数据子块 */
            i++;                                                   /* 块结束符 */
        } else {
            cg_free(b);
            return 0;                                              /* 不认识的块 */
        }
    }
    cg_free(b);
    return out->count > 0;
}

/* ============================================================ GDI+ 读一帧 */

/** 读帧失败在哪一步（诊断用：cg_test_note 打出来，省得下次再猜） */
static char s_step[40] = "?";

/**
 * @brief 用 GDI+ 读出第 frame 帧的像素（24 位 BGR，调用方给的缓冲）
 * @param path GIF 路径
 * @param frame 帧号（0 起）
 * @param w 出口：宽
 * @param h 出口：高
 * @param rgb 出口：w*h 个 RGB 像素（调用方保证够大）
 * @param ctx GDI+ 上下文
 * @return int 1 成功；0 失败（失败原因写在 s_step 里）
 * @note 帧号越界时 SelectActiveFrame 会失败 —— 测试就用这个判帧数（链接库里没有
 *       GdipGetFrameCount，而"第 4 帧选不到"等价于"正好 3 帧"）
 */
static int gdip_frame(const char *path, int frame, int *w, int *h,
                      unsigned char *rgb, gp_ctx *ctx)
{
    void    *img = NULL;
    HBITMAP  hbm = NULL;
    BITMAP   bm;
    BITMAPINFOHEADER bi;
    HDC      dc;
    int      y, ok = 0;

    {
        /* UTF-8 -> UTF-16（GDI+ 只收宽路径） */
        int need = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        WCHAR *ws = (WCHAR *)malloc((size_t)need * sizeof(WCHAR));

        if (!ws) { snprintf(s_step, sizeof(s_step), "malloc"); return 0; }
        MultiByteToWideChar(CP_UTF8, 0, path, -1, ws, need);
        if (GdipCreateBitmapFromFile(ws, &img) != 0 || !img) { snprintf(s_step, sizeof(s_step), "create"); free(ws); return 0; }
        free(ws);
    }
    {
        gp_status st = GdipImageSelectActiveFrame(img, &k_dim_time, (unsigned int)frame);

        if (st != 0) { snprintf(s_step, sizeof(s_step), "select=%d", st); goto done; }
    }
    if (GdipCreateHBITMAPFromBitmap(img, &hbm, 0xFFFFFFFFu) != 0 || !hbm) { snprintf(s_step, sizeof(s_step), "hbm"); goto done; }
    if (GetObject(hbm, sizeof(bm), &bm) != sizeof(bm)) { snprintf(s_step, sizeof(s_step), "getobj"); goto done; }

    *w = bm.bmWidth;
    *h = bm.bmHeight;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = -bm.bmHeight;                     /* 自上而下 */
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    dc = GetDC(NULL);
    if (!dc) goto done;
    {
        unsigned char *bgr = (unsigned char *)malloc((size_t)bm.bmWidth * bm.bmHeight * 3);

        if (!bgr) { ReleaseDC(NULL, dc); goto done; }
        if (GetDIBits(dc, hbm, 0, (UINT)bm.bmHeight, bgr, (BITMAPINFO *)&bi, DIB_RGB_COLORS)
                == (int)bm.bmHeight) {
            for (y = 0; y < bm.bmHeight; y++) {
                const unsigned char *s = bgr + (size_t)y * bm.bmWidth * 3;
                unsigned char       *d = rgb + (size_t)y * bm.bmWidth * 3;
                int x;

                for (x = 0; x < bm.bmWidth; x++) {
                    d[x * 3 + 0] = s[x * 3 + 2];
                    d[x * 3 + 1] = s[x * 3 + 1];
                    d[x * 3 + 2] = s[x * 3 + 0];
                }
            }
            ok = 1;
        }
        free(bgr);
        ReleaseDC(NULL, dc);
    }

done:
    if (hbm) DeleteObject(hbm);
    if (img) GdipDisposeImage(img);
    return ok;
}

/* ============================================================ 合成帧（精确色） */

#define T_W 64
#define T_H 48
/** 几个纯净色：自适应调色板会给每色分到独立的盒，代表色误差 <= 4（见 diff_count 的容差） */
static const unsigned char k_black[3] = { 0, 0, 0 };
static const unsigned char k_white[3] = { 255, 255, 255 };
static const unsigned char k_red[3]   = { 255, 0, 0 };
static const unsigned char k_blue[3]  = { 0, 0, 255 };

/**
 * @brief 画棋盘底（8x8 一块黑白相间）
 * @param rgb 出口缓冲（T_W*T_H 像素）
 * @return void 无返回值
 */
static void fill_chess(unsigned char *rgb)
{
    int x, y;

    for (y = 0; y < T_H; y++)
        for (x = 0; x < T_W; x++) {
            const unsigned char *c = (((x / 8) + (y / 8)) & 1) ? k_white : k_black;
            unsigned char       *d = rgb + ((size_t)y * T_W + x) * 3;

            d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
        }
}

/**
 * @brief 在期望画布上糊一个色块（测试自己的"期望模型"，与编码器无关）
 * @param rgb 期望画布
 * @param x0 块左上角
 * @param y0 块左上角
 * @param w 块宽
 * @param h 块高
 * @param c 颜色
 * @return void 无返回值
 */
static void fill_rect(unsigned char *rgb, int x0, int y0, int w, int h, const unsigned char *c)
{
    int x, y;

    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            unsigned char *d = rgb + ((size_t)y * T_W + x) * 3;

            d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
        }
}

/** 像素判"一致"的通道容差：自适应调色板的代表色是 5-5-5 桶中心，离真值最多 4 */
#define TOL 4

/**
 * @brief 逐像素比两个 RGB 画布，返回不一致的像素数（带容差）
 * @param a 画布 A
 * @param b 画布 B
 * @return int 超出容差的像素个数（0 = 每个像素三通道都在 TOL 内）
 * @note 旧版按逐字节比相等 —— 那依赖"合成色恰好在固定调色板里"的巧合；
 *       2026-10-17 调色板改为按首帧分布生成后，代表色是桶中心，逐字节相等
 *       的前提不存在了，容差 4 是这条链路上的数学上限（不是拍脑袋的松约束）。
 */
static int diff_count(const unsigned char *a, const unsigned char *b)
{
    int i, n = 0;

    for (i = 0; i < T_W * T_H; i++) {
        int c;

        for (c = 0; c < 3; c++) {
            int d = (int)a[i * 3 + c] - (int)b[i * 3 + c];

            if (d < 0) d = -d;
            if (d > TOL) { n++; break; }     /* 一个通道超差就算这个像素不一致 */
        }
    }
    return n;
}

/* ============================================================ 测试用例 */

/**
 * @brief 参数兜底：NULL / 非法尺寸 / 空路径都干净失败，不留文件
 * @return void 无返回值
 */
static void test_gif_contract(void)
{
    cg_file_delete("_gif_none.gif");
    cg_test_ok("路径 NULL：返回 NULL 不崩", cg_gif_open(NULL, 8, 8) == NULL);
    cg_test_ok("路径空串：返回 NULL 不崩", cg_gif_open("", 8, 8) == NULL);
    cg_test_ok("宽 0：返回 NULL 不崩", cg_gif_open("_gif_none.gif", 0, 8) == NULL);
    cg_test_ok("高为负：返回 NULL 不崩", cg_gif_open("_gif_none.gif", 8, -1) == NULL);
    cg_test_ok("宽超 u16：返回 NULL 不崩", cg_gif_open("_gif_none.gif", 65536, 8) == NULL);
    cg_test_ok("  -> 且没留下文件", cg_file_exists("_gif_none.gif") == 0);
    cg_test_int("编码器传 NULL：报失败不崩", cg_gif_add(NULL, NULL, 0, 100), 0);
    cg_test_int("像素传 NULL：报失败不崩", cg_gif_add(NULL, "x", 0, 100), 0);
    cg_test_int("关闭传 NULL：报失败不崩", cg_gif_close(NULL), 0);
    cg_gif_abort(NULL);                            /* 幂等：不该崩 */
    cg_test_ok("abort(NULL) 不崩", 1);
    cg_test_int("录屏：没窗口时开始失败", cg_gif_rec_start(NULL, 0, 0, 8, 8, 10), 0);
    cg_test_int("录屏：没在录时停止失败", cg_gif_rec_stop(NULL, "x.gif"), 0);
    {
        int frames = -1, ms = -1;

        cg_gif_rec_info(NULL, &frames, &ms);
        cg_test_int("录屏：没在录时帧数报 0", frames, 0);
        cg_test_int("录屏：没在录时计时报 0", ms, 0);
    }
}

/**
 * @brief 合成 3 帧 -> 编码 -> GDI+ 解码 -> 逐帧逐像素比 + 结构断言
 * @return void 无返回值
 */
static void test_gif_roundtrip(void)
{
    static unsigned char f1[T_W * T_H * 3];
    static unsigned char f2[T_W * T_H * 3];
    static unsigned char f3[T_W * T_H * 3];
    static unsigned char want[T_W * T_H * 3];
    static unsigned char got[T_W * T_H * 3];
    gp_ctx    gp;
    gif_scan_t sc;
    cg_gif_t *g;
    int       i, w = 0, h = 0, bad = 0;
    void     *img = NULL;

    cg_file_delete("_gif_rt.gif");

    fill_chess(f1);
    memcpy(f2, f1, sizeof(f2));
    fill_rect(f2, 10, 8, 16, 12, k_red);            /* 第 2 帧：中间一块变红 */
    memcpy(f3, f2, sizeof(f3));
    fill_rect(f3, 40, 30, 12, 10, k_blue);          /* 第 3 帧：右下再糊一块蓝 */

    g = cg_gif_open("_gif_rt.gif", T_W, T_H);
    cg_test_ok("打开编码器", g != NULL);
    if (!g) return;
    cg_test_int("第 1 帧写入", cg_gif_add(g, f1, T_W * 3, 100), 1);
    cg_test_int("第 2 帧写入", cg_gif_add(g, f2, T_W * 3, 100), 1);
    cg_test_int("第 3 帧写入", cg_gif_add(g, f3, T_W * 3, 100), 1);
    cg_test_int("收尾成功", cg_gif_close(g), 1);

    /* ---- 结构断言 ---- */
    cg_test_ok("文件生成了", cg_file_exists("_gif_rt.gif"));
    cg_test_ok("顶层块走通", gif_scan("_gif_rt.gif", &sc));
    cg_test_int("  -> 帧数 = 3", sc.count, 3);
    cg_test_int("  -> 全局色表 256 项", sc.gct_entries, 256);
    cg_test_ok("  -> 有循环扩展", sc.loop_ext);
    cg_test_int("  -> 末帧宽 = 12（脏矩形：只有变蓝的那块）", sc.last_w, 12);
    cg_test_int("  -> 末帧高 = 10", sc.last_h, 10);
    cg_test_int("  -> 末帧 left = 40", sc.last_left, 40);
    cg_test_int("  -> 末帧 top = 30", sc.last_top, 30);

    /* ---- GDI+ 当独立解码器：逐帧比 ---- */
    gp_on(&gp);
    if (!gp.ok) {
        cg_test_note("GDI+ 起不来：跳过解码断言（本机应有）");
        return;
    }
    {
        int need = MultiByteToWideChar(CP_UTF8, 0, "_gif_rt.gif", -1, NULL, 0);
        WCHAR *ws = (WCHAR *)malloc((size_t)need * sizeof(WCHAR));

        if (ws) {
            MultiByteToWideChar(CP_UTF8, 0, "_gif_rt.gif", -1, ws, need);
            if (GdipCreateBitmapFromFile(ws, &img) != 0) img = NULL;
            free(ws);
        }
    }
    cg_test_ok("GDI+ 打得开这个 GIF（独立解码器认可）", img != NULL);
    if (!img) { gp_off(&gp); return; }

    /* 先问帧数再选帧：GDI+ 从文件建位图时**只解了第一帧**，帧表要等问帧数才补齐 ——
     * 不先问就选第 1 帧，它会回 Win32Error(7)（实测踩到过）。 */
    {
        unsigned int n = 0;
        gp_status    st = GdipImageGetFrameCount(img, &k_dim_time, &n);

        cg_test_int("  -> GDI+ 报的帧数 = 3", (st == 0) ? (long long)n : (long long)-1, 3);
    }

    /* 逐帧比：第 i 帧放出来应该等于 f(i+1)（第 2、3 帧靠上一帧透出 + 脏矩形补差）。
     * 循环到 4 是故意的：第 4 帧选不到，正好证明文件里只有 3 帧。 */
    for (i = 0; i < 4; i++) {
        char what[64];

        if (!gdip_frame("_gif_rt.gif", i, &w, &h, got, &gp)) {
            if (i < 3) {
                char why[80];

                snprintf(why, sizeof(why), "GDI+ 读得出该帧（卡在 %s）", s_step);
                cg_test_ok(why, 0);
                bad++;
            } else {
                cg_test_ok("  -> 第 4 帧选不到（正好 3 帧）", 1);
            }
            continue;
        }
        if (i == 3) {                            /* 多出一帧也是错 */
            cg_test_ok("  -> 不该有第 4 帧", 0);
            bad++;
            continue;
        }
        memcpy(want, (i == 0) ? f1 : (i == 1) ? f2 : f3, sizeof(want));
        if (w != T_W || h != T_H) { cg_test_ok("  -> 尺寸 64x48", 0); bad++; continue; }
        {
            int d = diff_count(want, got);

            snprintf(what, sizeof(what), "  -> 第 %d 帧逐像素一致", i + 1);
            cg_test_int(what, d, 0);
            if (d) bad++;
        }
    }
    cg_test_int("三帧全部一致", bad, 0);
    GdipDisposeImage(img);
    gp_off(&gp);
}

/**
 * @brief 脏矩形与跳帧：整帧没变应返回 2；小块变化只写那一块
 * @return void 无返回值
 */
static void test_gif_dirty(void)
{
    static unsigned char a[T_W * T_H * 3];
    static unsigned char b[T_W * T_H * 3];
    cg_gif_t  *g;
    gif_scan_t sc;

    cg_file_delete("_gif_dirty.gif");
    fill_chess(a);
    memcpy(b, a, sizeof(b));
    fill_rect(b, 5, 5, 9, 7, k_red);

    g = cg_gif_open("_gif_dirty.gif", T_W, T_H);
    cg_test_ok("打开编码器（脏矩形）", g != NULL);
    if (!g) return;
    cg_test_int("首帧写入", cg_gif_add(g, a, T_W * 3, 100), 1);
    cg_test_int("整帧没变：返回 2（跳过）", cg_gif_add(g, a, T_W * 3, 100), 2);
    cg_test_int("再没变：还是 2", cg_gif_add(g, a, T_W * 3, 100), 2);
    cg_test_int("小块变化：写入", cg_gif_add(g, b, T_W * 3, 100), 1);
    cg_test_int("收尾成功", cg_gif_close(g), 1);

    cg_test_ok("结构走通", gif_scan("_gif_dirty.gif", &sc));
    cg_test_int("  -> 只有 2 帧（第 2、3 帧被跳过）", sc.count, 2);
    cg_test_int("  -> 末帧宽 = 9（脏矩形）", sc.last_w, 9);
    cg_test_int("  -> 末帧高 = 7", sc.last_h, 7);
    cg_test_int("  -> 末帧 left = 5", sc.last_left, 5);
    cg_test_int("  -> 末帧 top = 5", sc.last_top, 5);
}

/**
 * @brief 一帧都没写就收尾：算失败，且不留下文件
 * @return void 无返回值
 */
static void test_gif_empty(void)
{
    cg_gif_t *g;

    cg_file_delete("_gif_empty.gif");
    g = cg_gif_open("_gif_empty.gif", 8, 8);
    cg_test_ok("打开编码器（空）", g != NULL);
    if (!g) return;
    cg_test_int("一帧都没写：收尾返回 0", cg_gif_close(g), 0);
    cg_test_int("  -> 且文件被删掉", cg_file_exists("_gif_empty.gif"), 0);
}

CG_TEST_SUITE("GIF：参数不合法时的接口契约", test_gif_contract);
CG_TEST_SUITE("GIF：合成帧 -> 编码 -> GDI+ 解码 -> 逐帧比像素", test_gif_roundtrip);
CG_TEST_SUITE("GIF：脏矩形与整帧跳过的结构断言", test_gif_dirty);
CG_TEST_SUITE("GIF：一帧都没写时收尾失败且不留文件", test_gif_empty);
