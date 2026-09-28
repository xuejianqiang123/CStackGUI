/**
 * @file    main.c
 * @brief   采集记录器：模拟采集、实时曲线、CSV 落盘、回放、上限保护
 * @details 不接任何硬件：数据来自定周期定时器产生的"整数正弦 + 噪声"序列。
 *          一条数据流有三个去向：① 实时喂图表的两条曲线（设备A / 设备B）；
 *          ② 在内存缓冲里累计（每点 = 相对时间戳 + 两个值）；③ 点「保存 CSV」时一次落盘。
 *
 *          为什么"内存累计 + 点保存一次写盘"：定时器回调每 200ms 跑一次，
 *          若每拍都追加一次文件，几小时下来就是几万次系统调用，自检 / 无窗口时更不该碰磁盘。
 *          所以采集期间只在内存累计（固定数组，不用 malloc），保存时才分块落盘。
 *
 *          上限保护：缓冲固定 10 万个点，满了就停采集 + 明确提示，绝不静默丢数据。
 *          自检（--selftest）不开窗：就绪钩子不启动采集；回调里先判"在不在采"；
 *          弹框 / 文件对话框前先问有没有窗口，没有就走用户数据目录或直接跳过。
 *
 * 界面在 datarec.cgui 里；本文件只有行为。编译：build-app.bat datarec
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** 内存样本上限（约 5.5 小时 @200ms），超了就停采集 */
#define DR_MAX_POINTS 100000
/** 采样周期（毫秒） */
#define DR_TICK_MS    200
/** 整数正弦表长度（一个周期） */
#define DR_SIN_LEN    32

/** 一个周期的整数正弦表（幅度 1000）：不引 `<math.h>` 也能造出"正弦" */
static const int k_sin[DR_SIN_LEN] = {
        0,  195,  383,  556,  707,  831,  924,  981,
     1000,  981,  924,  831,  707,  556,  383,  195,
        0, -195, -383, -556, -707, -831, -924, -981,
    -1000, -981, -924, -831, -707, -556, -383, -195
};

/** 采集器的全部状态：谁在什么时机读写都收在这一处 */
typedef struct {
    int           active;            /**< 1 = 正在采集（回调据此决定干不干活） */
    int           saturated;         /**< 1 = 已到上限被自动停下（提示用） */
    long          samples;           /**< 已收进缓冲的样本数 */
    unsigned long t0_ms;             /**< 本轮起始毫秒时基（相对时间戳与时长用） */
    unsigned long last_ms;           /**< 最近一拍的毫秒时基 */
    cg_timer_t   *timer;             /**< 在跳的那颗定时器句柄；NULL = 没在跑 */
    char          csv_path[1024];    /**< 最近一次保存 / 回放的 CSV 路径 */
    unsigned long ts[DR_MAX_POINTS]; /**< 每点相对起始的毫秒 */
    int           va[DR_MAX_POINTS]; /**< 曲线 1：设备A */
    int           vb[DR_MAX_POINTS]; /**< 曲线 2：设备B */
} dr_state_t;

static dr_state_t s_dr;
static unsigned   s_rand = 0x12345u;   /**< 伪随机种子：跑几次都一样，好复现 */
static int        s_beat = 0;          /**< 总拍数（决定正弦相位） */

/**
 * @brief 往日志列表框加一行，并镜像到运行时日志文件
 * @param app 应用
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void dr_log(cg_app_t *app, const char *fmt, ...)
{
    char        buf[256];
    va_list     ap;
    cg_widget_t *lst = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (lst) {
        cg_widget_list_addf(lst, "%s", buf);
        cg_widget_list_scroll_bottom(lst);
    }
    cg_log(CG_LOG_INFO, "[datarec] %s", buf);
}

/**
 * @brief 把「状态 / 已采集点数与时长 / CSV 文件名」刷到状态栏三段
 * @param app 应用
 * @return void 无返回值
 */
