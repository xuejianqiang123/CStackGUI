/**
 * @file    pl.c
 * @brief   palette —— 套件模块（原 examples\palette 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 pl_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 pl_bindings、原 on_ready 变成 pl_init。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   调色板 / 取色工具：颜色换算 + 8 格调色板 + 导出 CSS / HEX
 * @details 只读工具，不接硬件。三件事：
 *
 *          1. **当前色**：一个 `colorpicker` 加一个「取色」按钮（弹系统取色器）。选出的颜色会
 *             换算成 `#RRGGBB` / `rgb()` / `hsl()` / 相对亮度 / 与白与黑的对比度，并给出
 *             "该配白字还是黑字"（按 WCAG 的对比度公式算，阈值 4.5:1 记作达标 AA）。
 *          2. **调色板**：8 个 `colorpicker` 当色格；「加入调色板」把当前色填进第一个空位，
 *             「清空」复位；改任何色格都会把它设成当前色。
 *          3. **导出**：8 格拼成 CSS 变量（`--pal-1: #RRGGBB;`）与纯 HEX 列表两份文本写进
 *             用户数据目录，另给一个「复制 HEX」按钮。
 *
 *          【一条踩出来的经验】改 `colorpicker` 的颜色**就用 `cg_widget_set_value()`**：
 *          控件对颜色值是特判的、不按 `min`/`max` 夹取（格式文档里"别用它改颜色、会被区间截断"
 *          那句已经过时 —— 2026-09-28 读源码核实过）。
 *
 *          自检（--selftest）不开窗：就绪钩子只往控件里填种子色，**不读盘、不写盘、不弹窗**；
 *          弹取色器 / 写盘 / 写剪贴板之前一律先问有没有窗口。
 *
 *          编译：在项目根跑 build-app.bat palette
 */
#include "cstackgui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** 色格数量 */
#define PAL_N    8
/** WCAG AA 正文对比度阈值 */
#define AA_RATIO 4.5
/** 导出 / 拼串用的文本缓冲 */
#define TXT_CAP  512

static unsigned s_cur = 0x6750A4; /**< 当前色（初值 = 种子色第 1 格） */
static unsigned s_pal[PAL_N];     /**< 8 个色格；0 = 空位 */

/**
 * @brief 往底部终端记一行、镜像到控制台，并把同一句写进状态栏第 1 段
 * @param app 应用
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void note(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_term_write(cg_find(app, "term_out"), CG_TERM_SYS, buf, -1);
    cg_status_set(cg_find(app, "sb_main"), 0, buf);
    printf("[palette] %s\n", buf);
}

/**
 * @brief 记下当前色：写回控件、按 WCAG 换算并显示、更新状态栏
 * @param app 应用
 * @param rgb 0xRRGGBB（超出 24 位的部分被截掉）
 * @return void 无返回值
 * @note 相对亮度 L = 0.2126R + 0.7152G + 0.0722B，分量先线性化：c <= 0.03928 走 c/12.92，
 *       否则走 ((c+0.055)/1.055)^2.4；对比度 = (L亮 + 0.05) / (L暗 + 0.05)。
 *       白底的 L = 1、黑底的 L = 0，所以"对白"就是 1.05/(L+0.05)、"对黑"是 (L+0.05)/0.05，
 *       哪个大就该配哪种字色
 */
