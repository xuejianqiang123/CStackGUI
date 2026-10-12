/**
 * @file    main.c
 * @brief   控件陈列室（三合一）：全部控件与事件的业务逻辑示例
 * @details 一个示例讲完所有东西，窗口顶部的大标签页分三页：
 *   页 0「基础闭环」：分组框 / 输入 / 勾选 / 滑块 / 进度条 / 列表框 / 按钮（单击+双击），
 *                     演示业务状态结构、cg_set_timer、控件间联动。
 *   页 1「控件陈列」：菜单栏 / 工具栏 / 菜单按钮 / 开关 / 微调 / 日期时间 / 树 / 表格 /
 *                     富文本 / 标签页 / 圆环 / 月历 / 链接 / 图标按钮 / 图片 / 状态栏，
 *                     演示 close 拦截、动态加节点加行。
 *   页 2「新特性」  ：时钟 / 悬停提示 / 通用对话框 / 画板 / 颜色选择 / 表格勾选多选 /
 *                     行数值 / 评分 / 分页 / 徽章 / 标签片 / 卡片 / 外形框 / 网页浏览 / 焦点事件。
 *
 * 界面在 gallery.cgui 里；本文件只有行为和「启动时该干什么」。
 * 编译：在项目根跑 build-app.bat gallery
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态
 * 三页共用一个业务结构：字段不冲突，各页回调各用各的。
 */
typedef struct {
    /* 页 0（基础闭环） */
    char  name[64];
    int   vip;
    int   level;
    int   click_count;
    /* 页 1（控件陈列） */
    int   notify;
    int   count;
    int   tree_added;   /* 动态加过几个树节点 */
    int   rows_added;   /* 动态加过几行表格 */
    int   dirty;        /* 富文本改过还没"保存" */
    int   close_tried;  /* 已经拦过一次关闭 */
} biz_t;

static biz_t s_biz;

/**
 * @brief 把一行状态文字写进页 0 的状态标签
 * @param app 所属应用
 * @param text 要显示的状态文字
 * @return void 无返回值
 */
static void set_status(cg_app_t *app, const char *text)
{
    cg_widget_t *lab = cg_find(app, "lab_hello_status");
    if (lab) cg_widget_set_text(lab, text);
}

/* 把一行日志同时写到控制台和页 0 的 lst_log */
/**
 * @brief 把一行日志同时打印到控制台并追加到日志列表框
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @param ... 与 fmt 对应的可变参数
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    cg_widget_t *log = cg_find(app, "lst_log");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[log] %s\n", buf);
    if (log) cg_widget_list_add(log, buf);
}

/* 把业务状态推回界面：滑块 -> 进度条 + 文字 */
/**
 * @brief 把业务等级推回界面，刷新等级文字与进度条
 * @param app 所属应用
 * @return void 无返回值
 */
static void refresh_level(cg_app_t *app)
{
    cg_widget_t *lab = cg_find(app, "lab_level");
    cg_widget_t *prg = cg_find(app, "prg_level");
    char buf[64];

    snprintf(buf, sizeof(buf), "等级 %d", s_biz.level);
    if (lab) cg_widget_set_text(lab, buf);
    if (prg) cg_widget_set_value(prg, s_biz.level);
}

/* ============================================================ 页 1 陈列室 */

/* 把一句话同时写到状态栏第二段和 stdout */
/**
 * @brief 把一句话同时写到状态栏第二段和 stdout
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @param ... 与 fmt 对应的可变参数
 * @return void 无返回值
 */
static void status(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;
    cg_widget_t *sb = cg_find(app, "sb_main");

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[gallery] %s\n", buf);
    if (sb) cg_status_set(sb, 1, buf);
}

/* 状态栏第 1 段显示业务概况（第 2 段留给 status() 写最近一次操作） */
/**
 * @brief 在状态栏第一段显示业务概况
 * @param app 所属应用
 * @param biz 业务状态结构
 * @return void 无返回值
 * @note 状态栏第 2 段留给 status() 写最近一次操作
 */
static void refresh_status_summary(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *sb = cg_find(app, "sb_main");
    if (!sb || !biz) return;
    cg_status_setf(sb, 0, "开关 %s · 数量 %d · 树 +%d · 表 +%d",
                   biz->notify ? "开" : "关", biz->count, biz->tree_added, biz->rows_added);
}

/* ============================================================ 页 2 新特性 */

/**
 * @brief 把格式化后的一句话写进页 2 的状态标签
 * @param app 所属应用
 * @param fmt printf 风格格式串
 * @param ... 与 fmt 对应的可变参数
 * @return void 无返回值
 */
static void lab_statusf(cg_app_t *app, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    cg_widget_t *lab;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lab = cg_find(app, "lab_lab_status");
    if (lab) cg_widget_set_text(lab, buf);
}

/* ============================================================ 页 0 回调 */

