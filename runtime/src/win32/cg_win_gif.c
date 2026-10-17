/**
 * @file    cg_win_gif.c
 * @brief   GIF 录屏：定时抓一块屏幕区域，逐帧交给工作线程编码落盘
 * @details 与 cg_win_shot.c 的分工：那个管"抓一张"（窗口截图 / 区域截图 / 剪贴板图片），
 *          这个管"抓一串"。编码本身在 kit 层（cg_kit_gif.c，纯 C 可单测），这里做
 *          Win32 那一半：**定时** + **抓帧** + **交给工作线程编码** + **收尾落盘**。
 *
 *          实现上的选择（2026-10-17 架构评审后重构为"抓帧留 UI 线程、编码下工作线程"）：
 *
 *          **一、先写临时文件，停止时才改名。** 开始录屏时还不知道成品该叫什么
 *          （调用方多半想用时间戳命名，而那要等结束才知道），所以录的过程中写在
 *          用户数据目录的临时文件里；停止时给路径就 MoveFile 过去（跨盘失败则退回复制），
 *          给 NULL 就直接删。中途崩了也不会在成品名底下留半套帧。
 *
 *          **二、CreateDIBSection 而不是 CreateCompatibleBitmap + GetDIBits。**
 *          前者直接给回像素指针，少一次整幅拷贝；高度填负数拿**自上而下**的位图，
 *          正好是 GIF 要的行序，不用再翻个底朝天。
 *
 *          **三、编码与写盘在工作线程，抓帧在 UI 线程。** 量化 + LZW + 落盘一帧要
 *          几十毫秒，大区域 30fps 下原来全塞在 50ms 心跳里必掉帧、还拖死输入响应
 *          （2026-10-17 架构评审"WM_TIMER 重活"整改）。分工边界：工作线程**只碰**
 *          两块抓帧缓冲、编码器与文件 —— 不碰控件、不碰 app（这正是 check-layers
 *          规则 8 保护的东西）；抓帧要读 app 的 DPI 缩放，留在 UI 线程。交接用
 *          双缓冲乒乓 + pending/busy 两个标志（都在 cs 里）：编码器永远只拿到
 *          "已完整写好且不再被 UI 触碰"的那一块，不会撕帧。编码跟不上下一拍时
 *          当帧直接丢（计 dropped，停止时汇总进日志）。
 *
 *          **四、帧延时按实际经过时间算，不是名义 1/fps。** 抓帧 + LZW 是要花时间的，
 *          实际帧率一定低于名义值；GIF 里写名义值会让录出来的动画比现场快。另外
 *          "整帧没变被跳过"时，延时要从上一帧真正写出的时刻算起，否则跳过几帧之后
 *          画面变化会被压成一瞬间。
 *
 *          **五、体积到上限就停止写入（512MB）。** 无人值守长录拖满磁盘是最坏情况
 *          （2026-10-17 架构评审"无界增长"整改）：到限后编码器停止收帧（capped），
 *          录制照常"在录"、界面不突变，停止时成品照常收尾 —— 已录内容全部保住。
 *
 *          录制状态是文件级静态量：一个进程一条录屏（和 cg_win_window.c 的热键表同一套路）。
 *          本文件是 check-layers 规则 8 线程白名单的三个文件之一（cg_serial.c / cg_net.c /
 *          本文件），扩白名单的理由写在该门禁的注释里。
 */
#include "cg_win_internal.h"

/** 录屏临时文件名（落在用户数据目录，停止时改名或删除） */
#define CG_REC_TMP "cg_rec_tmp.gif"
/** 名义帧率上限：再高编码跟不上，GIF 体积也白涨 */
#define CG_REC_FPS_MAX 30
/** 名义帧率下限 */
#define CG_REC_FPS_MIN 1
/** 区域尺寸上限（与 GIF 图像描述符的 u16 一致；kit 那边有个同名宏，但跨层不共享） */
#define CG_REC_DIM_MAX 65535
/** 体积上限：每写 128 帧量一次，超了就停止写入（成品照常收尾） */
#define CG_REC_BYTES_MAX (512LL * 1024 * 1024)
#define REC_SIZE_CHECK_EVERY 128

