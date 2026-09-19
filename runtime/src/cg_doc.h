/* cg_doc.h -- .cgui 文档模型
 *
 * .cgui 的物理格式就是一个 JSON 文件，结构见 docs/CGUI-FORMAT.md。
 * 这里负责 JSON -> 控件树，以及控件树 -> stdout 的可读打印。
 */
#ifndef CG_DOC_H
#define CG_DOC_H

#include <stdio.h>
#include <stddef.h>

#include "cstackgui.h"

/** 「不设置颜色」：这个控件交给系统默认配色 */
#define CG_COLOR_NONE 0xFFFFFFFFu

/** 控件类型。新增类型时记得同步 cg_doc.c 的 k_kinds 和画布端的 model.ts/KINDS。 */
typedef enum {
    CG_K_BUTTON = 0,
    CG_K_LABEL,
    CG_K_EDIT,
    CG_K_MULTIEDIT,
    CG_K_CHECKBOX,
    CG_K_RADIO,
    CG_K_LISTBOX,
    CG_K_COMBO,
    CG_K_SLIDER,
    CG_K_PROGRESS,
    CG_K_GROUP,      /* 容器：子控件坐标相对它 */
    CG_K_BOX,        /* 纯色块 / 分隔用 */
    CG_K_SEPARATOR,
    CG_K_IMAGE,
    CG_K_SWITCH,     /* 开关（自绘） */
    CG_K_TABS,       /* 标签页（子控件按 page 分页） */
    CG_K_TREE,       /* 树 */
    CG_K_TABLE,      /* 表格（列表视图报表模式） */
    CG_K_STATUS,     /* 状态栏 */
    CG_K_SPINNER,    /* 数字微调（输入框 + 上下箭头） */
    CG_K_MENU,       /* 窗口菜单栏（没有矩形，挂在窗口上） */
    CG_K_MENUBUTTON, /* 按钮 + 弹出菜单 */
    CG_K_RICHTEXT,   /* 富文本（RICHEDIT，多行、能装大文本） */
    CG_K_DATEPICKER, /* 日期 / 时间选择（datetime 时是两个控件） */
    CG_K_TOOLBAR,    /* 工具栏（一排文字按钮） */
    CG_K_LINK,       /* 链接（可点、手型光标） */
    CG_K_ICONBUTTON, /* 图标按钮（BS_OWNERDRAW 画 src 图片） */
    CG_K_MONTHCAL,   /* 月历 */
    CG_K_RING,       /* 环状进度（自绘） */
    CG_K_WINDOW,     /* 伪控件：窗口自身，只有 close 事件 */
    CG_K_COUNT
} cg_kind_t;

/** 类型表里"支持哪些事件"用的位 */
#define CG_EV_BIT(e) (1u << (unsigned)(e))
#define CG_EV_CLICK      CG_EV_BIT(CG_EVENT_CLICK)
#define CG_EV_DBLCLICK   CG_EV_BIT(CG_EVENT_DBLCLICK)
#define CG_EV_CHANGE     CG_EV_BIT(CG_EVENT_CHANGE)
#define CG_EV_SUBMIT     CG_EV_BIT(CG_EVENT_SUBMIT)
#define CG_EV_SELECT     CG_EV_BIT(CG_EVENT_SELECT)
#define CG_EV_RELEASE    CG_EV_BIT(CG_EVENT_RELEASE)
#define CG_EV_EXPAND     CG_EV_BIT(CG_EVENT_EXPAND)
#define CG_EV_COLLAPSE   CG_EV_BIT(CG_EVENT_COLLAPSE)
#define CG_EV_CLOSE      CG_EV_BIT(CG_EVENT_CLOSE)

typedef struct {
    const char *name;      /* .cgui 里 type 字段的写法 */
    cg_kind_t   kind;
    int         container; /* 能否容纳子控件 */
    int         def_w, def_h;
    const char *def_text;
    /** 这个控件**真正**支持的事件（位掩码）。
     *  事件是按控件特性设计的：标签没有交互、进度条只能被写、树才有展开折叠，
     *  所以画布只该列出这里声明的事件，绑得上的也必须是这里有的。 */
    unsigned    events;
} cg_kind_info_t;

