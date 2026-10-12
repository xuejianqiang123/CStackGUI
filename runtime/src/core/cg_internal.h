/**
 * @file    cg_internal.h
 * @brief   运行时内部结构（控件/应用/后端接口），业务代码不应 include
 */
#ifndef CG_INTERNAL_H
#define CG_INTERNAL_H

#include "cstackgui.h"
#include "cg_doc.h"

typedef struct cg_app cg_app_t;

/**
 * @brief 有控制台时把它切到 UTF-8（printf 中文不乱码）
 * @return void 无返回值
 * @note 没有控制台（-mwindows 的 release 版、重定向输出）时是空操作
 */
void cg_win_console_utf8(void);

/** 树的一个节点（扁平存放，parent 是数组下标，-1 表示根） */
typedef struct {
    char *text;
    int   parent;
    int   expanded;
} cg_tree_row_t;

/** 图标列表（iconlist）的一行：图标 + 主标题 + 副标题 + 右侧状态文字 + 勾选态。
 *  前四个为 NULL / 空串都表示"这一段没有"，绘制时各自跳过。 */
typedef struct {
    char *icon;    /**< 图标文件路径（png/jpg/bmp/gif；NULL = 无图标，画占位框） */
    char *title;   /**< 主标题（大字，单行，超长省略号裁剪） */
    char *sub;     /**< 副标题（第二行小灰字，可空） */
    char *status;  /**< 右侧状态文字（右对齐，可空） */
    int   checked; /**< 勾选态（控件的 check 字段决定显不显示复选框列） */
    void *bmp;     /**< 后端：图标 HBITMAP 缓存（win32 维护，core 只经 cgw_iconlist_row_free 碰它） */
    char *bmp_src; /**< 后端：位图对应的路径（变了才重新加载，仿 sync_bitmap 的口径） */
} cg_ilrow_t;

/**
 * @brief 释放一行的后端图标位图（DeleteObject + 释放 bmp_src），行的字符串归 core
 * @param row 目标行；NULL 安全
 * @return void 无返回值
 * @note 由 cg_widget_iconlist.c 的 clear/set_row/free 调；无窗（自检）时 bmp 恒 NULL，是空操作
 */
void cgw_iconlist_row_free(cg_ilrow_t *row);

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
    /** 引用的动作名（.cgui 里菜单项的 action）；NULL = 不是动作项。
     *  有它时：文字取动作的 text（非空时覆盖 text）、点击触发动作、
     *  启用与勾选跟随动作。 */
    char *action;
    int   checked;
    /** 可用（1）/ 灰掉（0）。**建表时必须显式置 1**：memset 出来的 0 是"禁用"，
     *  与 .cgui 的缺省语义（可用）相反。动作项由动作的 enabled 覆盖它。 */
    int   enabled;
    int   parent;   /* 父项下标，-1 = 顶层 */
    int   popup;    /* 1 = 有子项，是个下拉 */
} cg_menu_row_t;

/* ------------------------------------------------------------ 快捷键（accel）
 * 解析放在 core（纯逻辑、能脱窗单测），**结果与平台无关** —— 这里只有「哪个键 +
 * 什么修饰键」，映射成 Win32 的虚拟键码是后端的事（core 不许 include windows.h，
 * check-layers.mjs 会拦）。
 */

#define CG_MOD_CTRL   1u
#define CG_MOD_SHIFT  2u
#define CG_MOD_ALT    4u

/** 键：三组"基址 + 偏移" + 一批具名键。放在一个 int 里，后端 switch 映射。 */
typedef enum {
    CG_ACCEL_NONE = 0,
    CG_ACCEL_A    = 1,     /* +0..25 = A..Z */
    CG_ACCEL_0    = 100,   /* +0..9  = 0..9 */
    CG_ACCEL_F1   = 200,   /* +0..23 = F1..F24 */
    CG_ACCEL_ESC  = 300,
    CG_ACCEL_ENTER,
    CG_ACCEL_TAB,
    CG_ACCEL_SPACE,
    CG_ACCEL_BACK,      /* 退格 */
    CG_ACCEL_DELETE,
    CG_ACCEL_INSERT,
    CG_ACCEL_HOME,
    CG_ACCEL_END,
    CG_ACCEL_PAGEUP,
    CG_ACCEL_PAGEDOWN,
    CG_ACCEL_LEFT,
    CG_ACCEL_RIGHT,
    CG_ACCEL_UP,
    CG_ACCEL_DOWN
} cg_accel_key_t;

typedef struct {
    int      key;    /* cg_accel_key_t（A/0/F1 是基址 + 偏移，见上） */
    unsigned mods;   /* CG_MOD_* 的按位或 */
} cg_accel_t;