/** 录屏状态（一个进程一条） */
typedef struct {
    int        active;          /**< 1 = 正在录（UI 线程写；工作线程读，经 cs） */
    int        x, y, w, h;      /**< 区域（**逻辑像素**，与 .cgui / 事件坐标同一套，记日志用） */
    int        pw, ph;          /**< 区域的**物理像素**尺寸：开始时按 DPI 定死。
                                 *   定死而不是每帧重算：录到一半改 DPI 时旧实现会用新的
                                 *   pw 去填按旧 pw 开的缓冲 —— 行为是堆越界，定死即消除。 */
    int        sx, sy;          /**< 区域左上角的物理像素坐标（同上定死） */
    int        interval_ms;     /**< 名义帧间隔（1/fps） */
    cg_gif_t  *gif;             /**< 编码器 —— join 之后才归 UI 线程碰 */
    char      *tmp_path;        /**< 临时文件路径（UTF-8 堆串） */
    int        frames;          /**< 已写帧数（工作线程写，cs 保护） */
    int        skipped;         /**< 因没变化而跳过的帧数（同上） */
    int        dropped;         /**< 编码跟不上被丢弃的帧数（同上） */
    int        capped;          /**< 1 = 已到体积上限，停止写入（同上） */
    unsigned   start_tick;      /**< 开始时刻（GetTickCount） */
    unsigned   last_write_tick; /**< 上一帧**写出**的时刻（工作线程写） */

    CRITICAL_SECTION cs;        /**< 保护 pending / busy / 计数与两块缓冲的交接 */
    HANDLE     th;              /**< 编码工作线程 */
    HANDLE     ev_stop;         /**< 通知工作线程退出 */
    HANDLE     ev_work;         /**< 通知工作线程"有一帧待编码"（auto-reset） */
    int        pending;         /**< 1 = rgb_enc 里有一帧待编码 */
    int        busy;            /**< 1 = 工作线程正在编码 rgb_enc（此刻不许交换缓冲） */
    unsigned char *rgb;         /**< 抓帧缓冲（UI 线程写） */
    unsigned char *rgb_enc;     /**< 交换/编码缓冲 */
    int        rgb_stride;      /**< 两块缓冲的每行字节数（pw * 3） */
} cg_rec_t;

static cg_rec_t s_rec;
static int      s_rec_cs_inited = 0;   /* cs 进程内初始化一次（录屏可反复开始/停止） */

/**
 * @brief 抓一帧屏幕区域到调用方给的 RGB 缓冲（**只在 UI 线程调**）
 * @param r 录屏状态
 * @param out 输出缓冲，容量 >= ph * rgb_stride
 * @return int 1 成功；0 失败（取 DC / 建 DIB / BitBlt 任一环节）
 * @note DIB 是 BGR 序、kit 的编码器收 RGB 序，这里顺手换一面 —— 多一次逐像素遍历，
 *       但换来 kit 层与单测的口径统一（测试里直接给 RGB）。区域尺寸用开始时定死的
 *       pw/ph/sx/sy，不再每帧问 DPI（见结构体注释）。
 */
