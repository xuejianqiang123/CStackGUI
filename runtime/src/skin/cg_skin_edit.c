/**
 * @file    cg_skin_edit.c
 * @brief   输入框外观：圆角裁切 + 单行文字垂直居中（EDIT 不支持 owner-draw，只能靠子类）
 * @details 单行 EDIT 的文字默认贴着客户区顶部：用 WM_NCCALCSIZE 把客户区上边让出一段，
 *          文字就落到中间。让出来的那条上边距成了非客户区，由我们自己在 WM_PAINT 里用底色补上，
 *          否则会露出窗口底色。字体是控件建好后才 WM_SETFONT 设进去的，所以还要在 WM_SETFONT
 *          时按实际字号重算一次。
 *          边框画在客户区之外（客户区四边由 WM_NCCALCSIZE 内缩 bw），
 *          所以控件自身重绘（选中 / 滚动 / 光标闪烁）永远盖不掉它。
 *          **整圈环都必须落在非客户区带里**：窗口最右列 / 最下行被圆角区域裁掉，
 *          客户区又是从 bw 到 w-bw；环的右 / 下带一旦越进客户区，就会被 EDIT
 *          自己的重绘刷掉，只剩上 / 左半圈（2026-11-08 用户连报的"输入框不完整"）。
 *          现在带宽 CG_EDIT_BORDER(3)、环宽 2，四边都留在非客户区。
 */
/* 明确走 W 系列 API：必须在 windows.h 之前定义 */
#define UNICODE
#define _UNICODE

#include "cg_skin.h"

#include <windows.h>
#include <commctrl.h>   /* SetWindowSubclass / DefSubclassProc */
#include <stdlib.h>
#include <string.h>

/* .cgui 的 border 字段取值（真相在 cg_doc.h::cg_node_t.border）。
 * 缺省 0 = 本工程原本的 M3 圆角；1~6 对应属性面板里那六种边框。 */
#define SK_EDGE_DEFAULT 0
#define SK_EDGE_NONE    1
#define SK_EDGE_INSET   2
#define SK_EDGE_OUTSET  3
#define SK_EDGE_SOFT    4
#define SK_EDGE_FRAME   5
#define SK_EDGE_LINE    6

/** 边框环宽：带宽 CG_EDIT_BORDER 里留 1px 给被圆角窗口区域裁掉的最外列 / 行，
 *  剩下这 2px 才拿来画环 —— 这样环的四条边都完整落在非客户区内。 */
#define SK_EDGE_RING_W  (CG_EDIT_BORDER - 1)

/* 边框配色（和 styles.css / 画布观感一致）：缺省灰、聚焦紫、3D 明暗、浅灰、硬灰。
 * **一律用 RGB() 宏写**：COLORREF 的内存布局是 0x00BBGGRR，直接写 0x6750A4 这种
 * 「看着像 #RRGGBB」的字面量会把红蓝弄反 —— 聚焦环会变成暗红（#A45067）而不是主题紫。 */
#define SK_EDGE_GREY    RGB(0xB8, 0xB2, 0xBD)
#define SK_EDGE_SOFT_C  RGB(0xCA, 0xC4, 0xD0)
#define SK_EDGE_LINE_C  RGB(0x79, 0x74, 0x7E)
#define SK_EDGE_DARK    RGB(0xA9, 0xA3, 0xAF)
#define SK_EDGE_LIGHT   RGB(0xFF, 0xFF, 0xFF)
#define SK_EDGE_FOCUS   RGB(0x67, 0x50, 0xA4)

typedef struct {
    int      rad;      /* 圆角半径 */
    int      single;   /* 单行（含数值框）：需要垂直居中；多行保持顶部对齐 */
    int      pad;      /* 上方让出的像素（= (控件高 - 文字高) / 2） */
    int      text_h;   /* 当前字体的文字高度（换字号时对比，变了才重排） */
    int      w, h;     /* 控件整块尺寸（比客户区大，含边框与让出来的上边距） */
    int      bw;       /* 边框宽度：客户区四边各内缩这么多 */
    int      left;     /* 左边距（EM_SETMARGINS）；箭头/换字体后会被清，要按它重设 */
    int      right;    /* 右边距：右对齐文字的额外留白，重设左边距时必须一起带回 */
    int      border;   /* 边框样式（.cgui 的 border 字段） */
    int      display;  /* 只读单行展示框（input:1 的结果 / 状态框）：不进焦点链、不产生选中高亮 */
    int      arrow;    /* 调节器占的宽度（物理像素）；0 = 没有调节器 */
    HWND     spin;     /* 调节器（上下箭头）的窗口；没有就是 NULL */
    COLORREF bg;       /* 补上边距用的底色（白或 style.bg） */
} sk_edit_sub_t;

/**
 * @brief 深色底输入框：把系统选中高亮重映射成「底色不变 + 文字提亮」，不让蓝块盖住输入框
 * @param hwnd 输入框原生窗口
 * @param s 输入框子类数据；s->bg 是客户区底色
 * @return void 无返回值
 * @note 标准 EDIT 用 COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT 画选中态，没有按控件改色的
 *       消息。长文本全选时那块 #0078D7 会铺满整个客户区、一直顶到圆角边框，深色主题下
 *       看着像「输入框被一块东西遮住了」（用户连报的"选中时遮挡输入框"）。这里在
 *       WM_PAINT 之后把客户区读进 32bpp DIB，凡是落在「高亮底色 → 高亮文字色」这条
 *       抗锯齿色带上的像素，按同一混合系数重映射到「输入框底色 → 近白文字」：
 *       蓝底消失、白字保留，边框与底色分毫不动，选中反馈靠文字变白来体现。
 *       投影 + 距离阈值保证普通底色 / 普通文字（含抗锯齿）不会被误改；处理是幂等的，
 *       局部重绘时上一轮已改过的像素不在这条色带上，会原样留下。
 *       浅色底（亮度 > 128）保持系统默认选中色，不改变浅色主题的原有观感。
 *       **只改颜色、不改交互**：可编辑框依旧能聚焦、拖选、Ctrl+A、Ctrl+C。
 *       写回时只 BitBlt 改动像素的包围盒，绝不整块覆盖客户区：圆角半径通常大于
 *       边框带宽（rad=10 / bw=3），圆弧有 7px 落在客户区里，整块写回会把它们
 *       抹成底色，聚焦框右上 / 右下角就断开、右边只剩一条孤立竖线。调用点还要在
 *       染色之后补一次 sk_edit_box()：EDIT 内部重绘客户区不发 WM_PAINT，空选区时
 *       本函数会提前返回，只有调用点补画才能覆盖「取消选中」这类情况。
 */
