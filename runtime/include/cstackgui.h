/**
 * @file    cstackgui.h
 * @brief   CStackGUI 运行时公共 API
 * @details 约定：业务代码只 include 本文件，不依赖任何 GUI 框架细节。
 *          一个业务程序 = 运行时库 + 你自己的 .c 文件 = 单个 exe。
 *
 * 典型用法：
 *
 *   static void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user) {
 *       cg_widget_t *lab = cg_find(cg_app_of(self), "lab_result");
 *       cg_widget_set_text(lab, "已点击");
 *   }
 *
 *   static const cg_binding_t k_bindings[] = {
 *       CG_BIND("on_ok_click", on_ok_click),
 *       CG_BIND_END
 *   };
 *
 *   int main(void) {
 *       cg_app_desc_t d = {0};
 *       d.cgui_path = "hello.cgui";
 *       d.bindings  = k_bindings;
 *       return cg_app_run(&d);
 *   }
 *
 * 回调名字写在 .cgui 里（events.click / events.dblclick ...），
 * 运行时按名字在 bindings 表里查函数指针 —— 这样画布和 C 代码彻底解耦。
 */
#ifndef CSTACKGUI_H
#define CSTACKGUI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CG_VERSION_MAJOR 0
#define CG_VERSION_MINOR 1
#define CG_VERSION_STRING "0.1.0"

/** 颜色字面量：0xRRGGBB（set_colors / canvas / choose_color 都用这套编码） */
#define CG_RGB(r, g, b) ((((unsigned)(r) & 0xFFu) << 16) | (((unsigned)(g) & 0xFFu) << 8) | ((unsigned)(b) & 0xFFu))

/** 「不设置颜色」：这个控件/窗口交给系统默认配色。
 *  业务代码只用 include 本头文件，所以它必须在这里公开（内部 cg_doc.h 直接复用本定义）。 */
#define CG_COLOR_NONE 0xFFFFFFFFu

/* ---------------------------------------------------------------- 事件 */

typedef enum {
    /* ---------------- 控件事件 ---------------- */
    CG_EVENT_CLICK = 0,   /* 单击 */
    CG_EVENT_DBLCLICK,    /* 双击 */
    CG_EVENT_CHANGE,      /* 内容 / 值 / 勾选变化 */
    CG_EVENT_SUBMIT,      /* 单行输入框回车 */
    CG_EVENT_SELECT,      /* 选择项变化（列表 / 标签页 / 树 / 表格 / 日历） */
    CG_EVENT_RELEASE,     /* 拖动结束（滑块松手才算，避免拖动过程中刷屏） */
    CG_EVENT_EXPAND,      /* 展开节点（树） */
    CG_EVENT_COLLAPSE,    /* 折叠节点（树） */
    CG_EVENT_CLOSE,       /* 窗口关闭前（窗口级事件） */
    CG_EVENT_TICK,        /* 时钟周期到（timer） */
    CG_EVENT_CHECK,       /* 表格行勾选状态变化（table 勾选列） */
    CG_EVENT_GOTFOCUS,    /* 获得键盘焦点 */
    CG_EVENT_LOSTFOCUS,   /* 失去键盘焦点 */

    /* ---------------- 窗口级事件 ----------------
     * 全部绑在窗口伪控件（id = CG_WINDOW_ID）上，写在 .cgui 的 app.events 里。
     */
    CG_EVENT_READY,         /* 创建完毕：窗口与全部控件建好、首次显示之后 */
    CG_EVENT_DESTROY,       /* 将被销毁（窗口已销毁，进程即将退出） */
    CG_EVENT_MOVE,          /* 位置被改变 */
    CG_EVENT_SIZE,          /* 尺寸被改变 */
    CG_EVENT_ACTIVATE,      /* 被激活（从别的程序切回来） */
    CG_EVENT_DEACTIVATE,    /* 被取消激活 */
    CG_EVENT_FIRSTACTIVATE, /* 首次激活 */
    CG_EVENT_IDLE,          /* 空闲：消息队列空着时（别在这里做耗时的活） */
    CG_EVENT_TRAY,          /* 托盘事件（用 cg_tray_action 问是哪种动作） */
    CG_EVENT_SHOW,          /* 被显示 */
    CG_EVENT_HIDE,          /* 被隐藏 */
    CG_EVENT_CLOSEQUERY,    /* 窗口可否被关闭（与 close 同时机，两处都能拦） */
    CG_EVENT_MOUSEDOWN,     /* 鼠标左键被按下 */
    CG_EVENT_MOUSEUP,       /* 鼠标左键被放开 */
    CG_EVENT_RDOWN,         /* 鼠标右键被按下 */
    CG_EVENT_RUP,           /* 鼠标右键被放开 */
    CG_EVENT_MOUSEMOVE,     /* 鼠标位置被移动 */
    CG_EVENT_KEYDOWN,       /* 按下某键 */
    CG_EVENT_KEYUP,         /* 放开某键 */
    CG_EVENT_CHAR,          /* 字符输入 */
    CG_EVENT_WHEEL,         /* 滚轮被滚动 */
    CG_EVENT_COUNT
} cg_event_t;

