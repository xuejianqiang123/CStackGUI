/**
 * @file    sh.c
 * @brief   shottool —— 套件模块（原 examples\shottool 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 sh_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 sh_bindings、原 on_ready 变成 sh_init、
 *          窗口事件改成直连转发 sh_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   shottool 示例：QQ 风格截图工具 —— 托盘 + 全局热键 + 屏幕框选 + 标注 + 保存 / 复制 / 贴图
 * @details 这个示例讲的是"一个常驻后台的截图工具怎么活"，对应 QQScreenShot 那类工具的核心
 *          链路（它 README 里的 OCR / 录屏 / 截长图需要外部引擎或逆向 DLL，是本示例的
 *          **非目标**，能力边界照实写在 README 里）：
 *
 *          **触发**：全局热键 Ctrl+Alt+A（`cg_hotkey_set`，后台 / 托盘里也按得到）、面板
 *          按钮、托盘左键 —— 三个入口服务同一条动作 `shot`。热键被别的程序占用只会注册
 *          失败（写日志），面板按钮与托盘就是兜底入口。
 *
 *          **框选**：自己先藏起来 → 一次性定时器等一拍 → `cg_screen_capture` 抓整屏存
 *          PNG → 窗口切无边框铺满（`cg_win_set_border(app,"none")`），把这张图设成**画板
 *          背景图**（`cg_widget_set_image` 作用在 canvas 上，运行时新增的能力）→ 在画板上
 *          拖橡皮筋。窗口在 (0,0) 无边框铺满，所以**画板坐标 == 屏幕逻辑坐标**，选区直接
 *          就是下一次抓屏的参数。
 *
 *          **标注**：按选区重抓一块 → 窗口贴到选区原点（位置是**物理**像素，用 `cg_scale_px`
 *          换算；尺寸是**逻辑**像素）→ 截图当背景、标注是录制的绘制命令。标注条摆在
 *          **选区下方那条额外撑出来的行**里：不盖住画面，也不进成品。
 *
 *          **成品 = 抓画板自己**（`cg_widget_screenshot(cv_shot)`）：背景图 + 命令回放恰好
 *          就是"选区 + 标注"，不用裁剪、也不用把工具条藏来藏去。
 *
 *          标注命令的"真相来源"是本文件的固定命令表（同 `examples\base\drawpad`）：画板只有
 *          "整体回放"，没有"擦一笔"，撤销 = 丢掉最后一笔 → 清空 → 按剩下的重放。一笔可能由
 *          多条命令拼成（箭头 = 3 条线），所以按**笔**记结束下标，撤销整笔撤。
 *
 *          红线：全程只 include cstackgui.h 与标准头，不碰 Windows 头；凡开窗 / 抓屏 /
 *          写剪贴板 / 建托盘先问 `cg_app_has_window()`，`--selftest`（不开窗）才跑得干净；
 *          就绪钩子里不注册定时器、不读写文件。
 *
 * 编译：在项目根跑 build-app.bat shottool
 * 自检：shottool.exe --selftest shottool.cgui
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 常量 */

#define kMaxOps      4096                 /**< 绘制命令表上限（固定数组，绝不 malloc） */
#define kMaxStrokes  512                  /**< 笔画表上限：每笔记一个"结束下标"，撤销按笔走 */
#define kOpTextLen   48                   /**< 文字命令的固定缓冲 */
#define kPanelW      600                  /**< 面板缺省宽 —— 必须与 shottool.cgui 的 width 一致 */
#define kPanelH      440                  /**< 面板缺省高 —— 必须与 shottool.cgui 的 height 一致 */
#define kPanelBorder "sizable"            /**< 面板边框 —— 必须与 shottool.cgui 的 app.border 一致 */
#define kHotkey      "Ctrl+Alt+A"         /**< QQ 经典截图热键 */
#define kBarMinW     862                  /**< 标注条行的最小窗口宽（最右按钮 790+60+12） */
#define kBarRowTop   8                    /**< 标注条行与选区下沿的间距 */
#define kBarRowH     30                   /**< 标注条行高 */
#define kBarExtra    (kBarRowTop + kBarRowH + 8)  /**< 选区之外额外撑出的窗口高度 */

/** 工作流状态：空闲 → 抓屏中（藏窗口等一拍）→ 框选 → 标注 / 录制 → 贴图 */
enum { MODE_IDLE = 0, MODE_CAPTURING, MODE_SELECT, MODE_ANNOTATE, MODE_REC, MODE_PIN };
/** 命令类型（画板只认这四种；箭头是 3 条线拼的，不占新类型） */
enum { OP_LINE = 0, OP_RECT, OP_ELLIPSE, OP_TEXT };
/** 标注工具 = cbo_tool 下标 */
enum { TOOL_RECT = 0, TOOL_ELLIPSE, TOOL_ARROW, TOOL_PEN, TOOL_TEXT };
/** 窗口级 keydown 的键码（业务层不 include Windows 头，用数说话） */
enum { KEY_ESC = 27, KEY_ENTER = 13 };

/** 一条绘制命令：类型 + 参数 + 颜色 + 笔宽，重绘时按序回放 */
typedef struct {
    int      type;             /**< OP_* */
    int      x1, y1, x2, y2;   /**< 画板内坐标；矩形 / 椭圆存左上角与右下角 */
    unsigned color;            /**< 0xRRGGBB */
    int      penw;             /**< 线宽 */
    char     text[kOpTextLen]; /**< 仅文字命令用 */
} op_t;

/** 标注条一个控件的摆位：相对"行顶"的 dy + 尺寸（行顶 = 选区高 + kBarRowTop） */
typedef struct { const char *id; int x, dy, w, h; } bar_item_t;

/** 标注条布局（与 shottool.cgui 里的 rect.x/w 一致，y 由运行时按选区高度重算） */
static const bar_item_t k_bar[] = {
    { "lab_tool",     8, 4,  32, 24 },
    { "cbo_tool",    44, 3,  96, 28 },
    { "lab_color",  146, 4,  36, 24 },
    { "cbo_color",  186, 3,  76, 28 },
    { "lab_width",  268, 4,  36, 24 },
    { "cbo_width",  308, 3,  64, 28 },
    { "lab_text",   378, 4,  36, 24 },
    { "edt_text",   418, 3, 120, 28 },
    { "btn_undo",   546, 0,  54, 30 },
    { "btn_clear",  604, 0,  54, 30 },
    { "btn_finish", 662, 0,  64, 30 },
    { "btn_pin",    730, 0,  56, 30 },
    { "btn_cancel", 790, 0,  60, 30 },
};

/** 面板那一组控件（overlay 时全部藏起来） */
static const char *const k_panel[] = {
    "lab_title", "lab_tip1", "lab_tip2", "btn_shot", "btn_quit", "btn_rec",
    "lab_fps", "cbo_fps", "lst_log", "sb_main"
};

/** 标注颜色 = cbo_color 下标（截图上要能看清，红橙黄绿蓝白） */
static const unsigned k_colors[6] = {
    0xE53935u, 0xFB8C00u, 0xFDD835u, 0x43A047u, 0x1E88E5u, 0xFFFFFFu
};
/** 笔宽 = cbo_width 下标（细 / 中 / 粗） */
static const int k_widths[3] = { 2, 4, 8 };
/** 录屏帧率 = cbo_fps 下标（与 shottool.cgui 里 cbo_fps 的 items 顺序一致） */
static const int k_fps[4] = { 5, 10, 15, 20 };