static void sk_edit_retint_selection(HWND hwnd, const sk_edit_sub_t *s)
{
    DWORD   sel;
    RECT    rc;
    HDC     dc, mem;
    HBITMAP bmp, old = NULL;
    BITMAPINFO bi;
    unsigned char *bits = NULL;
    COLORREF hi, ht;
    int ar, ag, ab, br, bgc, bb;     /* 输入色带 A=高亮底、B=高亮文字 */
    int nr, ng, nb, mr, mg, mb;     /* 输出色带 A'=输入框底色、B'=近白文字 */
    int abr, abg, abb, denom, lum;
    int w, h, x, y, stride;
    int changed = 0, bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;

    /* 浅色底不处理：系统蓝高亮本来就是浅色主题的正常观感 */
    nr = GetRValue(s->bg); ng = GetGValue(s->bg); nb = GetBValue(s->bg);
    lum = (nr * 299 + ng * 587 + nb * 114) / 1000;
    if (lum > 128) return;
    if (GetFocus() != hwnd) return;                 /* 高亮只在聚焦时显示 */
    sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
    if (LOWORD(sel) >= HIWORD(sel)) return;         /* 空选区：没有高亮块 */

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    hi = GetSysColor(COLOR_HIGHLIGHT);
    ht = GetSysColor(COLOR_HIGHLIGHTTEXT);
    ar = GetRValue(hi); ag = GetGValue(hi); ab = GetBValue(hi);
    br = GetRValue(ht); bgc = GetGValue(ht); bb = GetBValue(ht);
    abr = br - ar; abg = bgc - ag; abb = bb - ab;
    denom = abr * abr + abg * abg + abb * abb;
    if (denom <= 0) return;
    mr = 0xF2; mg = 0xF8; mb = 0xFF;               /* 选中文字：近白，比纯白柔和 */
    /* 选中底色只比输入框底色亮 16：能看出选区范围，又不会再变成"盖住输入框"的贴片。
     * 想彻底无底色（选中只靠文字提亮）把这三条去掉即可。 */
    nr += 16; if (nr > 255) nr = 255;
    ng += 16; if (ng > 255) ng = 255;
    nb += 16; if (nb > 255) nb = 255;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                    /* top-down：row 0 就是客户区顶行 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dc = GetDC(hwnd);
    if (!dc) return;
    mem = CreateCompatibleDC(dc);
    bmp = mem ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0) : NULL;
    if (!mem || !bmp || !bits) goto cleanup;
    old = (HBITMAP)SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, w, h, dc, 0, 0, SRCCOPY)) {
        SelectObject(mem, old);
        goto cleanup;
    }

    stride = w * 4;
    bx0 = w; by0 = h; bx1 = -1; by1 = -1;
    for (y = 0; y < h; ++y) {
        unsigned char *row = bits + y * stride;
        for (x = 0; x < w; ++x, row += 4) {
            int pr = row[2], pg = row[1], pb = row[0];   /* DIB 是 BGRA */
            int acr = pr - ar, acg = pg - ag, acb = pb - ab;
            int num = acr * abr + acg * abg + acb * abb;
            int tq = (num * 256) / denom;                 /* 色带混合系数 0..256 */
            int pjr, pjg, pjb, dr, dg, db2;

            if (tq < -20 || tq > 276) continue;           /* 不属于这条色带 */
            if (tq < 0) tq = 0;
            if (tq > 256) tq = 256;
            pjr = ar + tq * abr / 256;
            pjg = ag + tq * abg / 256;
            pjb = ab + tq * abb / 256;
            dr = pr - pjr; dg = pg - pjg; db2 = pb - pjb;
            if (dr * dr + dg * dg + db2 * db2 > 45 * 45) continue;  /* 离色带太远：普通底色/文字 */
            row[2] = (unsigned char)(nr + tq * (mr - nr) / 256);
            row[1] = (unsigned char)(ng + tq * (mg - ng) / 256);
            row[0] = (unsigned char)(nb + tq * (mb - nb) / 256);
            changed++;
            if (x < bx0) bx0 = x;
            if (y < by0) by0 = y;
            if (x > bx1) bx1 = x;
            if (y > by1) by1 = y;
        }
    }
    /* **只写回真正改过的那块**：整块客户区 BitBlt 会把圆角弧伸进客户区的那截抹成
     * 底色。圆角半径通常大于边框带宽度（rad=10 / bw=3），圆弧有 7px 落在客户区里，
     * 全量写回后右上 / 右下角就断开，右边只剩一条孤立竖线 —— 只有聚焦框会跑到这里
     * （无焦点时函数提前返回），所以表现为"只有当前聚焦的那个输入框右边不完整"。 */
    if (changed > 0)
        BitBlt(dc, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, mem, bx0, by0, SRCCOPY);
    SelectObject(mem, old);