/** 事件名的规范写法，也是 .cgui 中 events 对象的键名 */
const char *cg_event_name(cg_event_t ev);
/** 名字反查事件；不认识返回 -1 */
int cg_event_from_name(const char *name);

/* ------------------------------------------------------------ 不透明类型 */

typedef struct cg_app    cg_app_t;
typedef struct cg_widget cg_widget_t;

/** 窗口自身那个"控件"的 id。窗口级事件（close）就绑在它身上，
 *  想给 close 回调传业务数据时用 cg_find(app, CG_WINDOW_ID) 拿到它。 */
#define CG_WINDOW_ID "__window"

/** 所有控件事件的统一回调签名。
 *  @param self 触发事件的控件（不会是 NULL）
 *  @param ev   事件类型
 *  @param user cg_widget_set_user() 挂上的业务数据，默认 NULL
 */
typedef void (*cg_event_fn)(cg_widget_t *self, cg_event_t ev, void *user);

/* ------------------------------------------------------ 事件参数（窗口事件用）
 * 鼠标 / 按键 / 滚轮 / 托盘事件都带参数，但回调签名固定是三参数，
 * 所以参数放在"最近一次事件"里，回调里用下面这些函数读：
 *
 *   static void on_keydown(cg_widget_t *self, cg_event_t ev, void *user) {
 *       int key = cg_event_key(self);          // 键代码，见 VK_*
 *       int fnk = cg_event_keys(self);         // 1=Ctrl 2=Shift 4=Alt（对齐）
 *   }
 *
 * 坐标是相对窗口客户区的逻辑像素；非鼠标事件返回 0。
 */
int cg_event_x(const cg_widget_t *self);
int cg_event_y(const cg_widget_t *self);
/** 功能键状态：1=Ctrl 2=Shift 4=Alt，可位或 */
int cg_event_keys(const cg_widget_t *self);
/** 键代码（keydown / keyup 是虚拟键码，char 是字符码） */
int cg_event_key(const cg_widget_t *self);
/** 滚轮滚动量（正数向上）；非滚轮事件返回 0 */
int cg_event_delta(const cg_widget_t *self);
/** 托盘动作：1=左键单击 2=左键双击 3=右键单击 */
int cg_tray_action(const cg_widget_t *self);

/** 名字 -> 函数。表尾必须用 CG_BIND_END 结束。 */
typedef struct {
    const char *name;
    cg_event_fn fn;
} cg_binding_t;

#define CG_BIND(name_literal, fn) { (name_literal), (fn) }
#define CG_BIND_END               { NULL, NULL }

/* ---------------------------------------------------------------- 启动 */

/** 界面建好、消息循环开始之前调用一次。
 *  用来挂业务数据（cg_widget_set_user）、做初始检查、按业务状态调初始界面。 */
typedef void (*cg_ready_fn)(cg_app_t *app, void *user);