static void show_color(cg_app_t *app, unsigned rgb)
{
    unsigned r, g, b;
    int      mx, mn, i, h = 0, s = 0, l;
    double   lin[3], lum, cw, cb, best;
    char     out[TXT_CAP];

    s_cur = rgb & 0xFFFFFFu;
    cg_widget_set_value(cg_find(app, "cp_cur"), (int)s_cur);

    r = (s_cur >> 16) & 0xFFu;
    g = (s_cur >> 8) & 0xFFu;
    b = s_cur & 0xFFu;

    mx = (int)(r > g ? r : g);
    mn = (int)(r < g ? r : g);
    lin[0] = (double)r;
    lin[1] = (double)g;
    lin[2] = (double)b;
    for (i = 0; i < 3; i++) {
        double c = lin[i] / 255.0;

        lin[i] = (c <= 0.03928) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
    }
    lum = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
    cw  = 1.05 / (lum + 0.05);
    cb  = (lum + 0.05) / 0.05;
    best = (cw >= cb) ? cw : cb;

    if (mx < (int)b) mx = (int)b;
    if (mn > (int)b) mn = (int)b;
    l = (mx + mn) * 100 / 510;
    if (mx != mn) {
        s = (l <= 50) ? (mx - mn) * 100 / (mx + mn) : (mx - mn) * 100 / (510 - mx - mn);
        if (mx == (int)r)      h = (((int)g - (int)b) * 60 / (mx - mn) + 360) % 360;
        else if (mx == (int)g) h = ((int)b - (int)r) * 60 / (mx - mn) + 120;
        else                   h = ((int)r - (int)g) * 60 / (mx - mn) + 240;
    }

    snprintf(out, sizeof(out),
             "#RRGGBB  #%02X%02X%02X\r\n"
             "rgb()    rgb(%u, %u, %u)\r\n"
             "hsl()    hsl(%d, %d%%, %d%%)\r\n"
             "相对亮度 %.4f\r\n"
             "对白对比 %.2f:1\r\n"
             "对黑对比 %.2f:1\r\n"
             "建议字色 %s（对比 %.2f:1，%s）",
             r, g, b, r, g, b, h, s, l, lum, cw, cb,
             (cw >= cb) ? "白字 #FFFFFF" : "黑字 #000000",
             best, (best >= AA_RATIO) ? "达标 AA" : "偏低（装饰用可以，正文别用）");
    cg_widget_set_text(cg_find(app, "edt_info"), out);
    note(app, "当前色 #%06X", s_cur);
}

/**
 * @brief 当前色或任一色格被改：把它的颜色设为当前色
 * @param self 被改的 colorpicker（当前色或某个色格）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note colorpicker 只有 change 一个事件，且是"弹系统取色器点确定"之后才发 ——
 *       没有单独的"被点击"事件，所以色格靠 change 来设当前色
 */
static void on_color_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    unsigned rgb = (unsigned)cg_widget_value(self);

    (void)ev;
    (void)user;
    if (rgb) show_color(cg_app_of(self), rgb);
}

/**
 * @brief 「取色」按钮：弹系统取色器，确定后设为当前色
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹对话框是副作用：自检（不开窗）下只记一行日志就返回
 */
static void on_pick_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    unsigned  rgb = s_cur;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        note(app, "自检模式：跳过弹取色器");
        return;
    }
    if (cg_choose_color(self, &rgb)) show_color(app, rgb);
    else                             note(app, "取色：已取消");
}

/**
 * @brief 调色板四个按钮共用一个回调，靠控件 id 分流
 * @param self 被点的按钮（加入 / 清空 / 导出 / 复制）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写盘与写剪贴板都是副作用：自检（不开窗）下跳过
 */
