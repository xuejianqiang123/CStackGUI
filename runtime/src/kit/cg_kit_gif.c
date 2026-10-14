/**
 * @file    cg_kit_gif.c
 * @brief   GIF 动画编码：把一串 24 位 RGB 帧写成 GIF89a（LZW + 固定调色板 + 脏矩形）
 * @details 为什么自己写而不用 GDI+ 的 GIF 编码器：GDI+ 存多帧 GIF 靠 SaveAdd，
 *          调色板沿用**第一帧**的量化结果，后面帧内容一变就一片糊；而且它的行为随
 *          Windows 版本漂，没法写进单测。这里手写一个 ~500 行的编码器，换来三样东西：
 *            1. **可单测**：纯字节进出，测试里合成几帧、写盘、再用 GDI+ 当独立解码器
 *               读回来逐像素比（编码器自己说"对"不算对）；
 *            2. **脏矩形优化**：只编码与上一帧不同的包围盒，框内没变的像素走透明索引
 *               —— 录屏录的是一块基本不动的界面，这一条决定文件是 2MB 还是 50MB；
 *            3. **零新增链接依赖**（只用 stdio），既有用户的链接命令不用改。
 *
 *          三个刻意的取舍，知道了才不会误判：
 *          - **调色板是固定的**：216 网页色（6x6x6）+ 39 级灰阶 + 1 个透明索引 = 256。
 *            不按内容做量化（八叉树/中位切分）—— 屏幕录屏的内容以 UI 纯色与文字为主，
 *            固定色表对它几乎无损；照片类内容会有色带，那是已知边界。
 *          - **就近色查表**：输入先按 5-5-5 归到 32768 个桶，每个桶只算一次就近色
 *            （桶内共用结果）。每像素 256 次距离运算在 1080p 上是一秒几十毫秒，不能那么干。
 *          - **整帧没变就跳过**：不写这一帧（GIF 时长会短一点，画面不变就不占地方）。
 *
 *          GIF 字节流的模样见下面各写函数的注释；LZW 的码宽增长点必须与解码器严格对齐
 *          （差一位整段就废），实现里注明了推导。
 */
#include "cg_kit_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================ 内部常量与结构 */

/** 网页色 6x6x6 = 216 个（调色板 0..215） */
#define CG_GIF_WEB   216
/** 灰阶层数（调色板 216..254） */
#define CG_GIF_GRAY  39
/** 透明索引：脏矩形优化里"这一像素没变"用它，让上一帧透出来 */
#define CG_GIF_TRANS 255
/** 哈希字典槽位数（2 的幂；LZW 码上限 4096，装得下） */
#define CG_GIF_HASH  8192
/** LZW 最大码值（GIF 规定 12 位码） */
#define CG_GIF_LZW_MAX 4096
/** LZW 最大码宽 */
#define CG_GIF_CODE_MAX 12
/** GIF 尺寸上限（图像描述符里是 u16） */
#define CG_GIF_DIM_MAX 65535

/** 编码器状态：不透明给业务，定义收在本文件 */
struct cg_gif {
    FILE    *fp;                 /**< 输出文件（增量写，内存不随帧数涨） */
    char    *path;               /**< 输出路径（放弃时要删文件） */
    int      width, height;      /**< 画布尺寸（逻辑像素口径由调用方定） */
    int      frames;             /**< 已写帧数 */
    int      skipped;            /**< 因"整帧没变"而跳过的帧数 */
    unsigned char pal[256][3];   /**< 全局调色板 */
    unsigned char cache[32768];  /**< 5-5-5 桶 -> 调色板索引；0xFF = 还没算过 */
    unsigned char *prev;         /**< 上一帧的索引图（脏矩形比较用） */
    unsigned char *cur;          /**< 当前帧的索引图 */
    /* LZW 编码状态 */
    unsigned acc;                /**< 位累加器（LSB 优先） */
    int      nbits;              /**< 累加器里待出的位数 */
    int      code_size;          /**< 当前码宽（位） */
    int      clear_code;         /**< 清表码 */
    int      eoi_code;           /**< 数据结束码 */
    int      next_code;          /**< 下一个可分配的码 */
    int      max_code;           /**< 当前码宽能表示的最大码 +1 */
    int     *keys;               /**< 字典键（存 key+1，0 = 空槽） */
    int     *vals;               /**< 字典值（码） */
    unsigned char sub[255];      /**< LZW 子块缓冲（GIF 子块最长 255） */
    int      sub_len;            /**< 子块里已攒的字节数 */
};