/* ------------------------------------------------------------ 业务状态 */

static int s_mode;                                /**< MODE_* */
static int s_drag;                                /**< 1 = 正按下拖动 */
static int s_x0, s_y0, s_lx, s_ly;                /**< 按下点 / 自由笔上一点 */
static int s_base;                                /**< 起笔时的命令数（算这一笔加了几条） */
static int s_tool, s_penw;                        /**< 起笔时冻结的工具 / 笔宽 */
static int s_tool_sel;                            /**< 工具下拉框最近一次选中的下标 */
static int s_tool_known;                          /**< 1 = 收到过下拉框 select，s_tool_sel 可信 */
/** 工具名（日志用；下标必须与 shottool.cgui 里 cbo_tool 的 items 顺序一致） */
static const char *const k_tool_names[5] = { "矩形", "椭圆", "箭头", "画笔", "文字" };
static unsigned s_ink;                            /**< 起笔时冻结的颜色 */
static int s_sel_x, s_sel_y, s_sel_w, s_sel_h;    /**< 选区（逻辑像素，**画板坐标**） */
static int s_org_x, s_org_y;                     /**< 虚拟桌面原点（逻辑像素，可能为负） */
static int s_pan_x, s_pan_y;                      /**< 面板窗口位置（**物理**像素，move 事件给的） */
static int s_pan_w, s_pan_h;                      /**< 面板客户区尺寸（**逻辑**像素，size 事件给的） */
static int s_pan_valid;                           /**< 1 = 面板矩形记到过 */
static int s_cropping;                            /**< 1 = 框选已结束、正在等"重画干净"后抓选区 */
static int s_pending_rec;                        /**< 1 = 这次框选是要录 GIF（不是标注） */
static int s_fps = 10;                            /**< 录制帧率（面板上可换，默认 10） */
static int s_hot_ok;                              /**< 全局热键是否注册成功 */
static int s_shots;                               /**< 本次运行存下的张数 */
static int s_last_rw, s_last_rh;                  /**< 框选尺寸标签上次显示值（没变不重画） */
static int s_said_full;                           /**< 命令表满只报一次 */

static op_t s_ops[kMaxOps];                       /**< 命令表：前 s_live 条生效 */
static int  s_live;
static int  s_stroke_end[kMaxStrokes];            /**< 第 i 笔结束时的 s_live */
static int  s_nstroke;

/* ------------------------------------------------------------ 小助手 */

/**
 * @brief 往日志列表追加一行并写一份运行日志（不碰状态栏 —— 状态栏归 mode 信息所有）
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char         buf[256];
    va_list      va;
    cg_widget_t *lst = cg_find(app, "lst_log");

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    if (lst) {
        cg_widget_list_add(lst, buf);
        cg_widget_list_scroll_bottom(lst);
    }
    cg_log(CG_LOG_INFO, "%s", buf);
}

/**
 * @brief 状态模式的人话
 * @param m MODE_*
 * @return const char* 静态字符串
 */
static const char *mode_text(int m)
{
    switch (m) {
    case MODE_CAPTURING: return "抓屏中…";
    case MODE_SELECT:    return "框选中";
    case MODE_ANNOTATE:  return "标注中";
    case MODE_REC:       return "录制中";
    case MODE_PIN:       return "贴图中";
    default:             return "就绪";
    }
}

/**
 * @brief 把「当前模式 / 热键状态 / 已存张数」刷进状态栏
 * @param app 应用
 * @return void 无返回值
 */
static void status_refresh(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    if (!sb) return;
    cg_status_set(sb, 0, mode_text(s_mode));
    cg_status_set(sb, 1, s_hot_ok ? "热键 Ctrl+Alt+A 生效" : "热键未注册（用按钮 / 托盘）");
    cg_status_setf(sb, 2, "截图 %d 张", s_shots);
}

/**
 * @brief 读一个下拉框的当前下标（控件不在或越界时给缺省值）
 * @param app 应用
 * @param id 控件 id
 * @param def 缺省下标
 * @return int 下标
 */
static int cbo_int(cg_app_t *app, const char *id, int def)
{
    cg_widget_t *w = cg_find(app, id);
    int          v = w ? cg_widget_value(w) : def;

    return (v < 0) ? def : v;
}

/**
 * @brief 整数平方根（牛顿位运算法）—— 算箭头头部用，不引 math.h
 * @param x 被开方数（非负）
 * @return int floor(sqrt(x))
 */
static int isqrt(int x)
{
    int op = x, res = 0, one = 1 << 30;

    if (x <= 0) return 0;
    while (one > op) one >>= 2;
    while (one) {
        if (op >= res + one) { op -= res + one; res = (res >> 1) + one; }
        else                 { res >>= 1; }
        one >>= 2;
    }
    return res;
}

/**
 * @brief 一组控件整体显示 / 隐藏
 * @param app 应用
 * @param ids 控件 id 数组
 * @param n 数组长度
 * @param on 1 显示、0 隐藏
 * @return void 无返回值（缺控件跳过）
 */
static void set_visible_ids(cg_app_t *app, const char *const *ids, int n, int on)
{
    for (int i = 0; i < n; i++) {
        cg_widget_t *w = cg_find(app, ids[i]);
        if (w) cg_widget_set_visible(w, on);
    }
}

/**
 * @brief 把标注条整行摆到指定的行顶处
 * @param app 应用
 * @param row 行顶（窗口内 y）
 * @return void 无返回值
 * @note x / w 与 .cgui 一致，只有 y 是算出来的 —— 标注条跟着选区走
 */
static void bar_layout_at(cg_app_t *app, int row)
{
    for (size_t i = 0; i < sizeof(k_bar) / sizeof(k_bar[0]); i++) {
        cg_widget_t *w = cg_find(app, k_bar[i].id);
        if (w) cg_widget_set_rect(w, k_bar[i].x, row + k_bar[i].dy, k_bar[i].w, k_bar[i].h);
    }
}

/**
 * @brief 把标注条整行摆到"选区下沿 + kBarRowTop"处
 * @param app 应用
 * @return void 无返回值
 * @note 标注态：窗口 = 选区 + 撑出来的一条工具条行，标注条在这一行里 ——
 *       永远在**选区之外**，所以成品（抓画板）里不会有它
 */
static void bar_layout(cg_app_t *app)
{
    bar_layout_at(app, s_sel_h + kBarRowTop);
}

/* ------------------------------------------------------------ 命令表（同 drawpad 的套路） */

/**
 * @brief 按命令表前 count 条整体重画画板
 * @param cv 画板；NULL 安全
 * @param count 回放条数（拖动预览会用到 s_live + n）
 * @return void 无返回值
 * @note 画板没有"擦掉一笔"的接口：先清掉录制内容再逐条回放 —— 撤销 / 清空 / 预览全走这条路
 */
