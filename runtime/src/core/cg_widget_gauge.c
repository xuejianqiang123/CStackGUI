/**
 * @file    cg_widget_gauge.c
 * @brief   图表与指示类控件 API：折线/柱状图、状态灯（led）、仪表盘（meter）
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 折线 / 柱状图（chart）
 * 实时可视化的那块：业务按采样节奏 push 点，窗口、范围、网格与绘制都由控件管。
 *   - 系列名存在 items[] 里（每个条目一个系列），颜色按调色板自动分配，不用额外字段；
 *   - 点数据在拍平的 chart_pts（chart_nseries × chart_cap）里，按需分配；
 *   - **窗口满了整体左移一格**，不做环形缓冲：cap 只有几十到几百，memmove 一下比维护
 *     读写指针 + 一圈边界判断便宜得多，也不会出现"绕回时算错下标"这种隐蔽 bug。
 * 绘制策略见 chart_paint / chart_draw_series（点数超过像素宽度时按列画 min/max）。
 */

/** 每系列保留多少点（.cgui 的 value）；小于 2 就按缺省 120 */
#define CG_CHART_DEF_CAP 120
/** 系列数上限：再多就该换控件了，顺便防 .cgui 写错把内存吃光 */
#define CG_CHART_MAX_SERIES 8
/** 窗口点数上限（一条曲线最多几万个点也够用了） */
#define CG_CHART_MAX_CAP 4096

/**
 * @brief 每系列窗口点数（夹到合理区间）
 * @param w 图表控件
 * @return int 夹取后的窗口点数
 */
static int chart_cap(const cg_widget_t *w)
{
    int c = w->value;

    if (c < 2) c = CG_CHART_DEF_CAP;
    if (c > CG_CHART_MAX_CAP) c = CG_CHART_MAX_CAP;
    return c;
}

/**
 * @brief 系列数：items 为空时按 1 个系列算（单条曲线的 .cgui 不用写 items）
 * @param w 图表控件
 * @return int 系列数
 */
static int chart_nseries(const cg_widget_t *w)
{
    int n = w->nitems > 0 ? w->nitems : 1;

    return n > CG_CHART_MAX_SERIES ? CG_CHART_MAX_SERIES : n;
}

/**
 * @brief 点缓冲按需建 / 重建（items 或 value 变了就重建），返回系列数
 * @param w 图表控件；缓冲建好后挂在控件上，归控件所有
 * @return int 实际建成的系列数；分配失败返回 0
 */
int cg_chart_ensure(cg_widget_t *w)
{
    int n = chart_nseries(w);
    int cap = chart_cap(w);

    if (w->chart_pts && w->chart_fill && w->chart_nseries == n && w->chart_cap == cap) return n;

    free(w->chart_pts);
    free(w->chart_fill);
    w->chart_pts = (int *)calloc((size_t)n * (size_t)cap, sizeof(int));
    w->chart_fill = (int *)calloc((size_t)n, sizeof(int));
    w->chart_nseries = (w->chart_pts && w->chart_fill) ? n : 0;
    w->chart_cap = w->chart_nseries ? cap : 0;
    return w->chart_nseries;
}

/**
 * @brief 往某个系列塞一个点（不刷新，给批量接口用）
 * @param w 图表控件，要求缓冲已建好
 * @param series 系列下标，越界按 0 处理
 * @param value 要写入的点值
 * @return void 无返回值
 */
static void chart_push_raw(cg_widget_t *w, int series, int value)
{
    int *buf;
    int  cap = w->chart_cap;

    if (!w->chart_pts || !w->chart_fill || cap <= 0) return;
    if (series < 0 || series >= w->chart_nseries) series = 0;
    buf = w->chart_pts + (size_t)series * (size_t)cap;
    if (w->chart_fill[series] < cap) {
        buf[w->chart_fill[series]++] = value;
    } else {
        memmove(buf, buf + 1, (size_t)(cap - 1) * sizeof(int));
        buf[cap - 1] = value;
    }
}

/**
 * @brief 给某个系列追加一个点（满了顶掉最老的）
 * @param w 图表控件；类型不符直接忽略
 * @param series 系列下标，越界按 0 处理
 * @param value 要写入的点值
 * @return void 无返回值
 */
void cg_chart_push(cg_widget_t *w, int series, int value)
{
    if (!w || !want_type(w, "chart", "cg_chart_push")) return;
    if (cg_chart_ensure(w) <= 0) return;
    chart_push_raw(w, series, value);
    cg_widget_sync(w);
}

/**
 * @brief 一次给前 nvalues 个系列各追加一个点（多通道采样最常用的一次调用）
 * @param w 图表控件；类型不符直接忽略
 * @param values 逐系列的点值数组，取前 nvalues 个
 * @param nvalues values 的元素个数，超过系列数的部分忽略
 * @return void 无返回值
 */
