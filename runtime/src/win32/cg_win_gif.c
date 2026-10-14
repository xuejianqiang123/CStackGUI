/**
 * @file    cg_win_gif.c
 * @brief   GIF 录屏：定时抓一块屏幕区域，逐帧喂给 kit 的 GIF 编码器
 * @details 与 cg_win_shot.c 的分工：那个管"抓一张"（窗口截图 / 区域截图 / 剪贴板图片），
 *          这个管"抓一串"。编码本身在 kit 层（cg_kit_gif.c，纯 C 可单测），这里只做
 *          Win32 那一半：**定时** + **抓帧** + **收尾落盘**。
 *
 *          三个实现上的选择：
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
 *          **三、帧延时按实际经过时间算，不是名义 1/fps。** 抓帧 + LZW 是要花时间的
 *          （一帧几十毫秒），实际帧率一定低于名义值；GIF 里写名义值会让录出来的动画
 *          比现场快。另外"整帧没变被跳过"时，延时要从**上一帧真正写出的时刻**算起，
 *          否则跳过几帧之后画面变化会被压成一瞬间。
 *
 *          录制状态是文件级静态量：一个进程一条录屏（和 cg_win_window.c 的热键表同一套路）。
 *          不开新的工作线程 —— 帧全在消息循环的定时器回调里抓（check-layers 规则 8）。
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

/** 录屏状态（一个进程一条） */
typedef struct {
    int        active;        /**< 1 = 正在录 */
    int        x, y, w, h;    /**< 区域（**逻辑像素**，与 .cgui / 事件坐标同一套） */
    int        interval_ms;   /**< 定时器间隔（名义 1/fps） */
    cg_gif_t  *gif;           /**< 编码器 */
    char      *tmp_path;      /**< 临时文件路径（UTF-8 堆串） */
    int        frames;        /**< 已写帧数 */
    int        skipped;       /**< 因没变化而跳过的帧数 */
    unsigned   start_tick;    /**< 开始时刻（GetTickCount） */
    unsigned   last_tick;     /**< 上一帧**写出**的时刻 */
    unsigned char *rgb;       /**< 抓帧缓冲（已从 DIB 的 BGR 换成 RGB） */
    int        rgb_stride;    /**< 抓帧缓冲每行字节数 */
} cg_rec_t;

static cg_rec_t s_rec;

/**
 * @brief 抓一帧屏幕区域到 RGB 缓冲
 * @param r 录屏状态
 * @param app 应用（取 DPI 缩放比）
 * @return int 1 成功；0 失败（取 DC / 建 DIB / BitBlt 任一环节）
 * @note DIB 是 BGR 序、kit 的编码器收 RGB 序，这里顺手换一面 —— 多一次逐像素遍历，
 *       但换来 kit 层与单测的口径统一（测试里直接给 RGB）
 */
