/**
 * @file    cg_internal.h
 * @brief   运行时内部结构（控件/应用/后端接口），业务代码不应 include
 */
#ifndef CG_INTERNAL_H
#define CG_INTERNAL_H

#include "cstackgui.h"
#include "cg_doc.h"

typedef struct cg_app cg_app_t;

/** 后端提供的杂项：有控制台时把它切到 UTF-8（printf 中文不乱码）。
 *  没有控制台（-mwindows 的 release 版、重定向输出）时是空操作。 */
void cg_win_console_utf8(void);

/** 树的一个节点（扁平存放，parent 是数组下标，-1 表示根） */
typedef struct {
    char *text;
    int   parent;
    int   expanded;
} cg_tree_row_t;

/** 画板的一条绘制命令（WM_PAINT 时按记录顺序回放） */
typedef struct cg_canvas_op {
    int      type;      /* CG_CANVAS_OP_* */
    unsigned color;     /* 0xRRGGBB */
    int      penw;
    int      x1, y1, x2, y2;
    int      fill;      /* 1 = 实心 */
    char    *text;      /* CG_CANVAS_OP_TEXT 用 */
} cg_canvas_op_t;

#define CG_CANVAS_OP_LINE    1
#define CG_CANVAS_OP_RECT    2
#define CG_CANVAS_OP_ELLIPSE 3
#define CG_CANVAS_OP_TEXT    4

/** 菜单的一项（扁平存放，父项的下标保证比自己小）。
 *  cmd 是运行时分配的菜单命令 id，WM_COMMAND 回来的 LOWORD 就是它；
 *  弹出项（有子项）自己不可点，cmd 为 0。text 为 "-" 表示分隔线。 */
typedef struct {
    int   cmd;
    char *text;
    char *id;       /* .cgui 里写的菜单项标识 */
    int   checked;
    int   parent;   /* 父项下标，-1 = 顶层 */
    int   popup;    /* 1 = 有子项，是个下拉 */
} cg_menu_row_t;

/** 运行时控件：.cgui 节点 + 原生句柄 + 回调 + 状态 */
struct cg_widget {
    cg_node_t   *node;       /* 静态控件指向文档节点；动态控件为 NULL */
    char        *id;
    char        *type;
    char        *text;

    int          x, y, w, h;
    int          visible, enabled, checked, value, vmin, vmax;
    cg_style_t   style;

    void        *user;
    cg_app_t    *app;
    cg_widget_t *parent;
    cg_widget_t **children;  /* 容器控件 */
    size_t       nchildren;

    char       **items;      /* listbox / combo / tabs 标签 / statusbar 分段 */
    int          nitems;
    int          items_dirty;/* 需要后端重建条目 */
    int          scroll_bottom;

    /* 树：扁平数组 + 父下标（-1 为根），materialize 时逐层插入 */
    cg_tree_row_t *tree;
    int          ntree;
    int          tree_sel;      /* 当前选中项下标；-1 无 */

    /* 表格 */
    cg_col_t    *cols;
    int          ncols;
    char      ***rows;
    int          nrows;
    int          row_cells;
    int          table_sel;     /* 当前选中行；-1 无 */

    /* 标签页：选中页用 value；本控件属于父标签页的第几页用 page */
    int          page;

    /* 图片 */
    char        *src;
    void        *bitmap;        /* HBITMAP 缓存 */
    char        *bitmap_src;    /* 当前位图是从哪个路径加载的（后端维护） */
    int          image_failed;

    /* 悬停提示（tooltip）；运行时用 TTN_GETDISPINFO 现取，改了立即生效 */
    char        *hint;

    /* 时钟（timer）：周期毫秒，0 = 停。Win32 定时器 id 复用 ctrl_id */
    int          period;

    /* 表格增强：多选 / 勾选列 / 行数值 / 行图标 */
    int          multi;
    int          check;
    unsigned char *row_checked;  /* 每行勾选状态（随行数增长） */
    int            row_cap;      /* row_checked/row_val 的容量 */
    int           *row_val;      /* 每行业务数值（lParam） */
    char         **row_icon;     /* 每行图标路径（NULL = 无） */
    int            row_icon_n;
    void          *imglist;      /* HIMAGELIST，惰性创建 */

    /* 画板：绘制命令记录（重绘时按序回放） */
    struct cg_canvas_op *ops;
    int            nops, op_cap;
    unsigned       canvas_color;   /* 当前画笔颜色 0xRRGGBB */
    int            canvas_penw;    /* 当前线宽 */

    /* 细分格式（datepicker："date" / "time" / "datetime"；shape："rect"/"roundrect"/"ellipse"） */
    char        *format;

    /* 菜单（menu / menubutton） */
    cg_menu_row_t *menu;
    int          nmenu;
    char        *menu_last_id;    /* 最近点中的菜单项标识 */
    char        *menu_last_text;

    /* 工具栏：逻辑下标 -> 按钮命令 id */
    int         *tool_ids;
    int          ntool;

    void        *native2;       /* 需要第二个原生窗口的控件（spinner = edit + updown） */

    /* 树 / 表格：逻辑下标 -> 原生 item 句柄（后端维护，重建条目时刷新） */
    void       **item_map;
    int          item_map_n;

