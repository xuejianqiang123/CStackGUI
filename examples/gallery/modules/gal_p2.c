/**
 * @file    gal_p2.c
 * @brief   gallery 页 2「新特性」实现：时钟/通用对话框/画板/取色/表格勾选多选/评分/分页/焦点/图标列表
 */
#include "gal_p2.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 把格式化后的一句话写进页 2 的状态标签
 * @param app 所属应用
 * @param fmt printf 风格格式串
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
    gal_log_line(app, "iconlist 就绪：%d 行（3 行来自 .cgui items + 3 行运行时 add）",
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
    gal_log_line(cg_app_of(self), "iconlist select: row=%d / 共 %d 行", row, cg_iconlist_count(self));
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
    gal_log_line(cg_app_of(self), "iconlist check: 勾选 %d 行", chk);
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
    gal_log_line(cg_app_of(self), "iconlist dblclick: row=%d", row);
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

/**
 * @brief 页 2 的初始状态：给画板打底稿、给表格挂行数值与初始勾选、补图标列表数据
 * @param app 所属应用
 * @param user 就绪钩子的业务数据（本回调不用）
 * @return void 无返回值
 */
void gal_p2_ready(cg_app_t *app, void *user)
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
    il_ready(app);
}

/** 页 2 的事件绑定表（右边这些名字写在 gallery.cgui 里） */
const cg_binding_t gal_p2_bindings[] = {
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
    CG_BIND_END
};