static void on_pal_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    char        cp[8], css[TXT_CAP], hex[TXT_CAP];
    int         i, n, off;

    (void)ev;
    (void)user;
    if (!id) return;

    if (strcmp(id, "btn_add") == 0) {                  /* 加入调色板：填进第一个空位 */
        for (i = 0; i < PAL_N && s_pal[i]; i++) { }
        if (i == PAL_N) {
            note(app, "加入失败：8 格已满（先清空或改某个色格）");
            return;
        }
        s_pal[i] = s_cur;
        snprintf(cp, sizeof(cp), "cp_%d", i + 1);
        cg_widget_set_value(cg_find(app, cp), (int)s_cur);
        note(app, "加入第 %d 格：#%06X", i + 1, s_cur);
    } else if (strcmp(id, "btn_clear") == 0) {         /* 清空：8 格复位成空位 */
        for (i = 0; i < PAL_N; i++) {
            s_pal[i] = 0;
            snprintf(cp, sizeof(cp), "cp_%d", i + 1);
            cg_widget_set_value(cg_find(app, cp), 0);
        }
        note(app, "调色板已清空");
    } else if (strcmp(id, "btn_copy") == 0) {          /* 复制 HEX：拼成列表进剪贴板 */
        if (!cg_app_has_window(app)) {
            note(app, "自检模式：跳过写剪贴板");
            return;
        }
        for (i = 0, n = 0, off = 0; i < PAL_N; i++) {
            if (!s_pal[i]) continue;
            n++;
            off += snprintf(hex + off, sizeof(hex) - (size_t)off, "#%06X\r\n", s_pal[i]);
        }
        if (!n) {
            note(app, "复制：调色板是空的");
            return;
        }
        if (cg_clip_set_text(hex) == 0) note(app, "已复制 %d 个 HEX 到剪贴板", n);
        else                            note(app, "写剪贴板失败");
    } else {                                           /* 导出：CSS 变量 + 纯 HEX 两份文件 */
        char *dir, *pa, *pb;

        if (!cg_app_has_window(app)) {
            note(app, "自检模式：跳过写盘");
            return;
        }
        dir = cg_app_data_dir();
        if (!dir) {
            note(app, "拿不到用户数据目录");
            return;
        }
        off = snprintf(css, sizeof(css), ":root {\r\n");
        for (i = 0, n = 0; i < PAL_N; i++) {
            if (!s_pal[i]) continue;
            n++;
            off += snprintf(css + off, sizeof(css) - (size_t)off,
                            "  --pal-%d: #%06X;\r\n", i + 1, s_pal[i]);
        }
        snprintf(css + off, sizeof(css) - (size_t)off, "}\r\n");

        for (i = 0, off = 0; i < PAL_N; i++) {
            if (!s_pal[i]) continue;
            off += snprintf(hex + off, sizeof(hex) - (size_t)off, "#%06X\r\n", s_pal[i]);
        }

        pa = cg_path_join(dir, "palette.css");
        pb = cg_path_join(dir, "palette.txt");
        if (pa && cg_file_write(pa, css) == 0) note(app, "CSS → %s（%d 字节）", pa, (int)strlen(css));
        else                                   note(app, "CSS 写盘失败");
        if (pb && cg_file_write(pb, hex) == 0) note(app, "HEX → %s（%d 字节）", pb, (int)strlen(hex));
        else                                   note(app, "HEX 写盘失败");
        note(app, "已导出 %d 格", n);
        cg_free(pa);
        cg_free(pb);
        cg_free(dir);
    }
}

/**
 * @brief 界面就绪：给 8 个色格填上种子色，并对当前色做一次换算
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 * @note 就绪钩子不读不写：只往控件里填值、算一次换算，不弹窗、不写盘、不碰剪贴板
 */
void pl_init(cg_app_t *app, void *user)
{
    static const unsigned k_seed[PAL_N] = {
        0x6750A4, 0x1B6E33, 0xC03030, 0x0B57D0,
        0xF9AB00, 0x00A3A3, 0x8E24AA, 0x455A64
    };
    char cp[8];
    int  i;

    (void)user;
    for (i = 0; i < PAL_N; i++) {
        s_pal[i] = k_seed[i];
        snprintf(cp, sizeof(cp), "cp_%d", i + 1);
        cg_widget_set_value(cg_find(app, cp), (int)k_seed[i]);
    }
    show_color(app, s_cur);
}

/* ------------------------------------------------------------ 绑定表 */

const cg_binding_t pl_bindings[] = {
    CG_BIND("pl_on_color_change", on_color_change),   /**< 当前色与 8 个色格共用一个回调 */
    CG_BIND("pl_on_pick_click",   on_pick_click),
    CG_BIND("pl_on_pal_click",    on_pal_click),      /**< 四个按钮靠控件 id 分流 */
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
