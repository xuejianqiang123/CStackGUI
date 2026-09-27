/**
 * @file    cg_bind.c
 * @brief   控件绑定：一个控件的值/文字/勾选变化时自动同步到另一个控件
 * @details 解决的是"界面里两个控件要一直保持一致"这件事。以前只能自己写回调：
 *
 *              void on_sld_change(cg_widget_t *self, cg_event_t ev, void *user) {
 *                  cg_widget_set_text(cg_find(cg_app_of(self), "lab_value"),
 *                                     cg_str_format("%d", cg_widget_value(self)));
 *              }
 *
 *          现在在画布「属性」里（或 .cgui 里）一行搞定：
 *
 *              { "id": "lab_value", "type": "label",
 *                "bind": { "src": "sld_level" } }          <- 滑块动，标签跟着显示数字
 *
 *          抄什么由**目标的类型**决定，不用额外配置：
 *            - 目标有值语义（滑块/进度/圆环/微调/评分/分页/标签页/列表/下拉/工具栏）
 *                  -> 抄 value（列表类控件存的是条目下标，不按 min/max 夹）
 *            - 目标是勾选型（复选框/单选钮/开关）
 *                  -> 抄 checked 或 value != 0
 *            - 目标其它（能显示文字：标签/输入框/状态栏/按钮/链接/徽章/标签片）
 *                  -> 源是文本型就抄文字，否则把源的数值写成十进制
 *
 *          两条刻意的规矩：
 *            1) 绑定的同步**不派发目标的 change 回调**。镜像是"跟着变"，不是用户操作；
 *               否则一个控件动一下会连带触发一串回调，谁也说不清顺序；
 *            2) 所以也就不存在回调互推的死循环；两处值真的一样时直接不写、不 sync。
 */
#include "cg_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 传播深度上限。链式绑定（A->B->C）要能一路传下去，但 A<->B 双向互推必须有底 —— 
 *  正常情况下值相等就不写了、一轮就停，这个上限只是防"三个以上双向绕成环"。 */
#define CG_BIND_MAX_DEPTH 8

static int s_bind_depth;

/**
 * @brief 有值语义：绑定时要抄值的控件
 * @param w 待判定的控件；NULL 当作不是
 * @return int 1 = 值语义；0 = 不是
 * @note 按控件判而不是按类型名 —— 数值输入框（edit + 调节器）也是值语义，
 *       但它和普通文本 edit 是同一个类型名
 */
static int bind_is_value_kind(const cg_widget_t *w)
{
    static const char *const k[] = {
        "slider", "progress", "progressring", "rate", "pagination",
        "tabs", "listbox", "combo", "toolbar",
    };
    if (!w || !w->type) return 0;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (strcmp(w->type, k[i]) == 0) return 1;
    }
    return cg_widget_is_numedit(w);   /* 数值输入框：value 是真相 */
}

/** @brief 勾选语义：bind 时抄 checked 的控件 */
/**
 * @brief 勾选语义：绑定时要抄勾选态的控件
 * @param type 控件类型名；NULL 当作不是
 * @return int 1 = 勾选语义（复选框 / 单选钮 / 开关）；0 = 不是
 */
static int bind_is_check_kind(const char *type)
{
    return type && (!strcmp(type, "checkbox") || !strcmp(type, "radio") || !strcmp(type, "switch"));
}

/** @brief 列表类控件的 value 是条目下标：可以超出 min/max，取值时不能夹 */
/**
 * @brief 列表类控件的值是不是"条目下标"（可超出量程，取值时不能夹）
 * @param type 控件类型名；NULL 当作不是
 * @return int 1 = 列表类（列表框 / 下拉框 / 标签页 / 工具栏）；0 = 不是
 */
static int bind_is_list_kind(const char *type)
{
    return type && (!strcmp(type, "listbox") || !strcmp(type, "combo") ||
                    !strcmp(type, "tabs") || !strcmp(type, "toolbar"));
}

/**
 * @brief 源是不是"以文字为主"的控件（抄给文字型目标时抄文字而不是数字）
 * @param w 待判定的控件；NULL 当作不是
 * @return int 1 = 文字型；0 = 不是
 * @note 数值输入框（edit + 调节器）不算 —— 它的文字是从值派生的，抄给它等于抄数值
 */
static int bind_is_text_kind(const cg_widget_t *w)
{
    static const char *const k[] = {
        "richtext", "label", "link", "badge", "tag", "datepicker",
    };
    if (!w || !w->type) return 0;
    if (strcmp(w->type, "edit") == 0) return !cg_widget_is_numedit(w);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (strcmp(w->type, k[i]) == 0) return 1;
    }
    return 0;
}

/** @brief 把目标控件的文字换掉（同时回写文档节点），没变返回 0 */
/**
 * @brief 把目标控件的文字换掉（同时回写文档节点），没变返回 0
 * @param dst 目标控件
 * @param text 新文字（UTF-8；内部会复制一份）
 * @return int 1 = 真的改了并已同步；0 = 文字没变，什么都没做
 */
static int bind_set_text(cg_widget_t *dst, const char *text)
{
    if (dst->text && strcmp(dst->text, text) == 0) return 0;
    free(dst->text);
    dst->text = cg_str_dup(text);
    if (dst->node) {
        free(dst->node->text);
        dst->node->text = cg_str_dup(text);
    }
    cg_widget_sync(dst);
    return 1;
}

