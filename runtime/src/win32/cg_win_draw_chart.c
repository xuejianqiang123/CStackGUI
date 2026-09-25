/**
 * @file    cg_win_draw_chart.c
 * @brief   自绘窗口类：折线 / 柱状图（chart）
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 折线 / 柱状图（chart）
 * 全自绘（GDI，不走 GDI+）：网格 + 折线（批量 Polyline）/ 柱 + 图例 + 轴上下的范围文字。
 * 为什么要说的三件事：
 *   1. **点数超过绘图区像素宽度时不抽点**，而是对每个像素列取该列覆盖点的 min/max 画一条
 *      竖线（LVGL 的做法）—— 既不慢，也不会刚好把那个尖峰抽掉。
 *   2. 折线用一次 **Polyline** 批量提交（比逐段 LineTo 快约一半），且数据线不做抗锯齿。
 *   3. 这里不自己定时：实时性靠业务用 cg_set_timer 定周期 cg_chart_push，画的时候只读现成数组。
 */
/** 折线路径的栈缓冲上限（点数超过它、或超过像素宽度就走按列 min/max） */
#define CG_CHART_MAX_PTS 1024

/** 系列调色板（8 个够用；.cgui 不配颜色，按系列序取） */
static const COLORREF k_chart_colors[8] = {
    RGB(0x67, 0x50, 0xA4), RGB(0x1B, 0x6E, 0x33), RGB(0xC0, 0x4A, 0x2B),
    RGB(0x14, 0x5C, 0xA8), RGB(0xB0, 0x7A, 0x00), RGB(0x7A, 0x4E, 0x8A),
    RGB(0x00, 0x6B, 0x6B), RGB(0x8C, 0x4A, 0x4A),
};

/** @brief 值 -> 绘图区里的 y 像素（超范围按边界裁剪，所以越界数据不会画出框） */
static int chart_map_y(const cg_widget_t *w, int v, int top, int bottom)
{
    int range = w->vmax - w->vmin;

    if (range <= 0) range = 1;
    if (v < w->vmin) v = w->vmin;
    if (v > w->vmax) v = w->vmax;
    return bottom - (int)((long long)(v - w->vmin) * (bottom - top) / range);
}

/** @brief 第 i 个点落在哪一列（右对齐：最新一个点在最右边） */
static int chart_column(int i, int fill, int pw)
{
    if (fill <= 1 || pw <= 1) return 0;
    return (pw - 1) - (int)((long long)(fill - 1 - i) * (pw - 1) / (fill - 1));
}