typedef struct {
    /** 要加载的 .cgui 文件路径；与 cgui_text 至少给一个 */
    const char *cgui_path;
    /** 直接内嵌一段 .cgui JSON（资源打不进文件时用，优先级高于 cgui_path） */
    const char *cgui_text;
    /** 事件名 -> C 函数 的绑定表，可为 NULL */
    const cg_binding_t *bindings;
    /** 双击时是否吞掉第二次 click 回调；0=先 click 再 dblclick（默认） */
    int dblclick_suppress_click;
    /** 窗口标题覆盖；NULL 表示用 .cgui 里的 app.title */
    const char *title_override;
    /** 只在建窗口前打印文档树，不开窗（调试用） */
    int trace_doc;
    /** 找不到 cgui_path 时，自动去 exe 旁边和 ..\apps\<exe名>\ 里找同名 .cgui。
     *  设成 1 后双击 exe 也能直接跑，不用带命令行参数。 */
    int auto_locate_cgui;
    /** 界面就绪钩子，可 NULL */
    cg_ready_fn on_ready;
    /** 传给 on_ready 的自定义数据 */
    void *ready_user;
} cg_app_desc_t;

/** 建窗口 + 消息循环，返回进程退出码。 */
int cg_app_run(const cg_app_desc_t *desc);

/** 不开窗的自检：解析 .cgui、打印控件树、对每个绑定了回调的控件合成一次事件。
 *  用于 CI / 命令行确认「文件能解析、回调能命中」。返回 0 表示全部通过。 */
int cg_app_selftest(const cg_app_desc_t *desc);

/* ------------------------------------------------------------ 应用级操作 */

/** 从控件拿到它所属的应用（回调里最常用的第一步） */
cg_app_t   *cg_app_of(const cg_widget_t *w);
/** 按 id 找控件（id 在画布「属性」里定，就是 .cgui 里的 id 字段） */
cg_widget_t *cg_find(cg_app_t *app, const char *id);
/** 退出消息循环，进程随后结束（退出码 0） */
void        cg_quit(cg_app_t *app);
/** 在窗口的 close 回调里调用：这次不关了（比如"还有没保存的内容"）。
 *  只有 close 回调里调才有意义。 */
void        cg_cancel_close(cg_app_t *app);
void        cg_set_title(cg_app_t *app, const char *title);
/** 当前有没有真正的窗口（--selftest 不开窗）：回调里想弹对话框之前先问一句，
 *  免得自检/CI 被弹窗卡住。 */
int         cg_app_has_window(const cg_app_t *app);

/** 把 .cgui 文档树打印到 stdout（调试/自检用） */
void cg_dump_doc(cg_app_t *app);

/* ------------------------------------------------------------ 窗口操作
 * 都是运行时改窗口属性；.cgui 的 app 节点里也能写（窗口属性表见 docs/CGUI-FORMAT.md）。
 */
/** 移动窗口（top-left，屏幕坐标） */
void cg_win_set_pos(cg_app_t *app, int x, int y);
/** 改窗口客户区尺寸（逻辑像素，和 .cgui 的 width/height 同一套单位） */
void cg_win_set_size(cg_app_t *app, int width, int height);
/** 窗口居中（当前显示器工作区） */
void cg_win_center(cg_app_t *app);
/** 显示 / 隐藏窗口 */
void cg_win_show(cg_app_t *app, int show);
/** 最小化 / 最大化 / 还原：cmd 传 "min" / "max" / "normal" */
void cg_win_set_state(cg_app_t *app, const char *cmd);
/** 总在最前（1）或取消（0） */
void cg_win_set_topmost(cg_app_t *app, int on);
/** 改窗口图标（.ico 路径，同 .cgui 里 app.icon 的查找规则）；NULL 还原默认 */
void cg_win_set_icon(cg_app_t *app, const char *ico_path);

/* ------------------------------------------------------------ 托盘
 * .cgui 的 app 里写 tray:true 会自动建托盘图标；也可以运行时代码调用。
 * 托盘被点（左键单击 / 左键双击 / 右键单击）时派发 window 的 tray 事件，
 * 回调里用 cg_tray_action() 分辨动作。
 */
/** 建/改托盘图标：ico_path 传 NULL 用窗口图标，tip 传 NULL 用窗口标题 */
void cg_tray_set(cg_app_t *app, const char *ico_path, const char *tip);
/** 移除托盘图标 */
void cg_tray_remove(cg_app_t *app);
/** 气泡提示（tip_title / text 都可 NULL） */
void cg_tray_balloon(cg_app_t *app, const char *tip_title, const char *text);

