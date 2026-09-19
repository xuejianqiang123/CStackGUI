/* lab.c -- 新控件与新 API 的演练场：时钟 / 悬停提示 / 通用对话框 /
 * 画板 / 颜色选择 / 表格勾选多选 / 评分 / 分页 / 徽章 / 标签片 / 卡片 / 外形框 / 焦点事件。
 *
 * 编译走仓库根的 build-app.bat lab；自检：lab.exe --selftest
 */
#include "cstackgui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_ticks = 0;

static void set_statusf(cg_app_t *app, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    cg_widget_t *lab;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lab = cg_find(app, "lab_status");
    if (lab) cg_widget_set_text(lab, buf);
}

/* 时钟：每秒把计数写回状态栏 */
static void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self; (void)ev; (void)user;
    g_ticks++;
    if (cg_app_has_window(cg_app_of(self))) {
        set_statusf(cg_app_of(self), "时钟走了 %d 秒", g_ticks);
    }
}

/* 通用对话框：自检（无窗口）时什么都不做，避免弹窗卡住 CI */
static void on_pick_file(cg_widget_t *self, cg_event_t ev, void *user)
{
    char path[1024];
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    if (cg_open_file_dialog(self, "选择一个文件",
                            "文本文件|*.txt|图片|*.png;*.jpg|全部|*.*",
                            path, sizeof(path))) {
        set_statusf(cg_app_of(self), "打开: %s", path);
    } else {
        set_statusf(cg_app_of(self), "取消了打开");
    }
}

static void on_pick_save(cg_widget_t *self, cg_event_t ev, void *user)
{
    char path[1024];
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    if (cg_save_file_dialog(self, "保存到哪里", "文本文件|*.txt", "txt",
                            path, sizeof(path))) {
        set_statusf(cg_app_of(self), "保存到: %s", path);
    } else {
        set_statusf(cg_app_of(self), "取消了保存");
    }
}

static void on_show_msg(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    if (!cg_app_has_window(cg_app_of(self))) return;
    cg_message_box(self, "这是运行时的消息框。", "lab", 0);
}

/* 颜色选择：value 就是 0xRRGGBB */
static void on_color_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "选了颜色 #%06X", (unsigned)cg_widget_value(self));
}

/* 表格：勾选 / 多选 / 行数值 */
static void on_table_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "第 %d 行勾选变成 %d（行数值=%d）",
                cg_table_selected(self) >= 0 ? cg_table_selected(self) : 0,
                cg_table_checked(self, cg_table_selected(self) >= 0 ? cg_table_selected(self) : 0),
                cg_table_row_value(self, cg_table_selected(self) >= 0 ? cg_table_selected(self) : 0));
}

static void on_table_select(cg_widget_t *self, cg_event_t ev, void *user)
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
    set_statusf(cg_app_of(self), "选中: %s", names);
}

/* 画板：点一下画一个圆，ready 时先打底稿 */
static void on_canvas_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    cg_canvas_set_color(self, 0x6750A4);
    cg_canvas_ellipse(self, 60, 60, 40, 40, 1);
    cg_canvas_text(self, 12, 110, "点了一下");
}

static void on_rate_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "评分: %d / %d", cg_widget_value(self), 5);
}

static void on_page_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "翻到第 %d 页", cg_widget_value(self) + 1);
}

static void on_tag_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "标签片被点了");
}

static void on_switch_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "卡片开关: %s", cg_widget_checked(self) ? "开" : "关");
}

/* 焦点事件：失焦时校验输入框 */
static void on_focus(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self; (void)ev; (void)user;
    set_statusf(cg_app_of(self), "输入框获得焦点");
}

static void on_blur(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "失焦，内容是 \"%s\"",
                *cg_widget_text(self) ? cg_widget_text(self) : "(空)");
}

static void on_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    set_statusf(cg_app_of(self), "提交: %s", cg_widget_text(self));
}

/* 界面就绪：给画板打底稿、给表格挂行数值 */
static void on_ready(cg_app_t *app, void *user)
{
    cg_widget_t *cv = cg_find(app, "cv_board");
    cg_widget_t *tb = cg_find(app, "tb_users");
    (void)user;

    if (cv) {
        cg_canvas_set_line_width(cv, 2);
        cg_canvas_set_color(cv, 0x333333);
        cg_canvas_line(cv, 10, 10, 260, 10);
        cg_canvas_rect(cv, 10, 24, 80, 40, 0);
        cg_canvas_ellipse(cv, 120, 24, 80, 40, 0);
        cg_canvas_set_color(cv, 0x1A73E8);
        cg_canvas_text(cv, 12, 90, "画板：cg_canvas_* 画的");
    }
    if (tb) {
        cg_table_set_row_value(tb, 0, 101);
        cg_table_set_row_value(tb, 1, 102);
        cg_table_set_row_value(tb, 2, 103);
        cg_table_set_row_value(tb, 3, 104);
        cg_table_set_checked(tb, 0, 1);
    }
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_tick", on_tick),
    CG_BIND("on_pick_file", on_pick_file),
    CG_BIND("on_pick_save", on_pick_save),
    CG_BIND("on_show_msg", on_show_msg),
    CG_BIND("on_color_change", on_color_change),
    CG_BIND("on_table_check", on_table_check),
    CG_BIND("on_table_select", on_table_select),
    CG_BIND("on_canvas_click", on_canvas_click),
    CG_BIND("on_rate_change", on_rate_change),
    CG_BIND("on_page_change", on_page_change),
    CG_BIND("on_tag_click", on_tag_click),
    CG_BIND("on_switch_change", on_switch_change),
    CG_BIND("on_focus", on_focus),
    CG_BIND("on_blur", on_blur),
    CG_BIND("on_submit", on_submit),
    CG_BIND_END,
};

int main(int argc, char **argv)
{
    cg_app_desc_t d;

    memset(&d, 0, sizeof(d));
    d.bindings = k_bindings;
    d.auto_locate_cgui = 1;
    d.on_ready = on_ready;

    /* --selftest 走命令行自检：解析 + 打印控件树 + 合成事件，不开窗口 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        d.cgui_path = (argc > 2) ? argv[2] : "lab.cgui";
        return cg_app_selftest(&d);
    }
    d.cgui_path = (argc > 1) ? argv[1] : "lab.cgui";
    return cg_app_run(&d);
}
