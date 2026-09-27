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
    CG_EVENT_ARRIVE,      /* 串口组件收到数据（serial；数据用 cg_widget_serial 取句柄读） */

    /* ---------------- 窗口级事件 ----------------
     * 全部绑在窗口伪控件（id = CG_WINDOW_ID）上，写在 .cgui 的 app.events 里。
     */
    CG_EVENT_READY,         /* 创建完毕：窗口与全部控件都建好、**还没显示**之前派发
                             * （所以回调里可以调窗口/控件 API；首次激活 firstactivate 排在它后面） */
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
    CG_EVENT_SPIN,          /* 调节钮被按下（输入框右侧的上下箭头，delta 是 ±1） */
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
/** 多行编辑框滚到最底部（串口监视器那种"新数据始终可见"）；非编辑框 / NULL 空转 */
void cg_edit_scroll_end(cg_widget_t *w);
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

/* ------------------------------------------------------------ 控件绑定
 * 让两个控件一直保持一致，省掉"自己写两个回调互相 set"的活。
 * 最常用的一例：滑块拖动时旁边的标签跟着显示数字。
 *
 *   .cgui 里写： { "id": "lab_value", "type": "label",
 *                  "bind": { "src": "sld_level" } }
 *   C 代码里写： cg_bind(cg_find(app, "sld_level"), cg_find(app, "lab_value"), 0);
 *
 * 抄什么由**目标的类型**决定，不用额外配置：
 *   滑块/进度/圆环/微调/评分/分页/标签页/列表/下拉/工具栏 -> 抄值
 *   复选框/单选钮/开关                                    -> 抄勾选状态
 *   其它（能显示文字的：标签/输入框/状态栏/按钮...）       -> 源是输入框就抄文字，
 *                                                            否则把源的数值写成十进制
 *
 * two_way 非 0 表示双向：目标自己被用户改动时也推回源（滑块配微调框、
 * 复选框配开关这类"两个入口改同一个值"的用法）。
 *
 * 注意：绑定的同步不会触发目标的 change 回调 —— 它是"跟着变"，不是用户操作；
 * 也因此不存在回调互推的死循环。想在被同步时做事，请绑 change 到源控件上。
 */
int cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way);

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

/* ---- 运行时改菜单（v1：纯菜单项）
 *
 * 定位一律用 .cgui 里的项 id；顶层用 parent_id = NULL 或 ""。
 * 改完**自动重建**（menubutton 的弹出菜单本来就是每次点击重建，不需要同步）；
 * 只改内存里的菜单，**不写回 .cgui** —— 重开界面回到文件里那份。
 *
 * 三条要知道的：
 *   ① 往某个下拉里加项，那个下拉**必须先在 .cgui 里有 id**（画布菜单项 DSL 写 `文件 | menu_file`）；
 *   ② 运行时新增的项**不支持引用动作**（要快捷键就在 .cgui 里写固定项，或用 cg_menu_last_id 分发）；
 *   ③ 对**动作项**调 set_text / set_enabled / set_checked 不生效（那三项归动作管），只记一条 WARN。
 *
 * 逐字语义（pos / "-" 分隔线 / 返回值 / 索引平移）见 docs/CGUI-FORMAT.md 第 2.7 节。
 */
int  cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos,
                      const char *text, const char *id);   /* 返回新项在行表里的下标；失败 -1 */
int  cg_menu_remove_item(cg_widget_t *w, const char *id);  /* 返回删掉的行数（含子树）；失败 -1 */
int  cg_menu_clear_items(cg_widget_t *w, const char *parent_id); /* 同上；下拉项自身保留 */
void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text);
void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled);
void cg_menu_set_item_checked(cg_widget_t *w, const char *id, int checked);

/* ------------------------------------------------------------ 动作（actions）
 * **一条命令写一次，挂在多处 UI 上**：文字 + 快捷键 + 一个回调名。
 * 菜单项和控件用 `.cgui` 的 "action" 字段引用它，于是「一个回调、一处启用状态」
 * 同时管住所有引用点 —— 不用再给菜单项和按钮各绑一份回调、各改一次灰不灰。
 *
 *   { "actions": {
 *       "save": { "text": "保存(&S)", "accel": "Ctrl+S", "handler": "on_save" },
 *       "grid": { "text": "显示网格", "handler": "on_grid_toggle", "checked": true }
 *   } }
 *   { "id": "mnu_file", "type": "menu", "nodes": [
 *       { "text": "文件(&F)", "children": [
 *           { "action": "save" },                       // 文字与快捷键都从 action 来
 *           { "action": "grid" }
 *       ]}
 *   ]}
 *
 * 回调签名与事件回调完全一样，但有两个固定值：
 *   - `self` 恒为**主窗口伪控件**（`cg_widget_id(self)` 是 `"__window"`）——
 *     因为一个动作可能有三个入口，"从哪个入口点的"不是业务该依赖的信息；
 *   - `ev` 恒为 `CG_EVENT_CLICK`（与"菜单项点击 = click"是同一套词汇）。
 *   一个函数挂了多个动作时，用 `cg_action_name(self)` 问是哪一个。
 *
 * 快捷键（`accel`）由**消息循环里的加速键表**处理，所以**焦点在输入框里照样生效**
 * （窗口级 keydown 事件做不到这一点：EDIT 会吃掉按键）。语法：
 *
 *   [Ctrl+][Shift+][Alt+]键
 *   键 = A-Z / 0-9 / F1-F24 / Esc / Enter / Tab / Space / Back(退格) / Delete /
 *        Insert / Home / End / PageUp / PageDown / Left / Right / Up / Down
 *
 * **不接受无修饰键的单个字母或数字**（裸 `S` 会让用户没法往输入框里打 s）；
 * 裸功能键（`F5`）可以。解析不了的 accel 会被忽略并写警告日志，`--selftest` 报错。
 *
 * 启用状态是**动作的**：`cg_action_set_enabled()` 会把所有引用它的菜单项与控件一起
 * 变灰/恢复。反过来，你手动 `cg_widget_set_enabled(w, 0)` 禁用某个按钮**不会**影响动作，
 * 但它会在下一次动作同步时被覆盖回动作的状态 —— 要禁就禁动作。
 *
 * v1 的边界（有意不做，别猜）：工具栏（toolbar）的按钮**不支持**引用动作（它的条目模型
 * 是纯文字列表）；没有运行时增删菜单项的 API；动作的 `text` 不能在运行时改。
 */