/* ------------------------------------------------------------ 程序配置（版本信息）
 * 来自 .cgui 的 app.info（画布「程序配置」对话框填的那些），
 * 编译时会被写进 exe 的版本信息，运行时也能读出来做「关于」对话框。
 * 支持的 key："product"（程序名称）/"desc"（描述）/"comments"（备注）/"version"（版本号）
 *              "author"/"company"/"copyright"/"address"/"postcode"/"phone"/"fax"
 *              "email"/"homepage"/"build"（创建号）
 * 没有的 key 返回 NULL。
 */
const char *cg_app_info(const cg_app_t *app, const char *key);

/* ------------------------------------------------------------ 控件读取 */

/** 控件 id；w 为 NULL 返回 NULL */
const char *cg_widget_id(const cg_widget_t *w);
/** 控件类型名（"button"/"edit"...） */
const char *cg_widget_type(const cg_widget_t *w);
/** 控件当前文字；w 为 NULL 返回 NULL */
const char *cg_widget_text(const cg_widget_t *w);
/** 控件位置与尺寸（逻辑像素，相对父容器） */
int         cg_widget_x(const cg_widget_t *w);
int         cg_widget_y(const cg_widget_t *w);
int         cg_widget_w(const cg_widget_t *w);
int         cg_widget_h(const cg_widget_t *w);
/** 当前状态；w 为 NULL 时 visible/enabled 返回 0，其余返回 0 */
int         cg_widget_visible(const cg_widget_t *w);
int         cg_widget_enabled(const cg_widget_t *w);
int         cg_widget_checked(const cg_widget_t *w);
/** 当前值（滑块位置 / 列表选中下标 / 标签页页码等） */
int         cg_widget_value(const cg_widget_t *w);
/** 当前控件总数（不含窗口伪控件），调试用 */
int         cg_widget_count(const cg_app_t *app);

/* ------------------------------------------------------------ 控件修改
 * 所有 set 都立即反映到界面上（内部做了 Win32 同步），不需要手动刷新。
 */

void cg_widget_set_text(cg_widget_t *w, const char *text);
void cg_widget_set_rect(cg_widget_t *w, int x, int y, int width, int height);
void cg_widget_set_pos(cg_widget_t *w, int x, int y);
void cg_widget_set_size(cg_widget_t *w, int width, int height);
void cg_widget_set_visible(cg_widget_t *w, int visible);
void cg_widget_set_enabled(cg_widget_t *w, int enabled);
void cg_widget_set_checked(cg_widget_t *w, int on);
/** 滑块 / 进度条 / 列表选择位置 */
void cg_widget_set_value(cg_widget_t *w, int value);
void cg_widget_set_value_range(cg_widget_t *w, int min, int max);
/** 前景 / 背景色，0xRRGGBB 或 CG_RGB(r,g,b) */
void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg);
void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold);

void  *cg_widget_get_user(const cg_widget_t *w);
void   cg_widget_set_user(cg_widget_t *w, void *user);

/* ------------------------------------------------------ 列表类控件
 * 作用于 listbox / combo / tabs（标签文字）/ statusbar（分段文字）：
 * 内部保存一份字符串表，改完界面自动刷新。
 */
int  cg_widget_list_add(cg_widget_t *w, const char *item);
int  cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...);
void cg_widget_list_clear(cg_widget_t *w);
int  cg_widget_list_count(const cg_widget_t *w);
const char *cg_widget_list_item(const cg_widget_t *w, int index);
/** 让列表滚到最后一项（日志类控件常用） */
void cg_widget_list_scroll_bottom(cg_widget_t *w);

/* ---------------------------------------------------------------- 树 */
/** 加一个树节点；parent 传 -1 表示加在根上。返回节点下标（大于等于 0），失败返回 -1。 */
int  cg_tree_add(cg_widget_t *w, int parent, const char *text);
int  cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...);
void cg_tree_clear(cg_widget_t *w);
int  cg_tree_count(const cg_widget_t *w);
const char *cg_tree_item_text(const cg_widget_t *w, int item);
/** 当前选中的节点下标，没选中返回 -1 */
int  cg_tree_selected(const cg_widget_t *w);
const char *cg_tree_selected_text(const cg_widget_t *w);
void cg_tree_expand(cg_widget_t *w, int item, int expand);