cleanup:
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(hwnd, dc);
    /* 边框由各调用点在染色之后统一补画：EDIT 内部重绘客户区（选中、取消选中、
     * 光标闪烁）不一定发 WM_PAINT，伸进客户区的圆角弧随时可能被刷掉，
     * 只在染色里补是不够的。 */
}

/**
 * @brief 输入框的"边框带补底"色：写了 style.bg 听用户的，否则浅色交系统、深色跟主题
 * @param w 输入框控件
 * @return COLORREF 底色
 * @note 边框带（sk_edit_fill_edge）与客户区（WM_CTLCOLOR 的深色分支，cg_win_ctl.c）
 *       共用这一套值 —— 两处不同值就会出现"边框带一截、客户区一截"。
 */
static COLORREF sk_edit_bg_color(const cg_widget_t *w)
{
    if (w->style.bg != CG_COLOR_NONE)
        return RGB((w->style.bg >> 16) & 0xFF, (w->style.bg >> 8) & 0xFF, w->style.bg & 0xFF);
    /* 深色给 SURFACE_2（与 WM_CTLCOLOR 深色分支同值）；浅色仍是系统窗口色 */
    return (w->app && w->app->dark) ? RGB(0x1D, 0x1B, 0x20) : GetSysColor(COLOR_WINDOW);
}

/**
 * @brief 深色底上把 M3 的浅灰边框换成看得清的冷蓝线
 * @param base 浅色主题下原本的边框色
 * @param bg 输入框底色（sk_edit_bg_color 的结果）
 * @return COLORREF 实际要用的边框色
 * @note 缺省灰环 RGB(0xB8,0xB2,0xBD) 在深海军蓝底上是刺眼白边，但**不能反过来压得太暗**：
 *       曾经统一给 RGB(0x3D,0x5B,0x78)，1px 线压在 #16263F 上亮度只差 49，
 *       肉眼几乎看不见，截图再缩放一次就断成「左边一小截 + 底边一条线」，
 *       看上去和"边框只画了半圈"一模一样。现在按底色自适应抬亮：
 *       深色一律抬到与底色有约 3:1 以上对比的冷蓝，浅色仍保持 M3 原值。
 *       子类过程手里只有 hwnd 和子类数据，拿不到 app->dark，用 s->bg 的亮度判断即可。
 */
static COLORREF sk_edit_edge_tone(COLORREF base, COLORREF bg)
{
    int r = GetRValue(bg);
    int g = GetGValue(bg);
    int b = GetBValue(bg);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    int tr, tg, tb;

    if (lum >= 128) return base;
    tr = r + 70;
    tg = g + 95;
    tb = b + 125;
    if (tr > 235) tr = 235;
    if (tg > 235) tg = 235;
    if (tb > 245) tb = 245;
    return RGB(tr, tg, tb);
}

/**
 * @brief 沿非客户区边框带填充一圈圆角环（四边闭合，且不会被客户区重绘盖掉）
 * @param dc 目标设备上下文；环填在它上面
 * @param s 输入框子类数据；提供整块尺寸、边框带宽与圆角半径
 * @param col 环的颜色
 * @param penw 环宽（像素）：细线 / 缺省 / 焦点环给 SK_EDGE_RING_W，镜框双圈给 1
 * @param half 环离可见区边缘的距离（像素）：外圈 0，镜框内圈 1
 * @return void 无返回值
 * @note **不可以用 RoundRect + 画笔描边**：GDI 的笔沿路径往右下铺，右 / 下两条路径
 *       正好压在被圆角窗口区域裁掉的最右列 / 最下行上，画完就没了。
 *       也不可以只按「可见区外框」画 1px 环：WM_NCCALCSIZE 让出的客户区是
 *       [bw, w-bw) × [pad, h-bw)，而可见区是 [0, w-1) × [0, h-1)。带宽只有 1px 时，
 *       环的右带落在 x=w-2、下带落在 y=h-2，**两处都在客户区里**，EDIT 一重绘
 *       客户区就把它们刷成底色 —— 这才是「只有上边和左边有框」的真正原因；
 *       之前把边框颜色提亮只是让留下来的半圈更显眼，并没有补上缺的两条边。
 *       现在带宽 3、环宽 2：外圆角区域 [0,w-1) 减内圆角区域 [2,w-3)，
 *       右带 x∈{w-3,w-2}、下带 y∈{h-3,h-2} 全在非客户区 [w-3,w) 内，
 *       控件怎么重绘客户区都碰不到；最外那 1px 被裁掉也还有 2px 完整闭合。
 *       圆角半径按矩形尺寸夹取，rad 过大时 GDI 区域仍然完整。
 */
static void sk_edit_ring(HDC dc, const sk_edit_sub_t *s, COLORREF col, int penw, int half)
{
    HRGN   outer, inner, band;
    HBRUSH br;
    int    w = s->w - 1;   /* 可见区 exclusive 右界（最右一列被圆角区域裁掉） */
    int    h = s->h - 1;   /* 可见区 exclusive 下界（最下一行同理） */
    int    rw, rh;
    int    d, di;
    int    rc;

    if (penw < 1) penw = 1;
    if (half < 0) half = 0;
    /* 越过这条线，环的右 / 下带就会伸进客户区被刷掉；宁可不画也不留半圈 */
    if (half + penw > s->bw - 1) return;
    rw = w - 2 * half;
    rh = h - 2 * half;
    if (rw <= 2 * penw || rh <= 2 * penw) return;
    d = s->rad * 2;
    if (d > rw) d = rw;
    if (d > rh) d = rh;
    if (d < 2) d = 2;
    di = d - 2 * penw;
    if (di < 2) di = 2;

    outer = CreateRoundRectRgn(half, half, w - half, h - half, d, d);
    inner = CreateRoundRectRgn(half + penw, half + penw,
                               w - half - penw, h - half - penw, di, di);
    band  = CreateRectRgn(0, 0, 0, 0);
    if (!outer || !inner || !band) goto cleanup;
    rc = CombineRgn(band, outer, inner, RGN_DIFF);
    if (rc == ERROR || rc == NULLREGION) goto cleanup;
    br = CreateSolidBrush(col);
    if (br) {
        FillRgn(dc, band, br);
        DeleteObject(br);
    }
cleanup:
    if (band)  DeleteObject(band);
    if (inner) DeleteObject(inner);
    if (outer) DeleteObject(outer);
}

