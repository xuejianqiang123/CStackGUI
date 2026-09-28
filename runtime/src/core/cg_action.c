/**
 * @file    cg_action.c
 * @brief   动作层：命名命令（文字 / 快捷键 / 回调名）、快捷键解析、启用与勾选同步
 * @details .cgui 顶层 actions 里一条动作 = 一条命令；菜单项与控件用 "action" 字段按名字
 *          引用它。于是「一个回调、一处启用状态」同时管住所有引用点 —— 不用再给菜单项和
 *          按钮各绑一份回调、各改一次灰不灰。
 *
 *          两个刻意的设计：
 *          1) **不引入注册机制**：handler 就是 C 侧 CG_BIND 表里的名字，走既有的
 *             cg_resolve_binding() 查表，所以"名字写错不编译报错、启动时汇总提示、
 *             --selftest 非 0 退出"这一整套兜底原样复用。
 *          2) **快捷键是表驱动的加速键**，不是窗口级 keydown 事件：EDIT 会吃掉按键，
 *             焦点在输入框里时主窗口过程根本收不到 WM_KEYDOWN，Ctrl+S 就按不出来。
 *             TranslateAccelerator 在消息循环那一层，**与焦点无关**。
 *
 *          快捷键的解析放在 core、结果与平台无关（只有"哪个键 + 什么修饰键"），
 *          映射成 Win32 虚拟键码是后端的事 —— core 不许 include windows.h
 *          （tools\check-layers.mjs 会拦），这样解析表也能脱窗单测。
 *
 *          语义、字段与 v1 边界见 cstackgui.h 的「动作（actions）」一节与
 *          docs/CGUI-FORMAT.md。
 */
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ 快捷键解析 */

/** 具名键表。**同一个 key 的规范名必须排在别名前面** —— cg_accel_format 取第一个匹配
 *  当显示形式（"Esc" 而不是 "escape"）。 */
static const struct {
    const char *name;
    int         key;
} k_named[] = {
    { "Esc",      CG_ACCEL_ESC },
    { "escape",   CG_ACCEL_ESC },
    { "Enter",    CG_ACCEL_ENTER },
    { "return",   CG_ACCEL_ENTER },
    { "Tab",      CG_ACCEL_TAB },
    { "Space",    CG_ACCEL_SPACE },
    { "Back",     CG_ACCEL_BACK },
    { "backspace",CG_ACCEL_BACK },
    { "Delete",   CG_ACCEL_DELETE },
    { "del",      CG_ACCEL_DELETE },
    { "Insert",   CG_ACCEL_INSERT },
    { "ins",      CG_ACCEL_INSERT },
    { "Home",     CG_ACCEL_HOME },
    { "End",      CG_ACCEL_END },
    { "PageUp",   CG_ACCEL_PAGEUP },
    { "PageDown", CG_ACCEL_PAGEDOWN },
    { "Left",     CG_ACCEL_LEFT },
    { "Right",    CG_ACCEL_RIGHT },
    { "Up",       CG_ACCEL_UP },
    { "Down",     CG_ACCEL_DOWN },
};

/**
 * @brief 不区分大小写的整串比较（"Ctrl" / "ctrl" / "CTRL" 都认）
 * @param a 参与比较的字符串；NULL 视为不相等
 * @param b 参与比较的字符串；NULL 视为不相等
 * @return int 1 = 忽略大小写完全相同，0 = 不同
 */