/** 触发一个动作：快捷键、菜单项点击、控件点击最终都走到这里。
 *  @return 1 = 真的调用了回调；0 = 没有这个动作 / 动作被禁用 / handler 没登记 */
int cg_action_invoke(cg_app_t *app, const char *name);

/** 启用 / 禁用一个动作，并把结果同步到所有引用它的 UI（菜单项灰掉、控件灰掉）。
 *  动作不存在时是空操作。 */
void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled);
/** 动作当前是否启用；动作不存在返回 0。 */
int  cg_action_is_enabled(const cg_app_t *app, const char *name);

/** 勾选 / 取消勾选一个动作，并同步到所有引用它的**菜单项**（打勾或去掉）。
 *  动作不存在时是空操作。 */
void cg_action_set_checked(cg_app_t *app, const char *name, int checked);
/** 动作当前是否勾选；动作不存在返回 0。 */
int  cg_action_is_checked(const cg_app_t *app, const char *name);

/** 最近一次被激活的动作名（在动作的回调里用 self 取）；没有则返回 NULL。 */
const char *cg_action_name(const cg_widget_t *self);

/** 动作个数 / 第 i 个动作名（遍历用，参数越界返回 NULL）；调试与自检用。 */
int         cg_action_count(const cg_app_t *app);
const char *cg_action_at(const cg_app_t *app, int i);
/** 动作的快捷键（规范化后的显示形式，如 "Ctrl+S"）；没有或动作不存在返回 ""。 */
const char *cg_action_accel(const cg_app_t *app, const char *name);

/* ------------------------------------------------------------ 提示与时钟 */

/** 鼠标悬停提示（tooltip）；text 传空串清除。对所有控件生效。 */
void cg_widget_set_hint(cg_widget_t *w, const char *text);
const char *cg_widget_hint(const cg_widget_t *w);

/** 时钟（timer 控件）的触发周期，毫秒；0 = 停止，>0 = 立即按新周期重新计时 */
void cg_widget_set_period(cg_widget_t *w, int period_ms);
int  cg_widget_period(const cg_widget_t *w);

/* ------------------------------------------------------------ 串口组件（serial 控件）
 * 无界面控件：.cgui 里只是个组件（画布上是图标，运行时不建窗口），
 * 参数（端口/波特率/数据位/停止位/校验）写在控件属性里，"数据到达"（arrive 事件）
 * 由运行时在主线程派发 —— 数据落到哪个控件由用户代码决定：
 *
 *   void on_arrive(cg_widget_t *self, cg_event_t ev, void *user) {
 *       char buf[4096];
 *       int  n = cg_serial_read(cg_widget_serial(self), buf, sizeof buf);   // 非阻塞
 *       if (n > 0) cg_term_write(cg_find(app, "term_log"), CG_TERM_RX, buf, n);
 *       // 想同时喂图表 / 列表，就在这里继续写 —— 一份数据去哪全由这行代码说了算
 *   }
 *
 * 端口/波特率既可以在画布属性里填死，也可以运行时用 cg_widget_serial_open(w, "COM4")
 * 动态指定（界面上的端口下拉就是给这用的）。未打开时 cg_widget_serial 返回 NULL，
 * 回调务必 NULL 安全（自检模式会合成触发 arrive，而那时链路不存在）。 */

/* ------------------------------------------------------------ 分隔条（splitter）
 * 能拖动的分区边界：拖它 = 撑开它**前面那一块**，它之后的兄弟整体让位，同一容器里
 * **最后一块**吃掉余量（反过来缩水）。所以 2 段布局就是"上/左变大、下/右变小"：
 *
 *   [日志区]───────────────
 *   [分隔条]        <- 拖这根：日志区变高、下面的发送区变矮
 *   [发送区]
 *
 * [pane1][分隔条][pane2][分隔条][pane3] 这种多段布局也自洽 —— 每根分隔条只认
 * "前一块 + 尾巴"，所以用不上布局引擎。
 *
 * 方向由 .cgui 的 format 决定：缺省 "col" = 左右分栏（一根竖条），"row" = 上下分行（横条）。
 * 两侧被拖到最小尺寸（16px）就停住。拖完派发一次 release 事件 —— 适合在那里把布局
 * 记进 INI，下次启动用 cg_splitter_set 恢复。
 * 没有矩形的东西（菜单栏、时钟）不参与布局。
 */
int  cg_splitter_pos(const cg_widget_t *w);
void cg_splitter_set(cg_widget_t *w, int pos);