/**
 * @brief 解析快捷键文本：[Ctrl+][Shift+][Alt+]键
 * @param accel 快捷键文本，如 "Ctrl+Shift+S"；修饰键大小写不敏感、可任意顺序、重复算一次
 * @param out 解析结果的出参，只有成功时才写
 * @return int 1 解析成功并写出 out；0 语法 / 键名 / 组合不合法
 * @note 接受的键：A-Z / 0-9 / F1-F24 / Esc / Enter / Tab / Space / Back / Delete / Insert /
 *       Home / End / PageUp / PageDown / Left / Right / Up / Down；
 *       **不接受无修饰键的单个字母或数字**（裸 S 会让用户没法往输入框里打 s）
 */
int cg_accel_parse(const char *accel, cg_accel_t *out);

/**
 * @brief 规范化快捷键的显示形式（修饰键顺序固定 Ctrl+Shift+Alt）
 * @param accel 快捷键文本，语义同 cg_accel_parse
 * @param buf 输出缓冲
 * @param cap buf 的字节数
 * @return int 1 成功（如 "Ctrl+Shift+S"）；0 解析不了（buf 写成空串）
 */
int cg_accel_format(const char *accel, char *buf, size_t cap);

/** 运行时的动作（.cgui 顶层 actions 的一项 + 运行期状态）。
 *  字符串都是**自己持有一份**（不指向文档，避免文档生命周期耦合）；
 *  accel 存规范化后的显示形式，没有快捷键时是空串。 */
typedef struct {
    char       *name;
    char       *text;
    char       *accel;     /* 规范化后的 "Ctrl+S"；"" = 无快捷键 */
    char       *handler;
    cg_event_fn fn;        /* 启动时解析好的回调；NULL = C 侧没登记（已记进 unresolved） */
    int         enabled;
    int         checked;
    int         accel_ok;  /* accel 解析成功（false 且 accel 非空 = 启动时警告 + 自检报错） */
    cg_accel_t  key;
    int         accel_cmd; /* 加速键表的命令 id；0 = 没有快捷键 */
} cg_action_row_t;

/** 余量分配的输入输出单元（core/cg_layout.c 用；见 .codebuddy/plans/grow-layout.md §2） */
typedef struct {
    int x, y, w, h;     /* 进来是基准坐标，出去是算好的坐标 */
    int grow;           /* 权重 0..99；0 = 不吃余量 */
} cg_layout_box_t;

/**
 * @brief 按 grow 把"容器尺寸变化"分配到各 box
 * @param boxes 布局单元数组；进来是基准坐标，出去是算好的坐标
 * @param count boxes 的个数
 * @param base_w 容器的基准宽（设计宽）
 * @param base_h 容器的基准高（设计高）
 * @param now_w 容器的当前宽
 * @param now_h 容器的当前高
 * @return void 无返回值
 * @note 纯整数算术、不碰 Win32，所以可脱窗单测
 */
void cg_layout_apply(cg_layout_box_t *boxes, int count,
                     int base_w, int base_h, int now_w, int now_h);

/** 运行时控件：.cgui 节点 + 原生句柄 + 回调 + 状态 */
struct cg_widget {
    cg_node_t   *node;       /* 静态控件指向文档节点；动态控件为 NULL */
    char        *id;
    char        *type;
    char        *text;

    int          x, y, w, h;
    int          grow;       /* 余量分配权重（来自 .cgui 的 grow；算法见 cg_layout.c） */
    /* 布局基准：控件创建时（= .cgui 设计坐标）记下来，每次窗口尺寸变化都从基准重算，
     * 不做增量累加 —— 否则来回拉伸会漂移。见 .codebuddy/plans/grow-layout.md */
    int          base_x, base_y, base_w, base_h;
    /* 我们上一次写下去的坐标（判断"有没有人在我们之外动过它"） */
    int          out_x, out_y, out_w, out_h;
    int          have_base;
    int          visible, enabled, checked, value, vmin, vmax;
    int          hot;        /* 鼠标悬停中（自绘外观用，FocusProc 维护） */
    int          pressed;    /* 鼠标按下中（自绘外观用，FocusProc 维护） */
    cg_style_t   style;

    void        *user;
    cg_app_t    *app;
    cg_widget_t *parent;
    cg_widget_t **children;  /* 容器控件 */
    size_t       nchildren;