static void replay_n(cg_widget_t *cv, int count)
{
    int i;

    if (!cv) return;
    if (count > kMaxOps) count = kMaxOps;
    if (count < 0) count = 0;
    cg_canvas_clear(cv);
    for (i = 0; i < count; i++) {
        const op_t *o = &s_ops[i];

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
 * @brief 把一笔"箭头"拆成 3 条线命令（杆 + 两个倒钩）
 * @param shaft 杆命令（type 必须是 OP_LINE，箭头端是 x2,y2）
 * @param out 输出 3 条
 * @return int 恒为 3
 * @note 拆成 3 条线而不是新增画板命令类型：**一笔在命令表里是 3 条、在笔画表里是 1 笔**，
 *       撤销整笔撤，回放不用给画板加新语义
 */
static int arrow_ops(const op_t *shaft, op_t *out)
{
    int dx = shaft->x2 - shaft->x1;
    int dy = shaft->y2 - shaft->y1;
    int len = isqrt(dx * dx + dy * dy);
    int head, ux, uy, px, py, wing;

    out[0] = *shaft;
    out[1] = *shaft;
    out[2] = *shaft;
    if (len < 6) return 3;                     /* 太短画不出头，就当普通杆 */
    head = 8 + 2 * shaft->penw;
    if (head > len) head = len;
    ux = dx * head / len;
    uy = dy * head / len;
    px = -uy;                                  /* 单位化的垂直方向（模长同 u） */
    py =  ux;
    wing = head * 3 / 5;
    if (wing < 2) wing = 2;
    out[1].type = OP_LINE;
    out[1].x1 = shaft->x2;                     out[1].y1 = shaft->y2;
    out[1].x2 = shaft->x2 - ux + px * wing / head;
    out[1].y2 = shaft->y2 - uy + py * wing / head;
    out[2] = out[1];
    out[2].x2 = shaft->x2 - ux - px * wing / head;
    out[2].y2 = shaft->y2 - uy - py * wing / head;
    return 3;
}

/**
 * @brief 追加一条命令（表满返回 0，调用方负责提示）
 * @param o 待记录命令
 * @return int 1 记上、0 表满
 */
static int cmd_push(const op_t *o)
{
    if (s_live >= kMaxOps) return 0;
    s_ops[s_live++] = *o;
    return 1;
}

/**
 * @brief 收一笔：把"当前命令数"记成一笔的结束下标
 * @param app 应用（表满时提示用）
 * @return void 无返回值
 * @note 笔画表满了就把这一笔并进上一笔（撤销粒度变粗但不丢画面），只提示一次
 */
static void stroke_end(cg_app_t *app)
{
    if (s_nstroke < kMaxStrokes) {
        s_stroke_end[s_nstroke++] = s_live;
        return;
    }
    if (!s_said_full) {
        s_said_full = 1;
        log_line(app, "笔画表已满（%d 笔）：后续新笔并入上一笔撤销", kMaxStrokes);
    }
}

/**
 * @brief 撤掉最后一整笔
 * @return void 无返回值（没有笔时不动）
 */
static void undo_stroke(void)
{
    if (s_nstroke <= 0) return;
    s_nstroke--;
    s_live = (s_nstroke > 0) ? s_stroke_end[s_nstroke - 1] : 0;
}

/**
 * @brief 拖动预览：已提交的命令 + n 条还没提交的临时命令
 * @param cv 画板
 * @param ops 临时命令数组；NULL = 取消预览（回到已提交的样子）
 * @param n 临时命令条数
 * @return void 无返回值
 * @note 预览 = 把临时命令写进命令表第一个空槽（**不推进 s_live**）再按 s_live+n 回放；
 *       提交时同一个槽位被正式命令覆盖，所以预览永远算不上"已提交"
 */
static void preview_ops(cg_widget_t *cv, const op_t *ops, int n)
{
    if (!cv) return;
    if (!ops || n <= 0) { replay_n(cv, s_live); return; }
    if (s_live + n > kMaxOps) return;
    memcpy(&s_ops[s_live], ops, sizeof(op_t) * (size_t)n);
    replay_n(cv, s_live + n);
}

/**
 * @brief 命令表满的提示：每次"满"只报一条
 * @param app 应用
 * @return void 无返回值
 */
static void note_full(cg_app_t *app)
{
    if (s_live < kMaxOps) { s_said_full = 0; return; }
    if (s_said_full) return;
    s_said_full = 1;
    log_line(app, "命令表已满（%d 条）：点「清除」或撤销几笔再画", kMaxOps);
}

/**
 * @brief 把坐标夹进画板范围（框选拖出边界的点要夹回来，选区不能是负的）
 * @param cv 画板
 * @param x 入参为事件坐标，出参为夹好的值
 * @param y 同上
 * @return void 无返回值
 */
static void clamp_xy(cg_widget_t *cv, int *x, int *y)
{
    int w = cg_widget_w(cv);
    int h = cg_widget_h(cv);

    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > w) *x = w;
    if (*y > h) *y = h;
}

/* ------------------------------------------------------------ overlay / 面板切换 */

/**
 * @brief 面板那组控件整体显示 / 隐藏（画板、提示条、标注条各自单独管）
 * @param app 应用
 * @param on 1 显示、0 隐藏
 * @return void 无返回值
 */
static void panel_widgets(cg_app_t *app, int on)
{
    set_visible_ids(app, k_panel, (int)(sizeof(k_panel) / sizeof(k_panel[0])), on);
}

/**
 * @brief 标注条整行显示 / 隐藏
 * @param app 应用
 * @param on 1 显示、0 隐藏
 * @return void 无返回值
 */
static void bar_widgets(cg_app_t *app, int on)
{
    for (size_t i = 0; i < sizeof(k_bar) / sizeof(k_bar[0]); i++) {
        cg_widget_t *w = cg_find(app, k_bar[i].id);
        if (w) cg_widget_set_visible(w, on);
    }
}

/**
 * @brief 录制态工具条：只摆「停止」与「取消」，其余一律藏掉
 * @param app 应用
 * @param on 1 显示、0 隐藏
 * @return void 无返回值
 * @note 录制时工具条**不能压在选区上**（会盖住要录的画面），所以窗口收成
 *       "选区 + 一行工具条"，工具条那条行在选区下方 —— 与标注态同一招。
 *       只留这两个按钮：录制期间改工具 / 改颜色都没意义（改了也只能影响下一笔，
 *       而录制是自动抓屏、不落笔），留着反而让人误点。
 */
static void bar_rec_widgets(cg_app_t *app, int on)
{
    static const char *const keep[] = { "btn_finish", "btn_cancel" };
    int k;

    for (k = 0; k < (int)(sizeof(keep) / sizeof(keep[0])); k++) {
        cg_widget_t *w = cg_find(app, keep[k]);

        if (w) cg_widget_set_visible(w, on);
    }
}

/**
 * @brief 提示条（画布左上角那行）显示一句话 / 藏起来
 * @param app 应用
 * @param text 文本；NULL = 藏起来
 * @return void 无返回值
 */
static void mode_hint(cg_app_t *app, const char *text)
{
    cg_widget_t *lm = cg_find(app, "lab_mode");

    if (!lm) return;
    if (!text) { cg_widget_set_visible(lm, 0); return; }
    cg_widget_set_text(lm, text);
    cg_widget_set_visible(lm, 1);
}

/**
 * @brief 收起 overlay、把窗口还原成面板（取消 / 完成 / 贴图退出 / 抓屏失败都走这条）
 * @param app 应用
 * @return void 无返回值
 * @note 顺序有讲究：**先置 IDLE 再挪窗口** —— move / size 事件只在 IDLE 时记面板矩形，
 *       否则 overlay 的全屏矩形会把面板位置覆盖掉，下次回来窗口就飞了
 */
static void restore_panel(cg_app_t *app)
{
    int was = s_mode;
    cg_widget_t *cv;

    if (!cg_app_has_window(app)) { s_mode = MODE_IDLE; s_cropping = 0; return; }
    s_mode = MODE_IDLE;
    s_cropping = 0;                             /* 那一拍（等重画后抓选区）作废：收尾了就不需要了 */
    cg_win_show(app, 0);                        /* 挪位置期间先藏着，免得肉眼看见"跳过去" */

    cv = cg_find(app, "cv_shot");
    if (cv) {
        cg_widget_set_visible(cv, 0);
        cg_widget_set_image(cv, NULL);          /* 释放背景位图；临时 PNG 留在数据目录里 */
    }
    mode_hint(app, NULL);
    bar_widgets(app, 0);
    panel_widgets(app, 1);

    cg_win_set_border(app, kPanelBorder);
    cg_win_set_pos(app, s_pan_valid ? s_pan_x : 100, s_pan_valid ? s_pan_y : 100);
    cg_win_set_size(app, s_pan_w > 0 ? s_pan_w : kPanelW, s_pan_h > 0 ? s_pan_h : kPanelH);
    cg_win_set_topmost(app, 0);
    cg_win_show(app, 1);
    cg_win_focus(app);                          /* 回面板也抢一下：Ctrl+Shift+Q 这类快捷键要前台 */

    status_refresh(app);
    /* 只说"回面板"：存没存图是调用方的事（完成 / 取消 / 贴图收回各有各的说法），
     * 这里猜一句"未存图"会在"完成"那条路上写错 —— 实测就是这么错的 */
    log_line(app, (was == MODE_PIN) ? "贴图已收回，回面板" : "已回面板");
}

/* ------------------------------------------------------------ 截图流程 */

/**
 * @brief 拼一个临时 PNG 路径：用户数据目录 + tag.png
 * @param app 应用
 * @param tag 文件名主体（不含扩展名）
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 */
static char *shot_temp_path(cg_app_t *app, const char *tag)
{
    char *dir  = cg_app_data_dir();
    char *name = cg_str_format("%s.png", tag);
    char *path = (dir && name) ? cg_path_join(dir, name) : NULL;

    cg_free(name);
    cg_free(dir);
    return path;
}

/**
 * @brief 抓屏后的那一拍（一次性定时器回调）：抓整屏、把窗口切成全屏 overlay、进框选态
 * @param user cg_app_t*（定时器只给 void*，这里转回来）
 * @return void 无返回值
 * @note 为什么隔一拍：`cg_win_show(app,0)` 之后 DWM 还要几帧才真正把我们的窗口撤下屏，
 *       立刻抓的话截出来是**自己的面板**。150ms 是经验值，业务侧等不起就别藏窗口。
 */
static void capture_tick(void *user)
{
    cg_app_t    *app = (cg_app_t *)user;
    cg_widget_t *cv;
    int          sw = 0, sh = 0;
    char        *path;

    if (!app || s_mode != MODE_CAPTURING) return;
    if (!cg_app_has_window(app)) { s_mode = MODE_IDLE; return; }

    if (!cg_screen_size(app, &sw, &sh) || !cg_screen_origin(app, &s_org_x, &s_org_y)) {
        log_line(app, "抓屏失败：取不到虚拟桌面尺寸 / 原点");
        restore_panel(app);
        return;
    }
    path = shot_temp_path(app, "shot_full");
    if (!path || !cg_screen_capture(app, s_org_x, s_org_y, sw, sh, path)) {
        log_line(app, "抓屏失败：cg_screen_capture 返回 0（原因见日志）");
        cg_free(path);
        restore_panel(app);
        return;
    }

    cv = cg_find(app, "cv_shot");
    if (!cv) {
        log_line(app, "抓屏失败：找不到画板控件 cv_shot");
        cg_free(path);
        restore_panel(app);
        return;
    }

    /* 面板 → 无边框全屏：先样式、再位置、后尺寸（尺寸按当前样式算非客户区）。
     * 位置是**虚拟桌面原点**（左边竖了屏时是负的），过 cg_scale_px 换成物理像素 ——
     * cg_win_set_pos 按物理像素收参 */
    cg_win_set_border(app, "none");
    cg_win_set_pos(app, cg_scale_px(app, s_org_x), cg_scale_px(app, s_org_y));
    cg_win_set_size(app, sw, sh);
    cg_win_set_topmost(app, 1);
    panel_widgets(app, 0);

    cg_widget_set_rect(cv, 0, 0, sw, sh);
    cg_widget_set_image(cv, path);              /* 整屏截图 = 画板背景图 */
    cg_widget_set_visible(cv, 1);
    s_live = 0;
    s_nstroke = 0;
    s_said_full = 0;
    s_last_rw = s_last_rh = 0;
    cg_canvas_clear(cv);
    /* 框选态**不放**取消按钮：画板铺满全屏，任何子控件都在它下面接不到点击
     * （实测 WindowFromPoint 落在画板上）。取消走 Esc，或先框一块进标注态再点「取消」 */
    mode_hint(app, "按住左键拖动框选，松手进入标注；Esc 取消（取消按钮在标注态的工具条上）");

    s_mode = MODE_SELECT;
    cg_win_show(app, 1);
    /* 抢前台：不抢的话键盘消息不进本进程，表现为"按 Esc 没反应"（点过画布又好了）。
     * 被系统拒绝就照实说一句，用户还有鼠标与按钮两条路。 */
    if (!cg_win_focus(app)) {
        log_line(app, "没抢到前台（系统拒绝了这次请求）：Esc / 回车可能收不到，用鼠标或按钮操作");
    }
    status_refresh(app);
    log_line(app, "整屏已抓取（%dx%d 逻辑像素），按住拖动框选", sw, sh);
    cg_free(path);
}

/** @brief 前向声明：crop_tick 排在 annotate_begin 前面（定时器回调要用它）
 *  @param app 应用
 *  @return void 无返回值
 *  @note 声明上的注释也要写全 @param / @return：tools\check-comments.mjs 按定义处的规矩查
 */
static void annotate_begin(cg_app_t *app);
/** @brief 前向声明：同上，录制态的入口也在 crop_tick 之后
 *  @param app 应用
 *  @return void 无返回值
 */
static void rec_begin(cg_app_t *app);

/**
 * @brief 框选结束后隔的那一拍：画面已经重画干净，这时才抓选区
 * @param user cg_app_t*（定时器只给 void*，这里转回来）
 * @return void 无返回值
 * @note 为什么要隔一拍：取消预览与藏提示行都只是"标脏"，要等下一次重画才真正消失；
 *       立刻抓会把橡皮筋 / 提示行截进成品（实测成品图边缘带一圈黄框就是这个原因）。
 *       期间用户已经取消（Esc / 托盘）时 s_cropping 会被清掉，这一拍就空转
 */
static void crop_tick(void *user)
{
    cg_app_t *app = (cg_app_t *)user;

    if (!app || !s_cropping) return;
    s_cropping = 0;
    if (!cg_app_has_window(app) || s_mode != MODE_SELECT) return;
    if (s_pending_rec) { s_pending_rec = 0; rec_begin(app); return; }
    annotate_begin(app);
}

/**
 * @brief 框选结束：按选区重抓一块、窗口贴到选区原点、进标注态
 * @param app 应用
 * @return void 无返回值
 * @note 选区坐标是**画板坐标**（overlay 无边框铺满虚拟桌面、画板在客户区 0,0）。
 *       换成虚拟桌面坐标要加原点 s_org_*：抓屏参数与窗口位置都用加过偏移的那套 ——
 *       左边竖了屏时原点为负，不加偏移就会抓到主屏左上角那块去
 */
static void annotate_begin(cg_app_t *app)
{
    cg_widget_t *cv = cg_find(app, "cv_shot");
    int          bw = (s_sel_w < kBarMinW) ? kBarMinW : s_sel_w;
    int          bh = s_sel_h + kBarExtra;
    int          dx = s_org_x + s_sel_x;        /* 选区的虚拟桌面坐标 */
    int          dy = s_org_y + s_sel_y;
    char        *path;

    path = shot_temp_path(app, "shot_sel");
    if (!path || !cg_screen_capture(app, dx, dy, s_sel_w, s_sel_h, path)) {
        log_line(app, "选区抓屏失败：cg_screen_capture 返回 0（原因见日志）");
        cg_free(path);
        restore_panel(app);
        return;
    }
    if (!cv) {
        log_line(app, "找不到画板控件 cv_shot");
        cg_free(path);
        restore_panel(app);
        return;
    }

    s_mode = MODE_ANNOTATE;                     /* 先置：接下来的 move/size 不许记成面板矩形 */
    cg_win_set_pos(app, cg_scale_px(app, dx), cg_scale_px(app, dy));
    cg_win_set_size(app, bw, bh);

    cg_widget_set_rect(cv, 0, 0, s_sel_w, s_sel_h);
    cg_widget_set_image(cv, path);              /* 选区截图 = 新背景图 */
    s_live = 0;
    s_nstroke = 0;
    s_said_full = 0;
    cg_canvas_clear(cv);

    bar_layout(app);
    bar_widgets(app, 1);
    mode_hint(app, "标注：完成 = 存图并复制｜贴图 = 钉在屏幕｜取消或 Esc = 不存");

    status_refresh(app);
    log_line(app, "选区 %d,%d %dx%d：进标注（工具条在选区下方）", dx, dy, s_sel_w, s_sel_h);
    cg_free(path);
}

/**
 * @brief 框选结束且这次是"录屏"：按选区开始录，工具条贴在选区下方
 * @param app 应用
 * @return void 无返回值
 * @note 录制期间**绝不能**让窗口盖住选区：窗口是置顶的，盖上去之后抓屏抓到的是
 *       我们自己的窗口底（实测：录出来的 300x300 只有 28 种颜色、全程不变），
 *       而不是桌面。所以这里把画布藏掉、窗口收成**只有工具条那一行**，摆在选区
 *       **正下方** —— 选区露出的是真·桌面，才录得到实时变化。
 *       代价：录制时看不到选区边框，区域信息在提示条与日志里说
 */
static void rec_begin(cg_app_t *app)
{
    cg_widget_t *cv = cg_find(app, "cv_shot");
    int          bw = (s_sel_w < kBarMinW) ? kBarMinW : s_sel_w;
    int          dx = s_org_x + s_sel_x;
    int          dy = s_org_y + s_sel_y;

    if (!cv) {
        log_line(app, "找不到画板控件 cv_shot");
        restore_panel(app);
        return;
    }
    if (!cg_gif_rec_start(app, dx, dy, s_sel_w, s_sel_h, s_fps)) {
        log_line(app, "录屏起不来（原因见日志；自检模式没有窗口就会这样）");
        restore_panel(app);
        return;
    }
    s_mode = MODE_REC;                          /* 先置：接下来的 move/size 不许记成面板矩形 */
    cg_widget_set_visible(cv, 0);               /* 画布藏掉：它铺在选区上就是一堵墙 */
    bar_layout_at(app, kBarRowTop);             /* 工具条就是窗口的全部内容 */
    bar_rec_widgets(app, 1);
    cg_win_set_size(app, bw, kBarExtra);        /* 窗口只有工具条那么高 */
    cg_win_set_pos(app, cg_scale_px(app, dx), cg_scale_px(app, dy + s_sel_h));

    {
        char *hint = cg_str_format("录制中（区域 %dx%d）：点「停止」存 GIF；「取消」丢弃",
                                   s_sel_w, s_sel_h);

        mode_hint(app, hint ? hint : "录制中：点「停止」存 GIF；「取消」丢弃");
        cg_free(hint);
    }
    status_refresh(app);
    log_line(app, "录制开始：区域 %d,%d %dx%d @ %d fps", dx, dy, s_sel_w, s_sel_h, s_fps);
}

/**
 * @brief 收住录制：停止并按 save 决定落盘还是丢弃
 * @param app 应用
 * @param save 1 = 存到用户数据目录、0 = 丢弃
 * @return void 无返回值
 */
static void rec_finish(cg_app_t *app, int save)
{
    char *path = NULL;

    if (s_mode != MODE_REC) return;
    if (save) {
        char *dir = cg_app_data_dir();
        char *now = cg_time_now("Rec%Y%m%d_%H%M%S.gif");

        path = (dir && now) ? cg_path_join(dir, now) : NULL;
        cg_free(now);
        cg_free(dir);
    }
    if (!cg_gif_rec_stop(app, path)) {
        log_line(app, "录屏收尾失败（原因见日志）");
        cg_free(path);
        bar_widgets(app, 0);
        mode_hint(app, NULL);
        restore_panel(app);
        return;
    }
    if (path) {
        long long sz = cg_file_size(path);
        char     *tip = cg_str_format("GIF 已落到：%s（%lld 字节）", path, sz);

        s_shots++;
        log_line(app, "录制结束：%lld 字节 -> %s", sz, path);
        cg_tray_balloon(app, "录屏已存", tip ? tip : "GIF 已落到用户数据目录");
        cg_free(tip);
        cg_free(path);
    } else {
        log_line(app, "录制取消：这一段没存");
    }
    bar_widgets(app, 0);
    mode_hint(app, NULL);
    restore_panel(app);
}

/**
 * @brief 把成品（画板 = 背景图 + 标注）抓成 PNG，落在用户数据目录
 * @param app 应用
 * @return char * 堆串（文件路径），用完 cg_free；失败返回 NULL
 * @note 文件名 QQ 口味：ScreenShot + 日期时间；**抓画板自己**就够 —— 画板里恰好只有
 *       "选区 + 标注"，工具条在画板外面
 */
static char *save_result(cg_app_t *app)
{
    cg_widget_t *cv   = cg_find(app, "cv_shot");
    char        *dir  = cg_app_data_dir();
    char        *now  = cg_time_now("ScreenShot%Y%m%d_%H%M%S.png");
    char        *path = (dir && now) ? cg_path_join(dir, now) : NULL;

    cg_free(now);
    cg_free(dir);
    if (!path || !cv || !cg_widget_screenshot(cv, path)) {
        log_line(app, "存图失败：抓画板返回 0（原因见日志）");
        cg_free(path);
        return NULL;
    }
    s_shots++;
    log_line(app, "已存图 %lld 字节：%s", (long long)cg_file_size(path), path);
    return path;
}

/**
 * @brief 完成 / 贴图：存图 + 复制到剪贴板，然后回面板或留在屏幕上
 * @param app 应用
 * @param pin 0 = 完成（回面板）、1 = 贴图（留在屏幕上）
 * @return void 无返回值
 */
static void finish_shot(cg_app_t *app, int pin)
{
    char *path;

    if (!cg_app_has_window(app) || s_mode != MODE_ANNOTATE) return;
    path = save_result(app);
    if (path) {
        if (cg_clip_set_image(path)) log_line(app, "已复制到剪贴板，可直接粘贴");
        else                        log_line(app, "复制到剪贴板失败（原因见日志），文件在：%s", path);
    }
    if (!pin) {
        log_line(app, "完成：已存图并复制到剪贴板，回面板");
        cg_free(path);
        restore_panel(app);
        return;
    }

    /* 贴图：工具条与提示都收掉，窗口缩到选区本身 —— 屏幕上只剩那张图 */
    bar_widgets(app, 0);
    mode_hint(app, NULL);
    cg_win_set_size(app, s_sel_w, s_sel_h);
    s_mode = MODE_PIN;
    status_refresh(app);
    cg_tray_balloon(app, "贴图中", "截图已钉在屏幕上；双击贴图或按 Esc 返回面板。");
    log_line(app, "进入贴图模式（双击贴图 / Esc 回面板）");
    cg_free(path);
}

/**
 * @brief 触发一次截图（动作 shot：热键 / 面板按钮 / 托盘左键三个入口都到这里）
 * @param app 应用
 * @return void 无返回值
 */
static void shot_begin(cg_app_t *app)
{
    if (!cg_app_has_window(app)) {
        cg_log(CG_LOG_INFO, "自检模式没有窗口，跳过截图");
        return;
    }
    if (s_mode != MODE_IDLE) {
        log_line(app, "截图流程进行中（先完成或取消）");
        return;
    }
    s_mode = MODE_CAPTURING;
    status_refresh(app);
    log_line(app, "准备截图：先把面板藏起来…");
    cg_win_show(app, 0);
    cg_set_timer(app, 150, capture_tick, app);  /* 一次性：等 DWM 把面板撤下屏再抓 */
}

/* ------------------------------------------------------------ 画板鼠标：框选 + 标注 */

/**
 * @brief 画板鼠标事件：一个 handler 按当前模式分派（框选 / 标注 / 贴图双击）
 * @param self 画板（这三个事件由画板自己派发，self 就是画板）
 * @param ev mousedown / mousemove / mouseup / dblclick
 * @param user 未使用
 * @return void 无返回值
 * @note 坐标是**画板内**逻辑像素；窗口无边框在 (0,0) 铺满时它 == 屏幕逻辑坐标
 */
static void on_shot_mouse(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *cv  = self;
    int          x, y;

    (void)user;
    if (!cg_app_has_window(app)) return;         /* 自检合成事件：不画也不报错 */

    if (ev == CG_EVENT_DBLCLICK) {
        if (s_mode == MODE_PIN) {                /* 贴图态：双击贴图 = 回面板 */
            restore_panel(app);
            return;
        }
        /* 标注态：双击 = 两次 mousedown，文字工具会连着落两个。双击事件比第二次落笔
         * 晚到一步，这里把重复的那笔撤掉（只撤"最后落的是文字"这一种，别误伤刚画的形状） */
        if (s_mode == MODE_ANNOTATE && s_nstroke > 0 && s_ops[s_live - 1].type == OP_TEXT) {
            undo_stroke();
            replay_n(cv, s_live);
            log_line(app, "双击按一次算：已撤掉重复落的那一笔文字");
        }
        return;
    }
    x = cg_event_x(self);
    y = cg_event_y(self);

    /* ---------------- 框选态：拖一个橡皮筋矩形 ---------------- */
    if (s_mode == MODE_SELECT) {
        if (ev == CG_EVENT_MOUSEDOWN) {
            clamp_xy(cv, &x, &y);
            s_drag = 1;
            s_x0 = x;
            s_y0 = y;
            return;
        }
        if (ev == CG_EVENT_MOUSEMOVE) {
            op_t o;

            if (!s_drag) return;
            clamp_xy(cv, &x, &y);
            memset(&o, 0, sizeof(o));
            o.type  = OP_RECT;
            o.x1 = (s_x0 < x) ? s_x0 : x;
            o.y1 = (s_y0 < y) ? s_y0 : y;
            o.x2 = (s_x0 > x) ? s_x0 : x;
            o.y2 = (s_y0 > y) ? s_y0 : y;
            o.color = 0xFDD835u;                 /* 黄色：深浅底图上都看得见 */
            o.penw  = 2;
            preview_ops(cv, &o, 1);
            if (o.x2 - o.x1 != s_last_rw || o.y2 - o.y1 != s_last_rh) {
                cg_widget_t *lm = cg_find(app, "lab_mode");
                char         buf[128];

                s_last_rw = o.x2 - o.x1;
                s_last_rh = o.y2 - o.y1;
                snprintf(buf, sizeof(buf), "选区 %d x %d（松手进标注，Esc 取消）", s_last_rw, s_last_rh);
                if (lm) cg_widget_set_text(lm, buf);
            }
            return;
        }
        /* 抬起 */
        if (!s_drag) return;
        s_drag = 0;
        preview_ops(cv, NULL, 0);                /* 取消预览，画面回到干净底图 */
        clamp_xy(cv, &x, &y);                    /* 松手点可能在画布外：先夹回范围 */
        {
            int rx = (s_x0 < x) ? s_x0 : x;
            int ry = (s_y0 < y) ? s_y0 : y;
            int rw = ((s_x0 > x) ? s_x0 : x) - rx;
            int rh = ((s_y0 > y) ? s_y0 : y) - ry;

            if (rw < 8 || rh < 8) {
                log_line(app, "选区太小（%dx%d，至少 8x8），请重新框", rw, rh);
                return;
            }
            s_sel_x = rx;
            s_sel_y = ry;
            s_sel_w = rw;
            s_sel_h = rh;

            /* 抓选区之前先让画面干净：取消预览那一帧还没重画出去，立刻抓的话橡皮筋
             * （以及压在画板上、同样会被抓进去的提示行）会留在成品里。藏掉提示行 + 隔
             * 一拍再抓（一次性定时器），抓到的就是纯桌面，不是"我们的 overlay"。 */
            mode_hint(app, NULL);
            s_cropping = 1;
            cg_set_timer(app, 120, crop_tick, app);
        }
        return;
    }

    /* ---------------- 标注态 ---------------- */
    if (s_mode != MODE_ANNOTATE) return;

    if (ev == CG_EVENT_MOUSEDOWN) {
        cg_widget_t *ed;

        if (s_drag) return;
        clamp_xy(cv, &x, &y);
        s_tool = s_tool_known ? s_tool_sel : cbo_int(app, "cbo_tool", TOOL_RECT);
        s_ink  = k_colors[cbo_int(app, "cbo_color", 0) % 6];
        s_penw = k_widths[cbo_int(app, "cbo_width", 1) % 3];
        s_base = s_live;
        s_x0 = x;
        s_y0 = y;
        s_lx = x;
        s_ly = y;

        if (s_tool == TOOL_TEXT) {               /* 文字：按下即落笔，不进拖动 */
            char        buf[kOpTextLen];
            const char *txt;
            op_t        o;

            ed = cg_find(app, "edt_text");
            txt = ed ? cg_widget_text(ed) : NULL;
            if (!txt || !*txt) {
                log_line(app, "文字内容是空的：先在「文字」框里输入内容");
                return;
            }
            snprintf(buf, sizeof(buf), "%.*s", kOpTextLen - 1, txt);
            memset(&o, 0, sizeof(o));
            o.type = OP_TEXT;
            o.x1 = x;
            o.y1 = y;
            o.color = s_ink;
            o.penw = s_penw;
            memcpy(o.text, buf, kOpTextLen);
            if (!cmd_push(&o)) { note_full(app); return; }
            stroke_end(app);
            replay_n(cv, s_live);
            log_line(app, "文字落在 (%d,%d)", x, y);
            return;
        }
        s_drag = 1;
        return;
    }

    if (ev == CG_EVENT_MOUSEMOVE) {
        op_t o;

        if (!s_drag) return;
        clamp_xy(cv, &x, &y);

        if (s_tool == TOOL_PEN) {
            int dx = x - s_lx;
            int dy = y - s_ly;

            /* 贴着上一个点的移动不记：命令表是固定数组，鼠标每像素一条几秒就满 */
            if (dx < 2 && dx > -2 && dy < 2 && dy > -2) return;
            memset(&o, 0, sizeof(o));
            o.type = OP_LINE;
            o.x1 = s_lx;
            o.y1 = s_ly;
            o.x2 = x;
            o.y2 = y;
            o.color = s_ink;
            o.penw = s_penw;
            if (!cmd_push(&o)) { s_drag = 0; note_full(app); return; }
            cg_canvas_set_color(cv, o.color);    /* 这一段直接补画，不必整表重放 */
            cg_canvas_set_line_width(cv, o.penw);
            cg_canvas_line(cv, o.x1, o.y1, o.x2, o.y2);
            s_lx = x;
            s_ly = y;
            return;
        }
        if (s_tool == TOOL_TEXT) return;         /* 文字没有拖动形变 */

        memset(&o, 0, sizeof(o));
        o.color = s_ink;
        o.penw = s_penw;
        if (s_tool == TOOL_ARROW) {
            op_t tmp[3];

            o.type = OP_LINE;
            o.x1 = s_x0;
            o.y1 = s_y0;
            o.x2 = x;
            o.y2 = y;
            arrow_ops(&o, tmp);
            preview_ops(cv, tmp, 3);
            return;
        }
        o.type = (s_tool == TOOL_ELLIPSE) ? OP_ELLIPSE : OP_RECT;
        o.x1 = (s_x0 < x) ? s_x0 : x;
        o.y1 = (s_y0 < y) ? s_y0 : y;
        o.x2 = (s_x0 > x) ? s_x0 : x;
        o.y2 = (s_y0 > y) ? s_y0 : y;
        preview_ops(cv, &o, 1);
        return;
    }

    /* ---------------- 抬起：提交这一笔 ---------------- */
    if (ev != CG_EVENT_MOUSEUP || !s_drag) return;
    s_drag = 0;
    clamp_xy(cv, &x, &y);

    if (s_tool == TOOL_PEN) {
        if (s_live > s_base) stroke_end(app);    /* 一段都没画就不算一笔 */
        status_refresh(app);
        return;
    }

    {
        op_t o;

        memset(&o, 0, sizeof(o));
        o.color = s_ink;
        o.penw = s_penw;
        if (s_tool == TOOL_ARROW) {
            op_t tmp[3];

            o.type = OP_LINE;
            o.x1 = s_x0;
            o.y1 = s_y0;
            o.x2 = x;
            o.y2 = y;
            if (x == s_x0 && y == s_y0) { preview_ops(cv, NULL, 0); return; }
            arrow_ops(&o, tmp);
            if (s_live + 3 > kMaxOps) { preview_ops(cv, NULL, 0); note_full(app); return; }
            memcpy(&s_ops[s_live], tmp, sizeof(tmp));
            s_live += 3;
            stroke_end(app);
            replay_n(cv, s_live);
            log_line(app, "箭头 (%d,%d)→(%d,%d)", o.x1, o.y1, o.x2, o.y2);
            return;
        }
        o.type = (s_tool == TOOL_ELLIPSE) ? OP_ELLIPSE : OP_RECT;
        o.x1 = (s_x0 < x) ? s_x0 : x;
        o.y1 = (s_y0 < y) ? s_y0 : y;
        o.x2 = (s_x0 > x) ? s_x0 : x;
        o.y2 = (s_y0 > y) ? s_y0 : y;
        if (o.x2 - o.x1 < 2 || o.y2 - o.y1 < 2) {/* 拖了个点：不算一笔 */
            preview_ops(cv, NULL, 0);
            return;
        }
        if (!cmd_push(&o)) { preview_ops(cv, NULL, 0); note_full(app); return; }
        stroke_end(app);
        replay_n(cv, s_live);
        log_line(app, "%s (%d,%d)-(%d,%d) 色 #%06X 笔宽 %d",
                 (s_tool == TOOL_ELLIPSE) ? "椭圆" : "矩形", o.x1, o.y1, o.x2, o.y2, s_ink, s_penw);
    }
}

/* ------------------------------------------------------------ 按钮与窗口事件 */

/**
 * @brief 标注条按钮：撤销 / 清除 / 完成 / 贴图 / 取消，一个 handler 靠控件 id 分派
 * @param self 被点的按钮
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tool_btn(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    const char  *id  = cg_widget_id(self);
    cg_widget_t *cv  = cg_find(app, "cv_shot");

    (void)ev;
    (void)user;
    if (!id || !cg_app_has_window(app)) return;
    /* 录制态只认「停止」「取消」：其余（撤销 / 清除 / 贴图）在录制期间没有意义，
     * 而且它们整排都藏在下面，只有这两个被 bar_rec_widgets 摆出来 */
    if (s_mode == MODE_REC) {
        if (strcmp(id, "btn_finish") == 0)      rec_finish(app, 1);
        else if (strcmp(id, "btn_cancel") == 0) rec_finish(app, 0);
        return;
    }
    if (s_mode != MODE_ANNOTATE) return;         /* 这排按钮只在标注态有意义 */

    if (strcmp(id, "btn_undo") == 0) {
        if (s_nstroke <= 0) { log_line(app, "没有可撤销的了"); return; }
        undo_stroke();
        replay_n(cv, s_live);
        log_line(app, "撤销一笔，剩 %d 笔", s_nstroke);
    } else if (strcmp(id, "btn_clear") == 0) {
        s_live = 0;
        s_nstroke = 0;
        replay_n(cv, s_live);
        log_line(app, "标注已清空（截图背景不动）");
    } else if (strcmp(id, "btn_finish") == 0) {
        finish_shot(app, 0);
    } else if (strcmp(id, "btn_pin") == 0) {
        finish_shot(app, 1);
    } else if (strcmp(id, "btn_cancel") == 0) {
        restore_panel(app);
    }
}

