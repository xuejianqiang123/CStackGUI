/**
 * @file    cg_app.c
 * @brief   应用生命周期：定位并解析 .cgui、建窗口、消息循环、收尾与自检
 * @details 从原 cg_app.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const cg_backend_t *cg_backend_active = NULL;

/* ------------------------------------------------------------ 启动期错误可见化
 * 发布版是 -mwindows（没有控制台）：致命错误必须弹窗 + 落日志，
 * 否则用户看到的就是"双击 exe 什么都没发生"。自检（allow_ui=0）只写日志不弹窗。 */
static char s_fatal[1024];

/**
 * @brief 往致命错误缓冲里追加一段（截断安全）
 * @param fmt printf 风格格式串，其后是它的参数
 * @return void 无返回值
 */
static void fatal_add(const char *fmt, ...)
{
    size_t  used = strlen(s_fatal);
    va_list ap;

    if (used + 1 >= sizeof(s_fatal)) return;
    va_start(ap, fmt);
    vsnprintf(s_fatal + used, sizeof(s_fatal) - used, fmt, ap);
    va_end(ap);
}

/**
 * @brief 报一个致命错误：写日志；允许界面时弹窗（并附日志文件位置）
 * @param allow_ui 非 0 = 允许弹窗；0 = 只写日志（自检模式）
 * @param title 标题
 * @param text 正文；NULL 当空串
 * @return void 无返回值
 */
static void fatal_report(int allow_ui, const char *title, const char *text)
{
    cg_log(CG_LOG_ERROR, "%s：%s", title, text ? text : "");
    if (!allow_ui) return;
    {
        char        buf[1500];
        const char *log = cg_log_file();
        if (log && *log) snprintf(buf, sizeof(buf), "%s\n\n日志文件：\n%s", text ? text : "", log);
        else             snprintf(buf, sizeof(buf), "%s", text ? text : "");
        cg_fatal_dialog(title, buf);
    }
}


/* ------------------------------------------------------------ 程序配置 */

/**
 * @brief 读程序配置（.cgui 的 app.info）里的一项
 * @param app 应用
 * @param key "product"/"desc"/"comments"/"version"/"build"/"author"/"company"/
 *            "copyright"/"address"/"postcode"/"phone"/"fax"/"email"/"homepage"
 * @return const char* 值（UTF-8）；没有返回 NULL
 */
const char *cg_app_info(const cg_app_t *app, const char *key)
{
    const cg_info_t *in;
    if (!app || !key) return NULL;
    in = &app->doc.info;
    if (!strcmp(key, "product"))   return in->product;
    if (!strcmp(key, "desc"))      return in->desc;
    if (!strcmp(key, "comments"))  return in->comments;
    if (!strcmp(key, "version"))   return in->version;
    if (!strcmp(key, "build"))     return in->build;
    if (!strcmp(key, "author"))    return in->author;
    if (!strcmp(key, "company"))   return in->company;
    if (!strcmp(key, "copyright")) return in->copyright;
    if (!strcmp(key, "address"))   return in->address;
    if (!strcmp(key, "postcode"))  return in->postcode;
    if (!strcmp(key, "phone"))     return in->phone;
    if (!strcmp(key, "fax"))       return in->fax;
    if (!strcmp(key, "email"))     return in->email;
    if (!strcmp(key, "homepage"))  return in->homepage;
    return NULL;
}


/* ------------------------------------------------------------ 启动 */

/* ------------------------------------------------------------ 找界面文件
 * 双击 exe 时没有命令行参数，当前目录也不一定是项目目录，
 * 所以 auto_locate_cgui 打开时会按几个约定位置去找：
 *   1) exe 旁边           build\hello.cgui
 *   2) 标准布局           build\..\apps\hello\hello.cgui
 *   3) 标准布局但名字不同  build\..\apps\hello\<原文件名>
 */

/**
 * @brief 取路径的文件名部分（兼容反斜杠与斜杠）
 * @param path 路径（UTF-8）
 * @return const char* 指向 path 内部的文件名；没有分隔符时就是 path 本身
 */
