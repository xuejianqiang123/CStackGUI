/**
 * @file    cg_win_draw_gauge.c
 * @brief   自绘窗口类：状态灯（led）与仪表盘（meter）
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 状态灯（led）
 * 自绘一个小圆点（或方块）+ 右边一句说明。闪烁**由控件自己驱动**：状态是"闪"就在 WM_PAINT 里
 * SetTimer（周期 = period），其余状态 KillTimer —— 业务只写 cg_led_set(w, 2)，于是也不存在
 * "忘了关时钟"这种事。亮/灭的相位从 core 的 cg_led_lit() 取，用的是**绝对时间**，
 * 所以哪怕因为别的原因重画、把时钟重置了，闪的节奏也不会乱。
 */
#define CG_LED_TIMER 1

/** @brief 画状态灯：圆/方 + 可选的右边说明文字 */
static void led_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    int      cw = rc->right - rc->left;
    int      ch = rc->bottom - rc->top;
    int      side = (cw < ch) ? cw : ch;
    int      lit = cg_led_lit(w, (unsigned long)GetTickCount());
    int      round = !(w->format && strcmp(w->format, "square") == 0);
    COLORREF col;
    RECT     box;
    HBRUSH   br;

    /* 底：先铺满，不然圆/方之外露出来的是脏画面 */
    FillRect(dc, rc, (w->style.bg != CG_COLOR_NONE)
                       ? cgw_brush_for(w->style.bg)
                       : cgw_brush_for(cg_win_behind_rgb(app, (void *)w->native)));

    /* 亮 = style.fg（缺省绿）；灭 = 灰。 */
    col = lit ? ((w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg) : RGB(0x1B, 0x6E, 0x33))
              : RGB(0xC0, 0xBC, 0xC6);

    /* 有文字：灯在左边占一个正方形、文字在右边；没文字：灯占满整个矩形 */
    if (w->text && *w->text) {
        HFONT   f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 9, 0, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        wchar_t *wt = cgw_wc_from_utf8(w->text);
        int     oldmode = SetBkMode(dc, TRANSPARENT);
        RECT    tr = *rc;

        tr.left += side + 6;
        SetTextColor(dc, RGB(0x1D, 0x1B, 0x20));
        if (wt) {
            DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }

    /* 缩 1px 留点余量，免得贴到边界上被裁掉半像素 */
    box.left = rc->left + 1;
    box.top = rc->top + (ch - side) / 2 + 1;
    box.right = box.left + side - 2;
    box.bottom = box.top + side - 2;
    if (box.right - box.left < 2 || box.bottom - box.top < 2) return;

    br = CreateSolidBrush(col);
    if (round) {
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
    } else {
        FillRect(dc, &box, br);
    }
    DeleteObject(br);
}

/** @brief 状态灯窗口过程：只管重画，不处理鼠标（纯显示控件） */
static LRESULT CALLBACK LedProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);

    (void)wp;
    (void)lp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);           /* 闪的那半拍：重画一次看亮/灭 */
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        /* "闪"就把时钟开着，其它状态关掉。相位是按绝对时间算的，所以这里重复 SetTimer
         * 只会重置计时、不会让灯停在某一相位。 */
        if (w && w->value == 2) {
            unsigned ms = (w->period >= 100) ? (unsigned)w->period : 500;
            SetTimer(hwnd, CG_LED_TIMER, ms, NULL);
        } else {
            KillTimer(hwnd, CG_LED_TIMER);
        }
        if (w) {
            led_paint(app, w, dc, &rc);
        } else {
            FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));   /* 还没挂上运行时的那一帧 */
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册状态灯的自绘窗口类（整进程一次） */
void cgw_register_led_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = LedProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_LED_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------------ 仪表盘（meter）
 * 底弧（灰）+ 值弧（粗、彩色，随值增长）+ 刻度线 + 指针 + 中心大数值 + 单位。
 * 表盘要"顺时针"走，而 GDI 的 AngleArc/Arc 是逆时针、还要按端点坐标算，所以干脆自己按角度
 * 算点、用 Polyline 连 —— 可控且能脱窗单测（角度算法在 core 的 cg_meter_angle_deg）。
 * 三角函数**不引 math.h**：自带一张 0..90° 的 Q1000 正弦表（90 个整数，象限折叠），
 * 与项目"必经路径上不放 float"的一贯做法一致，也省得为链接 -lm 操心。
 */
static const short k_sin1000[91] = {
       0,   17,   35,   52,   70,   87,  105,  122,  139,  156,  174,  191,  208,  225,  242,
     259,  276,  292,  309,  326,  342,  358,  375,  391,  407,  423,  438,  454,  469,  485,
     500,  515,  530,  545,  559,  574,  588,  602,  616,  629,  643,  656,  669,  682,  695,
     707,  719,  731,  743,  755,  766,  777,  788,  799,  809,  819,  829,  839,  848,  857,
     866,  875,  883,  891,  899,  906,  914,  921,  927,  934,  940,  946,  951,  956,  961,
     966,  970,  974,  978,  982,  985,  988,  990,  993,  995,  996,  998,  999,  999, 1000,
    1000,
};

/** @brief 度数 -> Q1000 的正弦值（任意角度；表只存 0..90，其余靠象限折叠） */
static int meter_sin1000(int deg)
{
    int d = deg % 360;

    if (d < 0) d += 360;
    if (d <= 90)  return k_sin1000[d];
    if (d <= 180) return k_sin1000[180 - d];
    if (d <= 270) return -k_sin1000[d - 180];
    return -k_sin1000[360 - d];
}

