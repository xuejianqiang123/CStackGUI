/**
 * @file    main.c
 * @brief   入门 05（综合练习）：待办清单 —— 把前四课串成一个小工具
 * @details 用到的都是前四课学过的东西，只是凑到一起了：
 *
 *            入门 01  编辑框 + 按钮 + 共用回调
 *            入门 02  控件状态的读与写
 *            入门 03  窗口事件（ready / close 拦截）
 *            入门 04  列表框的增删
 *            这一课   再加「菜单」和「文件存取」——菜单用 cg_menu_last_id() 分发，
 *                     文件与配置全走 cg_kit（cg_file_read / cg_file_write / cg_ini_*），
 *                     **全程没有 include 任何 Windows 头**。
 *
 *          两个新手最容易踩的点，这里都有实例：
 *            1. 列表框**没有"改一项 / 删一项"的 API**（只有 add / clear / count / item），
 *               所以改内容只能整表重建 —— 见 list_rebuild()，顺便练习堆内存的借还；
 *            2. "改过还没存"这种状态要自己维护（dirty 标志）：用来标标题、拦关闭。
 *
 * 界面在 lesson05_todo.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson05_todo
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

#define DONE_PREFIX "[x] "      /**< 完成标记：列表项前面加这个前缀就算做完了 */
#define MAX_ITEMS   512         /**< 重建列表时的临时数组上限（够教学用了） */

typedef struct {
    char path[1024];   /**< 当前文件；空串 = 还没保存过 */
    char cfg[1200];    /**< 配置文件：<用户数据目录>\lesson05_todo.ini */
    int  dirty;        /**< 改过还没保存 */
} biz_t;

static biz_t s_biz;

/* ------------------------------------------------------------ 界面刷新 */

/** @brief 把项数 / 文件名 / 是否改过刷到状态栏上 */
static void refresh(cg_app_t *app)
{
    cg_widget_t *sb  = cg_find(app, "sb_main");
    cg_widget_t *lst = cg_find(app, "lst_todo");

    cg_status_setf(sb, 0, "%d 项", cg_widget_list_count(lst));
    cg_status_set(sb, 1, s_biz.path[0] ? cg_path_name(s_biz.path) : "未命名");
    cg_status_set(sb, 2, s_biz.dirty ? "未保存（Ctrl+S 无效，用菜单「保存」）" : "已保存");
}

/**
 * @brief 整表重建：跳过 skip 那一项，或把 replace_idx 那一项换成 replace_text
 * @param app 应用
 * @param skip 要删掉的下标；-1 = 不删
 * @param replace_idx 要替换的下标；-1 = 不替换
 * @param replace_text 替换成什么；NULL = 不替换
 * @note 列表框没有"改一项"的 API，只能：先复制出全部文字，清空，再按需要加回去。
 *       复制出来的都是堆串（cg_str_format 的产物），最后必须逐个 cg_free。
 */
static void list_rebuild(cg_app_t *app, int skip, int replace_idx, const char *replace_text)
{
    cg_widget_t *lst = cg_find(app, "lst_todo");
    char        *copy[MAX_ITEMS];
    int          n = cg_widget_list_count(lst);
    int          i;

    if (n > MAX_ITEMS) n = MAX_ITEMS;

    for (i = 0; i < n; i++) {
        const char *item = cg_widget_list_item(lst, i);
        copy[i] = cg_str_format("%s", item ? item : "");
    }

    cg_widget_list_clear(lst);
    for (i = 0; i < n; i++) {
        if (i == skip) continue;
        cg_widget_list_add(lst, (i == replace_idx && replace_text) ? replace_text : copy[i]);
    }

    for (i = 0; i < n; i++) cg_free(copy[i]);   /* 借的堆内存全部还回去 */
}

/* ------------------------------------------------------------ 增 / 改 / 删 */

/** @brief 加一条待办（尾部空白会去掉；空的不加） */
static void add_item(cg_app_t *app, const char *text)
{
    cg_widget_t *lst  = cg_find(app, "lst_todo");
    char        *trim = cg_str_trim(text);

    if (!trim || !*trim) {
        cg_free(trim);
        if (cg_app_has_window(app)) {      /* 自检时不要弹框，会卡住 */
            cg_message_box(cg_find(app, "edt_new"), "先写点什么再添加吧。", "提示", 0);
        }
        return;
    }
    cg_widget_list_add(lst, trim);
    cg_widget_list_scroll_bottom(lst);
    cg_log(CG_LOG_INFO, "新增待办：%s", trim);
    cg_free(trim);

    cg_widget_set_text(cg_find(app, "edt_new"), "");
    s_biz.dirty = 1;
    refresh(app);
}

