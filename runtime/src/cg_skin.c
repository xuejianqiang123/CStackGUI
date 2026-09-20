/**
 * @file    cg_skin.c
 * @brief   自绘外观层（M3 风格）：用 GDI+ 画圆角按钮 + 主题色 + 状态
 * @details 这是自绘后端的第一个可替换内核。所有绘制都靠 widget_by_hwnd() 反查控件，
 *          不直接碰 Win32 控件创建逻辑，所以换一套外观时这里改完即可，上层零改动。
 *          GDI+ 已经被 image / canvas 控件初始化（cg_win32.c 的 win_init 调 GdiplusStartup），
 *          本文件只负责在 WM_DRAWITEM 时往 DC 上画；GDI+ 万一没起来，自动回退 GDI RoundRect。
 */
#include "cg_skin.h"

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>   /* NMCUSTOMDRAW / CDRF_* / TBCD_* / SetWindowSubclass */
#include <stdlib.h>
#include <string.h>

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

/* 0xAARRGGBB */
static unsigned int sk_argb(unsigned char a, unsigned rgb)
{
    return ((unsigned int)a << 24) | (rgb & 0xFFFFFFu);
}

/** 0xRRGGBB -> COLORREF（GDI 文本用） */
static COLORREF sk_colorref(unsigned rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/** 把颜色按系数变亮/变暗（f<1 变暗，f>1 变亮） */
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

/** 相对亮度（0..1），用于决定文字黑/白 */
static float sk_luminance(unsigned c)
{
    float r = (float)((c >> 16) & 0xFF) / 255.0f;
    float g = (float)((c >> 8) & 0xFF) / 255.0f;
    float b = (float)(c & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/** UTF-8 -> UTF-16（MultiByteToWideChar，CP_UTF8） */
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

/** 往 GDI+ Path 里加一个圆角矩形（四角用圆弧） */
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

/** 控件"背后"是什么颜色（0xRRGGBB）：自绘控件要清掉自身底色时拿它填充。
 *  不能一律填窗口底色 —— 控件在带「背景色」的分组框/容器块里时，那块地方是面板的颜色，
 *  填窗口底色就会露出一块方块（表现为"分组框背景色没生效、反而控件变色"）。 */
static unsigned sk_behind(const cg_widget_t *w)
{
    if (!w || !w->app) return 0xF3EDF7u;
    return cg_win_behind_rgb(w->app, w->native);
}

void cg_skin_draw_button(cg_app_t *app, const DRAWITEMSTRUCT *dis)
{
    cg_widget_t *w = cg_widget_by_hwnd(app, dis->hwndItem);
    RECT rc;
    int x, y, wpx, hpx, scale;
    unsigned base, fill, textcol, border, winbg;
    int filled, disabled, pressed, hot, has_border;
    int rad;
    GpGraphics *g = NULL;

    if (!w) return;
    rc = dis->rcItem;
    x = rc.left; y = rc.top;
    wpx = rc.right - rc.left;
    hpx = rc.bottom - rc.top;
    if (wpx <= 0 || hpx <= 0) return;

    scale = (app && app->scale > 0) ? app->scale : 100;
    winbg = sk_behind(w);

    filled   = (w->style.bg != CG_COLOR_NONE);
    base     = filled ? w->style.bg : 0xFFFFFFu;
    disabled = (dis->itemState & ODS_DISABLED) ? 1 : 0;
    pressed  = (dis->itemState & ODS_SELECTED) ? 1 : 0;
    if (w->pressed) pressed = 1;
    hot      = w->hot;

    has_border = 0;
    border = 0;
    if (disabled) {
        if (filled) {
            fill = 0xE2E2E4u;
            textcol = 0x9A9A9Eu;
        } else {
            fill = 0xECECEEu;
            border = 0xDCDCDEu; has_border = 1;
            textcol = 0x9A9A9Eu;
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

    /* ---- 圆角填充（GDI+，抗锯齿） ---- */
    if (GdipCreateFromHDC(dis->hDC, &g) == SK_GDIP_OK && g) {
        GpPath *p = NULL;
        GdipSetSmoothingMode(g, SK_SMOOTH_AA);
        if (GdipCreatePath(SK_FILL_WINDING, &p) == SK_GDIP_OK && p) {
            sk_round_rect(p, (REAL)x, (REAL)y, (REAL)wpx, (REAL)hpx, (REAL)rad);
            {
                GpSolidFill *br = NULL;
                if (GdipCreateSolidFill(sk_argb(255, fill), &br) == SK_GDIP_OK && br) {
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
            GdipDeletePath(p);
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI 圆角矩形，保证按钮永不消失 */
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

/** M3 开关：右侧胶囊轨道 + 白色圆钮，规格对齐画布（track 2:1、钮内缩 3px） */
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
    else if (disabled) track_fill = 0xC9C5D0u;
    else if (pressed)  track_fill = sk_shade(0x9A949Eu, 0.85f);
    else if (hot)      track_fill = sk_shade(0x9A949Eu, 0.92f);
    else               track_fill = 0x9A949Eu;                     /* off：画布同款灰 */

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
            SetTextColor(hdc, sk_colorref(disabled ? 0x9A9A9Eu : 0x1D1B20u));
            DrawTextW(hdc, wt, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
    }
}

/** M3 复选/单选：圆角方框 + 勾，或圆环 + 实心点；圆角与禁用态都按状态走 */
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
                else if (disa)      { fill = 0xE2E2E4u; border = 0xDCDCDEu; }
                else if (pressed)   { fill = 0xEADDFFu; border = 0x6750A4u; }
                else if (hot)       { fill = 0xF6F2FAu; border = 0x6750A4u; }
                else                { fill = 0xFFFFFFFFu; border = 0x79747Eu; }

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
            SetTextColor(dis->hDC, sk_colorref(disa ? 0x9A9A9Eu : 0x1C1B1Fu));
            DrawTextW(dis->hDC, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
    }
}

/** 滑块 NM_CUSTOMDRAW：把轨道和滑块画成 M3 圆角；返回给 WM_NOTIFY 的 CDRF 标志 */
LRESULT cg_skin_draw_slider_notify(cg_app_t *app, const NMHDR *hdr)
{
    const NMCUSTOMDRAW *cd = (const NMCUSTOMDRAW *)hdr;
    int rw, rh;
    (void)app;
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
                if (GdipCreateSolidFill(sk_argb(255, 0xE7E0ECu), &bf) == SK_GDIP_OK && bf) {
                    GdipFillPath(g, (GpBrush *)bf, p);
                    GdipDeleteBrush((GpBrush *)bf);
                }
                GdipDeletePath(p);
            }
            GdipDeleteGraphics(g);
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
        }
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

/** 进度条绘制：浅灰圆角轨道 + 主题紫填充，与画布预览一致。
 *  进度条不走原生控件——Win32 的 PROGRESS_CLASS 不支持 custom draw（开不开主题都不发通知），
 *  所以整个控件换成运行时自绘类（cg_win32.c 的 ProgressProc），这里只管往 DC 上画。 */
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
            if (GdipCreateSolidFill(sk_argb(255, 0xE6E1E9u), &bf) == SK_GDIP_OK && bf) {
                GdipFillPath(g, (GpBrush *)bf, p);
                GdipDeleteBrush((GpBrush *)bf);
            }
            if (GdipCreatePen1(sk_argb(255, 0xCAC4D0u), 1.0f, SK_UNIT_PIXEL, &bd) == SK_GDIP_OK && bd) {
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
                if (GdipCreateSolidFill(sk_argb(255, 0x6750A4u), &ff) == SK_GDIP_OK && ff) {
                    GdipFillPath(g, (GpBrush *)ff, fp);
                    GdipDeleteBrush((GpBrush *)ff);
                }
                GdipDeletePath(fp);
            }
        }
        GdipDeleteGraphics(g);
    } else {
        /* 回退：GDI 圆角矩形 */
        HBRUSH bg = CreateSolidBrush(sk_colorref(0xE6E1E9u));
        HBRUSH fb = CreateSolidBrush(sk_colorref(0x6750A4u));
        HBRUSH ob = (HBRUSH)SelectObject(hdc, bg);
        RoundRect(hdc, x, y, x + wp, y + hp, rad * 2, rad * 2);
        SelectObject(hdc, fb);
        if (fw > 0) RoundRect(hdc, x + 1, y + 1, x + 1 + fw, y + hp - 1, rad * 2, rad * 2);
        SelectObject(hdc, ob);
        DeleteObject(bg);
        DeleteObject(fb);
    }
}

/* ---- 输入框：圆角裁切 + 单行文字垂直居中（EDIT 不支持 owner-draw） ----
 * 单行 EDIT 的文字默认贴着客户区顶部：用 WM_NCCALCSIZE 把客户区上边让出一段，文字就落到中间。
 * 让出来的那条上边距成了非客户区，由我们自己在 WM_PAINT 里用底色补上，否则会露出窗口底色。
 * 字体是在控件建好后才 WM_SETFONT 设进去的，所以还要在 WM_SETFONT 时按实际字号重算一次。 */

typedef struct {
    int      rad;      /* 圆角半径 */
    int      single;   /* 单行（edit / spinner）：需要垂直居中；multiedit 保持顶部对齐 */
    int      pad;      /* 上方让出的像素（= (控件高 - 文字高) / 2） */
    int      text_h;   /* 当前字体的文字高度（换字号时对比，变了才重排） */
    int      w, h;     /* 控件整块尺寸（比客户区大，含边框与让出来的上边距） */
    int      bw;       /* 边框宽度：客户区四边各内缩这么多 */
    int      left;     /* 左边距（EM_SETMARGINS）；箭头/换字体后会被清，要按它重设 */
    COLORREF bg;       /* 补上边距用的底色（白或 style.bg） */
} sk_edit_sub_t;

/** 画上边距的底 + 圆角边框（聚焦时主题紫）。
 *  边框画在客户区之外（客户区已由 WM_NCCALCSIZE 四边内缩 bw），所以控件怎么重绘都盖不掉它。 */
static void sk_edit_box(HWND hwnd, const sk_edit_sub_t *s, int focus)
{
    HDC dc;
    HPEN pn, op;
    HBRUSH ob;
    COLORREF col = focus ? 0x6750A4 : 0xB8B2BD;
    int half = s->bw / 2;

    if (s->w <= 2 * s->bw || s->h <= 2 * s->bw) return;
    dc = GetWindowDC(hwnd);
    if (!dc) return;

    /* 让出来的上边距：填成输入框底色，免得露窗口底色 */
    if (s->pad > 0) {
        RECT top;
        HBRUSH fb;
        top.left = 0; top.top = 0; top.right = s->w; top.bottom = s->pad;
        fb = CreateSolidBrush(s->bg);
        FillRect(dc, &top, fb);
        DeleteObject(fb);
    }

    /* 路径从 half 起，2px 笔正好压在 [0, bw) 上（贴边画会被窗口区域裁掉一半） */
    pn = CreatePen(PS_SOLID, s->bw, col);
    op = (HPEN)SelectObject(dc, pn);
    ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, half, half, s->w - half, s->h - half, s->rad * 2, s->rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pn);
    ReleaseDC(hwnd, dc);
}

/** 取当前字体的文字行高（拿不到就是 0） */
static int sk_edit_text_height(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    TEXTMETRICW tm;
    int th = 0;
    if (!dc) return 0;
    memset(&tm, 0, sizeof(tm));
    if (GetTextMetricsW(dc, &tm)) th = tm.tmHeight;
    ReleaseDC(hwnd, dc);
    return th;
}

/** 重算整块尺寸 + 圆角区域；多行输入框顺带把客户区往里缩一点 */
static void sk_edit_resize(HWND hwnd, sk_edit_sub_t *s)
{
    RECT wr;
    GetWindowRect(hwnd, &wr);
    s->w = wr.right - wr.left;
    s->h = wr.bottom - wr.top;
    if (s->w > 0 && s->h > 0) {
        HRGN rgn = CreateRoundRectRgn(0, 0, s->w, s->h, s->rad * 2, s->rad * 2);
        if (rgn) { SetWindowRgn(hwnd, rgn, TRUE); DeleteObject(rgn); }
    }
    if (!s->single) {
        RECT tr;
        GetClientRect(hwnd, &tr);
        InflateRect(&tr, -5, -3);
        SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&tr);
    }
}

static LRESULT CALLBACK sk_edit_sub(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR data)
{
    sk_edit_sub_t *s = (sk_edit_sub_t *)data;

    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, sk_edit_sub, id);
        free(s);
        return DefSubclassProc(hwnd, msg, wp, lp);
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
    if (msg == WM_SETFONT) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        /* spinner 的上下箭头换字体时会重设 buddy 边距、把左边距清成 0：补一次 */
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(s->left, 0));
        if (s->single && sk_edit_text_height(hwnd) != s->text_h) {
            /* 字号变了 -> 重排一次（SWP_FRAMECHANGED 会再发 WM_NCCALCSIZE） */
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        return r;
    }
    if (msg == WM_SIZE) {
        sk_edit_resize(hwnd, s);
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(s->left, 0));
        /* resize 时系统按默认非客户区重算客户区，会把让出来的上边距抹掉（spinner 的
         * 上下箭头改 buddy 尺寸就会触发）。补一次 FRAMECHANGED 让 WM_NCCALCSIZE 重新生效；
         * 尺寸没变不会递归发 WM_SIZE。 */
        if (s->single)
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (msg == WM_PAINT || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LRESULT r;
        if (msg == WM_SETFOCUS)
            SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(s->left, 0));
        r = DefSubclassProc(hwnd, msg, wp, lp);
        sk_edit_box(hwnd, s, GetFocus() == hwnd);
        return r;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void cg_skin_apply_edit(cg_widget_t *w)
{
    HWND hw = (HWND)w->native;
    int scale, r, multiline;
    sk_edit_sub_t *s;
    if (!hw) return;
    scale = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    r = (w->style.radius > 0 ? w->style.radius : 2) * scale / 100;   /* 画布输入框默认 2px */
    if (r < 1) r = 1;
    multiline = (GetWindowLongW(hw, GWL_STYLE) & ES_MULTILINE) != 0;

    s = (sk_edit_sub_t *)malloc(sizeof(sk_edit_sub_t));
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->rad = r;
    s->single = multiline ? 0 : 1;
    s->bw = CG_EDIT_BORDER;
    s->left = CG_EDIT_TEXT_MARGIN;
    s->bg = (w->style.bg != CG_COLOR_NONE)
              ? RGB((w->style.bg >> 16) & 0xFF, (w->style.bg >> 8) & 0xFF, w->style.bg & 0xFF)
              : GetSysColor(COLOR_WINDOW);

    sk_edit_resize(hw, s);   /* 初始尺寸 + 圆角区域 */
    /* 左右留边距：文字别怼到圆角边框上（边框在客户区外，垂直居中见 WM_NCCALCSIZE）。
     * spinner 的箭头稍后会重设边距，WM_SETFONT/WM_SIZE 里会按 s->left 再补。 */
    SendMessageW(hw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(CG_EDIT_TEXT_MARGIN, CG_EDIT_TEXT_MARGIN));
    SetWindowSubclass(hw, sk_edit_sub, 1, (DWORD_PTR)s);
    /* 挂上子类后强制一次布局：立刻按当前字体算好垂直居中量 */
    SetWindowPos(hw, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