/* ------------------------------------------------------------ 终端 / 日志视图（terminal）
 * 通信类程序最需要的东西：把收到的字节丢进一个只读的行视图，格式与上限都由控件管。
 *
 *   cg_term_write(term, CG_TERM_RX, buf, n);        // 收到的一段数据
 *   cg_term_write(term, CG_TERM_TX, "AT\r\n", -1);  // 发出（len 传 -1 就按字符串取长度）
 *
 * 几个约定（写清楚省得猜）：
 *   - **一次调用 = 一段数据**：里面带 `\r\n` 会**自动切成多行**（串口一次收
 *     `"OK\r\nERR"` 是常态），`\r` 丢掉；一次调用只刷新一次界面。
 *   - 方向决定行首标记与颜色：`RX ` / `TX ` / `-- `。
 *   - HEX / 文本模式由 `.cgui` 的 `format` 决定（`"hex"` 或留空），**只影响之后写入的行**。
 *   - 行首要不要 `[HH:MM:SS]` 由 `.cgui` 的 `checked` 决定（画布上显示成「显示时间戳」）。
 *   - 最多保留多少行由 `.cgui` 的 `value` 决定（缺省 500），超了从最老的丢。
 *   - 文本模式只把控制字符换成 `.`，0x80 以上原样保留 —— 所以中文（UTF-8 多字节）不会被搅坏。
 *   - 内容可以选中复制，也能 `cg_term_save()` 存成文件。
 */
#define CG_TERM_RX  1   /* 收到 */
#define CG_TERM_TX  2   /* 发出 */
#define CG_TERM_SYS 3   /* 自己记的（连接状态、错误提示之类） */

void cg_term_write(cg_widget_t *w, int dir, const void *data, int len);
void cg_term_clear(cg_widget_t *w);
void cg_term_set_hex(cg_widget_t *w, int on);
int  cg_term_hex(const cg_widget_t *w);
void cg_term_set_timestamp(cg_widget_t *w, int on);
void cg_term_set_max_lines(cg_widget_t *w, int lines);
int  cg_term_count(const cg_widget_t *w);
/** 存成文本文件（UTF-8 + CRLF，不含方向标记）；成功返回 0 */
int  cg_term_save(cg_widget_t *w, const char *path);

/* ------------------------------------------------------------ 折线 / 柱状图（chart）
 * 实时数据可视化的那块：业务按采样节奏 push 点，窗口、范围、网格与绘制都由控件管。
 *
 *   cg_chart_push_all(cht, temps, 2);   // 一次给所有系列各推一个点（多通道采样最常用）
 *   cg_chart_push(cht, 0, 42);          // 只推给第 0 个系列
 *
 * 系列在 .cgui 里用 items 声明（每个条目是一个系列名），颜色按调色板自动分配；
 * 什么都不写就是"单系列"，不用配：
 *
 *   { "id": "cht_temp", "type": "chart", "value": 200, "min": 0, "max": 100,
 *     "items": ["温度", "湿度"] }
 *
 * 这几个字段在这个控件上有专门含义：
 *   value   = 窗口点数（每系列保留多少个点，缺省 120，最多 4096）
 *   min/max = Y 轴范围（缺省 0~100），超出的值按边界裁剪后画
 *   format  = "line"（缺省，折线）/ "bar"（柱状）
 *
 * 两个刻意的做法（都是踩过才知道的）：
 *   - **窗口满了整体左移一格**，不做环形缓冲：cap 只有几十到几百，memmove 一下比维护
 *     读写指针 + 一圈边界判断便宜，也不会出现"绕回时算错下标"这种隐蔽 bug。
 *   - 点数超过绘图区像素宽度时**不抽点**，而是对每个像素列取该列覆盖点的 min/max 画一条
 *     竖线（LVGL 的做法）：既不慢，也不会刚好把那个尖峰抽掉。抽点最坑的就是丢毛刺。
 *
 * 实时性靠"业务用 cg_set_timer 定周期 push"，**不要另开线程碰控件**（控件不是线程安全的）。
 */
void cg_chart_push(cg_widget_t *w, int series, int value);
void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues);
void cg_chart_clear(cg_widget_t *w);
/** 运行时加一个系列（返回下标；到上限或没内存了返回 -1） */
int  cg_chart_add_series(cg_widget_t *w, const char *name);
int  cg_chart_series_count(const cg_widget_t *w);
/** 某系列已经填了多少点 */
int  cg_chart_filled(const cg_widget_t *w, int series);
/** 读回某个点（下标 0 = 最老的点）；越界返回 0 */
int  cg_chart_point(const cg_widget_t *w, int series, int index);
void cg_chart_set_range(cg_widget_t *w, int min, int max);

/* ------------------------------------------------------------ 状态灯（led）
 * 最小的一块"看得见状态"的控件：通信连上没有、串口在不在收、设备就绪没有。
 *
 *   cg_led_set(cg_find(app, "led_net"), 1);   // 0 灭 / 1 亮 / 2 闪
 *
 * .cgui 里：{ "id": "led_net", "type": "led", "value": 1, "format": "round",
 *             "period": 500, "text": "已连接" }
 *
 * 这几个字段在这个控件上有专门含义：
 *   value    = 状态 0 灭 / 1 亮 / 2 闪（越界夹到 0~2）
 *   format   = "round"（缺省，圆）/ "square"（方）
 *   period   = 闪烁周期毫秒（只管"闪"；缺省 500，小于 100 会被兜成 500）
 *   text     = 灯右边的说明文字；为空时灯自己占满整个矩形
 *   style.fg = 亮起的颜色（缺省绿）
 *
 * 闪烁**由控件自己定时重绘**（半个周期亮、半个周期灭），业务只要 cg_led_set(w, 2)，
 * 不用自己开时钟。它是纯显示控件：没有事件，点它没有反应。
 */