/* ---------------------------------------------------------------- 表格 */
/** 加一列；width 传 0 表示按控件宽度均分。返回列号，失败返回 -1。 */
int  cg_table_add_column(cg_widget_t *w, const char *title, int width);
/** 加一行；cells 是列数个字符串，不足的列按空串处理。返回行号，失败返回 -1。 */
int  cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells);
void cg_table_clear(cg_widget_t *w);
int  cg_table_row_count(const cg_widget_t *w);
const char *cg_table_cell(const cg_widget_t *w, int row, int col);
/** 当前选中行，没选中返回 -1 */
int  cg_table_selected(const cg_widget_t *w);

/* ------------------------------------------------------------ 表格（增强） */

/** 表格行数值 / 勾选 / 行图标（row 从 0 起）。
 *  勾选需要 .cgui 里 table 开了 check（显示复选框列）；勾选变化触发 check 事件。 */
int  cg_table_set_row_value(cg_widget_t *w, int row, int value);
int  cg_table_row_value(const cg_widget_t *w, int row);
void cg_table_set_checked(cg_widget_t *w, int row, int on);
int  cg_table_checked(const cg_widget_t *w, int row);
/** 行首图标（png/jpg/bmp/gif 路径，同 image 控件的查找规则）；path 传 NULL 清除 */
void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path);
/** 多选（.cgui 里 table 开了 multi）时：选中行数量 / 第 i 个选中行号 */
int  cg_table_selected_count(const cg_widget_t *w);
int  cg_table_selected_row(const cg_widget_t *w, int index);

/* ------------------------------------------------------------ 状态栏 */
/** 改第 part 段的文字（段不存在且 part 等于段数时相当于追加一段） */
void cg_status_set(cg_widget_t *w, int part, const char *text);
void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...);

/* ------------------------------------------------------------ 标签页 */
int  cg_tabs_count(const cg_widget_t *w);
void cg_tabs_select(cg_widget_t *w, int index);
int  cg_tabs_selected(const cg_widget_t *w);

/* ------------------------------------------------------------ 图片 */
/** 换图片文件（png/jpg/bmp/gif）；路径不存在时控件会显示成占位框 */
void cg_widget_set_image(cg_widget_t *w, const char *path);
const char *cg_widget_image(const cg_widget_t *w);

/* ------------------------------------------------------------ 菜单
 * menu（窗口菜单栏）和 menubutton（按钮弹菜单）共用一套菜单结构。
 * 点中某一项时，运行时给该控件派发 click 事件，再用下面两个函数问是哪一项：
 *
 *   static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user) {
 *       const char *id = cg_menu_last_id(self);
 *       if (id && strcmp(id, "m_new") == 0) { ... }
 *   }
 *
 * 菜单项在 .cgui 里写 id；没写 id 的项只能用 cg_menu_last_text() 按文字判断。
 */
const char *cg_menu_last_id(const cg_widget_t *w);
const char *cg_menu_last_text(const cg_widget_t *w);
/** 菜单里有多少个可点的项（不含分隔线和顶级弹出项），调试用 */
int         cg_menu_count(const cg_widget_t *w);

/* ------------------------------------------------------------ 提示与时钟 */

/** 鼠标悬停提示（tooltip）；text 传空串清除。对所有控件生效。 */
void cg_widget_set_hint(cg_widget_t *w, const char *text);
const char *cg_widget_hint(const cg_widget_t *w);

/** 时钟（timer 控件）的触发周期，毫秒；0 = 停止，>0 = 立即按新周期重新计时 */
void cg_widget_set_period(cg_widget_t *w, int period_ms);
int  cg_widget_period(const cg_widget_t *w);

/* ------------------------------------------------------------ 通用对话框
 * 都不是控件，是弹系统对话框的函数。parent 传控件（定位用）或 NULL。
 * 路径缓冲区至少 1024 字节。返回 1 = 用户确定，0 = 取消/失败。
 */
int  cg_open_file_dialog(cg_widget_t *parent, const char *title,
                         const char *filter, char *out_path, int out_size);