/**
 * @brief 把边框带（上边距 + 左右下）整条填成输入框底色
 * @param dc 目标设备上下文；用整窗 DC 填充
 * @param s 输入框子类数据；提供底色、边框宽与箭头占位
 * @return void 无返回值
 * @note **每次重画都先填**：不然细线样式只覆盖了带内的一个像素，上一次聚焦时画的
 *       2px 紫环会残留一条紫边（"点亮一次之后边框痕迹不消失"就是这么来的）。
 *       右上角那块"垂直居中的上边距"只铺到客户区右边界：那一列是调节器箭头的地盘，
 *       铺过去会把"向上箭头"的头顶削掉（用户报过的现象）
 */
static void sk_edit_fill_edge(HDC dc, const sk_edit_sub_t *s)
{
    HBRUSH br = CreateSolidBrush(s->bg);
    int    cut = s->w - s->bw - s->arrow;   /* 箭头列的左边界 = 客户区右边界 */
    RECT   r;
    if (!br) return;
    /* 上下的**边框带**整宽填：它们在箭头之上 / 之下，和箭头不重叠 */
    r.left = 0; r.top = 0;            r.right = s->w; r.bottom = s->bw; FillRect(dc, &r, br);
    r.left = 0; r.top = s->h - s->bw; r.right = s->w; r.bottom = s->h;  FillRect(dc, &r, br);
    /* 垂直居中的上边距：只铺到客户区右边（右边那条带同理，只填边框宽度） */
    if (s->pad > s->bw) {
        r.left = 0; r.top = s->bw; r.right = cut; r.bottom = s->pad; FillRect(dc, &r, br);
    }
    r.left = 0;            r.top = 0; r.right = s->bw; r.bottom = s->h; FillRect(dc, &r, br);
    r.left = s->w - s->bw; r.top = 0; r.right = s->w;  r.bottom = s->h; FillRect(dc, &r, br);
    DeleteObject(br);
}

/**
 * @brief 立体边框：整圈先按「下 / 右」的颜色画圆角环，再把「上 / 左」两条边换成另一种色
 * @param dc 目标设备上下文；环与两条边都画在它上面
 * @param s 输入框子类数据；提供边框带宽与圆角半径
 * @param outset 立体方向：0 = 凹入式（上 / 左暗、下 / 右亮），非 0 = 凸出式
 * @return void 无返回值
 * @note 覆盖上 / 左用 FillRect 而不是画笔：画笔沿路径往右下铺，压在客户区边界上
 *       的那部分会被 EDIT 重绘刷掉；填充只碰非客户区带里的 2px，四边都留得住。
 */
static void sk_edit_bevel(HDC dc, const sk_edit_sub_t *s, int outset)
{
    HBRUSH   br;
    RECT     r;
    COLORREF lo = outset ? SK_EDGE_LIGHT : SK_EDGE_DARK;   /* 上/左 */
    COLORREF hi = outset ? SK_EDGE_DARK  : SK_EDGE_LIGHT;  /* 下/右 */

    sk_edit_ring(dc, s, hi, SK_EDGE_RING_W, 0);
    br = CreateSolidBrush(lo);
    if (!br) return;
    /* 两端各留出圆角半径，剩下的直线段覆色；圆角仍由环负责 */
    r.left = s->rad; r.top = 0; r.right = s->w - 1 - s->rad; r.bottom = SK_EDGE_RING_W;
    if (r.right > r.left) FillRect(dc, &r, br);
    r.left = 0; r.top = s->rad; r.right = SK_EDGE_RING_W; r.bottom = s->h - 1 - s->rad;
    if (r.bottom > r.top) FillRect(dc, &r, br);
    DeleteObject(br);
}

/**
 * @brief 画上边距的底 + 边框
 * @param hwnd 输入框窗口；用整窗 DC 画
 * @param s 输入框子类数据；提供边框样式、宽度与圆角
 * @param focus 是否聚焦：非 0 = 画主题紫环，0 = 按 .cgui 的 border 样式画
 * @return void 无返回值
 * @note 边框样式由 .cgui 的 border 决定，**聚焦时一律换成主题紫** —— 键盘用户必须看得见
 *       焦点在哪，这条优先级高于边框样式（无边框也一样）；边框画在客户区之外
 *       （客户区已四边内缩），所以控件怎么重绘都盖不掉它
 */
