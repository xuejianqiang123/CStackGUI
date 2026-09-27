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
    /* 输入框只有这一种类型：单行 / 多行 / 数值框都由 edit 的字段表达
     * （multiline / scroll / spin，见 cg_doc.h 的 cg_node_t）。
     * 2026-09-22 把原来的 multiedit、spinner 合并进来了，老 .cgui 由 cg_doc.c 的
     * parse_node 自动升级（读到旧名按新字段填），所以这里不再有它们。 */
    CG_K_EDIT,
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
    CG_K_SERIAL,     /* 串口组件（无窗口：像易语言的串口组件，数据到达发 arrive） */
    CG_K_WINDOW,     /* 伪控件：窗口自身；支持全部窗口级事件（就绪 / 关闭 / 移动 / 尺寸 / 托盘 / 拖放……，见 cg_doc_kind.c 的类型表） */
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
#define CG_EV_SPIN          CG_EV_BIT(CG_EVENT_SPIN)
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
#define CG_EV_ARRIVE        CG_EV_BIT(CG_EVENT_ARRIVE)
#define CG_EV_DROP          CG_EV_BIT(CG_EVENT_DROP)

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

/**
 * @brief 按 .cgui 的 type 字段找控件类型信息
 * @param type 类型名，如 "button"（大小写敏感，与画布 KINDS 一致）
 * @return const cg_kind_info_t* 静态表项，别 free；不认识返回 NULL
 */
const cg_kind_info_t *cg_kind_lookup(const char *type);
/**
 * @brief 按枚举值找控件类型信息
 * @param kind 类型枚举
 * @return const cg_kind_info_t* 静态表项，别 free；越界返回 NULL
 */
const cg_kind_info_t *cg_kind_info(cg_kind_t kind);
/**
 * @brief 取控件类型名
 * @param kind 类型枚举
 * @return const char* 静态字符串（如 "button"），别 free；越界返回 "?"
 */
const char           *cg_kind_name(cg_kind_t kind);
/**
 * @brief 某个控件类型支不支持这个事件
 * @param type 类型名
 * @param ev 事件类型
 * @return int 1 支持、0 不支持或类型不认识
 */
int                   cg_kind_supports_event(const char *type, cg_event_t ev);
/**
 * @brief 取某个控件类型支持的事件名列表
 * @param kind 类型枚举
 * @return const char* 静态串，形如 "click/dblclick"，便于报错时提示；没有事件返回 ""
 */
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

/** style 里"到底写了哪几个字段" —— 合并命名样式与内联字段时**必须**知道，
 *  否则分不出"没写"与"写成默认值"（后者会把命名样式里的值顶掉）。
 *  见 .codebuddy/plans/named-styles-dark.md §2。 */
enum {
    CG_STYLE_HAS_FONT   = 1 << 0,
    CG_STYLE_HAS_SIZE   = 1 << 1,
    CG_STYLE_HAS_BOLD   = 1 << 2,
    CG_STYLE_HAS_FG     = 1 << 3,
    CG_STYLE_HAS_BG     = 1 << 4,
    CG_STYLE_HAS_RADIUS = 1 << 5,
    CG_STYLE_HAS_ALIGN  = 1 << 6
};