/* ============================================================ 小工具 */

/**
 * @brief 写一个 little-endian u16（GIF 的所有多字节字段都是小端）
 * @param p 输出缓冲（至少 2 字节）
 * @param v 待写值（只取低 16 位）
 * @return void 无返回值
 */
static void put_u16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

/**
 * @brief 原样写一段字节（文件头 / 色表 / 各种扩展用；不走子块缓冲）
 * @param g 编码器
 * @param p 数据
 * @param n 字节数
 * @return int 1 成功；0 写失败
 */
static int gif_raw(cg_gif_t *g, const void *p, int n)
{
    return (n <= 0) || (fwrite(p, 1, (size_t)n, g->fp) == (size_t)n);
}

/**
 * @brief 往当前 LZW 子块里塞一个字节，满 255 就整块冲给文件
 * @param g 编码器
 * @param b 字节
 * @return int 1 成功；0 写失败
 */
static int gif_byte(cg_gif_t *g, unsigned char b)
{
    g->sub[g->sub_len++] = b;
    if (g->sub_len < 255) return 1;
    if (fputc(255, g->fp) == EOF) return 0;
    if (fwrite(g->sub, 1, 255, g->fp) != 255) return 0;
    g->sub_len = 0;
    return 1;
}

/**
 * @brief 建固定调色板：216 网页色 + 39 灰阶 + 透明位
 * @param pal 输出（256 x 3 字节 RGB）
 * @return void 无返回值
 */
static void pal_init(unsigned char pal[256][3])
{
    static const unsigned char lv[6] = { 0, 51, 102, 153, 204, 255 };
    int i, r, gc, b, n = 0;

    for (r = 0; r < 6; r++)
        for (gc = 0; gc < 6; gc++)
            for (b = 0; b < 6; b++, n++) {
                pal[n][0] = lv[r];
                pal[n][1] = lv[gc];
                pal[n][2] = lv[b];
            }
    /* n 现在正好 216；灰阶按 0..255 均分 39 档（四舍五入） */
    for (i = 0; i < CG_GIF_GRAY; i++, n++) {
        unsigned char v = (unsigned char)((i * 255 + CG_GIF_GRAY / 2) / (CG_GIF_GRAY - 1));

        pal[n][0] = pal[n][1] = pal[n][2] = v;
    }
    /* n == 255：透明索引，颜色值无所谓（解码器不会拿它当颜色用） */
    pal[255][0] = pal[255][1] = pal[255][2] = 0;
}

/**
 * @brief 就近色查询：5-5-5 桶 -> 调色板索引（带缓存，透明索引不参与）
 * @param g 编码器（用它的调色板与缓存）
 * @param r 红 0..255
 * @param gc 绿 0..255
 * @param b 蓝 0..255
 * @return unsigned char 调色板索引（0..254，永远不是 255）
 * @note 同一个 5-5-5 桶里的颜色共用一次搜索结果 —— 桶内最多差 7，而调色板本身
 *       就是 51 一档的粗粒度，这点误差看不出来；换来的是每像素 O(1)。
 */
