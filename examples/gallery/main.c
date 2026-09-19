/* main.c -- 控件陈列室：演示 v1.1 新增的 6 种控件
 *
 *   image      按控件大小缩放显示图片（路径相对 .cgui 所在目录）
 *   switch     开关（自绘）
 *   spinner    数字微调
 *   tabs       标签页，子控件按 "page" 分页
 *   tree       树（可在 .cgui 里声明，也能用 cg_tree_add 动态加）
 *   table      表格（列/行）
 *   statusbar  状态栏（分段文字）
 *
 * 界面在 gallery.cgui 里；本文件只有行为和「启动时该干什么」。
 * 编译：在项目根跑 build-app.bat gallery      产物 build\gallery.exe
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int notify;
    int count;
    int tree_added;   /* 动态加过几个树节点 */
    int rows_added;   /* 动态加过几行表格 */
    int dirty;        /* 富文本改过还没"保存" */
    int close_tried;  /* 已经拦过一次关闭 */
} biz_t;

static biz_t g_biz;

static void refresh_status_summary(cg_app_t *app, biz_t *biz);

/* 把一句话同时写到状态栏第二段和 stdout */
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
static void refresh_status_summary(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *sb = cg_find(app, "sb_main");
    if (!sb || !biz) return;
    cg_status_setf(sb, 0, "开关 %s · 数量 %d · 树 +%d · 表 +%d",
                   biz->notify ? "开" : "关", biz->count, biz->tree_added, biz->rows_added);
}

/* ------------------------------------------------------------ 回调 */

/* 菜单栏：所有菜单项都汇到这一个回调，用 cg_menu_last_id 区分是哪一项 */
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
    }
}

/* 菜单按钮：点一下弹菜单，选中项同样回到这个回调 */
static void on_more_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "弹出菜单项：%s", cg_menu_last_text(self) ? cg_menu_last_text(self) : "?");
}

/* 工具栏：value 是第几个按钮（0 起） */
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
 * @brief 111
 * @param self 
 * @param ev 
 * @param user 
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

static void on_date_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "日期 -> %s", cg_widget_text(self));
}

static void on_time_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "时间 -> %s", cg_widget_text(self));
}

static void on_datetime_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "日期+时间 -> %s", cg_widget_text(self));
}

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

static void on_count_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    biz_t *biz = (biz_t *)user;
    char buf[64];

    (void)ev;
    cg_widget_t *ring = cg_find(app, "ring_count");

    if (biz) biz->count = cg_widget_value(self);
    snprintf(buf, sizeof(buf), "数量 %d", cg_widget_value(self));
    cg_widget_set_text(cg_find(app, "lab_count"), buf);
    /* 控件之间联动：圆环进度跟着微调框走 */
    if (ring) cg_widget_set_value(ring, cg_widget_value(self));
    status(app, "微调 -> %d", cg_widget_value(self));
    refresh_status_summary(app, biz);
}

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

static void on_tree_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "双击了树节点：%s", cg_tree_selected_text(self) ? cg_tree_selected_text(self) : "-");
}

static void on_add_node(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *tree = cg_find(app, "tree_files");
    biz_t *biz = (biz_t *)user;

    (void)ev;
    if (!tree) return;

    if (biz && biz->tree_added == 0) {
        /* 第一次：往已有的「apps」节点下面挂 —— 需要知道它的下标，
         * 声明式的树按 .cgui 里的顺序摊平，apps 是第 3 个（从 0 数） */
        int parent = cg_tree_add(tree, 0, "运行时动态加的");   /* 挂在根「项目」下 */
        cg_tree_addf(tree, parent, "第 %d 次点击生成的子节点", 1);
    } else {
        cg_tree_addf(tree, -1, "动态节点 #%d", biz ? biz->tree_added + 1 : 1);
    }
    if (biz) biz->tree_added++;
    status(app, "树现在有 %d 个节点", cg_tree_count(tree));
    refresh_status_summary(app, biz);
}

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

