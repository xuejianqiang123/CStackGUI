/**
 * @file    cg_skin.c
 * @brief   自绘外观层（M3 风格）：用 GDI+ 画圆角按钮 / 开关 / 勾选 / 滑块 / 进度 / 圆环
 * @details 这是自绘后端的第一个可替换内核。所有绘制都靠 cg_widget_by_hwnd() 反查控件，
 *          不直接碰 Win32 控件创建逻辑，所以换一套外观时这里改完即可，上层零改动。
 *          GDI+ 已经被 image / canvas 控件初始化（cg_win_window.c 的 cgw_win_init 调 GdiplusStartup），
 *          本文件只负责在 WM_DRAWITEM 时往 DC 上画；GDI+ 万一没起来，自动回退 GDI RoundRect。
 *          输入框（edit：单行 / 多行 / 数值框）的圆角裁切与垂直居中在 cg_skin_edit.c。
 */
/* 明确走 W 系列 API：必须在 windows.h 之前定义 */
#define UNICODE
#define _UNICODE

#include "cg_skin.h"

#include <windows.h>
#include <commctrl.h>   /* NMCUSTOMDRAW / CDRF_* / TBCD_* / SetWindowSubclass */
#include <stdlib.h>
#include <string.h>
#include <math.h>       /* 圆环的 GDI 回退要算弧上的点 */

/**
 * @brief 画在「背后色」（= 透明底）上的**默认**文字色：深色下必须变浅
 * @param w 控件（取它的 app 判断明暗）；可传 NULL（那就当浅色）
 * @param disabled 禁用态（浅色下是灰、深色下用暗一点的灰，别用浅色那套灰 —— 那在深底上太亮）
 * @return unsigned 0xRRGGBB 文字色（不带 alpha）
 * @note 2026-09-27 的教训，值得留着：同一份"写死的深色字"在这个文件里有**两处**
 *       （开关那处 / 复选框·单选那处），当时只改了前者、漏了后者 —— 而且两处的常量
 *       差最后一位（`0x1D1B20` 与 `0x1C1B1F`），**按常量 grep 都搜不全**。
 *       是深色示例的队友拿代码 + 截图证据报回来的。所以收成一个函数：以后要调只改这里。
 *       （按钮那句文字色不在此列：那是画在浅紫填充上的字，深色下填充仍是浅紫。）
 */
static unsigned sk_default_text(const cg_widget_t *w, int disabled)
{
    int dark = (w && w->app && w->app->dark);

    if (disabled) return dark ? 0x6E6E72u : 0x9A9A9Eu;
    return dark ? 0xE6E0E9u : 0x1D1B20u;
}

/* ---- GDI+ 扁平 API（本机 MinGW 无 gdiplus.h 头，仅前向声明用到的几个） ---- */
typedef int     GpStatus;
typedef float   REAL;
typedef struct GpGraphics GpGraphics;
typedef struct GpBrush    GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpPath     GpPath;
typedef struct GpPen      GpPen;

#define SK_GDIP_OK 0
#define SK_FILL_WINDING 0
#define SK_UNIT_PIXEL   2
#define SK_SMOOTH_AA    4

__declspec(dllimport) GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
__declspec(dllimport) GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
__declspec(dllimport) GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, int mode);
__declspec(dllimport) GpStatus WINAPI GdipCreateSolidFill(unsigned int color, GpSolidFill **brush);
__declspec(dllimport) GpStatus WINAPI GdipDeleteBrush(GpBrush *brush);
__declspec(dllimport) GpStatus WINAPI GdipCreatePath(int fill, GpPath **path);
__declspec(dllimport) GpStatus WINAPI GdipDeletePath(GpPath *path);
__declspec(dllimport) GpStatus WINAPI GdipAddPathArc(GpPath *path, REAL x, REAL y, REAL w, REAL h, REAL sa, REAL ea);
__declspec(dllimport) GpStatus WINAPI GdipAddPathLine(GpPath *path, REAL x1, REAL y1, REAL x2, REAL y2);
__declspec(dllimport) GpStatus WINAPI GdipClosePathFigure(GpPath *path);
__declspec(dllimport) GpStatus WINAPI GdipFillPath(GpGraphics *g, GpBrush *brush, GpPath *path);
__declspec(dllimport) GpStatus WINAPI GdipCreatePen1(unsigned int color, REAL width, int unit, GpPen **pen);
__declspec(dllimport) GpStatus WINAPI GdipDeletePen(GpPen *pen);
__declspec(dllimport) GpStatus WINAPI GdipDrawPath(GpGraphics *g, GpPen *pen, GpPath *path);
__declspec(dllimport) GpStatus WINAPI GdipAddPathEllipse(GpPath *path, REAL x, REAL y, REAL w, REAL h);
__declspec(dllimport) GpStatus WINAPI GdipDrawLine(GpGraphics *g, GpPen *pen, REAL x1, REAL y1, REAL x2, REAL y2);
__declspec(dllimport) GpStatus WINAPI GdipDrawArc(GpGraphics *g, GpPen *pen, REAL x, REAL y, REAL w, REAL h, REAL sa, REAL sw);
__declspec(dllimport) GpStatus WINAPI GdipSetPenStartCap(GpPen *pen, int cap);
__declspec(dllimport) GpStatus WINAPI GdipSetPenEndCap(GpPen *pen, int cap);

#define SK_CAP_ROUND 2   /* GDI+ LineCapRound */
#define SK_PI        3.14159265358979
#define SK_RING_STEPS 72

/* 0xAARRGGBB */
/**
 * @brief 把 8 位透明度与 RGB 拼成 GDI+ 要的 0xAARRGGBB
 * @param a 透明度 0..255（0 = 全透明，255 = 不透明）
 * @param rgb 颜色 0xRRGGBB（只用低 24 位，高位丢掉）
 * @return unsigned int 0xAARRGGBB
 */
static unsigned int sk_argb(unsigned char a, unsigned rgb)
{
    return ((unsigned int)a << 24) | (rgb & 0xFFFFFFu);
}

/**
 * @brief 0xRRGGBB 转成 GDI 的 COLORREF
 * @param rgb 颜色 0xRRGGBB
 * @return COLORREF 颜色（GDI 文本 / 画刷用）
 */