/** @brief 角度 -> 圆上一点（顺时针、0° = +x 轴；屏幕 y 向下为正，所以 sin 直接相加） */
static void meter_pt(int cx, int cy, int r, int deg, POINT *p)
{
    p->x = cx + r * meter_sin1000(90 - deg) / 1000;   /* cos(d) = sin(90-d) */
    p->y = cy + r * meter_sin1000(deg) / 1000;
}

/** @brief 用折线近似画一段弧（from 起、扫 sweep 度） */
static void meter_arc(HDC dc, int cx, int cy, int r, int from, int sweep, int segs)
{
    POINT pts[65];
    int   i, n = 0;

    if (segs > 64) segs = 64;
    if (segs < 2) segs = 2;
    for (i = 0; i <= segs; i++) {
        meter_pt(cx, cy, r, from + sweep * i / segs, &pts[n]);
        n++;
    }
    Polyline(dc, pts, n);
}

/** @brief 画整个仪表盘 */
static void meter_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    int      cw = rc->right - rc->left;
    int      ch = rc->bottom - rc->top;
    int      cx = (rc->left + rc->right) / 2;
    int      cy = rc->top + ch / 2 - 2;
    int      r  = ((cw < ch) ? cw : ch) / 2 - 8;
    int      half = (w->format && strcmp(w->format, "half") == 0);
    int      from = half ? 180 : 135;
    int      sweep = half ? 180 : 270;
    int      val = cg_widget_value(w);
    int      ang = cg_meter_angle_deg(w, val);
    int      done = ang - from;
    int      i;
    HPEN     pen;
    HGDIOBJ  old;
    COLORREF main = (w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg)
                                                   : RGB(0x67, 0x50, 0xA4);

    FillRect(dc, rc, (w->style.bg != CG_COLOR_NONE)
                       ? cgw_brush_for(w->style.bg)
                       : cgw_brush_for(cg_win_behind_rgb(app, (void *)w->native)));
    if (r < 12) return;                        /* 太小了画出来只是噪点，干脆不画 */

    /* 底弧（灰、细） */
    pen = CreatePen(PS_SOLID, 1, RGB(0xB4, 0xB0, 0xBB));
    old = SelectObject(dc, pen);
    meter_arc(dc, cx, cy, r, from, sweep, 48);
    SelectObject(dc, old);
    DeleteObject(pen);

    /* 值弧（粗、主色）：从起点画到当前值 */
    if (done > 0) {
        pen = CreatePen(PS_SOLID, 4, main);
        old = SelectObject(dc, pen);
        meter_arc(dc, cx, cy, r, from, done, done / 6 + 2);
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    /* 刻度：11 条等分短线 */
    pen = CreatePen(PS_SOLID, 1, RGB(0x9A, 0x96, 0xA2));
    old = SelectObject(dc, pen);
    for (i = 0; i <= 10; i++) {
        POINT a, b;
        int   t = from + sweep * i / 10;
        meter_pt(cx, cy, r - 3, t, &a);
        meter_pt(cx, cy, r - 9, t, &b);
        MoveToEx(dc, a.x, a.y, NULL);
        LineTo(dc, b.x, b.y);
    }
    SelectObject(dc, old);
    DeleteObject(pen);

    /* 指针（圆心 -> 0.78r）+ 圆心小圆帽 */
    pen = CreatePen(PS_SOLID, 2, RGB(0x49, 0x45, 0x4F));
    old = SelectObject(dc, pen);
    {
        POINT b;
        meter_pt(cx, cy, r * 78 / 100, ang, &b);
        MoveToEx(dc, cx, cy, NULL);
        LineTo(dc, b.x, b.y);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    {
        HBRUSH  br = CreateSolidBrush(RGB(0x49, 0x45, 0x4F));
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - 4, cy - 4, cx + 4, cy + 4);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(br);
    }

    /* 中心大数值 + 单位（放在圆心下面：270° 弧的下方和半圆样式的下半都是空的） */
    {
        wchar_t wtx[64];
        HFONT   f = cgw_font_for(app, w->style.font,
                                 w->style.size ? w->style.size + 6 : 14, 1, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        int     oldmode = SetBkMode(dc, TRANSPARENT);
        RECT    tr;

        if (w->text && *w->text) {
            wchar_t *wu = cgw_wc_from_utf8(w->text);
            _snwprintf(wtx, 63, L"%d %ls", val, wu ? wu : L"");
            if (wu) free(wu);
        } else {
            _snwprintf(wtx, 63, L"%d", val);
        }
        tr.left = rc->left;
        tr.right = rc->right;
        tr.top = cy + r * 30 / 100;
        tr.bottom = tr.top + 28;
        SetTextColor(dc, RGB(0x1D, 0x1B, 0x20));
        DrawTextW(dc, wtx, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }
}

/** @brief 仪表盘窗口过程（纯显示：没有鼠标、没有时钟） */
static LRESULT CALLBACK MeterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);

    (void)wp;
    (void)lp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (w) {
            meter_paint(app, w, dc, &rc);
        } else {
            FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册仪表盘的自绘窗口类（整进程一次） */
void cgw_register_meter_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MeterProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_METER_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}