/* 定时器回调：cg_set_timer 到点后在消息循环里执行，此时跳出原事件栈，安全 */
/**
 * @brief 定时器到点后把等级回落到 40 并刷新界面
 * @param user 定时器传入的应用指针（不是挂在控件上的业务数据）
 * @return void 无返回值
 * @note 定时器回调：到点后在消息循环里执行，此时已跳出原事件栈，安全
 */
static void reset_level_later(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_widget_t *sld = cg_find(app, "sld_level");

    s_biz.level = 40;
    if (sld) cg_widget_set_value(sld, s_biz.level);
    refresh_level(app);
    log_line(app, "2 秒后自动回落到等级 40");
}

/**
 * @brief 单击时累加点击次数并把姓名、VIP、等级写到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    char buf[160];

    (void)ev;
    if (!biz) biz = &s_biz;

    biz->click_count++;
    snprintf(buf, sizeof(buf), "单击：%s，VIP=%s，等级 %d（第 %d 次单击）",
             biz->name[0] ? biz->name : "(未填姓名)",
             biz->vip ? "是" : "否", biz->level, biz->click_count);
    set_status(app, buf);
    log_line(app, "BTN 单击 #%d", biz->click_count);
}

/**
 * @brief 双击时把等级拉满到 100 并安排 2 秒后回落
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_ok_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char buf[160];

    (void)ev;
    /* 双击时顺手把等级拉满，演示「事件驱动改状态」 */
    s_biz.level = 100;
    {
        cg_widget_t *sld = cg_find(app, "sld_level");
        if (sld) cg_widget_set_value(sld, s_biz.level);   /* 会触发 on_level_change */
    }
    snprintf(buf, sizeof(buf), "双击：等级已拉满到 %d", s_biz.level);
    set_status(app, buf);
    log_line(app, "BTN 双击");

    /* 顺便演示 cg_set_timer：2 秒后自动回落到 40 */
    cg_set_timer(app, 2000, reset_level_later, app);
}

/**
 * @brief 重置按钮清零业务状态、等级回到 40 并同步界面
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_reset_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_name");
    cg_widget_t *chk = cg_find(app, "chk_vip");
    cg_widget_t *sld = cg_find(app, "sld_level");

    (void)ev;
    (void)user;
    memset(&s_biz, 0, sizeof(s_biz));
    s_biz.level = 40;

    if (edt) cg_widget_set_text(edt, "");     /* 界面 -> 会自动回灌 CHANGE，但值一样 */
    if (chk) cg_widget_set_checked(chk, 0);
    if (sld) cg_widget_set_value(sld, s_biz.level);
    refresh_level(app);
    set_status(app, "已重置");
    log_line(app, "重置完成");
}

/**
 * @brief 退出按钮打印业务摘要后退出应用
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_quit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    log_line(app, "退出");
    printf("business summary: clicks=%d vip=%d level=%d name=%s\n",
           s_biz.click_count, s_biz.vip, s_biz.level, s_biz.name);
    cg_quit(app);
}

/**
 * @brief 姓名框变动时把文本同步进业务状态并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_name_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    const char *text = cg_widget_text(self);

    (void)ev;
    (void)user;
    snprintf(s_biz.name, sizeof(s_biz.name), "%s", text ? text : "");
    set_status(app, s_biz.name[0] ? "姓名已记录" : "姓名已清空");
}

/**
 * @brief 姓名框回车提交时记一条日志并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_name_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    log_line(app, "姓名回车提交: %s", cg_widget_text(self));
    set_status(app, "回车提交");
}

/**
 * @brief VIP 勾选变化时把勾选状态写进业务并提示
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_vip_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    s_biz.vip = cg_widget_checked(self);
    log_line(app, "VIP = %s", s_biz.vip ? "开" : "关");
    set_status(app, s_biz.vip ? "VIP 已开启" : "VIP 已关闭");
}

/**
 * @brief 等级滑块变化时把数值写进业务并刷新界面
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_level_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    s_biz.level = cg_widget_value(self);
    refresh_level(app);
    log_line(app, "等级 -> %d", s_biz.level);
}

/* ============================================================ 页 1 回调 */

/* 菜单栏：所有菜单项都汇到这一个回调，用 cg_menu_last_id 区分是哪一项 */
/**
 * @brief 菜单项被点击时按 id 分发处理
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 * @note 菜单栏：所有菜单项都汇到这一个回调，用"最近点中的菜单项 id"区分是哪一项
 */
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    const char *id = cg_menu_last_id(self);
    const char *text = cg_menu_last_text(self);

    (void)ev;
    (void)user;
    status(app, "菜单：%s（id=%s）", text ? text : "?", id ? id : "无");

    if (id && strcmp(id, "m_quit") == 0) {
        cg_quit(app);
    } else if (id && strcmp(id, "m_new") == 0) {
        cg_widget_set_text(cg_find(app, "rt_notes"), "");
        cg_table_clear(cg_find(app, "tbl_data"));
        status(app, "已新建（清空了富文本和表格）");
    } else if (id && strcmp(id, "m_about") == 0) {
        /* 模态：打开期间主窗口被挡住，返回的就是"确定=1 / 取消=0" */
        int r = cg_dialog_open(cg_find(app, "dlg_about"));
        status(app, "对话框关了（result=%d）", r);
    } else if (id && strcmp(id, "m_about_nm") == 0) {
        /* 非模态：立即返回，主窗口照常能用，对话框活着等用户自己关 */
        cg_dialog_open_nonmodal(cg_find(app, "dlg_about"));
        status(app, "已开非模态对话框（主窗口仍可用）");
    }
}