static void dr_status(cg_app_t *app)
{
    cg_widget_t  *sb  = cg_find(app, "sb_main");
    unsigned long dur = (s_dr.samples > 0) ? (s_dr.last_ms - s_dr.t0_ms) : 0;
    const char   *nm;

    if (!sb) return;

    if (s_dr.active)         cg_status_setf(sb, 0, "采集中（%d ms/拍）", DR_TICK_MS);
    else if (s_dr.saturated) cg_status_set(sb, 0, "已停：达到内存上限");
    else                     cg_status_set(sb, 0, "待机");

    cg_status_setf(sb, 1, "已采集 %ld 点 · 时长 %.1f s", s_dr.samples, (double)dur / 1000.0);

    nm = s_dr.csv_path[0] ? cg_path_name(s_dr.csv_path) : "（未保存）";
    cg_status_setf(sb, 2, "CSV: %s", nm);
}

/**
 * @brief 造一拍模拟数据：正弦 + 伪随机噪声，两条曲线
 * @param beat 总拍数（决定正弦相位）
 * @param va 出参：设备A 的值（0..100）
 * @param vb 出参：设备B 的值（0..100）
 * @return void 无返回值
 */
static void dr_make_sample(int beat, int *va, int *vb)
{
    int a, b;

    s_rand = s_rand * 1103515245u + 12345u;      /* 简单线性同余，不引 rand() */
    a = 50 + (40 * k_sin[beat % DR_SIN_LEN]) / 1000 + (int)((s_rand >> 16) % 11u) - 5;

    s_rand = s_rand * 1103515245u + 12345u;
    b = 50 + (40 * k_sin[(beat * 3 + 8) % DR_SIN_LEN]) / 1000 + (int)((s_rand >> 16) % 11u) - 5;

    if (a < 0) a = 0;
    if (a > 100) a = 100;
    if (b < 0) b = 0;
    if (b > 100) b = 100;
    *va = a;
    *vb = b;
}

/**
 * @brief 采集定时器回调：造一拍数据，喂曲线、进缓冲，再把自己重挂一颗
 * @param user 业务数据（本程序传应用指针）
 * @return void 无返回值
 * @note 定时器是**一次性**的：到点触发后自己被回收，定周期靠回调末尾再挂一颗；
 *       停采集时不再重挂即可
 */
static void dr_tick(void *user)
{
    cg_app_t    *app   = (cg_app_t *)user;
    cg_widget_t *chart = cg_find(app, "cht_wave");
    int          a = 0, b = 0;
    int          vals[2];

    s_dr.timer = NULL;                       /* 这一颗已经用掉了 */
    if (!s_dr.active) return;

    /* 上限保护：到顶就停采集 + 明确提示，绝不静默丢数据 */
    if (s_dr.samples >= DR_MAX_POINTS) {
        s_dr.active    = 0;
        s_dr.saturated = 1;
        dr_log(app, "已达内存上限 %d 点，采集自动停止（请先保存 CSV）", DR_MAX_POINTS);
        dr_status(app);
        if (cg_app_has_window(app)) {
            cg_message_box(cg_find(app, "btn_start"),
                           "内存缓冲已满（10 万点），采集已自动停止。\n请先「保存 CSV」，再重新开始。",
                           "采集上限", 1);
        }
        return;
    }

    dr_make_sample(++s_beat, &a, &b);

    /* 去向一：曲线（一次给两条系列各推一个点） */
    vals[0] = a;
    vals[1] = b;
    cg_chart_push_all(chart, vals, 2);

    /* 去向二：内存缓冲（固定数组，不用 malloc） */
    s_dr.last_ms          = cg_now_ms();
    s_dr.ts[s_dr.samples] = s_dr.last_ms - s_dr.t0_ms;
    s_dr.va[s_dr.samples] = a;
    s_dr.vb[s_dr.samples] = b;
    s_dr.samples++;

    if (s_dr.samples == 1)     dr_log(app, "开始采集：%d ms/拍，上限 %d 点", DR_TICK_MS, DR_MAX_POINTS);
    if (s_dr.samples % 5 == 0) dr_status(app);   /* 每 5 拍刷一次状态栏就够 */

    /* 重挂下一拍：这正是"一次性定时器当周期定时器用"的那一行 */
    s_dr.timer = cg_set_timer(app, DR_TICK_MS, dr_tick, app);
}

/**
 * @brief 开始采集：清空缓冲与曲线、重置计时，挂第一颗定时器
 * @param app 应用
 * @return void 无返回值
 */
