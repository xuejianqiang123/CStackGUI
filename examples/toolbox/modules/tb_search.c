/**
 * @file    tb_search.c
 * @brief   toolbox 全局搜索实现：名称/命令子串过滤 + 回车启动第一个 + 双击启动选中
 */
#include "tb_search.h"

#include <stdio.h>
#include <string.h>

#include "tb_log.h"
#include "tb_mytools.h"
#include "tb_opts.h"
#include "tb_tools.h"

/** 搜索结果条数上限 */
#define K_HIT_MAX 32

/** 搜索命中的工具下标（与 lst_search 行号一一对应） */
static int s_hit[K_HIT_MAX];

/** 搜索命中条数 */
static int s_hit_n;

/**
 * @brief 大小写不敏感地找子串（自己写，strcasestr 不是标准 C）
 * @param hay 长串
 * @param needle 子串
 * @return const char* 命中位置；没命中返回 NULL
 */
static const char *ci_str(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);

    if (nlen == 0) return hay;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            char a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == nlen) return hay;
    }
    return NULL;
}

/**
 * @brief 按当前搜索框内容重过滤结果列表（就绪钩子里用空关键字跑一次，显示"共 N 个"）
 * @param app 应用
 * @return void 无返回值
 */
void tb_search_apply(cg_app_t *app)
{
    cg_widget_t *lst = cg_find(app, "lst_search");
    const char  *q   = cg_widget_text(cg_find(app, "edt_search"));
    int         i;

    s_hit_n = 0;
    if (lst) cg_widget_list_clear(lst);
    if (!q || !*q) {
        if (lst) cg_widget_list_addf(lst, "共 %d 个工具，输入关键字过滤",
                                     tb_tool_builtin_count() + tb_mytools_count());
        return;
    }

    for (i = 0; i < tb_tool_count() && s_hit_n < K_HIT_MAX; i++) {
        const char *name = tb_tool_name(i);
        const char *cmd  = tb_tool_cmd(i);
        if (!name[0]) continue;                       /* 没配的 mytools 槽位不参加搜索 */
        if (ci_str(name, q) || ci_str(cmd, q)) {
            s_hit[s_hit_n++] = i;
            if (lst) cg_widget_list_addf(lst, "%s    (%s)", name, cmd);
        }
    }
    if (lst && s_hit_n == 0) cg_widget_list_add(lst, "（没有匹配的工具）");

    if (tb_opt_get(2)) cg_tabs_select(cg_find(app, "tab_main"), 0);   /* AutoSearchPage */
}

/**
 * @brief 搜索输入框内容变化：重过滤
 * @param self 搜索输入框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_search_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    tb_search_apply(cg_app_of(self));
}

/**
 * @brief 搜索框回车：有匹配就启动第一个
 * @param self 搜索输入框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_search_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (s_hit_n == 0) {
        tb_status_msg(app, "没有匹配的工具");
        return;
    }
    tb_launch_tool(app, s_hit[0]);
}

/**
 * @brief 搜索结果双击：启动双击的那一行
 * @param self 搜索结果列表框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_lst_search_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    int row = cg_widget_value(self);

    (void)ev;
    (void)user;

    if (row >= 0 && row < s_hit_n) tb_launch_tool(cg_app_of(self), s_hit[row]);
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_search_bindings[] = {
    CG_BIND("on_edt_search_change",   on_edt_search_change),
    CG_BIND("on_edt_search_submit",   on_edt_search_submit),
    CG_BIND("on_lst_search_dblclick", on_lst_search_dblclick),
    CG_BIND_END
};