int  cg_save_file_dialog(cg_widget_t *parent, const char *title,
                         const char *filter, const char *default_ext,
                         char *out_path, int out_size);
/** rgb 进出都是 0xRRGGBB；确认时写回并返回 1 */
int  cg_choose_color(cg_widget_t *parent, unsigned *rgb);
/** 消息框（确定钮）。icon: 0=信息 1=警告 2=错误 3=问号 */
void cg_message_box(cg_widget_t *parent, const char *text, const char *title, int icon);

/* ------------------------------------------------------------ 画板
 * canvas 控件上用代码绘制。坐标系是控件内左上角 (0,0)，
 * x 向右 y 向下；控件重绘（被盖住再露出来、改大小）时按记录重画。
 */
void cg_canvas_clear(cg_widget_t *w);
/** 后续图形的颜色与线宽（0xRRGGBB） */
void cg_canvas_set_color(cg_widget_t *w, unsigned rgb);
void cg_canvas_set_line_width(cg_widget_t *w, int px);
void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2);
/** fill=0 描边，1 实心 */
void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text);

/* ------------------------------------------------------------ 网页浏览 */
/** 让 browser 控件打开一个地址（http/https/本地文件路径均可）；实验性功能 */
void cg_browser_navigate(cg_widget_t *w, const char *url);

/* ------------------------------------------------------------ ECharts 图表 */
/** 运行时改 ECharts 图表的 option（整段 JSON 文本）；实验性功能，需系统装有 WebView2 Runtime */
void cg_echarts_set_option(cg_widget_t *w, const char *json);

/* ------------------------------------------------------------ 日志
 * 为什么需要它：GUI 版（-mwindows）没有控制台，printf / stderr 用户一个字都看不见。
 * 运行时启动时会自己开一份日志（%LOCALAPPDATA%\<exe名>\app.log），这里让业务代码
 * 也能往同一个文件里补记录 —— 用户报"点了没反应"时，让他把日志发过来就能查。
 */
#define CG_LOG_INFO  0
#define CG_LOG_WARN  1
#define CG_LOG_ERROR 2

/** 写一条日志（printf 风格）。始终镜像到 stderr，并追加到日志文件。 */
void cg_log(int level, const char *fmt, ...);
/** 只记 >= level 的条目（默认 CG_LOG_INFO）。 */
void cg_log_set_level(int level);

/* ------------------------------------------------------------ 基础能力（cg_kit）
 * 和界面无关，但做真工具一定会用到：读写文件、配置持久化、文本与编码、剪贴板、
 * 打开外部程序、注册表。**凡是返回 char* 的函数都是堆内存，用完请 cg_free()**。
 * 这样业务代码不必 include windows.h / stdlib.h，仍然只认本头文件。
 */

/** 释放上面这些函数返回的堆内存（等价 free()，省得业务代码去 include stdlib.h） */
void cg_free(void *p);

/* ---- 文件 ---- */
/** 文件是否存在（目录不算） */
int   cg_file_exists(const char *path);
/** 读整个文件成字符串（去掉 UTF-8 BOM；二进制文件也是原样返回）；失败 NULL */
char *cg_file_read(const char *path);
/** 覆盖写文件（UTF-8 文本）；成功返回 0 */
int   cg_file_write(const char *path, const char *text);
/** 追加到文件末尾（文件不存在就新建）；成功返回 0 */
int   cg_file_append(const char *path, const char *text);
/** 删文件；成功返回 0 */
int   cg_file_delete(const char *path);
/** 拷文件（覆盖已存在的）；成功返回 0 */
int   cg_file_copy(const char *from, const char *to);
/** 目录是否存在 */
int   cg_dir_exists(const char *path);
/** 建目录（父目录必须已存在）；成功返回 0 */
int   cg_dir_create(const char *path);
/** 枚举目录：index 从 0 递增，返回 1 = 拿到一个名字（写进 out），0 = 没有了。
 *  filter 传 NULL 或 "*" 表示所有文件；不返回 "." 与 ".." */
int   cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size);