void cg_led_set(cg_widget_t *w, int state);
int  cg_led_state(const cg_widget_t *w);
/** 亮起的颜色（等价于 cg_widget_set_colors(w, rgb, CG_COLOR_NONE)） */
void cg_led_set_color(cg_widget_t *w, unsigned rgb);

/* ------------------------------------------------------------ 仪表盘（meter）
 * 看单个读数"现在处在量程的哪个位置"：温度、转速、电压都合适。
 *
 * .cgui 里：{ "id": "mt_temp", "type": "meter", "value": 40, "min": 0, "max": 100,
 *             "format": "arc", "text": "°C" }
 *
 * 表盘长这样：底弧（灰）+ 值弧（彩色，随值增长）+ 刻度线 + 指针 + 中心大数值 + 单位。
 *   value    = 当前值（被 min/max 夹，和滑块同一套语义）
 *   min/max  = 量程（缺省 0~100；max<=min 时按 range=1 处理，不会除零）
 *   format   = "arc"（缺省，270° 圆弧）/ "half"（半圆 180°）
 *   text     = 单位文字，画在中心大数值下面（可空）
 *   style.fg = 圆弧/指针主色（缺省 #6750A4）
 *
 * 纯显示控件：没有事件。
 */
void cg_meter_set(cg_widget_t *w, int value);
void cg_meter_set_range(cg_widget_t *w, int min, int max);

/* ------------------------------------------------------------ 串口（cg_serial）
 * 这套东西最常被用来的场景就是"跟一块板子说话"。有了它，业务不用再 include windows.h 去
 * CreateFile("\\\\.\\COMx")，"只 include cstackgui.h" 的约定继续成立。
 *
 * 典型用法（**必须这么写**，原因见下面那段）：
 *
 *   static cg_timer_t *g_poll;
 *
 *   static void poll_tick(void *user) {
 *       cg_app_t *app = (cg_app_t *)user;
 *       char      buf[4096];
 *       int       n = cg_serial_read(g_ser, buf, sizeof buf);    // 非阻塞：没数据返回 0
 *       if (n > 0) cg_term_write(cg_find(app, "term"), CG_TERM_RX, buf, n);
 *       g_poll = cg_set_timer(app, 50, poll_tick, app);          // ← 必须自己重挂（见下）
 *   }
 *
 *   void on_open_click(...) {                                    // 连接按钮
 *       g_ser = cg_serial_open("COM3", 115200);
 *       if (!g_ser) {                                            // 失败原因直接写进日志视图
 *           cg_term_write(cg_find(app, "term"), CG_TERM_SYS,
 *                         cg_serial_last_error(NULL), -1);
 *           return;
 *       }
 *       g_poll = cg_set_timer(app, 50, poll_tick, app);           // 起第一颗
 *   }
 *   void on_close_click(...) { cg_kill_timer(g_poll); cg_serial_close(g_ser); }
 *
 * **注意 `cg_set_timer()` 是一次性的**：到点触发之后就自己被回收了，**不会自动重挂**。
 * 所以"定周期"的样子就是在回调末尾再挂一颗（上面 poll_tick 最后一行）。忘了重挂的表现是
 * "界面只刷新了第一轮"，很难往定时器上想 —— 完整例子见 examples/serialterm。
 *
 * **线程模型（唯一被支持的用法，也是唯一容易出事的地方）**：
 * cg_serial_open() 内部起一个读线程，它只做一件事 —— 把驱动里的字节搬进接收缓冲。
 * **你不能在那个线程里碰控件**（这不是本项目的风格偏好，是 Win32 的硬约束）。
 * 所以要像上面那样用 cg_set_timer 定周期去"取"，而不是指望被"推"。
 * 完整例子见 examples/serialterm。
 *
 * v1 的边界（有意不做，别猜）：只支持 8N1（不收数据位/停止位/校验位/流控）；一个句柄一条线；
 * 不自动重连；cg_serial_write 是同步写（带超时），不是写队列。
 */
typedef struct cg_serial_t cg_serial_t;

/** 枚举本机串口：每行一个端口名写入 buf（"COM3\nCOM7\n"），返回写入字节数。
 *  没有端口时返回 0（不是错误）。走注册表，不引任何额外链接依赖。 */
int  cg_serial_list(char *buf, int cap);

/** 打开串口（"COM3" 或 "\\\\.\\COM3" 都收），固定 8N1。失败返回 NULL；
 *  失败原因用 cg_serial_last_error(NULL) 取（打开失败时没有句柄可问）。 */
cg_serial_t *cg_serial_open(const char *port, int baud);

/** 打开串口（全参数版）：bits 数据位 5..8、stop 停止位 1..2、parity 0=无/1=奇/2=偶。
 *  参数不合法时**干净失败**（不合法的参数不往驱动送，原因见 cg_serial_last_error(NULL)）；
 *  cg_serial_open() 等价于 open_ex(port, baud, 8, 1, 0)。 */
cg_serial_t *cg_serial_open_ex(const char *port, int baud, int bits, int stop, int parity);

/** 关掉串口并回收读线程。**幂等、NULL 安全**（没连上时直接调它也没事）。
 *  它会等读线程真的退出再释放缓冲，所以最坏会卡一个读超时周期（约 50ms）。 */