static unsigned char pal_lookup(cg_gif_t *g, unsigned char r, unsigned char gc, unsigned char b)
{
    int       idx = ((int)(r >> 3) << 10) | ((int)(gc >> 3) << 5) | (int)(b >> 3);
    unsigned char hit = g->cache[idx];

    if (hit != 0xFF) return hit;
    {
        int       best = 0;
        long long bd   = -1;
        int       i;

        for (i = 0; i < 255; i++) {              /* 255 是透明位，跳过 */
            long long dr = (long long)r - g->pal[i][0];
            long long dg = (long long)gc - g->pal[i][1];
            long long db = (long long)b - g->pal[i][2];
            long long d  = dr * dr + dg * dg + db * db;

            if (bd < 0 || d < bd) { bd = d; best = i; }
        }
        g->cache[idx] = (unsigned char)best;
        return (unsigned char)best;
    }
}

/* ============================================================ LZW */

/**
 * @brief 字典键的哈希（乘法哈希取高位，低位那几位分布太差）
 * @param key 键 = (前缀码 << 8) | 像素
 * @return unsigned 槽位（0..CG_GIF_HASH-1）
 */
static unsigned gif_hash(int key)
{
    return ((unsigned)key * 2654435761u) >> (32 - 13);
}

/**
 * @brief 在字典里查 (前缀, 像素) 对应的码
 * @param g 编码器
 * @param key 键
 * @return int 码值；-1 表示不在表里
 */
static int dict_find(const cg_gif_t *g, int key)
{
    unsigned h = gif_hash(key);

    for (;;) {
        int k = g->keys[h];

        if (k == 0) return -1;                    /* 空槽：查无此项 */
        if (k == key + 1) return g->vals[h];
        h = (h + 1) & (CG_GIF_HASH - 1);
    }
}

/**
 * @brief 往字典里插一条 (前缀, 像素) -> 码
 * @param g 编码器
 * @param key 键
 * @param code 码值
 * @return void 无返回值（同键已存在就直接返回，不覆盖）
 */
static void dict_add(cg_gif_t *g, int key, int code)
{
    unsigned h = gif_hash(key);

    for (;;) {
        if (g->keys[h] == 0) { g->keys[h] = key + 1; g->vals[h] = code; return; }
        if (g->keys[h] == key + 1) return;
        h = (h + 1) & (CG_GIF_HASH - 1);
    }
}

/**
 * @brief 重置 LZW：清字典、码宽回到最小、位累加器归零
 * @param g 编码器
 * @return void 无返回值
 * @note 256 色 => 最小码长 8 => 起始码宽 9；下一个可分配码 = EOI+1 = 258
 */
static void lzw_reset(cg_gif_t *g)
{
    memset(g->keys, 0, sizeof(int) * CG_GIF_HASH);
    g->code_size = 9;
    g->clear_code = 256;
    g->eoi_code = 257;
    g->next_code = 258;
    g->max_code = 1 << g->code_size;              /* 512 */
    g->acc = 0;
    g->nbits = 0;
}

/**
 * @brief 按当前码宽吐一个码（LSB 优先拼进累加器，满 8 位就出一个字节）
 * @param g 编码器
 * @param code 码值（必须小于 1 << code_size）
 * @return int 1 成功；0 写失败
 */
static int lzw_emit(cg_gif_t *g, int code)
{
    g->acc |= (unsigned)code << g->nbits;
    g->nbits += g->code_size;
    while (g->nbits >= 8) {
        if (!gif_byte(g, (unsigned char)(g->acc & 0xFFu))) return 0;
        g->acc >>= 8;
        g->nbits -= 8;
    }
    return 1;
}

/**
 * @brief 把一段索引像素做 LZW 压缩，子块写进文件
 * @param g 编码器
 * @param pix 索引像素（0..254）
 * @param npix 像素个数
 * @return int 1 成功；0 写失败
 * @note **码宽增长点**：每吐一个码、插一条字典项，然后判
 *       `next_code > max_code` 就加一位（封顶 12）。这个">"不是">="——
 *       解码器天生比编码器落后一个表项（它要读到下一个码才知道该建什么串），
 *       两边用同一个切换点才会对齐：编码器在插完第 512 项之后、发第 256 个码之前加位，
 *       解码器也在建到第 512 项时加位。差一位整段图像就废了，GDI+ 会直接拒读。
 *       表满（4096）时先发一个清表码再重来，否则后面加不进新串、压缩率崩塌。
 */