static int eqi(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @brief 去掉首尾空白，返回指向缓冲内部的起点
 * @param buf 就地修改的缓冲（末尾空白被写成 '\0'）
 * @return char* 指向去空白后的起点（可能是 buf 本身）
 */
static char *trim(char *buf)
{
    char *e;
    while (*buf == ' ' || *buf == '\t') buf++;
    e = buf + strlen(buf);
    while (e > buf && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    return buf;
}

/**
 * @brief 解析键名（最后一段）：字母 / 数字 / F1-F24 / 具名键
 * @param tok 已去空白的键名
 * @return int 键值（快捷键键枚举之一）；不认识返回 CG_ACCEL_NONE
 */
static int accel_key_of(const char *tok)
{
    size_t n, i;

    if (!tok || !*tok) return CG_ACCEL_NONE;
    n = strlen(tok);

    if (n == 1) {
        char c = tok[0];
        if (c >= 'a' && c <= 'z') return CG_ACCEL_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return CG_ACCEL_A + (c - 'A');
        if (c >= '0' && c <= '9') return CG_ACCEL_0 + (c - '0');
        return CG_ACCEL_NONE;
    }
    if ((tok[0] == 'f' || tok[0] == 'F') && n >= 2 && n <= 3) {
        int num = 0;
        for (i = 1; i < n; i++) {
            if (tok[i] < '0' || tok[i] > '9') return CG_ACCEL_NONE;
            num = num * 10 + (tok[i] - '0');
        }
        if (num >= 1 && num <= 24) return CG_ACCEL_F1 + (num - 1);
        return CG_ACCEL_NONE;
    }
    for (i = 0; i < sizeof(k_named) / sizeof(k_named[0]); i++) {
        if (eqi(tok, k_named[i].name)) return k_named[i].key;
    }
    return CG_ACCEL_NONE;
}

/**
 * @brief 这个键是不是 F1-F24
 * @param key 键值
 * @return int 1 = F1-F24，0 = 其它
 * @note F 键是**唯一允许不带修饰键**的：裸 Esc / Delete / 字母都会被它抢走输入框的按键
 */
static int accel_is_function_key(int key)
{
    return key >= CG_ACCEL_F1 && key < CG_ACCEL_F1 + 24;
}

/**
 * @brief 解析快捷键文本
 * @param accel 形如 "Ctrl+S" / "Shift+F5" / "Ctrl+Alt+Delete"；NULL / 空 = 失败
 * @param out 成功时写出与平台无关的键与修饰键
 * @return int 1 = 解析成功；0 = 语法 / 键名 / 组合不合法
 */
int cg_accel_parse(const char *accel, cg_accel_t *out)
{
    char   buf[128];
    char  *p;
    unsigned mods = 0;
    int    key = CG_ACCEL_NONE;
    size_t n;

    if (!accel || !*accel) return 0;
    n = strlen(accel);
    if (n >= sizeof(buf)) return 0;
    memcpy(buf, accel, n + 1);

    /* 按 '+' 切段：**除最后一段外每一段都必须是修饰键**，最后一段是键本身。
     * 这么切一次就把"任意顺序、大小写不敏感、重复算一次"全解决了。 */
    p = buf;
    while (p && *p) {
        char *plus = strchr(p, '+');
        char *tok;
        if (plus) *plus = '\0';
        tok = trim(p);
        if (!*tok) return 0;                 /* "Ctrl++S" 这种空段 */
        if (plus) {
            if (eqi(tok, "ctrl") || eqi(tok, "control"))      mods |= CG_MOD_CTRL;
            else if (eqi(tok, "shift"))                       mods |= CG_MOD_SHIFT;
            else if (eqi(tok, "alt"))                         mods |= CG_MOD_ALT;
            else return 0;                   /* 修饰键之外的东西不能出现在前面 */
        } else {
            key = accel_key_of(tok);
            if (key == CG_ACCEL_NONE) return 0;
        }
        p = plus ? plus + 1 : NULL;
    }
    if (key == CG_ACCEL_NONE) return 0;
    /* 不带修饰键的字母/数字/具名键一律拒绝：加速键在焦点位于输入框时照样命中，
     * 裸 S 会让用户打不出 s，裸 Delete/Home 会毁掉编辑操作。 */
    if (mods == 0 && !accel_is_function_key(key)) return 0;

    if (out) {
        out->key = key;
        out->mods = mods;
    }
    return 1;
}

/**
 * @brief 键值 -> 显示名（F1 / A / PageUp…）
 * @param key 键值
 * @param buf 输出缓冲；不认识这个键时内容未定义
 * @param cap buf 的容量（字节）
 * @return int 1 = 写成功，0 = 不认识这个键
 */
static int accel_key_name(int key, char *buf, size_t cap)
{
    size_t i;

    if (key >= CG_ACCEL_A && key < CG_ACCEL_A + 26) {
        snprintf(buf, cap, "%c", (char)('A' + (key - CG_ACCEL_A)));
        return 1;
    }
    if (key >= CG_ACCEL_0 && key < CG_ACCEL_0 + 10) {
        snprintf(buf, cap, "%c", (char)('0' + (key - CG_ACCEL_0)));
        return 1;
    }
    if (key >= CG_ACCEL_F1 && key < CG_ACCEL_F1 + 24) {
        snprintf(buf, cap, "F%d", key - CG_ACCEL_F1 + 1);
        return 1;
    }
    for (i = 0; i < sizeof(k_named) / sizeof(k_named[0]); i++) {
        if (k_named[i].key == key) {          /* 规范名排前面，取第一个命中 */
            snprintf(buf, cap, "%s", k_named[i].name);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 把快捷键统一成规范显示形式（修饰键顺序固定 Ctrl+Shift+Alt）
 * @param accel 原文
 * @param buf 输出缓冲；失败时写成空串
 * @param cap buf 的容量（字节）
 * @return int 1 = 成功；0 = 解析不了（buf 写成空串）
 */
int cg_accel_format(const char *accel, char *buf, size_t cap)
{
    cg_accel_t a;
    char       key[16];

    if (buf && cap) buf[0] = '\0';
    if (!buf || cap == 0) return 0;
    if (!cg_accel_parse(accel, &a)) return 0;
    if (!accel_key_name(a.key, key, sizeof(key))) return 0;
    snprintf(buf, cap, "%s%s%s%s",
             (a.mods & CG_MOD_CTRL)  ? "Ctrl+"  : "",
             (a.mods & CG_MOD_SHIFT) ? "Shift+" : "",
             (a.mods & CG_MOD_ALT)   ? "Alt+"   : "",
             key);
    return 1;
}

/* ------------------------------------------------------------ 动作表 */

/**
 * @brief 按名字找动作下标
 * @param app 所属应用
 * @param name 动作名
 * @return int 下标；没有这个动作（或参数为空）返回 -1
 */
int cg_action_index(const cg_app_t *app, const char *name)
{
    if (!app || !name || !*name) return -1;
    for (int i = 0; i < app->nactions; i++) {
        if (app->actions[i].name && strcmp(app->actions[i].name, name) == 0) return i;
    }
    return -1;
}

/**
 * @brief 校验所有引用点：动作名存在 / 该类型支持 click / 没同时绑 click
 * @param app 所属应用（控件树已建好）
 * @return void 无返回值（问题只记账与写日志）
 */
static void action_validate_refs(cg_app_t *app)
{
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        const char *id = w->id ? w->id : "-";

        if (w->action && *w->action) {
            if (cg_action_index(app, w->action) < 0) {
                cg_log(CG_LOG_WARN, "控件 \"%s\" 引用了不存在的动作 \"%s\"", id, w->action);
                app->bad_actions++;
            }
            /* 不支持 click 的类型：动作永远不会被触发 —— 说出来，别让人等一个不来的事件 */
            if (!cg_kind_supports_event(w->type, CG_EVENT_CLICK)) {
                cg_log(CG_LOG_WARN, "控件 \"%s\"（%s）不支持 click，所以动作 \"%s\" 永远不会触发",
                       id, w->type ? w->type : "?", w->action);
                app->bad_actions++;
            }
            /* 两处真相：click 会被动作接管，自己绑的那份永远不会被调用 */
            if (w->handlers[CG_EVENT_CLICK]) {
                cg_log(CG_LOG_WARN, "控件 \"%s\" 既引用了动作 \"%s\" 又绑了 click —— click 会被忽略",
                       id, w->action);
                app->bad_actions++;
            }
        }
        for (int i = 0; i < w->nmenu; i++) {
            const cg_menu_row_t *r = &w->menu[i];
            if (!r->action || !*r->action) continue;
            if (cg_action_index(app, r->action) < 0) {
                cg_log(CG_LOG_WARN, "菜单 \"%s\" 的第 %d 项引用了不存在的动作 \"%s\"",
                       id, i, r->action);
                app->bad_actions++;
            }
        }
    }
}

/**
 * @brief 把动作的**声明式状态**（文字、初始启用）落到所有引用点上
 * @param app 所属应用（动作表已建好）
 * @return void 无返回值
 */
static void action_apply_to_refs(cg_app_t *app)
{
    /* ---- 控件引用 ---- */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        const char *t;
        int i;

        if (!w->action || !*w->action) continue;
        i = cg_action_index(app, w->action);
        if (i < 0) continue;                                   /* 已警告过 */
        if (!cg_kind_supports_event(w->type, CG_EVENT_CLICK)) continue;  /* 已警告过 */
        /* 引用无效时**什么都不做**：不然会出现"配置是错的、界面却按它变了"这种
         * 最难排查的组合（警告在日志里、界面已经歪了）。 */

        /* 初始启用：动作声明成禁用，引用它的按钮/菜单从第一帧起就该是灰的 ——
         * 只拦 cg_action_invoke 的话按钮看着能点、点了没反应。
         * 走 cg_widget_set_enabled 而不是直接改 enabled：它会顺手 sync 后端，
         * 而本函数在建原生控件之前跑，所以控件建出来时 enabled 就是对的。 */
        cg_widget_set_enabled(w, app->actions[i].enabled);

        /* 初始文字：动作写了文字就覆盖（Qt 的 QAction / GTK 的 GAction 都是这个语义，
         * 否则同一个命令在菜单和按钮上会显示成两个名字）。**直接改 w->text / node->text，
         * 不走 cg_widget_set_text()** —— 那个函数会顺手 cg_bind_propagate()，于是
         * "动作文字"会被当成一次真实改动推给绑定的目标；这里只是解析期的归一化。 */
        t = app->actions[i].text;
        if (!t || !*t) continue;
        free(w->text);
        w->text = cg_str_dup(t);
        if (w->node) {
            free(w->node->text);
            w->node->text = cg_str_dup(t);
        }
    }

    /* ---- 菜单项引用 ---- */
    /* 菜单项的静态文字也在这里归一化（**不是**在后端建菜单时）：这样
     * cg_menu_last_text() 拿到的也是界面上真正显示的那一份，而且这条规则在没有窗口的
     * 单测里就能验 —— 只放在 cgw_menu_build 里的话，它就成了"声明了却只在开窗后才
     * 生效"的那类路径。只对**可点的叶子项**做（cmd != 0）：分隔线与下拉项挂 action
     * 没有意义（而且把文字改成 "-" 会把那一项变成分隔线的判定条件）。 */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        for (int i = 0; i < w->nmenu; i++) {
            cg_menu_row_t *r = &w->menu[i];
            const char *t;
            int ai;

            if (!r->action || !*r->action || r->cmd == 0) continue;
            ai = cg_action_index(app, r->action);
            if (ai < 0) continue;
            t = app->actions[ai].text;
            if (!t || !*t) continue;
            free(r->text);
            r->text = cg_str_dup(t);
        }
    }
}

/**
 * @brief 按 .cgui 的 actions 建运行时动作表（并校验引用点）
 * @param app 所属应用；控件树必须已经建好（校验要读控件的 action 引用）
 * @return void 无返回值
 */
void cg_action_build(cg_app_t *app)
{
    if (!app || app->doc.nactions == 0) return;

    app->actions = (cg_action_row_t *)calloc(app->doc.nactions, sizeof(cg_action_row_t));
    if (!app->actions) return;
    app->nactions = (int)app->doc.nactions;

    for (int i = 0; i < app->nactions; i++) {
        const cg_action_t *da = &app->doc.actions[i];
        cg_action_row_t   *a  = &app->actions[i];
        char display[64];

        a->name    = cg_str_dup(da->name ? da->name : "");
        a->text    = cg_str_dup(da->text ? da->text : "");
        a->handler = cg_str_dup(da->handler ? da->handler : "");
        a->accel   = cg_str_dup("");
        a->enabled = da->enabled ? 1 : 0;
        a->checked = da->checked ? 1 : 0;

        /* handler 必须登记过 —— 与事件回调走同一张表、同一套兜底：
         * cg_resolve_binding 会写日志并记进启动期提示清单（cg_app_run 汇总弹一次）。 */
        if (!a->handler || !*a->handler) {
            cg_log(CG_LOG_WARN, "动作 \"%s\" 没写 handler，已忽略", a->name);
            app->bad_actions++;
        } else {
            a->fn = cg_resolve_binding(app, a->handler);
            if (!a->fn) app->bad_actions++;
        }

        /* 快捷键：解析失败只忽略这一条，不影响动作本身（没有快捷键的动作照样能用） */
        if (da->accel && *da->accel) {
            if (cg_accel_format(da->accel, display, sizeof(display))) {
                free(a->accel);
                a->accel = cg_str_dup(display);
                a->accel_ok = cg_accel_parse(da->accel, &a->key);
            } else {
                cg_log(CG_LOG_WARN, "动作 \"%s\" 的快捷键 \"%s\" 看不懂，已忽略", a->name, da->accel);
                app->bad_actions++;
            }
        }

        /* 命令 id 与菜单项**共用号段**：加速键与菜单项都从 WM_COMMAND 回来，必须唯一。
         * 只有真有快捷键的动作才发号（没快捷键就不会有消息回来）。 */
        if (a->accel_ok) a->accel_cmd = cg_alloc_menu_cmd(app);
    }

    /* 同一个快捷键被两个动作声明：加速键表按顺序取第一个匹配，后者**悄悄不生效**。
     * 这类"配了没反应"必须在启动时说清楚（后声明的让位）。 */
    for (int i = 0; i < app->nactions; i++) {
        for (int j = 0; j < i; j++) {
            if (!app->actions[i].accel_ok || !app->actions[j].accel_ok) continue;
            if (app->actions[i].key.key == app->actions[j].key.key &&
                app->actions[i].key.mods == app->actions[j].key.mods) {
                cg_log(CG_LOG_WARN, "动作 \"%s\" 的快捷键 %s 与 \"%s\" 重复，它不会生效",
                       app->actions[i].name, app->actions[i].accel, app->actions[j].name);
                app->bad_actions++;
                app->actions[i].accel_ok = 0;
                app->actions[i].accel_cmd = 0;
                break;
            }
        }
    }

    action_validate_refs(app);
    /* 声明式状态（文字、初始启用）落到引用点上，**放在校验之后**：
     * 校验要按 .cgui 里的原文报错，别让覆盖后的文字出现在"引用了不存在的动作"这种诊断里。 */
    action_apply_to_refs(app);
}

/**
 * @brief 释放动作表
 * @param app 所属应用
 * @return void 无返回值
 */
void cg_action_release(cg_app_t *app)
{
    if (!app || !app->actions) return;
    for (int i = 0; i < app->nactions; i++) {
        free(app->actions[i].name);
        free(app->actions[i].text);
        free(app->actions[i].accel);
        free(app->actions[i].handler);
    }
    free(app->actions);
    app->actions = NULL;
    app->nactions = 0;
}

/* ------------------------------------------------------------ 公共 API */

/**
 * @brief 触发一个动作
 * @param app 所属应用
 * @param name 动作名
 * @return int 1 = 真的调用了回调；0 = 没这个动作 / 被禁用 / handler 没登记
 */
int cg_action_invoke(cg_app_t *app, const char *name)
{
    int             i;
    cg_action_row_t *a;
    cg_widget_t     *win;

    i = cg_action_index(app, name);
    if (i < 0) return 0;
    a = &app->actions[i];
    if (!a->enabled || !a->fn) return 0;

    /* 回调签名是固定的三参数，所以"是哪个动作"只能像鼠标坐标那样带出去：
     * 写进窗口伪控件的 action_last，回调里用 cg_action_name(self) 取。 */
    win = app->window;
    if (win) {
        free(win->action_last);
        win->action_last = cg_str_dup(a->name ? a->name : "");
    }
    a->fn(win, CG_EVENT_CLICK, win ? win->user : NULL);
    return 1;
}

/**
 * @brief 按加速键的命令 id 触发动作
 * @param app 所属应用
 * @param cmd WM_COMMAND / WM_SYSCOMMAND 里回来的命令 id
 * @return int 1 = 命中了某个动作的快捷键并已处理；0 = 不是（调用方按菜单命令那条路走）
 */
int cg_action_invoke_cmd(cg_app_t *app, int cmd)
{
    if (!app || cmd == 0) return 0;
    for (int i = 0; i < app->nactions; i++) {
        if (app->actions[i].accel_cmd != cmd) continue;
        cg_action_invoke(app, app->actions[i].name);
        return 1;
    }
    return 0;
}

/**
 * @brief 启用 / 禁用一个动作，并同步到所有引用它的 UI
 * @param app 所属应用
 * @param name 动作名
 * @param enabled 非 0 = 启用
 * @return void 无返回值
 */
void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled)
{
    int i = cg_action_index(app, name);
    if (i < 0) return;
    app->actions[i].enabled = enabled ? 1 : 0;

    /* 控件那一半走正常路径：cg_widget_set_enabled 的契约要求把**两个** HWND 都覆盖
     * （数值框的调节钮、datetime 的第二个 DTP），自己 EnableWindow 一遍会漏。 */
    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        if (w->action && strcmp(w->action, name) == 0) {
            cg_widget_set_enabled(w, app->actions[i].enabled);
        }
    }
    /* 菜单项那一半只有后端知道（HMENU 在它手里） */
    cg_win_action_sync(app, name);
}

