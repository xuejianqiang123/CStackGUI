/**
 * @file    main.c
 * @brief   记事本示例：一个「真能用的工具」该怎么写
 * @details gallery 是控件陈列室（讲"控件怎么用"），这个示例讲另一件事：
 *          **一个工具的业务该怎么组织**——菜单分发、状态栏刷新、脏标记、
 *          文件读写、编码转换（UTF-8 / GBK）、配置持久化、关闭前拦截、日志。
 *
 *          它也是 cg_kit（基础能力库）的活例子：读文件、写文件、INI 配置、
 *          路径拼接、剪贴板、GBK 转换全都用到了，而且**没有 include 任何 Windows 头**。
 *
 *          「文件」菜单的四条命令（新建 / 打开 / 保存 / 另存为）用**动作（actions）**实现：
 *          `.cgui` 里一处写清「文字 + 快捷键 + 回调名」，菜单项只写 "action" 引用它们。
 *          快捷键由消息循环里的加速键表处理，所以焦点在输入框里按 Ctrl+S 也能保存。
 *
 * 界面在 notepad.cgui 里；本文件只有行为和「启动时该干什么」。
 * 编译：在项目根跑 build-app.bat notepad
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态
 * 一个工具的状态就这么点东西：当前文件、有没有改过、配置文件在哪。
 * 全放静态结构里，回调需要时随时拿（示例用全局，真实项目也可以挂到控件 user 上）。
 */
typedef struct {
    char path[1024];   /**< 当前文件路径；空串 = 未命名（还没存过） */
    char cfg[1200];    /**< 配置文件路径：<用户数据目录>\notepad.ini */
    int  dirty;        /**< 内容改过还没保存 */
    int  win_w, win_h; /**< 最近一次窗口尺寸（size 事件里记，关闭时写进配置） */
} biz_t;

static biz_t s_biz;

/**
 * @brief 把字符串当十进制整数读（不引 stdlib.h，业务代码只用框架接口与标准 C 字符串）
 * @param s 待解析的字符串，可有前导空白，读到第一个非数字就停；传 NULL 按空串处理
 * @return int 解析出的十进制值，一个数字都没有时是 0
 */