/**
 * @brief 工具下拉框换了选项：记一行日志，并把当前工具存下来
 * @param self 下拉框
 * @param ev 恒为 select
 * @param user 未使用
 * @return void 无返回值
 * @note 起笔时用的工具以这里存的值为准：下拉框的**选择变化**由这条事件通知，
 *       比"落笔那一刻再去读"更能说清"用户到底选了什么"（也便于排查"选了没生效"）
 */
static void on_tool_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       idx;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    idx = cbo_int(app, "cbo_tool", TOOL_RECT);
    if (idx > TOOL_TEXT) idx = TOOL_RECT;
    s_tool_sel = idx;
    s_tool_known = 1;
    log_line(app, "工具：%s", k_tool_names[idx]);
}

/**
 * @brief 窗口级键盘：Esc = 取消 / 退出贴图，回车 = 完成
 * @param self 窗口伪控件
 * @param ev 恒为 keydown
 * @param user 未使用
 * @return void 无返回值
 * @note 焦点在标注条的输入框里时 EDIT 会吃掉按键（窗口级 keydown 收不到）——
 *       那就用「取消 / 完成」按钮，键盘与按钮两条路都在
 */
static void on_key(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       key = cg_event_key(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app) || s_mode == MODE_IDLE) return;

    if (key == KEY_ESC) {
        restore_panel(app);
    } else if (key == KEY_ENTER) {
        if (s_mode == MODE_ANNOTATE) finish_shot(app, 0);
        else if (s_mode == MODE_PIN)  restore_panel(app);
    }
}

