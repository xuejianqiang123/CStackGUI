/**
 * @file    cg_win_create.c
 * @brief   原生控件创建与销毁：类注册后按树序建窗口、构建器、资源释放
 * @details 从原 cg_win32.c 按职责拆分而来，跨模块接口见 cg_win_internal.h。
 */
#include "cg_win_internal.h"

/** 按原生窗口反查控件：数值框（edit + 调节器）这类一个控件有两个 HWND，按 ctrl id 找会漏 */
cg_widget_t *cgw_widget_by_hwnd(cg_app_t *app, HWND h)
{
    if (!app || !h) return NULL;
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if ((HWND)w->native == h || (HWND)w->native2 == h) return w;
    }
    return NULL;
}

/* ------------------------------------------------------------ 控件创建 */

/** @brief 控件的原生父窗口（顶层控件挂主窗口，容器子控件挂容器） */
HWND cgw_parent_hwnd_of(const cg_app_t *app, const cg_widget_t *w)
{
    if (!w || !w->parent || w->parent->is_window) return (HWND)app->hwnd;
    return (HWND)w->parent->native;
}

/** @brief 给单个控件建原生窗口（menu/timer 除外），含子控件拼装与初始同步 */
void cgw_create_native(cg_widget_t *w)
{
    cg_app_t *app = w->app;
    HWND parent = cgw_parent_hwnd_of(app, w);
    HWND h;
    wchar_t *wt = cgw_wc_from_utf8(w->text);
    const char *type = w->type;
    DWORD style = cgw_style_of(w);
    DWORD ex = cgw_exstyle_of(w);

    /* 菜单栏没有自己的窗口：它是挂在主窗口上的 HMENU，在 cgw_win_init 里建。
     * 时钟也一样没有窗口：这里只负责给它起定时器（cg_widget_add 动态加的也走这）。 */
    if (!strcmp(type, "menu")) {
        free(wt);
        return;
    }
    if (!strcmp(type, "timer")) {
        free(wt);
        cg_win_restart_timer(w);
        return;
    }
    /* 串口组件同样没有窗口：只负责按 auto 决定要不要把链路打开（失败只记日志，
     * 不拦启动 —— 端口被占不该让整个程序起不来）。到达轮询在打开成功时起。 */
    if (!strcmp(type, "serial")) {
        free(wt);
        if (w->s_auto) cg_widget_serial_open(w, NULL);
        return;
    }

    if (!strcmp(type, "group") || !strcmp(type, "box")) cgw_register_panel_class();
    if (!strcmp(type, "image")) cgw_register_image_class();
    if (!strcmp(type, "progressring")) cgw_register_ring_class();
    if (!strcmp(type, "progress")) cgw_register_progress_class();
    if (!strcmp(type, "canvas")) cgw_register_canvas_class();
    if (!strcmp(type, "splitter")) cgw_register_splitter_class();
    if (!strcmp(type, "chart")) cgw_register_chart_class();
    if (!strcmp(type, "led")) cgw_register_led_class();
    if (!strcmp(type, "meter")) cgw_register_meter_class();

    /* browser 的窗口名就是起始地址（AtlAxWin 的约定） */
    if (!strcmp(type, "browser") && w->src && *w->src) {
        free(wt);
        wt = cgw_wc_from_utf8(w->src);
    }

    h = CreateWindowExW(ex, cgw_class_of(w), wt ? wt : L"",
                        style,
                        cgw_scale_px(app, w->x), cgw_scale_px(app, w->y),
                        cgw_scale_px(app, w->w), cgw_scale_px(app, w->h),
                        parent, (HMENU)(INT_PTR)w->ctrl_id,
                        GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!h) {
        fprintf(stderr, "[cstackgui] 创建控件失败: %s #%s\n", type ? type : "-", w->id ? w->id : "-");
        return;
    }
    w->native = h;


    /* 输入框圆角 + 单行文字垂直居中（多行 / 数值框都是 edit，一样要）
     * Subclass 在 resize/换字体时重算，Win32 原生 EDIT 不支持 owner-draw */
    if (!strcmp(type, "edit"))
        cg_skin_apply_edit(w);

    /* 最大允许长度：只能靠 EM_SETLIMITTEXT 设（没有对应的窗口样式位）。
     * maxlen=0 保持系统默认，不去动它 —— 免得"没填"变成"限 0 个字符"。 */
    if (!strcmp(type, "edit") && w->maxlen > 0)
        SendMessageW(h, EM_SETLIMITTEXT, (WPARAM)w->maxlen, 0);

    /* 自绘窗口类（CG_*_CLASS）的窗口过程要靠 GWLP_USERDATA 反查 app 和控件，
     * 所以**新增一个自绘控件就必须在这里登记**。漏了它的后果很迷惑：
     * 窗口过程拿到 NULL 的 app，于是控件画不出来（首帧填了兜底色），
     * 运气差时还会在 cg_win_behind_rgb 里直接访问冲突崩掉 —— splitter 就是这么踩过一次。 */
    if (!strcmp(type, "group") || !strcmp(type, "box") ||
        !strcmp(type, "card") || !strcmp(type, "shape") ||
        !strcmp(type, "image") || !strcmp(type, "progressring") ||
        !strcmp(type, "progress") || !strcmp(type, "splitter") ||
        !strcmp(type, "chart") || !strcmp(type, "led") || !strcmp(type, "meter") ||
        !strcmp(type, "canvas")) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)app);
    }

    /* 统一挂焦点子类：gotfocus/lostfocus，rate/pagination 的按点判定 */
    cgw_subclass_for_events(h);
    {
        cg_win_state_t *st = (cg_win_state_t *)app->natives;
        if (st && st->tooltip && w->hint && *w->hint) {
            cgw_tooltip_add_for(st->tooltip, h);
        }
    }

    /* 调节器（.cgui 的 spin）：右侧一列上下箭头，由「输入框 + UPDOWN」两个原生窗口
     * 拼成（原来的独立类型 spinner 就是这么做的，2026-09-22 合并进 edit）。
     * 自动方式点箭头就改值 + 写文本（见 cg_win_notify.c 的 UDN_DELTAPOS），
     * 手动方式只派发「调节钮被按下」让业务自己改。多行输入框不给箭头（没意义）。
     * **不带 UDS_ALIGNRIGHT** —— 那个样式按窗口矩形贴右，会把箭头压在自绘边框上；
     * 位置交给 cg_skin_edit_fit_spin()（按客户区内侧摆），尺寸变化时自绘层会重摆。
     * 也不用 UDS_SETBUDDYINT：它会在位置一变就用 SetWindowText 重写 buddy 文本、
     * 把光标/选区清回 0（点箭头或敲数字后光标总在最前面）。文本由我们自管。 */
    if (!strcmp(type, "edit") && w->spin && !w->multiline) {
        HWND up = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                  0, 0, 0, 0, parent, NULL, GetModuleHandleW(NULL), NULL);
        if (up) {
            SendMessageW(up, UDM_SETBUDDY, (WPARAM)h, 0);
            SendMessageW(up, UDM_SETRANGE32, (WPARAM)w->vmin, (LPARAM)w->vmax);
            SendMessageW(up, UDM_SETPOS32, 0, (LPARAM)w->value);
            w->native2 = up;
        }
    }

    /* 日期时间：Windows 没有「日期+时间」合一的控件，用两个 DTP 并排拼 */
    if (!strcmp(type, "datepicker") && cgw_is_datetime(w)) {
        HWND t = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT,
                                 0, 0, 0, 0, parent, NULL, GetModuleHandleW(NULL), NULL);
        w->native2 = t;
    }

    /* 第二个原生窗口也要能报焦点事件 */
    if (w->native2) cgw_subclass_for_events((HWND)w->native2);

    cgw_sync_widget(w);

    /* 建箭头 / 设字体都可能把输入框左边距清掉：全部就位后再补一次 */
    if (!strcmp(type, "edit") && w->spin)
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(CG_EDIT_TEXT_MARGIN, 0));

    /* 调节器建好了：交给自绘层按客户区内侧摆位（并给右边留出箭头宽度） */
    if (!strcmp(type, "edit") && w->spin)
        cg_skin_edit_fit_spin(w);
}

