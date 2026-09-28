/**
 * @file    main.c
 * @brief   画板：鼠标拖拽作图、撤销 / 重做、导出 PNG
 * @details 画板上的图形不是像素，而是**被录制的绘制命令**：每画一笔，运行时就把一条命令记进
 *          控件自己的绘制记录，控件重绘（被盖住又露出、改大小）时按序回放 —— 所以**没有**
 *          "擦掉某一笔"的接口。撤销只能：丢掉最后一条 → 清空记录 → 把剩下的逐条重放。
 *          本示例把每条命令**另存**进一张固定数组，它才是可撤销的"真相来源"。
 *
 *          交互：**画板有自己的鼠标事件**（`mousedown` / `mousemove` / `mouseup`，2026-09-28 补齐），
 *          所以"按下 → 拖 → 抬起"直接绑在**画板**上；回调里 `cg_event_x` / `cg_event_y` 给的
 *          就是**画板内**的逻辑像素，拿到就能画（不用再减 `cg_widget_x/y`）。运行时在按下时
 *          自动捕获鼠标，拖出画板边界也还能收到移动与抬起，一笔不会在边上断掉。
 *
 *          拖动中有**橡皮筋预览**：直线 / 矩形 / 椭圆在拖动时就跟着鼠标走，松手才提交成命令
 *          （自由笔本来就是一段段提交，天然实时）。画板的命令模型没有任何"预演"接口，所以预览
 *          只能这么实现：把一条**临时命令**塞进命令表的空槽（不推进 s_live），再整表重放一次
 *          —— 见 preview_op。松手提交的那条与预览用的是同一套取点 / 归一化，所以画面不会跳。
 *
 *          为什么不能挂**窗口级**（`app.events`）：按下 / 移动 / 抬起落在画板这个**子窗口**上时
 *          消息只送给画板，父窗口收不到 —— 早先那种写法在真机上"按下拖动毫无反应"，根因就是这条。
 *          当时把"画板不能拖拽作图"记成了已知缺口，能力补齐后那条结论已作废。
 *
 *          只 include cstackgui.h 与标准头；不碰 Windows 头、不 malloc / free（命令表是固定数组，
 *          上限写在宏里）；截图 / 写盘前先问有没有窗口，这样自检（不开窗）也能跑干净。
 *
 * 编译：build-app.bat drawpad     自检：drawpad.exe --selftest
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** 命令表上限：满了不再接新笔（固定数组，绝不 malloc） */
#define kMaxOps    512
/** 文字命令的固定缓冲 */
#define kOpTextLen 48

/** 命令类型 */
enum { OP_LINE = 0, OP_RECT, OP_ELLIPSE, OP_TEXT };
/** 工具 = 下拉框下标 */
enum { TOOL_LINE = 0, TOOL_RECT, TOOL_ELLIPSE, TOOL_TEXT, TOOL_FREEHAND };

/** 一条绘制命令：类型 + 参数 + 颜色 + 笔宽，重绘时按它逐条回放 */
typedef struct {
    int      type;             /**< 命令类型 OP_* */
    int      x1, y1, x2, y2;   /**< 画板内坐标；矩形 / 椭圆存左上角与右下角 */
    unsigned color;            /**< 0xRRGGBB */
    int      penw;             /**< 线宽 1..12 */
    char     text[kOpTextLen]; /**< 仅文字命令用 */
} draw_op_t;

static draw_op_t s_ops[kMaxOps];  /**< 命令表：前 s_live 条生效中，前 s_used 条录过（含被撤销的） */
static int       s_live;          /**< 当前回放多少条；撤销 / 重做移动它 */
static int       s_used;          /**< 录过的总数；撤销后再画新笔会把"重做"那段截断 */
static int       s_drawing;       /**< 1 = 正按下拖动中 */
static int       s_tool;          /**< 本次落笔冻结的工具 */
static int       s_penw;          /**< 本次落笔冻结的笔宽 */
static unsigned  s_ink;           /**< 本次落笔冻结的颜色 */
static int       s_x0, s_y0;      /**< 按下点（直线 / 矩形 / 椭圆的起点、文字的落点） */
static int       s_lx, s_ly;      /**< 自由笔的上一个点 */
static int       s_px, s_py;      /**< 上一次预览到的点：同一个点重复上报时不必白重放一遍 */
static char      s_last[160];     /**< 最近一次操作，供状态栏第 3 段显示 */