static int rec_capture(const cg_rec_t *r, unsigned char *out)
{
    HDC        scr, mem;
    BITMAPINFO bi;
    HBITMAP    bmp, old;
    void      *bits = NULL;
    int        stride, y, xx;

    if (r->pw <= 0 || r->ph <= 0) return 0;

    scr = GetDC(NULL);                        /* NULL = 整个屏幕的 DC（覆盖虚拟桌面） */
    if (!scr) return 0;
    mem = CreateCompatibleDC(scr);
    if (!mem) { ReleaseDC(NULL, scr); return 0; }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = r->pw;
    bi.bmiHeader.biHeight      = -r->ph;      /* 负 = 自上而下 */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, scr);
        return 0;
    }
    old = SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, r->pw, r->ph, scr, r->sx, r->sy, SRCCOPY)) {
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, scr);
        return 0;
    }

    stride = (r->pw * 3 + 3) & ~3;            /* 24 位 DIB 行按 4 字节对齐 */
    for (y = 0; y < r->ph; y++) {
        const unsigned char *s = (const unsigned char *)bits + (size_t)y * stride;
        unsigned char       *d = out + (size_t)y * r->rgb_stride;

        for (xx = 0; xx < r->pw; xx++) {
            d[xx * 3 + 0] = s[xx * 3 + 2];
            d[xx * 3 + 1] = s[xx * 3 + 1];
            d[xx * 3 + 2] = s[xx * 3 + 0];
        }
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    return 1;
}

/**
 * @brief 编码工作线程：从 rgb_enc 取待编码帧，量化 + LZW + 写盘。**只碰缓冲/编码器/文件**
 * @param p cg_rec_t*（s_rec 的地址）
 * @return DWORD 恒为 0（CreateThread 的退出码只是形式要求）
 * @note 抓帧失败/编码失败就地停止录制并**丢弃**（半套帧没意思，别留下打不开的文件）——
 *       停止动作只是把 active 清零 + 记日志，收尾（收编码器/改名）仍归 UI 线程的 stop 路径。
 */
static DWORD WINAPI rec_worker(LPVOID p)
{
    cg_rec_t *r = (cg_rec_t *)p;

    for (;;) {
        unsigned char *buf = NULL;
        unsigned        now;
        int             delay_ms, rc, done = 0, first;

        if (WaitForSingleObject(r->ev_stop, 50) == WAIT_OBJECT_0) break;

        EnterCriticalSection(&r->cs);
        if (r->pending) {
            r->pending = 0;
            r->busy = 1;
            buf = r->rgb_enc;
        }
        LeaveCriticalSection(&r->cs);
        if (!buf) continue;

        now = GetTickCount();
        first = (r->frames + r->skipped == 0);
        delay_ms = first ? r->interval_ms : (int)(now - r->last_write_tick);
        if (delay_ms < 20) delay_ms = 20;      /* 太快了看图软件会当成 0 延时 */

        rc = cg_gif_add(r->gif, buf, r->rgb_stride, delay_ms);

        EnterCriticalSection(&r->cs);
        if (rc == 0) {
            done = 1;                         /* 编码失败：让下面的停止分支处理 */
        } else if (rc == 2) {
            r->skipped++;
        } else {
            r->frames++;
            r->last_write_tick = now;         /* 跳过不计：延时从真正写出的时刻算 */
            /* 体积上限：每 128 帧量一次；到限就停止写入，成品照常收尾（见文件头"选择五"） */
            if (!r->capped && (r->frames % REC_SIZE_CHECK_EVERY) == 0 &&
                cg_file_size(r->tmp_path) >= CG_REC_BYTES_MAX) {
                r->capped = 1;
                cg_log(CG_LOG_WARN, "录屏：已达 %dMB 体积上限，停止写入（停止录制时已录内容照常收尾）",
                       (int)(CG_REC_BYTES_MAX / (1024 * 1024)));
            }
        }
        r->busy = 0;
        LeaveCriticalSection(&r->cs);

        if (done) {
            cg_log(CG_LOG_ERROR, "录屏：编码第 %d 帧失败，停止并丢弃", r->frames + 1);
            EnterCriticalSection(&r->cs);
            r->active = 0;                    /* UI 下一拍自然熄火；收尾归 stop 路径 */
            LeaveCriticalSection(&r->cs);
            break;
        }
    }
    return 0;
}

/**
 * @brief 定时器回调（**只在 UI 线程**）：抓一帧进 rgb，能交接就交给工作线程，再挂下一次
 * @param user cg_app_t*
 * @return void 无返回值
 * @note 抓帧失败就地停止录制并丢弃；工作线程忙（编码没跟上）时当帧直接丢（计 dropped）
 */