static int rec_capture(cg_rec_t *r, const cg_app_t *app)
{
    HDC        scr, mem;
    BITMAPINFO bi;
    HBITMAP    bmp, old;
    void      *bits = NULL;
    int        pw, ph, sx, sy, stride, y, xx;

    pw = cgw_scale_px(app, r->w);
    ph = cgw_scale_px(app, r->h);
    sx = cgw_scale_px(app, r->x);
    sy = cgw_scale_px(app, r->y);
    if (pw <= 0 || ph <= 0) return 0;

    scr = GetDC(NULL);                        /* NULL = 整个屏幕的 DC（覆盖虚拟桌面） */
    if (!scr) return 0;
    mem = CreateCompatibleDC(scr);
    if (!mem) { ReleaseDC(NULL, scr); return 0; }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = pw;
    bi.bmiHeader.biHeight      = -ph;          /* 负 = 自上而下 */
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
    if (!BitBlt(mem, 0, 0, pw, ph, scr, sx, sy, SRCCOPY)) {
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, scr);
        return 0;
    }

    stride = (pw * 3 + 3) & ~3;               /* 24 位 DIB 行按 4 字节对齐 */
    for (y = 0; y < ph; y++) {
        const unsigned char *s = (const unsigned char *)bits + (size_t)y * stride;
        unsigned char       *d = r->rgb + (size_t)y * r->rgb_stride;

        for (xx = 0; xx < pw; xx++) {
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
 * @brief 定时器回调：抓一帧、喂编码器、再挂下一次
 * @param user cg_app_t*
 * @return void 无返回值
 * @note 抓帧或编码失败就地停止录制并**丢弃**（半套帧没意思，别留下个打不开的文件）；
 *       已经出过帧的话至少日志里说一声录到第几帧
 */
static void rec_tick(void *user)
{
    cg_app_t  *app = (cg_app_t *)user;
    cg_rec_t  *r = &s_rec;
    unsigned   now;
    int        delay_ms, rc;

    if (!app || !r->active) return;

    now = GetTickCount();
    delay_ms = (r->frames + r->skipped == 0) ? r->interval_ms : (int)(now - r->last_tick);
    if (delay_ms < 20) delay_ms = 20;          /* 太快了看图软件会当成 0 延时 */

    if (!rec_capture(r, app)) {
        cg_log(CG_LOG_ERROR, "录屏：抓第 %d 帧失败，停止并丢弃", r->frames + 1);
        cg_gif_rec_stop(app, NULL);
        return;
    }
    rc = cg_gif_add(r->gif, r->rgb, r->rgb_stride, delay_ms);
    if (rc == 0) {
        cg_log(CG_LOG_ERROR, "录屏：编码第 %d 帧失败，停止并丢弃", r->frames + 1);
        cg_gif_rec_stop(app, NULL);
        return;
    }
    if (rc == 2) {
        r->skipped++;
    } else {
        r->frames++;
        r->last_tick = now;                    /* 跳过不计：延时从真正写出的时刻算 */
    }
    cg_set_timer(app, r->interval_ms, rec_tick, app);
}

/**
 * @brief 释放录屏占用的资源（不删临时文件：那是 stop 的活）
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
    r->rgb_stride = 0;
    r->active = 0;
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
 * @return int 1 成功；0 失败（没窗口 / 已在录 / 尺寸非法 / 打不开临时文件）
 * @note 录屏期间画面变化靠调用方自己的界面反馈（计时 / 帧数），本函数不弹任何东西
 */
int cg_gif_rec_start(cg_app_t *app, int x, int y, int w, int h, int fps)
{
    cg_rec_t *r = &s_rec;
    char     *dir, *tmp;
    int       pw, ph;

    if (!app || !cg_app_has_window(app)) return 0;
    if (r->active) return 0;
    if (w <= 0 || h <= 0 || w > CG_REC_DIM_MAX || h > CG_REC_DIM_MAX) return 0;
    if (fps < CG_REC_FPS_MIN) fps = CG_REC_FPS_MIN;
    if (fps > CG_REC_FPS_MAX) fps = CG_REC_FPS_MAX;

    pw = cgw_scale_px(app, w);
    ph = cgw_scale_px(app, h);
    if (pw <= 0 || ph <= 0) return 0;

    dir = cg_app_data_dir();
    tmp = cg_str_format("%s", CG_REC_TMP);
    if (!dir || !tmp) { cg_free(dir); cg_free(tmp); return 0; }
    r->tmp_path = cg_path_join(dir, tmp);
    cg_free(dir);
    cg_free(tmp);
    if (!r->tmp_path) return 0;

    r->rgb_stride = pw * 3;
    r->rgb = (unsigned char *)malloc((size_t)ph * r->rgb_stride);
    if (!r->rgb) { rec_free(r); return 0; }

    r->gif = cg_gif_open(r->tmp_path, w, h);
    if (!r->gif) {
        cg_log(CG_LOG_ERROR, "录屏：打不开临时文件 %s", r->tmp_path);
        rec_free(r);
        return 0;
    }

    r->x = x; r->y = y; r->w = w; r->h = h;
    r->interval_ms = 1000 / fps;
    r->frames = 0;
    r->skipped = 0;
    r->start_tick = GetTickCount();
    r->last_tick = r->start_tick;
    r->active = 1;

    cg_set_timer(app, r->interval_ms, rec_tick, app);
    cg_log(CG_LOG_INFO, "录屏开始：区域 %d,%d %dx%d（逻辑像素），%d fps，临时文件 %s",
           x, y, w, h, fps, r->tmp_path);
    return 1;
}

/**
 * @brief 停止录屏
 * @param app 应用
 * @param path 成品路径（UTF-8）；NULL = 丢弃（临时文件直接删掉）
 * @return int 1 成功；0 失败（没在录 / 收尾写出错 / 一帧都没录到）
 * @note 给路径时先把临时文件 MoveFile 过去（跨盘失败退回复制再删临时文件）；
 *       调用方看到的"失败"要区分两种：一帧都没录到（返回 0、无文件）与写出错（同样 0）
 */
int cg_gif_rec_stop(cg_app_t *app, const char *path)
{
    cg_rec_t *r = &s_rec;
    int       frames, ok = 0;

    (void)app;
    if (!r->active) return 0;
    frames = r->frames;

    /* **先收编码器，再改名** —— 顺序反了就会丢尾巴：编码器的 FILE* 还开着时
     * MoveFile 会被系统拒绝，而就算跨盘复制能读走，拿到的也只是已 flush 的那一截
     * （实测：报"收尾失败"、目标文件只有前 4096 字节、GDI+ 读出来帧数少了一大半）。
     * cg_gif_close 负责 flush + 写 trailer + fclose；cg_gif_abort 负责关文件并删掉。 */
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

        cg_log(CG_LOG_INFO, "录屏结束：%d 帧（跳过 %d 帧无变化），%lld 字节 -> %s",
               frames, r->skipped, sz, path);
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

    (void)app;
    if (frames) *frames = r->active ? r->frames : 0;
    if (elapsed_ms) *elapsed_ms = r->active ? (int)(GetTickCount() - r->start_tick) : 0;
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
    rec_free(r);
}
