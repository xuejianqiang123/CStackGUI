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

/** 取窗口底色（控件外区域用它填充，避免 owner-draw 自带的灰底） */
static unsigned sk_winbg(cg_app_t *app)
{
    if (app && app->doc.bg != CG_COLOR_NONE) return app->doc.bg;
    return 0xF3EDF7u;
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
    winbg = sk_winbg(app);

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
        /* 默认（未设 style.bg）：M3 tonal 填充按钮，与画布预览一致（#EADDFF） */
        if (pressed)      fill = sk_shade(0xEADDFFu, 0.90f);
        else if (hot)     fill = sk_shade(0xEADDFFu, 0.95f);
        else              fill = 0xEADDFFu;
        border = 0x79747Eu; has_border = 1;
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
    winbg    = sk_winbg(w->app);

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
    kx = on ? (x + wpx - inset - knob) : (x + inset);
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
            GdipAddPathEllipse(p, (REAL)(x + wpx - inset - tw), (REAL)(y + inset),
                               (REAL)tw, (REAL)th);
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
    winbg = sk_winbg(app);

    /* 1) 整块填窗口底色，挡掉 owner-draw 按钮默认灰底 */
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

/* ---- 输入框圆角：窗口区域裁切 + Subclass 在 resize 时重裁（EDIT 不支持 owner-draw） ---- */

typedef struct { int rad; } sk_edit_sub_t;

/** 在客户区四周画一圈圆角边框（聚焦时用主题紫），画在 DefSubclassProc 之后叠上去 */
static void sk_edit_border(HWND hwnd, const sk_edit_sub_t *s, int focus)
{
    RECT rc;
    HDC dc;
    HPEN pn, op;
    HBRUSH ob;
    COLORREF col = focus ? 0x6750A4 : 0xB8B2BD;

    GetClientRect(hwnd, &rc);
    if (rc.right <= 2 || rc.bottom <= 2) return;
    dc = GetDC(hwnd);
    pn = CreatePen(PS_SOLID, 2, col);
    op = (HPEN)SelectObject(dc, pn);
    ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, rc.right, rc.bottom, s->rad * 2 + 4, s->rad * 2 + 4);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pn);
    ReleaseDC(hwnd, dc);
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
    if (msg == WM_SIZE) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, s->rad * 2, s->rad * 2);
            if (rgn) { SetWindowRgn(hwnd, rgn, TRUE); DeleteObject(rgn); }
            /* 多行输入框：客户端矩形内缩，文字离边框远一点（单行 EDIT 自带边距与垂直居中） */
            if (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) {
                RECT tr = rc;
                InflateRect(&tr, -5, -3);
                SendMessageW(hwnd, EM_SETRECT, 0, (LPARAM)&tr);
            }
        }
    } else if (msg == WM_PAINT || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        sk_edit_border(hwnd, s, GetFocus() == hwnd);
        return r;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void cg_skin_apply_edit(cg_widget_t *w)
{
    HWND hw = (HWND)w->native;
    int scale, r;
    sk_edit_sub_t *s;
    if (!hw) return;
    scale = (w->app && w->app->scale > 0) ? w->app->scale : 100;
    r = (w->style.radius > 0 ? w->style.radius : 2) * scale / 100;   /* 画布输入框默认 2px */
    if (r < 1) r = 1;
    s = (sk_edit_sub_t *)malloc(sizeof(sk_edit_sub_t));
    if (!s) return;
    s->rad = r;
    {
        RECT rc;
        GetClientRect(hw, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, r * 2, r * 2);
            if (rgn) { SetWindowRgn(hw, rgn, TRUE); DeleteObject(rgn); }
        }
    }
    /* 左右各留 6px 边距：文字别怼到圆角边框上 */
    SendMessageW(hw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
    SetWindowSubclass(hw, sk_edit_sub, 1, (DWORD_PTR)s);
}