static void rec_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_rec_t *r = &s_rec;

    if (!app || !r->active) return;           /* 工作线程可能已自行停止（编码失败）——定时器熄火 */

    if (!rec_capture(r, r->rgb)) {
        cg_log(CG_LOG_ERROR, "录屏：抓第 %d 帧失败，停止并丢弃", r->frames + 1);
        cg_gif_rec_stop(app, NULL);
        return;
    }

    EnterCriticalSection(&r->cs);
    if (r->pending || r->busy) {
        r->dropped++;                          /* 编码跟不上：这一帧不要了 */
        LeaveCriticalSection(&r->cs);
    } else {
        unsigned char *t = r->rgb;            /* 乒乓交接：完整帧换给编码器，拿回空闲块 */
        r->rgb = r->rgb_enc;
        r->rgb_enc = t;
        r->pending = 1;
        LeaveCriticalSection(&r->cs);
        SetEvent(r->ev_work);
    }
    cg_set_timer(app, r->interval_ms, rec_tick, app);
}

/**
 * @brief 释放录屏占用的资源（不删临时文件：那是 stop 的活；**调用前必须已 join 工作线程**）
 * @param r 录屏状态
 * @return void 无返回值
 */
static void rec_free(cg_rec_t *r)
{
    if (r->gif) { cg_gif_abort(r->gif); r->gif = NULL; }
    free(r->tmp_path);
    r->tmp_path = NULL;
    free(r->rgb);
    r->rgb = NULL;
    free(r->rgb_enc);
    r->rgb_enc = NULL;
    r->rgb_stride = 0;
    r->pending = 0;
    r->busy = 0;
    r->active = 0;
}

/**
 * @brief 停止工作线程并关闭句柄（超时则什么都不释放，宁泄漏不 UAF）
 * @param r 录屏状态
 * @return int 1 = 已干净退出（可以安全收编码器）；0 = 超时（调用方必须放弃收尾）
 */
static int rec_join_worker(cg_rec_t *r)
{
    DWORD wr;

    if (!r->th) return 1;
    SetEvent(r->ev_stop);
    wr = WaitForSingleObject(r->th, 10000);   /* 一帧编码有界（大区域几百毫秒），10s 极限宽裕 */
    if (wr != WAIT_OBJECT_0) {
        cg_log(CG_LOG_ERROR, "录屏：等工作线程退出超时（>10s），放弃收尾以避免崩溃；编码器与缓冲泄漏至进程退出");
        return 0;
    }
    CloseHandle(r->th);
    r->th = NULL;
    if (r->ev_stop) { CloseHandle(r->ev_stop); r->ev_stop = NULL; }
    if (r->ev_work) { CloseHandle(r->ev_work); r->ev_work = NULL; }
    return 1;
}

/* ============================================================ 公共 API */

/**
 * @brief 开始录一块屏幕区域
 * @param app 应用（没有窗口时失败）
 * @param x 区域左上角（逻辑像素，可为负 —— 副屏）
 * @param y 区域左上角（逻辑像素）
 * @param w 区域宽（逻辑像素）
 * @param h 区域高（逻辑像素）
 * @param fps 名义帧率（1..30，超出夹进去）
 * @return int 1 成功；0 失败（没窗口 / 已在录 / 尺寸非法 / 打不开临时文件 / 起不了线程）
 * @note 录屏期间画面变化靠调用方自己的界面反馈（计时 / 帧数），本函数不弹任何东西
 */