    void        *native;     /* HWND */
    int          ctrl_id;    /* Win32 控件 ID，用于 WM_COMMAND 反查 */
    int          is_window;  /* 代表主窗口本身的伪控件 */

    cg_event_fn  handlers[CG_EVENT_COUNT];
    unsigned long last_click_tick; /* 双击探测 */
    int          dbl_pending;      /* 上一次 click 已回调，第二次要补 dblclick */

    cg_widget_t *next;       /* 全局链表 */
};

struct cg_app {
    cg_doc_t     doc;
    cg_widget_t *widgets;
    cg_widget_t *window;     /* 代表主窗口的伪控件，承接 CG_EVENT_CLOSE */
    int          next_ctrl_id;
    int          next_menu_cmd;
    /** .cgui 里绑了该控件不支持的事件：自检据此报错 */
    int          unsupported_events;
    /** close 回调里调 cg_cancel_close() 会把这里置 1，窗口就不关了 */
    int          close_cancel;

    char        *cgui_path;  /* 实际用到的 .cgui 路径（可能是自动找到的） */
    char        *cgui_dir;   /* 它所在目录：image 的相对路径按这里解释 */

    const cg_binding_t *bindings;
    int          dblclick_suppress_click;
    int          quit;
    int          scale;      /* 逻辑像素 -> 物理像素：dpi/96 */
    void        *hwnd;
    void        *natives;    /* 后端私有数据 */
    unsigned long start_tick;
};

/** 依名字在绑定表里找函数；找不到返回 NULL 并打印一次警告 */
cg_event_fn cg_resolve_binding(cg_app_t *app, const char *name);

/** 事件派发中枢：后端拿到原生事件后调它 */
int cg_dispatch(cg_widget_t *w, cg_event_t ev);

/** 菜单项被点中：记下是哪一项，再给对应控件派发 click。返回 1 表示处理了。
 *  menu 走 WM_COMMAND（lParam 为 0），menubutton 走 TrackPopupMenu 的返回值。 */
int cg_dispatch_menu_cmd(cg_app_t *app, int cmd);

/** 按 id 找到运行时控件；找不到返回 NULL */
cg_widget_t *cg_widget_by_id(cg_app_t *app, const char *id);
/** 按 Win32 控件 ID 找控件；找不到返回 NULL */
cg_widget_t *cg_widget_by_ctrl_id(cg_app_t *app, int ctrl_id);

/** 由文档树构建运行时控件树（不含原生句柄），失败返回 NULL */
cg_widget_t *cg_build_widgets(cg_app_t *app);

/* -------------------------------------------------- 后端接口 */

typedef struct cg_backend {
    const char *name;
    int  (*init)(cg_app_t *app);          /* 建窗口 + 所有原生控件 */
    int  (*run)(cg_app_t *app);           /* 消息循环 */
    void (*shutdown)(cg_app_t *app);
    void (*sync)(cg_widget_t *w);         /* 把逻辑状态推给原生控件 */
    void (*attach)(cg_widget_t *w);       /* 为控件创建原生句柄 */
    void (*detach)(cg_widget_t *w);
    void (*set_title)(cg_app_t *app, const char *title);
} cg_backend_t;

extern const cg_backend_t cg_backend_win32;
extern const cg_backend_t *cg_backend_active;

/** 后端子控件注册回调：原生事件 -> cg_dispatch */
void cg_backend_notify(cg_widget_t *w, cg_event_t ev);

/** 分配控件 ID（后端建控件时用），从 1001 起递增 */
int cg_alloc_ctrl_id(cg_app_t *app);

/** 分配菜单命令 ID。菜单走 WM_COMMAND 的 LOWORD，必须和控件 ID 分开，
 *  所以从 0x8000 起单独发号，避免撞上控件的 1001 起那一片。 */
int cg_alloc_menu_cmd(cg_app_t *app);

/** 毫秒级时基（后端提供），用于双击判定与定时器 */
unsigned long cg_ticks(void);
/** 系统双击间隔，默认 500ms */
unsigned long cg_double_click_ms(void);

/* 定时器（cg_timer.c）：后端心跳驱动 */
void cg_timers_tick(cg_app_t *app);
void cg_timers_clear(cg_app_t *app);

/** 当前 exe 的完整路径（含文件名），成功返回 0 */
int cg_exe_path(char *buf, size_t size);

/* Win32 后端内部的联动入口（cg_app.c 的公共 API 会调）：
 * hint / period 改完要动 tooltip 和 SetTimer，那些句柄只有后端知道。 */
void cg_win_set_hint(cg_widget_t *w);
void cg_win_restart_timer(cg_widget_t *w);

/** 新建一个运行时控件（内部用） */
cg_widget_t *cg_widget_new(cg_app_t *app, const char *type, const char *id,
                           int x, int y, int w, int h, const char *text);
void cg_widget_free(cg_widget_t *w);

/** 表格行数变多时把行级附加数组（勾选/数值/图标）一起补长到 want */
void cg_table_rows_grow(cg_widget_t *w, int want);

#endif /* CG_INTERNAL_H */
