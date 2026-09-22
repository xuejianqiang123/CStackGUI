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

/* .cgui 的 border 字段取值（真相在 cg_doc.h::cg_node_t.border）。
 * 缺省 0 = 本工程原本的 M3 圆角；1~6 对应属性面板里那六种边框。 */
#define SK_EDGE_DEFAULT 0
#define SK_EDGE_NONE    1
#define SK_EDGE_INSET   2
#define SK_EDGE_OUTSET  3
#define SK_EDGE_SOFT    4
#define SK_EDGE_FRAME   5
#define SK_EDGE_LINE    6

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
    int      border;   /* 边框样式（.cgui 的 border 字段） */
    int      arrow;    /* 调节器占的宽度（物理像素）；0 = 没有调节器 */
    HWND     spin;     /* 调节器（上下箭头）的窗口；没有就是 NULL */
    COLORREF bg;       /* 补上边距用的底色（白或 style.bg） */
} sk_edit_sub_t;

/** 沿边框带画一圈圆角线。
 *  half 是路径离窗口边缘的距离，调用方给：**2px 笔给 1**（正好压住 [0,2)），
 *  **1px 笔给 0**（正好压住最外那一个像素 [0,1)）。
 *  另见 rr：1px 笔的右/下两条边要再各往内缩 1px。 */
static void sk_edit_ring(HDC dc, const sk_edit_sub_t *s, COLORREF col, int penw, int half)
{
    HPEN pn, op;
    HBRUSH ob;
    /* GDI 的线是"从路径坐标往右下铺"的：1px 笔在坐标 c 上占的是 [c, c+1) 那个像素。
     * 所以路径写成 x=w / y=h 时，右/下两条边正好落在窗口外 —— 整个控件只剩上/左两条边，
     * 看着就是"边框少了一半"（多行输入框带滚动条时最明显：右/下那两条本来就被滚动条压着，
     * 剩下能看见的上/左两条线就更像画错了）。右/下各减 1 才落进最后一个像素。 */
    int rr = (penw <= 1) ? 1 : 0;
    if (s->w <= 2 * half + rr || s->h <= 2 * half + rr) return;
    pn = CreatePen(PS_SOLID, penw, col);
    op = (HPEN)SelectObject(dc, pn);
    ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, half, half, s->w - half - rr, s->h - half - rr, s->rad * 2, s->rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pn);
}

/** 把边框带（上边距 + 左右下）整条填成输入框底色。
 *  **每次重画都先填**：不然细线样式只覆盖了带内的一个像素，上一次聚焦时画的
 *  2px 紫环会残留一条紫边（"点亮一次之后边框痕迹不消失"就是这么来的）。
 *
 *  **右上角那块"垂直居中的上边距"只铺到客户区右边界**：`s->arrow` 那一列是调节器
 *  箭头的地盘，而箭头范围是 [bw, h-bw) —— 上边距带的高度（pad = bw + 居中量）
 *  正好压在箭头的上半截上。我们用的是整窗 DC（GDI 不避让同级窗口），
 *  铺过去就会把"向上箭头"的头顶削掉（用户报的"向上的箭头被遮挡了"）。 */
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

/** 3D 边框：整圈先按「下/右」的颜色画圆角线，再用「上/左」的颜色覆盖那两条边。
 *  outset=0 时上/左暗、下/右亮（凹入式）；outset=1 反过来（凸出式）。 */
static void sk_edit_bevel(HDC dc, const sk_edit_sub_t *s, int outset)
{
    HPEN pn, op;
    int half = s->bw / 2;
    int lo = outset ? SK_EDGE_LIGHT : SK_EDGE_DARK;   /* 上/左 */
    int hi = outset ? SK_EDGE_DARK  : SK_EDGE_LIGHT;  /* 下/右 */

    sk_edit_ring(dc, s, hi, s->bw, s->bw / 2);
    pn = CreatePen(PS_SOLID, s->bw, lo);
    op = (HPEN)SelectObject(dc, pn);
    /* 两端各留出圆角半径，剩下的由圆角线接起来 */
    MoveToEx(dc, half + s->rad, half, NULL);
    LineTo(dc, s->w - half - s->rad, half);
    MoveToEx(dc, half, half + s->rad, NULL);
    LineTo(dc, half, s->h - half - s->rad);
    SelectObject(dc, op);
    DeleteObject(pn);
}

/** 画上边距的底 + 边框。边框样式由 .cgui 的 border 决定，**聚焦时一律换成主题紫** ——
 *  键盘用户必须看得见焦点在哪，这条优先级高于边框样式（无边框也一样）。
 *  边框画在客户区之外（客户区已由 WM_NCCALCSIZE 四边内缩 bw），所以控件怎么重绘都盖不掉它。 */