static const char *path_base(const char *path)
{
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s1) s1 = s2;
    return s1 ? s1 + 1 : path;
}

/**
 * @brief 取路径的目录部分
 * @param path 路径
 * @param out 输出缓冲（写目录后以 NUL 结尾）
 * @param size out 的容量（字节）
 * @return void 无返回值（无目录时写 "."；不带末尾分隔符，盘符根除外）
 */
static void path_dir(const char *path, char *out, size_t size)
{
    const char *b = path_base(path);
    size_t n = (size_t)(b - path);

    if (n == 0) {
        snprintf(out, size, ".");
        return;
    }
    /* 不削掉末尾分隔符的话，拼出来的候选路径会变成 "...\gallery\\xx.cgui" */
    while (n > 1 && (path[n - 1] == '\\' || path[n - 1] == '/')) {
        if (n == 3 && path[1] == ':') break;
        n--;
    }
    if (n > size - 1) n = size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/**
 * @brief 按约定位置找界面文件并读入（内嵌文本 > 显式路径 > exe 旁 > 示例目录）
 * @param desc 启动参数（内嵌文本 / 显式路径 / 是否自动定位）
 * @param found 输出实际用到的路径；可为 NULL
 * @param found_size found 的容量（字节）
 * @return char* JSON 原文（调用方 free）；找不到返回 NULL 并打印试过的位置
 */
static char *open_cgui_text(const cg_app_desc_t *desc, char *found, size_t found_size)
{
    char  exe[512], dir[512], stem[512], cand[2048];
    char *json;
    const char *base;
    const char *exe_name;
    char *dot;

    if (found && found_size) found[0] = '\0';
    if (desc->cgui_text) return cg_str_dup(desc->cgui_text);

    if (desc->cgui_path) {
        json = cg_read_file(desc->cgui_path);
        if (json) {
            if (found) snprintf(found, found_size, "%s", desc->cgui_path);
            return json;
        }
        if (!desc->auto_locate_cgui) {
            s_fatal[0] = '\0';
            fatal_add("这个文件打不开：\n    %s\n", desc->cgui_path);
            return NULL;      /* 日志与弹窗由 app_open 的 fatal_report 统一处理 */
        }
        cg_log(CG_LOG_WARN, "读不到 %s，在 exe 附近找找看", desc->cgui_path);
    } else if (!desc->auto_locate_cgui) {
        s_fatal[0] = '\0';
        fatal_add("没有提供 cgui_path 或 cgui_text，也没有打开自动查找。\n");
        return NULL;
    }

    if (cg_exe_path(exe, sizeof(exe)) != 0) return NULL;
    path_dir(exe, dir, sizeof(dir));

    exe_name = path_base(exe);
    snprintf(stem, sizeof(stem), "%s", exe_name);
    dot = strrchr(stem, '.');
    if (dot) *dot = '\0';

    base = desc->cgui_path ? path_base(desc->cgui_path) : "";

    if (base[0]) {
        snprintf(cand, sizeof(cand), "%s\\%s", dir, base);
        json = cg_read_file(cand);
        if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }
    }

    snprintf(cand, sizeof(cand), "%s\\..\\apps\\%s\\%s.cgui", dir, stem, stem);
    json = cg_read_file(cand);
    if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }

    snprintf(cand, sizeof(cand), "%s\\..\\examples\\%s\\%s.cgui", dir, stem, stem);
    json = cg_read_file(cand);
    if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }

    if (base[0] && strcmp(base, stem) != 0) {
        snprintf(cand, sizeof(cand), "%s\\..\\apps\\%s\\%s", dir, stem, base);
        json = cg_read_file(cand);
        if (json) { fprintf(stderr, "[cstackgui] 界面文件: %s\n", cand); if (found) snprintf(found, found_size, "%s", cand); return json; }
    }

    s_fatal[0] = '\0';
    fatal_add("程序按顺序找过这些位置：\n");
    if (desc->cgui_path) fatal_add("    %s\n", desc->cgui_path);
    if (base[0])         fatal_add("    %s\\%s\n", dir, base);
    fatal_add("    %s\\..\\apps\\%s\\%s.cgui\n", dir, stem, stem);
    fatal_add("    %s\\..\\examples\\%s\\%s.cgui\n", dir, stem, stem);
    fatal_add("\n把 .cgui 和 exe 放在同一个目录即可；"
              "也可以用命令行指定：%s <界面文件.cgui>", exe_name);
    return NULL;
}

