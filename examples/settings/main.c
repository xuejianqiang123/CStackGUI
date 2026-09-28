/**
 * @file    main.c
 * @brief   设置持久化：INI / 注册表 / 用户数据目录文件 三个后端并排对比
 * @details 本示例专讲"设置该存哪儿"，并把三个后端摆在一起让人一眼看出差别。
 *
 *          【三个后端的能力边界（都是实测出来的）】
 *
 *          1. **INI 文件**（`cg_ini_read` / `cg_ini_write`，路径在用户数据目录）
 *             按「节 + 键」读写字符串；能读回默认值（键不存在时用默认值顶上）；
 *             人可以用记事本直接改；随用户数据目录走，卸载程序不带走它。
 *
 *          2. **注册表 HKCU**（`cg_reg_read` / `cg_reg_write`）
 *             只能按名读写**字符串值**，子键相对 HKCU；**没有**任何"枚举子键 / 列举值 / 删值 / 删键"
 *             的接口 —— 所以本示例的"清空"只能把每个值写成空串（值空 = 相当于没设），
 *             也没法做"列出这个键下都有什么"。
 *             本示例固定写 `HKCU\Software\CStackGUI\examples\settings`，**绝不动用户的真实设置**。
 *
 *          3. **用户数据目录里的一个文本文件**（`cg_file_write` / `cg_file_read`）
 *             一次写整份、一次读整份；想加一项就得改格式并整份重写；好处是格式完全自己定。
 *
 *          【并发与去重都没有】：三个后端都只有"按名读 / 按名写"，没有锁、没有事务。
 *          两个进程同时写同一个后端会互相覆盖 —— 真实项目里要么只由主进程写，要么自己加临时文件。
 *
 *          自检（--selftest）不开窗：**不读也不写任何后端**（就绪钩子只摆开场白），
 *          "保存 / 读取 / 清空"这些有副作用的回调一律先问 `cg_app_has_window()`。
 *          内存：不用 malloc，读回来的堆串用完 `cg_free`。
 *
 *          编译：在项目根跑 build-app.bat settings
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 三个后端与设置项 */

#define BACKEND_INI  0   /**< INI 文件 */
#define BACKEND_REG  1   /**< 注册表 HKCU */
#define BACKEND_DAT  2   /**< 用户数据目录里的文本文件 */
#define BACKEND_COUNT 3

/** 本示例固定用的注册表子键（相对 HKCU）：不碰用户真实设置 */
static const char *k_reg_subkey = "Software\\CStackGUI\\examples\\settings";
/** INI 里的节名 */
static const char *k_ini_section = "settings";

/** 一个设置项在三处的名字 */
typedef struct {
    const char *reg_name;  /**< 注册表值名 */
    const char *ini_key;   /**< INI 键名 */
    const char *dat_key;   /**< 文本文件里的键名 */
    const char *def;       /**< 缺字段时的默认值 */
} item_def_t;

/** 设置项表（界面上的顺序与 collect_values 的 getter 顺序必须一致） */
static const item_def_t k_items[] = {
    { "LastDir",   "last_dir",   "last_dir",   ""            },   /**< 上次目录 */
    { "FontSize",  "font_size",  "font_size",  "14"          },   /**< 字号 */
    { "DarkTheme", "dark_theme", "dark_theme", "0"           },   /**< 深色主题 */
    { "Recent1",   "recent1",    "recent1",    ""            },   /**< 最近文件 1 */
    { "Recent2",   "recent2",    "recent2",    ""            },   /**< 最近文件 2 */
    { "Recent3",   "recent3",    "recent3",    ""            }    /**< 最近文件 3 */
};
/** 设置项个数 */
#define K_ITEM_COUNT ((int)(sizeof k_items / sizeof k_items[0]))

/** 额外的"更新时间"项（三个后端各存一份，用来看"哪个后端是新的"） */
static const item_def_t k_saved = { "SavedAt", "saved_at", "saved_at", "" };

/** 字号取值范围（与 .cgui 里那个带调节器的输入框一致） */
#define FONT_MIN 8
#define FONT_MAX 28

