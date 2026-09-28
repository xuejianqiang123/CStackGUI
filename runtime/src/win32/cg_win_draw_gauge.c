/**
 * @file    cg_win_draw_gauge.c
 * @brief   自绘窗口类：状态灯（led）与仪表盘（meter）
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 抗锯齿基建
 * GDI 本身没有抗锯齿：Ellipse 画圆、粗笔画弧都是一圈硬边台阶（用户截图里的"锯齿"）。
 * 项目不引第三方绘图库（GDI+ 要 COM 初始化还全靠 float），所以用**超采样**补救：
 * 形状先画在 s 倍大小的内存位图上，再用 HALFTONE 缩回目标 DC —— 缩小时每个目标像素
 * 是 16 个源像素的平均，边缘就有了过渡色，肉眼上等于抗锯齿。项目里位图缩放本来
 * 就用 HALFTONE（见 cg_win_sync.c / cg_win_ownerdraw.c），这里是同一招的"先放大画"用法。
 * 文字不参与超采样：4x 画的字缩回来会发虚，所以文字一律在缩回后的 1x DC 上画。
 */
#define CG_GAUGE_SS 4

/**
 * @brief 形状绘制回调：dc 是 s 倍大小的内存 DC，rc 是 s 倍大小的矩形（在 s 倍坐标空间里画）
 * @param dc 放大后的内存 DC（当前绘制目标）
 * @param rc 放大后坐标空间里的绘制矩形
 * @param s 超采样倍数
 * @param ctx 调用方透传的上下文（应用与控件）
 * @return void 无返回值
 */
typedef void (*gauge_shape_fn)(HDC dc, const RECT *rc, int s, void *ctx);

/** @brief 传给形状回调的公共参数（背景取色要用 app，样式/状态要用 widget） */
typedef struct {
    cg_app_t    *app;
    cg_widget_t *w;
} gauge_ctx_t;

/**
 * @brief 超采样绘制：在 s 倍内存位图上调 fn 画形状，再按缩放采样缩回目标 DC 的矩形
 * @param dc 目标 DC（形状最终贴到它上面）
 * @param rc 目标 DC 上的落点与尺寸
 * @param fn 形状绘制回调（收到 s 倍大小的内存 DC 与 s 倍矩形）
 * @param ctx 透传给 fn 的上下文（应用与控件）
 * @return void 无返回值
 * @note 放大再缩回是为了拿到抗锯齿：GDI 直接画的粗弧 / 斜线锯齿很明显
 */
static void gauge_paint_ss(HDC dc, const RECT *rc, gauge_shape_fn fn, void *ctx)
{
    int     w  = rc->right - rc->left;
    int     h  = rc->bottom - rc->top;
    int     s  = CG_GAUGE_SS;
    HDC     mdc = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w * s, h * s);
    HGDIOBJ ob  = SelectObject(mdc, bmp);
    RECT    mrc;
    int     prev;

    mrc.left = 0; mrc.top = 0; mrc.right = w * s; mrc.bottom = h * s;
    fn(mdc, &mrc, s, ctx);

    prev = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchBlt(dc, rc->left, rc->top, w, h, mdc, 0, 0, w * s, h * s, SRCCOPY);
    SetStretchBltMode(dc, prev);

    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

/* ------------------------------------------------------------ 状态灯（led）
 * 自绘一个小圆点（或方块）+ 右边一句说明。闪烁**由控件自己驱动**：状态是"闪"就在 WM_PAINT 里
 * SetTimer（周期 = period；period < 100ms 时按 500ms 起，见 LedProc），其余状态 KillTimer ——
 * 业务只写 cg_led_set(w, 2)，于是也不存在"忘了关时钟"这种事。亮/灭的相位从 core 的
 * cg_led_lit() 取，用的是**绝对时间**，所以哪怕因为别的原因重画、把时钟重置了，
 * 闪的节奏也不会乱。
 */
#define CG_LED_TIMER 1