/** 顶层 `styles{}` 里的一条命名样式 */
typedef struct {
    char       *name;   /* 引用名（.cgui 里 "style": "name" 用的就是它） */
    cg_style_t  st;     /* 把它当"内联字段全写满"的一份样式 */
} cg_named_style_t;

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
    /** 引用的动作名（.cgui 的 action）；NULL = 这个菜单项不是动作。
     *  有它时：文字取动作的 text（非空时覆盖本项的 text）、点击触发动作、
     *  启用与勾选都跟随动作 —— 这一项**不再**给菜单控件派发 click。
     *  注意本结构**没有 events 字段**：菜单项本来就写不出自己的事件绑定，
     *  所以"动作项与普通项混用一根菜单"是正常写法（普通项照旧走菜单控件的 events.click）。 */
    char             *action;
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
    /** 引用的命名样式名（.cgui 的 `"style": "名字"` 或 `{ "use": "名字" }`）；NULL = 没引用。
     *  解析时只记名字，合并（命名打底 + 内联覆盖）在 styles{} 读完后的
     *  resolve_named_styles() 里统一做 —— 见 .codebuddy/plans/styles-dark.md 的"取值口径"一节。 */
    char    *style_name;
    /** 控件**自己**写了哪几个样式字段（CG_STYLE_HAS_* 位或）。
     *  没有它就分不出"没写"与"写成默认值"，后者会把命名样式里的值顶掉。 */
    unsigned style_mask;
    /** 余量分配权重（.cgui 的 grow，0..99）：容器变大时按权重吃多出来的空间；
     *  与它位置重叠的兄弟（同一行 / 同一列，即"同一条带"）原地不动，排在这一行 / 列**之后**的
     *  兄弟整体平移；0 / 不写 = 不吃余量。
     *  容错走 cj_int 的常规口径（小数四舍五入、true 当 1、数字串按数字读），负数与 >99 归 0。
     *  语义与几何算法见 .codebuddy/plans/grow-layout.md */
    int      grow;
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
    /* ---- 串口组件（serial）：参数写在控件属性里，"数据到达"（arrive）由运行时派发 ---- */
    /** 端口名（"COM4"；NULL/空 = 没配，运行时用 cg_widget_serial_open(w, port) 指定） */
    char    *sport;
    /** 波特率（缺省 9600） */
    int      s_baud;
    /** 数据位 5..8（缺省 8） */
    int      s_bits;
    /** 停止位 1..2（缺省 1） */
    int      s_stop;
    /** 校验 0=无 / 1=奇 / 2=偶（缺省 0） */
    int      s_parity;
    /** 自动启动：窗口建好就按上面参数打开链路（缺省 0 = 等用户代码开） */
    int      s_auto;
    /** 控件绑定：源控件 id（.cgui 的 bind.src）；NULL = 没绑 */
    char    *bind_src;
    /** 绑定是双向的（源变跟源走，自己也变时也推回源） */
    int      bind_two;
    /** 引用的动作名（.cgui 的 action）；NULL = 不是动作。
     *  只在**该类型支持 click** 时有意义：有它时点击交给动作（不再走自己的
     *  click/dblclick），文字取动作的 text（非空时覆盖本节点的 text），
     *  启用跟随动作。同时绑了 events.click 会被忽略（自检报错）。 */
    char    *action;
    /** table：允许多选行 */
    int      multi;
    /** table：显示行首复选框列 */
    int      check;
    /* ---- edit（单行输入）的输入框行为：缺省 0 = 保持原来的单行左对齐输入框 ---- */
    /** 允许多行（ES_MULTILINE + ES_WANTRETURN，Enter 变换行） */
    int      multiline;
    /** 滚动条：位 1 = 横向、位 2 = 纵向（0 无 / 1 横 / 2 纵 / 3 双向） */
    int      scroll;
    /** 最多允许输入多少字符；0 = 不限制（不调 EM_SETLIMITTEXT） */
    int      maxlen;
    /** 输入时的大小写转换：1 = 大写转小写、2 = 小写转大写 */
    int      conv;
    /** 输入方式：0 通常 / 1 只读 / 2 密码 / 3 整数文本 / 4 小数文本 /
     *  5 字节 / 6 短整数 / 7 整数 / 8 长整数 / 9 小数 / 10 双精度小数 / 11 日期时间 */
    int      input;
    /** 边框样式：0 = 缺省（M3 圆角自绘）/ 1 无边框 / 2 凹入式 / 3 凸出式 /
     *  4 浅凹入式 / 5 镜框式 / 6 单线边框式 */
    int      border;
    /** 调节器（右侧上下箭头）：1 自动（按 ±1 改值并夹到 min/max）/ 2 手动（只发 spin 事件）。
     *  **非 0 即"数值框"**：此时 `value` 是这个输入框的真相（文本由 value 派生），
     *  `cg_widget_value()`/`cg_widget_set_value()` 与 bind 抄 value 全部成立 —— 这就是
     *  原来独立类型 spinner 的能力（2026-09-22 合并进来）。多行输入框不支持调节器。 */
    int      spin;
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