/* 菜单按钮：点一下弹菜单，选中项同样回到这个回调 */
/**
 * @brief 菜单按钮选中项被点击时把选中项文字显示到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 * @note 菜单按钮：点一下弹菜单，选中项同样回到这个回调
 */
static void on_more_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "弹出菜单项：%s", cg_menu_last_text(self) ? cg_menu_last_text(self) : "?");
}

/* 工具栏：value 是第几个按钮（0 起） */
/**
 * @brief 工具栏按钮被点击时显示是第几个按钮
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 * @note 工具栏：控件的 value 是第几个按钮（0 起）
 */
static void on_toolbar_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int index = cg_widget_value(self);
    const char *label = cg_widget_list_item(self, index);

    (void)ev;
    (void)user;
    status(app, "工具栏第 %d 个按钮：%s", index + 1, label ? label : "?");
}

/**
 * @brief 富文本改动时置脏标记并在状态栏显示当前字数
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_richtext_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    (void)ev;

    if (biz) {
        biz->dirty = 1;
        biz->close_tried = 0;   /* 又改动了，关窗前重新拦一次 */
    }
    status(app, "富文本改了（未保存），现在 %d 个字符", (int)strlen(cg_widget_text(self)));
}

/**
 * @brief 日期变化时把新日期显示到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_date_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "日期 -> %s", cg_widget_text(self));
}

/**
 * @brief 时间变化时把新时间显示到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_time_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "时间 -> %s", cg_widget_text(self));
}

/**
 * @brief 日期时间变化时把新值显示到状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_datetime_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "日期+时间 -> %s", cg_widget_text(self));
}

/**
 * @brief 通知开关变化时更新业务、按钮文字与状态栏概况
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_notify_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    biz_t *biz = (biz_t *)user;
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    if (biz) biz->notify = cg_widget_checked(self);
    cg_widget_set_text(self, cg_widget_checked(self) ? "通知" : "静音");
    status(app, "开关 -> %s", cg_widget_checked(self) ? "开" : "关");
    refresh_status_summary(app, biz);
}

/**
 * @brief 数量微调变化时联动圆环与标签并刷新概况
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_count_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    char buf[64];
    cg_widget_t *ring = cg_find(app, "ring_count");

    (void)ev;
    if (biz) biz->count = cg_widget_value(self);
    snprintf(buf, sizeof(buf), "数量 %d", cg_widget_value(self));
    cg_widget_set_text(cg_find(app, "lab_count"), buf);
    /* 控件之间联动：圆环进度跟着微调框走 */
    if (ring) cg_widget_set_value(ring, cg_widget_value(self));
    status(app, "微调 -> %d", cg_widget_value(self));
    refresh_status_summary(app, biz);
}

/**
 * @brief 选中树节点时把节点文字显示到标签和状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_tree_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    const char *text = cg_tree_selected_text(self);
    char buf[128];

    (void)ev;
    (void)user;
    snprintf(buf, sizeof(buf), "树：%s", text ? text : "未选中");
    cg_widget_set_text(cg_find(app, "lab_tree_sel"), buf);
    status(app, "选中树节点 %d: %s", cg_tree_selected(self), text ? text : "-");
}

/**
 * @brief 双击树节点时在状态栏显示节点文字
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_tree_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "双击了树节点：%s", cg_tree_selected_text(self) ? cg_tree_selected_text(self) : "-");
}

/**
 * @brief 点击按钮向树里动态添加节点
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_add_node(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *tree = cg_find(app, "tree_files");
    biz_t *biz = (biz_t *)user;

    (void)ev;
    if (!tree) return;

    if (biz && biz->tree_added == 0) {
        /* 第一次：往根上挂一个，再给它挂个子节点 */
        int parent = cg_tree_add(tree, 0, "运行时动态加的");
        cg_tree_addf(tree, parent, "第 %d 次点击生成的子节点", 1);
    } else {
        cg_tree_addf(tree, -1, "动态节点 #%d", biz ? biz->tree_added + 1 : 1);
    }
    if (biz) biz->tree_added++;
    status(app, "树现在有 %d 个节点", cg_tree_count(tree));
    refresh_status_summary(app, biz);
}