/** @brief 把目标控件的值换掉（同时回写文档节点），没变返回 0 */
/**
 * @brief 把目标控件的值换掉（同时回写文档节点），没变返回 0
 * @param dst 目标控件
 * @param v 新值
 * @return int 1 = 真的改了并已同步；0 = 值没变，什么都没做
 */
static int bind_set_value(cg_widget_t *dst, int v)
{
    if (dst->value == v) return 0;
    dst->value = v;
    if (dst->node) dst->node->value = v;
    cg_widget_sync(dst);
    return 1;
}

/** @brief 把目标控件的勾选状态换掉（同时回写文档节点），没变返回 0 */
/**
 * @brief 把目标控件的勾选状态换掉（同时回写文档节点），没变返回 0
 * @param dst 目标控件
 * @param on 新勾选态；非 0 一律当勾上
 * @return int 1 = 真的改了并已同步；0 = 状态没变，什么都没做
 */
static int bind_set_checked(cg_widget_t *dst, int on)
{
    on = on ? 1 : 0;
    if (dst->checked == on) return 0;
    dst->checked = on;
    if (dst->node) dst->node->checked = on;
    cg_widget_sync(dst);
    return 1;
}

/**
 * @brief 把源控件的值 / 文字抄到目标控件
 * @param src 源控件（值的出处）
 * @param dst 目标控件（抄什么按它的类型决定）
 * @return int 1 = 目标真的变了；0 = 值本来就一样（也用于打断双向绑定的循环）
 */
int cg_bind_copy(cg_widget_t *src, cg_widget_t *dst)
{
    if (!src || !dst || src == dst) return 0;

    if (bind_is_value_kind(dst)) {
        int v = src->value;
        if (!bind_is_list_kind(dst->type)) {   /* 列表类存的是下标，条目可以超过 100 */
            if (v < dst->vmin) v = dst->vmin;
            if (v > dst->vmax) v = dst->vmax;
        }
        return bind_set_value(dst, v);
    }
    if (bind_is_check_kind(dst->type)) {
        return bind_set_checked(dst, bind_is_check_kind(src->type) ? src->checked
                                                                  : (src->value != 0));
    }
    {
        char buf[32];
        const char *text;
        if (bind_is_check_kind(src->type)) {
            snprintf(buf, sizeof(buf), "%d", src->checked ? 1 : 0);
            text = buf;
        } else if (bind_is_text_kind(src)) {
            text = src->text ? src->text : "";
        } else {
            snprintf(buf, sizeof(buf), "%d", src->value);
            text = buf;
        }
        return bind_set_text(dst, text);
    }
}

/**
 * @brief 控件状态变了：抄给所有"以它为源"的控件；双向的还要推回自己的源
 * @param w 变化的控件
 * @return void 无返回值
 */
void cg_bind_propagate(cg_widget_t *w)
{
    cg_app_t    *app;
    cg_widget_t *x;

    if (!w || !(app = w->app) || app->nbinds <= 0) return;   /* 没绑定时零开销 */
    if (s_bind_depth >= CG_BIND_MAX_DEPTH) return;
    s_bind_depth++;

    /* 正向：目标是 x（x->bind_src 指向我）。用链表顺序遍历，结果与控件顺序无关 */
    if (w->id) {
        for (x = app->widgets; x; x = x->next) {
            if (!x->bind_src || strcmp(x->bind_src, w->id) != 0) continue;
            if (cg_bind_copy(w, x)) cg_bind_propagate(x);   /* 目标又变了：链式往下传 */
        }
    }

    /* 反向：我自己是从谁那儿抄来的（双向），现在我自己动了，推回去 */
    if (w->bind_two && w->bind_src) {
        cg_widget_t *s = cg_widget_by_id(app, w->bind_src);
        if (s && cg_bind_copy(w, s)) cg_bind_propagate(s);
    }

    s_bind_depth--;
}

/**
 * @brief 启动时把 .cgui 里的绑定应用一遍（目标控件先跟源对齐）
 * @param app 应用
 * @return void 无返回值
 */
void cg_bind_apply_all(cg_app_t *app)
{
    if (!app) return;

    for (cg_widget_t *w = app->widgets; w; w = w->next) {
        cg_widget_t *src;
        if (!w->bind_src) continue;
        src = cg_widget_by_id(app, w->bind_src);
        if (!src) {
            app->bad_binds++;
            cg_log(CG_LOG_WARN, "控件 \"%s\" 绑定到 \"%s\"，但没有这个 id 的控件",
                   w->id ? w->id : "-", w->bind_src);
            continue;
        }
        if (cg_bind_copy(src, w)) cg_bind_propagate(w);   /* 目标若还是别人的源，继续往下传 */
    }
}

/**
 * @brief 运行时绑定两个控件（覆盖原来 .cgui 里写的绑定）
 * @param src 源控件（变化的源头）
 * @param dst 目标控件（跟着 src 走）
 * @param two_way 非 0 时为双向同步（目标变化也会推回源）
 * @return int 0 成功；-1 参数不对（同一个控件、跨应用、控件没有 id）
 */
int cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way)
{
    if (!src || !dst || src == dst) return -1;
    if (!src->app || src->app != dst->app) return -1;
    if (!src->id || !*src->id) return -1;

    if (!dst->bind_src) dst->app->nbinds++;
    free(dst->bind_src);
    dst->bind_src = cg_str_dup(src->id);
    dst->bind_two = two_way ? 1 : 0;
    if (!dst->bind_src) return -1;

    cg_bind_copy(src, dst);     /* 立刻对齐一次，别等源下一次变化 */
    return 0;
}