static int lzw_compress(cg_gif_t *g, const unsigned char *pix, int npix)
{
    int prefix, i;

    if (!lzw_emit(g, g->clear_code)) return 0;
    if (npix <= 0) return lzw_emit(g, g->eoi_code);

    prefix = pix[0];
    for (i = 1; i < npix; i++) {
        int k   = pix[i];
        int key = (prefix << 8) | k;
        int hit = dict_find(g, key);

        if (hit >= 0) { prefix = hit; continue; }

        if (!lzw_emit(g, prefix)) return 0;
        if (g->next_code >= CG_GIF_LZW_MAX) {
            if (!lzw_emit(g, g->clear_code)) return 0;
            lzw_reset(g);
        } else {
            dict_add(g, key, g->next_code);
            g->next_code++;
            if (g->next_code > g->max_code && g->code_size < CG_GIF_CODE_MAX) {
                g->code_size++;
                g->max_code <<= 1;
            }
        }
        prefix = k;
    }
    if (!lzw_emit(g, prefix)) return 0;
    return lzw_emit(g, g->eoi_code);
}

/* ============================================================ 帧写出 */

/**
 * @brief 写一帧：图形控制扩展 + 图像描述符 + LZW 数据
 * @param g 编码器
 * @param left 帧左上角在画布里的横坐标（脏矩形用）
 * @param top 帧左上角纵坐标
 * @param w 帧宽
 * @param h 帧高
 * @param pix 帧的索引像素（w*h 个，0..254 或 255 表示透明）
 * @param delay_cs 帧延时（单位 1/100 秒）
 * @return int 1 成功；0 写失败
 * @note 处置方法固定取 1（留在画布上）：这是脏矩形优化的前提 —— 上一帧的画面要留着，
 *       这一帧只补变化的那块，框内没变的像素用透明索引让上一帧透出来。
 */
static int write_frame(cg_gif_t *g, int left, int top, int w, int h,
                       const unsigned char *pix, int delay_cs)
{
    unsigned char gce[8];
    unsigned char desc[10];                       /* 0x2C + 4×u16 + packed */
    unsigned char mincode = 8;

    /* 图形控制扩展：disposal=1（留在画布上）+ 透明标志，透明索引 255 */
    gce[0] = 0x21; gce[1] = 0xF9; gce[2] = 0x04;
    gce[3] = (unsigned char)((1 << 2) | 0x80);
    gce[4] = (unsigned char)(delay_cs & 0xFF);
    gce[5] = (unsigned char)((delay_cs >> 8) & 0xFF);
    gce[6] = CG_GIF_TRANS;
    gce[7] = 0x00;
    if (!gif_raw(g, gce, 8)) return 0;

    /* 图像描述符（共 10 字节）：0x2C + left + top + w + h 各两个字节 + packed。
     * packed 在**第 10 字节**（下标 9）—— 曾经把它写到 desc[8]，那正好是高的高字节，
     * 于是描述符短了一字节、后面的 LZW 最小码长被解码器当成 packed 读，整个文件报废。
     * 无本地色表、不隔行（隔行只影响观感，录屏不需要）。 */
    desc[0] = 0x2C;
    put_u16(desc + 1, left);
    put_u16(desc + 3, top);
    put_u16(desc + 5, w);
    put_u16(desc + 7, h);
    desc[9] = 0x00;
    if (!gif_raw(g, desc, 10)) return 0;

    if (!gif_raw(g, &mincode, 1)) return 0;
    lzw_reset(g);
    if (!lzw_compress(g, pix, w * h)) return 0;

    /* 收尾：把累加器里不足一个字节的残余补成整字节、冲掉半块、写块结束符 */
    if (g->nbits > 0) {
        if (!gif_byte(g, (unsigned char)(g->acc & 0xFFu))) return 0;
        g->acc = 0;
        g->nbits = 0;
    }
    if (g->sub_len > 0) {
        if (fputc(g->sub_len, g->fp) == EOF) return 0;
        if (fwrite(g->sub, 1, (size_t)g->sub_len, g->fp) != (size_t)g->sub_len) return 0;
        g->sub_len = 0;
    }
    return fputc(0, g->fp) != EOF;                 /* 0 = 数据块结束 */
}

