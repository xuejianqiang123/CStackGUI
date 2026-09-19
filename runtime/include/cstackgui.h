/* cstackgui.h -- CStackGUI 运行时公共 API
 *
 * 约定：业务代码只 include 本文件，不依赖任何 GUI 框架细节。
 * 一个业务程序 = 运行时库 + 你自己的 .c 文件 = 单个 exe。
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

/* ---------------------------------------------------------------- 事件 */

typedef enum {
    CG_EVENT_CLICK = 0,   /* 单击 */
    CG_EVENT_DBLCLICK,    /* 双击 */
    CG_EVENT_CHANGE,      /* 内容 / 值 / 勾选变化 */
    CG_EVENT_SUBMIT,      /* 单行输入框回车 */
    CG_EVENT_SELECT,      /* 选择项变化（列表 / 标签页 / 树 / 表格 / 日历） */
    CG_EVENT_RELEASE,     /* 拖动结束（滑块松手才算，避免拖动过程中刷屏） */
    CG_EVENT_EXPAND,      /* 展开节点（树） */
    CG_EVENT_COLLAPSE,    /* 折叠节点（树） */
    CG_EVENT_CLOSE,       /* 窗口关闭前（窗口级事件） */
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

cg_app_t   *cg_app_of(const cg_widget_t *w);
cg_widget_t *cg_find(cg_app_t *app, const char *id);
void        cg_quit(cg_app_t *app);
/** 在窗口的 close 回调里调用：这次不关了（比如"还有没保存的内容"）。
 *  只有 close 回调里调才有意义。 */
void        cg_cancel_close(cg_app_t *app);
void        cg_set_title(cg_app_t *app, const char *title);

/** 把 .cgui 文档树打印到 stdout（调试/自检用） */
void cg_dump_doc(cg_app_t *app);

/* ------------------------------------------------------------ 控件读取 */

const char *cg_widget_id(const cg_widget_t *w);
const char *cg_widget_type(const cg_widget_t *w);
const char *cg_widget_text(const cg_widget_t *w);
int         cg_widget_x(const cg_widget_t *w);
int         cg_widget_y(const cg_widget_t *w);
int         cg_widget_w(const cg_widget_t *w);
int         cg_widget_h(const cg_widget_t *w);
int         cg_widget_visible(const cg_widget_t *w);
int         cg_widget_enabled(const cg_widget_t *w);
int         cg_widget_checked(const cg_widget_t *w);
int         cg_widget_value(const cg_widget_t *w);
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