void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues)
{
    int i, n;

    if (!w || !values || !want_type(w, "chart", "cg_chart_push_all")) return;
    n = cg_chart_ensure(w);
    if (n <= 0) return;
    for (i = 0; i < nvalues && i < n; i++) chart_push_raw(w, i, values[i]);
    cg_widget_sync(w);
}

/**
 * @brief 清空所有点（保留系列与范围）
 * @param w 图表控件；类型不符直接忽略
 * @return void 无返回值
 */
void cg_chart_clear(cg_widget_t *w)
{
    if (!w || !want_type(w, "chart", "cg_chart_clear")) return;
    if (cg_chart_ensure(w) <= 0) return;
    memset(w->chart_fill, 0, (size_t)w->chart_nseries * sizeof(int));
    cg_widget_sync(w);
}

/**
 * @brief 运行时加一个系列（返回下标；超过上限或内存不足返回 -1）
 * @param w 图表控件；类型不符直接忽略
 * @param name 系列名，为空时自动取"系列"；内部复制，调用方不必释放
 * @return int 新系列下标；超过上限或内存不足返回 -1
 */
int cg_chart_add_series(cg_widget_t *w, const char *name)
{
    char **ni;

    if (!w || !want_type(w, "chart", "cg_chart_add_series")) return -1;
    if (w->nitems >= CG_CHART_MAX_SERIES) return -1;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) return -1;
    w->items = ni;
    w->items[w->nitems++] = cg_str_dup(name && *name ? name : "系列");
    cg_chart_ensure(w);              /* 系列数变了：缓冲按新的宽度重建 */
    cg_widget_sync(w);
    return w->nitems - 1;
}

/**
 * @brief 系列数（items 为空时是 1）
 * @param w 图表控件，可为 NULL
 * @return int 系列数；w 为空返回 0
 */
int cg_chart_series_count(const cg_widget_t *w)
{
    return w ? chart_nseries(w) : 0;
}

/**
 * @brief 某系列已经填了多少点
 * @param w 图表控件，可为 NULL
 * @param series 系列下标，从 0 开始
 * @return int 已填点数；控件 / 缓冲不存在或下标越界返回 0
 */
int cg_chart_filled(const cg_widget_t *w, int series)
{
    if (!w || !w->chart_fill) return 0;
    if (series < 0 || series >= w->chart_nseries) return 0;
    return w->chart_fill[series];
}

/**
 * @brief 读回某个点（下标 0 = 最老的点）；越界返回 0
 * @param w 图表控件，可为 NULL
 * @param series 系列下标，从 0 开始
 * @param index 系列内点下标，0 表示最老的点
 * @return int 点值；控件 / 缓冲不存在或越界返回 0
 */
int cg_chart_point(const cg_widget_t *w, int series, int index)
{
    if (!w || !w->chart_pts || w->chart_cap <= 0) return 0;
    if (series < 0 || series >= w->chart_nseries) return 0;
    if (index < 0 || index >= w->chart_fill[series]) return 0;
    return w->chart_pts[(size_t)series * (size_t)w->chart_cap + (size_t)index];
}

/**
 * @brief 设 Y 轴范围（超出的值按边界裁剪后画）
 * @param w 图表控件；类型不符直接忽略
 * @param min Y 轴下界
 * @param max Y 轴上界，<= min 时纠正成 min+1
 * @return void 无返回值
 */
void cg_chart_set_range(cg_widget_t *w, int min, int max)
{
    if (!w || !want_type(w, "chart", "cg_chart_set_range")) return;
    if (max <= min) max = min + 1;
    w->vmin = min;
    w->vmax = max;
    if (w->node) {
        w->node->vmin = min;
        w->node->vmax = max;
    }
    cg_widget_sync(w);
}

/* ------------------------------------------------------------ 状态灯（led）
 * 最小的一块"看得见状态"的控件：通信连上没有、串口在不在收、设备就绪没有。
 * 状态就存在普通的 value 里（0 灭 / 1 亮 / 2 闪），形状在 format、闪烁周期在 period、
 * 亮灯颜色在 style.fg —— **全是既有字段，没为它加新存储**。
 * 闪烁相位的算法放在这里而不是画的时候，是为了能脱窗单测：闪错了不崩，只是"看不出来在闪"。
 */

/** 闪烁周期的缺省值：period 太小（<100ms）兜到这里 —— 再快也看不出在闪，只会看着像暗的 */
#define CG_LED_DEF_PERIOD 500

/**
 * @brief 状态夹到 0..2
 * @param w 状态灯控件
 * @return int 夹取后的状态：0 灭 / 1 亮 / 2 闪
 */
static int led_state(const cg_widget_t *w)
{
    if (w->value < 0) return 0;
    if (w->value > 2) return 2;
    return w->value;
}

/**
 * @brief 此刻灯亮着吗（状态 = 闪时按 period 前后半周期翻转）
 * @param w 状态灯控件，可为 NULL
 * @param now_ms 当前时刻，单位毫秒
 * @return int 亮返回 1、灭返回 0
 */