/**
 * @brief 动作当前是否启用
 * @param app 所属应用
 * @param name 动作名
 * @return int 1 = 启用；没这个动作返回 0
 */
int cg_action_is_enabled(const cg_app_t *app, const char *name)
{
    int i = cg_action_index(app, name);
    return (i < 0) ? 0 : (app->actions[i].enabled ? 1 : 0);
}

/**
 * @brief 勾选 / 取消勾选一个动作（同步到引用它的菜单项）
 * @param app 所属应用
 * @param name 动作名
 * @param checked 非 0 = 勾选
 * @return void 无返回值
 */
void cg_action_set_checked(cg_app_t *app, const char *name, int checked)
{
    int i = cg_action_index(app, name);
    if (i < 0) return;
    app->actions[i].checked = checked ? 1 : 0;
    cg_win_action_sync(app, name);
}

/**
 * @brief 动作当前是否勾选
 * @param app 所属应用
 * @param name 动作名
 * @return int 1 = 勾选；没这个动作返回 0
 */
int cg_action_is_checked(const cg_app_t *app, const char *name)
{
    int i = cg_action_index(app, name);
    return (i < 0) ? 0 : (app->actions[i].checked ? 1 : 0);
}

/**
 * @brief 最近一次被激活的动作名（回调里用 self 取）
 * @param self 回调里的 self（动作回调恒为主窗口伪控件）
 * @return const char* 动作名；没有则 NULL
 */
const char *cg_action_name(const cg_widget_t *self)
{
    return self ? self->action_last : NULL;
}

/**
 * @brief 动作个数
 * @param app 所属应用
 * @return int 个数；app 为空返回 0
 */
int cg_action_count(const cg_app_t *app)
{
    return app ? app->nactions : 0;
}

/**
 * @brief 第 i 个动作的名字（遍历用）
 * @param app 所属应用
 * @param i 下标（0 起，上界为动作数减一）
 * @return const char* 名字（动作表持有，不释放）；越界返回 NULL
 */
const char *cg_action_at(const cg_app_t *app, int i)
{
    if (!app || i < 0 || i >= app->nactions) return NULL;
    return app->actions[i].name;
}

/**
 * @brief 动作的快捷键（规范化显示形式）
 * @param app 所属应用
 * @param name 动作名
 * @return const char* 形如 "Ctrl+S"；没有快捷键或没这个动作返回空串
 */
const char *cg_action_accel(const cg_app_t *app, const char *name)
{
    int i = cg_action_index(app, name);
    if (i < 0) return "";
    return app->actions[i].accel ? app->actions[i].accel : "";
}