    char       **items;      /* listbox / combo / tabs 标签 / statusbar 分段 / terminal 的行 */
    int          nitems;
    int          items_dirty;/* 需要后端重建条目 */
    /** 表格批量更新计数：> 0 时加行 / 清空只记脏、不重建（见 cg_table_begin_update） */
    int          items_batch;
    int          scroll_bottom;
    /** statusbar 专用：上一次设过的分段布局（段数 + 客户区宽）。SB_SETPARTS 会让整条状态栏
     *  重算重画，而状态栏是"数字一跳就刷一次"的控件 —— 布局没变就别重设（见 cg_win_sync.c）。 */
    int          sb_n;
    int          sb_w;
    /** terminal 专用：后端已经推送了多少行。比 nitems 大 = 中途裁剪/清空过，
     *  后端得整体重灌（见 cg_win_sync.c 的 sync_terminal）。 */
    int          term_pushed;
    /** chart 专用：拍平的点数据（chart_nseries × chart_cap）与每个系列已填的点数。
     *  系列名存在 items[] 里；缓冲按需建（见 cg_chart_ensure，在 cg_widget_gauge.c）。 */
    int         *chart_pts;
    int         *chart_fill;
    int          chart_nseries;
    int          chart_cap;

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
    int          table_col;     /* 最近一次双击的列；-1 = 还没双击过（见 cg_table_last_col） */

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

    /* 串口组件（serial，无窗口）：参数从 .cgui 节点拷来；打开后的 cg_serial_t* 放
     * native2（数据到达的轮询定时器 id 也复用 ctrl_id，见 cg_win_restart_serial） */
    char         sport[64];
    int          s_baud, s_bits, s_stop, s_parity, s_auto;

    /* 控件绑定（cg_bind.c）：以谁为源；bind_two 时自己的变化也推回源 */
    char        *bind_src;
    int          bind_two;

    /* 动作引用（.cgui 的 action）：点击交给它，文字与启用跟随它。
     * NULL = 不是动作控件。运行时自己持有一份名字（动态控件没有文档节点）。 */
    char        *action;
    /** **窗口伪控件专用**：最近一次被激活的动作名（cg_action_name(self) 读它）。
     *  与 menu_last_id 同一套路：回调签名固定，参数只能这样带出去。 */
    char        *action_last;

    /* 表格增强：多选 / 勾选列 / 行数值 / 行图标 */
    int          multi;
    int          check;
    /* dialog：打开时居中于主窗口（缺省 1；0 = 按 rect.x/y 屏幕坐标摆）。
       从 doc 节点拷来（cg_widget.c 的 build），模态打开时读它（cg_win_modal.c） */
    int          center;

    /* edit（单行输入）的输入框行为：建原生窗口时由 cgw_style_of 拼成 ES_ 与 WS_ 样式位，
       maxlen 例外（它只能靠 EM_SETLIMITTEXT 设，见 cg_win_create.c） */
    int          multiline;
    int          scroll;
    int          maxlen;
    int          conv;
    int          input;
    int          border;
    int          spin;
    unsigned char *row_checked;  /* 每行勾选状态（随行数增长） */
    int            row_cap;      /* row_checked/row_val 的容量 */
    int           *row_val;      /* 每行业务数值（lParam） */
    char         **row_icon;     /* 每行图标路径（NULL = 无） */
    int            row_icon_n;
    void          *imglist;      /* HIMAGELIST，惰性创建 */

    /* 图标列表（iconlist）：行数据在 il_rows（唯一真相，绘制时读）；
       check 字段复用 table 的"显示复选框列"语义，这里不再单开一个 */
    cg_ilrow_t *il_rows;
    int         il_n;
    int         il_cap;
    int         il_scroll;   /* 首个可见行（行单位；滚动条与滚轮都动它） */
    int         il_hover;    /* 悬停行（-1 = 无），WM_MOUSEMOVE 维护 */

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