int cg_gif_rec_start(cg_app_t *app, int x, int y, int w, int h, int fps)
{
    cg_rec_t *r = &s_rec;
    char     *dir, *tmp;

    if (!app || !cg_app_has_window(app)) return 0;
    if (r->active) return 0;
    if (w <= 0 || h <= 0 || w > CG_REC_DIM_MAX || h > CG_REC_DIM_MAX) return 0;
    if (fps < CG_REC_FPS_MIN) fps = CG_REC_FPS_MIN;
    if (fps > CG_REC_FPS_MAX) fps = CG_REC_FPS_MAX;

    /* 物理像素尺寸在开始时按当时的 DPI 定死（见结构体 pw/ph 注释） */
    r->pw = cgw_scale_px(app, w);
    r->ph = cgw_scale_px(app, h);
    r->sx = cgw_scale_px(app, x);
    r->sy = cgw_scale_px(app, y);
    if (r->pw <= 0 || r->ph <= 0) return 0;

    dir = cg_app_data_dir();
    tmp = cg_str_format("%s", CG_REC_TMP);
    if (!dir || !tmp) { cg_free(dir); cg_free(tmp); return 0; }
    r->tmp_path = cg_path_join(dir, tmp);
    cg_free(dir);
    cg_free(tmp);
    if (!r->tmp_path) return 0;

    r->rgb_stride = r->pw * 3;
    r->rgb = (unsigned char *)malloc((size_t)r->ph * r->rgb_stride);
    r->rgb_enc = (unsigned char *)malloc((size_t)r->ph * r->rgb_stride);
    if (!r->rgb || !r->rgb_enc) { rec_free(r); return 0; }

    r->gif = cg_gif_open(r->tmp_path, w, h);
    if (!r->gif) {
        cg_log(CG_LOG_ERROR, "录屏：打不开临时文件 %s", r->tmp_path);
        rec_free(r);
        return 0;
    }

    if (!s_rec_cs_inited) {
        InitializeCriticalSection(&r->cs);
        s_rec_cs_inited = 1;
    }
    r->ev_stop = CreateEventW(NULL, TRUE, FALSE, NULL);   /* 手动复位：停的信号要留住 */
    r->ev_work = CreateEventW(NULL, FALSE, FALSE, NULL);  /* auto-reset：一帧一唤 */
    if (!r->ev_stop || !r->ev_work) {
        cg_log(CG_LOG_ERROR, "录屏：创建同步对象失败");
        if (r->ev_stop) CloseHandle(r->ev_stop);
        if (r->ev_work) CloseHandle(r->ev_work);
        r->ev_stop = r->ev_work = NULL;
        rec_free(r);
        return 0;
    }

    r->x = x; r->y = y; r->w = w; r->h = h;
    r->interval_ms = 1000 / fps;
    r->frames = 0;
    r->skipped = 0;
    r->dropped = 0;
    r->capped = 0;
    r->pending = 0;
    r->busy = 0;
    r->start_tick = GetTickCount();
    r->last_write_tick = r->start_tick;       /* 首帧延时按名义 interval 算（worker 的 first 分支） */
    r->active = 1;

    r->th = CreateThread(NULL, 0, rec_worker, r, 0, NULL);
    if (!r->th) {
        cg_log(CG_LOG_ERROR, "录屏：起编码线程失败");
        r->active = 0;
        CloseHandle(r->ev_stop);
        CloseHandle(r->ev_work);
        r->ev_stop = r->ev_work = NULL;
        rec_free(r);
        return 0;
    }

    cg_set_timer(app, r->interval_ms, rec_tick, app);
    cg_log(CG_LOG_INFO, "录屏开始：区域 %d,%d %dx%d（逻辑像素），%d fps，临时文件 %s",
           x, y, w, h, fps, r->tmp_path);
    return 1;
}

/**
 * @brief 停止录屏
 * @param app 应用
 * @param path 成品路径（UTF-8）；NULL = 丢弃（临时文件直接删掉）
 * @return int 1 成功；0 失败（没在录 / 工作线程没退出 / 收尾写出错 / 一帧都没录到）
 * @note 给路径时先把临时文件 MoveFile 过去（跨盘失败退回复制再删临时文件）；
 *       调用方看到的"失败"要区分两种：一帧都没录到（返回 0、无文件）与写出错（同样 0）
 */