/**
 * @brief 往终端与状态栏第 3 段写一条操作记录，并落一份运行时日志
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void note(cg_app_t *app, const char *fmt, ...)
{
    char         buf[160];
    va_list      va;
    cg_widget_t *term = cg_find(app, "term_log");
    cg_widget_t *sb   = cg_find(app, "sb_main");

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    snprintf(s_last, sizeof(s_last), "%s", buf);
    if (term) cg_term_write(term, CG_TERM_SYS, buf, -1);   /* 一次调用 = 一段（自动分行） */
    if (sb)   cg_status_set(sb, 2, buf);
    cg_log(CG_LOG_INFO, "%s", buf);
}

/**
 * @brief 刷新状态栏：工具 / 已画几笔 / 最近操作
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    static const char *k_names[] = { "直线", "矩形", "椭圆", "文字", "自由笔" };
    cg_widget_t *sb  = cg_find(app, "sb_main");
    cg_widget_t *cbo = cg_find(app, "cbo_tool");
    int          tool = cbo ? cg_widget_value(cbo) : 0;

    if (tool < 0 || tool > 4) tool = 0;
    if (!sb) return;
    cg_status_setf(sb, 0, "工具：%s", k_names[tool]);
    cg_status_setf(sb, 1, "已画 %d 笔", s_live);
    cg_status_set(sb, 2, s_last);
}

/**
 * @brief 按命令表的前 count 条整体重画画板
 * @param cv 画板控件；NULL 安全
 * @param count 回放多少条；会被夹到 0..kMaxOps（拖动预览时会用到 s_live + 1）
 * @return void 无返回值
 * @note 画板没有"擦掉一笔"的接口：先清掉全部绘制记录，再把命令逐条回放 ——
 *       撤销 / 重做 / 清空 / 拖动预览都只能走这一条路
 */
static void replay_canvas_n(cg_widget_t *cv, int count)
{
    int i;

    if (!cv) return;
    if (count > kMaxOps) count = kMaxOps;
    if (count < 0) count = 0;
    cg_canvas_clear(cv);                          /* 清掉录制内容 */
    for (i = 0; i < count; i++) {
        const draw_op_t *o = &s_ops[i];

        cg_canvas_set_color(cv, o->color);
        cg_canvas_set_line_width(cv, o->penw);
        switch (o->type) {
        case OP_LINE:    cg_canvas_line(cv, o->x1, o->y1, o->x2, o->y2); break;
        case OP_RECT:    cg_canvas_rect(cv, o->x1, o->y1, o->x2 - o->x1, o->y2 - o->y1, 0); break;
        case OP_ELLIPSE: cg_canvas_ellipse(cv, o->x1, o->y1, o->x2 - o->x1, o->y2 - o->y1, 0); break;
        case OP_TEXT:    cg_canvas_text(cv, o->x1, o->y1, o->text); break;
        default:         break;
        }
    }
}

/**
 * @brief 按已提交的 s_live 条命令整体重画画板
 * @param cv 画板控件；NULL 安全
 * @return void 无返回值
 */
static void replay_canvas(cg_widget_t *cv)
{
    replay_canvas_n(cv, s_live);
}

/**
 * @brief 拖动预览（橡皮筋）：已提交的命令 + 一条还没提交的临时命令
 * @param cv 画板控件
 * @param o 临时命令；传 NULL 表示只重放已提交的（等于取消预览）
 * @return void 无返回值
 * @note 画板是"录制命令 → 重绘时按序回放"的模型，**没有"画一笔但不作数"的接口** —— 所以预览
 *       只能整表重放一次：把临时命令写进命令表的第一个空槽（**不推进 s_live**），再按
 *       s_live + 1 条回放；下一帧会覆盖那个槽位，所以它始终算不上"已提交"。
 *       命令上限 512 条，一次重放是几百次 GDI 调用 —— 拖动时够快
 */
static void preview_op(cg_widget_t *cv, const draw_op_t *o)
{
    if (!cv) return;
    if (!o) {
        replay_canvas(cv);                        /* 取消预览：回到已提交的样子 */
        return;
    }
    if (s_live >= kMaxOps) return;                /* 表满：预览也没地方放（提交时同样会被拒） */
    s_ops[s_live] = *o;                           /* 临时占用第一个空槽，不推进 s_live */
    replay_canvas_n(cv, s_live + 1);
}