/** @brief 画一个系列（折线或柱） */
static void chart_draw_series(HDC dc, const cg_widget_t *w, int series,
                              const RECT *plot, COLORREF col, int bars)
{
    int fill = cg_chart_filled(w, series);
    int pw   = plot->right - plot->left;
    int i;

    if (fill <= 0 || pw < 2) return;

    if (bars) {
        HBRUSH br = CreateSolidBrush(col);
        int step = (pw - 1) / (fill > 1 ? fill - 1 : 1);
        int bw = step > 2 ? step - 1 : 1;
        for (i = 0; i < fill; i++) {
            int x = plot->right - (fill - 1 - i) * step;
            int y = chart_map_y(w, cg_chart_point(w, series, i), plot->top + 1, plot->bottom - 1);
            RECT r;
            r.left = x; r.right = x + bw;
            r.top = y;  r.bottom = plot->bottom;
            if (r.right > plot->right + 1) r.right = plot->right + 1;
            if (r.left < plot->left) continue;
            FillRect(dc, &r, br);
        }
        DeleteObject(br);
        return;
    }

    if (fill <= pw && fill <= CG_CHART_MAX_PTS) {
        /* 稀疏：一条折线整批画出去 */
        POINT pts[CG_CHART_MAX_PTS];
        int   n = 0;
        int   step = (pw - 1) / (fill > 1 ? fill - 1 : 1);
        HPEN  pen;
        HGDIOBJ old;

        if (step < 1) step = 1;
        for (i = 0; i < fill; i++) {
            int x = plot->right - (fill - 1 - i) * step;
            if (x < plot->left) break;              /* 左边界外的不画，免得压到边框和刻度上 */
            pts[n].x = x;
            pts[n].y = chart_map_y(w, cg_chart_point(w, series, i), plot->top + 1, plot->bottom - 1);
            n++;
        }
        if (n >= 2) {
            pen = CreatePen(PS_SOLID, 1, col);
            old = SelectObject(dc, pen);
            Polyline(dc, pts, n);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        return;
    }

    /* 密集：按像素列取 min/max 画竖线 —— 不抽点，尖峰不会被抽掉 */
    {
        HPEN    pen = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ old = SelectObject(dc, pen);
        int     c = 0;

        for (i = 0; i < fill; i++) {
            int lo, hi, y1, y2, v;

            while (i < fill && chart_column(i, fill, pw) < c) i++;
            if (i >= fill) break;
            lo = hi = cg_chart_point(w, series, i);
            while (i < fill && chart_column(i, fill, pw) == c) {
                v = cg_chart_point(w, series, i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                i++;
            }
            i--;                                    /* 外层循环还要 +1 */
            y1 = chart_map_y(w, hi, plot->top + 1, plot->bottom - 1);
            y2 = chart_map_y(w, lo, plot->top + 1, plot->bottom - 1);
            MoveToEx(dc, plot->left + c, y1, NULL);
            LineTo(dc, plot->left + c, y2 == y1 ? y1 + 1 : y2);
            c++;
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

/** @brief 画整个图表：底、网格、边框、各系列、范围文字、图例 */
static void chart_paint(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    RECT  plot;
    int   right_pad = 44;                    /* 给图例留的地方（有系列名时） */
    int   n = cg_chart_series_count(w);
    int   bars = (w->format && strcmp(w->format, "bar") == 0);
    int   i;
    HBRUSH bg, grid;
    HPEN  frame;
    HGDIOBJ oldpen;

    if (w->style.bg != CG_COLOR_NONE) bg = cgw_brush_for(w->style.bg);
    else                              bg = cgw_brush_for(cg_win_behind_rgb(app, (void *)w->native));
    FillRect(dc, rc, bg);

    /* 绘图区：留 28px 给左边的刻度文字、8px 内边距；有名字才给右边留图例位 */
    plot.left = rc->left + 30;
    plot.top = rc->top + 8;
    plot.right = rc->right - 8;
    plot.bottom = rc->bottom - 8;
    if (w->nitems > 0) plot.right -= right_pad;
    if (plot.right - plot.left < 8 || plot.bottom - plot.top < 8) return;

    /* 网格：4 横 3 竖（浅色，别抢数据的视线） */
    grid = cgw_brush_for(0xE7E0EC);
    for (i = 1; i < 4; i++) {
        RECT r;
        r.left = plot.left; r.right = plot.right;
        r.top = plot.top + (plot.bottom - plot.top) * i / 4;
        r.bottom = r.top + 1;
        FillRect(dc, &r, grid);
    }
    for (i = 1; i < 3; i++) {
        RECT r;
        r.top = plot.top; r.bottom = plot.bottom;
        r.left = plot.left + (plot.right - plot.left) * i / 3;
        r.right = r.left + 1;
        FillRect(dc, &r, grid);
    }

    /* 各系列 */
    for (i = 0; i < n; i++) {
        chart_draw_series(dc, w, i, &plot, k_chart_colors[i % 8], bars);
    }

    /* 边框 */
    frame = CreatePen(PS_SOLID, 1, RGB(0xB4, 0xB0, 0xBB));
    oldpen = SelectObject(dc, frame);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, plot.left, plot.top, plot.right, plot.bottom);
    SelectObject(dc, oldpen);
    DeleteObject(frame);

    /* 范围文字（上边界/下边界）+ 图例 */
    {
        HFONT  f = cgw_font_for(app, w->style.font, w->style.size ? w->style.size : 8, 0, 0);
        HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
        wchar_t buf[32];
        RECT    t;
        int     oldmode = SetBkMode(dc, TRANSPARENT);

        SetTextColor(dc, RGB(0x49, 0x45, 0x4F));
        _snwprintf(buf, 31, L"%d", w->vmax);
        t.left = rc->left + 2; t.right = plot.left - 4; t.top = plot.top - 4; t.bottom = plot.top + 14;
        DrawTextW(dc, buf, -1, &t, DT_RIGHT | DT_SINGLELINE);
        _snwprintf(buf, 31, L"%d", w->vmin);
        t.top = plot.bottom - 14; t.bottom = plot.bottom + 4;
        DrawTextW(dc, buf, -1, &t, DT_RIGHT | DT_SINGLELINE);

        for (i = 0; i < n && i < w->nitems; i++) {
            RECT lr;
            int  ly = plot.top + 2 + i * 16;
            lr.left = plot.right + 6; lr.right = plot.right + 14;
            lr.top = ly + 5;          lr.bottom = ly + 11;
            FillRect(dc, &lr, cgw_brush_for((unsigned)k_chart_colors[i % 8]));
            SetTextColor(dc, RGB(0x1D, 0x1B, 0x20));
            lr.left = plot.right + 18; lr.right = rc->right - 2;
            lr.top = ly; lr.bottom = ly + 16;
            {
                wchar_t *wn = cgw_wc_from_utf8(w->items[i] ? w->items[i] : "");
                if (wn) {
                    DrawTextW(dc, wn, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    free(wn);
                }
            }
        }
        SetBkMode(dc, oldmode);
        if (of) SelectObject(dc, of);
    }
}

/** @brief 图表双缓冲回调参数 */
typedef struct { cg_app_t *app; cg_widget_t *w; } chart_ctx_t;

/** @brief 图表一帧适配器：整张画进内存位图 */
static void chart_body(HDC mem, const RECT *rc, void *vctx)
{
    chart_ctx_t *c = (chart_ctx_t *)vctx;
    chart_paint(c->app, c->w, mem, rc);
}

/** @brief 图表窗口过程（自绘，纯显示；没绑任何事件） */
static LRESULT CALLBACK ChartProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t    *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    (void)wp;
    (void)lp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        chart_ctx_t ctx;
        GetClientRect(hwnd, &rc);
        ctx.app = app;
        ctx.w = cgw_widget_by_hwnd(app, hwnd);
        if (ctx.w) {
            /* 双缓冲：实时图表被 cg_chart_push 高频刷新，多笔直绘会闪 */
            cgw_paint_buffered(dc, &rc, chart_body, &ctx);
        } else {
            /* 控件还没挂上运行时的那一帧：填个系统面色就行，别去问"背后的颜色" */
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

/** @brief 注册图表的自绘窗口类（整进程一次） */
void cgw_register_chart_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChartProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_CHART_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}