/**
 * @brief 往表格末尾追加一行（姓名 / 年龄 / 城市）并刷新状态栏
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 */
static void on_add_row(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "tbl_data");
    biz_t *biz = (biz_t *)user;
    const char *cells[3];
    char name[32], age[16];
    int n;

    (void)ev;
    if (!tbl) return;
    n = biz ? ++biz->rows_added : 1;
    snprintf(name, sizeof(name), "新同事 %d", n);
    snprintf(age, sizeof(age), "%d", 20 + n);
    cells[0] = name;
    cells[1] = age;
    cells[2] = "北京";
    cg_table_add_row(tbl, cells, 3);
    status(app, "表格现在有 %d 行", cg_table_row_count(tbl));
    refresh_status_summary(app, biz);
}

/**
 * @brief 页内标签页切换时把当前页号与总页数写到状态栏
 * @param self 该标签页控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_tab_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "切到第 %d 页（共 %d 页）", cg_tabs_selected(self) + 1, cg_tabs_count(self));
}

/**
 * @brief 顶层标签页切换时把当前页号写到状态栏
 * @param self 该标签页控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_pages_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "顶层切到第 %d 页", cg_tabs_selected(self) + 1);
}

/**
 * @brief 表格选中某行时把该行前两列写到状态栏
 * @param self 表格控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_table_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int row = cg_table_selected(self);

    (void)ev;
    (void)user;
    if (row >= 0) {
        status(app, "表格选中第 %d 行：%s / %s", row + 1,
               cg_table_cell(self, row, 0), cg_table_cell(self, row, 1));
    }
}

/**
 * @brief 读取第一页输入框内容并写到状态栏，空则显示"（空）"
 * @param self 触发事件的控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_p1_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_p1");
    (void)ev;
    (void)user;
    status(app, "第一页输入框里是：%s",
           (edt && cg_widget_text(edt)[0]) ? cg_widget_text(edt) : "(空)");
}

/**
 * @brief 第一页输入框内容变化时把新内容写到状态栏
 * @param self 该输入框控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_p1_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "第一页输入框改了：%s", cg_widget_text(self));
}

/**
 * @brief 第二页复选框变化时把勾选 / 取消写到状态栏
 * @param self 该复选框控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_p2_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "第二页复选框 -> %s", cg_widget_checked(self) ? "勾上" : "取消");
}

/**
 * @brief 树节点展开时把节点文字与节点序号写到状态栏
 * @param self 该树控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_tree_expand(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    /* expand / collapse 是树专有的：回调里能拿到展开的是哪个节点 */
    status(app, "展开：%s（第 %d 个节点）",
           cg_tree_item_text(self, cg_tree_selected(self)) ? cg_tree_item_text(self, cg_tree_selected(self)) : "-",
           cg_tree_selected(self));
}

/**
 * @brief 树节点折叠时把节点文字写到状态栏
 * @param self 该树控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_tree_collapse(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "折叠：%s",
           cg_tree_item_text(self, cg_tree_selected(self)) ? cg_tree_item_text(self, cg_tree_selected(self)) : "-");
}

/**
 * @brief 链接文字被点击时把链接文字写到状态栏
 * @param self 该链接控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_link_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    /* 真实程序里这里可以用 ShellExecute 打开浏览器；示例只写状态栏 */
    status(app, "点的是链接：%s", cg_widget_text(self));
}

/**
 * @brief 图标按钮被点击时把图标资源路径写到状态栏
 * @param self 该图标按钮控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_icon_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "图标按钮被点了（src = %s）", cg_widget_image(self));
}

/**
 * @brief 月历选中日期后同步到日期控件，并把选中值写到状态栏
 * @param self 该月历控件
 * @param ev 事件类型
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_cal_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *dt = cg_find(app, "dt_date");
    (void)ev;
    (void)user;

    status(app, "月历选中 %s", cg_widget_text(self));
    /* 月历和日期控件都规规矩矩把值放在 text 里，所以两边可以直接互通 */
    if (dt) cg_widget_set_text(dt, cg_widget_text(self));
}

/* 窗口关闭前：有没保存的内容就拦一次。
 * 窗口自身也是个"控件"（id 是 __window），所以回调签名和别的事件一样。 */
/**
 * @brief 窗口关闭前拦一次：富文本有未保存改动时取消本次关闭
 * @param self 窗口控件
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 无返回值
 * @note 第一次关闭被拦下并提示，再点一次才真正退出
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    cg_widget_t *sb = cg_find(app, "sb_main");
    (void)ev;

    if (biz && biz->dirty && !biz->close_tried) {
        biz->close_tried = 1;
        printf("[gallery] 富文本有未保存的改动，已拦下这次关闭（再点一次就退出）\n");
        if (sb) cg_status_set(sb, 1, "有未保存的改动：再点一次关闭才退出");
        cg_cancel_close(app);      /* 这次先不关 */
        return;
    }
    if (sb) cg_status_set(sb, 1, "关闭中…");
    printf("[gallery] 窗口关闭\n");
}