static int  s_backend = BACKEND_INI;  /**< 当前后端 BACKEND_* */
static char s_ini[1024];              /**< INI 文件全路径（就绪钩子里定下来） */
static char s_dat[1024];              /**< 数据文件全路径（就绪钩子里定下来） */

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 往终端写一行日志
 * @param app 应用；可为 NULL
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (app) {
        cg_widget_t *term = cg_find(app, "term_log");

        if (term) cg_term_write(term, CG_TERM_SYS, buf, -1);
    }
    printf("[settings] %s\n", buf);
}

/**
 * @brief 后端编号 → 显示名
 * @param b 后端编号 BACKEND_*
 * @return const char * 静态字符串，别 free
 */
static const char *backend_name(int b)
{
    switch (b) {
    case BACKEND_REG: return "注册表 HKCU";
    case BACKEND_DAT: return "数据目录文件";
    default:          return "INI 文件";
    }
}

/**
 * @brief 后端编号 → 它把值放在哪里的摘要（写进对比表）
 * @param b 后端编号 BACKEND_*
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void backend_location(int b, char *out, int cap)
{
    if (b == BACKEND_REG)      snprintf(out, (size_t)cap, "HKCU\\%s", k_reg_subkey);
    else if (b == BACKEND_DAT) snprintf(out, (size_t)cap, "%.240s", s_dat);
    else                       snprintf(out, (size_t)cap, "%.240s", s_ini);
}

/**
 * @brief 把一段文本当整数解析（只认整串都是数字）
 * @param s 以 '\0' 结尾的文本；NULL 安全
 * @return int 解析结果；不是数字返回 0
 */
static int to_int(const char *s)
{
    int v = 0, any = 0;

    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    return any ? v : 0;
}

/* ------------------------------------------------------------ 后端读写（三处各一个分支，收在一起） */

/**
 * @brief 从某个后端按名读一个值（只读，可安全用于刷新对比表）
 * @param backend 后端编号 BACKEND_*
 * @param it 项定义（提供三个名字）
 * @param out 输出缓冲；读到才写
 * @param cap out 的字节数
 * @return int 1 读到且非空、0 没读到或值为空
 * @note 注册表 / INI 返回的是堆串，读完立刻 cg_free；数据文件后端按行找 "键=值"
 */
