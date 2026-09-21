/**
 * @file    cg_doc.h
 * @brief   .cgui 文档模型
 * @details .cgui 的物理格式就是一个 JSON 文件，结构见 docs/CGUI-FORMAT.md。
 *          这里负责 JSON -> 控件树，以及控件树 -> stdout 的可读打印。
 */
#ifndef CG_DOC_H
#define CG_DOC_H

#include <stdio.h>
#include <stddef.h>

#include "cstackgui.h"

/* CG_COLOR_NONE（「不设置颜色」）定义在公共头 cstackgui.h 里，上面已经 include 进来，
 * 这里不再重复定义 —— 业务代码也需要它，单一定义才不会漂移。 */

/** 控件类型。新增类型时记得同步 cg_doc_kind.c 的 k_kinds 和画布端的 model.ts/KINDS。 */
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
    CG_K_TIMER,      /* 时钟（无窗口，按周期发 tick） */
    CG_K_CANVAS,     /* 画板（自绘 + cg_canvas_* 绘制命令） */
    CG_K_COLORPICK,  /* 颜色选择（色块按钮 + 系统选色对话框） */
    CG_K_BROWSER,    /* 网页浏览（系统 WebBrowser，实验性） */
    CG_K_BADGE,      /* 徽章（自绘小圆片） */
    CG_K_TAG,        /* 标签片（自绘圆角片，可点） */
    CG_K_RATE,       /* 评分（自绘五角星，点几星就是几分） */
    CG_K_PAGINATION, /* 分页（自绘 < 页码 >，点两侧翻页） */
    CG_K_CARD,       /* 卡片（圆角边框容器） */
    CG_K_SHAPE,      /* 外形框（矩形/圆角/椭圆，纯视觉） */
    CG_K_SPLITTER,   /* 分隔条（能拖动的分区边界：撑开前一块、后面让位、最后一块吃掉余量） */
    CG_K_TERMINAL,   /* 终端 / 日志视图（只读、按行着色、自动滚到底、行数有上限） */
    CG_K_CHART,      /* 折线 / 柱状图（实时曲线：滑动窗口 + 超像素密度时按列画 min/max） */
    CG_K_LED,        /* 状态灯（纯显示：灭 / 亮 / 闪；闪烁由控件自己定时重绘） */
    CG_K_METER,      /* 仪表盘（纯显示：圆弧量程 + 值弧 + 指针 + 中心数值） */
    CG_K_WINDOW,     /* 伪控件：窗口自身，只有 close 事件 */
    CG_K_COUNT
} cg_kind_t;

/** 类型表里"支持哪些事件"用的位。
 *  注意是 64 位：窗口事件加上后总数已超 32（见 cg_event_t），
 *  用 unsigned 会让高位静默丢失，绑了却没反应。 */
typedef unsigned long long cg_evmask_t;
#define CG_EV_BIT(e) (1ULL << (unsigned)(e))
#define CG_EV_CLICK      CG_EV_BIT(CG_EVENT_CLICK)
#define CG_EV_DBLCLICK   CG_EV_BIT(CG_EVENT_DBLCLICK)
#define CG_EV_CHANGE     CG_EV_BIT(CG_EVENT_CHANGE)
#define CG_EV_SUBMIT     CG_EV_BIT(CG_EVENT_SUBMIT)
#define CG_EV_SELECT     CG_EV_BIT(CG_EVENT_SELECT)
#define CG_EV_RELEASE    CG_EV_BIT(CG_EVENT_RELEASE)
#define CG_EV_EXPAND     CG_EV_BIT(CG_EVENT_EXPAND)
#define CG_EV_COLLAPSE   CG_EV_BIT(CG_EVENT_COLLAPSE)
#define CG_EV_CLOSE      CG_EV_BIT(CG_EVENT_CLOSE)
#define CG_EV_TICK       CG_EV_BIT(CG_EVENT_TICK)
#define CG_EV_CHECK      CG_EV_BIT(CG_EVENT_CHECK)
#define CG_EV_GOTFOCUS   CG_EV_BIT(CG_EVENT_GOTFOCUS)
#define CG_EV_LOSTFOCUS  CG_EV_BIT(CG_EVENT_LOSTFOCUS)
/* 窗口级事件 */
#define CG_EV_READY         CG_EV_BIT(CG_EVENT_READY)
#define CG_EV_DESTROY       CG_EV_BIT(CG_EVENT_DESTROY)
#define CG_EV_MOVE          CG_EV_BIT(CG_EVENT_MOVE)
#define CG_EV_SIZE          CG_EV_BIT(CG_EVENT_SIZE)
#define CG_EV_ACTIVATE      CG_EV_BIT(CG_EVENT_ACTIVATE)
#define CG_EV_DEACTIVATE    CG_EV_BIT(CG_EVENT_DEACTIVATE)
#define CG_EV_FIRSTACTIVATE CG_EV_BIT(CG_EVENT_FIRSTACTIVATE)
#define CG_EV_IDLE          CG_EV_BIT(CG_EVENT_IDLE)
#define CG_EV_TRAY          CG_EV_BIT(CG_EVENT_TRAY)
#define CG_EV_SHOW          CG_EV_BIT(CG_EVENT_SHOW)
#define CG_EV_HIDE          CG_EV_BIT(CG_EVENT_HIDE)
#define CG_EV_CLOSEQUERY    CG_EV_BIT(CG_EVENT_CLOSEQUERY)
#define CG_EV_MOUSEDOWN     CG_EV_BIT(CG_EVENT_MOUSEDOWN)
#define CG_EV_MOUSEUP       CG_EV_BIT(CG_EVENT_MOUSEUP)
#define CG_EV_RDOWN         CG_EV_BIT(CG_EVENT_RDOWN)
#define CG_EV_RUP           CG_EV_BIT(CG_EVENT_RUP)
#define CG_EV_MOUSEMOVE     CG_EV_BIT(CG_EVENT_MOUSEMOVE)
#define CG_EV_KEYDOWN       CG_EV_BIT(CG_EVENT_KEYDOWN)
#define CG_EV_KEYUP         CG_EV_BIT(CG_EVENT_KEYUP)
#define CG_EV_CHAR          CG_EV_BIT(CG_EVENT_CHAR)
#define CG_EV_WHEEL         CG_EV_BIT(CG_EVENT_WHEEL)