/** 动作（.cgui 顶层 actions 的一项）：一条**命名命令** = 文字 + 快捷键 + 回调名。
 *  菜单项与控件用 "action" 字段按名字引用它，于是同一个回调与同一份启用状态
 *  同时管住所有引用点。用法与坑见 cstackgui.h 的「动作（actions）」一节。
 *
 *  与其它结构化字段的一个区别：**actions 是顶层对象**（键就是动作名），不是控件数组。 */
typedef struct {
    char *name;    /* actions 对象的键（动作名）；空名的条目会被忽略 */
    char *text;    /* 引用它的菜单项 / 控件显示的文字；NULL / 空 = 用引用点自己的文字 */
    char *accel;   /* 快捷键原文（"Ctrl+S"）；NULL / 空 = 没有快捷键 */
    /** C 侧 CG_BIND 的名字。NULL / 空 = 这个动作没有意义，运行时忽略它并警告
     *  （--selftest 据此报错）。 */
    char *handler;
    int   checked; /* 初始勾选状态（同步给引用它的菜单项） */
    int   enabled; /* 初始启用状态，缺省 1 */
} cg_action_t;

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
    /** 顶层 actions：命名命令表（可为空 = 这个界面没有动作）。
     *  按 .cgui 里的书写顺序存，重复名后者覆盖。 */
    cg_action_t *actions;
    size_t       nactions;
    /** 顶层 styles：命名样式表（可为空）。控件用 "style": "名字" 引用它。
     *  与 actions 同一口径：按书写顺序存，重名后者覆盖。
     *  语义见 .codebuddy/plans/styles-dark.md */
    cg_named_style_t *styles;
    size_t            nstyles;
    /** 引用了**不存在**的样式名的控件个数（解析时统计，>0 就是 .cgui 里有失效引用）。
     *  运行时按"没写 style"处理（不崩、不中断加载），但要**报一条 WARN**、并被 `--selftest`
     *  计入失败 —— 否则这种失效是**完全静默**的：外观悄悄退回缺省，谁都不知道为什么。
     *  与动作那句"未登记"同一个口径（2026-09-27 补，此前只跳过、一声不吭）。 */
    int               style_missing;
    /** 第一个失效引用的名字（指向节点里的字符串，随文档一起释放；只给那条 WARN 用） */
    const char       *style_missing_name;
    /** 深色模式（.cgui 的 app.dark）：1 = 深色，0 = 浅色（缺省），**-1 = "auto" 跟随系统**。
     *  core 不碰 Win32，所以 auto 只记成 -1，由**后端**去读系统主题（HKCU 的 AppsUseLightTheme）。
     *  运行时要切换用 cg_theme_set_dark()。 */
    int               dark;
} cg_doc_t;

/**
 * @brief 解析 .cgui 文本成文档树
 * @param json .cgui 的 JSON 原文（UTF-8）
 * @param err 失败时写错误信息的缓冲；可传 NULL
 * @param errsz err 的字节数
 * @return cg_doc_t* 文档结构（用 cg_doc_free 释放）；失败返回 NULL
 */
cg_doc_t *cg_doc_parse(const char *json, char *err, size_t errsz);
/**
 * @brief 释放文档内容，但 doc 本身不动
 * @param doc 文档；NULL 安全
 * @return void 无返回值
 * @note doc 可能嵌在别的结构里、或是栈变量，所以只清内容（与 cg_doc_free 的分工就在这）
 */
void      cg_doc_release(cg_doc_t *doc);
/**
 * @brief 释放内容并释放 doc 本身
 * @param doc cg_doc_parse 返回的指针；NULL 安全
 * @return void 无返回值
 */
void      cg_doc_free(cg_doc_t *doc);

/**
 * @brief 以缩进树的形式打印文档结构
 * @param doc 文档
 * @param out 输出流（如 stdout / stderr）
 * @return void 无返回值（自检 / 排查用）
 */
void cg_doc_dump(const cg_doc_t *doc, FILE *out);

/**
 * @brief 读文件到堆内存（UTF-8 原文）
 * @param path 文件路径（UTF-8）
 * @return char * 堆串，用完 free；失败返回 NULL
 */
char *cg_read_file(const char *path);

#endif /* CG_DOC_H */