static int backend_get(int backend, const item_def_t *it, char *out, int cap)
{
    char *v = NULL;
    int   has;

    if (out && cap > 0) out[0] = '\0';

    if (backend == BACKEND_DAT) {
        char *txt = cg_file_read(s_dat);              /* 整读；文件不存在返回 NULL */
        char  key[128];
        char *p;

        if (!txt) return 0;
        snprintf(key, sizeof(key), "%s=", it->dat_key);
        p = strstr(txt, key);
        if (p) {
            char *e;

            p += strlen(key);
            e = strchr(p, '\n');
            if (out && cap > 0) {
                int n = e ? (int)(e - p) : (int)strlen(p);

                if (n > cap - 1) n = cap - 1;
                memcpy(out, p, (size_t)n);
                out[n] = '\0';
                while (n > 0 && (out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = '\0';
            }
            cg_free(txt);
            return 1;
        }
        cg_free(txt);
        return 0;
    }

    if (backend == BACKEND_REG) v = cg_reg_read(k_reg_subkey, it->reg_name, NULL);
    else                        v = cg_ini_read(s_ini, k_ini_section, it->ini_key, NULL);

    has = (v && *v) ? 1 : 0;
    if (has && out && cap > 0) snprintf(out, (size_t)cap, "%s", v);
    cg_free(v);
    return has;
}

/**
 * @brief 往"按名写一个值"式后端（INI / 注册表）写一个键
 * @param backend 后端编号 BACKEND_*（数据文件后端不支持，走整文件写）
 * @param it 项定义（提供键名）
 * @param value 要写的字符串
 * @return int 0 成功、非 0 失败或后端不支持
 */
static int backend_put(int backend, const item_def_t *it, const char *value)
{
    if (backend == BACKEND_REG) return cg_reg_write(k_reg_subkey, it->reg_name, value);
    if (backend == BACKEND_INI) return cg_ini_write(s_ini, k_ini_section, it->ini_key, value);
    return -1;
}

/**
 * @brief 把界面上的这一组设置项收进文本缓冲（顺序同设置项表）
 * @param app 应用
 * @param out 输出：out[i] 对应设置项表的第 i 项，每格至少 256 字节
 * @return void 无返回值
 * @note 字号 / 深色是"数值型"设置项：字号从带调节器的输入框取值，深色从勾选框取状态
 */
static void collect_values(cg_app_t *app, char out[][256])
{
    static const char *k_ids[K_ITEM_COUNT] = {
        "edt_dir", "edt_font", "chk_dark", "edt_recent1", "edt_recent2", "edt_recent3"
    };
    cg_widget_t *fs = cg_find(app, "edt_font");
    cg_widget_t *dk = cg_find(app, "chk_dark");
    int          i;

    for (i = 0; i < K_ITEM_COUNT; i++) {
        cg_widget_t *w = cg_find(app, k_ids[i]);

        if (i == 1)      snprintf(out[i], 256, "%d", fs ? cg_widget_value(fs) : FONT_MIN);
        else if (i == 2) snprintf(out[i], 256, "%d", dk ? cg_widget_checked(dk) : 0);
        else             snprintf(out[i], 256, "%s", (w && cg_widget_text(w)) ? cg_widget_text(w) : "");
    }
}

/**
 * @brief 把界面设置写进当前后端（数据文件后端整文件覆盖写）
 * @param app 应用
 * @return int 0 全部成功、非 0 有失败
 */
static int save_backend(cg_app_t *app)
{
    char  vals[K_ITEM_COUNT][256];
    char  stamp[64], text[4096];
    char *now = cg_time_now(NULL);
    int   i, off = 0, rc = 0;

    collect_values(app, vals);
    snprintf(stamp, sizeof(stamp), "%s", now ? now : "?");
    cg_free(now);

    if (s_backend == BACKEND_DAT) {
        for (i = 0; i < K_ITEM_COUNT && off < (int)sizeof(text) - 300; i++)
            off += snprintf(text + off, sizeof(text) - (size_t)off, "%s=%s\n", k_items[i].dat_key, vals[i]);
        off += snprintf(text + off, sizeof(text) - (size_t)off, "%s=%s\n", k_saved.dat_key, stamp);
        rc = cg_file_write(s_dat, text);
    } else {
        for (i = 0; i < K_ITEM_COUNT; i++)
            if (backend_put(s_backend, &k_items[i], vals[i]) != 0) rc = -1;
        if (backend_put(s_backend, &k_saved, stamp) != 0) rc = -1;
    }
    return rc;
}

/**
 * @brief 从当前后端读回设置项；缺字段 / 脏数据优雅回退默认值并写日志
 * @param app 应用
 * @return void 无返回值
 */
static void load_backend(cg_app_t *app)
{
    static const char *k_recent[3] = { "edt_recent1", "edt_recent2", "edt_recent3" };
    char buf[256];
    int  fv, i;

    if (backend_get(s_backend, &k_items[0], buf, sizeof(buf)))
        cg_widget_set_text(cg_find(app, "edt_dir"), buf);
    else {
        cg_widget_set_text(cg_find(app, "edt_dir"), k_items[0].def);
        log_line(app, "上次目录：缺字段，用默认值「%s」", k_items[0].def);
    }

    fv = backend_get(s_backend, &k_items[1], buf, sizeof(buf)) ? to_int(buf) : 0;
    if (fv < FONT_MIN || fv > FONT_MAX) {
        fv = to_int(k_items[1].def);
        log_line(app, "字号：缺字段或脏数据，用默认值 %d", fv);
    }
    cg_widget_set_value(cg_find(app, "edt_font"), fv);

    if (backend_get(s_backend, &k_items[2], buf, sizeof(buf)))
        cg_widget_set_checked(cg_find(app, "chk_dark"), buf[0] == '1');
    else {
        cg_widget_set_checked(cg_find(app, "chk_dark"), 0);
        log_line(app, "深色主题：缺字段，用默认值 0（浅色）");
    }
    for (i = 0; i < 3; i++) {
        cg_widget_t *w = cg_find(app, k_recent[i]);

        cg_widget_set_text(w, backend_get(s_backend, &k_items[3 + i], buf, sizeof(buf)) ? buf : "");
    }
}

/**
 * @brief 重刷对比表：三个后端此刻各自存着什么（字号 / 深色 / 更新时间或"不存在"）
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_table(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_back");
    int          b;

    if (tbl) cg_table_clear(tbl);
    for (b = 0; b < BACKEND_COUNT; b++) {
        const char *cells[5];
        char        loc[300], t[64], fb[64], db[64];

        backend_location(b, loc, sizeof(loc));
        cells[0] = backend_name(b);
        cells[1] = loc;
        snprintf(fb, sizeof(fb), "%s", backend_get(b, &k_items[1], t, sizeof(t)) ? t : "不存在");
        snprintf(db, sizeof(db), "%s",
                 backend_get(b, &k_items[2], t, sizeof(t)) ? (t[0] == '1' ? "是" : "否") : "不存在");
        cells[2] = fb;
        cells[3] = db;
        cells[4] = backend_get(b, &k_saved, t, sizeof(t)) ? t : "不存在";
        if (tbl) cg_table_add_row(tbl, cells, 5);
    }
    cg_status_setf(cg_find(app, "sb_back"), 1, "对比表：%d 行 × 5 列", tbl ? cg_table_row_count(tbl) : 0);
}

/**
 * @brief 注册表往返自测：往本示例专用子键写一个探针值，再读回来比对
 * @param app 应用
 * @return int 1 一致、0 不一致或写失败
 * @note 注册表这套接口此前没有任何示例用过，所以先让它自证"写得进、读得回"。
 *       只动本示例自己的子键下的一个独立探针值（不碰那 6 个设置项，更不碰用户的真实设置）；
 *       探针串里带时间戳，免得读到上次留下的残留还以为通了
 */
static int reg_roundtrip(cg_app_t *app)
{
    char  want[64], got[64];
    char *now = cg_time_now("%H%M%S");
    int   rc, ok = 0;

    snprintf(want, sizeof(want), "probe-%s", now ? now : "x");
    cg_free(now);

    rc = cg_reg_write(k_reg_subkey, "RoundTrip", want);
    got[0] = '\0';
    if (rc == 0) {
        char *v = cg_reg_read(k_reg_subkey, "RoundTrip", NULL);   /* 堆串，用完还回去 */

        if (v) snprintf(got, sizeof(got), "%s", v);
        cg_free(v);
        ok = (got[0] && strcmp(got, want) == 0);
    }

    log_line(app, "注册表往返自测：写「%s」→ 读回「%s」→ %s",
             want, got[0] ? got : "(读不到)",
             (rc != 0) ? "写失败" : (ok ? "一致（写得进、读得回）" : "不一致"));
    return ok;
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 切换存储后端：记下选择、更新位置提示与状态栏，并重刷对比表（只读）
 * @param self 后端下拉框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_cmb_backend_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      loc[300];

    (void)ev;
    (void)user;
    s_backend = cg_widget_value(self);
    if (s_backend < 0 || s_backend >= BACKEND_COUNT) s_backend = BACKEND_INI;
    backend_location(s_backend, loc, sizeof(loc));
    cg_widget_set_text(cg_find(app, "lab_loc"), loc);
    cg_status_setf(cg_find(app, "sb_back"), 0, "当前后端：%s", backend_name(s_backend));
    log_line(app, "后端切到「%s」：%s", backend_name(s_backend), loc);
    refresh_table(app);
}

/**
 * @brief 「保存」：把界面设置写进当前后端（写盘 / 写注册表都是副作用，自检下跳过）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_save_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过保存（不写盘、不写注册表）");
        return;
    }
    if (save_backend(app) == 0) {
        log_line(app, "已保存到「%s」", backend_name(s_backend));
        cg_status_set(cg_find(app, "sb_back"), 2, "保存成功");
    } else {
        log_line(app, "保存失败：「%s」", backend_name(s_backend));
        cg_status_set(cg_find(app, "sb_back"), 2, "保存失败");
    }
    refresh_table(app);
}

/**
 * @brief 「读取」：从当前后端读回设置（只读；缺字段 / 脏数据回退默认值）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_load_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    load_backend(app);
    log_line(app, "已从「%s」读取（缺字段已回退默认值）", backend_name(s_backend));
    cg_status_set(cg_find(app, "sb_back"), 2, "读取完成");
    refresh_table(app);
}

/**
 * @brief 「清空」：删掉当前后端里的设置（副作用，自检下跳过；注册表无删接口，改写空串）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 注册表没有"删值 / 删键"接口，只能把每个值写空串 ——"值空着 = 相当于没设"；
 *       读取侧也是按"空 = 没有"处理的，所以两种情形在界面上表现一致
 */
static void on_btn_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       i, rc = 0;

    (void)ev;
    (void)user;
    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过清空（不删文件、不写注册表）");
        return;
    }
    if (s_backend == BACKEND_REG) {
        for (i = 0; i < K_ITEM_COUNT; i++)
            if (backend_put(BACKEND_REG, &k_items[i], "") != 0) rc = -1;
        if (backend_put(BACKEND_REG, &k_saved, "") != 0) rc = -1;
        log_line(app, "注册表没有删值 / 删键接口：已把 HKCU\\%s 的值写成空串（值空 = 相当于没设）",
                 k_reg_subkey);
    } else {
        const char *path = (s_backend == BACKEND_DAT) ? s_dat : s_ini;

        if (cg_file_exists(path)) rc = cg_file_delete(path);
        log_line(app, "已删除文件：%s", path);
    }
    cg_status_set(cg_find(app, "sb_back"), 2, rc == 0 ? "清空完成" : "清空失败");
    refresh_table(app);
}