/** @brief 给选中的那条加/去「[x] 」前缀 */
static void toggle_done(cg_app_t *app)
{
    cg_widget_t *lst = cg_find(app, "lst_todo");
    int          idx = cg_widget_value(lst);          /* 列表框：value 就是选中第几项 */
    const char  *item = cg_widget_list_item(lst, idx);
    char        *next = NULL;

    if (idx < 0 || !item) {
        if (cg_app_has_window(app)) {
            cg_message_box(lst, "先在列表里点中一条。", "提示", 0);
        }
        return;
    }

    if (strncmp(item, DONE_PREFIX, strlen(DONE_PREFIX)) == 0) {
        next = cg_str_format("%s", item + strlen(DONE_PREFIX));   /* 去掉前缀 = 取消完成 */
    } else {
        next = cg_str_format("%s%s", DONE_PREFIX, item);          /* 加前缀 = 标成完成 */
    }
    list_rebuild(app, -1, idx, next);
    cg_free(next);
    s_biz.dirty = 1;
    refresh(app);
}

/* ------------------------------------------------------------ 文件读写 */

/** @brief 把列表里的每一项拼成"一行一条"的文本 */
static char *items_to_text(cg_widget_t *lst)
{
    int   n = cg_widget_list_count(lst);
    char *text = cg_str_format("");     /* 起一个空串，后面一行一行接上去 */

    for (int i = 0; i < n; i++) {
        const char *item = cg_widget_list_item(lst, i);
        char       *next = cg_str_format("%s%s\n", text ? text : "", item ? item : "");
        cg_free(text);
        text = next;
    }
    return text;    /* 返回堆串，调用方负责 cg_free */
}

/** @brief 存到指定文件，并记住它 */
static int save_to(cg_app_t *app, const char *path)
{
    cg_widget_t *lst = cg_find(app, "lst_todo");
    char        *text = items_to_text(lst);
    int          rc;

    if (!text) return -1;
    rc = cg_file_write(path, text);
    cg_free(text);
    if (rc != 0) {
        cg_log(CG_LOG_ERROR, "保存失败：%s", path);
        if (cg_app_has_window(app)) {
            cg_message_box(lst, "写不进去（可能没有权限或被占用）。", "保存失败", 2);
        }
        return -1;
    }
    snprintf(s_biz.path, sizeof(s_biz.path), "%s", path);
    s_biz.dirty = 0;
    if (s_biz.cfg[0]) cg_ini_write(s_biz.cfg, "文件", "上次", path);   /* 记住它，下次自动打开 */
    cg_log(CG_LOG_INFO, "已保存 %d 项到 %s", cg_widget_list_count(lst), path);
    refresh(app);
    return 0;
}

/** @brief 打开一个纯文本文件：一行一条 */
static void load_from(cg_app_t *app, const char *path)
{
    cg_widget_t *lst = cg_find(app, "lst_todo");
    char        *raw = cg_file_read(path);
    const char  *p;
    char         line[512];
    int          count = 0;

    if (!raw) {
        if (cg_app_has_window(app)) {
            cg_message_box(lst, "读不到这个文件（路径不对或被占用）。", "打开失败", 1);
        }
        return;
    }

    cg_widget_list_clear(lst);
    p = raw;
    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '\n' && len + 1 < sizeof(line)) len++;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';   /* 老 Windows 文本是 \r\n */
        if (line[0]) {
            cg_widget_list_add(lst, line);
            count++;
        }
        while (*p && *p != '\n') p++;      /* 跳过这一行的剩余部分 */
        if (*p == '\n') p++;
    }
    cg_free(raw);

    snprintf(s_biz.path, sizeof(s_biz.path), "%s", path);
    s_biz.dirty = 0;
    if (s_biz.cfg[0]) cg_ini_write(s_biz.cfg, "文件", "上次", path);
    cg_log(CG_LOG_INFO, "已打开 %s（%d 项）", path, count);
    refresh(app);
}

