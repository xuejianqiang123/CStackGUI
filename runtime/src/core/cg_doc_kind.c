/**
 * @file    cg_doc_kind.c
 * @brief   控件类型表（k_kinds）与 cg_kind_* 查询
 * @details 新增一种控件 = 这张表加一行 + cg_win_ctl.c 的 class_of / style_of 补分支
 *          + 画布端 model.ts 的 KINDS 加一行；事件的位掩码要和 KIND_EVENTS 同步。
 *          查表一律按 kind 字段线性查找，**别拿数组下标当枚举值**
 *          （表是按控件逻辑分组排的，和枚举声明顺序故意解耦）。
 */
#include "cg_doc.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 类型表 */

/* 类型表。**事件按控件特性分配**，不是所有控件都有全套事件：
 *   - 能点的才有 click / dblclick
 *   - 能改内容的才有 change
 *   - 有"选择"概念的才有 select（列表 / 标签页 / 树 / 表格 / 日历）
 *   - 只有树有 expand / collapse，只有滑块有 release
 *   - 纯显示或只被程序写的（label / progress / statusbar / separator / 容器）一个事件都没有
 * 画布只列出这里声明的事件；.cgui 绑了不支持的事件时，运行时启动会警告、自检会报错。
 */
static const cg_kind_info_t k_kinds[] = {
    { "button",     CG_K_BUTTON,     0, 120,  40, "按钮", CG_EV_CLICK | CG_EV_DBLCLICK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "iconbutton", CG_K_ICONBUTTON, 0,  48,  48, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "label",      CG_K_LABEL,      0, 160,  24, "标签", 0 },
    { "link",       CG_K_LINK,       0, 160,  24, "链接", CG_EV_CLICK | CG_EV_DBLCLICK },
    { "switch",     CG_K_SWITCH,     0,  56,  32, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "edit",       CG_K_EDIT,       0, 200,  32, "",     CG_EV_CHANGE | CG_EV_SUBMIT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "multiedit",  CG_K_MULTIEDIT,  0, 240, 120, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "richtext",   CG_K_RICHTEXT,   0, 320, 160, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "spinner",    CG_K_SPINNER,    0, 100,  28, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "datepicker", CG_K_DATEPICKER, 0, 140,  28, "",     CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "monthcal",   CG_K_MONTHCAL,   0, 230, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK },
    { "checkbox",   CG_K_CHECKBOX,   0, 160,  28, "复选", CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "radio",      CG_K_RADIO,      0, 160,  28, "单选", CG_EV_CHANGE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "listbox",    CG_K_LISTBOX,    0, 220, 140, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "combo",      CG_K_COMBO,      0, 200, 200, "",     CG_EV_SELECT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "slider",     CG_K_SLIDER,     0, 200,  32, "",     CG_EV_CHANGE | CG_EV_RELEASE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "progress",   CG_K_PROGRESS,   0, 200,  20, "",     0 },
    { "progressring", CG_K_RING,     0, 120, 120, "",     0 },
    { "tabs",       CG_K_TABS,       1, 320, 220, "",     CG_EV_SELECT | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "tree",       CG_K_TREE,       0, 220, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_EXPAND | CG_EV_COLLAPSE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "table",      CG_K_TABLE,      0, 360, 180, "",     CG_EV_SELECT | CG_EV_DBLCLICK | CG_EV_CHECK | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS },
    { "statusbar",  CG_K_STATUS,     0, 400,  24, "",     0 },
    { "menu",       CG_K_MENU,       0,   0,   0, "",     CG_EV_CLICK },
    { "menubutton", CG_K_MENUBUTTON, 0, 130,  32, "菜单", CG_EV_CLICK },
    { "toolbar",    CG_K_TOOLBAR,    0, 400,  32, "",     CG_EV_CLICK },
    { "group",      CG_K_GROUP,      1, 260, 180, "分组", 0 },
    { "box",        CG_K_BOX,        1, 200, 120, "",     0 },
    { "separator",  CG_K_SEPARATOR,  0, 200,   2, "",     0 },
    { "image",      CG_K_IMAGE,      0, 200, 120, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "timer",      CG_K_TIMER,      0,   0,   0, "",     CG_EV_TICK },
    { "canvas",     CG_K_CANVAS,     0, 240, 160, "",     CG_EV_CLICK | CG_EV_DBLCLICK },
    { "colorpicker", CG_K_COLORPICK, 0,  64,  28, "",     CG_EV_CHANGE },
    { "browser",    CG_K_BROWSER,    0, 320, 240, "",     0 },
    { "badge",      CG_K_BADGE,      0,  48,  24, "99+",  0 },
    { "tag",        CG_K_TAG,        0,  72,  28, "标签", CG_EV_CLICK },
    { "rate",       CG_K_RATE,       0, 140,  32, "",     CG_EV_CHANGE },
    { "pagination", CG_K_PAGINATION, 0, 180,  32, "",     CG_EV_CHANGE },
    { "card",       CG_K_CARD,       1, 280, 180, "卡片", 0 },
    { "shape",      CG_K_SHAPE,      0, 120, 120, "",     0 },
    /* 伪控件：窗口自身。事件表与「启动窗口」的事件表对齐（见 cg_event_t 注释）。 */
    { "window",     CG_K_WINDOW,     1,   0,   0, "",
      CG_EV_CLOSE | CG_EV_READY | CG_EV_DESTROY | CG_EV_MOVE | CG_EV_SIZE |
      CG_EV_ACTIVATE | CG_EV_DEACTIVATE | CG_EV_FIRSTACTIVATE | CG_EV_IDLE |
      CG_EV_TRAY | CG_EV_SHOW | CG_EV_HIDE | CG_EV_CLOSEQUERY |
      CG_EV_MOUSEDOWN | CG_EV_MOUSEUP | CG_EV_DBLCLICK | CG_EV_RDOWN | CG_EV_RUP |
      CG_EV_MOUSEMOVE | CG_EV_GOTFOCUS | CG_EV_LOSTFOCUS |
      CG_EV_KEYDOWN | CG_EV_KEYUP | CG_EV_CHAR | CG_EV_WHEEL },
};