/**
 * @brief 把一条命令追加进命令表（撤销后再画会截断"重做"那部分）
 * @param o 待记录的命令
 * @return int 1 记录成功、0 命令表已满（固定数组，满了就丢新笔）
 */
static int cmd_push(const draw_op_t *o)
{
    if (s_live >= kMaxOps) return 0;
    s_ops[s_live] = *o;
    s_live++;
    s_used = s_live;
    return 1;
}

/**
 * @brief 点是否落在画板内
 * @param cv 画板控件
 * @param x 画板内 x（画板自己派发的鼠标事件给的就是画板内坐标）
 * @param y 画板内 y
 * @return int 1 落在画板内、0 落在画板外（这一下不算画）
 * @note 坐标口径 2026-09-28 起变了：画板有了自己的鼠标事件，事件里的坐标**已经是画板内**的
 *       逻辑像素，所以这里只做范围判断 —— 不再需要减掉画板的 x / y（那是窗口级事件时代的算法）
 */
static int canvas_hit(cg_widget_t *cv, int x, int y)
{
    return (x >= 0 && y >= 0 && x <= cg_widget_w(cv) && y <= cg_widget_h(cv)) ? 1 : 0;
}

/**
 * @brief 画板级鼠标事件：一个 handler 服务按下 / 拖动 / 抬起三个事件
 * @param self 画板控件（这三个事件由画板自己派发，self 就是画板）
 * @param ev 事件类型（按下 / 拖动 / 抬起之一）
 * @param user 未使用
 * @return void 无返回值
 * @note 坐标就是**画板内**的逻辑像素（画板自己的鼠标事件给的口径），拿到直接用；只有"起笔"与
 *       "每一段"做范围判断 —— 拖到画板外的点跳过，笔迹不会糊到边上。自由笔在拖动事件里逐段补线
 */