static void dr_start(cg_app_t *app)
{
    if (s_dr.active) {
        dr_log(app, "已在采集中，忽略这次的「开始」");
        return;
    }
    s_dr.active    = 1;
    s_dr.saturated = 0;
    s_dr.samples   = 0;
    s_dr.t0_ms     = cg_now_ms();
    s_dr.last_ms   = s_dr.t0_ms;

    cg_chart_clear(cg_find(app, "cht_wave"));   /* 只清曲线，保留上次的 CSV 路径 */
    dr_log(app, "「开始采集」：清空缓冲与曲线，按 %d ms/拍 采集", DR_TICK_MS);
    dr_status(app);

    s_dr.timer = cg_set_timer(app, DR_TICK_MS, dr_tick, app);   /* 起第一颗 */
}

/**
 * @brief 停止采集：杀掉在跳的定时器，内存缓冲留着（保存 CSV 要用）
 * @param app 应用
 * @return void 无返回值
 */
static void dr_stop(cg_app_t *app)
{
    if (!s_dr.active) {
        dr_log(app, "当前没有在采集");
        return;
    }
    s_dr.active = 0;
    cg_kill_timer(s_dr.timer);   /* 句柄为 NULL 也安全 */
    s_dr.timer = NULL;
    dr_log(app, "已停止采集：本轮共 %ld 点", s_dr.samples);
    dr_status(app);
}

/**
 * @brief 选一个保存路径：有窗口时问用户，无窗口（自检）时用用户数据目录
 * @param self 调用的控件（对话框定位用）
 * @param out 路径输出缓冲
 * @param cap out 的字节数
 * @return int 1 拿到路径、0 取消或失败
 * @note 弹文件对话框前先问有没有窗口：自检不开窗，弹框会把 CI 卡住
 */
static int dr_pick_path(cg_widget_t *self, char *out, int cap)
{
    cg_app_t *app   = cg_app_of(self);
    char     *stamp = cg_time_now("%Y%m%d_%H%M%S");
    char     *name  = cg_str_format("datarec_%s.csv", stamp ? stamp : "now");
    char     *dir   = cg_app_data_dir();                 /* 用户数据目录，自动建 */
    char     *full  = (dir && name) ? cg_path_join(dir, name) : NULL;
    int       ok    = 0;

    cg_free(stamp);
    cg_free(name);
    cg_free(dir);
    if (!full) return 0;

    out[0] = '\0';
    if (cg_app_has_window(app)) {
        ok = cg_save_file_dialog(self, "保存采集数据",
                                 "CSV 文件(*.csv)|*.csv|所有文件(*.*)|*.*", "csv", out, cap);
    } else {
        snprintf(out, (size_t)cap, "%s", full);
        ok = 1;
    }
    cg_free(full);
    return ok;
}

/**
 * @brief 把内存缓冲写成 CSV：先写表头，再按 4KB 分块追加
 * @param path 目标 CSV 路径
 * @return long 写出的数据行数；-1 失败
 * @note 用固定大小的分块缓冲 + 追加接口，避免在内存里拼出整份 CSV 文本
 */
static long dr_write_csv(const char *path)
{
    char chunk[4096];
    char row[64];
    int  used = 0;
    long i;

    if (cg_file_write(path, "ts_ms,dev_a,dev_b\n") != 0) return -1;   /* 表头：覆盖写 */

    for (i = 0; i < s_dr.samples; i++) {
        int n = snprintf(row, sizeof(row), "%lu,%d,%d\n",
                         s_dr.ts[i], s_dr.va[i], s_dr.vb[i]);
        if (n <= 0 || n >= (int)sizeof(row)) return -1;
        if (used + n >= (int)sizeof(chunk)) {          /* 块快满了先落一块 */
            chunk[used] = '\0';
            if (cg_file_append(path, chunk) != 0) return -1;
            used = 0;
        }
        memcpy(chunk + used, row, (size_t)n);
        used += n;
    }
    if (used > 0) {
        chunk[used] = '\0';
        if (cg_file_append(path, chunk) != 0) return -1;
    }
    return s_dr.samples;
}