#define K_KIND_COUNT (sizeof(k_kinds) / sizeof(k_kinds[0]))

/** @brief 某个控件类型支不支持这个事件 */
/**
 * @brief 某个控件类型支不支持这个事件
 * @param type 控件类型名（"button"/"edit"...）
 * @param ev 事件枚举
 * @return int 1 = 支持，0 = 不支持或参数不对
 */
int cg_kind_supports_event(const char *type, cg_event_t ev)
{
    const cg_kind_info_t *ki = cg_kind_lookup(type);
    if (!ki || (int)ev < 0 || ev >= CG_EVENT_COUNT) return 0;
    return (ki->events & CG_EV_BIT(ev)) != 0;
}

/** @brief 某个控件类型支持的事件名列表，写成 "click/dblclick" 便于报错时提示 */
/**
 * @brief 列出某控件类型支持的事件名（"click/dblclick" 形式），报错时提示用
 * @param kind 控件类型枚举
 * @return const char* 事件名列表（内部静态缓冲）；类型未知返回"(该控件没有事件)"
 */
const char *cg_kind_event_list(cg_kind_t kind)
{
    static char buf[192];
    const cg_kind_info_t *ki = cg_kind_info(kind);
    size_t used;

    buf[0] = '\0';
    if (!ki) return buf;
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (!(ki->events & CG_EV_BIT(i))) continue;
        used = strlen(buf);
        if (used && used + 1 < sizeof(buf)) strncat(buf, "/", sizeof(buf) - used - 1);
        used = strlen(buf);
        if (used < sizeof(buf) - 1) strncat(buf, cg_event_name((cg_event_t)i), sizeof(buf) - used - 1);
    }
    if (!buf[0]) snprintf(buf, sizeof(buf), "(该控件没有事件)");
    return buf;
}

/**
 * @brief 按类型名查类型表
 * @param type .cgui 里的 type 字段写法
 * @return const cg_kind_info_t* 表项；没找到返回 NULL
 */
const cg_kind_info_t *cg_kind_lookup(const char *type)
{
    if (!type) return NULL;
    for (size_t i = 0; i < K_KIND_COUNT; i++) {
        if (strcmp(k_kinds[i].name, type) == 0) return &k_kinds[i];
    }
    return NULL;
}

/* 按 kind 字段找，**不要**用数组下标当枚举值：
 * 表是按控件逻辑分组排的，和枚举声明顺序故意解耦，
 * 省得以后插一个控件就把所有下标全带歪（事件名表就踩过这个坑）。 */
/**
 * @brief 按枚举查类型表（按 kind 字段线性查找，别用数组下标）
 * @param kind 控件类型枚举
 * @return const cg_kind_info_t* 表项；没找到返回 NULL
 */
const cg_kind_info_t *cg_kind_info(cg_kind_t kind)
{
    for (size_t i = 0; i < K_KIND_COUNT; i++) {
        if (k_kinds[i].kind == kind) return &k_kinds[i];
    }
    return NULL;
}

/**
 * @brief 枚举转类型名
 * @param kind 控件类型枚举
 * @return const char* 类型名；未知返回 "unknown"
 */
const char *cg_kind_name(cg_kind_t kind)
{
    const cg_kind_info_t *k = cg_kind_info(kind);
    return k ? k->name : "unknown";
}