static void on_board_mouse(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cv  = self;                     /* 事件从画板来，cv 就是它自己 */
    draw_op_t    o;
    int          x, y;

    (void)user;
    if (!cg_app_has_window(app)) return;         /* 自检不开窗：合成事件下不画、也不报错 */

    x = cg_event_x(self);
    y = cg_event_y(self);

    if (ev == CG_EVENT_MOUSEDOWN) {
        cg_widget_t *cbo, *cp, *pe;

        if (!canvas_hit(cv, x, y)) return;       /* 起笔必须落在画板里 */
        cbo = cg_find(app, "cbo_tool");
        cp  = cg_find(app, "cp_ink");
        pe  = cg_find(app, "edt_pen");
        s_tool = cbo ? cg_widget_value(cbo) : TOOL_LINE;
        s_ink  = cp ? ((unsigned)cg_widget_value(cp) & 0xFFFFFFu) : 0x1E1E1Eu;
        s_penw = pe ? cg_widget_value(pe) : 2;
        if (s_penw < 1)  s_penw = 1;
        if (s_penw > 12) s_penw = 12;             /* 线宽夹到 1..12 */
        s_drawing = 1;
        s_x0 = x;
        s_y0 = y;
        s_lx = x;
        s_ly = y;
        s_px = x;
        s_py = y;
        return;
    }

    /* 事件名就是 CG_EVENT_* 这一套（别写成 CG_EV_*：2026-09-28 编译期撞过，gcc 会替你纠正） */
    if (ev == CG_EVENT_MOUSEMOVE) {
        cg_widget_t *sb = cg_find(app, "sb_main");

        if (!s_drawing) return;                  /* 没按下：悬停移动不做事（事件很频繁） */
        if (!canvas_hit(cv, x, y)) return;       /* 画板外：这一段跳过，笔迹不糊到边上 */

        if (s_tool == TOOL_FREEHAND) {
            memset(&o, 0, sizeof(o));
            o.type = OP_LINE;
            o.x1 = s_lx;
            o.y1 = s_ly;
            o.x2 = x;
            o.y2 = y;
            o.color = s_ink;
            o.penw  = s_penw;
            if (!cmd_push(&o)) {
                s_drawing = 0;
                note(app, "命令表已满（%d 条）", kMaxOps);
                return;
            }
            cg_canvas_set_color(cv, o.color);    /* 这一段直接补画，不必整块重放 */
            cg_canvas_set_line_width(cv, o.penw);
            cg_canvas_line(cv, o.x1, o.y1, o.x2, o.y2);
            s_lx = x;
            s_ly = y;
            refresh_status(app);
            return;
        }

        /* 其余工具：拖动中给"橡皮筋"预览，松手才提交（不预览的话用户看不到自己画到哪儿了）。
         * 同一个点重复上报时直接返回 —— 预览是整表重放，别为没变化的位置白重放一遍 */
        if (x == s_px && y == s_py) return;
        s_px = x;
        s_py = y;

        memset(&o, 0, sizeof(o));
        o.color = s_ink;
        o.penw  = s_penw;
        if (s_tool == TOOL_LINE) {
            o.type = OP_LINE;
            o.x1 = s_x0;
            o.y1 = s_y0;
            o.x2 = x;
            o.y2 = y;
            if (sb) cg_status_setf(sb, 2, "拖动中：直线 (%d,%d)→(%d,%d)", o.x1, o.y1, o.x2, o.y2);
        } else if (s_tool == TOOL_RECT || s_tool == TOOL_ELLIPSE) {
            o.type = (s_tool == TOOL_RECT) ? OP_RECT : OP_ELLIPSE;
            o.x1 = s_x0 < x ? s_x0 : x;          /* 与提交时同一套归一化：松手那一下画面不跳 */
            o.y1 = s_y0 < y ? s_y0 : y;
            o.x2 = s_x0 > x ? s_x0 : x;
            o.y2 = s_y0 > y ? s_y0 : y;
            if (sb) {
                cg_status_setf(sb, 2, "拖动中：%s %d×%d",
                               (s_tool == TOOL_RECT) ? "矩形" : "椭圆", o.x2 - o.x1, o.y2 - o.y1);
            }
        } else {
            return;                              /* 文字按按下点落笔，拖动里没有可预览的形变 */
        }
        preview_op(cv, &o);
        return;
    }

    if (!s_drawing) return;                      /* 剩下的就是抬起（没在画板上按下过） */
    s_drawing = 0;
    if (s_tool == TOOL_FREEHAND) {               /* 各段已在拖动里记下，这里只收尾 */
        note(app, "自由笔画完，共 %d 条命令", s_live);
        refresh_status(app);
        return;
    }
    if (!canvas_hit(cv, x, y)) return;           /* 收尾这一下也要在画板里 */

    memset(&o, 0, sizeof(o));
    o.color = s_ink;
    o.penw  = s_penw;
    if (s_tool == TOOL_LINE) {
        o.type = OP_LINE;
        o.x1 = s_x0;
        o.y1 = s_y0;
        o.x2 = x;
        o.y2 = y;
        note(app, "直线 (%d,%d)→(%d,%d) 色 #%06X 笔宽 %d", o.x1, o.y1, o.x2, o.y2, s_ink, s_penw);
    } else if (s_tool == TOOL_RECT || s_tool == TOOL_ELLIPSE) {
        o.type = (s_tool == TOOL_RECT) ? OP_RECT : OP_ELLIPSE;
        o.x1 = s_x0 < x ? s_x0 : x;              /* 归一化成 左上 / 右下 */
        o.y1 = s_y0 < y ? s_y0 : y;
        o.x2 = s_x0 > x ? s_x0 : x;
        o.y2 = s_y0 > y ? s_y0 : y;
        note(app, "%s (%d,%d)-(%d,%d) 色 #%06X",
             s_tool == TOOL_RECT ? "矩形" : "椭圆", o.x1, o.y1, o.x2, o.y2, s_ink);
    } else {                                     /* 文字：在按下点写一段固定文字 */
        o.type = OP_TEXT;
        o.x1 = s_x0;
        o.y1 = s_y0;
        snprintf(o.text, kOpTextLen, "%s", "CStackGUI 画板");
        note(app, "文字落在 (%d,%d)", o.x1, o.y1);
    }
    if (!cmd_push(&o)) {
        note(app, "命令表已满（%d 条），这一笔没记下", kMaxOps);
        return;
    }
    replay_canvas(cv);                           /* 进表后把绘制记录整体重建 */
    refresh_status(app);
}

/**
 * @brief 撤销 / 重做 / 清空：三个按钮共用一个 handler，靠控件 id 分辨
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 三者都只改"生效条数"，再"清空记录 + 重放"—— 画板没有擦掉某一笔的接口
 */