/**
 * @brief 「保存 CSV」：挑路径 → 分块落盘 → 路径写进输入框 / 状态栏 / 日志
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_save_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_path");
    char         path[1024];
    long         rows;

    (void)ev;
    (void)user;

    if (s_dr.samples <= 0) {
        dr_log(app, "缓冲里还没有数据，先「开始采集」再来保存");
        return;
    }
    if (!dr_pick_path(self, path, sizeof(path))) {
        dr_log(app, "保存被取消");
        return;
    }

    rows = dr_write_csv(path);
    if (rows < 0) {
        dr_log(app, "写盘失败：%s", path);
        return;
    }

    snprintf(s_dr.csv_path, sizeof(s_dr.csv_path), "%s", path);
    if (edt) cg_widget_set_text(edt, s_dr.csv_path);
    dr_log(app, "已保存 %ld 点到 %s", rows, cg_path_name(path));
    dr_status(app);
}

/**
 * @brief 「回放」：读回 CSV → 逐行解析 → 清空图表后重放，并写日志
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 用整读接口（带长度出口）；按行切分后逐行解析，匹配不上三个数字的行（如表头）自动跳过
 */
static void on_btn_replay_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t      *app   = cg_app_of(self);
    cg_widget_t   *chart = cg_find(app, "cht_wave");
    unsigned char *data;
    char           line[128];
    int            len = 0, i, linelen = 0;
    long           played = 0;

    (void)ev;
    (void)user;

    if (s_dr.csv_path[0] == '\0') {
        dr_log(app, "还没有可回放的 CSV（先「保存 CSV」一次）");
        return;
    }
    if (!cg_file_exists(s_dr.csv_path)) {
        dr_log(app, "文件不见了：%s", s_dr.csv_path);
        return;
    }
    data = cg_file_read_bytes(s_dr.csv_path, &len);
    if (!data) {
        dr_log(app, "读不出来：%s", s_dr.csv_path);
        return;
    }

    cg_chart_clear(chart);   /* 清空后重放，免得与实时曲线混在一起 */

    for (i = 0; i <= len; i++) {
        unsigned char c = (i < len) ? data[i] : (unsigned char)'\n';  /* 末尾补换行收尾 */

        if (c == '\n' || c == '\r') {
            if (linelen > 0) {
                long ts = 0;
                int  a = 0, b = 0;
                int  vals[2];

                line[linelen] = '\0';
                if (sscanf(line, "%ld,%d,%d", &ts, &a, &b) == 3) {
                    (void)ts;
                    vals[0] = a;
                    vals[1] = b;
                    cg_chart_push_all(chart, vals, 2);
                    played++;
                }
                linelen = 0;
            }
        } else if (linelen < (int)sizeof(line) - 1) {
            line[linelen++] = (char)c;
        }
    }
    cg_free(data);

    dr_log(app, "回放完成：%s 解析出 %ld 点", cg_path_name(s_dr.csv_path), played);
    dr_status(app);
}

/**
 * @brief 「开始」按钮
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_start_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    dr_start(cg_app_of(self));
}

/**
 * @brief 「停止」按钮
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_stop_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    dr_stop(cg_app_of(self));
}

/**
 * @brief 窗口关闭前：把在跳的定时器停掉（进程反正要退，这里图个干净）
 * @param self 窗口伪控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_win_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    s_dr.active = 0;
    cg_kill_timer(s_dr.timer);
    s_dr.timer = NULL;
}

/**
 * @brief 界面就绪：只做初始化与提示，**不启动采集**
 * @param app 应用
 * @param user 就绪钩子的业务数据，本程序没用
 * @return void 无返回值
 * @note 自检不开窗却会调这个钩子；若在这里自动开始采集，定时器就会在自检里跑起来
 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *edt = cg_find(app, "edt_path");

    (void)user;

    memset(&s_dr, 0, sizeof(s_dr));     /* 重开界面也干净：缓冲清零、没有在跑的定时器 */
    s_dr.timer = NULL;

    if (edt) cg_widget_set_text(edt, "（尚未保存）");
    dr_log(app, "采集记录器就绪：点「开始」按 %d ms/拍 采集模拟数据", DR_TICK_MS);
    dr_log(app, "「保存 CSV」落盘到用户数据目录；「回放」从 CSV 读回来重画曲线");
    dr_status(app);
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_win_close",        on_win_close),
    CG_BIND("on_btn_start_click",  on_btn_start_click),
    CG_BIND("on_btn_stop_click",   on_btn_stop_click),
    CG_BIND("on_btn_save_click",   on_btn_save_click),
    CG_BIND("on_btn_replay_click", on_btn_replay_click),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "datarec.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "datarec.cgui";
    return cg_app_run(&desc);
}
