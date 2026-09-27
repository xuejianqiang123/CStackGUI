/**
 * @file    cg_win_layout.c
 * @brief   win32 侧的余量分配：把 core 算好的新坐标落到原生窗口上
 * @details 几何算法在 core/cg_layout.c（纯整数、能被纯单测覆盖），这里只负责四件事：
 *          1. **取基准**：窗口第一次 `WM_SIZE` 时把客户区尺寸记成"设计尺寸" ——
 *             窗口是先按 `.cgui` 的 `app.w/h` 建的、且先于 `on_ready` 里可能的
 *             "恢复上次窗口尺寸"，所以第一次拿到的就是设计尺寸。这样**不用去猜窗口边框
 *             和菜单栏占多少**（那是非客户区，设计时根本不知道）；
 *          2. **每个容器各跑一遍**：窗口客户区用状态里的基准尺寸，面板用面板自己的基准尺寸
 *             （面板自己变了几何之后，用它自己的"基准 → 现在"再排一遍里面的孩子）；
 *          3. **把算好的坐标写回控件并 cgw_sync_widget()** —— 那个函数内部按 DPI 缩放，
 *             所以本文件**全程逻辑像素**，不碰任何缩放数学（与 .cgui 同一套单位）；
 *          4. **自检式基准**：重排前若发现某控件的 rect 与"我们上次写下去的"不一致，
 *             说明有人在我们之外动过它（**分隔条拖动** / `cg_widget_set_rect`），
 *             就把当前值认成新的设计基准 —— 否则下一次拉伸会把那次拖动悄悄丢掉。
 *             这样做还有个附带的便宜：**不需要去改分隔条的代码**
 *             （cg_win_draw_panel.c 里当时还有别人未提交的调试打印，不该混进来）。
 *
 *          什么时候不该重排：DPI 变化（`WM_DPICHANGED`）会整体缩放控件并触发 `WM_SIZE` ——
 *          那次尺寸变化不是用户拉的，所以 `cgw_layout_reset()` 把基准丢掉重取，
 *          免得 grow 控件把显示器的缩放吃一遍。
 */
#include "cg_win_internal.h"

/**
 * @brief 按 grow 重排一个容器的子控件，并递归进"自己变了几何"的子容器
 * @param app 应用
 * @param children 子控件数组（窗口客户区就是 app->window->children）
 * @param n 子控件个数
 * @param base_w 该容器的基准宽（设计尺寸）；base_h 同理
 * @param now_w 该容器现在的宽；now_h 同理
 */
static void layout_container(cg_app_t *app, cg_widget_t **children, size_t n,
                             int base_w, int base_h, int now_w, int now_h)
{
    cg_layout_box_t *boxes;
    size_t           i;

    if (!children || n == 0) return;

    if (base_w == now_w && base_h == now_h) {
        /* 容器尺寸没变：不做分配，但"别人动过的"子控件仍要认成新基准 */
        for (i = 0; i < n; i++) {
            cg_widget_t *c = children[i];

            if (c->have_base &&
                (c->x != c->out_x || c->y != c->out_y || c->w != c->out_w || c->h != c->out_h)) {
                c->base_x = c->x; c->base_y = c->y; c->base_w = c->w; c->base_h = c->h;
                c->out_x  = c->x; c->out_y  = c->y; c->out_w  = c->w; c->out_h  = c->h;
            }
        }
        return;
    }

    boxes = (cg_layout_box_t *)malloc(sizeof(cg_layout_box_t) * n);
    if (!boxes) return;                          /* 分配失败：这一趟不重排，比崩了好 */

    for (i = 0; i < n; i++) {
        cg_widget_t *c = children[i];

        if (!c->have_base) {                     /* 运行期动态建的控件：以当前值为基准 */
            c->base_x = c->x; c->base_y = c->y; c->base_w = c->w; c->base_h = c->h;
            c->have_base = 1;
        } else if (c->x != c->out_x || c->y != c->out_y || c->w != c->out_w || c->h != c->out_h) {
            c->base_x = c->x; c->base_y = c->y; c->base_w = c->w; c->base_h = c->h;
        }
        boxes[i].x    = c->base_x;
        boxes[i].y    = c->base_y;
        boxes[i].w    = c->base_w;
        boxes[i].h    = c->base_h;
        boxes[i].grow = c->grow;
    }

    cg_layout_apply(boxes, (int)n, base_w, base_h, now_w, now_h);

    for (i = 0; i < n; i++) {
        cg_widget_t *c       = children[i];
        int          moved   = (c->x != boxes[i].x || c->y != boxes[i].y ||
                                c->w != boxes[i].w || c->h != boxes[i].h);
        int          resized = (c->w != boxes[i].w || c->h != boxes[i].h);

        if (moved) {
            c->x = boxes[i].x; c->y = boxes[i].y; c->w = boxes[i].w; c->h = boxes[i].h;
            cgw_sync_widget(c);                  /* 落到原生窗口（内部按 DPI 缩放） */
        }
        c->out_x = c->x; c->out_y = c->y; c->out_w = c->w; c->out_h = c->h;

        /* 容器控件自己的尺寸变了：里面的孩子按它自己的（基准 → 现在）再排一遍。
         * 只挪位置不影响孩子，所以只在尺寸变了时递归。 */
        if (resized && c->children && c->nchildren) {
            layout_container(app, c->children, c->nchildren, c->base_w, c->base_h, c->w, c->h);
        }
    }
    free(boxes);
}

/** @brief 按 .cgui 的 grow 重排窗口里的控件（WM_SIZE 里调；尺寸是**逻辑**像素） */
void cgw_layout_apply(cg_app_t *app, int now_w, int now_h)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;

    if (!app || !app->window || !st) return;
    if (!st->layout_ready) {
        /* 第一次（或刚经历 DPI 变化）：这一趟拿到的就是设计尺寸，只记基准 */
        st->layout_base_w = now_w;
        st->layout_base_h = now_h;
        st->layout_ready  = 1;
    }
    layout_container(app, app->window->children, app->window->nchildren,
                     st->layout_base_w, st->layout_base_h, now_w, now_h);
}

/** @brief 丢掉布局基准，让下一个 WM_SIZE 重新取（DPI 变化后调） */
void cgw_layout_reset(cg_app_t *app)
{
    cg_win_state_t *st = app ? (cg_win_state_t *)app->natives : NULL;

    if (st) st->layout_ready = 0;
}
