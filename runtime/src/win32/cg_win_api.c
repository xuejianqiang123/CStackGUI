/**
 * @file    cg_win_api.c
 * @brief   画板与表格增强 API、browser 导航
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/* ------------------------------------------------------------ 画板 */

/** 类型检查：canvas 的绘制命令只能用在 canvas 上 */
static int canvas_ok(const cg_widget_t *w, const char *who)
{
    if (w && w->type && strcmp(w->type, "canvas") == 0) return 1;
    fprintf(stderr, "[cstackgui] %s: 需要 canvas 控件，实际给的是 %s\n",
            who, (w && w->type) ? w->type : "(null)");
    return 0;
}

/** @brief 给画板追加一条绘制命令并触发重绘（容量不够倍增扩容） */
static void canvas_push(cg_widget_t *w, const cg_canvas_op_t *op)
{
    if (w->nops >= w->op_cap) {
        int nc = w->op_cap > 0 ? w->op_cap * 2 : 64;
        cg_canvas_op_t *no = (cg_canvas_op_t *)realloc(w->ops, (size_t)nc * sizeof(cg_canvas_op_t));
        if (!no) return;
        w->ops = no;
        w->op_cap = nc;
    }
    w->ops[w->nops++] = *op;
    if (w->native) InvalidateRect((HWND)w->native, NULL, FALSE);
}

/**
 * @brief 清空画板上所有绘制内容
 * @param w canvas 控件
 * @return void 无返回值
 */
void cg_canvas_clear(cg_widget_t *w)
{
    if (!canvas_ok(w, "cg_canvas_clear")) return;
    for (int i = 0; i < w->nops; i++) {
        if (w->ops[i].type == CG_CANVAS_OP_TEXT) free(w->ops[i].text);
    }
    w->nops = 0;
    if (w->native) InvalidateRect((HWND)w->native, NULL, TRUE);
}

/**
 * @brief 设置后续图形的颜色（0xRRGGBB）
 * @param w canvas 控件
 * @param rgb 颜色，如 CG_RGB(0x33,0x33,0x33) 或 0x333333
 * @return void 无返回值
 */
void cg_canvas_set_color(cg_widget_t *w, unsigned rgb)
{
    if (!canvas_ok(w, "cg_canvas_set_color")) return;
    w->canvas_color = rgb & 0xFFFFFF;
}

/**
 * @brief 设置后续图形的线宽（像素，最小 1）
 * @param w canvas 控件
 * @param px 线宽
 * @return void 无返回值
 */
void cg_canvas_set_line_width(cg_widget_t *w, int px)
{
    if (!canvas_ok(w, "cg_canvas_set_line_width")) return;
    w->canvas_penw = px < 1 ? 1 : px;
}

/**
 * @brief 画一条线段
 * @param w canvas 控件
 * @param x1,y1 起点；x2,y2 终点（控件内坐标，左上角为 0,0）
 * @return void 无返回值
 */
void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_line")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_LINE;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.x1 = x1; op.y1 = y1; op.x2 = x2; op.y2 = y2;
    canvas_push(w, &op);
}

/**
 * @brief 画矩形
 * @param w canvas 控件
 * @param x,y 左上角；wd,ht 宽高
 * @param fill 1 = 实心，0 = 描边
 * @return void 无返回值
 */
void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_rect")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_RECT;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.fill = fill ? 1 : 0;
    op.x1 = x; op.y1 = y; op.x2 = x + wd; op.y2 = y + ht;
    canvas_push(w, &op);
}

/**
 * @brief 画椭圆（含圆）
 * @param w canvas 控件
 * @param x,y 外接矩形左上角；wd,ht 外接矩形宽高
 * @param fill 1 = 实心，0 = 描边
 * @return void 无返回值
 */
void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_ellipse")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_ELLIPSE;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.fill = fill ? 1 : 0;
    op.x1 = x; op.y1 = y; op.x2 = x + wd; op.y2 = y + ht;
    canvas_push(w, &op);
}

/**
 * @brief 写一行文字（左上角定位，颜色用当前画笔颜色）
 * @param w canvas 控件
 * @param x,y 文字左上角
 * @param text UTF-8 文本
 * @return void 无返回值
 */
void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text)
{
    cg_canvas_op_t op;
    if (!canvas_ok(w, "cg_canvas_text")) return;
    memset(&op, 0, sizeof(op));
    op.type = CG_CANVAS_OP_TEXT;
    op.color = w->canvas_color;
    op.penw = w->canvas_penw;
    op.x1 = x; op.y1 = y;
    op.text = (text && *text) ? _strdup(text) : NULL;
    canvas_push(w, &op);
}

/* ------------------------------------------------------------ 表格增强 */

/**
 * @brief 给表格某行绑一个业务数值（不显示在表格里，取回用）
 * @param w table 控件
 * @param row 行号（0 起）
 * @param value 要存的整数
 * @return int 0 成功，-1 参数不对
 */