/* ============================================================ 页 2 回调 */

/* 时钟：每秒把计数写回页内状态标签 */
/**
 * @brief 时钟每秒把累计秒数写回页内状态标签
 * @param self 时钟控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 自检等无窗口场景不刷新界面
 */
static void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    static int ticks = 0;
    (void)self; (void)ev; (void)user;
    ticks++;
    if (cg_app_has_window(cg_app_of(self))) {
        lab_statusf(cg_app_of(self), "时钟走了 %d 秒", ticks);
    }
}

/* 通用对话框：自检（无窗口）时什么都不做，避免弹窗卡住 CI */
/**
 * @brief 弹打开文件对话框，把所选路径写到页内状态标签
 * @param self 触发事件的控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 自检（无窗口）时什么都不做，避免弹窗卡住 CI
 */
static void on_pick_file(cg_widget_t *self, cg_event_t ev, void *user)
{
    char path[1024];
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    if (cg_open_file_dialog(self, "选择一个文件",
                            "文本文件|*.txt|图片|*.png;*.jpg|全部|*.*",
                            path, sizeof(path))) {
        lab_statusf(cg_app_of(self), "打开: %s", path);
    } else {
        lab_statusf(cg_app_of(self), "取消了打开");
    }
}

/**
 * @brief 弹保存文件对话框，把所选路径写到页内状态标签
 * @param self 触发事件的控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 自检（无窗口）时什么都不做，避免弹窗卡住 CI
 */
static void on_pick_save(cg_widget_t *self, cg_event_t ev, void *user)
{
    char path[1024];
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    if (cg_save_file_dialog(self, "保存到哪里", "文本文件|*.txt", "txt",
                            path, sizeof(path))) {
        lab_statusf(cg_app_of(self), "保存到: %s", path);
    } else {
        lab_statusf(cg_app_of(self), "取消了保存");
    }
}

/**
 * @brief 弹一个运行时消息框
 * @param self 触发事件的控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 自检（无窗口）时什么都不做，避免弹窗卡住 CI
 */
static void on_show_msg(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    cg_message_box(self, "这是运行时的消息框。", "gallery", 0);
}

/* 颜色选择：value 就是 0xRRGGBB */
/**
 * @brief 颜色选择变化时把 0xRRGGBB 拆成分量写到页内状态标签
 * @param self 该颜色选择控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 控件的 value 就是 0xRRGGBB
 */
static void on_color_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "选了颜色 #%06X", (unsigned)cg_widget_value(self));
}

/* 表格：勾选 / 多选 / 行数值 */
/**
 * @brief 表格某行勾选变化时把行号、勾选值与行数值写到页内状态标签
 * @param self 该表格控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 没有选中行时按第 0 行处理
 */
static void on_table_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_table_selected(self);
    (void)ev; (void)user;
    if (row < 0) row = 0;
    lab_statusf(cg_app_of(self), "第 %d 行勾选变成 %d（行数值=%d）",
                row, cg_table_checked(self, row), cg_table_row_value(self, row));
}

/**
 * @brief 表格多选变化时把所有选中行第一列拼成串写到页内状态标签
 * @param self 该表格控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 * @note 拼接缓冲固定 128 字节，超长会自动截断
 */
static void on_lab_table_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int i;
    char names[128] = "";
    (void)ev; (void)user;
    for (i = 0; i < cg_table_selected_count(self); i++) {
        int row = cg_table_selected_row(self, i);
        const char *cell = cg_table_cell(self, row, 0);
        if (cell) strncat(names, cell, sizeof(names) - strlen(names) - 2);
        if (i + 1 < cg_table_selected_count(self)) strncat(names, ",", sizeof(names) - strlen(names) - 2);
    }
    lab_statusf(cg_app_of(self), "选中: %s", names);
}

/* 画板：点一下画一个圆 */
/**
 * @brief 画板被点击时画一个圆与一段说明文字
 * @param self 该画板控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_canvas_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    cg_canvas_set_color(self, 0x6750A4);
    cg_canvas_ellipse(self, 60, 60, 40, 40, 1);
    cg_canvas_text(self, 12, 110, "点了一下");
}

/**
 * @brief 评分变化时把当前分值与满分 5 写到页内状态标签
 * @param self 该评分控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_rate_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "评分: %d / %d", cg_widget_value(self), 5);
}

/* ------------------------------------------------------------ 图标列表（iconlist） */

/**
 * @brief 页 2 图标列表初始化：给 .cgui 预置的三行补图标/副标题/状态，再追加三行完整数据
 * @param app 所属应用
 * @return void 无返回值
 * @note 演示两种数据来源：.cgui 的 items（只有主标题，用 set_row 补全）+ 运行时 add；
 *       图标只在绘制时才加载，所以自检（无窗）跑这段也零副作用。
 */