static COLORREF sk_colorref(unsigned rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/**
 * @brief 把颜色按系数变亮 / 变暗
 * @param c 颜色 0xRRGGBB
 * @param f 系数；小于 1 变暗、大于 1 变亮（结果逐通道夹回 0..255）
 * @return unsigned 0xRRGGBB
 */
static unsigned sk_shade(unsigned c, float f)
{
    int r = (int)((float)((c >> 16) & 0xFF) * f + 0.5f);
    int g = (int)((float)((c >> 8) & 0xFF) * f + 0.5f);
    int b = (int)((float)(c & 0xFF) * f + 0.5f);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (unsigned)((r << 16) | (g << 8) | b);
}

/**
 * @brief 相对亮度（0..1），用于决定文字黑 / 白
 * @param c 颜色 0xRRGGBB
 * @return float 感知亮度（权重 0.2126 / 0.7152 / 0.0722）
 */
static float sk_luminance(unsigned c)
{
    float r = (float)((c >> 16) & 0xFF) / 255.0f;
    float g = (float)((c >> 8) & 0xFF) / 255.0f;
    float b = (float)(c & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/**
 * @brief UTF-8 转 UTF-16（含结尾 0）
 * @param s UTF-8 字符串；可传 NULL
 * @return wchar_t* 堆上宽字符串（调用方 free）；NULL / 转换失败返回 NULL
 */
static wchar_t *sk_to_wcs(const char *s)
{
    int n;
    wchar_t *buf;
    if (!s) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    buf = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!buf) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, n);
    return buf;
}

/**
 * @brief 往已有的 GDI+ Path 里追加一个圆角矩形（四角用圆弧）
 * @param p 目标路径（不接管所有权）
 * @param x 左上角横坐标（物理像素）
 * @param y 左上角纵坐标（物理像素）
 * @param w 宽（物理像素）
 * @param h 高（物理像素）
 * @param r 圆角半径（物理像素）；超过宽 / 高一半时按一半夹取，<= 0 退化成直角矩形
 * @return void 无返回值
 */
static void sk_round_rect(GpPath *p, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        GdipAddPathLine(p, x, y, x + w, y);
        GdipAddPathLine(p, x + w, y, x + w, y + h);
        GdipAddPathLine(p, x + w, y + h, x, y + h);
        GdipAddPathLine(p, x, y + h, x, y);
        GdipClosePathFigure(p);
        return;
    }
    GdipAddPathArc(p, x, y, 2 * r, 2 * r, 180, 90);
    GdipAddPathArc(p, x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    GdipAddPathArc(p, x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
    GdipAddPathArc(p, x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
    GdipClosePathFigure(p);
}

/**
 * @brief **中性灰部件**（轨道 / 槽 / 描边这类）在浅底与深底下的取值
 * @param app 取它的明暗标志；可传 NULL（那就当浅色）
 * @param light 浅色下用的值 0xRRGGBB
 * @param dark 深色下用的值 0xRRGGBB
 * @return unsigned 0xRRGGBB 当前主题该用的那个值
 * @note 2026-09-27 收拢：这类"浅灰细部件"原先写死在**八处**、值还不尽相同
 *       （圆环轨道、滑块槽、进度槽、进度描边、灭态灯点、图表边框、图表网格），
 *       深色下就是"深底上一条条发亮的浅槽"——深色示例那一路截图里最扎眼的一处。
 *       与默认文字色同一类教训：同一件事散在多处、且**常量各不相同**，
 *       按值 grep 根本找不全 ⇒ 收成一个函数，以后要调只改这里。
 *       （**不**包括品牌色填充：按钮 / 勾选 / 开关 / 进度填充的浅紫不跟随，
 *         画在它上面的字也就不跟着变浅）
 */
unsigned cg_skin_neutral(const cg_app_t *app, unsigned light, unsigned dark)
{
    return (app && app->dark) ? dark : light;
}

/**
 * @brief 控件"背后"是什么颜色（0xRRGGBB），自绘控件清自身底色时拿它填充
 * @param w 控件；NULL 或缺 app 时按缺省浅底处理
 * @return unsigned 0xRRGGBB 背后色（在带背景色的分组框 / 容器里就是那块面板的颜色）
 * @note 不能一律填窗口底色 —— 控件在带「背景色」的分组框 / 容器块里时，那块地方是面板的颜色，
 *       填窗口底色就会露出一块方块（表现为"分组框背景色没生效、反而控件变色"）
 */
static unsigned sk_behind(const cg_widget_t *w)
{
    if (!w || !w->app) return 0xF3EDF7u;
    return cg_win_behind_rgb(w->app, w->native);
}

/**
 * @brief 抗锯齿的圆角实心矩形（可选描边）—— M3 视觉里"丝滑圆角"的最小原语
 * @param hdc 目标 DC
 * @param rc 矩形（物理像素）
 * @param radius 圆角半径（物理像素）；大于高/宽一半时按一半夹取，半径 = 高即得胶囊形
 * @param fill_rgb 填充色 0xRRGGBB；CG_COLOR_NONE = 只描边不填充
 * @param border_rgb 描边色 0xRRGGBB；CG_COLOR_NONE = 不描边
 * @param border_w 描边宽度（物理像素）；不描边时忽略
 * @return void 无返回值；GDI+ 不可用时回退 GDI RoundRect（有锯齿但不至于不显示）
 * @details 独立导出是因为 owner-draw 小控件（徽章/标签片）与面板（card/shape 圆角）拿不到
 *          皮肤那几个函数里的 DC 通路；各写一遍 GDI `RoundRect` 就没有抗锯齿（圆角发毛）。
 */
void cg_skin_fill_round_rect(HDC hdc, const RECT *rc, int radius,
                             unsigned fill_rgb, unsigned border_rgb, float border_w)
{
    int w, h;
    GpGraphics *g = NULL;

    if (!hdc || !rc) return;
    w = rc->right - rc->left;
    h = rc->bottom - rc->top;
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    if (GdipCreateFromHDC(hdc, &g) == SK_GDIP_OK && g) {
        GpPath *p = NULL;
        GdipSetSmoothingMode(g, SK_SMOOTH_AA);
        if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
            sk_round_rect(p, (REAL)rc->left, (REAL)rc->top, (REAL)w, (REAL)h, (REAL)radius);
            if (fill_rgb != CG_COLOR_NONE) {
                GpSolidFill *br = NULL;
                if (GdipCreateSolidFill(sk_argb(255, fill_rgb), &br) == SK_GDIP_OK && br) {
                    GdipFillPath(g, (GpBrush *)br, p);
                    GdipDeleteBrush((GpBrush *)br);
                }
            }
            if (border_rgb != CG_COLOR_NONE) {
                GpPen *bd = NULL;
                if (GdipCreatePen1(sk_argb(255, border_rgb), (border_w > 0.0f) ? border_w : 1.0f,
                                   SK_UNIT_PIXEL, &bd) == SK_GDIP_OK && bd) {
                    GdipDrawPath(g, bd, p);
                    GdipDeletePen(bd);
                }
            }
            GdipDeletePath(p);
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI（原实现，圆角有锯齿） */
        HBRUSH br = (fill_rgb != CG_COLOR_NONE) ? CreateSolidBrush(sk_colorref(fill_rgb)) : NULL;
        HPEN   pn = (border_rgb != CG_COLOR_NONE)
                        ? CreatePen(PS_SOLID, (border_w > 1.5f) ? 2 : 1, sk_colorref(border_rgb))
                        : NULL;
        HGDIOBJ ob = SelectObject(hdc, br ? (HGDIOBJ)br : GetStockObject(NULL_BRUSH));
        HGDIOBJ op = SelectObject(hdc, pn ? (HGDIOBJ)pn : GetStockObject(NULL_PEN));
        RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius * 2, radius * 2);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        if (br) DeleteObject(br);
        if (pn) DeleteObject(pn);
    }
}

/**
 * @brief 圆环进度（progressring）的轨道 + 进度弧，全部走 GDI+ 抗锯齿
 * @param w 圆环控件（读 value/vmin/vmax/style，比例与暗色标志取自它的 app）
 * @param hdc 目标 DC
 * @param rc 客户区矩形（物理像素）
 * @return void 无返回值；GDI+ 不可用时回退 GDI 画弧（有锯齿但不至于不显示）
 * @details 中心百分比文字不在这里画：那要用字体缓存（在 cg_win_util.c），
 *          调用方画完圆环再补文字。GDI 的粗线 / 粗弧锯齿很明显，圆环上最扎眼，
 *          所以这里一定优先 GDI+
 */
void cg_skin_draw_ring(cg_widget_t *w, HDC hdc, const RECT *rc)
{
    int width, height, scale, thick, cx, cy, radius, span, val, sweep;
    unsigned fg, trackc;
    GpGraphics *g = NULL;

    if (!w) return;
    width  = rc->right - rc->left;
    height = rc->bottom - rc->top;
    if (width <= 0 || height <= 0) return;

    scale = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    thick = (width < 80 ? 6 : (width < 160 ? 10 : 14)) * scale / 100;
    if (thick < 2) thick = 2;
    cx = (rc->left + rc->right) / 2;
    cy = (rc->top + rc->bottom) / 2;
    radius = ((width < height ? width : height) - thick) / 2 - 2 * scale / 100;
    if (radius < 4) return;

    span = (w->vmax > w->vmin) ? (w->vmax - w->vmin) : 1;
    val = w->value;
    if (val < w->vmin) val = w->vmin;
    if (val > w->vmax) val = w->vmax;
    sweep = (int)((long long)(val - w->vmin) * 360 / span);
    fg = (w->style.fg != CG_COLOR_NONE) ? w->style.fg : 0x6750A4u;
    /* 轨道是中性灰 ⇒ 跟着主题走（否则深色下是一圈发亮的浅环）。填充色（弧）仍是
     * style.fg 或 M3 主色，**不**跟随 —— 与进度条/仪表盘同一口径。 */
    trackc = cg_skin_neutral(w->app, 0xE0DAE6u, 0x2E2B33u);

    /* 底：设了背景色用它，否则用"背后"的颜色 */
    {
        HBRUSH bb = CreateSolidBrush(sk_colorref((w->style.bg != CG_COLOR_NONE) ? w->style.bg
                                                                                : sk_behind(w)));
        FillRect(hdc, rc, bb);
        DeleteObject(bb);
    }

    if (GdipCreateFromHDC(hdc, &g) == SK_GDIP_OK && g) {
        REAL x = (REAL)(cx - radius), y = (REAL)(cy - radius), d = (REAL)(radius * 2);
        GpPen *pen = NULL;

        GdipSetSmoothingMode(g, SK_SMOOTH_AA);

        /* 轨道：整圈（椭圆路径描边 —— GdipDrawArc 画 360° 在部分实现上会退化） */
        {
            GpPath *p = NULL;
            if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
                GdipAddPathEllipse(p, x, y, d, d);
                if (GdipCreatePen1(sk_argb(255, trackc), (REAL)thick, SK_UNIT_PIXEL, &pen) == SK_GDIP_OK && pen) {
                    GdipDrawPath(g, pen, p);
                    GdipDeletePen(pen);
                    pen = NULL;
                }
                GdipDeletePath(p);
            }
        }
        /* 进度弧：12 点方向顺时针，圆头收尾 */
        if (sweep > 0 &&
            GdipCreatePen1(sk_argb(255, fg), (REAL)thick, SK_UNIT_PIXEL, &pen) == SK_GDIP_OK && pen) {
            GdipSetPenStartCap(pen, SK_CAP_ROUND);
            GdipSetPenEndCap(pen, SK_CAP_ROUND);
            GdipDrawArc(g, pen, x, y, d, d, -90.0f, (REAL)sweep);
            GdipDeletePen(pen);
            pen = NULL;
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI（无抗锯齿） */
        HPEN pen = CreatePen(PS_SOLID, thick, sk_colorref(trackc));
        HGDIOBJ oldpen = SelectObject(hdc, pen);
        MoveToEx(hdc, cx + radius, cy, NULL);
        AngleArc(hdc, cx, cy, radius, 0.0f, 359.9f);
        SelectObject(hdc, oldpen);
        DeleteObject(pen);

        if (sweep > 0) {
            POINT pts[SK_RING_STEPS + 1];
            int steps = SK_RING_STEPS * sweep / 360;
            int k;
            if (steps < 1) steps = 1;
            if (steps > SK_RING_STEPS) steps = SK_RING_STEPS;
            for (k = 0; k <= steps; k++) {
                double a = -SK_PI / 2 + (SK_PI / 180) * (double)sweep * (double)k / (double)steps;
                pts[k].x = cx + (int)((double)radius * cos(a) + 0.5);
                pts[k].y = cy + (int)((double)radius * sin(a) + 0.5);
            }
            pen = CreatePen(PS_SOLID, thick, sk_colorref(fg));
            oldpen = SelectObject(hdc, pen);
            Polyline(hdc, pts, steps + 1);
            SelectObject(hdc, oldpen);
            DeleteObject(pen);
        }
    }
}

/**
 * @brief 自绘 owner-draw 按钮（圆角填充 / 背景图 / 悬停按下蒙版 / 居中文字）
 * @param app 应用（取缩放，并用绘制项里的控件句柄反查控件）
 * @param dis 绘制项（控件句柄 / 矩形 / 状态 / DC 都从这里取）
 * @return void 无返回值；反查不到控件或矩形为空时直接返回，不画
 * @details 背景图（源路径指定的位图）优先：有图就不再铺纯色，悬停 / 按下 / 禁用改为在图上
 *          叠一层半透明蒙版；文字色在无图时按底色亮度自动黑 / 白，有图且设了前景色时听用户的。
 *          GDI+ 起不来则回退 GDI 圆角矩形（有背景图时图已经画上了）
 */
void cg_skin_draw_button(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    cg_widget_t *w = cg_widget_by_hwnd(app, dis->hwndItem);
    RECT rc;
    int x, y, wpx, hpx, scale;
    unsigned base, fill, textcol, border, winbg;
    int filled, disabled, pressed, hot, has_border;
    int rad;
    int has_img = 0;
    BITMAP bm;
    GpGraphics *g = NULL;

    if (!w) return;
    rc = dis->rcItem;
    x = rc.left; y = rc.top;
    wpx = rc.right - rc.left;
    hpx = rc.bottom - rc.top;
    if (wpx <= 0 || hpx <= 0) return;

    scale = (app && app->scale > 0) ? app->scale : 100;
    winbg = sk_behind(w);

    /* 背景图（src 指定时）：有就代替纯色填充（位图由 cg_win_sync.c 的 sync_bitmap 按需加载） */
    if (w->bitmap && GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm) &&
        bm.bmWidth > 0 && bm.bmHeight > 0) {
        has_img = 1;
    }

    filled   = (w->style.bg != CG_COLOR_NONE);
    base     = filled ? w->style.bg : 0xFFFFFFu;
    disabled = (dis->itemState & ODS_DISABLED) ? 1 : 0;
    pressed  = (dis->itemState & ODS_SELECTED) ? 1 : 0;
    if (w->pressed) pressed = 1;
    hot      = w->hot;

    has_border = 0;
    border = 0;
    if (disabled) {
        /* 禁用态的灰原本写死浅色系 —— 深底上一块发亮的浅灰（深色皮肤 T1）。
         * 文字色直接复用 sk_default_text 的禁用分支（深色 0x6E6E72 有先例）。 */
        if (filled) {
            fill = cg_skin_neutral(w->app, 0xE2E2E4u, 0x2E2B33u);
            textcol = sk_default_text(w, 1);
        } else {
            fill = cg_skin_neutral(w->app, 0xECECEEu, 0x2E2B33u);
            border = cg_skin_neutral(w->app, 0xDCDCDEu, 0x49454Fu); has_border = 1;
            textcol = sk_default_text(w, 1);
        }
    } else if (filled) {
        /* 实心主题色按钮：文字按底色亮度自动黑/白 */
        if (pressed)      fill = sk_shade(base, 0.86f);
        else if (hot)     fill = sk_shade(base, 0.94f);
        else              fill = base;
        textcol = sk_luminance(base) < 0.55f ? 0xFFFFFFu : 0x000000u;
    } else {
        /* 默认（未设 style.bg）：M3 tonal 填充按钮，与画布预览一致（#EADDFF）。
         * 无描边——1px 灰边在实机上又重又脏，画布/渲染效果差异最大的一处。 */
        if (pressed)      fill = sk_shade(0xEADDFFu, 0.90f);
        else if (hot)     fill = sk_shade(0xEADDFFu, 0.95f);
        else              fill = 0xEADDFFu;
        textcol = 0x1D1B20u;
    }
    /* 背景图上文字颜色没法按底色亮度自动算（图是用户挑的）：设了前景色就听用户的 */
    if (has_img && w->style.fg != CG_COLOR_NONE) textcol = w->style.fg;

    rad = (w->style.radius > 0 ? w->style.radius : 6) * scale / 100;   /* 画布按钮默认 6px */
    if (rad > wpx / 2) rad = wpx / 2;
    if (rad > hpx / 2) rad = hpx / 2;

    /* ---- 先用窗口底色清整块区域：圆角外/上一帧的残留都会导致黑边 ---- */
    {
        HBRUSH b = CreateSolidBrush(sk_colorref(winbg));
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, b);
        PatBlt(dis->hDC, x, y, wpx, hpx, PATCOPY);
        SelectObject(dis->hDC, ob);
        DeleteObject(b);
    }

    /* ---- 背景图：拉伸铺满整块，裁进圆角里（文字稍后画在图上） ---- */
    if (has_img) {
        HDC    mem  = CreateCompatibleDC(dis->hDC);
        HGDIOBJ oldb = SelectObject(mem, (HGDIOBJ)w->bitmap);
        HRGN   rgn  = CreateRoundRectRgn(x, y, x + wpx + 1, y + hpx + 1, rad * 2, rad * 2);
        SelectClipRgn(dis->hDC, rgn);
        SetStretchBltMode(dis->hDC, HALFTONE);
        SetBrushOrgEx(dis->hDC, 0, 0, NULL);
        StretchBlt(dis->hDC, x, y, wpx, hpx, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectClipRgn(dis->hDC, NULL);
        DeleteObject(rgn);
        SelectObject(mem, oldb);
        DeleteDC(mem);
    }

    /* ---- 圆角填充（GDI+，抗锯齿） ---- */
    if (GdipCreateFromHDC(dis->hDC, &g) == SK_GDIP_OK && g) {
        GpPath *p = NULL;
        GdipSetSmoothingMode(g, SK_SMOOTH_AA);
        if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
            sk_round_rect(p, (REAL)x, (REAL)y, (REAL)wpx, (REAL)hpx, (REAL)rad);
            {
                GpSolidFill *br = NULL;
                if (!has_img &&
                    GdipCreateSolidFill(sk_argb(255, fill), &br) == SK_GDIP_OK && br) {
                    GdipFillPath(g, (GpBrush *)br, p);
                    GdipDeleteBrush((GpBrush *)br);
                }
            }
            if (has_border) {
                GpPen *bd = NULL;
                if (GdipCreatePen1(sk_argb(255, border), 1.0f, SK_UNIT_PIXEL, &bd) == SK_GDIP_OK && bd) {
                    GdipDrawPath(g, bd, p);
                    GdipDeletePen(bd);
                }
            }
            if (has_img) {
                /* 图上叠一层蒙版给悬停/按下/禁用反馈（半透明，图还看得见） */
                unsigned veil = disabled ? sk_argb(150, 0xECECEEu)
                              : pressed   ? sk_argb(60, 0x000000u)
                              : hot       ? sk_argb(35, 0x000000u) : 0u;
                if (veil) {
                    GpSolidFill *vr = NULL;
                    if (GdipCreateSolidFill(veil, &vr) == SK_GDIP_OK && vr) {
                        GdipFillPath(g, (GpBrush *)vr, p);
                        GdipDeleteBrush((GpBrush *)vr);
                    }
                }
            }
            GdipDeletePath(p);
        }
        GdipDeleteGraphics(g);
    } else if (!has_img) {
        /* 回退：GDI 圆角矩形，保证按钮永不消失（有背景图时图已经画上了） */
        HBRUSH br = CreateSolidBrush(sk_colorref(fill));
        HPEN   pn = CreatePen(PS_SOLID, 1, sk_colorref(has_border ? border : fill));
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, br);
        HPEN   op = (HPEN)SelectObject(dis->hDC, pn);
        RoundRect(dis->hDC, x, y, x + wpx, y + hpx, rad * 2, rad * 2);
        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        DeleteObject(br);
        DeleteObject(pn);
    }

    /* ---- 文字用 GDI 画在 GDI+ 之上（同个 DC，GDI+ 已结束绘制） ---- */
    if (w->text && *w->text) {
        wchar_t *wt = sk_to_wcs(w->text);
        if (wt) {
            RECT tr = rc;
            int m = (int)(4 * scale / 100);
            InflateRect(&tr, -m, -m);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, sk_colorref(textcol));
            DrawTextW(dis->hDC, wt, -1, &tr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
    }
}