    void        *native2;       /* 需要第二个原生窗口的控件（数值框 = edit + updown） */

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
    /** 当前是不是深色（**有效值**，不是 .cgui 里的原始意图）。
     *  .cgui 的 app.dark 写 true/false/"auto"：前两者直接定，`"auto"` 由**后端**在
     *  建窗口时读系统主题解析（core 不碰注册表）。运行时切换走 cg_theme_set_dark()。
     *  皮肤层画"默认色"时读它（w->app->dark）；控件自己写了 fg/bg 的一律以自己为准。 */
    int          dark;
    /** 本次"文件被拖进窗口"的路径列表（UTF-8），**只在 CG_EVENT_DROP 的回调期间有效**：
     *  后端取完路径填进来（cg_app_drop_put）→ 派发 drop 事件 → 回调返回后立刻清掉。
     *  对外的取用方式是 cg_drop_count(app) / cg_drop_file(app, i)。 */
    char       **drop_files;
    int          ndrop;
    cg_widget_t *window;     /* 代表主窗口的伪控件，承接 CG_EVENT_CLOSE */
    int          next_ctrl_id;
    int          next_menu_cmd;
    /** .cgui 里绑了该控件不支持的事件：自检据此报错 */
    int          unsupported_events;
    /** 绑定个数：为 0 时事件派发完全不做绑定传播，等于没有这个功能 */
    int          nbinds;
    /** bind.src 指向了不存在的控件：启动时警告，自检据此报错 */
    int          bad_binds;
    /** 动作表（.cgui 顶层 actions；cg_action_build() 建，收尾时 cg_action_release()） */
    cg_action_row_t *actions;
    int          nactions;
    /** 动作相关的三类问题计数：引用了不存在的动作名 / 该类型不支持 click 却写了
     *  action / 有 action 又同时绑了 events.click。启动时警告，自检据此报错 */
    int          bad_actions;
    /** close 回调里调 cg_cancel_close() 会把这里置 1，窗口就不关了 */
    int          close_cancel;
    /** 当前打开的模态对话框（多窗体，实现见 cg_win_modal.c）；NULL = 没开。
     *  T1 只支持一次一个：已经开着时再 open 直接返回 0。
     *  窗口过程靠它分辨"这条消息属于主窗口还是对话框"（见 cgw_MainProc 开头的 host 解析）。 */
    cg_widget_t *modal_dlg;
    /** cg_dialog_open 的返回值：对话框回调里 cg_dialog_close(self, 1) 置 1，其余为 0 */
    int          modal_result;

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

/**
 * @brief 依名字在绑定表里找回调函数
 * @param app 应用
 * @param name .cgui 里写的事件回调名
 * @return cg_event_fn 找到的函数指针；找不到返回 NULL 并打印一次警告
 */
cg_event_fn cg_resolve_binding(cg_app_t *app, const char *name);

/**
 * @brief 事件派发中枢：查回调、跑绑定传播、调业务回调
 * @param w 事件来自哪个控件
 * @param ev 事件类型
 * @return int 1 派发给了业务回调、0 没回调（绑定传播照做）
 */
int cg_dispatch(cg_widget_t *w, cg_event_t ev);

/**
 * @brief 装 / 换 / 清空本次"文件拖进窗口"的路径清单（各自复制一份）
 * @param app 应用
 * @param paths 路径数组（UTF-8）；NULL 或 n <= 0 即清空
 * @param n 路径个数
 * @return void 无返回值
 * @note 后端取完路径调它填进来，再派发 CG_EVENT_DROP，回调返回后立刻
 *       `cg_app_drop_put(app, NULL, 0)` 收掉。对外取用见 cg_drop_count() / cg_drop_file()；
 *       实现在 cg_event.c（与那条 drop 事件放在一起）
 */
void cg_app_drop_put(cg_app_t *app, const char *const *paths, int n);

/**
 * @brief 菜单项被点中：记下是哪一项，再给对应控件派发 click
 * @param app 应用
 * @param cmd 菜单命令 id（WM_COMMAND 的 LOWORD）
 * @return int 1 处理了、0 这个 cmd 不属于任何菜单项
 * @note menu 走 WM_COMMAND（lParam 为 0），menubutton 走 TrackPopupMenu 的返回值
 */
int cg_dispatch_menu_cmd(cg_app_t *app, int cmd);

/* -------------------------------------------------- 动作（core/cg_action.c）
 * 命名命令：文字 + 快捷键 + 回调名；菜单项与控件按名字引用它，
 * 于是同一个回调与同一份启用状态同时管住所有引用点。
 * 用法与语义见 cstackgui.h 的「动作（actions）」一节。 */

/**
 * @brief 按 .cgui 的 actions 建运行时动作表：查 handler、解析 accel、发加速键命令 id
 * @param app 应用
 * @return void 无返回值
 * @note 启动时调一次（在控件树建好之后 —— 校验要读控件的 action 引用）；
 *       顺带校验所有引用点（动作名存在 / 该类型支持 click / 没同时绑 click）
 */
void cg_action_build(cg_app_t *app);
/**
 * @brief 释放动作表
 * @param app 应用
 * @return void 无返回值（收尾时调；幂等、NULL 安全）
 */
void cg_action_release(cg_app_t *app);
/**
 * @brief 按名字找动作下标
 * @param app 应用
 * @param name 动作名（.cgui 顶层 actions 的键）
 * @return int 下标；找不到返回 -1
 */
int  cg_action_index(const cg_app_t *app, const char *name);
/**
 * @brief 按**加速键的命令 id** 触发动作
 * @param app 应用
 * @param cmd TranslateAccelerator 发回来的 WM_COMMAND / WM_SYSCOMMAND 命令 id
 * @return int 1 命中了某个动作的快捷键并已处理；0 不是（调用方按菜单命令那条路走）
 */
int  cg_action_invoke_cmd(cg_app_t *app, int cmd);

/**
 * @brief 按 id 找到运行时控件
 * @param app 应用
 * @param id 控件 id（窗口伪控件是 CG_WINDOW_ID）
 * @return cg_widget_t* 控件；找不到返回 NULL
 */
cg_widget_t *cg_widget_by_id(cg_app_t *app, const char *id);
/**
 * @brief 按 Win32 控件 ID 找控件
 * @param app 应用
 * @param ctrl_id 运行时分配的控件命令 id（1001 起）
 * @return cg_widget_t* 控件；找不到返回 NULL
 */
cg_widget_t *cg_widget_by_ctrl_id(cg_app_t *app, int ctrl_id);
/**
 * @brief 这个控件是不是数值输入框（edit + 调节器且非多行）
 * @param w 控件
 * @return int 1 是、0 不是
 * @note 只有它的 value 才是真相、文本由 value 派生：bind 抄 value、cg_widget_set_value
 *       写文本、后端同步文本都按它分流
 */
int cg_widget_is_numedit(const cg_widget_t *w);
/**
 * @brief 按原生窗口句柄反查控件（native 与 native2 都比对）
 * @param app 应用
 * @param hwnd HWND，以 void* 传入
 * @return cg_widget_t* 控件；找不到返回 NULL
 * @note 自绘外观层（cg_skin.c）用它把 WM_DRAWITEM 的 hwndItem 映射到控件
 */
cg_widget_t *cg_widget_by_hwnd(cg_app_t *app, void *hwnd);

/**
 * @brief 问某个控件"背后"是什么颜色（0xRRGGBB）
 * @param app 应用
 * @param hwnd 控件的原生窗口句柄（以 void* 传入）
 * @return unsigned 主窗口上的控件 = 窗口底色；分组框 / 容器块 / 卡片 / 图形里的 =
 *         面板 style.bg（没设就用系统面色）；标签页里的 = 采样到的页体色
 * @note 无底色标签、以及"清掉自身底色"的自绘按钮 / 开关 / 勾选框都该用它，
 *       否则控件在带底色的面板里会露出一块窗口底色的方块
 */
unsigned cg_win_behind_rgb(cg_app_t *app, void *hwnd);

/**
 * @brief 由文档树构建运行时控件树（不含原生句柄）
 * @param app 应用（doc 已解析好）
 * @return cg_widget_t* 窗口伪控件（树的根）；失败返回 NULL
 */
cg_widget_t *cg_build_widgets(cg_app_t *app);

/**
 * @brief 角色求值：按当前模式取 roles{} 里 light / dark 槽的值（深色皮肤 G1/T2）
 * @param doc 文档（roles 表在里面）
 * @param name 角色名（不带 @）
 * @param dark 1 = 取 dark 槽，0 = 取 light 槽
 * @return unsigned 颜色值；角色不存在或该槽没写返回 CG_COLOR_NONE（= 当没写）
 * @note core 层纯函数（不开窗可测）；win32 的主题钩子按 app->dark 调它。
 */
unsigned cg_role_lookup(const cg_doc_t *doc, const char *name, int dark);

/* ---------------------------------------------- 日志与诊断（cg_log.c / cg_win_diag.c）
 * GUI 版没有控制台，光 fprintf(stderr, ...) 用户看不到，所以要有文件日志与错误弹窗。 */

/**
 * @brief 打开日志文件（追加）
 * @param path 日志路径（UTF-8）；为 NULL / 空串表示只写 stderr
 * @return void 无返回值
 * @note 后端启动时调
 */
void cg_log_open(const char *path);
/**
 * @brief 关闭日志文件（之后只写 stderr）
 * @return void 无返回值
 * @note 进程收尾时调
 */
void cg_log_close(void);
/**
 * @brief 取当前日志文件路径
 * @return const char* 内部缓冲，别 free；没打开返回 NULL（弹窗里用它告诉用户日志在哪）
 */
const char *cg_log_file(void);

/**
 * @brief 起始诊断：定位日志文件、轮转、装未处理异常过滤器
 * @return void 无返回值
 * @note cg_app_run / cg_app_selftest 一开始就调，这样"找不到界面文件"也留得下记录
 */
void cg_diag_init(void);
/**
 * @brief 弹一个阻塞式错误框
 * @param title 标题
 * @param text 正文（建议带上日志路径）
 * @return void 无返回值
 * @note 只在 cg_app_run（有界面）里用，自检别弹
 */
void cg_fatal_dialog(const char *title, const char *text);

/**
 * @brief 启动期"回调名没在 C 侧登记"的条数
 * @return int 条数；cg_app_run 据此决定要不要汇总弹一次
 * @note 名字写错不编译报错，是新手最容易卡住的地方，光在 stderr 飘一行不够
 */
int cg_unresolved_count(void);
/**
 * @brief 取第 i 个没登记的回调名（遍历用）
 * @param i 序号，0 起
 * @return const char* 名字（文档内部字符串，别 free）；越界返回 NULL
 */
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

/**
 * @brief 后端子控件注册回调：原生事件 -> cg_dispatch
 * @param w 事件来自哪个控件
 * @param ev 事件类型
 * @return void 无返回值
 */
void cg_backend_notify(cg_widget_t *w, cg_event_t ev);

/**
 * @brief 分配一个控件命令 id
 * @param app 应用（内部计数器递增）
 * @return int 从 1001 起递增的 id
 * @note 后端建控件时用
 */
int cg_alloc_ctrl_id(cg_app_t *app);

/**
 * @brief 分配一个菜单命令 id
 * @param app 应用（内部计数器递增）
 * @return int 从 0x8000 起递增的 id
 * @note 菜单走 WM_COMMAND 的 LOWORD，必须和控件 id 分开，否则会撞上 1001 起那一片
 */
int cg_alloc_menu_cmd(cg_app_t *app);

/**
 * @brief 取毫秒级时基（后端提供）
 * @return unsigned long 自系统启动起的毫秒数，用于双击判定与定时器
 */
unsigned long cg_ticks(void);
/**
 * @brief 取系统的双击间隔
 * @return unsigned long 毫秒，默认 500
 */
unsigned long cg_double_click_ms(void);

/**
 * @brief 推进一个应用的全部定时器（后端心跳调，50ms 一次）
 * @param app 应用
 * @return void 无返回值
 * @note 到点的先摘链再回调，避免"回调里再挂一颗"被同一轮重复触发
 */
void cg_timers_tick(cg_app_t *app);
/**
 * @brief 清掉一个应用的全部定时器
 * @param app 正在关闭的应用
 * @return void 无返回值
 */
void cg_timers_clear(cg_app_t *app);

/**
 * @brief 取当前 exe 的完整路径（含文件名）
 * @param buf 输出缓冲
 * @param size buf 的字节数
 * @return int 0 成功、非 0 失败
 */
int cg_exe_path(char *buf, size_t size);

/**
 * @brief 读本 exe 里内嵌的资源（PE 的 RT_RCDATA）
 * @param name 资源名（ASCII，如 "CGUI"）
 * @param size 输出资源字节数；可为 NULL
 * @return char * 资源内容（malloc 的字节块，**末尾补了一个 '\0'**，调用方 free）；
 *                没有这个资源或读取失败返回 NULL
 * @note 为什么要有这个端口：单文件发布要把 .cgui 编进 exe，而 core 必须平台中立
 *       （不许 include windows.h），所以按 cg_exe_path 的同一套路，由 win32 后端实现。
 */
char *cg_embedded_resource(const char *name, size_t *size);

/* Win32 后端内部的联动入口（cg_app.c 的公共 API 会调）：
 * hint / period 改完要动 tooltip 和 SetTimer，那些句柄只有后端知道。 */
/**
 * @brief 把控件的悬停提示推给后端的 tooltip 窗口
 * @param w 控件
 * @return void 无返回值
 */
void cg_win_set_hint(cg_widget_t *w);
/**
 * @brief 按控件的 period 重设 Win32 定时器
 * @param w timer 控件（period 为 0 时停掉定时器）
 * @return void 无返回值
 */
void cg_win_restart_timer(cg_widget_t *w);
/**
 * @brief 动作的启用 / 勾选变了：把结果同步到后端的**菜单项**（灰掉 / 打勾，文字也刷新）
 * @param app 应用
 * @param name 动作名
 * @return void 无返回值
 * @note 控件那一半不走这里 —— cg_action_set_enabled 直接调 cg_widget_set_enabled，
 *       它自己的契约要求把两个 HWND 都覆盖；无窗口时是空操作（自检 / 单测里就这么跑）
 */
void cg_win_action_sync(cg_app_t *app, const char *name);
/**
 * @brief 菜单行表变了（运行时增删改）时让后端重建窗口菜单栏
 * @param w 菜单所属控件
 * @return void 无返回值
 * @note menubutton 的弹出菜单不在这里 —— 它每次点击都现场重建，改完行表自然生效；
 *       无窗口时是空操作（自检 / 单测里就这么跑）
 */
void cg_win_menu_rebuild(cg_widget_t *w);
/**
 * @brief 按控件配置重启"串口到达"轮询定时器
 * @param w serial 组件控件（打开才转；复用 ctrl_id 做 Win32 定时器 id）
 * @return void 无返回值
 */
void cg_win_restart_serial(cg_widget_t *w);

/**
 * @brief 新建一个运行时控件（内部用，不建原生窗口）
 * @param app 所属应用
 * @param type 控件类型名
 * @param id 唯一标识
 * @param x 初始左边界（逻辑像素）
 * @param y 初始上边界（逻辑像素）
 * @param w 初始宽度（逻辑像素）
 * @param h 初始高度（逻辑像素）
 * @param text 初始文字
 * @return cg_widget_t* 控件；失败返回 NULL
 */
cg_widget_t *cg_widget_new(cg_app_t *app, const char *type, const char *id,
                           int x, int y, int w, int h, const char *text);
/**
 * @brief 释放控件的全部资源（结构化内容、画板记录、行级数组、文字等）
 * @param w 控件；NULL 安全
 * @return void 无返回值（只释放逻辑模型，原生化句柄归后端 detach）
 */
void cg_widget_free(cg_widget_t *w);

/**
 * @brief 表格行数变多时把行级附加数组（勾选 / 数值 / 图标）一起补长到 want
 * @param w table 控件
 * @param want 目标行数
 * @return void 无返回值
 */
void cg_table_rows_grow(cg_widget_t *w, int want);

/**
 * @brief 控件类型守卫：w 的类型等于 type 就返回 1，否则打一行明确的报错并返回 0
 * @param w 控件
 * @param type 期望的类型名，如 "table"
 * @param who 报错时用的调用方名字，如 "cg_table_clear"
 * @return int 1 类型相符、0 不符（已打印报错）
 * @note cg_widget_data.c 拆成七个文件后由多个控件族共用（定义在 cg_widget_tree.c），
 *       提升到这里声明，免得每个文件各写一份
 */
int want_type(const cg_widget_t *w, const char *type, const char *who);

/* -------------------------------------------------- 控件绑定（cg_bind.c）
 * 一个控件的值/文字/勾选变化时，自动抄到另一个控件上。写在 .cgui 的 bind 字段，
 * 或运行时用 cg_bind()。规则见 cg_bind.c 顶部注释。 */

/**
 * @brief 控件 w 的状态变了：把它的值 / 文字抄给所有"以它为源"的控件（双向的还要推回源）
 * @param w 变化的控件（源）
 * @return void 无返回值
 * @note 事件派发与公共 set 都会调它；没有任何绑定时直接返回，开销为零
 */
void cg_bind_propagate(cg_widget_t *w);

/**
 * @brief 启动时把 .cgui 里的绑定全部应用一次（源控件已经有初值，目标控件跟上）
 * @param app 应用
 * @return void 无返回值
 * @note bind.src 找不到的在这里记一笔并警告
 */
void cg_bind_apply_all(cg_app_t *app);

/**
 * @brief 把源控件的值 / 文字 / 勾选抄到目标控件
 * @param src 源控件
 * @param dst 目标控件
 * @return int 1 真的改了（顺便 sync）、0 没变或抄不了
 */
int cg_bind_copy(cg_widget_t *src, cg_widget_t *dst);

/* -------------------------------------------------- 分隔条（cg_widget_split.c）
 * 几何计算在 core（纯 rect 运算，可脱窗单测），后端的鼠标处理只负责把位移喂进来。 */

/**
 * @brief 拖动分隔条：撑开前一个兄弟、自己与之后的兄弟整体让位、最后一块吃掉余量
 * @param w splitter 控件
 * @param delta 鼠标位移（逻辑像素，可正可负）
 * @return int 实际位移（可能被最小尺寸夹小）；0 表示没动
 */
int cg_splitter_apply(cg_widget_t *w, int delta);

/* -------------------------------------------------- 环形字节缓冲（core/cg_ringbuf.c）
 * 平台服务层收数据用的定容 FIFO（串口的接收缓冲；网口以后复用同一套）。
 * **本身不带锁** —— 谁用谁负责同步（串口那边是一把 CRITICAL_SECTION），这样测试可以在
 * 单线程里把环绕/溢出/清空的边界挨个锤一遍。满了丢**最老的**。 */

typedef struct {
    unsigned char *buf;      /* 容量 cap 的字节环 */
    int            cap;
    int            head;     /* 下一个要读的位置 */
    int            count;    /* 当前有多少字节 */
    long long      dropped;  /* 累计因为满而丢掉的字节数（排错用） */
} cg_ring_t;

/**
 * @brief 建环（先无条件清零，绝不读旧内容）
 * @param r 目标环；可以递未初始化的结构体进来
 * @param cap 容量字节数；<= 0 视为非法
 * @return int 1 成功、0 失败（参数非法或没内存）
 * @note 代价是"要用新容量重建，得先自己 cg_ring_free"
 */
int  cg_ring_init(cg_ring_t *r, int cap);
/**
 * @brief 释放环占用的缓冲（幂等、NULL 安全）
 * @param r 目标环
 * @return void 无返回值
 */
void cg_ring_free(cg_ring_t *r);
/**
 * @brief 写入字节
 * @param r 目标环
 * @param data 要写的字节
 * @param len 字节数
 * @return int 实际写入的字节数；满了丢最老的，一次超过容量时只留最后 cap 个字节
 */
int  cg_ring_push(cg_ring_t *r, const void *data, int len);
/**
 * @brief 取出字节（先进先出）
 * @param r 目标环
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return int 实际取出的字节数
 */
int  cg_ring_pop(cg_ring_t *r, void *out, int cap);
/**
 * @brief 可读字节数
 * @param r 目标环
 * @return int 字节数；r 为 NULL 或没建好返回 0
 */
int  cg_ring_count(const cg_ring_t *r);
/**
 * @brief 清空（dropped 是"历史上一共丢了多少"的累计，清空不清它）
 * @param r 目标环
 * @return void 无返回值
 */
void cg_ring_clear(cg_ring_t *r);

/* -------------------------------------------------- 串口（kit/cg_serial.c） */

/**
 * @brief 拿一个**已经打开好的句柄**建链路并起读线程（内部用）
 * @param handle 已打开的 HANDLE（串口或文件句柄），以 void* 传入
 * @param name 只用于日志与错误信息
 * @param rx_cap 接收环形缓冲的容量字节数
 * @return cg_serial_t* 链路句柄；失败返回 NULL（此时句柄不被接管，调用方自己关）
 * @note 正常的 cg_serial_open() 走的就是它（先 CreateFile + 配 DCB 再进来）
 * @note 单测拿**文件句柄**喂进来，于是"读线程 + 环形缓冲 + 取回"这条链在没有串口设备的
 *       机器上也能验掉
 */
cg_serial_t *cg_serial_from_handle(void *handle, const char *name, int rx_cap);

/* -------------------------------------------------- 状态灯 / 仪表盘（cg_widget_gauge.c） */

/**
 * @brief 取 led 此刻是否亮着（闪态按 period 前后半周期翻转）
 * @param w led 控件
 * @param now_ms 当前毫秒时基（传 cg_ticks()）
 * @return int 1 亮、0 灭
 * @note 算在 core 而不是画的时候，是为了能脱窗单测时序 —— 闪烁算错了不崩、
 *       只是"看不出来在闪"
 */
int cg_led_lit(const cg_widget_t *w, unsigned long now_ms);

/**
 * @brief meter：把读数换算成表盘角度（度）
 * @param w meter 控件
 * @param value 读数；超量程按边界夹取
 * @return int 角度：arc 从 135° 起顺时针扫 270°，half 从 180° 起扫 180°（左端到右端）
 * @note max <= min 时按 range = 1 处理；同样放 core 是为了能脱窗单测
 */
int cg_meter_angle_deg(const cg_widget_t *w, int value);

/* -------------------------------------------------- 图表（cg_widget_gauge.c） */

/**
 * @brief chart：把点缓冲对齐当前配置（items 的系列数 / value 的窗口点数）
 * @param w chart 控件
 * @return int 系列数；0 = 没内存
 * @note 配置变了就重建，**重建会丢已有点**（旧的窗口配不上新的）；配置没变时只是一次比较。
 *       cg_widget_sync() 每次都会调它，所以查询与绘制看到的存储永远与配置一致
 */
int cg_chart_ensure(cg_widget_t *w);

/* ---------------------------------------------- cg_app.c 拆分后的跨模块接口
 * 原来这几个是 cg_app.c 里的 static 助手，拆成 cg_widget.c / cg_event.c 之后
 * 需要跨模块调用。名字都加了 cg_ 前缀，避免和业务代码里的同名符号撞车。 */

/**
 * @brief 复制字符串到堆内存
 * @param s 源串；为 NULL 时返回 NULL
 * @return char * 堆串，由调用方 free；失败返回 NULL
 */
char *cg_str_dup(const char *s);
/**
 * @brief 把运行时控件挂到父控件的 children 末尾（尾插）
 * @param parent 父控件
 * @param child 子控件，child->parent 会被设好
 * @return void 无返回值（分配失败就静默丢弃）
 */
void cg_widget_attach_child(cg_widget_t *parent, cg_widget_t *child);
/**
 * @brief 在控件的树模型末尾追加一个节点
 * @param w tree 控件
 * @param parent 父节点下标；-1 表示根
 * @param text 节点文字（会复制一份）
 * @param expanded 1 展开、0 折叠
 * @return int 新节点下标（大于等于 0）；-1 表示失败
 */
int cg_widget_tree_push(cg_widget_t *w, int parent, const char *text, int expanded);
/**
 * @brief 把控件逻辑状态推给后端（标记 dirty 并让后端重建对应部分）
 * @param w 控件
 * @return void 无返回值（无后端时是空操作）
 */
void cg_widget_sync(cg_widget_t *w);

#endif /* CG_INTERNAL_H */
