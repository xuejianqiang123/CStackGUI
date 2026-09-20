/**
 * @file    cg_skin_edit.c
 * @brief   输入框外观：圆角裁切 + 单行文字垂直居中（EDIT 不支持 owner-draw，只能靠子类）
 * @details 单行 EDIT 的文字默认贴着客户区顶部：用 WM_NCCALCSIZE 把客户区上边让出一段，
 *          文字就落到中间。让出来的那条上边距成了非客户区，由我们自己在 WM_PAINT 里用底色补上，
 *          否则会露出窗口底色。字体是控件建好后才 WM_SETFONT 设进去的，所以还要在 WM_SETFONT
 *          时按实际字号重算一次。
 *          边框画在客户区之外（客户区四边由 WM_NCCALCSIZE 内缩 bw），
 *          所以控件自身重绘（选中 / 滚动 / 光标闪烁）永远盖不掉它。
 */
/* 明确走 W 系列 API：必须在 windows.h 之前定义 */
#define UNICODE
#define _UNICODE

#include "cg_skin.h"

#include <windows.h>
#include <commctrl.h>   /* SetWindowSubclass / DefSubclassProc */
#include <stdlib.h>
#include <string.h>

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