/**
 * @brief 状态灯的灯体（圆 / 方）：在 s 倍内存 DC 上画
 * @param dc s 倍大小的内存 DC（当前绘制目标）
 * @param rc s 倍坐标空间里的绘制矩形
 * @param s 超采样倍数（本文件固定值）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void led_body(HDC dc, const RECT *rc, int s, void *vctx)
{
    gauge_ctx_t *ctx = (gauge_ctx_t *)vctx;
    cg_widget_t *w = ctx->w;
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
                       : cgw_brush_for(cg_win_behind_rgb(ctx->app, (void *)w->native)));

    /* 亮 = style.fg（缺省绿）；灭 = 灰。 */
    /* 亮 = style.fg（缺省绿）；灭 = 中性灰 —— 那个灰要**跟着主题**：深底上还画一个亮灰点，
     * 会被读成"亮着"（2026-09-27 把这类中性灰一并收口时发现）。 */
    col = lit ? ((w->style.fg != CG_COLOR_NONE) ? cgw_to_colorref(w->style.fg) : RGB(0x1B, 0x6E, 0x33))
              : cgw_to_colorref(cg_skin_neutral(w->app, 0xC0BCC6u, 0x4A4750u));

    /* 缩 1px（s 倍）留点余量，免得贴到边界上被裁掉半像素 */
    box.left = rc->left + s;
    box.top = rc->top + (ch - side) / 2 + s;
    box.right = box.left + side - 2 * s;
    box.bottom = box.top + side - 2 * s;
    if (box.right - box.left < 2 * s || box.bottom - box.top < 2 * s) return;

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

/**
 * @brief 画状态灯：灯体走超采样，说明文字留在 1x DC 上画
 * @param app 所属应用（背后取色、明暗判定与字体）
 * @param w 状态灯控件（取值 / 闪烁周期 / 说明文字 / 样式）
 * @param dc 目标 DC（1x，文字直接画在它上面）
 * @param rc 客户区矩形
 * @return void 无返回值
 * @note 文字不跟着超采样：缩回来的字会发虚
 */
static void led_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    int cw = rc->right - rc->left;
    int ch = rc->bottom - rc->top;
    int side = (cw < ch) ? cw : ch;
    gauge_ctx_t ctx;

    ctx.app = app;
    ctx.w = w;
    gauge_paint_ss(dc, rc, led_body, &ctx);

    /* 有文字：灯在左边占一个正方形、文字在右边 */
    if (w->text && *w->text) {
        HFONT   f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 9, 0, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        wchar_t *wt = cgw_wc_from_utf8(w->text);
        int     oldmode = SetBkMode(dc, TRANSPARENT);
        RECT    tr = *rc;

        tr.left += side + 6;
        /* 灯右边的说明文字画在「背后色」上 ⇒ 深色下也得变浅，否则深底暗字看不清 */
        SetTextColor(dc, (app && app->dark) ? RGB(0xE6, 0xE0, 0xE9) : RGB(0x1D, 0x1B, 0x20));
        if (wt) {
            DrawTextW(dc, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            free(wt);
        }
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }
}

/**
 * @brief 状态灯窗口过程：只管重画（闪态自己开时钟驱动），不处理鼠标
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
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

/**
 * @brief 注册状态灯的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
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
 * 三角函数**不引 math.h**：自带一张 0..90° 的 Q1000 正弦表（91 个整数：0°~90° 各一个，象限折叠），
 * 与项目"必经路径上不放 float"的一贯做法一致，也省得为链接 -lm 操心。
 * 全部形状（弧/刻度/指针/圆帽）都在 s 倍内存位图上画、缩回，粗笔和圆弧的硬边台阶就变成了
 * 过渡色；只有中心数值文字留在 1x DC 上画。
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

/**
 * @brief 度数 -> 放大 1000 倍的正弦值（任意角度，靠象限折叠）
 * @param deg 角度（度，可负、可超过 360）
 * @return int 正弦值乘 1000（-1000 ~ 1000）
 */
static int meter_sin1000(int deg)
{
    int d = deg % 360;

    if (d < 0) d += 360;
    if (d <= 90)  return k_sin1000[d];
    if (d <= 180) return k_sin1000[180 - d];
    if (d <= 270) return -k_sin1000[d - 180];
    return -k_sin1000[360 - d];
}

/**
 * @brief 角度 -> 圆上一点（顺时针、0° 指向 +x 轴）
 * @param cx 圆心 x
 * @param cy 圆心 y
 * @param r 半径
 * @param deg 角度（度，顺时针，0° 指向 +x 轴）
 * @param p 出参：算出的点（调用方提供存储）
 * @return void 无返回值
 */
static void meter_pt(int cx, int cy, int r, int deg, POINT *p)
{
    p->x = cx + r * meter_sin1000(90 - deg) / 1000;   /* cos(d) = sin(90-d) */
    p->y = cy + r * meter_sin1000(deg) / 1000;
}

/**
 * @brief 用折线近似画一段弧（from 起、扫 sweep 度）
 * @param dc 目标 DC（用当前笔色与笔宽）
 * @param cx 圆心 x
 * @param cy 圆心 y
 * @param r 半径
 * @param from 起始角度（度，顺时针）
 * @param sweep 扫过的角度（度，正数即顺时针）
 * @param segs 折线段数（内部夹到 2 ~ 64）
 * @return void 无返回值
 */
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