static int to_int(const char *s)
{
    int v = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

/**
 * @brief 这段字节是不是合法 UTF-8（拿来判断"老 GBK 文件"）
 * @param s 待检查的字节串，以 '\0' 结尾，所有权不转移
 * @return int 非 0 = 整串都是合法 UTF-8，0 = 出现非法序列（调用方按 GBK 处理）
 */
static int looks_like_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p) {
        if (*p < 0x80) { p++; continue; }
        if ((*p & 0xE0) == 0xC0) {
            if ((p[1] & 0xC0) != 0x80) return 0;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
            p += 3;
        } else {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 统计字数（按 UTF-8 字符）与行数
 * @param t 要统计的文本，传 NULL 按空串处理
 * @param chars 出参：写入字数，续字节不算一个字
 * @param lines 出参：写入行数，空文本算 1 行
 * @return void 无返回值
 */
static void count_text(const char *t, int *chars, int *lines)
{
    const unsigned char *p = (const unsigned char *)(t ? t : "");
    int c = 0, l = 1;

    for (; *p; p++) {
        if (*p == '\n') l++;
        if ((*p & 0xC0) != 0x80) c++;   /* 续字节不算一个字 */
    }
    *chars = c;
    *lines = l;
}

/**
 * @brief 刷新标题与状态栏（任何改动之后都要调一次）
 * @param app 应用
 * @return void 无返回值
 */
static void refresh(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");
    cg_widget_t *edt = cg_find(app, "edt_body");
    const char  *text = edt ? cg_widget_text(edt) : "";
    const char  *name = s_biz.path[0] ? cg_path_name(s_biz.path) : "未命名";
    int          chars = 0, lines = 0;

    count_text(text, &chars, &lines);
    {
        /* cg_str_format 返回的是堆串，用完必须还回去 —— 别直接塞进 cg_set_title */
        char *title = cg_str_format("%s%s - 记事本", name, s_biz.dirty ? " *" : "");
        cg_set_title(app, title);
        cg_free(title);
    }
    if (sb) {
        cg_status_set(sb, 0, s_biz.path[0] ? s_biz.path : "未命名（还没保存过）");
        cg_status_setf(sb, 1, "%d 字", chars);
        cg_status_setf(sb, 2, "行 %d", lines);
    }
}

/**
 * @brief 把编辑框内容写进文件并记住当前路径（保存 / 另存为共用）
 * @param app 应用
 * @param path 目标文件路径，写成功后它同时成为"当前文件"
 * @return void 无返回值
 */
static void save_to(cg_app_t *app, const char *path)
{
    cg_widget_t *edt = cg_find(app, "edt_body");
    const char  *text = edt ? cg_widget_text(edt) : "";

    if (cg_file_write(path, text ? text : "") != 0) {
        cg_message_box(edt, "写不进去，路径可能没有权限或被占用。", "保存失败", 2);
        cg_log(CG_LOG_ERROR, "保存失败：%s", path);
        return;
    }
    snprintf(s_biz.path, sizeof(s_biz.path), "%s", path);
    s_biz.dirty = 0;
    if (s_biz.cfg[0]) cg_ini_write(s_biz.cfg, "文件", "上次", path);
    cg_log(CG_LOG_INFO, "已保存 %s", path);
    refresh(app);
}

/**
 * @brief 读文件进编辑框（UTF-8 直接显示；看着像 GBK 就转一下）
 * @param app 应用
 * @param path 要读的文件路径，读成功后它成为"当前文件"
 * @return void 无返回值
 */
static void open_path(cg_app_t *app, const char *path)
{
    cg_widget_t *edt = cg_find(app, "edt_body");
    char        *raw = cg_file_read(path);
    char        *text = raw;

    if (!raw) {
        cg_message_box(edt, "读不到这个文件（可能被占用或路径不对）。", "打开失败", 1);
        return;
    }
    /* 老文件多是 GBK/ANSI：字节不像 UTF-8 就按系统编码转过来，否则中文全是乱码 */
    if (!looks_like_utf8(raw)) {
        char *u8 = cg_gbk_to_utf8(raw);
        if (u8) {
            cg_free(raw);
            text = u8;
            cg_log(CG_LOG_INFO, "%s 不是 UTF-8，已按 GBK 转换", path);
        }
    }
    if (edt) cg_widget_set_text(edt, text);
    snprintf(s_biz.path, sizeof(s_biz.path), "%s", path);
    s_biz.dirty = 0;
    if (s_biz.cfg[0]) cg_ini_write(s_biz.cfg, "文件", "上次", path);
    cg_free(text);
    refresh(app);
}

/**
 * @brief 把窗口尺寸写进配置（只在真有尺寸、有配置文件时才写）
 * @return void 无返回值
 */
static void save_win_size(void)
{
    char *sw, *sh;

    if (!s_biz.cfg[0] || s_biz.win_w <= 0 || s_biz.win_h <= 0) return;
    sw = cg_str_format("%d", s_biz.win_w);
    sh = cg_str_format("%d", s_biz.win_h);
    if (sw && sh) {
        cg_ini_write(s_biz.cfg, "窗口", "宽", sw);
        cg_ini_write(s_biz.cfg, "窗口", "高", sh);
    }
    cg_free(sw);
    cg_free(sh);
}

/**
 * @brief 问路径后保存：有当前路径就直接写盘，否则弹「另存为」
 * @param app 应用
 * @param self 通用对话框的 parent（主窗口伪控件）
 * @param force_dialog 非 0 = 必弹「另存为」（另存为动作）；0 = 有当前路径就直接写
 * @return void 无返回值
 */
static void save_ask(cg_app_t *app, cg_widget_t *self, int force_dialog)
{
    char  path[1024];
    const char *filter = "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*";

    if (!force_dialog && s_biz.path[0]) {
        save_to(app, s_biz.path);
        return;
    }
    path[0] = '\0';
    if (cg_save_file_dialog(self, "保存到", filter, "txt", path, sizeof(path))) {
        save_to(app, path);
    }
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 启动钩子：恢复上次的文件与窗口大小（这些只在这里能做）
 * @param app 应用
 * @param user 未使用
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    char *dir;
    char *cfg;

    (void)user;
    dir = cg_app_data_dir();                       /* %LOCALAPPDATA%\notepad，自动建 */
    cfg = dir ? cg_path_join(dir, "notepad.ini") : NULL;
    if (cfg) snprintf(s_biz.cfg, sizeof(s_biz.cfg), "%s", cfg);
    cg_free(cfg);
    cg_free(dir);

    if (s_biz.cfg[0]) {
        char *w = cg_ini_read(s_biz.cfg, "窗口", "宽", NULL);
        char *h = cg_ini_read(s_biz.cfg, "窗口", "高", NULL);
        s_biz.win_w = to_int(w);
        s_biz.win_h = to_int(h);
        if (s_biz.win_w >= 320 && s_biz.win_h >= 200) {
            cg_win_set_size(app, s_biz.win_w, s_biz.win_h);
        }
        cg_free(w);
        cg_free(h);
    }

    /* 上次编辑的文件还在的话，直接接上 */
    if (s_biz.cfg[0]) {
        char *last = cg_ini_read(s_biz.cfg, "文件", "上次", NULL);
        if (last && *last && cg_file_exists(last)) open_path(app, last);
        cg_free(last);
    }
    refresh(app);
}

/**
 * @brief 菜单点击：分发「文件」菜单以外的菜单项
 * @param self 菜单控件（菜单栏 / 菜单按钮通用）
 * @param ev 事件（总是 click）
 * @param user 未使用
 * @return void 无返回值
 * @details 「文件」菜单的新建 / 打开 / 保存 / 另存为已改成动作，点击走动作回调，
 *          不在这里 —— 见下面的「动作回调」一节
 */
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id = cg_menu_last_id(self);
    cg_widget_t *edt = cg_find(app, "edt_body");

    (void)ev;
    (void)user;
    if (!id) return;            /* 自检里会合成一次 click，那时没有"刚点的项" */

    if (strcmp(id, "m_copy") == 0) {
        cg_clip_set_text(edt ? cg_widget_text(edt) : "");
        cg_status_set(cg_find(app, "sb_main"), 0, "已复制全部内容到剪贴板");
    } else if (strcmp(id, "m_paste") == 0) {
        char *clip = cg_clip_get_text();
        if (edt && clip) {
            /* cg_str_format 返回的是**堆内存**，而 cg_widget_set_text 只是拷贝一份
             * （不接管所有权）—— 所以必须先落到变量、用完 cg_free，
             * 直接写进 set_text 就是每次粘贴泄漏一块。 */
            char *merged = cg_str_format("%s%s", cg_widget_text(edt), clip);
            cg_widget_set_text(edt, merged);
            cg_free(merged);
            s_biz.dirty = 1;
        }
        cg_free(clip);
        refresh(app);
    } else if (strcmp(id, "m_clear") == 0) {
        if (edt) cg_widget_set_text(edt, "");
        s_biz.dirty = 1;
        refresh(app);
    } else if (strcmp(id, "m_about") == 0) {
        char *text = cg_str_format("%s\n版本 %s\n\n%s\n\n本程序由 CStackGUI 画布设计。",
                                   cg_app_info(app, "product"),
                                   cg_app_info(app, "version"),
                                   cg_app_info(app, "desc"));
        cg_message_box(self, text, "关于", 0);
        cg_free(text);
    } else if (strcmp(id, "m_quit") == 0) {
        cg_quit(app);
    }
}

/**
 * @brief 内容变了：记脏标记 + 刷新字数
 * @param self 编辑框
 * @param ev 事件（change）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_body_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_biz.dirty = 1;
    refresh(cg_app_of(self));
}

/**
 * @brief 窗口尺寸变了：先记在内存里（别在拖动中反复写盘）
 * @param self 窗口伪控件
 * @param ev 事件（size）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_window_size(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    s_biz.win_w = cg_event_x(self);         /* size 事件：x = 宽，y = 高 */
    s_biz.win_h = cg_event_y(self);
}