/**
 * @brief 打开应用：读界面文件、解析、构建控件树（不开窗）
 * @param app 应用结构（进来先清零）
 * @param desc 启动参数
 * @param allow_ui 非 0 = 致命错误时允许弹窗
 * @return int 0 成功；2 找不到界面文件；3 解析失败
 */
static int app_open(cg_app_t *app, const cg_app_desc_t *desc, int allow_ui)
{
    char  err[256] = { 0 };
    char  resolved[2048] = { 0 };
    char *json = NULL;

    memset(app, 0, sizeof(*app));
    app->scale = 1;
    app->bindings = desc ? desc->bindings : NULL;
    app->dblclick_suppress_click = desc ? desc->dblclick_suppress_click : 0;

    json = desc ? open_cgui_text(desc, resolved, sizeof(resolved)) : NULL;
    if (!json) {
        fatal_report(allow_ui, "CStackGUI：找不到界面文件",
                     s_fatal[0] ? s_fatal : "没有提供 .cgui 界面文件。");
        return 2;
    }

    /* 记住界面文件在哪：image 的相对路径按它所在目录解释，符合直觉 */
    if (resolved[0]) {
        const char *slash = strrchr(resolved, '\\');
        const char *slash2 = strrchr(resolved, '/');
        if (slash2 > slash) slash = slash2;
        app->cgui_path = cg_str_dup(resolved);
        if (slash) {
            size_t n = (size_t)(slash - resolved);
            app->cgui_dir = (char *)malloc(n + 1);
            if (app->cgui_dir) {
                memcpy(app->cgui_dir, resolved, n);
                app->cgui_dir[n] = '\0';
            }
        }
    }

    {
        cg_doc_t *doc = cg_doc_parse(json, err, sizeof(err));
        free(json);
        if (!doc) {
            fatal_report(allow_ui, "CStackGUI：界面文件（.cgui）格式有误", err);
            free(app->cgui_path); app->cgui_path = NULL;   /* 解析失败路径别漏放 */
            free(app->cgui_dir);  app->cgui_dir  = NULL;
            return 3;
        }
        app->doc = *doc;
        free(doc);   /* doc 的所有权已转移 */
    }

    if (desc && desc->title_override) {
        free(app->doc.title);
        app->doc.title = cg_str_dup(desc->title_override);
    }

    cg_build_widgets(app);
    /* .cgui 里的控件绑定（bind）先应用一遍：目标控件跟源对齐之后再建原生控件，
     * 所以窗口第一次显示出来时两边就是一致的，不会先闪一个默认值再跳。 */
    cg_bind_apply_all(app);
    /* 动作层：handler 查表、快捷键解析、引用点校验。
     * 时机有讲究 —— 必须在**建控件树之后**（校验要读控件的 action 引用），
     * 而且在**后端 init 之前**：动作的 text 会覆盖控件文字，原生控件建出来时就该带对文字，
     * 否则先显示 .cgui 里那份、再闪一下变成动作的文字。 */
    cg_action_build(app);
    app->start_tick = cg_ticks();

    if (desc && desc->trace_doc) cg_doc_dump(&app->doc, stdout);
    (void)allow_ui;
    return 0;
}

/**
 * @brief 关闭应用：清定时器、关后端、断开文档、释放全部控件
 * @param app 应用
 * @return void 无返回值
 */