/** 递归建原生控件：父先于子 */
void cgw_create_native_tree(cg_widget_t *parent)
{
    if (!parent) return;
    for (size_t i = 0; i < parent->nchildren; i++) {
        cg_widget_t *c = parent->children[i];
        cgw_create_native(c);
        cgw_create_native_tree(c);
    }
}

/** @brief 销毁控件的原生窗口并释放后端资源（位图 / imagelist / 映射表） */
void cgw_destroy_native(cg_widget_t *w)
{
    /* 串口组件的 native2 是 cg_serial_t* 不是 HWND —— 必须在下面的
     * DestroyWindow 之前拦下来关链路，把它当窗口销毁是访问冲突。 */
    if (w->native2 && cgw_is_t(w, "serial")) {
        cg_serial_close((cg_serial_t *)w->native2);
        w->native2 = NULL;
    }
    if (w->native2) {
        DestroyWindow((HWND)w->native2);       /* 数值框的上下箭头 */
        w->native2 = NULL;
    }
    if (w->bitmap) {                            /* 图片控件的位图 */
        DeleteObject((HGDIOBJ)w->bitmap);
        w->bitmap = NULL;
    }
    if (w->imglist) {                           /* 表格行图标 */
        ImageList_Destroy((HIMAGELIST)w->imglist);
        w->imglist = NULL;
    }
    if (w->item_map) {
        free(w->item_map);
        w->item_map = NULL;
        w->item_map_n = 0;
    }
    if (w->native) {
        DestroyWindow((HWND)w->native);
        w->native = NULL;
    }
}