/**
 * @brief 关闭前：有未保存内容就拦一次，顺便把窗口尺寸存进配置
 * @param self 窗口伪控件
 * @param ev 事件（close）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_close(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    save_win_size();                        /* 关窗是写配置的好时机，不是每帧 */
    if (!s_biz.dirty) return;               /* 没改过就直接关 */
    cg_message_box(cg_find(app, "edt_body"),
                   "内容还没保存。\n\n请先用「文件 → 保存」保存，再关闭。",
                   "还有未保存的改动", 1);
    cg_cancel_close(app);                   /* 这次不关了 */
}

/* ------------------------------------------------------------ 动作回调
 * 签名与事件回调完全一样，但有两个固定值：self 恒为**主窗口伪控件**（不是菜单项），
 * ev 恒为 click。所以拿 app 用 cg_app_of(self)，拿控件用 cg_find()；
 * 一个 handler 挂了多个动作时，用 cg_action_name(self) 问是哪一个。
 */

/**
 * @brief 动作 new：新建（有未保存内容就拦一下）
 * @param self 主窗口伪控件
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_file_new(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *edt = cg_find(app, "edt_body");

    (void)ev;
    (void)user;
    if (s_biz.dirty) {
        cg_message_box(self, "当前内容还没保存。先保存，再新建。", "提示", 1);
        return;
    }
    if (edt) cg_widget_set_text(edt, "");
    s_biz.path[0] = '\0';
    s_biz.dirty = 0;
    refresh(app);
}

/**
 * @brief 动作 open：选文件后读进来
 * @param self 主窗口伪控件（可直接当通用对话框的 parent）
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_file_open(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;
    path[0] = '\0';
    if (cg_open_file_dialog(self, "打开", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                           path, sizeof(path))) {
        open_path(app, path);
    }
}

/**
 * @brief 动作 save / save_as：**同一个 handler**，靠"最近激活的动作名"区分是哪个动作
 * @param self 主窗口伪控件
 * @param ev 恒为 click
 * @param user 未使用
 * @return void 无返回值
 * @details 保存 = 有当前路径就直接写盘；另存为 = 强制弹路径对话框
 */
static void on_file_save(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *name = cg_action_name(self);
    cg_app_t   *app  = cg_app_of(self);
    int         force_dialog = (name && strcmp(name, "save_as") == 0);

    (void)ev;
    (void)user;
    save_ask(app, self, force_dialog);
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_menu_click",   on_menu_click),
    CG_BIND("on_body_change",  on_body_change),
    CG_BIND("on_window_size",  on_window_size),
    CG_BIND("on_close",        on_close),
    CG_BIND("on_file_new",     on_file_new),
    CG_BIND("on_file_open",    on_file_open),
    CG_BIND("on_file_save",    on_file_save),
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
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;      /* 双击 exe 也能跑：自动在 exe 旁边找 .cgui */
    desc.on_ready = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "notepad.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "notepad.cgui";
    cg_log(CG_LOG_INFO, "记事本启动，界面文件：%s", desc.cgui_path);
    return cg_app_run(&desc);
}