static void il_ready(cg_app_t *app)
{
    cg_widget_t *il = cg_find(app, "il_demo");

    if (!il) return;
    /* 前三行来自 .cgui 的 items：这里补图标 / 副标题 / 右侧状态 */
    cg_iconlist_set_row(il, 0, "assets/icon.png", "复制文件路径",
                        "\"...\\Mxx1Toolbox.exe\" copypath --name \"%1\"", "在用 · 用户区");
    cg_iconlist_set_row(il, 1, "assets/icon.png", "解除文件占用",
                        "\"...\\Mxx1Toolbox.exe\" rightmenu unlock \"%1\"", "在用 · 用户区");
    cg_iconlist_set_row(il, 2, NULL, "用记事本打开", "notepad %1", "在用 · 系统区（要管理员）");
    cg_iconlist_set_checked(il, 1, 1);
    /* 运行时追加的三行：图标 / 副标题 / 状态一次给全 */
    cg_iconlist_add(il, "assets/icon.png", "使用微信输入法隔空传送",
                    "\"...\\WeType_update.exe\" --filetransfer \"%1\"", "已禁用 · 用户区");
    cg_iconlist_add(il, NULL, "获取管理员所有权限",
                    "cmd.exe /c takeown /f \"%1\" & icacls \"%1\" /grant administrators:F",
                    "在用 · 系统区（要管理员）");
    cg_iconlist_add(il, "assets/icon.png", "7-Zip Shell Extension（扩展项）",
                    "CLSID {23170F69-3C4C-1000-0001-000000000000} · 7-zip.dll",
                    "扩展项 · 系统区");
    cg_iconlist_set_checked(il, 4, 1);
    log_line(app, "iconlist 就绪：%d 行（3 行来自 .cgui items + 3 行运行时 add）",
           cg_iconlist_count(il));
}

/**
 * @brief 图标列表点行选中：状态标签与日志写出选中的行
 * @param self 图标列表控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_il_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_widget_value(self);

    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "图标列表选中第 %d 行", row + 1);
    log_line(cg_app_of(self), "iconlist select: row=%d / 共 %d 行", row, cg_iconlist_count(self));
}

/**
 * @brief 图标列表勾选变化：数一遍勾选行数写进日志
 * @param self 图标列表控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_il_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    int i, n = cg_iconlist_count(self), chk = 0;

    (void)ev; (void)user;
    for (i = 0; i < n; i++)
        if (cg_iconlist_checked(self, i)) chk++;
    lab_statusf(cg_app_of(self), "勾选 %d / %d 行", chk, n);
    log_line(cg_app_of(self), "iconlist check: 勾选 %d 行", chk);
}

/**
 * @brief 图标列表双击：写出双击的那一行标题（双击由运行时从 click 合成，不用自己判时间）
 * @param self 图标列表控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_il_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_widget_value(self);

    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "双击了第 %d 行", row + 1);
    log_line(cg_app_of(self), "iconlist dblclick: row=%d", row);
}

/**
 * @brief 分页控件翻页时把页号（从 1 起）写到页内状态标签
 * @param self 该分页控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_page_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "翻到第 %d 页", cg_widget_value(self) + 1);
}

/**
 * @brief 标签片被点击时往页内状态标签打一条提示
 * @param self 该标签片控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_tag_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "标签片被点了");
}

/**
 * @brief 卡片开关变化时把开 / 关状态写到页内状态标签
 * @param self 该开关控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_switch_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "卡片开关: %s", cg_widget_checked(self) ? "开" : "关");
}

/* 焦点事件：失焦时校验输入框 */
/**
 * @brief 输入框获得焦点时往页内状态标签打一条提示
 * @param self 该输入框控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_focus(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self; (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "输入框获得焦点");
}

/**
 * @brief 输入框失焦时把当前内容写到页内状态标签，空则显示"（空）"
 * @param self 该输入框控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_blur(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "失焦，内容是 \"%s\"",
                *cg_widget_text(self) ? cg_widget_text(self) : "(空)");
}

/**
 * @brief 提交时把输入框内容写到页内状态标签
 * @param self 该输入框控件
 * @param ev 事件类型（本回调不用）
 * @param user 调用方传进来的业务数据（本回调不用）
 * @return void 无返回值
 */
static void on_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    lab_statusf(cg_app_of(self), "提交: %s", cg_widget_text(self));
}

/* ============================================================ 界面就绪 */

/**
 * @brief 页 0「基础闭环」的初始状态：挂业务数据、摆滑块初值、写启动日志
 * @param app 所属应用
 * @param biz 业务状态结构，非空，等级字段在此被置为 40
 * @return void 无返回值
 */
static void hello_ready(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *w;

    biz->level = 40;

    /* 业务数据挂到需要它的控件上，回调里就能直接拿到 */
    if ((w = cg_find(app, "btn_ok")) != NULL)  cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "chk_vip")) != NULL) cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "edt_name")) != NULL) cg_widget_set_user(w, biz);

    /* 用业务状态摆一次初始界面（.cgui 里给的是设计态默认值） */
    if ((w = cg_find(app, "sld_level")) != NULL) cg_widget_set_value(w, biz->level);
    refresh_level(app);
    set_status(app, "就绪：界面由 .cgui 描述，逻辑在 main.c");

    log_line(app, "启动完成，共 %d 个控件", cg_widget_count(app));
}

