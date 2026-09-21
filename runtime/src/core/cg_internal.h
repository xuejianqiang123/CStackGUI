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
    int          hot;        /* 鼠标悬停中（自绘外观用，FocusProc 维护） */
    int          pressed;    /* 鼠标按下中（自绘外观用，FocusProc 维护） */
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

    /* 最近一次事件的参数（鼠标 / 按键 / 滚轮 / 托盘）。
     * 回调签名固定，参数只能这样带给业务，读法见 cg_event_x()/cg_event_key() 等。 */
    int          ev_x, ev_y;       /* 客户区坐标（逻辑像素） */
    int          ev_keys;          /* 1=Ctrl 2=Shift 4=Alt */
    int          ev_key;           /* 虚拟键码 / 字符码 */
    int          ev_delta;         /* 滚轮滚动量 */
    int          ev_action;        /* 托盘动作：1=左键单击 2=左键双击 3=右键单击 */

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
/** 按原生窗口句柄（HWND，以 void* 传入）反查控件；同时比对 native 与 native2；
 *  找不到返回 NULL。自绘外观层（cg_skin.c）用它把 WM_DRAWITEM 的 hwndItem 映射到控件。 */
cg_widget_t *cg_widget_by_hwnd(cg_app_t *app, void *hwnd);

/** 某个控件"背后"是什么颜色（0xRRGGBB）：主窗口上的控件 = 窗口底色；分组框 / 容器块 /
 *  卡片 / 图形里的 = 面板 style.bg（没设就用系统面色）；标签页里的 = 采样到的页体色。
 *  无底色标签、以及"清掉自身底色"的自绘按钮/开关/勾选框都该用它，
 *  否则控件在带底色的面板里会露出一块窗口底色的方块。 */
unsigned cg_win_behind_rgb(cg_app_t *app, void *hwnd);

/** 由文档树构建运行时控件树（不含原生句柄），失败返回 NULL */
cg_widget_t *cg_build_widgets(cg_app_t *app);

/* ---------------------------------------------- 日志与诊断（cg_log.c / cg_win_diag.c）
 * GUI 版没有控制台，光 fprintf(stderr, ...) 用户看不到，所以要有文件日志与错误弹窗。 */

/** 打开日志文件（追加）；path 为空表示只写 stderr。后端启动时调 */
void cg_log_open(const char *path);
/** 关闭日志文件（进程收尾） */
void cg_log_close(void);
/** 当前日志文件路径；没打开返回 NULL（弹窗里用它告诉用户日志在哪） */
const char *cg_log_file(void);

/** 起始诊断（win32 实现）：定位日志文件、轮转、装未处理异常过滤器。
 *  cg_app_run / cg_app_selftest 一开始就调，这样"找不到界面文件"也留得下记录。 */
void cg_diag_init(void);
/** 弹一个阻塞式错误框（win32 实现）。只在 cg_app_run（有界面）里用，自检别弹 */
void cg_fatal_dialog(const char *title, const char *text);

/** 启动期"回调名没在 C 侧登记"的收集（cg_event.c）：cg_app_run 汇总弹一次。
 *  名字写错不编译报错，是新手最容易卡住的地方，光在 stderr 飘一行不够。 */
int cg_unresolved_count(void);
const char *cg_unresolved_name(int i);

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

/* ---------------------------------------------- cg_app.c 拆分后的跨模块接口
 * 原来这几个是 cg_app.c 里的 static 助手，拆成 cg_widget.c / cg_event.c 之后
 * 需要跨模块调用。名字都加了 cg_ 前缀，避免和业务代码里的同名符号撞车。 */

/** @brief 复制字符串到堆内存（调用方 free），失败返回 NULL */
char *cg_str_dup(const char *s);
/** @brief 把运行时控件挂到父控件末尾，child->parent 设好；失败就静默丢弃 */
void cg_widget_attach_child(cg_widget_t *parent, cg_widget_t *child);
/** @brief 在控件的树模型末尾追加一个节点，返回新节点下标（-1 表示失败） */
int cg_widget_tree_push(cg_widget_t *w, int parent, const char *text, int expanded);
/** @brief 把控件逻辑状态推给后端（无后端时是空操作） */
void cg_widget_sync(cg_widget_t *w);

#endif /* CG_INTERNAL_H */
