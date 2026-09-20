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

/** @brief 取路径的文件名部分（兼容 \\ 和 /） */
static const char *path_base(const char *path)
{
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s1) s1 = s2;
    return s1 ? s1 + 1 : path;
}

/** @brief 取路径的目录部分（无目录时写 "."） */
static void path_dir(const char *path, char *out, size_t size)
{
    const char *b = path_base(path);
    size_t n = (size_t)(b - path);

    if (n == 0) {
        snprintf(out, size, ".");
        return;
    }
    if (n > size - 1) n = size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/**
 * @brief 按约定位置找界面文件并读入（内嵌文本 > 显式路径 > exe 旁 > apps/examples）
 * @param desc 启动参数；found 输出实际用到的路径；found_size 容量
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
            fprintf(stderr, "[cstackgui] 读不到界面文件: %s\n", desc->cgui_path);
            return NULL;
        }
        fprintf(stderr, "[cstackgui] 读不到 %s，在 exe 附近找找看\n", desc->cgui_path);
    } else if (!desc->auto_locate_cgui) {
        fprintf(stderr, "[cstackgui] 没有提供 cgui_path 或 cgui_text\n");
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

    fprintf(stderr, "[cstackgui] 找不到界面文件，试过这些位置：\n");
    if (desc->cgui_path) fprintf(stderr, "    %s\n", desc->cgui_path);
    if (base[0]) fprintf(stderr, "    %s\\%s\n", dir, base);
    fprintf(stderr, "    %s\\..\\apps\\%s\\%s.cgui\n", dir, stem, stem);
    fprintf(stderr, "  用法: %s <界面文件.cgui>\n", exe_name);
    return NULL;
}

/**
 * @brief 打开应用：读界面文件、解析、构建控件树（不开窗）
 * @param app 应用结构（先清零）；desc 启动参数；allow_ui 预留
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
    if (!json) return 2;

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
            fprintf(stderr, "[cstackgui] 解析 .cgui 失败: %s\n", err);
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
    app->start_tick = cg_ticks();

    if (desc && desc->trace_doc) cg_doc_dump(&app->doc, stdout);
    (void)allow_ui;
    return 0;
}

/** @brief 关闭应用：清定时器、关后端、断开文档、释放全部控件 */
static void app_close(cg_app_t *app)
{
    cg_timers_clear(app);
    free(app->cgui_path);
    free(app->cgui_dir);
    app->cgui_path = NULL;
    app->cgui_dir = NULL;
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

/** @brief 建窗口 + 消息循环，返回进程退出码 */
/**
 * @brief 建窗口 + 跑消息循环，结束后释放全部资源
 * @param desc 启动参数
 * @return int 进程退出码（0 正常，2/3/4 见各失败点）
 */
int cg_app_run(const cg_app_desc_t *desc)
{
    cg_app_t app;
    int rc = app_open(&app, desc, 1);

    if (rc != 0) return rc;

    cg_backend_active = &cg_backend_win32;
    if (cg_backend_active->init(&app) != 0) {
        fprintf(stderr, "[cstackgui] 创建窗口失败\n");
        app_close(&app);
        return 4;
    }
    /* 建好原生控件后再同步一次初始状态 */
    for (cg_widget_t *w = app.widgets; w; w = w->next) {
        if (w->native && cg_backend_active->sync) cg_backend_active->sync(w);
    }

    if (desc && desc->on_ready) desc->on_ready(&app, desc->ready_user);

    rc = cg_backend_active->run(&app);
    app_close(&app);
    cg_backend_active = NULL;
    return rc;
}

/* ------------------------------------------------------------ 自检 */

typedef struct {
    int total;
    int fired;
    int unresolved;
} selftest_stat_t;

/** @brief 递归走控件树，对每个绑了回调的事件做一次合成触发并统计 */
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
            fprintf(stderr, "  [FAIL] %-10s #%-14s %-13s 绑定 \"%s\" 未在 C 侧登记\n",
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
            fprintf(stderr, "  [FAIL] %-10s #%-14s %-9s 绑定 \"%s\" 未在 C 侧登记\n",
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

/** @brief 不开窗的自检：解析 .cgui、打印控件树、对每个绑定了回调的控件合成一次事件 */
/**
 * @brief 不开窗的自检：解析 .cgui、打印控件树、合成触发全部回调
 * @param desc 启动参数（同 cg_app_run）
 * @return int 0 全部通过；5 有未登记/不支持的事件；2/3 同 app_open
 */
int cg_app_selftest(const cg_app_desc_t *desc)
{
    cg_app_t app;
    selftest_stat_t st = { 0, 0, 0 };
    int rc;

    /* 树/事件名输出里有中文（UTF-8 字节），控制台默认代码页会显示成乱码 */
    cg_win_console_utf8();

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
    printf("-- 结果: 控件 %d 个, 触发回调 %d 次, 未登记 %d 个, 事件不支持 %d 个 --\n",
           st.total, st.fired, st.unresolved, app.unsupported_events);

    {
        int bad = st.unresolved + app.unsupported_events;
        app_close(&app);
        if (bad) return 5;
    }
    printf("selftest OK\n");
    return 0;
}