/**
 * @brief 仪表盘的全部形状（弧 / 刻度 / 指针 / 圆帽）：在 s 倍内存 DC 上画
 * @param dc s 倍大小的内存 DC（当前绘制目标）
 * @param rc s 倍坐标空间里的绘制矩形
 * @param s 超采样倍数（本文件固定值）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void meter_body(HDC dc, const RECT *rc, int s, void *vctx)
{
    gauge_ctx_t *ctx = (gauge_ctx_t *)vctx;
    cg_widget_t *w = ctx->w;
    int      cw = rc->right - rc->left;
    int      ch = rc->bottom - rc->top;
    int      cx = rc->left + cw / 2;
    int      cy = rc->top + ch / 2 - 2 * s;
    int      r  = ((cw < ch) ? cw : ch) / 2 - 8 * s;
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
                       : cgw_brush_for(cg_win_behind_rgb(ctx->app, (void *)w->native)));
    if (r < 12 * s) return;                    /* 太小了画出来只是噪点，干脆不画 */

    /* 底弧（灰、细） */
    pen = CreatePen(PS_SOLID, s, RGB(0xB4, 0xB0, 0xBB));
    old = SelectObject(dc, pen);
    meter_arc(dc, cx, cy, r, from, sweep, 48);
    SelectObject(dc, old);
    DeleteObject(pen);

    /* 值弧（粗、主色）：从起点画到当前值 */
    if (done > 0) {
        pen = CreatePen(PS_SOLID, 4 * s, main);
        old = SelectObject(dc, pen);
        meter_arc(dc, cx, cy, r, from, done, done / 6 + 2);
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    /* 刻度：11 条等分短线 */
    pen = CreatePen(PS_SOLID, s, RGB(0x9A, 0x96, 0xA2));
    old = SelectObject(dc, pen);
    for (i = 0; i <= 10; i++) {
        POINT a, b;
        int   t = from + sweep * i / 10;
        meter_pt(cx, cy, r - 3 * s, t, &a);
        meter_pt(cx, cy, r - 9 * s, t, &b);
        MoveToEx(dc, a.x, a.y, NULL);
        LineTo(dc, b.x, b.y);
    }
    SelectObject(dc, old);
    DeleteObject(pen);

    /* 指针（圆心 -> 0.78r）+ 圆心小圆帽 */
    pen = CreatePen(PS_SOLID, 2 * s, RGB(0x49, 0x45, 0x4F));
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
        Ellipse(dc, cx - 4 * s, cy - 4 * s, cx + 4 * s, cy + 4 * s);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(br);
    }
}

/**
 * @brief 画整个仪表盘：形状走超采样，中心数值文字留在 1x DC 上画
 * @param app 所属应用（背后取色与字体）
 * @param w 仪表盘控件（取值 / 单位文字 / 样式）
 * @param dc 目标 DC（1x，数值文字直接画在它上面）
 * @param rc 客户区矩形
 * @return void 无返回值
 */
static void meter_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    int      cw = rc->right - rc->left;
    int      ch = rc->bottom - rc->top;
    int      cy = rc->top + ch / 2 - 2;
    int      r  = ((cw < ch) ? cw : ch) / 2 - 8;
    gauge_ctx_t ctx;

    ctx.app = app;
    ctx.w = w;
    gauge_paint_ss(dc, rc, meter_body, &ctx);
    if (r < 12) return;

    /* 中心大数值 + 单位（放在圆心下面：270° 弧的下方和半圆样式的下半都是空的） */
    {
        wchar_t wtx[64];
        HFONT   f = cgw_font_for(app, w->style.font,
                                 w->style.size ? w->style.size + 6 : 14, 1, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        int     oldmode = SetBkMode(dc, TRANSPARENT);
        int     val = cg_widget_value(w);
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
        /* 中心数值也要跟随主题：这里原来是写死的近黑，深色盘面上一片黑读不出来（2026-09-28
         * 排查发现，与圆环中心文字同一毛病）。深色取主题浅字，浅色保持原来的近黑 */
        {
            unsigned tfg = cgw_theme_text(app);

            SetTextColor(dc, tfg != CG_COLOR_NONE ? cgw_to_colorref(tfg) : RGB(0x1D, 0x1B, 0x20));
        }
        DrawTextW(dc, wtx, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }
}

/**
 * @brief 仪表盘窗口过程（纯显示：没有鼠标、没有时钟）
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
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

/**
 * @brief 注册仪表盘的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
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
