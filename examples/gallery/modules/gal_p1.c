/**
 * @file    gal_p1.c
 * @brief   gallery 页 1「控件陈列」实现：菜单/工具栏/开关/微调/日期/树/表格/月历等回调
 */
#include "gal_p1.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 在状态栏第一段显示业务概况
 * @param app 所属应用
 * @param biz 业务状态结构
 * @return void 无返回值
 * @note 状态栏第 2 段留给 gal_status() 写最近一次操作
 */
static void refresh_status_summary(cg_app_t *app, biz_t *biz)
{
    cg_widget_t *sb = cg_find(app, "sb_main");
    if (!sb || !biz) return;
    cg_status_setf(sb, 0, "开关 %s · 数量 %d · 树 +%d · 表 +%d",
                   biz->notify ? "开" : "关", biz->count, biz->tree_added, biz->rows_added);
}

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
    gal_status(app, "菜单：%s（id=%s）", text ? text : "?", id ? id : "无");

    if (id && strcmp(id, "m_quit") == 0) {
        cg_quit(app);
    } else if (id && strcmp(id, "m_new") == 0) {
        cg_widget_set_text(cg_find(app, "rt_notes"), "");
        cg_table_clear(cg_find(app, "tbl_data"));
        gal_status(app, "已新建（清空了富文本和表格）");
    } else if (id && strcmp(id, "m_about") == 0) {
        /* 模态：打开期间主窗口被挡住，返回的就是"确定=1 / 取消=0" */
        int r = cg_dialog_open(cg_find(app, "dlg_about"));
        gal_status(app, "对话框关了（result=%d）", r);
    } else if (id && strcmp(id, "m_about_nm") == 0) {
        /* 非模态：立即返回，主窗口照常能用，对话框活着等用户自己关 */
        cg_dialog_open_nonmodal(cg_find(app, "dlg_about"));
        gal_status(app, "已开非模态对话框（主窗口仍可用）");
    }
}

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
    gal_status(app, "弹出菜单项：%s", cg_menu_last_text(self) ? cg_menu_last_text(self) : "?");
}

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
    gal_status(app, "工具栏第 %d 个按钮：%s", index + 1, label ? label : "?");
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
    gal_status(app, "富文本改了（未保存），现在 %d 个字符", (int)strlen(cg_widget_text(self)));
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
    gal_status(app, "日期 -> %s", cg_widget_text(self));
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
    gal_status(app, "时间 -> %s", cg_widget_text(self));
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
    gal_status(app, "日期+时间 -> %s", cg_widget_text(self));
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
    gal_status(app, "开关 -> %s", cg_widget_checked(self) ? "开" : "关");
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
    gal_status(app, "微调 -> %d", cg_widget_value(self));
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
    gal_status(app, "选中树节点 %d: %s", cg_tree_selected(self), text ? text : "-");
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
    gal_status(app, "双击了树节点：%s", cg_tree_selected_text(self) ? cg_tree_selected_text(self) : "-");
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
    gal_status(app, "树现在有 %d 个节点", cg_tree_count(tree));
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
    gal_status(app, "表格现在有 %d 行", cg_table_row_count(tbl));
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
    gal_status(app, "切到第 %d 页（共 %d 页）", cg_tabs_selected(self) + 1, cg_tabs_count(self));
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
        gal_status(app, "表格选中第 %d 行：%s / %s", row + 1,
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
    gal_status(app, "第一页输入框里是：%s",
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
    gal_status(app, "第一页输入框改了：%s", cg_widget_text(self));
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
    gal_status(app, "第二页复选框 -> %s", cg_widget_checked(self) ? "勾上" : "取消");
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
    gal_status(app, "展开：%s（第 %d 个节点）",
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
    gal_status(app, "折叠：%s",
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
    gal_status(app, "点的是链接：%s", cg_widget_text(self));
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
    gal_status(app, "图标按钮被点了（src = %s）", cg_widget_image(self));
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

    gal_status(app, "月历选中 %s", cg_widget_text(self));
    /* 月历和日期控件都规规矩矩把值放在 text 里，所以两边可以直接互通 */
    if (dt) cg_widget_set_text(dt, cg_widget_text(self));
}

/**
 * @brief 页 1 的初始状态：挂业务数据、联动圆环、给树 / 表格补内容
 * @param app 所属应用
 * @param biz 业务状态结构，可为 NULL；非空时把开关置开、数量置 5
 * @return void 无返回值
 */
void gal_p1_ready(cg_app_t *app, biz_t *biz)
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
    gal_status(app, "就绪，图片 %s", cg_widget_image(cg_find(app, "img_logo")));
    refresh_status_summary(app, biz);
}

/** 页 1 的事件绑定表（右边这些名字写在 gallery.cgui 里） */
const cg_binding_t gal_p1_bindings[] = {
    CG_BIND("on_menu_click",      on_menu_click),
    CG_BIND("on_more_click",      on_more_click),
    CG_BIND("on_toolbar_click",   on_toolbar_click),
    CG_BIND("on_tree_select",     on_tree_select),
    CG_BIND("on_tree_dblclick",   on_tree_dblclick),
    CG_BIND("on_tree_expand",     on_tree_expand),
    CG_BIND("on_tree_collapse",   on_tree_collapse),
    CG_BIND("on_tab_change",      on_tab_change),
    CG_BIND("on_table_select",    on_table_select),
    CG_BIND("on_cal_select",      on_cal_select),
    CG_BIND("on_add_node",        on_add_node),
    CG_BIND("on_add_row",         on_add_row),
    CG_BIND("on_notify_change",   on_notify_change),
    CG_BIND("on_count_change",    on_count_change),
    CG_BIND("on_richtext_change", on_richtext_change),
    CG_BIND("on_date_change",     on_date_change),
    CG_BIND("on_time_change",     on_time_change),
    CG_BIND("on_datetime_change", on_datetime_change),
    CG_BIND("on_p1_click",        on_p1_click),
    CG_BIND("on_p1_change",       on_p1_change),
    CG_BIND("on_p2_check",        on_p2_check),
    CG_BIND("on_link_click",      on_link_click),
    CG_BIND("on_icon_click",      on_icon_click),
    CG_BIND_END
};