static void app_close(cg_app_t *app)
{
    cg_timers_clear(app);
    cg_action_release(app);      /* 动作表自己持有一份字符串，先收掉 */
    free(app->cgui_path);
    free(app->cgui_dir);
    app->cgui_path = NULL;
    app->cgui_dir = NULL;
    cg_app_drop_put(app, NULL, 0);   /* 拖放路径列表（正常在 drop 回调返回时就已清空） */
    if (cg_backend_active && cg_backend_active->shutdown) cg_backend_active->shutdown(app);
    if (app->doc.root) {      /* 运行时控件会回写节点，先断开再放文档 */
        for (cg_widget_t *w = app->widgets; w; w = w->next) w->node = NULL;
        cg_doc_release(&app->doc);   /* 注意：app->doc 是内嵌结构，不能 free 它 */
    }
    cg_widget_t *w = app->widgets;
    while (w) {
        cg_widget_t *next = w->next;
        cg_widget_free(w);
        w = next;
    }
    app->widgets = NULL;
}

/**
 * @brief 建窗口 + 跑消息循环，结束后释放全部资源
 * @param desc 启动参数
 * @return int 进程退出码（0 正常）
 */
int cg_app_run(const cg_app_desc_t *desc)
{
    cg_app_t app;
    int rc;

    cg_diag_init();     /* 日志 + 崩溃兜底：后面任何失败都要留下痕迹（GUI 版看不到 stderr） */

    rc = app_open(&app, desc, 1);
    if (rc != 0) return rc;

    cg_backend_active = &cg_backend_win32;
    if (cg_backend_active->init(&app) != 0) {
        fatal_report(1, "CStackGUI：创建窗口失败",
                     "窗口没能建起来（可能是桌面会话、DPI 或系统控件异常）。\n");
        app_close(&app);
        cg_backend_active = NULL;
        return 4;
    }
    /* 建好原生控件后再同步一次初始状态 */
    for (cg_widget_t *w = app.widgets; w; w = w->next) {
        if (w->native && cg_backend_active->sync) cg_backend_active->sync(w);
    }

    /* 名字写错不会编译报错 —— 新手最常卡在这里：汇总弹一次，并写进日志 */
    {
        int n = cg_unresolved_count();
        if (n > 0) {
            char   msg[1200];
            size_t used = (size_t)snprintf(msg, sizeof(msg),
                                           "有 %d 个事件绑的函数名在 C 代码里找不到，这些事件不会触发：\n\n", n);
            for (int i = 0; i < n && i < 8; i++) {
                int wrote = snprintf(msg + used, sizeof(msg) - used, "    %s\n", cg_unresolved_name(i));
                if (wrote < 0) break;
                used += (size_t)wrote;
                if (used >= sizeof(msg) - 1) { used = sizeof(msg) - 1; break; }
            }
            if (n > 8) snprintf(msg + used, sizeof(msg) - used, "    …（还有 %d 个，见日志）\n", n - 8);
            fatal_report(1, "CStackGUI：有事件绑定找不到函数", msg);
        }
    }

    if (desc && desc->on_ready) desc->on_ready(&app, desc->ready_user);

    rc = cg_backend_active->run(&app);
    app_close(&app);
    cg_backend_active = NULL;
    cg_log(CG_LOG_INFO, "结束：退出码 %d", rc);
    cg_log_close();
    return rc;
}

/* ------------------------------------------------------------ 自检 */

typedef struct {
    int total;
    int fired;
    int unresolved;
} selftest_stat_t;

/**
 * @brief 递归走控件树，对每个绑了回调的事件做一次合成触发并统计
 * @param app 所属应用
 * @param w 当前节点（窗口伪控件或普通控件）
 * @param st 统计累加器
 * @param depth 递归深度
 * @return void 无返回值
 */