static void on_edit_op(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cv  = cg_find(app, "cv_board");
    const char  *id  = cg_widget_id(self);

    (void)ev;
    (void)user;
    if (!cv || !id) return;

    if (strcmp(id, "btn_undo") == 0) {
        if (s_live <= 0) {
            note(app, "没有可撤销的了");
            return;
        }
        s_live--;                                /* 丢弃最后一条（命令表里仍留着，给重做用） */
        note(app, "撤销一笔，剩 %d 笔", s_live);
    } else if (strcmp(id, "btn_redo") == 0) {
        if (s_live >= s_used) {
            note(app, "没有可重做的了");
            return;
        }
        s_live++;
        note(app, "重做一笔，共 %d 笔", s_live);
    } else if (strcmp(id, "btn_clear") == 0) {
        s_live = 0;
        s_used = 0;
        note(app, "已清空画板");
    } else {
        return;
    }
    replay_canvas(cv);
    refresh_status(app);
}

/**
 * @brief 把画板存成 PNG，落在用户数据目录（文件名带时间戳）
 * @param self 被点的导出按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 只存画板这一个控件；要连标题栏 / 工具行一起存就改用整窗截图那个接口。
 *       自检没有窗口，先问有没有窗口
 */
static void on_export(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cv  = cg_find(app, "cv_board");
    char        *dir, *name, *path, *now;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        note(app, "自检模式没有窗口，跳过导出");
        return;
    }
    if (!cv) return;

    dir = cg_app_data_dir();                     /* 用户数据目录，运行时保证已建 */
    now = cg_time_now("%Y%m%d_%H%M%S");
    if (!dir || !now) {
        note(app, "导出失败：拿不到用户数据目录或时间");
        cg_free(dir);
        cg_free(now);
        return;
    }
    name = cg_str_format("drawpad_%s.png", now);
    path = name ? cg_path_join(dir, name) : NULL;
    if (!path || !cg_widget_screenshot(cv, path)) {
        note(app, "导出失败：截图返回 0（图像库起不来或写盘失败）");
    } else {
        note(app, "已导出 %lld 字节：%s", cg_file_size(path), path);
    }
    cg_free(path);
    cg_free(name);
    cg_free(now);
    cg_free(dir);
    refresh_status(app);
}

/**
 * @brief 启动钩子：先画一组示例图案（只在真窗口下）
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 自检也会调这个钩子 —— 那时没有窗口，所以整段包在"有没有窗口"里，
 *       既不发绘制命令、也不写日志
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *cv = cg_find(app, "cv_board");
    draw_op_t    o;

    (void)user;
    if (!cg_app_has_window(app) || !cv) {
        cg_log(CG_LOG_INFO, "画板自检：没有窗口，跳过示例图案");
        return;
    }
    memset(&o, 0, sizeof(o));
    o.color = 0x6750A4;
    o.penw  = 3;
    o.type  = OP_LINE;
    o.x1 = 24;
    o.y1 = 24;
    o.x2 = 300;
    o.y2 = 24;
    cmd_push(&o);
    o.color = 0x333333;
    o.penw  = 2;
    o.type  = OP_RECT;
    o.x1 = 24;
    o.y1 = 64;
    o.x2 = 200;
    o.y2 = 200;
    cmd_push(&o);
    o.color = 0x1A73E8;
    o.type  = OP_ELLIPSE;
    o.x1 = 240;
    o.y1 = 64;
    o.x2 = 416;
    o.y2 = 200;
    cmd_push(&o);
    o.type = OP_TEXT;
    o.x1 = 24;
    o.y1 = 224;
    snprintf(o.text, kOpTextLen, "%s", "在画板上按下并拖动");
    cmd_push(&o);
    replay_canvas(cv);
    note(app, "示例图案已画好（%d 条命令）", s_live);
    refresh_status(app);
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_board_mouse", on_board_mouse),   /**< 画板的按下 / 拖动 / 抬起共用 */
    CG_BIND("on_edit_op",   on_edit_op),     /**< 撤销 / 重做 / 清空共用 */
    CG_BIND("on_export",    on_export),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int           selftest = (argc > 1 && strcmp(argv[1], "--selftest") == 0);

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.cgui_path        = (argc > (selftest ? 2 : 1)) ? argv[selftest ? 2 : 1] : "drawpad.cgui";
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动在 exe 旁边找 .cgui */
    desc.on_ready         = on_ready;

    if (selftest) return cg_app_selftest(&desc);

    cg_log(CG_LOG_INFO, "画板启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