/* ---- 路径 ---- */
/** 拼路径（自动补一个分隔符） */
char *cg_path_join(const char *a, const char *b);
/** 取目录部分（不含末尾分隔符） */
char *cg_path_dir(const char *path);
/** 取文件名部分；返回 path 内部的指针，**不用释放** */
const char *cg_path_name(const char *path);
/** 取扩展名（含点，如 ".txt"）；没有扩展名返回 ""。同样不用释放 */
const char *cg_path_ext(const char *path);
/** exe 所在目录（写日志、读同目录配置文件用） */
char *cg_app_dir(void);
/** 本程序的用户数据目录 %LOCALAPPDATA%\<exe名>（会自动建），存配置/缓存用 */
char *cg_app_data_dir(void);

/* ---- 配置（INI 文本格式） ---- */
/** 读 [section] key；文件/节/键不存在时返回 def 的副本 */
char *cg_ini_read(const char *path, const char *section, const char *key, const char *def);
/** 写 [section] key=value（文件里其它内容保持不动，没有就新建）；成功返回 0 */
int   cg_ini_write(const char *path, const char *section, const char *key, const char *value);

/* ---- 文本与编码 ---- */
/** 拼字符串（printf 风格），返回堆内存 */
char *cg_str_format(const char *fmt, ...);
/** 去掉首尾空白（空格 / 制表 / 换行），返回堆内存 */
char *cg_str_trim(const char *s);
/** 比较字符串（忽略英文大小写）：相等返回 1 */
int   cg_str_equal(const char *a, const char *b);
/** 把 src 里所有 from 换成 to，返回堆内存（from 为空则返回 src 的副本） */
char *cg_str_replace(const char *src, const char *from, const char *to);
/** UTF-8 -> GBK（读老式 ANSI 文件后转成界面能显示的） */
char *cg_utf8_to_gbk(const char *utf8);
/** GBK -> UTF-8（要把界面里的中文写进老式 ANSI 文件时用） */
char *cg_gbk_to_utf8(const char *gbk);

/* ---- 剪贴板 / 外部程序 / 时间 ---- */
/** 取剪贴板文本（没有文本返回 NULL；用完 cg_free） */
char *cg_clip_get_text(void);
/** 写剪贴板文本；成功返回 0 */
int   cg_clip_set_text(const char *text);
/** 用系统默认程序打开文件 / 文件夹 / 网址；成功返回 0 */
int   cg_shell_open(const char *path_or_url);
/** 启动外部程序（cmd_line 是完整命令行）；wait=1 时返回它的退出码，wait=0 返回 0 */
int   cg_run_process(const char *cmd_line, int wait);
/** 当前时间字符串；fmt 传 NULL 用 "2026-09-21 02:33:53"（strftime 格式） */
char *cg_time_now(const char *fmt);

/* ---- 注册表（HKEY_CURRENT_USER） ---- */
/** 读注册表字符串值；没有返回 def 的副本。subkey 如 "Software\\MyApp" */
char *cg_reg_read(const char *subkey, const char *name, const char *def);
/** 写注册表字符串值（没有就新建）；成功返回 0。
 *  subkey 如 "Software\\Microsoft\\Windows\\CurrentVersion\\Run" 可做开机自启 */
int   cg_reg_write(const char *subkey, const char *name, const char *value);

/* ------------------------------------------------------------ 动态扩展 */

/** 运行时用代码追加控件（不进 .cgui）。parent 为 NULL 时挂在窗口上。
 *  新控件的 id 也参与事件名查找，所以可以先在 .cgui 里写好事件名再动态造控件。 */
cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent,
                           const char *type, const char *id,
                           int x, int y, int w, int h, const char *text);

/** 直接在 C 里给控件绑回调（覆盖 .cgui 里的名字绑定）。 */
int cg_on(cg_widget_t *w, cg_event_t ev, cg_event_fn fn);
/** 按 .cgui 里的名字绑定 */
int cg_on_named(cg_widget_t *w, cg_event_t ev, const char *callback_name);

/** 在回调内部「稍后执行」，避免在事件里立刻销毁控件。 */
typedef struct cg_timer cg_timer_t;
cg_timer_t  *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user);
void         cg_kill_timer(cg_timer_t *t);

#ifdef __cplusplus
}
#endif
#endif /* CSTACKGUI_H */