void cg_serial_close(cg_serial_t *s);

/** 发送数据。返回实际写出的字节数，< 0 表示失败（原因见 cg_serial_last_error）。 */
int  cg_serial_write(cg_serial_t *s, const void *data, int len);

/** 从接收缓冲取最多 cap 字节（**非阻塞**：没数据返回 0，不是错误）。
 *  返回的字节数可能是**半个 UTF-8 序列** —— 要按文本显示的话，把不满意的尾巴留到下次
 *  （终端控件是按字节灌的，所以直接 cg_term_write 也行，只是那一帧可能显示成截断的）。 */
int  cg_serial_read(cg_serial_t *s, void *buf, int cap);

/** 接收缓冲里还有多少字节没取走 */
int  cg_serial_available(const cg_serial_t *s);

/** 丢掉接收缓冲里还没取走的数据（"清空接收"按钮用） */
void cg_serial_flush_rx(cg_serial_t *s);

/** 累计收 / 发了多少字节（状态栏显示用；只增不减，要归零就重连） */
long long cg_serial_rx_bytes(const cg_serial_t *s);
long long cg_serial_tx_bytes(const cg_serial_t *s);

/** 最后一次错误的中文描述（"找不到这个端口"这种）；没出错返回 ""。
 *  s 传 NULL 时返回**最近一次 cg_serial_open 失败**的原因 —— 打开失败时用得上。
 *  返回的指针指向内部缓冲，下次报错会被覆盖；要留就自己拷走。 */
const char *cg_serial_last_error(const cg_serial_t *s);

/* ---- 串口组件（serial 控件）的控件侧 API：参数见上面"串口组件"一段 ---- */

/** 取串口组件的链路句柄（未打开/类型不对返回 NULL；回调里先判 NULL） */
cg_serial_t *cg_widget_serial(cg_widget_t *w);

/** 按控件属性打开链路（端口名可省略 = 用控件里配的）；已打开则先关旧的再开。
 *  返回 0 成功；失败返回 -1（原因见 cg_serial_last_error(NULL)，不影响界面运行）。 */
int  cg_widget_serial_open(cg_widget_t *w, const char *port);

/** 关掉串口组件的链路（幂等、NULL 安全；未打开时调它也没事） */
void cg_widget_serial_close(cg_widget_t *w);

/** 改串口组件的波特率（界面上的波特率下拉就是给这用的）；
 *  链路开着时按新参数原样重开（关旧的再开新的），没开就只记住等下次打开用。 */
void cg_widget_serial_set_baud(cg_widget_t *w, int baud);

/* ------------------------------------------------------------ 网口（cg_net）
 * 与串口（cg_serial）**同一套形状**，因为面对的是同一类问题：网络 I/O 不能做在 UI 线程上
 * （等数据的每一毫秒都是消息循环被堵住的毫秒）。所以同样是"运行时起一个读线程把字节搬进
 * 环形缓冲，业务在时钟里拉"：
 *
 *   客户端：  n = cg_net_connect("192.168.1.10", 8080, 3000);
 *            时钟里：n2 = cg_net_read(n, buf, sizeof buf);  ->  cg_term_write(term, CG_TERM_RX, buf, n2)
 *            掉线判断：cg_net_connected(n) 变 0（对端关掉了连接）
 *
 *   服务端：  srv = cg_net_listen(8080, 4);
 *            时钟里：c = cg_net_accept(srv, 0);   // 0 = 不等，立刻返回；没连接返回 NULL（**不是错误**）
 *                    if (c) 把它存进自己的连接数组（一个句柄 = 一条连接）
 *
 * **一个句柄 = 一条连接**：服务端要同时管多个客户端，就自己拿一个句柄数组（比如 16 个），
 * 在时钟里逐个 cg_net_read —— 本层不引入"客户端列表"这个概念，API 面保持一条线。
 *
 * 错误处理与串口一致：失败返回 NULL / 负数，原因用 cg_net_last_error() 取；连接/监听失败时
 * 句柄是 NULL，所以 cg_net_last_error(NULL) 给的就是"刚刚那次失败的原因"。
 *
 * v1 的边界（有意不做，别猜）：**只有 TCP**（UDP 的 sendto/recvfrom 下一步）；不做 TLS；
 * 不自动重连；写是同步的（带超时，不是写队列）；接收缓冲 64KB 满了丢最老的（与串口同策略）。
 * 链接：需要 winsock2（`-lws2_32`，构建脚本里已经加上）。
 */
typedef struct cg_net_t cg_net_t;

/** 连到一台主机；host 传 NULL / 空串 = 127.0.0.1（本机自测最常用）。
 *  timeout_ms <= 0 按 3000 算。用非阻塞 connect + select 实现超时 —— 坏地址不会把界面冻住。 */
cg_net_t *cg_net_connect(const char *host, int port, int timeout_ms);

/** 监听端口（backlog <= 0 按 4）。绑不上（端口被占）返回 NULL，原因见 cg_net_last_error(NULL)。 */
cg_net_t *cg_net_listen(int port, int backlog);

/** 收一个连接。**timeout_ms <= 0 = 不等，立刻返回**（适合放在时钟里轮询）；
 *  没有连接时返回 NULL —— 那是正常情况，不是错误。每个连接是一个独立句柄，用完 cg_net_close。 */
cg_net_t *cg_net_accept(cg_net_t *srv, int timeout_ms);