static void sk_edit_box(HWND hwnd, const sk_edit_sub_t *s, int focus)
{
    HDC      dc;
    COLORREF c_grey = sk_edit_edge_tone(SK_EDGE_GREY,    s->bg);
    COLORREF c_soft = sk_edit_edge_tone(SK_EDGE_SOFT_C,  s->bg);
    COLORREF c_line = sk_edit_edge_tone(SK_EDGE_LINE_C,  s->bg);

    if (s->w <= 2 * s->bw || s->h <= 2 * s->bw) return;
    dc = GetWindowDC(hwnd);
    if (!dc) return;

    /* 第一步永远是把整条边框带（含让出来的上边距）填成底色：
     * 既补上"非客户区没人画"的部分，也顺手抹掉上一次画过的边框/焦点环。 */
    sk_edit_fill_edge(dc, s);

    if (focus) {                                  /* 聚焦：统一紫环（优先级高于边框样式） */
        sk_edit_ring(dc, s, SK_EDGE_FOCUS, SK_EDGE_RING_W, 0);
        ReleaseDC(hwnd, dc);
        return;
    }

    /* 所有环都从可见区外沿 (half=0) 起画、宽 SK_EDGE_RING_W：右 / 下带整段落在
     * 非客户区里，客户区怎么重绘都刷不到。镜框改用两圈 1px 双色线代替 2px 单环。 */
    switch (s->border) {
    case SK_EDGE_NONE:   break;                          /* 无边框：底色已经填好，不画线 */
    case SK_EDGE_INSET:  sk_edit_bevel(dc, s, 0); break;
    case SK_EDGE_OUTSET: sk_edit_bevel(dc, s, 1); break;
    case SK_EDGE_SOFT:   sk_edit_ring(dc, s, c_soft, SK_EDGE_RING_W, 0); break;
    case SK_EDGE_FRAME:                                  /* 镜框：外圈 + 紧邻内圈，双色双线 */
        sk_edit_ring(dc, s, c_line, 1, 0);
        sk_edit_ring(dc, s, c_soft, 1, 1);
        break;
    case SK_EDGE_LINE:   sk_edit_ring(dc, s, c_line, SK_EDGE_RING_W, 0); break;
    default:             sk_edit_ring(dc, s, c_grey, SK_EDGE_RING_W, 0); break;   /* SK_EDGE_DEFAULT */
    }
    ReleaseDC(hwnd, dc);
}

/**
 * @brief 取当前字体的文字行高（拿不到就是 0）
 * @param hwnd 输入框窗口；用它的控件字体量高度
 * @return int 文字行高（物理像素）；取 DC 或取字体度量失败返回 0
 * @note **必须先把控件自己的字体选进 DC 再量**：直接拿 DC 里选中的是系统默认字体
 *       （16px 的位图字体），拿它量雅黑 10pt（行高约 19px）会量小，上边距就算大了 ——
 *       文字整条往下坠（用户报过的"输入框里的字往下沉"）
 */
static int sk_edit_text_height(HWND hwnd)
{
    HDC        dc = GetDC(hwnd);
    HFONT      f, of;
    TEXTMETRICW tm;
    int th = 0;
    if (!dc) return 0;
    memset(&tm, 0, sizeof(tm));
    f = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    of = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
    if (GetTextMetricsW(dc, &tm)) th = tm.tmHeight;
    SelectObject(dc, of);
    ReleaseDC(hwnd, dc);
    return th;
}

/**
 * @brief 重算整块尺寸 + 圆角区域；多行输入框顺带把客户区往里缩一点
 * @param hwnd 输入框窗口；按它的窗口矩形更新子类数据并重设圆角区域
 * @param s 输入框子类数据；尺寸被就地更新
 * @return void 无返回值（尺寸非正时不设区域）
 */
static void sk_edit_resize(HWND hwnd, sk_edit_sub_t *s)
{
    RECT wr;
    GetWindowRect(hwnd, &wr);
    s->w = wr.right - wr.left;
    s->h = wr.bottom - wr.top;
    if (s->w > 0 && s->h > 0) {
        HRGN rgn = CreateRoundRectRgn(0, 0, s->w, s->h, s->rad * 2, s->rad * 2);
        if (rgn) SetWindowRgn(hwnd, rgn, TRUE);   /* 系统接管区域所有权，调用方不得再删 */
    }
    if (!s->single) {
        RECT tr;
        GetClientRect(hwnd, &tr);
        InflateRect(&tr, -5, -3);
        SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&tr);
    }
}

/**
 * @brief 把调节器（上下箭头）摆到边框内侧的右边
 * @param hwnd 输入框窗口；箭头按它的窗口矩形定位
 * @param s 输入框子类数据；提供箭头宽度、边框宽与整块尺寸
 * @return void 无返回值（无调节器或宽度为 0 时直接返回）
 * @note **宽度由我们定**：up-down 控件建出来是 0x0，等它自报宽度就是把命交给系统 ——
 *       有的机器上它一直是 0x0，箭头就"消失"了。也**故意不用系统的靠右对齐样式**：
 *       那个按伙伴窗口的"窗口矩形"贴右，会压在自绘边框上、越过上下圆角
 */