/**
 * @brief 界面就绪：定下两个文件路径并摆好开场白（按自检红线，这里不读也不写任何后端）
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例未用
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    char *dir = cg_app_data_dir();
    char *ini = dir ? cg_path_join(dir, "settings.ini") : NULL;
    char *dat = dir ? cg_path_join(dir, "settings.dat") : NULL;
    char  loc[300];

    (void)user;
    snprintf(s_ini, sizeof(s_ini), "%s", ini ? ini : "settings.ini");
    snprintf(s_dat, sizeof(s_dat), "%s", dat ? dat : "settings.dat");
    cg_free(ini);
    cg_free(dat);
    cg_free(dir);

    s_backend = BACKEND_INI;
    cg_widget_set_value(cg_find(app, "cmb_backend"), BACKEND_INI);
    backend_location(s_backend, loc, sizeof(loc));
    cg_widget_set_text(cg_find(app, "lab_loc"), loc);
    cg_status_set(cg_find(app, "sb_back"), 0, "当前后端：INI 文件");
    cg_status_set(cg_find(app, "sb_back"), 1, "对比表：点「读取 / 保存」后刷新");
    cg_status_set(cg_find(app, "sb_back"), 2, "就绪");

    log_line(app, "设置持久化示例就绪：三个后端并排对比");
    log_line(app, "注册表固定写 HKCU\\%s，绝不碰用户真实设置", k_reg_subkey);
    log_line(app, "点「保存 / 读取 / 清空」只作用于状态栏第 1 段显示的那个后端");

    /* 真窗口下顺手做一次注册表往返自测（写探针 + 读回比对）：这套接口此前没人用过，
       先让它在日志里自证通了。自检不开窗，一步都不做 —— 不会平白写注册表 */
    if (cg_app_has_window(app)) {
        (void)reg_roundtrip(app);
        refresh_table(app);
    }
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_cmb_backend_select", on_cmb_backend_select),
    CG_BIND("on_btn_save_click",     on_btn_save_click),
    CG_BIND("on_btn_load_click",     on_btn_load_click),
    CG_BIND("on_btn_clear_click",    on_btn_clear_click),
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
        desc.cgui_path = (argc > 2) ? argv[2] : "settings.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "settings.cgui";
    return cg_app_run(&desc);
}