int cg_led_lit(const cg_widget_t *w, unsigned long now_ms)
{
    unsigned long p;
    int           s;

    if (!w) return 0;
    s = led_state(w);
    if (s != 2) return s == 1;                      /* 灭 = 不亮；亮 = 一直亮 */
    if (w->period <= 0) return 0;                    /* 已停止闪烁：不再用默认周期假闪 */
    p = (w->period >= 100) ? (unsigned long)w->period : (unsigned long)CG_LED_DEF_PERIOD;
    return ((now_ms % p) < (p / 2)) ? 1 : 0;        /* 前半周期亮、后半周期灭 */
}

/**
 * @brief 设状态（0 灭 / 1 亮 / 2 闪；越界夹取）
 * @param w 状态灯控件；类型不符直接忽略
 * @param state 目标状态，0 灭 / 1 亮 / 2 闪，越界夹取
 * @return void 无返回值
 */
void cg_led_set(cg_widget_t *w, int state)
{
    if (!w || !want_type(w, "led", "cg_led_set")) return;
    if (state < 0) state = 0;
    if (state > 2) state = 2;
    w->value = state;
    if (w->node) w->node->value = state;
    cg_widget_sync(w);
}

/**
 * @brief 读状态（0 / 1 / 2）
 * @param w 状态灯控件，可为 NULL
 * @return int 状态值 0 灭 / 1 亮 / 2 闪；w 为空返回 0
 */
int cg_led_state(const cg_widget_t *w)
{
    return w ? led_state(w) : 0;
}

/**
 * @brief 亮起的颜色（走既有的颜色通道：自绘控件都从 style.fg 取主色）
 * @param w 状态灯控件；类型不符直接忽略
 * @param rgb 亮灯颜色，0xRRGGBB
 * @return void 无返回值
 */
void cg_led_set_color(cg_widget_t *w, unsigned rgb)
{
    if (!w || !want_type(w, "led", "cg_led_set_color")) return;
    cg_widget_set_colors(w, rgb, CG_COLOR_NONE);
}

/* ------------------------------------------------------------ 仪表盘（meter）
 * 看单个读数"现在处在量程的哪个位置"。value/min/max 就是滑块那套（超量程夹取），
 * 样式在 format、单位在 text、主色在 style.fg —— 同样没加新存储。
 * 角度算法放 core 是为了能脱窗单测：表盘画歪了，肉眼很难判断是数据的问题还是控件的问题。
 */

#define CG_METER_ARC_FROM   135      /* arc 样式：135° 起（左下）…… */
#define CG_METER_ARC_SWEEP  270      /* ……顺时针扫 270°（经正上方，到右下 45°） */
#define CG_METER_HALF_FROM  180      /* half 样式：180° 起（正左）…… */
#define CG_METER_HALF_SWEEP 180      /* ……顺时针扫 180°（到正右） */

/**
 * @brief 值 -> 表盘角度（度，顺时针、以 +x 轴为 0°）；超量程按边界夹取
 * @param w 仪表盘控件，可为 NULL；format == "half" 时走半圆量程
 * @param value 要换算的值，超量程按边界夹取
 * @return int 表盘角度，单位度；w 为空或量程非法时返回起点角
 */
int cg_meter_angle_deg(const cg_widget_t *w, int value)
{
    int from, sweep, range;

    if (!w) return CG_METER_ARC_FROM;
    if (w->format && strcmp(w->format, "half") == 0) {
        from = CG_METER_HALF_FROM;
        sweep = CG_METER_HALF_SWEEP;
    } else {
        from = CG_METER_ARC_FROM;
        sweep = CG_METER_ARC_SWEEP;
    }
    range = w->vmax - w->vmin;
    if (range <= 0) return from;                    /* 量程非法：只给起点，不除零 */
    if (value < w->vmin) value = w->vmin;
    if (value > w->vmax) value = w->vmax;
    return from + (int)((long long)(value - w->vmin) * sweep / range);
}

/**
 * @brief 设当前值（夹到量程；既有 set_value 就带夹取与同步）
 * @param w 仪表盘控件；类型不符直接忽略
 * @param value 目标值，超出量程时夹取
 * @return void 无返回值
 */
void cg_meter_set(cg_widget_t *w, int value)
{
    if (!w || !want_type(w, "meter", "cg_meter_set")) return;
    cg_widget_set_value(w, value);
}

/**
 * @brief 设量程（max<=min 时纠正成 min+1，免得除零）
 * @param w 仪表盘控件；类型不符直接忽略
 * @param min 量程下界
 * @param max 量程上界，<= min 时纠正成 min+1
 * @return void 无返回值
 */
void cg_meter_set_range(cg_widget_t *w, int min, int max)
{
    if (!w || !want_type(w, "meter", "cg_meter_set_range")) return;
    if (max <= min) max = min + 1;
    w->vmin = min;
    w->vmax = max;
    if (w->node) {
        w->node->vmin = min;
        w->node->vmax = max;
    }
    cg_widget_sync(w);
}