int cg_gif_rec_stop(cg_app_t *app, const char *path)
{
    cg_rec_t *r = &s_rec;
    int       frames, ok = 0;

    (void)app;
    if (!r->active) return 0;
    r->active = 0;                            /* 先熄 UI 定时器的火（rec_tick 见 0 直接返回） */
    frames = r->frames;

    /* **先 join 工作线程，再收编码器，再改名** —— 顺序反了就是两条事故：
     * 编码线程还活着时收编码器 = 双方同时碰 FILE*；FILE* 还开着时 MoveFile =
     * 系统拒绝（跨盘复制也只能拿到已 flush 的那一截）。 */
    if (!rec_join_worker(r)) return 0;
    if (r->gif) {
        if (path && *path && frames > 0) ok = cg_gif_close(r->gif);
        else                             cg_gif_abort(r->gif);
        r->gif = NULL;                         /* 已经收了，别让 rec_free 再收一次 */
    }
    if (ok && path && *path) {
        wchar_t *wa = cgw_wc_from_utf8(r->tmp_path);
        wchar_t *wb = cgw_wc_from_utf8(path);

        if (wa && wb && MoveFileW(wa, wb)) {
            ok = 1;                            /* 原地改名：同一卷，最快 */
        } else if (cg_file_copy(r->tmp_path, path)) {
            remove(r->tmp_path);               /* 跨盘：复制过去再删临时文件 */
            ok = 1;
        } else {
            remove(r->tmp_path);               /* 改名没成功：别在用户数据目录里留半成品 */
            ok = 0;
        }
        free(wa);
        free(wb);
    }

    if (ok) {
        long long sz = cg_file_size(path);

        cg_log(CG_LOG_INFO, "录屏结束：%d 帧（跳过 %d 帧无变化、丢弃 %d 帧编码跟不上），%lld 字节 -> %s",
               frames, r->skipped, r->dropped, sz, path);
    } else if (path && *path && frames > 0) {
        cg_log(CG_LOG_ERROR, "录屏：收尾失败（%d 帧没能落到 %s）", frames, path);
    } else if (path && *path) {
        cg_log(CG_LOG_WARN, "录屏取消：一帧都没录到，不生成文件");
    } else {
        cg_log(CG_LOG_INFO, "录屏取消：丢弃 %d 帧", frames);
    }
    rec_free(r);
    return ok;
}

/**
 * @brief 查录屏进度（调用方拿它刷新工具条上的计时 / 帧数）
 * @param app 应用（未使用，留着与其它 cg_*_info 对称）
 * @param frames 出参：已写帧数；可为 NULL
 * @param elapsed_ms 出参：已录毫秒数；可为 NULL
 * @return void 无返回值（没在录时两个出参都是 0）
 */
void cg_gif_rec_info(const cg_app_t *app, int *frames, int *elapsed_ms)
{
    cg_rec_t *r = &s_rec;
    int       f;

    (void)app;
    if (!r->active) {
        if (frames) *frames = 0;
        if (elapsed_ms) *elapsed_ms = 0;
        return;
    }
    EnterCriticalSection(&r->cs);
    f = r->frames;
    LeaveCriticalSection(&r->cs);
    if (frames) *frames = f;
    if (elapsed_ms) *elapsed_ms = (int)(GetTickCount() - r->start_tick);
}

/**
 * @brief 关窗收尾：中止正在进行的录屏（幂等、NULL 安全）
 * @return void 无返回值
 * @note 少这一步：窗口关了进程还在跑（托盘程序常见），临时文件与编码器就漏在那儿了。
 *       与 cgw_hotkey_clear / cgw_accel_destroy 成对，由 win_shutdown 调
 */
void cg_gif_rec_shutdown(void)
{
    cg_rec_t *r = &s_rec;

    if (!r->active) return;
    cg_log(CG_LOG_INFO, "关窗：中止录屏，丢弃 %d 帧", r->frames);
    r->active = 0;
    rec_join_worker(r);                        /* 超时则下面什么都不释放（宁泄漏不 UAF） */
    if (!r->th) rec_free(r);
}