static void sk_edit_spin_fit(HWND hwnd, const sk_edit_sub_t *s)
{
    RECT er;
    int  uw, uh;

    if (!s->spin || !IsWindow(s->spin) || s->arrow <= 0) return;
    uw = s->arrow;
    uh = s->h - 2 * s->bw;                 /* 只占圆角框内部的高度 */
    if (uh < 8) uh = s->h;
    GetWindowRect(hwnd, &er);
    MapWindowPoints(HWND_DESKTOP, GetParent(hwnd), (POINT *)&er, 2);   /* 换成父窗口坐标 */
    /* **要提到输入框之上**（HWND_TOP）。子窗口建出来时排在先建的那些**之下**，
     * 而输入框比箭头先建 —— 于是箭头被输入框的窗口整个压住：看得见看不见全看谁后重绘，
     * 点击也落在输入框的非客户区上（用户报的"没有上下图标、点不了"就是这个）。
     * 提起层级后，输入框的 DC 裁剪区会自动排除箭头，两边互不干扰。
     * 位置仍然是我们算的（不是 UDS_ALIGNRIGHT 那种系统摆位）。 */
    SetWindowPos(s->spin, HWND_TOP, er.left + s->w - s->bw - uw, er.top + s->bw, uw, uh,
                 SWP_NOACTIVATE);
    /* 摆完必须**立刻整块重画**：SetWindowPos 之后系统只让控件重画"它认为失效"的那部分，
     * 结果常见半个箭头（上下两个按钮只画了一个）——用户看到的就是"没图标了"。
     * 提层级 + 改尺寸后自己 RedrawWindow 一次，两个按钮都在。 */
    RedrawWindow(s->spin, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    /* 文字留在客户区里（客户区右边已经让开箭头了），再留一点空隙免得贴着 */
    SendMessageW(hwnd, EM_SETMARGINS, EC_RIGHTMARGIN, MAKELPARAM(0, 2));
}

/**
 * @brief 输入框子类过程：让出边框与上边距、换字体 / 改尺寸时重排、焦点与非客户区重画
 * @param hwnd 输入框窗口
 * @param msg 窗口消息号
 * @param wp 消息的附加参数；含义随 msg 变化
 * @param lp 消息的附加参数；含义随 msg 变化
 * @param id 子类 id（本文件固定传 1）
 * @param data 子类数据：输入框子类数据结构指针；窗口销毁时在此释放
 * @return LRESULT 交给系统：各分支取原窗口过程的结果或原样透传
 */
static LRESULT CALLBACK sk_edit_sub(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR data)
{
    sk_edit_sub_t *s = (sk_edit_sub_t *)data;

    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, sk_edit_sub, id);
        free(s);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    /* 只读单行展示框（计算器的结果框 / 统计框这类 input:1）：纯展示，不参与编辑交互。
     * 标准 EDIT 只在有焦点时画选中高亮，用的还是系统亮蓝 COLOR_HIGHLIGHT —— 深色主题下
     * 双击 / 拖选 / Ctrl+A 就会留下一块 #0078D7 底 + 白字的"贴片"，看着像有东西把输入框
     * 右端遮住了（2026-11-08 用户连报三次的"文本被遮挡"）。只读单行框本来也不该被编辑，
     * 干脆从源头不让它拿到焦点：鼠标按下 / 双击 / 右键一律吞掉，Tab 链也在 apply 时去掉。
     * **多行只读输出区不受影响**：日志 / 终端需要能选中复制，它们仍走默认行为。 */
    if (s->display) {
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return 0;                       /* 不聚焦、不定位插入点、不开始拖选、不弹编辑菜单 */
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));   /* 别暗示"可以点进去输入" */
            return TRUE;
        case WM_KEYDOWN:
        case WM_CHAR:
            return 0;                       /* 兜底：程序化聚焦也拦住 Ctrl+A / Shift+扩选 */
        default:
            break;
        }
    }
    /* 选中高亮是 EDIT **内部拿 GetDC 直接画**的：Ctrl+A / 拖选 / 双击选词 / EM_SETSEL
     * 全都不发 WM_PAINT（实测诊断日志：全选之后一条 WM_PAINT 都没有，选区仍是空的
     * 那种记录）。所以只把染色挂在 WM_PAINT 上永远抓不到这块蓝底。对这些"会改选区"
     * 的消息，先让默认处理把高亮画完，随后立刻在窗口 DC 上重映射一次。 */
    if (msg == WM_KEYDOWN || msg == WM_CHAR || msg == WM_SYSCHAR ||
        msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
        msg == WM_MOUSEMOVE || msg == EM_SETSEL) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        sk_edit_retint_selection(hwnd, s);
        /* EDIT 内部直接拿 GetDC 重绘客户区（选中 / 取消选中 / 拖选）不发 WM_PAINT，
         * 圆角弧伸进客户区的那截会被刷成底色，所以补画一次边框，且不能只在
         * "染色改了像素"时才补：取消选中后选区为空也要补。
         * **多行框例外**：灌日志 / 滚到底时 EDIT 会用 ScrollDC 搬整窗表面，
         * 在 EM_SETSEL（滚动之前）画的这圈灰环会被一起滚进客户区中间，
         * 变成一条横贯的实心线（test_skin_edit.c 的流式用例就是这么复现的）。
         * 多行框的边框由 WM_PAINT / 焦点 / NCPAINT 分支在滚动之后补，不受影响。 */
        if (s->single) sk_edit_box(hwnd, s, GetFocus() == hwnd);
        return r;
    }
    if (msg == WM_NCCALCSIZE && wp) {
        /* 进来时 rgrc[0] 是通过的建议窗口矩形，处理完变成客户区 */
        NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
        int win_w = p->rgrc[0].right - p->rgrc[0].left;
        int win_h = p->rgrc[0].bottom - p->rgrc[0].top;
        int bw = s->bw;
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);

        if (bw * 2 >= win_w || bw * 2 >= win_h) bw = 0;   /* 控件太小就别内缩 */
        s->w = win_w;
        s->h = win_h;
        /* 客户区四边内缩出边框宽度：边框画在客户区之外，控件重绘（选中/滚动/闪烁）盖不掉 */
        p->rgrc[0].left += bw;
        p->rgrc[0].right -= bw;
        p->rgrc[0].bottom -= bw;
        /* 调节器那一列同样让出客户区：箭头摆在非客户区上，输入框自己的白底重绘盖不到它，
         * 点击也不会被输入框吞掉（留够文字宽度才让，窄控件宁可不要箭头） */
        if (s->arrow > 0 && win_w - 2 * bw - s->arrow >= 24) {
            p->rgrc[0].right -= s->arrow;
        }
        if (s->single) {
            int th = sk_edit_text_height(hwnd);
            int pad = (th > 0) ? (win_h - 2 * bw - th) / 2 : 0;
            if (pad < 0) pad = 0;
            s->text_h = th;
            s->pad = bw + pad;                 /* 上边距 = 边框 + 居中量 */
        } else {
            s->pad = bw;
        }
        p->rgrc[0].top += s->pad;              /* 让出上边距 -> 文字垂直居中 */
        return r;
    }
    if (msg == WM_PAINT) {
        /* 客户区刚重绘完：补一次边框。EDIT 的选中 / 光标刷新不一定带 WM_NCPAINT，
         * 不补的话个别时序下非客户区会短暂留白。环全在非客户区，补画不碰文字。
         * 多行框只在下面「焦点 / NCPAINT」分支补画：灌日志时每轮 WM_PAINT 后紧接着
         * 滚动，刚画好的灰环会被 ScrollDC 一起搬进客户区中间，留下横贯实心线。 */
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        /* 系统画完（含选中高亮）之后，把深色底上那块 #0078D7 重映射成
         * 「底色不变 + 文字提亮」：可编辑框照样能选中复制，但不会再被蓝块盖住。
         * 必须在补画边框之前做，补画会顺手把被蓝块顶到的右 / 下边框修回来。 */
        sk_edit_retint_selection(hwnd, s);
        /* 只读框（input:1 → ES_READONLY）不给插入符：聚焦时 Win32 照样画 caret，
         * 闪烁重绘又会把 HideCaret 抵消，所以每次客户区重绘后都压住一次。 */
        if (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) HideCaret(hwnd);
        if (s->single) sk_edit_box(hwnd, s, GetFocus() == hwnd);
        return r;
    }
    if (msg == WM_SETFONT) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        /* 带调节器时：换字体会让箭头重新按 buddy 摆位，补摆一次 */
        sk_edit_spin_fit(hwnd, s);
        /* 数值框的上下箭头换字体时会重设 buddy 边距、把左边距清成 0：补一次 */
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(s->left, s->arrow > 0 ? 2 : s->right));
        if (s->single && sk_edit_text_height(hwnd) != s->text_h) {
            /* 字号变了 -> 重排一次（SWP_FRAMECHANGED 会再发 WM_NCCALCSIZE） */
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        return r;
    }
    if (msg == WM_SIZE) {
        sk_edit_resize(hwnd, s);
        sk_edit_spin_fit(hwnd, s);   /* 尺寸变了：箭头跟着摆到新的右边内侧 */
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(s->left, s->arrow > 0 ? 2 : s->right));
        /* resize 时系统按默认非客户区重算客户区，会把让出来的上边距抹掉（数值框的
         * 上下箭头改 buddy 尺寸就会触发）。补一次 FRAMECHANGED 让 WM_NCCALCSIZE 重新生效；
         * 尺寸没变不会递归发 WM_SIZE。 */
        if (s->single)
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_NCPAINT) {
        /* WM_NCPAINT 也要跟着画：边框和箭头占的那一列都在非客户区上，
         * 只等 WM_PAINT 的话，"只重画非客户区"的场合（改尺寸、系统重算边框）会留白 */
        LRESULT r;
        if (msg == WM_SETFOCUS)
            SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(s->left, s->arrow > 0 ? 2 : s->right));
        r = DefSubclassProc(hwnd, msg, wp, lp);
        /* 只读单行展示框即使被程序化 / Tab 聚焦也立刻把焦点还回去：没有焦点就不会画
         * 系统选中高亮，也不会画紫色焦点环，界面永远保持"纯展示"的样子。lp 是刚失去
         * 焦点的窗口，还给它不会形成循环。 */
        /* 防递归：万一前后两个都是展示框，A→B→A 会无限互还焦点 */
        static int refocusing;
        if (msg == WM_SETFOCUS && s->display && !refocusing && lp && IsWindow((HWND)lp)) {
            refocusing = 1;
            SetFocus((HWND)lp);
            refocusing = 0;
        }
        /* 只读框聚焦时系统会把插入符显示在文本开头；右对齐的结果文字在另一端，
         * 那条竖线就孤零零压在数字左边，看着像"文字被遮挡"。隐藏它，
         * 闪烁重绘由 WM_PAINT 分支继续压住。 */
        if (msg == WM_SETFOCUS && (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY))
            HideCaret(hwnd);
        sk_edit_retint_selection(hwnd, s);   /* 焦点回来时旧选区会重新画出蓝底 */
        sk_edit_box(hwnd, s, GetFocus() == hwnd);   /* 同上：焦点变化也会重绘客户区 */
        return r;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/**
 * @brief 建好调节器后把它摆进输入框（记下句柄并按客户区内侧重摆）
 * @param w edit 控件；无原生窗口或还没挂子类时直接返回
 * @return void 无返回值
 */