/** @brief 「保存」：没有文件名就先弹另存为 */
static void save_ask(cg_app_t *app, cg_widget_t *self, int force_dialog)
{
    char        path[1024];
    const char *filter = "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*";

    if (!force_dialog && s_biz.path[0]) {
        save_to(app, s_biz.path);
        return;
    }
    path[0] = '\0';
    if (cg_save_file_dialog(self, "保存待办清单", filter, "txt", path, sizeof(path))) {
        save_to(app, path);
    }
}

/* ------------------------------------------------------------ 事件回调 */

/** @brief 菜单：一个回调分发全部菜单项 */
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id = cg_menu_last_id(self);   /* .cgui 里菜单项的 id；自检时是 NULL */

    (void)ev;
    (void)user;
    if (!id) return;

    if (cg_str_equal(id, "m_open")) {
        char path[1024];
        path[0] = '\0';
        if (cg_open_file_dialog(self, "打开待办清单",
                                "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                                path, sizeof(path))) {
            load_from(app, path);
        }
    } else if (cg_str_equal(id, "m_save")) {
        save_ask(app, self, 0);
    } else if (cg_str_equal(id, "m_save_as")) {
        save_ask(app, self, 1);
    } else if (cg_str_equal(id, "m_about")) {
        char *text = cg_str_format("%s\n版本 %s\n\n%s",
                                   cg_app_info(app, "product"),
                                   cg_app_info(app, "version"),
                                   cg_app_info(app, "desc"));
        cg_message_box(self, text, "关于", 0);
        cg_free(text);
    } else if (cg_str_equal(id, "m_quit")) {
        if (s_biz.dirty) {        /* 有没保存的就先别退 */
            cg_message_box(self, "还有没保存的改动。\n先用「文件 → 保存」存一下。", "提示", 1);
            return;
        }
        cg_quit(app);
    }
}

/** @brief 「添加」按钮 */
static void on_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    add_item(cg_app_of(self), cg_widget_text(cg_find(cg_app_of(self), "edt_new")));
}

/** @brief 在输入框里按回车 == 点「添加」 */
static void on_add_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    add_item(cg_app_of(self), cg_widget_text(self));
}

/** @brief 「完成/取消」按钮 */
static void on_done_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    toggle_done(cg_app_of(self));
}

/** @brief 双击列表项 = 删掉它 */
static void on_list_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    int         idx = cg_widget_value(self);
    const char *item = cg_widget_list_item(self, idx);

    (void)ev;
    (void)user;
    if (idx < 0 || !item) return;

    cg_log(CG_LOG_INFO, "删除待办：%s", item);
    list_rebuild(app, idx, -1, NULL);      /* skip = idx，等于删掉它 */
    s_biz.dirty = 1;
    refresh(app);
}

/** @brief 界面就绪：恢复上次打开的文件（配置存在 <用户数据目录>\lesson05_todo.ini） */
static void on_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     *dir;
    char     *cfg;

    (void)ev;
    (void)user;

    dir = cg_app_data_dir();                        /* %LOCALAPPDATA%\lesson05_todo，会自动建 */
    cfg = dir ? cg_path_join(dir, "lesson05_todo.ini") : NULL;
    if (cfg) snprintf(s_biz.cfg, sizeof(s_biz.cfg), "%s", cfg);
    cg_free(cfg);
    cg_free(dir);

    if (s_biz.cfg[0]) {
        char *last = cg_ini_read(s_biz.cfg, "文件", "上次", NULL);
        if (last && *last && cg_file_exists(last)) {
            load_from(app, last);                   /* 上次那份还在，直接接上 */
        }
        cg_free(last);
    }
    refresh(app);
    cg_log(CG_LOG_INFO, "待办清单启动，配置文件：%s", s_biz.cfg);
}

/** @brief 关闭前：改过没存就拦一次 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    if (!s_biz.dirty) return;

    if (cg_app_has_window(app)) {
        cg_message_box(cg_find(app, "lst_todo"),
                       "还有没保存的改动。\n\n先用「文件 → 保存」存一下，再关闭。",
                       "还没保存", 1);
    }
    cg_cancel_close(app);       /* 这一次不关 */
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_menu_click",    on_menu_click),
    CG_BIND("on_add_click",     on_add_click),
    CG_BIND("on_add_submit",    on_add_submit),
    CG_BIND("on_done_click",    on_done_click),
    CG_BIND("on_list_dblclick", on_list_dblclick),
    CG_BIND("on_ready",         on_ready),
    CG_BIND("on_close",         on_close),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson05_todo.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson05_todo.cgui";
    return cg_app_run(&desc);
}