static void on_tab_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "切到第 %d 页（共 %d 页）", cg_tabs_selected(self) + 1, cg_tabs_count(self));
}

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

static void on_p1_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_p1");
    (void)ev;
    (void)user;
    status(app, "第一页输入框里是：%s",
           (edt && cg_widget_text(edt)[0]) ? cg_widget_text(edt) : "(空)");
}

static void on_p1_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "第一页输入框改了：%s", cg_widget_text(self));
}

static void on_p2_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "第二页复选框 -> %s", cg_widget_checked(self) ? "勾上" : "取消");
}



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

static void on_tree_collapse(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "折叠：%s",
           cg_tree_item_text(self, cg_tree_selected(self)) ? cg_tree_item_text(self, cg_tree_selected(self)) : "-");
}

static void on_link_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    /* 真实程序里这里可以用 ShellExecute 打开浏览器；示例只写状态栏 */
    status(app, "点的是链接：%s", cg_widget_text(self));
}

static void on_icon_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    (void)ev;
    (void)user;
    status(app, "图标按钮被点了（src = %s）", cg_widget_image(self));
}

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

/* ------------------------------------------------------------ 界面就绪 */

static void on_ready(cg_app_t *app, void *user)
{
    biz_t *biz = (biz_t *)user;
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
    /* 菜单 / 工具栏 / 日期时间这些不需要业务数据，按名字走绑定表就够了 */

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
        cg_tabs_select(cg_find(app, "tabs_main"), 0);   /* 显式回到第一页 */
    }

    printf("[gallery] 就绪：控件 %d 个；树 %d 节点；表格 %d 行；菜单 %d 项\n",
           cg_widget_count(app),
           cg_tree_count(cg_find(app, "tree_files")),
           cg_table_row_count(cg_find(app, "tbl_data")),
           cg_menu_count(cg_find(app, "menu_main")));
    status(app, "就绪，图片 %s", cg_widget_image(cg_find(app, "img_logo")));
    refresh_status_summary(app, biz);
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    /* 窗口级 */
    CG_BIND("on_close",         on_close),
    /* 菜单栏 / 菜单按钮 / 工具栏 */
    CG_BIND("on_menu_click",    on_menu_click),
    CG_BIND("on_more_click",    on_more_click),
    CG_BIND("on_toolbar_click", on_toolbar_click),
    /* 数据类控件 */
    CG_BIND("on_tree_select",   on_tree_select),
    CG_BIND("on_tree_dblclick", on_tree_dblclick),
    CG_BIND("on_tree_expand",   on_tree_expand),
    CG_BIND("on_tree_collapse", on_tree_collapse),
    CG_BIND("on_tab_change",    on_tab_change),
    CG_BIND("on_table_select",  on_table_select),
    CG_BIND("on_cal_select",    on_cal_select),
    CG_BIND("on_add_node",      on_add_node),
    CG_BIND("on_add_row",       on_add_row),
    /* 输入类控件 */
    CG_BIND("on_notify_change", on_notify_change),
    CG_BIND("on_count_change",  on_count_change),
    CG_BIND("on_richtext_change", on_richtext_change),
    CG_BIND("on_date_change",   on_date_change),
    CG_BIND("on_time_change",   on_time_change),
    CG_BIND("on_datetime_change", on_datetime_change),
    CG_BIND("on_p1_click",      on_p1_click),
    CG_BIND("on_p1_change",     on_p1_change),
    CG_BIND("on_p2_check",      on_p2_check),
    /* 链接 / 图标按钮 */
    CG_BIND("on_link_click",    on_link_click),
    CG_BIND("on_icon_click",    on_icon_click),
    CG_BIND_END
};

/* ------------------------------------------------------------ 入口 */

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready = on_ready;
    desc.ready_user = &g_biz;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "gallery.cgui";
        return cg_app_selftest(&desc);
    }

    desc.cgui_path = (argc > 1) ? argv[1] : "gallery.cgui";
    return cg_app_run(&desc);
}