void cg_skin_edit_fit_spin(cg_widget_t *w)
{
    HWND        h;
    DWORD_PTR   data = 0;
    sk_edit_sub_t *s;

    if (!w || !w->native) return;
    h = (HWND)w->native;
    /* 子类数据里存着布局信息；取不到说明这个输入框还没挂子类，等挂上再说 */
    if (!GetWindowSubclass(h, sk_edit_sub, 1, &data) || !data) return;
    s = (sk_edit_sub_t *)data;
    s->spin = (HWND)w->native2;            /* 记下来：以后改尺寸/换字体时还要重摆 */
    sk_edit_spin_fit(h, s);
}

/**
 * @brief DPI 变化后刷新输入框皮肤里与 scale 绑定的量（圆角 / 边框带 / 箭头占位）
 * @param w 控件（edit）
 * @return void 无返回值
 * @details 这些量都按 scale 算，而 `cg_skin_apply_edit()` 只在建控件时调用一次 ——
 *          窗口跨到另一块不同 DPI 的显示器后，圆角与箭头宽度会停在旧比例
 *          （2026-09-23 高 DPI 审计发现）。这里只更新子类数据里的那几个值并重排：
 *          不重建子类，因为二次 `SetWindowSubclass` 会叠一层。
 */
void cg_skin_edit_rescale(cg_widget_t *w)
{
    HWND     hw;
    DWORD_PTR ref = 0;
    sk_edit_sub_t *s;
    int      scale, r, multiline;

    if (!w) return;
    hw = (HWND)w->native;
    if (!hw) return;
    if (!GetWindowSubclass(hw, sk_edit_sub, 1, &ref) || !ref) return;
    s = (sk_edit_sub_t *)ref;

    scale = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    r = (w->style.radius > 0 ? w->style.radius : 2) * scale / 100;   /* 与 apply_edit 同口径 */
    if (r < 1) r = 1;
    multiline = (GetWindowLongW(hw, GWL_STYLE) & ES_MULTILINE) != 0;

    s->rad  = r;
    s->spin = (HWND)w->native2;
    /* 与 cg_skin_apply_edit 同口径：带宽一律 CG_EDIT_BORDER，环才不会伸进客户区 */
    s->bw = CG_EDIT_BORDER;
    s->arrow = (s->spin && !multiline) ? (CG_EDIT_SPIN_W * scale / 100) : 0;
    if (s->arrow > 0 && s->arrow < 12) s->arrow = 12;

    sk_edit_resize(hw, s);
    sk_edit_spin_fit(hw, s);
    SetWindowPos(hw, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/**
 * @brief 主题切换后刷新输入框的缓存底色（深色皮肤 T1）
 * @param w 输入框控件
 * @return void 无返回值（没有子类数据时什么也不做）
 * @details s->bg 是建控件时算好缓存的，换主题不会自己变；这里只重算这一个值并让
 *          非客户区重画（WM_NCPAINT 重走 sk_edit_fill_edge 就用上了新底色）。
 *          客户区那半由 WM_CTLCOLOR 的深色分支负责（cg_win_ctl.c），两处同值才对得上。
 */
void cg_skin_edit_retheme(cg_widget_t *w)
{
    HWND          hw;
    DWORD_PTR     ref = 0;
    sk_edit_sub_t *s;

    if (!w) return;
    hw = (HWND)w->native;
    if (!hw) return;
    if (!GetWindowSubclass(hw, sk_edit_sub, 1, &ref) || !ref) return;
    s = (sk_edit_sub_t *)ref;
    s->bg = sk_edit_bg_color(w);
    SetWindowPos(hw, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/**
 * @brief 给输入框挂皮肤子类：算圆角 / 边框带 / 箭头占位，设边距并强制一次布局
 * @param w edit 控件；无原生窗口时直接返回
 * @return void 无返回值（子类数据分配失败时什么也不做）
 */
void cg_skin_apply_edit(cg_widget_t *w)
{
    HWND hw = (HWND)w->native;
    int scale, r, multiline;
    LONG sty;
    sk_edit_sub_t *s;
    if (!hw) return;
    scale = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    r = (w->style.radius > 0 ? w->style.radius : 2) * scale / 100;   /* 画布输入框默认 2px */
    if (r < 1) r = 1;
    sty = GetWindowLongW(hw, GWL_STYLE);
    multiline = (sty & ES_MULTILINE) != 0;

    s = (sk_edit_sub_t *)malloc(sizeof(sk_edit_sub_t));
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->rad = r;
    s->single = multiline ? 0 : 1;
    /* 只读 + 单行 = 纯展示框（input:1 的结果 / 状态框）。多行只读是日志 / 输出区，
     * 用户要能选中复制，保持原行为。展示框连 Tab 链都不进：对话框管理器切焦点时会
     * 顺手给 edit 发全选，深色主题下就留下刺眼的系统蓝高亮块。 */
    s->display = (!multiline && (sty & ES_READONLY)) ? 1 : 0;
    if (s->display)
        SetWindowLongW(hw, GWL_STYLE, sty & ~WS_TABSTOP);
    /* 边框带一律让出 CG_EDIT_BORDER(3px)：2px 环 + 1px 给被圆角区域裁掉的最外列 / 行。
     * 以前按样式分档（细线只让 1px），环的右 / 下两条边正好落在客户区里被 EDIT
     * 自己的重绘刷掉，于是输入框只剩上 / 左半圈 —— 这是"边框不完整"的根因。 */
    s->bw = CG_EDIT_BORDER;
    s->left = CG_EDIT_TEXT_MARGIN;
    s->border = w->border;
    s->spin = (HWND)w->native2;   /* 调节器可能还没建；建好后 create 会再调 fit 补上 */
    /* 箭头占位：多行输入框不给箭头（运行时也不建），宽度按 scale 缩放、太窄兜底 12px。
     * 有了它，WM_NCCALCSIZE 才能把右边这列从客户区让出去。 */
    s->arrow = (w->spin && !multiline) ? (CG_EDIT_SPIN_W * scale / 100) : 0;
    if (s->arrow > 0 && s->arrow < 12) s->arrow = 12;
    s->bg = sk_edit_bg_color(w);

    sk_edit_resize(hw, s);   /* 初始尺寸 + 圆角区域 */
    /* 左右留边距：文字别怼到圆角边框上（边框在客户区外，垂直居中见 WM_NCCALCSIZE）。
     * 数值框的箭头稍后会重设边距，WM_SETFONT / WM_SIZE / SETFOCUS 会按 s->left/right 再补。
     * 右对齐（如只读结果框）额外多留 10px：ES_RIGHT 会把文字贴到客户区右边界，
     * 再叠 2px 焦点环，最后一个字符看着就像被右边框压住 / 遮住。
     * **必须存进 s->right**：建控件后 cgw_sync_widget 换字体、WM_SIZE 重排都会重发
     * EM_SETMARGINS，只在 apply_edit 设一次会被冲掉（实测 EM_GETMARGINS 只剩 right=2）。 */
    s->right = (w->style.align == CG_ALIGN_RIGHT)
               ? CG_EDIT_TEXT_MARGIN + 10 : CG_EDIT_TEXT_MARGIN;
    SendMessageW(hw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(s->left, s->right));
    SetWindowSubclass(hw, sk_edit_sub, 1, (DWORD_PTR)s);
    /* 挂上子类后强制一次布局：立刻按当前字体算好垂直居中量 */
    SetWindowPos(hw, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