/**
 * @brief 关闭 = 藏进托盘（不是退出）；真正退出走动作 quit
 * @param self 窗口伪控件
 * @param ev 恒为 close
 * @param user 未使用
 * @return void 无返回值
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    cg_cancel_close(app);
    if (!cg_app_has_window(app)) return;
    cg_win_show(app, 0);
    log_line(app, "窗口已藏进托盘（进程继续跑），退出用「退出」按钮或 Ctrl+Shift+Q");
    cg_tray_balloon(app, "还在后台待命", "左键点托盘图标 = 直接开始截图；右键 = 叫回面板。");
}

/**
 * @brief 托盘被点：左键 = 截图，双击 / 右键 = 叫回面板
 * @param self 窗口伪控件
 * @param ev 恒为 tray
 * @param user 未使用
 * @return void 无返回值
 * @note 动作码 1 = 左键单击、2 = 左键双击、3 = 右键单击（框架不会自动弹菜单）
 */
static void on_tray(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       act = cg_tray_action(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) return;

    if (act == 1) {
        if (s_mode == MODE_IDLE)      shot_begin(app);
        else if (s_mode == MODE_PIN)  { restore_panel(app); shot_begin(app); }
        else                          log_line(app, "截图流程进行中（先完成或取消）");
    } else {
        if (s_mode != MODE_IDLE && s_mode != MODE_CAPTURING) restore_panel(app);
        cg_win_show(app, 1);
        cg_win_set_topmost(app, 1);
        log_line(app, "从托盘叫回面板");
    }
}

