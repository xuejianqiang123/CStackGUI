/**
 * @file    tb_opts.c
 * @brief   toolbox 成对开关（沙箱）实现：开关状态读写沙箱键 + 界面同步 + 查看/还原
 * @details 落点是本工具箱自己的沙箱键 HKCU\Software\CStackGUI\examples\toolbox，
 *          控制本工具箱自身行为（日志镜像 / 时间戳 / 自动切搜索页 / 记录最近使用 /
 *          沙箱标记），真改真还原，绝不碰系统键。
 */
#include "tb_opts.h"

#include <stdio.h>
#include <string.h>

#include "tb_log.h"

/** 注册表沙箱子键（相对 HKCU）——绝不写系统键 */
#define K_REG_SUBKEY "Software\\CStackGUI\\examples\\toolbox"

/** 开关定义：注册表值名 + 界面名 + 接管的行为说明 */
static const struct {
    const char *val;   /**< 沙箱键里的值名 */
    const char *name;  /**< 界面上显示的名字 */
} k_opts[TB_OPT_COUNT] = {
    { "MirrorLog",      "日志镜像到控制台" },
    { "LogTimestamp",   "日志行首加时间戳" },
    { "AutoSearchPage", "输入关键字自动切到搜索页" },
    { "RecentOn",       "记录最近使用" },
    { "RmenuSandbox",   "右键增强沙箱标记（演示）" }
};

/** 开关当前状态（1=装上 0=撤掉），就绪钩子里从沙箱键读回 */
static int s_opt[TB_OPT_COUNT];

/**
 * @brief 读一个开关的当前状态
 * @param idx 开关下标（0 起，TB_OPT_COUNT 内）；越界按 0（关）返回
 * @return int 1=装上 0=撤掉
 */
int tb_opt_get(int idx)
{
    return (idx >= 0 && idx < TB_OPT_COUNT) ? s_opt[idx] : 0;
}

/**
 * @brief 就绪钩子用：从沙箱键读回全部开关状态
 * @param read_reg 1=真读注册表；0=自检，保持全关
 * @return void 无返回值
 */
void tb_opts_load(int read_reg)
{
    int i;

    for (i = 0; i < TB_OPT_COUNT; i++) {
        if (!read_reg) { s_opt[i] = 0; continue; }
        {
            char *v = cg_reg_read(K_REG_SUBKEY, k_opts[i].val, NULL);
            s_opt[i] = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
            cg_free(v);
        }
    }
}

/**
 * @brief 同步全部开关按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 * @note 装上/撤掉互斥禁用；说明 label 显示「已装上 / 未装」
 */
void tb_opts_sync(cg_app_t *app)
{
    static const char *on_id[TB_OPT_COUNT]  = { "opt0_on",  "opt1_on",  "opt2_on",  "opt3_on",  "opt4_on"  };
    static const char *off_id[TB_OPT_COUNT] = { "opt0_off", "opt1_off", "opt2_off", "opt3_off", "opt4_off" };
    static const char *lab_id[TB_OPT_COUNT] = { "lab_opt0", "lab_opt1", "lab_opt2", "lab_opt3", "lab_opt4" };
    int i;

    for (i = 0; i < TB_OPT_COUNT; i++) {
        char buf[128];
        cg_widget_t *on  = cg_find(app, on_id[i]);
        cg_widget_t *off = cg_find(app, off_id[i]);

        if (on)  cg_widget_set_enabled(on,  !s_opt[i]);
        if (off) cg_widget_set_enabled(off,  s_opt[i]);
        snprintf(buf, sizeof(buf), "%s —— %s", k_opts[i].name,
                 s_opt[i] ? "已装上" : "未装");
        cg_widget_set_text(cg_find(app, lab_id[i]), buf);
    }
}

/**
 * @brief 成对开关统一回调：按 id 尾缀 _on/_off 判方向，写沙箱键并同步界面
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_opt_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx, on;

    (void)ev;
    (void)user;

    if (!id || strlen(id) < 7) return;   /* opt0_on 是 7 字符 */
    idx = id[3] - '0';
    on  = (strcmp(id + 5, "on") == 0);   /* id 形如 opt3_on：id+4 是 "_on"，方向在 id+5 */
    if (idx < 0 || idx >= TB_OPT_COUNT) return;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过写沙箱键 %s=%d", k_opts[idx].val, on);
        s_opt[idx] = on;
        tb_opts_sync(app);
        return;
    }

    if (cg_reg_write(K_REG_SUBKEY, k_opts[idx].val, on ? "1" : "") == 0) {
        s_opt[idx] = on;
        tb_opts_sync(app);
        tb_status_msg(app, "%s：%s", k_opts[idx].name, on ? "已装上" : "已撤掉");
        tb_log_line(app, "开关 %s %s（写沙箱键）", k_opts[idx].val, on ? "=1" : "=空");
    } else {
        tb_status_msg(app, "写沙箱键失败：%s", k_opts[idx].val);
    }
}

/**
 * @brief 「查看曾改动」：把沙箱键里 5 个开关的当前值弹出来看
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹框是副作用：自检下只写日志不弹
 */
static void on_btn_reg_view_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     buf[512];
    int      i, off = 0;

    (void)ev;
    (void)user;

    off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                    "沙箱键 HKCU\\%s\n\n", K_REG_SUBKEY);
    for (i = 0; i < TB_OPT_COUNT && off < (int)sizeof(buf) - 64; i++) {
        char *v = cg_app_has_window(app) ? cg_reg_read(K_REG_SUBKEY, k_opts[i].val, NULL) : NULL;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%-16s = %s\n",
                        k_opts[i].val, (v && *v) ? v : "(空)");
        cg_free(v);
    }

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过弹「查看曾改动」");
        return;
    }
    cg_message_box(self, buf, "曾改动（沙箱）", 0);
}

/**
 * @brief 「还原全部开关」：5 个值全写空串（本工程没有删值接口，值空 = 未设）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_reg_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int      i, ok = 1;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过写沙箱键（还原全部）");
        return;
    }
    for (i = 0; i < TB_OPT_COUNT; i++)
        if (cg_reg_write(K_REG_SUBKEY, k_opts[i].val, "") != 0) ok = 0;

    memset(s_opt, 0, sizeof(s_opt));
    tb_opts_sync(app);
    tb_status_msg(app, ok ? "已还原全部开关" : "部分值还原失败");
    tb_log_line(app, ok ? "沙箱键全部还原（写空串）" : "沙箱键还原失败");
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_opts_bindings[] = {
    CG_BIND("on_opt_click",           on_opt_click),
    CG_BIND("on_btn_reg_view_click",  on_btn_reg_view_click),
    CG_BIND("on_btn_reg_reset_click", on_btn_reg_reset_click),
    CG_BIND_END
};