/**
 * @brief 页 1「控件陈列」的初始状态：挂业务数据、联动圆环、给树 / 表格补内容
 * @param app 所属应用
 * @param biz 业务状态结构，可为 NULL；非空时把开关置开、数量置 5
 * @return void 无返回值
 */
static void gallery_ready(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *w;

    if (biz) {
        biz->notify = 1;
        biz->count = 5;
    }

    /* 业务数据挂到需要它的控件上 */
    if ((w = cg_find(app, "sw_notify")) != NULL)    cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "spn_count")) != NULL)    cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "btn_add_node")) != NULL) cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "btn_add_row")) != NULL)  cg_widget_set_user(w, biz);
    if ((w = cg_find(app, "rt_notes")) != NULL)     cg_widget_set_user(w, biz);
    /* 窗口自身也是控件（id = __window），close 回调要靠它拿业务数据 */
    if ((w = cg_find(app, CG_WINDOW_ID)) != NULL)   cg_widget_set_user(w, biz);

    /* 圆环进度跟上微调框的初值 */
    if ((w = cg_find(app, "ring_count")) != NULL) {
        cg_widget_set_value(w, cg_widget_value(cg_find(app, "spn_count")));
    }

    /* 启动时可以用代码往声明式的树/表格里追加内容 */
    if ((w = cg_find(app, "tree_files")) != NULL) {
        int parent = cg_tree_add(w, -1, "启动时加的（代码）");
        cg_tree_add(w, parent, "说明：.cgui 里声明 + C 里追加 都行");
    }
    if ((w = cg_find(app, "tbl_data")) != NULL) {
        const char *cells[3] = { "王五", "41", "广州" };
        cg_table_add_row(w, cells, 3);
    }

    printf("[gallery] 就绪：控件 %d 个；树 %d 节点；表格 %d 行；菜单 %d 项\n",
           cg_widget_count(app),
           cg_tree_count(cg_find(app, "tree_files")),
           cg_table_row_count(cg_find(app, "tbl_data")),
           cg_menu_count(cg_find(app, "menu_main")));
    status(app, "就绪，图片 %s", cg_widget_image(cg_find(app, "img_logo")));
    refresh_status_summary(app, biz);
}

/**
 * @brief 页 2「新特性」的初始状态：给画板打底稿、给表格挂行数值与初始勾选
 * @param app 所属应用
 * @param user 就绪钩子的业务数据（本回调不用）
 * @return void 无返回值
 */
static void lab_ready(cg_app_t *app, void *user)
{
    cg_widget_t *cv = cg_find(app, "cv_board");
    cg_widget_t *tb = cg_find(app, "tb_users");
    (void)user;

    if (cv) {
        /* 画板打底稿：线 / 描边矩形 / 椭圆 / 文字 */
        cg_canvas_set_line_width(cv, 2);
        cg_canvas_set_color(cv, 0x333333);
        cg_canvas_line(cv, 10, 10, 260, 10);
        cg_canvas_rect(cv, 10, 24, 80, 40, 0);
        cg_canvas_ellipse(cv, 120, 24, 80, 40, 0);
        cg_canvas_set_color(cv, 0x1A73E8);
        cg_canvas_text(cv, 12, 90, "画板：cg_canvas_* 画的");
    }
    if (tb) {
        /* 行数值（不显示在表格里）+ 初始勾选 */
        cg_table_set_row_value(tb, 0, 101);
        cg_table_set_row_value(tb, 1, 102);
        cg_table_set_row_value(tb, 2, 103);
        cg_table_set_row_value(tb, 3, 104);
        cg_table_set_checked(tb, 0, 1);
    }
}

/**
 * @brief 界面就绪钩子：依次调三页的初始化
 * @param app 所属应用
 * @param user 就绪钩子的业务数据，作为三页共用的业务状态
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    biz_t *biz = (biz_t *)user;

    hello_ready(app, biz);
    gallery_ready(app, biz);
    lab_ready(app, user);
    il_ready(app);
}

/* ------------------------------------------------------------ 多窗体（对话框）
 * 对话框是**独立顶层窗口**：cg_dialog_open 模态打开（返回时才有结果），
 * cg_dialog_open_nonmodal 非模态打开（立即返回）。关闭一律 cg_dialog_close(self, result)。
 */
/**
 * @brief 对话框里的"确定"：带结果 1 关闭
 * @param self 触发事件的控件（对话框里的按钮）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 */
static void on_dlg_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}