/** 发送（同步，带 1 秒超时）；返回实际写出字节数，< 0 失败 */
int  cg_net_write(cg_net_t *n, const void *data, int len);
/** 从接收缓冲取最多 cap 字节（**非阻塞**：没数据返回 0）；对端已关闭时也能把剩下的取完 */
int  cg_net_read(cg_net_t *n, void *buf, int cap);
/** 接收缓冲里还有多少字节没取走 */
int  cg_net_available(const cg_net_t *n);
/** 对端还在吗（对方关闭后变 0；监听句柄恒为 0）—— 这是判断"掉线"的唯一信号 */
int  cg_net_connected(const cg_net_t *n);
/** 累计收 / 发字节数（状态栏用） */
long long cg_net_rx_bytes(const cg_net_t *n);
long long cg_net_tx_bytes(const cg_net_t *n);
/** 对端 "ip:port"（监听句柄返回"（监听中）"） */
const char *cg_net_peer(const cg_net_t *n);
/** 最后一次错误的中文描述；n 传 NULL 时给"最近一次连接/监听失败"的原因 */
const char *cg_net_last_error(const cg_net_t *n);
/** 关掉连接或监听（幂等、NULL 安全；会等读线程退出，所以最坏卡一个收超时周期约 50ms） */
void cg_net_close(cg_net_t *n);

/* ------------------------------------------------------------ 网口：UDP（数据报）
 * **为什么不套上面那套"读线程 + 环形缓冲"**：数据报有两样东西必须原样交到业务手里 ——
 * **报文边界**与**来源地址**。字节环会把两者一起毁掉（把两个数据报粘成一条流、丢掉"谁发的"）。
 * 所以 UDP 是**拉取式**的：你自己调 cg_net_recvfrom()，一次拿一个完整数据报 + 它的来源。
 * 于是 UDP 句柄上**没有读线程**，流那套 API 也不适用（见下面的返回语义）。
 *
 *   cg_net_t *u = cg_net_udp_open(9000, 1);          // 本地 9000、允许广播
 *   cg_net_sendto(u, "255.255.255.255", 9000, "hi", 2);   // 广播发现：板子回包就知道它的 IP
 *
 *   时钟里（照串口/网口的"在时钟里拉"那一套）：
 *       char buf[1472], ip[64]; int port = 0, n;
 *       n = cg_net_recvfrom(u, ip, sizeof ip, &port, buf, sizeof buf);
 *       if (n >= 0) {                                  // >= 0 才是"收到了一个数据报"
 *           char *line = cg_str_format("[%s:%d] %.*s", ip, port, n, buf);
 *           cg_term_write(term, CG_TERM_RX, line, -1);
 *           cg_free(line);
 *       }
 *
 * **返回值语义（和 TCP 那套不一样，别套）**：
 *   > 0  收到的字节数（已经带出来源 ip/port）
 *   = 0  收到了一个**空数据报**（UDP 允许，是正常现象，不是错误）
 *   = -1 暂时没有数据（**不是错误**）；真出错时 cg_net_last_error() 才有内容
 * 想区分"没有数据"和"出错"，看 cg_net_last_error() 是不是空的。
 *
 * 边界：**UDP 句柄上只有 sendto / recvfrom / close / last_error / rx_bytes / tx_bytes 可用**；
 * 流那套（read / available / connected / write）在 UDP 句柄上分别返回 0 / 0 / 0 / -1 ——
 * 与其"悄悄支持"一个语义不对的行为，不如明确拒绝。
 */
cg_net_t *cg_net_udp_open(int local_port, int broadcast);
/** 发一个数据报（每次调用解析一次主机名：填 IP 无所谓，填域名会多花一点时间）。
 *  返回实际发出的字节数，< 0 失败。*/
int  cg_net_sendto(cg_net_t *n, const char *host, int port, const void *data, int len);
/** 收一个数据报（非阻塞，一次一个，带出来源）。
 *  ip_out 可传 NULL 表示不关心来源；port_out 同理。返回值语义见上面那段。 */
int  cg_net_recvfrom(cg_net_t *n, char *ip_out, int ip_cap, int *port_out, void *buf, int cap);

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
/** 复制一个字符串（堆内存，用完 cg_free；s 为 NULL 时返回 NULL）
 *  与 cg_str_format("%s", s) 等价，但不用过一遍 printf —— 想留下借用串的副本时用它。 */
char *cg_str_dup(const char *s);
char *cg_str_trim(const char *s);
/** 比较字符串（忽略英文大小写）：相等返回 1 */
int   cg_str_equal(const char *a, const char *b);
/** 把 src 里所有 from 换成 to，返回堆内存（from 为空则返回 src 的副本） */
char *cg_str_replace(const char *src, const char *from, const char *to);
/** UTF-8 -> GBK（读老式 ANSI 文件后转成界面能显示的） */
char *cg_utf8_to_gbk(const char *utf8);
/** GBK -> UTF-8（要把界面里的中文写进老式 ANSI 文件时用） */
char *cg_gbk_to_utf8(const char *gbk);

/* ---- 编码：十六进制 / Base64 / URL ----
 * 都是**调用方给缓冲**（与 cg_open_file_dialog / cg_serial_read 同一口径），
 * 不在这里 malloc —— 业务多是"把结果喂给控件或直接发出去"，少一次分配少一处泄漏面。
 * 返回值统一是**写出的长度**（不含结尾 '\0'）：参数或内容非法返回 -1；
 * 解码类缓冲放不下返回 -2（编码类需要的长度能事先算出来，放不下也归 -1）。
 */

