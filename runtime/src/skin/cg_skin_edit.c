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
 * @brief 沿边框带画一圈圆角线
 * @param dc 目标设备上下文；线画在它上面
 * @param s 输入框子类数据；提供整块尺寸与圆角半径
 * @param col 线的颜色
 * @param penw 笔宽（像素）
 * @param half 路径离窗口边缘的距离（像素）：2px 笔给 1，1px 笔给 0
 * @return void 无返回值
 * @note half 要按笔宽给：2px 笔给 1（正好压住 [0,2)）、1px 笔给 0（正好压住最外那一个像素）
 */
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
 * @brief 立体边框：整圈先按「下 / 右」的颜色画圆角线，再用「上 / 左」的颜色覆盖那两条边
 * @param dc 目标设备上下文；线画在它上面
 * @param s 输入框子类数据；提供边框宽与圆角半径
 * @param outset 立体方向：0 = 凹入式（上 / 左暗、下 / 右亮），非 0 = 凸出式
 * @return void 无返回值
 */
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
    } else if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_NCPAINT) {
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
    switch (w->border) {
    case SK_EDGE_SOFT:
    case SK_EDGE_LINE:  s->bw = 1; break;
    case SK_EDGE_FRAME: s->bw = 3; break;
    default:            s->bw = CG_EDIT_BORDER; break;
    }
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
    s->bg = sk_edit_bg_color(w);

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