/**
 * @brief 对话框里的"取消"：带结果 0 关闭
 * @param self 触发事件的控件（对话框里的按钮）
 * @param ev 事件类型
 * @param user 挂在控件上的业务数据
 * @return void 回调无返回值
 * @note Esc 与右上角关闭钮走的也是这条 result=0 的路，业务不必各接一遍。
 */
static void on_dlg_cancel(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 0);
}

/* ------------------------------------------------------------ 绑定表
 * 左边是 .cgui 里写的名字，右边是本文件里的函数。
 */
static const cg_binding_t k_bindings[] = {
    /* 窗口级 */
    CG_BIND("on_close",         on_close),
    /* 顶层标签页 */
    CG_BIND("on_pages_change",  on_pages_change),
    /* ---- 页 0 基础闭环 ---- */
    CG_BIND("on_ok_click",      on_ok_click),
    CG_BIND("on_ok_dblclick",   on_ok_dblclick),
    CG_BIND("on_reset_click",   on_reset_click),
    CG_BIND("on_quit_click",    on_quit_click),
    CG_BIND("on_name_change",   on_name_change),
    CG_BIND("on_name_submit",   on_name_submit),
    CG_BIND("on_vip_change",    on_vip_change),
    CG_BIND("on_level_change",  on_level_change),
    /* ---- 页 1 控件陈列 ---- */
    CG_BIND("on_menu_click",    on_menu_click),
    CG_BIND("on_more_click",    on_more_click),
    CG_BIND("on_toolbar_click", on_toolbar_click),
    CG_BIND("on_tree_select",   on_tree_select),
    CG_BIND("on_tree_dblclick", on_tree_dblclick),
    CG_BIND("on_tree_expand",   on_tree_expand),
    CG_BIND("on_tree_collapse", on_tree_collapse),
    CG_BIND("on_tab_change",    on_tab_change),
    CG_BIND("on_table_select",  on_table_select),
    CG_BIND("on_cal_select",    on_cal_select),
    CG_BIND("on_add_node",      on_add_node),
    CG_BIND("on_add_row",       on_add_row),
    CG_BIND("on_notify_change", on_notify_change),
    CG_BIND("on_count_change",  on_count_change),
    CG_BIND("on_richtext_change", on_richtext_change),
    CG_BIND("on_date_change",   on_date_change),
    CG_BIND("on_time_change",   on_time_change),
    CG_BIND("on_datetime_change", on_datetime_change),
    CG_BIND("on_p1_click",      on_p1_click),
    CG_BIND("on_p1_change",     on_p1_change),
    CG_BIND("on_p2_check",      on_p2_check),
    CG_BIND("on_link_click",    on_link_click),
    CG_BIND("on_icon_click",    on_icon_click),
    /* ---- 页 2 新特性 ---- */
    CG_BIND("on_tick",          on_tick),
    CG_BIND("on_pick_file",     on_pick_file),
    CG_BIND("on_pick_save",     on_pick_save),
    CG_BIND("on_show_msg",      on_show_msg),
    CG_BIND("on_color_change",  on_color_change),
    CG_BIND("on_table_check",   on_table_check),
    CG_BIND("on_lab_table_select", on_lab_table_select),
    CG_BIND("on_canvas_click",  on_canvas_click),
    CG_BIND("on_rate_change",   on_rate_change),
    CG_BIND("on_il_select",     on_il_select),
    CG_BIND("on_il_check",      on_il_check),
    CG_BIND("on_il_dblclick",   on_il_dblclick),
    CG_BIND("on_page_change",   on_page_change),
    CG_BIND("on_tag_click",     on_tag_click),
    CG_BIND("on_switch_change", on_switch_change),
    CG_BIND("on_focus",         on_focus),
    CG_BIND("on_blur",          on_blur),
    CG_BIND("on_submit",        on_submit),
    /* ---- 多窗体（对话框）---- */
    CG_BIND("on_dlg_ok",        on_dlg_ok),
    CG_BIND("on_dlg_cancel",    on_dlg_cancel),
    CG_BIND_END
};

/* ------------------------------------------------------------ 入口 */

/**
 * @brief 程序入口：组装应用描述，按命令行参数走自检或正常运行
 * @param argc 命令行参数个数，大于 1 时才会解析开关
 * @param argv 命令行参数数组，argv[1] 可为 --selftest、--trace 或 .cgui 路径
 * @return int 进程退出码，0 表示正常结束
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 双击时先给 click 再给 dblclick */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */
    desc.on_ready = on_ready;
    desc.ready_user = &s_biz;

    /* --selftest 走命令行自检：解析 + 打印控件树 + 合成事件，不开窗口 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "gallery.cgui";
        return cg_app_selftest(&desc);
    }
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "gallery.cgui";
        desc.trace_doc = 1;
    } else {
        desc.cgui_path = (argc > 1) ? argv[1] : "gallery.cgui";
    }

    printf("CStackGUI %s -- 界面文件: %s\n", CG_VERSION_STRING, desc.cgui_path);
    return cg_app_run(&desc);
}