/** 字节 -> 十六进制文本。sep 是每字节之间的分隔串（NULL/"" 不隔，显示常用 " "），
 *  upper 非 0 用大写。out 需要 len*(2+strlen(sep))+1 字节；返回写入长度 */
int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size);

/** 十六进制文本 -> 字节。空格 / 制表 / 换行 / 逗号 / 连字符都当分隔符，大小写都认，
 *  "1A2B" 这种不写分隔的也算对。返回字节数；非法字符或只剩半个字节返回 -1；
 *  out 放不下返回 -2。（语义与 examples\serialterm、netterm 里那两段手写实现一致） */
int cg_hex_decode(const char *text, void *out, int out_size);

/** Base64 编码（RFC 4648，带 '=' 填充）。out 需要 ((len+2)/3)*4+1 字节；返回写入长度 */
int cg_base64_encode(const void *data, int len, char *out, int out_size);

/** Base64 解码。空白与换行忽略；**带不带 '=' 填充都认**（JWT 那类不填充的也能解）。
 *  长度不合法（不含规填充、尾部有多余位）返回 -1。out 需要 (strlen+3)/4*3 字节 */
int cg_base64_decode(const char *text, void *out, int out_size);

/** URL 编码（百分号）。不编码 A-Za-z0-9-_.~，其余按**字节**编成 %XX —— 所以 UTF-8 中文
 *  会变成多个 %XX，这是对的。space_plus 非 0 时空格编成 '+'（表单风格）。
 *  out 需要 strlen*3+1 字节；返回写入长度 */
int cg_url_encode(const char *text, int space_plus, char *out, int out_size);

/** URL 解码：'%XX' 与 '+'（'+' 一律还原成空格）。'%' 后面不是两位十六进制返回 -1；
 *  out 需要 strlen+1 字节 */
int cg_url_decode(const char *text, char *out, int out_size);

/* ---- 进制转换与补码 ----
 * 给调试工具用的：寄存器值在十六进制/二进制/十进制之间来回看，是嵌入式与工控的日常
 * （位段看二进制、报文看十六进制、协议文档写十进制）。输出一律**大写**（与数据手册、
 * 寄存器表的写法一致）；要小写十六进制用 cg_hex_encode()。
 */

/** 整数 -> 任意进制文本（2..36）。负数带前导 '-'。返回写入长度；base 非法或放不下返回 -1 */
int cg_int_to_base(long long v, int base, char *out, int out_size);

/** 任意进制文本 -> 整数（2..36）。认前导 '-'/'+'，也认与进制相符的 0x / 0b / 0o 前缀
 *  （调试试工具里到处是 `0x1A` 这种写法）；允许首尾空白，但**中间有空白即失败**
 *  （`"1A 2B"` 是两段，不该被拼成一段）。整串必须合法、溢出也算失败。
 *  @param s 待解析文本
 *  @param base 进制 2..36
 *  @param out 结果；可传 NULL（只想问"合不合法"）
 *  @return int 1 = 成功；0 = 失败（此时 out 不动） */
int cg_base_to_int(const char *s, int base, long long *out);

/** 进制互转（字符串进、字符串出）：cg_base_convert("FF", 16, 2) -> "11111111"。
 *  返回写入长度；源串非法或放不下返回 -1（此时 out 是空串） */
int cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size);

/** 把低 bits 位按**补码**解释成有符号数：cg_to_signed(0xFFF6, 12) -> -10。
 *  工控里天天要用：报文给的是 0x…，协议说的是 12 位有符号。
 *  bits 取 1..63；给了 64 或越界就直接按 long long 原样返回（不做符号扩展）。 */
long long cg_to_signed(unsigned long long v, int bits);

/** 反向：把一个有符号数写成 bits 位补码（只保留低 bits 位）：cg_from_signed(-10, 12) -> 0xFF6。
 *  拼报文时用它 —— 负数不能直接塞进帧里。bits 取 1..63；越界按原样返回。 */
unsigned long long cg_from_signed(long long v, int bits);

/* ---- 校验（CRC） ----
 * 逐位计算、没有静态查表 —— 好处是**天然线程安全**、代码短；代价是比查表慢约 8 倍
 * （实测对协议帧与几 MB 的文件都够快；上百 MB 的大文件不划算，那种场合另说）。
 */

/** CRC32（IEEE 802.3 / zlib 同一套：多项式 0xEDB88320 反射，初值全 1，末尾取反）。
 *  文本 "123456789" 的校验值是 0xCBF43926；空输入返回 0 */
unsigned cg_crc32(const void *data, int len);

/** CRC16/Modbus（多项式 0xA001 反射，初值 0xFFFF，**不**取反）—— 工控设备协议帧最常用的那个。
 *  文本 "123456789" 的校验值是 0x4B37；空输入返回 0xFFFF。返回值低 16 位有效 */
unsigned cg_crc16(const void *data, int len);

/** 算整个文件的 CRC32（**分块读，不把文件整读进内存** —— 固件镜像动辄几十上百 MB）。
 *  @param path 文件路径（UTF-8）
 *  @param out 校验值出口；成功才写入
 *  @return int 1 = 成功；0 = 打不开或中途读出错（宁可报失败，也不给一个"看着像但不完整"的值） */
int cg_crc32_file(const char *path, unsigned *out);

