/**
 * @file    cg_win_draw_canvas.c
 * @brief   自绘窗口类：image / 圆环进度 / 进度条 / 画板
 */
#include "cg_win_internal.h"

/* 图片双缓冲：image_body 定义在文件后面，这里先前置声明 */
typedef struct { cg_app_t *app; cg_widget_t *w; } image_ctx_t;
/**
 * @brief 图片一帧适配器：画进内存位图
 * @param mem 内存位图 DC（双缓冲的离屏 DC）
 * @param rc 离屏位图矩形（左上角为 0,0）
 * @param vctx 上下文（应用与控件）
 * @return void 无返回值
 */
static void image_body(HDC mem, const RECT *rc, void *vctx);

/** 图片控件：按控件大小 StretchBlt，缺图时画占位框 */
/**
 * @brief 图片控件的自绘窗口过程，按控件大小 StretchBlt，缺图时画占位框
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 * @note image 在类型表里声明了 click/dblclick（cg_doc_kind.c），但 IMAGE_CLASS 自己
 *       不发任何通知 —— 所以照画板的做法在 WM_LBUTTONUP 里补一次 click；双击由 cg_dispatch
 *       按系统双击间隔合成（core/cg_event.c）。历史上这里漏了：
 *       给图片绑 click 永远不触发，而 --selftest 是直接调函数指针，所以门禁发现不了。
 */