typedef struct {
    const char *name;      /* .cgui 里 type 字段的写法 */
    cg_kind_t   kind;
    int         container; /* 能否容纳子控件 */
    int         def_w, def_h;
    const char *def_text;
    /** 这个控件**真正**支持的事件（位掩码）。
     *  事件是按控件特性设计的：标签没有交互、进度条只能被写、树才有展开折叠，
     *  所以画布只该列出这里声明的事件，绑得上的也必须是这里有的。 */
    cg_evmask_t events;
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
    /** 细分格式：datepicker 用 "date"（默认）/ "time" / "datetime"；
     *  shape 用 "rect" / "roundrect" / "ellipse" */
    char    *format;
    /** 鼠标悬停提示（tooltip），所有控件通用；NULL / 空 = 无提示 */
    char    *hint;
    /** timer 的触发周期（毫秒），0 = 不运行 */
    int      period;
    /** 控件绑定：源控件 id（.cgui 的 bind.src）；NULL = 没绑 */
    char    *bind_src;
    /** 绑定是双向的（源变跟源走，自己也变时也推回源） */
    int      bind_two;
    /** table：允许多选行 */
    int      multi;
    /** table：显示行首复选框列 */
    int      check;
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

/** 程序配置（画布「程序配置」对话框填的那些）。
 *  编译时由画布生成的 <工程>.rc 写进 exe 的版本信息；运行时也能读（cg_app_info）。 */
typedef struct {
    char *product;    /* 程序名称 */
    char *desc;       /* 程序描述 */
    char *comments;   /* 程序备注 */
    char *version;    /* 程序版本号，四段 "1.0.0.0" */
    char *build;      /* 创建号（那个 0.0，单独留一份便于对照） */
    char *author;     /* 作者 */
    char *company;    /* 单位 / 公司 */
    char *copyright;  /* 版权声明 */
    char *address;    /* 联系地址 */
    char *postcode;   /* 邮政编码 */
    char *phone;      /* 电话 */
    char *fax;        /* 传真 */
    char *email;      /* 电子信箱 */
    char *homepage;   /* 主页地址 */
} cg_info_t;

/** 窗口属性（.cgui 的 app 节点；缺省值 = 保持 Windows 默认观感，不设就一切照旧） */
typedef struct {
    char       *title;
    int         width, height;
    unsigned    bg;
    /* ---- 位置与外观（对齐「属性」面板） ---- */
    int         x, y;          /* 窗口左上角；has_pos=0 时交给系统 CW_USEDEFAULT */
    int         has_pos;
    int         center;        /* 位置 = 居中 */
    char       *border;        /* "sizable"(默认) / "fixed" / "dialog" / "none" */
    int         control_box;   /* 控制按钮（系统菜单/关闭钮），默认 1 */
    int         min_box;       /* 最小化按钮，默认 1 */
    int         max_box;       /* 最大化按钮，默认 1 */
    int         topmost;       /* 总在最前 */
    int         taskbar;       /* 在任务栏中显示，默认 1 */
    int         visible;       /* 可视，默认 1 */
    int         enabled;       /* 禁止（0 = 建好就整体禁用），默认 1 */
    int         enter_tab;     /* 回车下移焦点（默认 0 = 回车当 submit） */
    int         esc_close;     /* Esc 键关闭窗口，默认 1 */
    int         tray;          /* 建托盘图标 */
    char       *icon;          /* 窗口图标（.ico 路径） */
    char       *cursor;        /* 鼠标指针："arrow"/"hand"/"ibeam"/"wait"/"cross"/"sizeall"/... */
    char       *class_name;    /* 窗口类名，默认 CStackGUIMain */
    char       *note;          /* 备注（画布记录，运行时不用） */
    cg_info_t   info;          /* 程序配置（版本信息） */
    cg_node_t  *root;          /* 虚拟根：children 即顶层控件，无矩形 */
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