/* ---- 哈希（摘要） ----
 * MD5 / SHA-1 / SHA-256 / SHA-512，**手写实现**，不调系统的 CryptoAPI。
 * 理由有两条（原先写的"会改掉用户的链接命令"是错的，已按实验更正 —— advapi32 本来就能
 * 解析，见 runtime/src/kit/cg_kit_check.c 文件头）：
 *   1. **FIPS 策略下 CryptoAPI 会拒绝 CALG_MD5**，而"核对固件 MD5"是本功能的高频用法；
 *   2. 结果不依赖客户机器上的 crypto 配置，同一个文件在哪台机器上算出来都一样。
 * 正确性靠公开向量（RFC 1321 / FIPS 180-1,2,4）+ 由 Node/OpenSSL 独立派生的检验值双重钉死。
 */

/** 算一段内存或一个文件的摘要，返回**小写十六进制**堆串（记得 cg_free()）
 *  @param algo "md5" / "sha1" / "sha256" / "sha512"，大小写不敏感
 *  @return char* 摘要串（MD5 32 字符、SHA-1 40、SHA-256 64、SHA-512 128）；
 *         算法名不认识、文件打不开、内存不足都返回 NULL */
char *cg_hash(const void *data, int len, const char *algo);
char *cg_hash_file(const char *path, const char *algo);

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

/* ---- 系统信息 ----
 * 来历：精易 `系统_` 一族有 254 条，其中**头 34 条就是"每个环境变量一个命令"**
 * （系统_COMPUTERNAME / 系统_TEMP / 系统_WINDIR / 系统_USERNAME …）—— 那是易语言没有
 * getenv 的产物。C 里有，所以这一族在 C 侧首先是**一个** cg_env()：
 *   计算机名 = cg_env("COMPUTERNAME")、用户名 = cg_env("USERNAME")、
 *   临时目录 = cg_env("TEMP")、系统目录 = cg_env("SystemRoot")、CPU 个数 = cg_env_int("NUMBER_OF_PROCESSORS", 1)
 *
 * 明确**不抄**的三类（理由与决定写进 .codebuddy/plans/kit-sysinfo.md，免得以后照抄回来）：
 *   1. 恶意软件相邻的：取宽带用户名密码、修改主页、设置 IE 代理、屏蔽任务管理器、
 *      用户名密码验证、遍历右键菜单并执行 —— 这工程的立身之本是给人做正规工具；
 *   2. 破坏性的：关机 / 强制重启 / 强制注销 / 修改开机密码 / 卸载组件 / 清空系统临时目录；
 *   3. C 里已有等价物的（34 个环境变量命令、取 DOS 路径 = cg_env("COMSPEC")）。
 * 另有一条硬约束：**不为了这些功能引入新的链接库**（MAC 地址要 iphlpapi、哈希要 advapi32 ——
 * 都会改掉用户看得见的链接命令，所以那两件都不做）。
 */

/** 读环境变量（UTF-8，值可以是很长的字符串）。没有这个变量返回 NULL。
 *  @note 走 GetEnvironmentVariableW + 内部 UTF-8 转换，**不用 getenv()** ——
 *        CRT 的窄接口给的是 ANSI/MBCS，中文用户名或路径（`C:\Users\张三`）会变成乱码，
 *        而本工程内部一律 UTF-8。名字大小写不敏感（Windows 环境变量本就如此）。 */
char *cg_env(const char *name);

/** 读环境变量当整数。没有这个变量 / 值不是合法整数（或溢出）返回 fallback。
 *  例：cg_env_int("NUMBER_OF_PROCESSORS", 1) */
int cg_env_int(const char *name, int fallback);

/** 1 = 这台机器是 64 位 Windows。**不随本进程的位数骗人**：32 位程序跑在 64 位系统上照样返回 1。
 *  环境变量 PROCESSOR_ARCHITECTURE 做不到这一点 —— 它给的是进程视角。 */
int cg_sys_is64(void);

/** 操作系统可读名，形如 "Windows 10 (10.0.19045)"；失败返回 NULL。日志头 / 工单信息用。
 *  @note 走 ntdll 的 RtlGetVersion（GetProcAddress 取，不新增链接库）：
 *        GetVersionEx 会被 exe 清单里的兼容性声明骗，本工程没有那条声明，
 *        但别人复制这段代码时多半有。 */
char *cg_sys_version(void);

/** 取路径所在磁盘的剩余 / 总容量（字节）。两个出口都可以传 NULL（只问其中一个）。
 *  @param path 盘符或盘上任意路径（`"C:\\"` 或 cg_app_dir() 都行）
 *  @return int 1 = 成功；0 = 路径不存在或调不动（此时两个出口都不动）。
 *  @note 剩余量给的是**当前用户可用**的那个（写日志前问"还写得下吗"才准）。 */
int cg_disk_space(const char *path, unsigned long long *free_bytes, unsigned long long *total_bytes);

/** 按可执行文件名查进程是否在跑（大小写不敏感，如 "explorer.exe"）。返回 1 / 0。
 *  现场排查"设备厂商的服务 / 驱动在不在"用得上。 */
int cg_process_running(const char *exe_name);

/** 本机 IPv4 地址（点分十进制，如 "192.168.1.20"）；拿不到返回 NULL。
 *  @note 拿到的是**系统默认出口网卡**的地址：内部挑一条默认路由再问本地端地址，
 *        **不发任何数据包**、也不需要 DNS（离线工控机上也能用）。多网卡时未必是
 *        "连设备的那张"—— 要枚举网卡得用 iphlpapi 的 GetAdaptersInfo，那会改掉用户的
 *        链接命令，所以不做。实现在 cg_net.c（winsock2.h 必须在 windows.h 之前，
 *        那里已经处理好了）。 */
char *cg_local_ip(void);

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
