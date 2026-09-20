/**
 * @file    main.c
 * @brief   入门 04：数据控件 —— 列表框 / 树 / 表格
 * @details 前面几课都是"一个控件一个值"。这一课的三个控件装的是**一组数据**，
 *          所以 API 也分两类，记住这个区分就不会找错函数：
 *
 *            操作        列表框 listbox            树 tree                     表格 table
 *            加内容      cg_widget_list_add()      cg_tree_add(parent, text)   cg_table_add_row()
 *            清空        cg_widget_list_clear()    cg_tree_clear()             cg_table_clear()
 *            有多少个    cg_widget_list_count()    cg_tree_count()             cg_table_row_count()
 *            读某一项    cg_widget_list_item(i)    cg_tree_item_text(i)        cg_table_cell(row,col)
 *            当前选中    cg_widget_value()         cg_tree_selected()          cg_table_selected()
 *
 *          另外两件"只有表格/树才有"的事：
 *            - 树的 cg_tree_add(树, parent, 文字)：**parent 是节点下标，-1 表示加在根上**。
 *              所以"加在选中节点下面"就是 cg_tree_selected() 的返回值（没选中正好是 -1）；
 *            - 表格的行勾选 / 行数值 / 行图标**不是 .cgui 字段**，是运行时 API
 *              （cg_table_set_checked / cg_table_row_value / cg_table_set_row_icon）。
 *
 *          所有事件都往最下面的日志框里写一行，串起"事件 -> 数据 -> 界面"这条链。
 *
 * 界面在 lesson04_data.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson04_data
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 各控件自己加过几条，用来自动起名字 */
static int s_added_items;
static int s_added_nodes;
static int s_added_rows;

/** @brief 往日志框追加一行，同时更新状态栏（printf 风格） */
static void log_event(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_widget_list_add(cg_find(app, "lst_event"), buf);
    cg_widget_list_scroll_bottom(cg_find(app, "lst_event"));   /* 日志类控件常用：滚到最新一行 */
    cg_status_set(cg_find(app, "sb_main"), 0, buf);
    printf("[log] %s\n", buf);
}

/* ------------------------------------------------------------ 列表框 */

/** @brief 选中项变了：cg_widget_value() = 选中第几项（0 起），没选中是 -1 */
static void on_list_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int         idx  = cg_widget_value(self);
    const char *text = cg_widget_list_item(self, idx);

    (void)ev;
    (void)user;
    log_event(cg_app_of(self), "列表选中第 %d 项：%s", idx, text ? text : "（无）");
}

/**
 * @brief 「加一条」/「清空」共用（一个回调多个入口，用 id 分辨）
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 */
static void on_list_op(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *lst = cg_find(app, "lst_items");
    int          n;

    (void)ev;
    (void)user;

    if (cg_str_equal(cg_widget_id(self), "btn_list_add")) {
        /* 带格式化的版本：cg_widget_list_addf(控件, 格式, ...) */
        cg_widget_list_addf(lst, "条目 %d", ++s_added_items);
        log_event(app, "列表现在有 %d 项", cg_widget_list_count(lst));
    } else {
        n = cg_widget_list_count(lst);          /* 先记住个数，清空之后就拿不到了 */
        cg_widget_list_clear(lst);
        log_event(app, "列表已清空（原来是 %d 项）", n);
    }
}

/* ------------------------------------------------------------ 树 */

/** @brief 树上选中了某个节点：cg_tree_selected() = 节点下标，文字用 cg_tree_selected_text() */
static void on_tree_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *text = cg_tree_selected_text(self);

    (void)ev;
    (void)user;
    log_event(cg_app_of(self), "树上选中第 %d 个节点：%s", cg_tree_selected(self),
              text ? text : "（无）");
}

/** @brief 节点被展开 / 折叠（树独有的事件） */
static void on_tree_expand(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *text = cg_tree_selected_text(self);

    (void)user;
    log_event(cg_app_of(self), "节点「%s」%s", text ? text : "?",
              (ev == CG_EVENT_EXPAND) ? "展开了" : "折叠了");
}

/**
 * @brief 「加节点」/「全展开」共用
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 */
static void on_tree_op(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tre = cg_find(app, "tre_dir");

    (void)ev;
    (void)user;

    if (cg_str_equal(cg_widget_id(self), "btn_tree_add")) {
        /* parent 传"当前选中的节点下标"，没选中时 cg_tree_selected 正好返回 -1 = 加在根上 */
        int parent = cg_tree_selected(tre);
        int idx    = cg_tree_addf(tre, parent, "新节点 %d", ++s_added_nodes);
        log_event(app, "在 %s 下面加了节点 #%d（现在共 %d 个）",
                  (parent < 0) ? "根" : "选中节点", idx, cg_tree_count(tre));
    } else {
        int n = cg_tree_count(tre);
        for (int i = 0; i < n; i++) cg_tree_expand(tre, i, 1);
        log_event(app, "展开了全部 %d 个节点", n);
    }
}

/* ------------------------------------------------------------ 表格 */

/** @brief 表格选中行变了：cg_table_selected() = 行号，单元格用 cg_table_cell(row, col) */
static void on_table_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_table_selected(self);

    (void)ev;
    (void)user;
    if (row >= 0) {
        log_event(cg_app_of(self), "表格选中第 %d 行：%s / %s", row,
                  cg_table_cell(self, row, 0), cg_table_cell(self, row, 1));
    }
}

/** @brief 表格勾选列的行状态变了（.cgui 里 table 要开 check） */
static void on_table_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       row = cg_table_selected(self);   /* 点勾选框时这一行同时也是"选中行" */
    int       n = cg_table_row_count(self);
    int       checked = 0;

    (void)ev;
    (void)user;
    if (row < 0) row = 0;
    for (int i = 0; i < n; i++) {
        if (cg_table_checked(self, i)) checked++;
    }
    log_event(app, "第 %d 行勾选 = %d（全表共勾了 %d 行）",
              row, cg_table_checked(self, row), checked);
}

/**
 * @brief 「加一行」/「清空」共用
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 */
static void on_table_op(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "tbl_data");

    (void)ev;
    (void)user;

    if (cg_str_equal(cg_widget_id(self), "btn_row_add")) {
        char name[32];
        const char *cells[2];

        s_added_rows++;
        snprintf(name, sizeof(name), "新人 %d", s_added_rows);
        cells[0] = name;                  /* cells 的顺序 = 列的顺序 */
        cells[1] = "未填城市";
        cg_table_add_row(tbl, cells, 2);
        log_event(app, "加了一行，现在共 %d 行", cg_table_row_count(tbl));
    } else {
        int n = cg_table_row_count(tbl);
        cg_table_clear(tbl);
        log_event(app, "表格已清空（原来是 %d 行）", n);
    }
}

/** @brief 界面建好：先写一行开场白，让日志框不是空的 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;
    log_event(app, "三个数据控件已就绪，点点看，每个动作都会记在这里");
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_list_select",   on_list_select),
    CG_BIND("on_list_op",       on_list_op),
    CG_BIND("on_tree_select",   on_tree_select),
    CG_BIND("on_tree_expand",   on_tree_expand),
    CG_BIND("on_tree_collapse", on_tree_expand),   /* 展开/折叠共用一个：回调里看 ev 分辨 */
    CG_BIND("on_tree_op",       on_tree_op),
    CG_BIND("on_table_select",  on_table_select),
    CG_BIND("on_table_check",   on_table_check),
    CG_BIND("on_table_op",      on_table_op),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson04_data.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson04_data.cgui";
    return cg_app_run(&desc);
}