static LRESULT CALLBACK ImageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        image_ctx_t ctx;
        GetClientRect(hwnd, &rc);
        ctx.app = app;
        ctx.w = cgw_widget_by_hwnd(app, hwnd);
        cgw_paint_buffered(dc, &rc, image_body, &ctx);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        /* `image` 在类型表里声明了 click/dblclick（cg_doc_kind.c），但 IMAGE_CLASS 自己
         * 不发任何通知 —— 所以照画板的做法在这里补一次 click；双击由 cg_dispatch 按
         * 系统双击间隔合成（core/cg_event.c）。历史上这里漏了：
         * 给图片绑 click 永远不触发，而 --selftest 是直接调函数指针，所以门禁发现不了。 */
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        if (w && !g_win_in_notify) {
            g_win_in_notify = 1;
            cg_dispatch(w, CG_EVENT_CLICK);
            g_win_in_notify = 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册图片控件的自绘窗口类（整进程一次） */
/**
 * @brief 注册图片控件的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
void cgw_register_image_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ImageProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_IMAGE_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 环状进度（自绘）
 * 原生控件里没有圆环进度，这个和 switch 一样是 owner-draw 思路的样板：
 * 自己画在窗口上，所以能吃 style 的颜色，也才能有圆角/圆弧这种原生控件画不出的东西。
 */

/** @brief 传给双缓冲绘制回调的参数 */
typedef struct {
    cg_app_t    *app;
    HWND         hwnd;
    cg_widget_t *w;
} canvas_ctx_t;

/**
 * @brief 双缓冲上屏：fn 把整个控件画进内存位图，再一次 BitBlt 贴到屏幕
 * @param dc 目标 DC（屏幕或父窗口）
 * @param rc 控件客户区矩形，决定落点与尺寸
 * @param fn 整帧绘制回调，负责把一个控件画进给定的内存 DC
 * @param ctx 透传给 fn 的上下文
 * @return void 无返回值
 * @details GDI/GDI+ 分几笔直接画在屏幕 DC 上时，每一笔之间都是可见的中间状态
 *          （底色先糊上去、内容再一笔一笔出现）——拖滑块这类高频重绘下就是肉眼可见的
 *          闪烁。先画进内存位图、最后一次贴上屏，中间状态不会被扫出来（2026-09-25
 *          用户报"拖滑块时标签/进度条/圆环闪烁"后的统一治法，meter/led 同理）。
 */
void cgw_paint_buffered(HDC dc, const RECT *rc,
                        void (*fn)(HDC, const RECT *, void *), void *ctx)
{
    int     w = rc->right - rc->left;
    int     h = rc->bottom - rc->top;
    HDC     mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(mem, bmp);
    RECT    mrc;

    mrc.left = 0; mrc.top = 0; mrc.right = w; mrc.bottom = h;
    fn(mem, &mrc, ctx);
    BitBlt(dc, rc->left, rc->top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/** @brief 图片的一帧：位图按控件大小拉伸，缺图时画占位框 + 文件名/占位文字 */
/**
 * @brief 图片的一帧：位图按控件大小拉伸，缺图时画占位框 + 文件名/占位文字
 * @param app 应用对象（取背后配色）
 * @param w 图片控件
 * @param dc 目标 DC（内存位图）
 * @param rc 绘制区域
 * @return void 无返回值
 */
static void image_draw(cg_app_t *app, cg_widget_t *w, HDC dc, const RECT *rc)
{
    if (w && w->bitmap) {
        BITMAP bm;
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, (HGDIOBJ)w->bitmap);
        if (GetObjectW((HGDIOBJ)w->bitmap, sizeof(bm), &bm)) {
            int prev = SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, 0, 0, rc->right - rc->left, rc->bottom - rc->top,
                       mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SetStretchBltMode(dc, prev);
        }
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        COLORREF face = cgw_to_colorref(cg_win_behind_rgb(app, (void *)w->native));
        HBRUSH br = CreateSolidBrush(face);
        HGDIOBJ oldbr = SelectObject(dc, br);
        HPEN pen = CreatePen(PS_DASH, 1, RGB(120, 120, 128));
        HGDIOBJ oldpen = SelectObject(dc, pen);
        Rectangle(dc, rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1);
        SelectObject(dc, oldpen);
        DeleteObject(pen);
        SelectObject(dc, oldbr);
        DeleteObject(br);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(90, 90, 96));
        if (w && w->src && *w->src) {
            wchar_t *wt = cgw_wc_from_utf8(w->src);
            if (wt) {
                DrawTextW(dc, wt, -1, (RECT *)rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                free(wt);
            }
        } else {
            const wchar_t *ph = L"图片";
            DrawTextW(dc, ph, -1, (RECT *)rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

/** @brief 图片一帧适配器：画进内存位图 */
static void image_body(HDC mem, const RECT *rc, void *vctx)
{
    image_ctx_t *c = (image_ctx_t *)vctx;
    image_draw(c->app, c->w, mem, rc);
}

/**
 * @brief 画圆环中间那一行百分比文字
 * @param dc 目标 DC
 * @param w 圆环控件（取 value / vmin / vmax 与字体样式）
 * @param rc 客户区矩形（文字居中画在里面）
 * @return void 无返回值
 * @note 圆环本身（底色 / 轨道 / 进度弧）在皮肤模块里用 GDI+ 抗锯齿画
 *       （GDI 的粗弧锯齿非常明显）；文字要用本文件的字体缓存，所以留在调用方画完圆环之后补
 */
static void draw_ring_text(HDC dc, const cg_widget_t *w, const RECT *rc);  /* 定义在下面 */

/** @brief 圆环的一帧：底色/轨道/进度弧（GDI+ 抗锯齿）+ 中心百分比 */
/**
 * @brief 圆环的一帧：底色/轨道/进度弧（GDI+ 抗锯齿）+ 中心百分比
 * @param mem 内存位图 DC
 * @param rc 绘制区域
 * @param vctx canvas_ctx_t 上下文（app、hwnd 与控件）
 * @return void 无返回值
 */
static void ring_body(HDC mem, const RECT *rc, void *vctx)
{
    canvas_ctx_t *c = (canvas_ctx_t *)vctx;

    if (c->w) {
        cg_skin_draw_ring(c->w, mem, rc);
        draw_ring_text(mem, c->w, rc);       /* 中心百分比 */
    } else {
        FillRect(mem, rc, cgw_brush_for(cg_win_behind_rgb(c->app, c->hwnd)));
    }
}

/** @brief 进度条的一帧：浅灰圆角轨道 + 主题紫填充 */
/**
 * @brief 进度条的一帧：浅灰圆角轨道 + 主题紫填充
 * @param mem 内存位图 DC
 * @param rc 绘制区域
 * @param vctx canvas_ctx_t 上下文（app、hwnd 与控件）
 * @return void 无返回值
 */
static void progress_body(HDC mem, const RECT *rc, void *vctx)
{
    canvas_ctx_t *c = (canvas_ctx_t *)vctx;

    if (c->w) {
        cg_skin_draw_progress(c->w, mem, rc);
    } else {
        FillRect(mem, rc, cgw_brush_for(cg_win_behind_rgb(c->app, c->hwnd)));
    }
}
/**
 * @brief 画圆环中间那一行百分比文字
 * @param dc 目标 DC；w 控件；rc 客户区
 * @details 圆环本身（底色 / 轨道 / 进度弧）在 cg_skin.c 的 `cg_skin_draw_ring()` 里
 *          用 GDI+ 抗锯齿画（GDI 的粗弧锯齿非常明显）；文字要用本文件的 `cgw_font_for()`，
 *          所以留在调用方画完圆环之后补。
 * @return void 无返回值
 */
static void draw_ring_text(HDC dc, const cg_widget_t *w, const RECT *rc)
{
    int span = (w->vmax > w->vmin) ? (w->vmax - w->vmin) : 1;
    int val = w->value;
    wchar_t text[32];
    HFONT f, oldf;
    RECT tr = *rc;

    if (val < w->vmin) val = w->vmin;
    if (val > w->vmax) val = w->vmax;

    f = w->app ? cgw_font_for(w->app, w->style.font, w->style.size, w->style.bold, 0) : NULL;
    oldf = (HFONT)SelectObject(dc, f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    swprintf(text, 32, L"%d%%", (val - w->vmin) * 100 / span);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldf);
}

/** @brief 圆环进度控件窗口过程（自绘，无交互） */
/**
 * @brief 圆环进度控件窗口过程（自绘，无交互）
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
static LRESULT CALLBACK RingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        canvas_ctx_t ctx;
        GetClientRect(hwnd, &rc);
        ctx.app = app; ctx.hwnd = hwnd;
        ctx.w = cgw_widget_by_hwnd(app, hwnd);
        cgw_paint_buffered(dc, &rc, ring_body, &ctx);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册圆环进度的自绘窗口类（整进程一次） */
/**
 * @brief 注册圆环进度的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
void cgw_register_ring_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RingProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_RING_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/**
 * @brief 进度条窗口过程（自绘，无交互）
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 * @note 原生的 PROGRESS_CLASS 不管主题开没开都不发 NM_CUSTOMDRAW，只能整个换成自己的类
 */
static LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        canvas_ctx_t ctx;
        GetClientRect(hwnd, &rc);
        ctx.app = app; ctx.hwnd = hwnd;
        ctx.w = cgw_widget_by_hwnd(app, hwnd);
        cgw_paint_buffered(dc, &rc, progress_body, &ctx);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册进度条的自绘窗口类（整进程一次） */
/**
 * @brief 注册进度条的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
void cgw_register_progress_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_PROGRESS_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

/* ------------------------------------------------------ 画板（自绘）
 * cg_canvas_* 命令把每笔记录在 w->ops 里，WM_PAINT 按序回放。
 * 坐标是逻辑像素（跟 .cgui 同一套），绘制时按 DPI 缩放。
 */
/** @brief 画板坐标：逻辑像素 -> 物理像素（与控件布局同一套缩放） */
/**
 * @brief 画板坐标：逻辑像素 -> 物理像素（与控件布局同一套缩放）
 * @param w 画板控件（取其 DPI 缩放系数）
 * @param logical 逻辑像素坐标
 * @return int 缩放后的物理像素坐标
 */
static int canvas_scale(const cg_widget_t *w, int logical)
{
    return cgw_scale_px(w->app, logical);
}

/**
 * @brief 画板绘制：填底色后按记录顺序回放 cg_canvas_* 的绘制命令
 * @param dc 目标 DC
 * @param w canvas 控件（逐条读出绘制记录回放）
 * @param rc 客户区
 * @return void 无返回值
 */
static void draw_canvas(HDC dc, cg_widget_t *w, const RECT *rc)
{
    COLORREF bgc = (w->style.bg != CG_COLOR_NONE)
                       ? cgw_to_colorref(w->style.bg)
                       : cgw_to_colorref(cg_win_behind_rgb(w->app, w->native));
    HBRUSH bg = CreateSolidBrush(bgc);
    FillRect(dc, rc, bg);
    DeleteObject(bg);

    for (int i = 0; i < w->nops; i++) {
        const cg_canvas_op_t *op = &w->ops[i];
        HPEN pen = CreatePen(PS_SOLID, op->penw > 0 ? op->penw : 1, cgw_to_colorref(op->color));
        HGDIOBJ oldpen = SelectObject(dc, pen);
        HBRUSH br = NULL;
        HGDIOBJ oldbr = NULL;

        if (op->fill) {
            br = CreateSolidBrush(cgw_to_colorref(op->color));
            oldbr = SelectObject(dc, br);
        } else {
            oldbr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        }

        switch (op->type) {
        case CG_CANVAS_OP_LINE:
            MoveToEx(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1), NULL);
            LineTo(dc, canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_RECT:
            Rectangle(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                      canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_ELLIPSE:
            Ellipse(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                    canvas_scale(w, op->x2), canvas_scale(w, op->y2));
            break;
        case CG_CANVAS_OP_TEXT: {
            wchar_t *wt = cgw_wc_from_utf8(op->text ? op->text : "");
            HFONT f = cgw_font_for(w->app, w->style.font,
                               w->style.size > 0 ? w->style.size : 10, w->style.bold, 0);
            HFONT oldf = (HFONT)SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, cgw_to_colorref(op->color));
            if (wt) {
                TextOutW(dc, canvas_scale(w, op->x1), canvas_scale(w, op->y1),
                         wt, (int)wcslen(wt));
                free(wt);
            }
            SelectObject(dc, oldf);
            break;
        }
        default:
            break;
        }
        SelectObject(dc, oldpen);
        DeleteObject(pen);
        if (br) { SelectObject(dc, oldbr); DeleteObject(br); }
        else SelectObject(dc, oldbr);
    }
}

/** @brief 画板控件窗口过程：自绘 + 左键抬起派发 click（双击由运行时合成） */
/**
 * @brief 画板控件窗口过程：自绘 + 左键抬起派发 click（双击由运行时合成）
 * @param hwnd 本控件窗口
 * @param msg 消息号
 * @param wp 附加参数（随 msg 而定，本过程未使用）
 * @param lp 附加参数（随 msg 而定，本过程未使用）
 * @return LRESULT 处理结果，未处理的消息交给 DefWindowProcW
 */
static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_app_t *app = (cg_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        GetClientRect(hwnd, &rc);
        if (w) {
            draw_canvas(dc, w, &rc);
        } else {
            FillRect(dc, &rc, cgw_brush_for(cg_win_behind_rgb(app, hwnd)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        cg_widget_t *w = cgw_widget_by_hwnd(app, hwnd);
        if (w && !g_win_in_notify) {
            g_win_in_notify = 1;
            cg_dispatch(w, CG_EVENT_CLICK);   /* 双击由运行时按系统间隔合成 */
            g_win_in_notify = 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** @brief 注册画板的自绘窗口类（整进程一次） */
/**
 * @brief 注册画板的自绘窗口类（整进程一次）
 * @return void 无返回值
 */
void cgw_register_canvas_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc;
    if (done) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CanvasProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CG_CANVAS_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}