const cg_kind_info_t *cg_kind_lookup(const char *type);
const cg_kind_info_t *cg_kind_info(cg_kind_t kind);
const char           *cg_kind_name(cg_kind_t kind);
/** 某个控件类型支不支持这个事件 */
int                   cg_kind_supports_event(const char *type, cg_event_t ev);
/** 某个控件类型支持的事件名列表，写成 "click/dblclick" 便于报错时提示 */
const char           *cg_kind_event_list(cg_kind_t kind);

/* ------------------------------------------------------------ 样式 */

typedef enum {
    CG_ALIGN_LEFT = 0,
    CG_ALIGN_CENTER,
    CG_ALIGN_RIGHT
} cg_align_t;

typedef struct {
    char     *font;
    int       size;    /* 磅 */
    int       bold;
    unsigned  fg;      /* 0xRRGGBB，或 CG_COLOR_NONE */
    unsigned  bg;
    int       radius;  /* 圆角，原生控件不支持时忽略（自绘后端用） */
    cg_align_t align;
} cg_style_t;

/* --------------------------------------------------- 结构化条目
 * 这几种控件的内容不是一句 text 能表达的，.cgui 里各有一个字段描述：
 *   listbox / combo / tabs / statusbar -> "items":  ["第一项", "第二项"]
 *   tree                               -> "nodes":  [{ "text":"根", "children":[...] }]
 *   table                              -> "columns": [{ "title":"姓名", "w":120 }]
 *                                         "rows":    [["张三","18"], ["李四","20"]]
 */

/** 树节点 / 菜单项（可嵌套）。
 *  tree 用 text + expanded，menu 用 text + id + checked，text 写 "-" 表示分隔线。 */
typedef struct cg_item {
    char             *text;
    char             *id;         /* 菜单项标识，回调里用 cg_menu_last_id 取回 */
    int               expanded;   /* 树：初始展开 */
    int               checked;    /* 菜单：勾选标记 */
    struct cg_item  **children;
    size_t            nchildren;
} cg_item_t;

/** 表格列 */
typedef struct {
    char *title;
    int   width;
} cg_col_t;

typedef struct cg_node {
    char    *type;
    char    *id;
    char    *text;
    int      x, y, w, h;
    cg_style_t style;
    int      visible, enabled, checked, value, vmin, vmax;
    /** 属于标签页的第几页（tabs 的子控件用） */
    int      page;
    /** image 的图片路径（相对 .cgui 目录、exe 目录或绝对） */
    char    *src;
    /** 细分格式：datepicker 用 "date"（默认）/ "time" / "datetime" */
    char    *format;
    /** 各类事件绑定的回调名，NULL 表示没绑 */
    char    *ev[CG_EVENT_COUNT];

    /* 结构化内容 */
    char      **items;    size_t nitems;
    cg_item_t **nodes;    size_t nnodes;
    cg_col_t   *cols;     size_t ncols;
    char     ***rows;     size_t nrows;   size_t row_cells;

    struct cg_node  *parent;
    struct cg_node **children;
    size_t           nchildren;
} cg_node_t;

typedef struct {
    char       *title;
    int         width, height;
    unsigned    bg;
    cg_node_t  *root;      /* 虚拟根：children 即顶层控件，无矩形 */
} cg_doc_t;

/** 解析 .cgui 文本。失败返回 NULL 并写错误信息。 */
cg_doc_t *cg_doc_parse(const char *json, char *err, size_t errsz);
/** 释放文档内容，但 doc 本身（可能嵌在别的结构里，或是栈变量）不动 */
void      cg_doc_release(cg_doc_t *doc);
/** 释放内容并释放 doc 本身；只给 cg_doc_parse 的返回值用 */
void      cg_doc_free(cg_doc_t *doc);

/** 以缩进树的形式打印到 out（自检 / 排查用） */
void cg_doc_dump(const cg_doc_t *doc, FILE *out);

/** 读文件到堆内存（UTF-8 原文），失败返回 NULL */
char *cg_read_file(const char *path);

#endif /* CG_DOC_H */