/**
 * @brief 窗口被移动：只在面板态记位置（物理像素 —— 还原时原样喂给 cg_win_set_pos）
 * @param self 窗口伪控件
 * @param ev 恒为 move
 * @param user 未使用
 * @return void 无返回值
 */
static void on_move(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    if (s_mode != MODE_IDLE) return;
    s_pan_x = cg_event_x(self);
    s_pan_y = cg_event_y(self);
    s_pan_valid = 1;
}

/**
 * @brief 窗口尺寸变化：只在面板态记尺寸（**逻辑像素** —— 原样喂给 cg_win_set_size）
 * @param self 窗口伪控件
 * @param ev 恒为 size
 * @param user 未使用
 * @return void 无返回值
 */
static void on_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    if (s_mode != MODE_IDLE) return;
    s_pan_w = cg_event_x(self);
    s_pan_h = cg_event_y(self);
}

/**
 * @brief 动作回调：一个 handler 服务 shot / quit 两条命令
 * @param self 主窗口伪控件（动作回调恒为它）
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_action(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    const char  *name = cg_action_name(self);

    (void)ev;
    (void)user;
    if (!name) return;

    if (strcmp(name, "sh_shot") == 0) {
        shot_begin(app);
    } else if (strcmp(name, "sh_rec") == 0) {
        /* 帧率在**开始那一刻**读一次：录到一半改下拉没有意义（改了也只能影响之后的段） */
        s_fps = k_fps[cbo_int(app, "cbo_fps", 1) % 4];
        s_pending_rec = 1;                        /* 也要先框一块区域：走同一套框选 overlay */
        shot_begin(app);
    } else if (strcmp(name, "sh_quit") == 0) {
        log_line(app, "退出：先摘托盘图标，再关消息循环");
        if (cg_app_has_window(app)) cg_tray_remove(app);   /* 少这句托盘里会留死图标 */
        cg_quit(app);
    }
}