/**
 * @brief 画开关：右侧胶囊轨道 + 白色圆钮
 * @param w 开关控件（读 checked / enabled / hot / pressed / text 与应用的缩放）
 * @param hdc 目标 DC
 * @param rc 客户区矩形（物理像素）
 * @return void 无返回值；GDI+ 不可用时回退 GDI 圆角矩形 + 椭圆
 * @details 规格对齐画布（轨道 2:1、钮内缩 3px）；左侧文字画在**透明底**上，
 *          颜色走默认文字色，深色下必须换成浅字
 */
void cg_skin_draw_switch(cg_widget_t *w, HDC hdc, const RECT *rc)
{
    int on, disabled, scale, hot, pressed;
    int x, y, wpx, hpx, th, tw, inset, knob, kx, ky;
    unsigned track_fill, knob_fill = 0xFFFFFFu, winbg;
    GpGraphics *g = NULL;

    if (!w) return;
    x = rc->left; y = rc->top;
    wpx = rc->right - rc->left;
    hpx = rc->bottom - rc->top;
    if (wpx <= 0 || hpx <= 0) return;
    on       = w->checked ? 1 : 0;
    disabled = w->enabled ? 0 : 1;
    scale    = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    hot      = w->hot ? 1 : 0;
    pressed  = w->pressed ? 1 : 0;
    winbg    = sk_behind(w);

    /* 整块填窗口底色（圆角外的残留/owner-draw 默认灰底都盖掉） */
    {
        HBRUSH b = CreateSolidBrush(sk_colorref(winbg));
        HBRUSH ob = (HBRUSH)SelectObject(hdc, b);
        PatBlt(hdc, x, y, wpx, hpx, PATCOPY);
        SelectObject(hdc, ob);
        DeleteObject(b);
    }

    /* 轨道：高 = 控件高 - 8（画布 32 高里放 24 高轨道），宽 = 2 倍高，右对齐 */
    th = hpx - 8 * scale / 100;
    if (th > 24 * scale / 100) th = 24 * scale / 100;
    if (th < 12) th = 12;
    tw = th * 2;
    if (tw > wpx) tw = wpx;
    inset = (hpx - th) / 2;
    knob  = th - 6 * scale / 100;
    if (knob < 8) knob = 8;
    {
        int tspace = (tw - knob) / 2;
        if (inset > tspace) inset = tspace;
    }
    /* 钮贴在轨道内左右两端，各留 3px 边距（画布同款） */
    {
        int tl = x + wpx - inset - tw;          /* 轨道左缘 */
        int m = 3 * scale / 100;
        if (m * 2 + knob > tw) m = (tw - knob) / 2;
        kx = on ? (tl + tw - m - knob) : (tl + m);
    }
    ky = y + (hpx - knob) / 2;

    if (on)            track_fill = 0x6750A4u;                     /* on：主题紫 */
    else if (disabled) track_fill = cg_skin_neutral(w->app, 0xC9C5D0u, 0x3D3945u);
    else if (pressed)  track_fill = sk_shade(0x9A949Eu, 0.85f);
    else if (hot)      track_fill = sk_shade(0x9A949Eu, 0.92f);
    else               track_fill = cg_skin_neutral(w->app, 0x9A949Eu, 0x49454Fu);   /* off：画布同款灰（深色给深灰） */

    /* 轨道 + 圆钮（GDI+ 抗锯齿；挂了回退 GDI） */
    if (GdipCreateFromHDC(hdc, &g) == SK_GDIP_OK && g) {
        GpPath *p = NULL;
        GdipSetSmoothingMode(g, SK_SMOOTH_AA);
        if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
            GpSolidFill *bf = NULL;
            sk_round_rect(p, (REAL)(x + wpx - inset - tw), (REAL)(y + inset),
                          (REAL)tw, (REAL)th, (REAL)(th / 2));
            if (GdipCreateSolidFill(sk_argb(255, track_fill), &bf) == SK_GDIP_OK && bf) {
                GdipFillPath(g, (GpBrush *)bf, p);
                GdipDeleteBrush((GpBrush *)bf);
            }
            GdipDeletePath(p);
        }
        {
            GpPath *kp = NULL;
            if (GdipCreatePath(SK_FILL_WINDING, &kp) == SK_GDIP_OK && kp) {
                GpSolidFill *kf = NULL;
                GdipAddPathEllipse(kp, (REAL)kx, (REAL)ky, (REAL)knob, (REAL)knob);
                if (GdipCreateSolidFill(sk_argb(255, knob_fill), &kf) == SK_GDIP_OK && kf) {
                    GdipFillPath(g, (GpBrush *)kf, kp);
                    GdipDeleteBrush((GpBrush *)kf);
                }
                GdipDeletePath(kp);
            }
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI（原实现） */
        HBRUSH br = CreateSolidBrush(sk_colorref(track_fill));
        HGDIOBJ ob = SelectObject(hdc, br);
        HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
        RoundRect(hdc, x + wpx - inset - tw, y + inset,
                  x + wpx - inset, y + inset + th, th, th);
        SelectObject(hdc, ob);
        DeleteObject(br);
        br = CreateSolidBrush(sk_colorref(knob_fill));
        ob = SelectObject(hdc, br);
        Ellipse(hdc, kx, ky, kx + knob, ky + knob);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(br);
    }

    /* 文字（左侧，垂直居中） */
    if (w->text && *w->text) {
        wchar_t *wt = sk_to_wcs(w->text);
        if (wt) {
            RECT tr = *rc;
            tr.right = x + wpx - tw - 6 * scale / 100;
            SetBkMode(hdc, TRANSPARENT);
            /* 这里的字画在**透明底**上（= cg_win_behind_rgb 那套"背后色"），所以深色下必须
             * 换成浅字，否则复选/单选/开关的文字就是深底暗字（2026-09-27 探针截图抓到的）。
             * 注意**不要**顺手改上面按钮那句 textcol：那是画在 M3 浅紫填充上的字，
             * 深色下按钮填充仍是浅紫，字就该保持深色。 */
            SetTextColor(hdc, sk_colorref(sk_default_text(w, disabled)));
            DrawTextW(hdc, wt, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
    }
}

/**
 * @brief 自绘复选 / 单选：圆角方框 + 白勾，或圆环 + 实心点
 * @param app 应用（取缩放，并用绘制项里的控件句柄反查控件）
 * @param dis 绘制项（控件句柄 / 矩形 / 状态 / DC 都从这里取）
 * @return void 无返回值；反查不到控件或矩形为空时直接返回
 * @details 圆角与禁用态都按状态走（禁用 + 选中用降饱和紫，别和可用态混淆）；文字跟在方框右边，
 *          颜色走默认文字色。选中态用控件自己的 checked —— owner-draw 下
 *          绘制项里的选中位不跟着设勾命令走
 */
void cg_skin_draw_check(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    cg_widget_t *w = cg_widget_by_hwnd(app, dis->hwndItem);
    RECT rc = dis->rcItem;
    int x = rc.left, y = rc.top;
    int wpx = rc.right - rc.left, hpx = rc.bottom - rc.top;
    int scale, disa, on, radio, rad, box, pad, bx, by;
    int hot, pressed;
    unsigned winbg;

    if (!w || wpx <= 0 || hpx <= 0) return;
    scale = (app && app->scale > 0) ? app->scale : 100;
    disa  = (dis->itemState & ODS_DISABLED) ? 1 : 0;
    on    = w->checked ? 1 : 0;   /* owner-draw 下 ODS_CHECKED 不跟着 BM_SETCHECK 走，用控件自己的 checked */
    radio = !strcmp(w->type, "radio");
    hot     = w->hot ? 1 : 0;
    pressed = (w->pressed || (dis->itemState & ODS_SELECTED)) ? 1 : 0;
    rad   = (w->style.radius > 0 ? w->style.radius : 6) * scale / 100;

    box = (hpx >= 24 ? 18 : 14) * scale / 100;
    if (box > hpx - 4) box = hpx - 4;
    if (box < 10) box = 10;
    pad = (hpx - box) / 2;
    bx = x + (int)(2 * scale / 100);
    by = y + pad;
    winbg = sk_behind(w);

    /* 1) 整块填"背后"的底色，挡掉 owner-draw 按钮默认灰底 */
    {
        HBRUSH b = CreateSolidBrush(sk_colorref(winbg));
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, b);
        PatBlt(dis->hDC, x, y, wpx, hpx, PATCOPY);
        SelectObject(dis->hDC, ob);
        DeleteObject(b);
    }

    /* 2) 圆角方框 / 圆 + 选中标记 */
    {
        GpGraphics *g = NULL;
        GpPath *p = NULL;
        if (GdipCreateFromHDC(dis->hDC, &g) == SK_GDIP_OK && g) {
            GdipSetSmoothingMode(g, SK_SMOOTH_AA);
            if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
                unsigned fill, border = 0;
                if (radio) GdipAddPathEllipse(p, (REAL)bx, (REAL)by, (REAL)box, (REAL)box);
                else       sk_round_rect(p, (REAL)bx, (REAL)by, (REAL)box, (REAL)box, (REAL)rad);

                if (on && disa)       fill = 0xBEB5CFu;   /* 禁用+选中：降饱和紫，别和可用态混淆 */
                else if (on)          fill = pressed ? sk_shade(0x6750A4u, 0.86f) : 0x6750A4u;
                else if (disa)      { fill = cg_skin_neutral(app, 0xE2E2E4u, 0x2E2B33u); border = 0xDCDCDEu; }
                else if (pressed)   { fill = 0xEADDFFu; border = 0x6750A4u; }
                else if (hot)       { fill = 0xF6F2FAu; border = 0x6750A4u; }
                else                { fill = cg_skin_neutral(app, 0xFFFFFFFFu, 0x1D1B20u); border = 0x79747Eu; }

                {
                    GpSolidFill *bf = NULL;
                    if (GdipCreateSolidFill(sk_argb(255, fill), &bf) == SK_GDIP_OK && bf) {
                        GdipFillPath(g, (GpBrush *)bf, p);
                        GdipDeleteBrush((GpBrush *)bf);
                    }
                }
                if (border) {
                    GpPen *bd = NULL;
                    if (GdipCreatePen1(sk_argb(255, border), 1.0f, SK_UNIT_PIXEL, &bd) == SK_GDIP_OK && bd) {
                        GdipDrawPath(g, bd, p);
                        GdipDeletePen(bd);
                    }
                }
                if (on) {
                    if (radio) {
                        GpPath *dp = NULL;
                        int rr = box / 3;
                        if (GdipCreatePath(SK_FILL_WINDING, &dp) == SK_GDIP_OK && dp) {
                            GpSolidFill *wf = NULL;
                            GdipAddPathEllipse(dp, (REAL)(bx + box / 2 - rr / 2),
                                                (REAL)(by + box / 2 - rr / 2), (REAL)rr, (REAL)rr);
                            if (GdipCreateSolidFill(sk_argb(255, 0xFFFFFFFFu), &wf) == SK_GDIP_OK && wf) {
                                GdipFillPath(g, (GpBrush *)wf, dp);
                                GdipDeleteBrush((GpBrush *)wf);
                            }
                            GdipDeletePath(dp);
                        }
                    } else {
                        GpPen *wp = NULL;
                        if (GdipCreatePen1(sk_argb(255, 0xFFFFFFFFu), 2.0f, SK_UNIT_PIXEL, &wp) == SK_GDIP_OK && wp) {
                            GdipDrawLine(g, wp, (REAL)(bx + box * 0.26), (REAL)(by + box * 0.52),
                                            (REAL)(bx + box * 0.42), (REAL)(by + box * 0.70));
                            GdipDrawLine(g, wp, (REAL)(bx + box * 0.42), (REAL)(by + box * 0.70),
                                            (REAL)(bx + box * 0.74), (REAL)(by + box * 0.32));
                            GdipDeletePen(wp);
                        }
                    }
                }
                GdipDeletePath(p);
            }
            GdipDeleteGraphics(g);
        }
    }

    /* 3) 文字（左对齐，跟在方框右边） */
    if (w->text && *w->text) {
        RECT tr = rc;
        tr.left = bx + box + (int)(6 * scale / 100);
        wchar_t *wt = sk_to_wcs(w->text);
        if (wt) {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, sk_colorref(sk_default_text(w, disa)));
            DrawTextW(dis->hDC, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
    }
}

/**
 * @brief 滑块自绘通知：把轨道和滑块画成圆角，返回给通知消息的标志
 * @param app 应用（取明暗，中性灰轨道跟它走）
 * @param hdr 通知头（按绘制阶段 / 项标识分派到轨道与钮）
 * @return LRESULT 绘制标志；本函数画过的项返回"跳过默认绘制"，其余返回"按默认绘制"
 * @note 只请求 item 通知：**不**在这里填背景。2026-09-27 实测过两条路（预绘制里填"背后色"；
 *       再补"跳过默认绘制"），像素断言显示**一个像素都没变** —— 原生 trackbar 那层主题灰底
 *       不是从这里画上去的，所以拦不住。彻底解决要像进度条那样把控件换成自绘窗口类
 *       （连输入一起接管），没做；文档里按"槽跟随、画布底不跟随"如实写着
 */
LRESULT cg_skin_draw_slider_notify(cg_app_t *app, const NMHDR *hdr)
{
    const NMCUSTOMDRAW *cd = (const NMCUSTOMDRAW *)hdr;
    int rw, rh;
    /* 只请求 item 通知：**不**在这里填背景。
     * 2026-09-27 实测过两条路（PREPAINT 里填"背后色"；再补 CDRF_SKIPDEFAULT），
     * 像素断言显示**一个像素都没变** —— 原生 trackbar 那层主题灰底不是从这里画上去的，
     * 所以拦不住。彻底解决要像 `progress` 那样把控件换成自绘窗口类（连输入一起接管），
     * 没做；文档里按"槽跟随、画布底不跟随"如实写着。 */
    if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;

    rw = cd->rc.right - cd->rc.left;
    rh = cd->rc.bottom - cd->rc.top;

    if (cd->dwItemSpec == TBCD_CHANNEL) {
        GpGraphics *g = NULL;
        GpPath *p = NULL;
        if (GdipCreateFromHDC(cd->hdc, &g) == SK_GDIP_OK && g) {
            GdipSetSmoothingMode(g, SK_SMOOTH_AA);
            if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
                GpSolidFill *bf = NULL;
                sk_round_rect(p, (REAL)cd->rc.left, (REAL)cd->rc.top, (REAL)rw, (REAL)rh, (REAL)(rh / 2));
                if (GdipCreateSolidFill(sk_argb(255, cg_skin_neutral(app, 0xE7E0ECu, 0x2E2B33u)), &bf) == SK_GDIP_OK && bf) {
                    GdipFillPath(g, (GpBrush *)bf, p);
                    GdipDeleteBrush((GpBrush *)bf);
                }
                GdipDeletePath(p);
            }
            GdipDeleteGraphics(g);
        } else {
            /* 回退：GDI 圆角矩形。GDI+ 起不来时（理论情况）至少把轨道画出来，
             * 否则返回 CDRF_SKIPDEFAULT 又不画 = 滑块轨道整条消失（2026-09-25 审查发现） */
            HBRUSH br = CreateSolidBrush(sk_colorref(cg_skin_neutral(app, 0xE7E0ECu, 0x2E2B33u)));
            HBRUSH ob = (HBRUSH)SelectObject(cd->hdc, br);
            RoundRect(cd->hdc, cd->rc.left, cd->rc.top, cd->rc.right, cd->rc.bottom, rh, rh);
            SelectObject(cd->hdc, ob);
            DeleteObject(br);
        }
        return CDRF_SKIPDEFAULT;
    }
    if (cd->dwItemSpec == TBCD_THUMB) {
        GpGraphics *g = NULL;
        GpPath *p = NULL;
        int r = rw < rh ? rw : rh;
        if (GdipCreateFromHDC(cd->hdc, &g) == SK_GDIP_OK && g) {
            GdipSetSmoothingMode(g, SK_SMOOTH_AA);
            if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
                GpSolidFill *bf = NULL;
                sk_round_rect(p, (REAL)cd->rc.left, (REAL)cd->rc.top, (REAL)rw, (REAL)rh, (REAL)(r / 2));
                if (GdipCreateSolidFill(sk_argb(255, 0x6750A4u), &bf) == SK_GDIP_OK && bf) {
                    GdipFillPath(g, (GpBrush *)bf, p);
                    GdipDeleteBrush((GpBrush *)bf);
                }
                GdipDeletePath(p);
            }
            GdipDeleteGraphics(g);
        } else {
            /* 回退：GDI 圆角矩形（同轨道，避免 GDI+ 挂掉时滑块钮消失） */
            HBRUSH br = CreateSolidBrush(sk_colorref(0x6750A4u));
            HBRUSH ob = (HBRUSH)SelectObject(cd->hdc, br);
            RoundRect(cd->hdc, cd->rc.left, cd->rc.top, cd->rc.right, cd->rc.bottom, r, r);
            SelectObject(cd->hdc, ob);
            DeleteObject(br);
        }
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

/**
 * @brief 画进度条：圆角轨道 + 填充条，与画布预览一致
 * @param w 进度条控件（读 value/vmin/vmax、底色 / 前景色与应用的暗色标志）
 * @param hdc 目标 DC
 * @param rc 客户区矩形（物理像素）
 * @return void 无返回值；GDI+ 不可用时回退 GDI 圆角矩形（轨道中性灰、填充同一口径）
 * @details 进度条不走原生控件 —— Win32 的 PROGRESS_CLASS 不支持 custom draw（开不开主题都不发通知），
 *          所以整个控件换成运行时自绘类（在 cg_win_draw_canvas.c），这里只管往 DC 上画。
 *          填充色以前景色优先（没设才是主色，与圆环 / 仪表盘 / 状态灯同一口径），
 *          不是写死的"主题紫"；轨道与描边是中性灰，随应用的明暗走
 */
void cg_skin_draw_progress(cg_widget_t *w, HDC hdc, const RECT *rc)
{
    int x, y, wp, hp, rad, fw;
    float frac;
    GpGraphics *g = NULL;
    GpPath *p = NULL;

    if (!w) return;
    x = rc->left; y = rc->top;
    wp = rc->right - rc->left;
    hp = rc->bottom - rc->top;
    if (wp <= 0 || hp <= 0) return;

    /* 先按规则清底（设了背景色用它，否则用"背后"的颜色）：圆角外的残留才对得上 */
    {
        HBRUSH bb = CreateSolidBrush(sk_colorref((w->style.bg != CG_COLOR_NONE)
                                                     ? w->style.bg : sk_behind(w)));
        FillRect(hdc, rc, bb);
        DeleteObject(bb);
    }

    rad = 3;
    if (rad > hp / 2) rad = hp / 2;
    frac = (w->vmax > w->vmin)
         ? (float)(w->value - w->vmin) / (float)(w->vmax - w->vmin) : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    fw = (int)((wp - 2) * frac);

    if (GdipCreateFromHDC(hdc, &g) == SK_GDIP_OK && g) {
        GdipSetSmoothingMode(g, SK_SMOOTH_AA);
        if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
            GpSolidFill *bf = NULL;
            GpPen *bd = NULL;
            sk_round_rect(p, (REAL)x, (REAL)y, (REAL)wp, (REAL)hp, (REAL)rad);
            if (GdipCreateSolidFill(sk_argb(255, cg_skin_neutral(w->app, 0xE6E1E9u, 0x2E2B33u)), &bf) == SK_GDIP_OK && bf) {
                GdipFillPath(g, (GpBrush *)bf, p);
                GdipDeleteBrush((GpBrush *)bf);
            }
            if (GdipCreatePen1(sk_argb(255, cg_skin_neutral(w->app, 0xCAC4D0u, 0x3D3945u)), 1.0f, SK_UNIT_PIXEL, &bd) == SK_GDIP_OK && bd) {
                GdipDrawPath(g, bd, p);
                GdipDeletePen(bd);
            }
            GdipDeletePath(p);
        }
        if (fw > 0) {
            GpPath *fp = NULL;
            if (GdipCreatePath(SK_FILL_WINDING, &fp) == SK_GDIP_OK && fp) {
                GpSolidFill *ff = NULL;
                sk_round_rect(fp, (REAL)(x + 1), (REAL)(y + 1), (REAL)fw, (REAL)(hp - 2), (REAL)rad);
                /* 填充色：设了 style.fg 就用它（与 progressring / meter / led 同一口径），
                 * 否则用 M3 主色。这里以前写死 —— 于是画布上给进度条设"文字色/主色"
                 * 看不出任何变化（2026-09-23 字段覆盖审计发现）。 */
                if (GdipCreateSolidFill(sk_argb(255, (w->style.fg != CG_COLOR_NONE) ? w->style.fg : 0x6750A4u), &ff) == SK_GDIP_OK && ff) {
                    GdipFillPath(g, (GpBrush *)ff, fp);
                    GdipDeleteBrush((GpBrush *)ff);
                }
                GdipDeletePath(fp);
            }
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI 圆角矩形 */
        /* GDI 回退分支要与 GDI+ 主路径取同一套槽底/描边值（深浅色都一致）；
         * 填充 fb 是品牌紫，按口径不跟随主题 */
        HBRUSH bg = CreateSolidBrush(sk_colorref(cg_skin_neutral(w->app, 0xE6E1E9u, 0x2E2B33u)));
        HBRUSH fb = CreateSolidBrush(sk_colorref((w->style.fg != CG_COLOR_NONE) ? w->style.fg : 0x6750A4u));
        HPEN   dp = CreatePen(PS_SOLID, 1, sk_colorref(cg_skin_neutral(w->app, 0xCAC4D0u, 0x3D3945u)));
        HBRUSH ob = (HBRUSH)SelectObject(hdc, bg);
        HPEN   op = (HPEN)SelectObject(hdc, dp);
        RoundRect(hdc, x, y, x + wp, y + hp, rad * 2, rad * 2);
        SelectObject(hdc, fb);
        if (fw > 0) RoundRect(hdc, x + 1, y + 1, x + 1 + fw, y + hp - 1, rad * 2, rad * 2);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(bg);
        DeleteObject(fb);
        DeleteObject(dp);
    }
}