/* ============================================================ 公共 API */

/**
 * @brief 打开一个 GIF 准备写（头 + 逻辑屏描述 + 全局色表 + 循环扩展）
 * @param path 输出路径（UTF-8；目录必须已存在）
 * @param w 画布宽（1..65535）
 * @param h 画布高（1..65535）
 * @return cg_gif_t* 编码器；失败返回 NULL（参数非法 / 打不开文件 / 内存不足）
 * @note 循环次数固定为 0 = 永远循环（录屏产物就该这么放）
 */
cg_gif_t *cg_gif_open(const char *path, int w, int h)
{
    static const unsigned char k_loop[19] = {
        0x21, 0xFF, 0x0B,
        'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
        0x03, 0x01, 0x00, 0x00, 0x00
    };
    cg_gif_t      *g;
    unsigned char  hdr[13];
    unsigned char  ct[256 * 3];
    int            i;
    size_t         n;

    if (!path || !*path) return NULL;
    if (w <= 0 || h <= 0 || w > CG_GIF_DIM_MAX || h > CG_GIF_DIM_MAX) return NULL;

    g = (cg_gif_t *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->path = strdup(path);
    g->keys = (int *)malloc(sizeof(int) * CG_GIF_HASH);
    g->vals = (int *)malloc(sizeof(int) * CG_GIF_HASH);
    n = (size_t)w * (size_t)h;
    g->prev = (unsigned char *)malloc(n);
    g->cur = (unsigned char *)malloc(n);
    if (!g->path || !g->keys || !g->vals || !g->prev || !g->cur) {
        cg_gif_abort(g);
        return NULL;
    }
    g->fp = fopen(path, "wb");
    if (!g->fp) { cg_gif_abort(g); return NULL; }

    g->width = w;
    g->height = h;
    memset(g->prev, 0xFF, n);               /* 首帧全部算"变了"（真实索引到不了 255） */
    memset(g->cache, 0xFF, sizeof(g->cache));
    pal_init(g->pal);

    memcpy(hdr, "GIF89a", 6);
    put_u16(hdr + 6, w);
    put_u16(hdr + 8, h);
    hdr[10] = 0xF7;                          /* 有全局色表 + 8 位色深 + 256 项 */
    hdr[11] = 0;                             /* 背景色索引 */
    hdr[12] = 0;                             /* 像素宽高比 */
    if (!gif_raw(g, hdr, 13)) { cg_gif_abort(g); return NULL; }

    for (i = 0; i < 256; i++) {
        ct[i * 3 + 0] = g->pal[i][0];
        ct[i * 3 + 1] = g->pal[i][1];
        ct[i * 3 + 2] = g->pal[i][2];
    }
    if (!gif_raw(g, ct, sizeof(ct))) { cg_gif_abort(g); return NULL; }
    if (!gif_raw(g, k_loop, sizeof(k_loop))) { cg_gif_abort(g); return NULL; }
    return g;
}

/**
 * @brief 追加一帧（24 位 RGB，自上而下）
 * @param g 编码器
 * @param rgb 像素缓冲（w*h 个像素，每像素 R,G,B 三字节）
 * @param stride 每行字节数；<= 0 按 w*3 算（允许调用方给带填充的行）
 * @param delay_ms 这一帧的延时（毫秒）；内部换成 1/100 秒，最小按 1 记
 * @return int 1 = 帧已写入；2 = 整帧与上一帧相同、已跳过；0 = 失败
 * @note 只编码**变化的那一块**（脏矩形）：框内没变的像素写透明索引，靠上一帧透出来；
 *       整帧没变则直接跳过（GIF 总时长短一点，画面不变就不占地方）。
 */
int cg_gif_add(cg_gif_t *g, const void *rgb, int stride, int delay_ms)
{
    int          x, y, rowbytes, delay_cs;
    int          minx, miny, maxx, maxy, rw, rh, ok;
    unsigned char *tile;
    const unsigned char *src = (const unsigned char *)rgb;

    if (!g || !g->fp || !rgb) return 0;
    if (delay_ms < 0) delay_ms = 0;
    delay_cs = (delay_ms + 5) / 10;
    if (delay_cs < 1) delay_cs = 1;
    rowbytes = (stride > 0) ? stride : g->width * 3;

    /* 量化：8 位 RGB -> 调色板索引 */
    for (y = 0; y < g->height; y++) {
        const unsigned char *row = src + (size_t)y * rowbytes;
        unsigned char       *dst = g->cur + (size_t)y * g->width;

        for (x = 0; x < g->width; x++)
            dst[x] = pal_lookup(g, row[x * 3 + 0], row[x * 3 + 1], row[x * 3 + 2]);
    }

    /* 与上一帧比，找变化的包围盒（逐行 memcmp + 行内首尾定位，比逐像素 min/max 快） */
    minx = g->width; miny = g->height; maxx = -1; maxy = -1;
    for (y = 0; y < g->height; y++) {
        const unsigned char *c = g->cur + (size_t)y * g->width;
        const unsigned char *p = g->prev + (size_t)y * g->width;
        int fx, lx;

        if (memcmp(c, p, (size_t)g->width) == 0) continue;
        fx = 0;
        lx = g->width - 1;
        while (c[fx] == p[fx]) fx++;
        while (c[lx] == p[lx]) lx--;
        if (fx < minx) minx = fx;
        if (lx > maxx) maxx = lx;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    if (maxx < 0) { g->skipped++; return 2; }

    rw = maxx - minx + 1;
    rh = maxy - miny + 1;
    tile = (unsigned char *)malloc((size_t)rw * rh);
    if (!tile) return 0;

    for (y = miny; y <= maxy; y++) {
        unsigned char       *d = tile + (size_t)(y - miny) * rw;
        const unsigned char *c = g->cur + (size_t)y * g->width;
        const unsigned char *p = g->prev + (size_t)y * g->width;

        for (x = 0; x < rw; x++)
            d[x] = (c[minx + x] == p[minx + x]) ? CG_GIF_TRANS : c[minx + x];
    }
    ok = write_frame(g, minx, miny, rw, rh, tile, delay_cs);
    free(tile);
    if (!ok) return 0;

    memcpy(g->prev, g->cur, (size_t)g->width * g->height);
    g->frames++;
    return 1;
}

/**
 * @brief 收尾：写 trailer、关文件、释放编码器
 * @param g 编码器（释放后不再可用）
 * @return int 1 成功；0 失败（写尾 / 关文件出错，或**一帧都没写**）
 * @note 一帧都没写时算失败并删掉文件 —— 空 GIF 对调用方没用，还会留下个 13 字节的垃圾
 */
int cg_gif_close(cg_gif_t *g)
{
    int ok;

    if (!g) return 0;
    ok = (g->frames > 0) && (fputc(0x3B, g->fp) != EOF);
    if (fclose(g->fp) != 0) ok = 0;
    g->fp = NULL;
    if (!ok) {
        cg_gif_abort(g);                       /* 内部会释放结构 */
        return 0;
    }
    free(g->path);
    free(g->keys);
    free(g->vals);
    free(g->prev);
    free(g->cur);
    free(g);
    return 1;
}

/**
 * @brief 放弃这次编码：关文件、删掉半成品、释放编码器
 * @param g 编码器（释放后不再可用）；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_gif_abort(cg_gif_t *g)
{
    if (!g) return;
    if (g->fp) { fclose(g->fp); g->fp = NULL; }
    if (g->path) remove(g->path);
    free(g->path);
    free(g->keys);
    free(g->vals);
    free(g->prev);
    free(g->cur);
    free(g);
}