int cg_table_set_row_value(cg_widget_t *w, int row, int value)
{
    HWND h;
    LVITEMW it;
    if (!w || !cgw_is_t(w, "table")) return -1;
    if (row < 0 || row >= w->nrows) return -1;
    cg_table_rows_grow(w, w->nrows);
    w->row_val[row] = value;
    h = (HWND)w->native;
    if (!h) return 0;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = row;
    it.lParam = value;
    SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
    return 0;
}

/**
 * @brief 读某行绑定的业务数值
 * @param w table 控件
 * @param row 行号（0 起）
 * @return int 绑定的数值；参数不对返回 0
 */
int cg_table_row_value(const cg_widget_t *w, int row)
{
    if (!w || !cgw_is_t(w, "table")) return 0;
    if (row < 0 || row >= w->nrows) return 0;
    return (w->row_val && row < w->row_cap) ? w->row_val[row] : 0;
}

/**
 * @brief 设置某行的勾选状态（.cgui 里 table 要开 check）
 * @param w table 控件
 * @param row 行号（0 起）
 * @param on 1 = 勾上，0 = 取消
 * @return void 无返回值（程序设置的勾选不触发 check 事件）
 */
void cg_table_set_checked(cg_widget_t *w, int row, int on)
{
    HWND h;
    LVITEMW it;
    if (!w || !cgw_is_t(w, "table")) return;
    if (row < 0 || row >= w->nrows) return;
    cg_table_rows_grow(w, w->nrows);
    if (w->row_checked) w->row_checked[row] = on ? 1 : 0;
    h = (HWND)w->native;
    if (!h || !w->check) return;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_STATE;
    it.state = on ? INDEXTOSTATEIMAGEMASK(2) : INDEXTOSTATEIMAGEMASK(1);
    it.stateMask = LVIS_STATEIMAGEMASK;
    SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&it);
}

/**
 * @brief 查某行是否被勾选
 * @param w table 控件
 * @param row 行号（0 起）
 * @return int 1 = 勾选，0 = 没勾 / 参数不对
 */
int cg_table_checked(const cg_widget_t *w, int row)
{
    if (!w || !cgw_is_t(w, "table")) return 0;
    if (row < 0 || row >= w->nrows) return 0;
    return (w->row_checked && row < w->row_cap) ? w->row_checked[row] : 0;
}

/**
 * @brief 设置某行行首图标；path 传 NULL 清除
 * @param w table 控件
 * @param row 行号（0 起）
 * @param path 图片路径（png/jpg/bmp/gif，查找规则同 image 控件）
 * @return void 无返回值
 */
void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path)
{
    HWND h;
    HIMAGELIST il;
    int idx;
    if (!w || !cgw_is_t(w, "table")) return;
    if (row < 0 || row >= w->nrows) return;
    cg_table_rows_grow(w, w->nrows);
    free(w->row_icon[row]);
    w->row_icon[row] = (path && *path) ? _strdup(path) : NULL;

    h = (HWND)w->native;
    if (!h) return;
    if (!w->row_icon[row]) {
        /* 清图标：指向不存在的下标即可 */
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_IMAGE;
        it.iItem = row;
        it.iImage = -1;
        SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
        InvalidateRect(h, NULL, FALSE);
        return;
    }
    il = cgw_table_imagelist(h, w);
    idx = cgw_imagelist_add_file(w, il, w->row_icon[row], cgw_scale_px(w->app, 16));
    if (idx >= 0) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_IMAGE;
        it.iItem = row;
        it.iImage = idx;
        SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&it);
    }
    InvalidateRect(h, NULL, FALSE);
}

/**
 * @brief 多选表格当前选中了几行
 * @param w table 控件
 * @return int 选中行数（单选表格最多 1）
 */
int cg_table_selected_count(const cg_widget_t *w)
{
    if (!w || !cgw_is_t(w, "table") || !w->native) return 0;
    return (int)SendMessageW((HWND)w->native, LVM_GETSELECTEDCOUNT, 0, 0);
}

/**
 * @brief 取第 i 个选中的行号（配合 cg_table_selected_count 遍历）
 * @param w table 控件
 * @param index 第几个选中行（0 起）
 * @return int 行号；没有了返回 -1
 */
int cg_table_selected_row(const cg_widget_t *w, int index)
{
    int row = -1;
    if (!w || !cgw_is_t(w, "table") || !w->native || index < 0) return -1;
    for (int i = 0; i <= index; i++) {
        row = (int)SendMessageW((HWND)w->native, LVM_GETNEXTITEM, (WPARAM)row,
                                MAKELPARAM(LVNI_SELECTED, 0));
        if (row == -1) return -1;
    }
    return row;
}

/* ------------------------------------------------------------ 网页浏览 */

/**
 * @brief 让 browser 控件打开一个地址（实验性：IE 内核，仅本机文件 / 简单页面）
 * @param w browser 控件
 * @param url http/https 地址或本地文件完整路径
 * @return void 无返回值
 */
void cg_browser_navigate(cg_widget_t *w, const char *url)
{
    if (!w || !cgw_is_t(w, "browser")) return;
    free(w->src);
    w->src = (url && *url) ? _strdup(url) : NULL;
    cgw_sync_widget(w);   /* sync 发现 src 变了会整个重建 AtlAxWin */
}