static void selftest_walk(cg_app_t *app, cg_widget_t *w, selftest_stat_t *st, int depth)
{
    (void)app;
    (void)depth;

    if (w->is_window) {
        /* 窗口级事件：不合成触发（会真的关窗/退出），但要确认回调名在 C 侧登记过，
         * 否则事件名抄错在自检里看不出来。 */
        for (int i = 0; i < CG_EVENT_COUNT; i++) {
            if (!w->node || !w->node->ev[i]) continue;
            if (!cg_kind_supports_event(w->type, (cg_event_t)i)) continue;
            if (w->handlers[i]) continue;
            cg_log(CG_LOG_ERROR, "  [FAIL] %-10s #%-14s %-13s 绑定 \"%s\" 未在 C 侧登记",
                   "window", w->id ? w->id : "-",
                   cg_event_name((cg_event_t)i), w->node->ev[i]);
            st->unresolved++;
        }
        for (size_t i = 0; i < w->nchildren; i++) selftest_walk(app, w->children[i], st, depth);
        return;
    }

    st->total++;
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (i == CG_EVENT_CLOSE) continue;      /* 关窗事件不适合合成 */
        if (!w->node || !w->node->ev[i]) continue;
        if (!cg_kind_supports_event(w->type, (cg_event_t)i)) continue;  /* 不支持的事件上面已报过 */
        if (!w->handlers[i]) {
            cg_log(CG_LOG_ERROR, "  [FAIL] %-10s #%-14s %-9s 绑定 \"%s\" 未在 C 侧登记",
                   w->type ? w->type : "-", w->id ? w->id : "-",
                   cg_event_name((cg_event_t)i), w->node->ev[i]);
            st->unresolved++;
            continue;
        }
        printf("  [FIRE] %-10s #%-14s %-9s -> %s\n",
               w->type ? w->type : "-", w->id ? w->id : "-",
               cg_event_name((cg_event_t)i), w->node->ev[i]);
        w->handlers[i](w, (cg_event_t)i, w->user);
        st->fired++;
    }
    for (size_t i = 0; i < w->nchildren; i++) selftest_walk(app, w->children[i], st, depth);
}

/**
 * @brief 不开窗的自检：解析 .cgui、打印控件树、合成触发全部回调
 * @param desc 启动参数（同 cg_app_run）
 * @return int 0 全部通过；5 有未登记 / 不支持的事件；2、3 同打开应用
 */
int cg_app_selftest(const cg_app_desc_t *desc)
{
    cg_app_t app;
    selftest_stat_t st = { 0, 0, 0 };
    int rc;

    /* 树/事件名输出里有中文（UTF-8 字节），控制台默认代码页会显示成乱码 */
    cg_win_console_utf8();
    cg_diag_init();     /* 自检结果也写进日志（GUI 版看不到 stderr，CI 里两者都有） */

    /* app_open 里会调 cg_ticks()，后端在自检模式下不参与，这里给个空后端 */
    rc = app_open(&app, desc, 0);
    if (rc != 0) return rc;

    printf("== CStackGUI selftest ==\n");
    printf("-- 解析出的控件树 --\n");
    cg_doc_dump(&app.doc, stdout);

    if (desc && desc->on_ready) {
        printf("-- on_ready --\n");
        desc->on_ready(&app, desc->ready_user);
    }

    printf("-- 合成事件 --\n");
    if (app.window) selftest_walk(&app, app.window, &st, 0);
    printf("-- 结果: 控件 %d 个, 触发回调 %d 次, 未登记 %d 个, 事件不支持 %d 个, 绑定出错 %d 个, 动作出错 %d 个, 样式引用失效 %d 个 --\n",
           st.total, st.fired, st.unresolved, app.unsupported_events, app.bad_binds,
           app.bad_actions, app.doc.style_missing);

    {
        /* 必须在 app_close() **之前**取出来：它会把 doc 释放掉，之后再读就是悬空访问 */
        int missing = app.doc.style_missing;
        int bad = st.unresolved + app.unsupported_events + app.bad_binds + app.bad_actions + missing;

        app_close(&app);
        if (bad) {
            cg_log(CG_LOG_ERROR, "自检失败：未登记回调 %d 个，绑了不支持的事件 %d 个，绑定源不存在 %d 个，动作出错 %d 个，样式引用失效 %d 个",
                   st.unresolved, app.unsupported_events, app.bad_binds, app.bad_actions, missing);
            cg_log_close();
            return 5;
        }
    }
    cg_log(CG_LOG_INFO, "自检通过：控件 %d 个，触发回调 %d 次", st.total, st.fired);
    cg_log_close();
    printf("selftest OK\n");
    return 0;
}
