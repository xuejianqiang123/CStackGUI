/**
 * @file    tb_mytools.c
 * @brief   toolbox 我的工具实现：mytools.ini 槽位读取 + 槽位按钮同步 + 编辑/重载/开目录
 * @details 8 个槽位，配置文件在数据目录的 mytools.ini（[t1]..[t8] 的 name/cmd）。
 *          保存后点「重新载入」槽位按钮立刻生效；空槽位自动置灰 —— 加一个自定义工具
 *          连一行 C 代码都不用碰。
 */
#include "tb_mytools.h"

#include <stdio.h>
#include <string.h>

#include "tb_log.h"
#include "tb_tools.h"

/** 我的工具配置文件名（放用户数据目录） */
#define K_MY_INI "mytools.ini"

/** 我的工具槽位：名称 + 命令（重读 mytools.ini 填充） */
static struct {
    char name[64];
    char cmd[256];
} s_my[TB_MY_MAX];

/** 有槽位的工具数（「共 Y 个」计数用） */
static int s_my_n;

/** 我的工具配置文件全路径（就绪钩子里填一次） */
static char s_myini[1024];

/**
 * @brief 就绪钩子用：定 mytools.ini 全路径（数据目录下；无数据目录时用相对名）
 * @param path 数据目录全路径；NULL 表示拿不到数据目录
 * @return void 无返回值
 */
void tb_mytools_set_path(const char *path)
{
    if (path) {
        char *full = cg_path_join(path, K_MY_INI);
        snprintf(s_myini, sizeof(s_myini), "%s", full ? full : K_MY_INI);
        cg_free(full);
    } else {
        snprintf(s_myini, sizeof(s_myini), K_MY_INI);
    }
}

/**
 * @brief 重读 mytools.ini 并同步 8 个槽位按钮的文字与可用状态
 * @param app 应用
 * @param read_file 1=真读盘（有窗运行时）；0=自检，用默认值不动盘
 * @return void 无返回值
 */
void tb_mytools_reload(cg_app_t *app, int read_file)
{
    int i;

    memset(s_my, 0, sizeof(s_my));
    s_my_n = 0;
    if (read_file) {
        for (i = 0; i < TB_MY_MAX; i++) {
            char sec[8];
            char *name, *cmd;
            snprintf(sec, sizeof(sec), "t%d", i + 1);
            name = cg_ini_read(s_myini, sec, "name", NULL);
            cmd  = cg_ini_read(s_myini, sec, "cmd", NULL);
            if (name && *name && cmd && *cmd) {
                snprintf(s_my[i].name, sizeof(s_my[i].name), "%s", name);
                snprintf(s_my[i].cmd,  sizeof(s_my[i].cmd),  "%s", cmd);
                s_my_n++;
            }
            cg_free(name);
            cg_free(cmd);
        }
    }

    for (i = 0; i < TB_MY_MAX; i++) {
        char        id[16];
        cg_widget_t *btn;
        int         has = s_my[i].name[0] && s_my[i].cmd[0];

        snprintf(id, sizeof(id), "tool_my%d", i + 1);
        btn = cg_find(app, id);
        if (btn) {
            cg_widget_set_text(btn, has ? s_my[i].name : "空槽位");
            cg_widget_set_enabled(btn, has);
        }
    }
}

/**
 * @brief 有配置的槽位数（「共 Y 个」计数用）
 * @return int 槽位数（0..TB_MY_MAX）
 */
int tb_mytools_count(void)
{
    return s_my_n;
}

/**
 * @brief 取某个槽位的名称（空槽位或越界返回空串）
 * @param slot 槽位下标（0 起）
 * @return const char* 名称，永不为 NULL
 */
const char *tb_my_slot_name(int slot)
{
    return (slot >= 0 && slot < TB_MY_MAX) ? s_my[slot].name : "";
}

/**
 * @brief 取某个槽位的启动命令（空槽位或越界返回空串）
 * @param slot 槽位下标（0 起）
 * @return const char* 命令行，永不为 NULL
 */
const char *tb_my_slot_cmd(int slot)
{
    return (slot >= 0 && slot < TB_MY_MAX) ? s_my[slot].cmd : "";
}

/**
 * @brief 「重新载入」：重读 mytools.ini 并刷新槽位按钮
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_my_reload_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过重读 mytools.ini");
        return;
    }
    tb_mytools_reload(app, 1);
    tb_page_count_sync(app);
    tb_status_msg(app, "已载入 %d 个我的工具", s_my_n);
    tb_log_line(app, "重新载入 mytools.ini：%d 个槽位生效", s_my_n);
}

/**
 * @brief 「编辑 mytools.ini」：没有就先写一份带注释的模板，再用记事本打开
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_my_edit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      cmdline[1200];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过编辑 mytools.ini");
        return;
    }
    if (!cg_file_exists(s_myini)) {
        static const char *tpl =
            "[t1]\nname=示例：显示系统版本\ncmd=cmd /k ver\n\n"
            "[t2]\nname=\ncmd=\n\n"
            "; 在 [t3]..[t8] 里照抄两行，改掉 name 和 cmd 即可加工具；空槽位灰着。\n";
        cg_file_write_bytes(s_myini, tpl, (int)strlen(tpl));
        tb_log_line(app, "已生成模板 %s", s_myini);
    }
    snprintf(cmdline, sizeof(cmdline), "notepad \"%s\"", s_myini);
    if (cg_run_process(cmdline, 0) == 0) {
        tb_status_msg(app, "已打开 mytools.ini");
        tb_log_line(app, "编辑 %s", s_myini);
    } else {
        tb_status_msg(app, "打开编辑器失败");
    }
}

/**
 * @brief 打开用户数据目录（mytools.ini 所在处）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_open_data_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char     *dir = cg_app_data_dir();

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过打开数据目录");
        cg_free(dir);
        return;
    }
    if (dir && cg_run_process(dir, 0) == 0) {
        tb_status_msg(app, "已打开数据目录");
        tb_log_line(app, "打开数据目录 %s", dir);
    } else {
        tb_status_msg(app, "打开数据目录失败");
    }
    cg_free(dir);
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_mytools_bindings[] = {
    CG_BIND("on_btn_my_reload_click", on_btn_my_reload_click),
    CG_BIND("on_btn_my_edit_click",   on_btn_my_edit_click),
    CG_BIND("on_btn_open_data_click", on_btn_open_data_click),
    CG_BIND_END
};