/* ------------------------------------------------------------ 启动钩子 */

/**
 * @brief 启动钩子：建托盘提示、注册全局热键、写第一屏日志
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 自检不开窗：整段包在"有没有窗口"里，也不注册定时器、不读写文件
 */
void sh_init(cg_app_t *app, void *user)
{
    (void)user;
    if (!cg_app_has_window(app)) {
        cg_log(CG_LOG_INFO, "shottool 自检：没有窗口，跳过托盘与全局热键初始化");
        return;
    }

    cg_tray_set(app, NULL, "截图工具：左键 = 截图，右键 = 叫回面板");
    s_hot_ok = cg_hotkey_set(app, kHotkey, "sh_shot");
    log_line(app, s_hot_ok ? "全局热键 %s 已注册（后台 / 托盘里也按得到）"
                           : "全局热键 %s 注册失败（多半被别的程序占用）——用按钮 / 托盘兜底",
             kHotkey);
    log_line(app, "流程：热键 / 按钮 / 托盘 → 拖动框选 → 标注 → 完成（存图并复制）");
    status_refresh(app);
}

/* ------------------------------------------------------------ 绑定表 */

const cg_binding_t sh_bindings[] = {
    CG_BIND("sh_on_action",     on_action),
    CG_BIND("sh_on_shot_mouse", on_shot_mouse),
    CG_BIND("sh_on_tool_btn",   on_tool_btn),
    CG_BIND("sh_on_tool_change", on_tool_change),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv argv[1] 是 .cgui 路径；写 "--selftest" 则不开窗自检
 * @return int 进程退出码：0 = 正常
 */

/**
 * @brief 窗口事件 close 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev close
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void sh_app_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_close(self, ev, user);
}

/**
 * @brief 窗口事件 tray 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev tray
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void sh_app_tray(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_tray(self, ev, user);
}

/**
 * @brief 窗口事件 keydown 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev keydown
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void sh_app_keydown(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_key(self, ev, user);
}

/**
 * @brief 窗口事件 size 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev size
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void sh_app_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_size(self, ev, user);
}

/**
 * @brief 窗口事件 move 的套件转发入口（main 的分发器调进来；**不进绑定表** —— 直接调用而非按名派发）
 * @param self 窗口伪控件
 * @param ev move
 * @param user 业务数据（透传给原 handler）
 * @return void 无返回值
 */
void sh_app_move(cg_widget_t *self, cg_event_t ev, void *user)
{
    on_move(self, ev, user);
}