static void sk_edit_box(HWND hwnd, const sk_edit_sub_t *s, int focus)
{
    HDC dc;

    if (s->w <= 2 * s->bw || s->h <= 2 * s->bw) return;
    dc = GetWindowDC(hwnd);
    if (!dc) return;

    /* 第一步永远是把整条边框带（含让出来的上边距）填成底色：
     * 既补上"非客户区没人画"的部分，也顺手抹掉上一次画过的边框/焦点环。 */
    sk_edit_fill_edge(dc, s);

    if (focus) {                                  /* 聚焦：统一紫环（优先级高于边框样式） */
        sk_edit_ring(dc, s, SK_EDGE_FOCUS, s->bw, s->bw / 2);
        ReleaseDC(hwnd, dc);
        return;
    }

    /* 细线样式的边框带只有 1px（见 cg_skin_apply_edit），路径给 0 —— 笔宽 1、坐标整数，
     * 这样线正好落在带内。给 1 或 0.5 都会画成"半像素线"：一半在带内一半在带外，看着就是断线。 */
    switch (s->border) {
    case SK_EDGE_NONE:   break;                          /* 无边框：底色已经填好，不画线 */
    case SK_EDGE_INSET:  sk_edit_bevel(dc, s, 0); break;
    case SK_EDGE_OUTSET: sk_edit_bevel(dc, s, 1); break;
    case SK_EDGE_SOFT:   sk_edit_ring(dc, s, SK_EDGE_SOFT_C, 1, 0); break;
    case SK_EDGE_FRAME:                                  /* 镜框：外圈 [0,1)、内圈 [2,3)，中间留 1px 底色缝 */
        sk_edit_ring(dc, s, SK_EDGE_LINE_C, 1, 0);
        sk_edit_ring(dc, s, SK_EDGE_LINE_C, 1, 2);
        break;
    case SK_EDGE_LINE:   sk_edit_ring(dc, s, SK_EDGE_LINE_C, 1, 0); break;
    default:             sk_edit_ring(dc, s, SK_EDGE_GREY, s->bw, s->bw / 2); break;   /* SK_EDGE_DEFAULT */
    }
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

/** 把调节器（上下箭头）摆到边框内侧的右边。
 *  **宽度由我们定**（s->arrow），不看控件当前尺寸：up-down 控件建出来是 0x0，
 *  等它自报宽度就是把命交给系统 —— 有的机器上它一直是 0x0，箭头就"消失"了
 *  （旧版用的 `uw <= 0 就 return` 正是这么把箭头留成隐形的）。
 *  **故意不用 UDS_ALIGNRIGHT**：那个样式按 buddy 的"窗口矩形"贴右，会压在自绘边框上、
 *  越过上下圆角。这里按客户区内侧算：右让 bw + arrow、上下各让 bw。 */
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
    if (msg == WM_SETFONT) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        /* 带调节器时：换字体会让箭头重新按 buddy 摆位，补摆一次 */
        sk_edit_spin_fit(hwnd, s);
        /* 数值框的上下箭头换字体时会重设 buddy 边距、把左边距清成 0：补一次 */
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
        sk_edit_spin_fit(hwnd, s);   /* 尺寸变了：箭头跟着摆到新的右边内侧 */
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(s->left, 0));
        /* resize 时系统按默认非客户区重算客户区，会把让出来的上边距抹掉（数值框的
         * 上下箭头改 buddy 尺寸就会触发）。补一次 FRAMECHANGED 让 WM_NCCALCSIZE 重新生效；
         * 尺寸没变不会递归发 WM_SIZE。 */
        if (s->single)
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (msg == WM_PAINT || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_NCPAINT) {
        /* WM_NCPAINT 也要跟着画：边框和箭头占的那一列都在非客户区上，
         * 只等 WM_PAINT 的话，"只重画非客户区"的场合（改尺寸、系统重算边框）会留白 */
        LRESULT r;
        if (msg == WM_SETFOCUS)
            SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(s->left, 0));
        r = DefSubclassProc(hwnd, msg, wp, lp);
        sk_edit_box(hwnd, s, GetFocus() == hwnd);
        return r;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

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
    /* 边框带宽度按样式定：细线样式 1px（笔宽 1、坐标才不会落在半像素上），
     * 镜框式 3px（两圈线 + 中间 1px 缝），其余 2px */
    switch (w->border) {
    case SK_EDGE_SOFT:
    case SK_EDGE_LINE:  s->bw = 1; break;
    case SK_EDGE_FRAME: s->bw = 3; break;
    default:            s->bw = CG_EDIT_BORDER; break;
    }
    s->left = CG_EDIT_TEXT_MARGIN;
    s->border = w->border;
    s->spin = (HWND)w->native2;   /* 调节器可能还没建；建好后 create 会再调 fit 补上 */
    /* 箭头占位：多行输入框不给箭头（运行时也不建），宽度按 scale 缩放、太窄兜底 12px。
     * 有了它，WM_NCCALCSIZE 才能把右边这列从客户区让出去。 */
    s->arrow = (w->spin && !multiline) ? (CG_EDIT_SPIN_W * scale / 100) : 0;
    if (s->arrow > 0 && s->arrow < 12) s->arrow = 12;
    s->bg = (w->style.bg != CG_COLOR_NONE)
              ? RGB((w->style.bg >> 16) & 0xFF, (w->style.bg >> 8) & 0xFF, w->style.bg & 0xFF)
              : GetSysColor(COLOR_WINDOW);

    sk_edit_resize(hw, s);   /* 初始尺寸 + 圆角区域 */
    /* 左右留边距：文字别怼到圆角边框上（边框在客户区外，垂直居中见 WM_NCCALCSIZE）。
     * 数值框的箭头稍后会重设边距，WM_SETFONT/WM_SIZE 里会按 s->left 再补。 */
    SendMessageW(hw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(CG_EDIT_TEXT_MARGIN, CG_EDIT_TEXT_MARGIN));
    SetWindowSubclass(hw, sk_edit_sub, 1, (DWORD_PTR)s);
    /* 挂上子类后强制一次布局：立刻按当前字体算好垂直居中量 */
    SetWindowPos(hw, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
