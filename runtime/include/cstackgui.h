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
    CG_EVENT_DROP,          /* 文件被拖进窗口（窗口级事件，绑在 app.events.drop）。
                             * 回调里用 cg_drop_count(app) / cg_drop_file(app, i) 取路径 ——
                             * cg_drop_file 返回的是**副本**（堆串，用完 cg_free），回调返回后仍有效。
                             * 放在枚举末尾：事件值是位下标，插在中间会动到前面所有事件的值。 */
    CG_EVENT_COUNT
} cg_event_t;

/**
 * @brief 事件名的规范写法，也是 .cgui 中 events 对象的键名
 * @param ev 事件类型
 * @return const char* 静态字符串，不要 free；任何合法事件都有名字，不会返回 NULL
 */
const char *cg_event_name(cg_event_t ev);
/**
 * @brief 名字反查事件
 * @param name 事件名，与 cg_event_name 的输出一致（小写，如 "click"）
 * @return int 事件类型；不认识的名字返回 -1
 */
int cg_event_from_name(const char *name);

/* ------------------------------------------------------------ 不透明类型 */

typedef struct cg_app    cg_app_t;
typedef struct cg_widget cg_widget_t;

/** 窗口自身那个"控件"的 id。窗口级事件（close）就绑在它身上，
 *  想给 close 回调传业务数据时用 cg_find(app, CG_WINDOW_ID) 拿到它。 */
#define CG_WINDOW_ID "__window"

/**
 * @brief 所有控件事件的统一回调签名
 * @param self 触发事件的控件（不会是 NULL）
 * @param ev 事件类型
 * @param user cg_widget_set_user() 挂上的业务数据，默认 NULL
 * @return void 无返回值
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
/**
 * @brief 取鼠标事件的 X 坐标
 * @param self 触发事件的控件
 * @return int 相对该窗口客户区的 X 坐标（逻辑像素）；非鼠标事件返回 0
 */
int cg_event_x(const cg_widget_t *self);
/**
 * @brief 取鼠标事件的 Y 坐标
 * @param self 触发事件的控件
 * @return int 相对该窗口客户区的 Y 坐标（逻辑像素）；非鼠标事件返回 0
 */
int cg_event_y(const cg_widget_t *self);
/**
 * @brief 取功能键状态
 * @param self 触发事件的控件
 * @return int 1=Ctrl 2=Shift 4=Alt，可位或；非按键事件返回 0
 */
int cg_event_keys(const cg_widget_t *self);
/**
 * @brief 取键代码
 * @param self 触发事件的控件
 * @return int keydown / keyup 是虚拟键码（VK_*），char 是字符码；非按键事件返回 0
 */
int cg_event_key(const cg_widget_t *self);
/**
 * @brief 取滚轮滚动量
 * @param self 触发事件的控件
 * @return int 正数向上、负数向下；非滚轮事件返回 0
 */
int cg_event_delta(const cg_widget_t *self);
/**
 * @brief 取托盘事件的动作类型
 * @param self 触发事件的控件
 * @return int 1=左键单击 2=左键双击 3=右键单击；非托盘事件返回 0
 */
int cg_tray_action(const cg_widget_t *self);

/** 名字 -> 函数。表尾必须用 CG_BIND_END 结束。 */
typedef struct {
    const char *name;
    cg_event_fn fn;
} cg_binding_t;

#define CG_BIND(name_literal, fn) { (name_literal), (fn) }
#define CG_BIND_END               { NULL, NULL }

/* ---------------------------------------------------------------- 启动 */

/**
 * @brief 界面建好、消息循环开始之前调用一次的钩子
 * @param app 当前应用
 * @param user cg_app_desc_t 里 ready_user 传进来的自定义数据
 * @return void 无返回值
 * @note 用来挂业务数据（cg_widget_set_user）、做初始检查、按业务状态调初始界面
 */
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

/**
 * @brief 建窗口 + 消息循环
 * @param desc 启动描述（.cgui 来源、绑定表、钩子等），函数返回前一直要有效
 * @return int 进程退出码（业务回调里 cg_quit 退出时为 0）
 */
int cg_app_run(const cg_app_desc_t *desc);

/**
 * @brief 不开窗的自检：解析 .cgui、打印控件树、对每个绑定了回调的控件合成一次事件
 * @param desc 启动描述，语义同 cg_app_run；不需要窗口相关字段
 * @return int 0 全部通过，非 0 表示有解析失败或回调没命中
 * @note 用于 CI / 命令行确认「文件能解析、回调能命中」
 */
int cg_app_selftest(const cg_app_desc_t *desc);

/* ------------------------------------------------------------ 应用级操作 */

/**
 * @brief 从控件拿到它所属的应用（回调里最常用的第一步）
 * @param w 控件
 * @return cg_app_t* 所属应用；w 为 NULL 时返回 NULL
 */
cg_app_t   *cg_app_of(const cg_widget_t *w);
/**
 * @brief 按 id 找控件（id 在画布「属性」里定，就是 .cgui 里的 id 字段）
 * @param app 应用
 * @param id 控件 id，CG_WINDOW_ID 可拿到窗口伪控件
 * @return cg_widget_t* 找到的控件，不存在返回 NULL（运行时归它所有，别 free）
 */
cg_widget_t *cg_find(cg_app_t *app, const char *id);
/**
 * @brief 退出消息循环，进程随后结束
 * @param app 应用
 * @return void 无返回值（退出码 0）
 */
void        cg_quit(cg_app_t *app);
/**
 * @brief 在窗口的 close 回调里调用：这次不关了
 * @param app 应用
 * @return void 无返回值
 * @note 比如"还有没保存的内容"时用；只有 close 回调里调才有意义
 */
void        cg_cancel_close(cg_app_t *app);
/**
 * @brief 改窗口标题
 * @param app 应用
 * @param title UTF-8 标题，函数内部会拷贝，调用方可以立刻释放
 * @return void 无返回值
 */
void        cg_set_title(cg_app_t *app, const char *title);
/**
 * @brief 当前有没有真正的窗口（--selftest 不开窗）
 * @param app 应用
 * @return int 1 有窗口、0 没有（自检/CI 下总是 0）
 * @note 回调里想弹对话框之前先问一句，免得自检/CI 被弹窗卡住
 */
int         cg_app_has_window(const cg_app_t *app);

/**
 * @brief 把 .cgui 文档树打印到 stdout
 * @param app 应用
 * @return void 无返回值
 * @note 调试/自检用
 */
void cg_dump_doc(cg_app_t *app);

/* ------------------------------------------------------------ 窗口操作
 * 都是运行时改窗口属性；.cgui 的 app 节点里也能写（窗口属性表见 docs/CGUI-FORMAT.md）。
 */
/**
 * @brief 移动窗口（top-left，屏幕坐标）
 * @param app 应用
 * @param x 目标左边界，屏幕物理外的坐标由系统自己收进可见区
 * @param y 目标上边界
 * @return void 无返回值
 */
void cg_win_set_pos(cg_app_t *app, int x, int y);
/**
 * @brief 改窗口客户区尺寸
 * @param app 应用
 * @param width 客户区宽（逻辑像素，和 .cgui 的 width 同一套单位）
 * @param height 客户区高（逻辑像素，和 .cgui 的 height 同一套单位）
 * @return void 无返回值
 */
void cg_win_set_size(cg_app_t *app, int width, int height);
/**
 * @brief 窗口居中（当前显示器工作区）
 * @param app 应用
 * @return void 无返回值
 */
void cg_win_center(cg_app_t *app);
/**
 * @brief 显示 / 隐藏窗口
 * @param app 应用
 * @param show 1 显示、0 隐藏
 * @return void 无返回值
 */
void cg_win_show(cg_app_t *app, int show);
/**
 * @brief 最小化 / 最大化 / 还原
 * @param app 应用
 * @param cmd "min" / "max" / "normal"，认不出来就什么都不做
 * @return void 无返回值
 */
void cg_win_set_state(cg_app_t *app, const char *cmd);
/**
 * @brief 切窗口的"总在最前"
 * @param app 应用
 * @param on 1 置顶、0 取消
 * @return void 无返回值
 */
void cg_win_set_topmost(cg_app_t *app, int on);
/**
 * @brief 改窗口图标
 * @param app 应用
 * @param ico_path .ico 路径（同 .cgui 里 app.icon 的查找规则）；NULL 还原默认
 * @return void 无返回值
 */
void cg_win_set_icon(cg_app_t *app, const char *ico_path);
/**
 * @brief 逻辑像素换算成物理像素（DPI 换算）
 * @param app 应用（取当前缩放比）；NULL 按不缩放处理
 * @param logical 逻辑像素值（可正可负）
 * @return int 物理像素值（四舍五入）
 * @note 只为 `cg_win_set_pos()` 这类**按物理像素收参**的接口换算用；
 *       尺寸类接口（`cg_win_set_size` / `cg_widget_set_rect` / 事件坐标）收的
 *       本来就是逻辑像素，**不要**再过这层换算
 */
int  cg_scale_px(const cg_app_t *app, int logical);
/**
 * @brief 把主窗口提到前台并拿到键盘焦点（最小化时先还原）
 * @param app 应用
 * @return int 1 成功；0 失败（没有窗口，或系统拒绝这次抢前台）
 * @note 截图工具这类"弹一个全屏 overlay 等着按键（Esc 取消 / 回车完成）"的程序必须调它：
 *       只 `cg_win_show()` 不抢前台时，键盘消息不进本进程 —— 表现为"按 Esc 没反应"，
 *       而用户点过画布之后又好了（点击顺带把窗口激活了），最像"快捷键坏了"的那类坑。
 *       系统在前台锁生效时有权拒绝（返回 0），业务要留鼠标/按钮兜底入口
 */
int  cg_win_focus(cg_app_t *app);
/**
 * @brief 运行时改主窗口边框（`.cgui` 的 `app.border` 的运行时版）
 * @param app 应用
 * @param mode "sizable" 可调整 / "fixed" 普通固定 / "dialog" 对话框固定 / "none" 无边框
 * @return int 1 成功；0 失败（没有窗口 / mode 认不出来）
 * @note 只换边框样式，**不改**窗口位置与尺寸（全屏 overlay 这类用法要自己
 *       `cg_win_set_pos` + `cg_win_set_size`）；标题栏按钮的开关仍按 .cgui 的设置；
 *       `--selftest` 没有窗口时返回 0
 */
int  cg_win_set_border(cg_app_t *app, const char *mode);

/* ------------------------------------------------------------ 模态对话框 */

/**
 * @brief 打开一个模态对话框（.cgui 里 type 为 "dialog" 的节点）
 * @param dlg 对话框控件：cg_find(app, "对话框id") 拿到；必须是 dialog 类型
 * @return int 1 = 回调里 cg_dialog_close(self, 1) 过；0 = 取消 / Esc / 直接关掉 / 没开成
 * @note 模态期间主窗口被禁用，Esc 等于取消。对话框里控件的 id 仍在整个应用的
 *       命名空间里 —— open 返回后用 cg_find 直接读值（"确定后取 edit 的内容"就是这条路径）。
 *       同一时间只能开一个：已经开着时再调直接返回 0。
 */
int  cg_dialog_open(cg_widget_t *dlg);

/**
 * @brief 关闭当前打开的模态对话框（在对话框自己的回调里调）
 * @param self 触发事件的控件；对话框自己或它里面的控件都行
 * @param result 交给 cg_dialog_open 返回值的结果（1 = 确定，0 = 取消）
 * @return void 无返回值
 * @note 内部走 WM_CLOSE：close / closequery 回调照常触发，cg_cancel_close 仍能拦下不关
 */
void cg_dialog_close(cg_widget_t *self, int result);

/**
 * @brief 打开一个**非模态**对话框：立即返回，主窗口保持可用
 * @param dlg 对话框控件：cg_find(app, "对话框id") 拿到；必须是 dialog 类型
 * @return int 1 = 已打开；0 = 没开成（同一时间只支持一个对话框，模态或非模态合计）
 * @note 非模态没有返回值语义——结果靠回调（close/destroy）或 cg_find 读控件值。
 *       关闭走 cg_dialog_close() 或对话框的关闭钮，close/closequery/destroy 照常触发。
 *       与模态共用同一套地基（独立 HWND + 子树懒建）。窗口级 move/size/activate
 *       也派发到对话框上（2026-10-10 起；此前非模态一律收不到）—— 宿主按 HWND 在树里
 *       反查，只在窗口级事件这类**低频**消息上做，逐消息的路径不走它。
 *       剩下的边界：同一时间只支持一个对话框（模态或非模态合计）。
 */
int  cg_dialog_open_nonmodal(cg_widget_t *dlg);

/* ---- 深色模式 ----
 * .cgui 的 `app.dark` 写 `true` / `"auto"`（跟随系统）/ 不写（浅色）就能定下启动配色；
 * 这两个函数是给**程序里**改的。
 *
 * 它只改**默认色**：窗口与面板底色、无底色控件的底/字色、输入框与终端、
 * 以及列表/表格/树（走系统的 DarkMode_Explorer，Win10 1809 起有效）。
 * 控件自己写了 fg/bg 的一律以自己为准 —— 所以一份为浅色写死配色的 .cgui 在深色下
 * 会是"深底 + 你那套浅色"的混合观感。
 * **不跟随**的是系统经典控件（标签页 / 下拉列表 / 月历 / 日期选择 / 状态栏 / 工具栏 /
 * 滚动条箭头）：它们的主题由系统内部绘制，没有公开接口能改 —— 要一致只能全部重画。
 */
/**
 * @brief 运行时切换深色 / 浅色
 * @param app 应用
 * @param on 1 深色、0 浅色
 * @return int 1 成功、0 失败
 * @note 只改**默认色**：控件自己写了 fg/bg 的一律以自己为准
 */
int cg_theme_set_dark(cg_app_t *app, int on);
/**
 * @brief 现在是不是深色
 * @param app 应用
 * @return int 1 深色、0 浅色
 * @note `--selftest` 没有窗口也答得出
 */
int cg_theme_is_dark(const cg_app_t *app);

/* ---- 截图（工单取证 / 文档配图） ----
 * 存成 **PNG**。走 `PrintWindow`，所以窗口**被挡住或不在前台**也能拿到正确内容；
 * 少数控件不响应时退回抓屏并在日志里说明"这张可能被遮挡"。GDI+ 没起来的机器上返回 0。
 */
/**
 * @brief 把整个窗口存成 PNG（含标题栏）
 * @param app 应用
 * @param path 输出文件路径（UTF-8，目录必须已存在）
 * @return int 1 成功、0 失败（原因写日志）
 */
int cg_win_screenshot(cg_app_t *app, const char *path);

/* ---- 文件拖放 ----
 * 把文件从资源管理器拖到窗口上，会派发窗口级事件 **`drop`**（写在 `app.events` 里）：
 *
 *     { "app": { "events": { "drop": "on_files_drop" } } }
 *
 * 回调里用下面两个函数取路径。二者给的都是**副本**（cg_drop_file 返回堆串，用完 cg_free）：
 * 回调返回后失效的只是运行时内部那份清单，你自己存下来的副本可以延后使用。
 */
/**
 * @brief 本次拖进来几个文件
 * @param app 应用
 * @return int 文件数；不在 drop 回调里调用返回 0
 */
int cg_drop_count(cg_app_t *app);
/**
 * @brief 取第 index 个拖入文件的完整路径
 * @param app 应用
 * @param index 序号，0 起
 * @return char * UTF-8 堆串，用完由调用方 cg_free；越界或不在回调里返回 NULL
 * @note 只在 drop 回调期间有效，要留着就自己拷走
 */
char *cg_drop_file(cg_app_t *app, int index);
/**
 * @brief 把**一个控件**的原生窗口存成 PNG（"这个框里到底显示成什么样"取证用）
 * @param w 目标控件
 * @param path 输出文件路径（UTF-8，目录必须已存在）
 * @return int 1 成功、0 失败
 * @note 日期时间这类两个原生窗口拼的控件，存的是主窗口那个
 */
int cg_widget_screenshot(cg_widget_t *w, const char *path);

/* ---- 屏幕抓取（区域截图） ----
 * 与上面 `cg_win_screenshot`（抓**自己**的窗口）不同，这两个抓的是**屏幕**像素。
 * 坐标与尺寸一律是**逻辑像素**（与 .cgui、控件坐标、事件坐标同一套），DPI 换算
 * 在内部做。口径是**整个虚拟桌面**（多块屏拼起来的那一整块），不是主显示器：
 * 副屏上的区域同样抓得到，坐标可以是负数（虚拟桌面原点未必在 (0,0) —— 左边
 * 还竖了一块屏时原点就是负的）。
 * 已知边界：**混 DPI 多屏**下窗口的 DPI 上下文跟随主屏，副屏上的物理↔逻辑换算
 * 会差一点，跨屏拖选在缩放不一致的屏幕上边缘可能差 1px 级（示例 shottool 的
 * README 把这条写进了能力边界）。
 */
/**
 * @brief 虚拟桌面尺寸（逻辑像素；单屏时就是那块屏的尺寸）
 * @param app 应用（取 DPI 缩放比；NULL 按不缩放算）
 * @param w 出参：宽；可为 NULL
 * @param h 出参：高；可为 NULL
 * @return int 1 成功；0 失败（屏幕查询返回非正值）
 */
int cg_screen_size(const cg_app_t *app, int *w, int *h);
/**
 * @brief 虚拟桌面原点（逻辑像素）—— 全屏 overlay 要摆的位置
 * @param app 应用（取 DPI 缩放比；NULL 按不缩放算）
 * @param x 出参：左上角横坐标（**可能为负**：左边竖了屏时）；可为 NULL
 * @param y 出参：左上角纵坐标（可能为负）；可为 NULL
 * @return int 1 成功（当前实现**不失败**：虚拟桌面原点任何系统上都拿得到；
 *          留返回值是与 `cg_screen_size` 对称，也方便以后加失败路径）
 * @note 单屏且主屏在左上时返回 (0, 0)，与老行为一致；多屏时是
 *       `SM_XVIRTUALSCREEN`/`SM_YVIRTUALSCREEN` 折算后的逻辑像素。
 *       配 `cg_win_set_pos`（按**物理**像素收参）用时要过 `cg_scale_px`
 */
int cg_screen_origin(const cg_app_t *app, int *x, int *y);
/**
 * @brief 把屏幕上一块区域存成 PNG（区域截图）
 * @param app 应用（取 DPI 缩放比；NULL 直接失败）
 * @param x 区域左上角屏幕坐标（逻辑像素，**虚拟桌面坐标**，可为负）
 * @param y 区域左上角屏幕坐标（逻辑像素，可为负）
 * @param w 区域宽（逻辑像素）
 * @param h 区域高（逻辑像素）
 * @param path 输出路径（UTF-8，目录必须已存在；编码固定 PNG）
 * @return int 1 成功；0 失败（app/path 为空、区域夹完整体为 0、取 DC 或写盘失败）
 * @note 区域先换算成物理像素再**夹进虚拟桌面范围** —— 越出桌面的部分 BitBlt
 *       抓不到，不夹的话落盘是黑块。GDI+ 没起来的机器返回 0（原因写日志）
 */
int cg_screen_capture(const cg_app_t *app, int x, int y, int w, int h, const char *path);

/* ------------------------------------------------------------ 托盘
 * .cgui 的 app 里写 tray:true 会自动建托盘图标；也可以运行时代码调用。
 * 托盘被点（左键单击 / 左键双击 / 右键单击）时派发 window 的 tray 事件，
 * 回调里用 cg_tray_action() 分辨动作。
 */
/**
 * @brief 建 / 改托盘图标
 * @param app 应用
 * @param ico_path .ico 路径；NULL 表示用窗口图标
 * @param tip 悬停提示文字；NULL 表示用窗口标题
 * @return void 无返回值
 */
void cg_tray_set(cg_app_t *app, const char *ico_path, const char *tip);
/**
 * @brief 移除托盘图标
 * @param app 应用
 * @return void 无返回值
 */
void cg_tray_remove(cg_app_t *app);
/**
 * @brief 弹一个托盘气泡提示
 * @param app 应用
 * @param tip_title 标题，可为 NULL
 * @param text 正文，可为 NULL
 * @return void 无返回值
 */
void cg_tray_balloon(cg_app_t *app, const char *tip_title, const char *text);

/* ------------------------------------------------------------ 全局热键
 * 与 `.cgui` 里动作的 `accel`（**应用内**加速键：只在本窗口持有焦点时生效）不同，
 * 全局热键走 `RegisterHotKey`：**程序在后台 / 最小化到托盘时也按得到** —— 截图工具
 * 这类"常驻后台、一按就出来"的程序靠它。组合键被别的程序先注册了就会失败，
 * 所以返回 0 不算异常，业务要留兜底入口（面板按钮 / 托盘点击）。
 * 组合键写法与动作 accel 相同："Ctrl+Alt+A" / "Ctrl+Shift+F5"（至少要有一个修饰键）。
 */
/**
 * @brief 注册全局热键：按下组合键 = 触发同名动作（回调里用 cg_action_name 取）
 * @param app 应用
 * @param combo 组合键；传 NULL = 解绑本程序注册的**全部**全局热键（action 被忽略）
 * @param action 动作名（.cgui 的 actions 里要有、且回调已登记，否则失败）
 * @return int 1 成功（或 NULL 时清空成功）；0 失败（没有窗口 / 组合键看不懂 /
 *         动作不存在 / 键被别的程序占用 —— 失败原因写日志）
 * @note 同一动作重复注册会先解绑旧的再挂新的；关窗时自动清空，业务不用自己收；
 *       `--selftest` 没有窗口，返回 0
 */
int  cg_hotkey_set(cg_app_t *app, const char *combo, const char *action);

/* ------------------------------------------------------------ 程序配置（版本信息）
 * 来自 .cgui 的 app.info（画布「程序配置」对话框填的那些），
 * 编译时会被写进 exe 的版本信息，运行时也能读出来做「关于」对话框。
 * 支持的 key："product"（程序名称）/"desc"（描述）/"comments"（备注）/"version"（版本号）
 *              "author"/"company"/"copyright"/"address"/"postcode"/"phone"/"fax"
 *              "email"/"homepage"/"build"（创建号）
 * 没有的 key 返回 NULL。
 */
/**
 * @brief 读程序配置里的一个字段（.cgui 的 app.info，编译时进了 exe 版本信息）
 * @param app 应用
 * @param key 字段名，取值见上面一段：product/desc/comments/version/author/company/
 *            copyright/address/postcode/phone/fax/email/homepage/build
 * @return const char* 静态字符串，别 free；没有这个 key 返回 NULL
 */
const char *cg_app_info(const cg_app_t *app, const char *key);

/* ------------------------------------------------------------ 控件读取 */

/**
 * @brief 取控件 id
 * @param w 控件
 * @return const char* 控件自己的字符串，别 free；w 为 NULL 返回 NULL
 */
const char *cg_widget_id(const cg_widget_t *w);
/**
 * @brief 取控件类型名（"button" / "edit" ...）
 * @param w 控件
 * @return const char* 静态字符串，别 free；w 为 NULL 返回 NULL
 */
const char *cg_widget_type(const cg_widget_t *w);
/**
 * @brief 取控件当前文字
 * @param w 控件
 * @return const char* 控件自己的字符串，别 free；文字为空返回空串，w 为 NULL 返回 NULL
 */
const char *cg_widget_text(const cg_widget_t *w);
/**
 * @brief 让控件拿到键盘焦点
 * @param w 控件；没有原生窗口（自检 / 无窗口模式）时是空操作
 * @return void 无返回值
 * @note 典型用法：程序刚把一段文字预填进输入框（`cg_widget_set_text`），接着把焦点交给它，
 *       用户不用再去点一下就能直接敲字
 */
void cg_widget_focus(cg_widget_t *w);
/**
 * @brief 取控件左边界
 * @param w 控件
 * @return int 逻辑像素，相对父容器；w 为 NULL 返回 0
 */
int         cg_widget_x(const cg_widget_t *w);
/**
 * @brief 取控件上边界
 * @param w 控件
 * @return int 逻辑像素，相对父容器；w 为 NULL 返回 0
 */
int         cg_widget_y(const cg_widget_t *w);
/**
 * @brief 取控件宽度
 * @param w 控件
 * @return int 逻辑像素；w 为 NULL 返回 0
 */
int         cg_widget_w(const cg_widget_t *w);
/**
 * @brief 取控件高度
 * @param w 控件
 * @return int 逻辑像素；w 为 NULL 返回 0
 */
int         cg_widget_h(const cg_widget_t *w);
/**
 * @brief 控件是否可见
 * @param w 控件
 * @return int 1 可见、0 隐藏或 w 为 NULL
 */
int         cg_widget_visible(const cg_widget_t *w);
/**
 * @brief 控件是否可用
 * @param w 控件
 * @return int 1 可用、0 被禁用或 w 为 NULL
 */
int         cg_widget_enabled(const cg_widget_t *w);
/**
 * @brief 控件是否勾选（checkbox / radio / switch / 勾选列的表格行）
 * @param w 控件
 * @return int 1 勾选、0 未勾选或 w 为 NULL
 */
int         cg_widget_checked(const cg_widget_t *w);
/**
 * @brief 取控件当前值（滑块位置 / 列表选中下标 / 标签页页码等）
 * @param w 控件
 * @return int 当前值；w 为 NULL 返回 0
 */
int         cg_widget_value(const cg_widget_t *w);
/**
 * @brief 数当前有多少个控件
 * @param app 应用
 * @return int 控件总数，不含代表窗口自身的那个伪控件；调试用
 */
int         cg_widget_count(const cg_app_t *app);

/* ------------------------------------------------------------ 控件修改
 * 所有 set 都立即反映到界面上（内部做了 Win32 同步），不需要手动刷新。
 */

/**
 * @brief 改控件文字（同步到界面与文档节点）
 * @param w 控件
 * @param text UTF-8 文字，函数内部拷贝，调用方可以立刻释放
 * @return void 无返回值
 */
void cg_widget_set_text(cg_widget_t *w, const char *text);
/**
 * @brief 把多行编辑框滚到最底部（串口监视器那种"新数据始终可见"）
 * @param w 多行 edit 控件
 * @return void 无返回值；非编辑框或 w 为 NULL 时空转
 */
void cg_edit_scroll_end(cg_widget_t *w);
/**
 * @brief 一次改控件的位置和尺寸（逻辑像素，相对父容器）
 * @param w 控件
 * @param x 目标左边界
 * @param y 目标上边界
 * @param width 目标宽度
 * @param height 目标高度
 * @return void 无返回值
 */
void cg_widget_set_rect(cg_widget_t *w, int x, int y, int width, int height);
/**
 * @brief 只改控件位置（尺寸不动）
 * @param w 控件
 * @param x 目标左边界，逻辑像素
 * @param y 目标上边界，逻辑像素
 * @return void 无返回值
 */
void cg_widget_set_pos(cg_widget_t *w, int x, int y);
/**
 * @brief 只改控件尺寸（位置不动）
 * @param w 控件
 * @param width 目标宽度，逻辑像素
 * @param height 目标高度，逻辑像素
 * @return void 无返回值
 */
void cg_widget_set_size(cg_widget_t *w, int width, int height);
/**
 * @brief 显示 / 隐藏控件
 * @param w 控件
 * @param visible 1 显示、0 隐藏（与启用状态无关）
 * @return void 无返回值
 */
void cg_widget_set_visible(cg_widget_t *w, int visible);
/**
 * @brief 启用 / 禁用控件
 * @param w 控件
 * @param enabled 1 启用、0 禁用
 * @return void 无返回值
 * @note 禁用后事件不再派发（引用动作的控件会在下次动作同步时被动作状态覆盖回来）
 */
void cg_widget_set_enabled(cg_widget_t *w, int enabled);
/**
 * @brief 设置勾选状态（checkbox / radio / switch）
 * @param w 控件
 * @param on 1 勾选、0 取消
 * @return void 无返回值
 */
void cg_widget_set_checked(cg_widget_t *w, int on);
/**
 * @brief 设置控件当前值（滑块 / 进度条 / 列表选择位置等）
 * @param w 控件
 * @param value 目标值，自动夹到 [vmin, vmax]；"下标即 value"的类型夹到项数范围
 * @return void 无返回值
 * @note 数值输入框（edit + 调节器）写的是 value，文本由 value 派生
 */
void cg_widget_set_value(cg_widget_t *w, int value);
/**
 * @brief 设置取值范围
 * @param w 控件
 * @param min 下限
 * @param max 上限，<= min 时自动抬到 min + 1
 * @return void 无返回值
 */
void cg_widget_set_value_range(cg_widget_t *w, int min, int max);
/**
 * @brief 设置前景 / 背景色
 * @param w 控件
 * @param fg 文字 / 线条色，0xRRGGBB 或 CG_RGB(r,g,b)；CG_COLOR_NONE 交给系统默认
 * @param bg 底色，编码同上
 * @return void 无返回值
 */
void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg);
/**
 * @brief 设置控件字体
 * @param w 控件
 * @param family 字体名，NULL 表示回系统 UI 字体
 * @param size_pt 字号（磅）
 * @param bold 0 常规、非 0 加粗
 * @return void 无返回值
 */
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
/**
 * @brief 运行时绑定两个控件（覆盖原来 .cgui 里写的绑定）
 * @param src 源控件（变化的一方）
 * @param dst 目标控件（跟着 src 走的一方）
 * @param two_way 非 0 时双向同步；0 只从 src 推到 dst
 * @return int 0 成功；-1 参数不对（同一个控件、跨应用、控件没有 id）
 */
int cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way);

/**
 * @brief 取挂在控件上的业务数据
 * @param w 控件
 * @return void* cg_widget_set_user 挂上去的指针，运行时只管存不管释放；w 为 NULL 返回 NULL
 */
void  *cg_widget_get_user(const cg_widget_t *w);
/**
 * @brief 挂业务数据到控件
 * @param w 控件
 * @param user 任意业务数据指针，回调里的 user 参数就是它；运行时不会释放它
 * @return void 无返回值
 */
void   cg_widget_set_user(cg_widget_t *w, void *user);

/* ------------------------------------------------------ 列表类控件
 * 作用于 listbox / combo / tabs（标签文字）/ statusbar（分段文字）：
 * 内部保存一份字符串表，改完界面自动刷新。
 */
/**
 * @brief 给列表类控件追加一个条目
 * @param w 目标控件（listbox / combo / tabs / statusbar / toolbar）
 * @param item 条目文字，函数内部拷贝
 * @return int 新条目下标；控件类型不对或没内存返回 -1
 */
int  cg_widget_list_add(cg_widget_t *w, const char *item);
/**
 * @brief cg_widget_list_add 的 printf 版本
 * @param w 目标控件
 * @param fmt printf 风格格式串
 * @return int 新条目下标；失败返回 -1
 */
int  cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...);
/**
 * @brief 清空列表类控件的全部条目
 * @param w 目标控件
 * @return void 无返回值
 */
void cg_widget_list_clear(cg_widget_t *w);
/**
 * @brief 列表条目数
 * @param w 目标控件
 * @return int 条目数；w 为 NULL 返回 0
 */
int  cg_widget_list_count(const cg_widget_t *w);
/**
 * @brief 取第 index 个条目文字
 * @param w 目标控件
 * @param index 下标，0 起
 * @return const char* 控件内部的字符串，别 free；越界返回 NULL
 */
const char *cg_widget_list_item(const cg_widget_t *w, int index);
/**
 * @brief 让列表滚到最后一项
 * @param w 目标控件
 * @return void 无返回值
 * @note 日志类控件常用
 */
void cg_widget_list_scroll_bottom(cg_widget_t *w);
/* 图标列表（iconlist，带图标的行列表）的 cg_iconlist_add / clear / count /
   set_checked / checked / set_row 见下面「图标列表（iconlist）」分节（chart 分节之前）。 */

/* ---------------------------------------------------------------- 树 */
/**
 * @brief 给树加一个节点
 * @param w tree 控件
 * @param parent 父节点下标；-1 表示加在根上
 * @param text 节点文字，函数内部拷贝
 * @return int 节点下标（大于等于 0）；失败返回 -1
 */
int  cg_tree_add(cg_widget_t *w, int parent, const char *text);
/**
 * @brief cg_tree_add 的 printf 版本
 * @param w tree 控件
 * @param parent 父节点下标；-1 表示加在根上
 * @param fmt printf 风格格式串
 * @return int 节点下标；失败返回 -1
 */
int  cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...);
/**
 * @brief 清空树的全部节点
 * @param w tree 控件
 * @return void 无返回值（整树重建实现，选中态复位）
 */
void cg_tree_clear(cg_widget_t *w);
/**
 * @brief 树节点总数
 * @param w tree 控件
 * @return int 节点数；非 tree 控件返回 0
 */
int  cg_tree_count(const cg_widget_t *w);
/**
 * @brief 取第 item 个节点的文字
 * @param w tree 控件
 * @param item 节点下标，0 起
 * @return const char* 控件内部的字符串，别 free；越界返回 NULL
 */
const char *cg_tree_item_text(const cg_widget_t *w, int item);
/**
 * @brief 取当前选中的节点下标
 * @param w tree 控件
 * @return int 节点下标；没选中返回 -1
 */
int  cg_tree_selected(const cg_widget_t *w);
/**
 * @brief 取当前选中节点的文字
 * @param w tree 控件
 * @return const char* 控件内部的字符串，别 free；没选中返回 NULL
 */
const char *cg_tree_selected_text(const cg_widget_t *w);
/**
 * @brief 展开 / 折叠某个节点
 * @param w tree 控件
 * @param item 节点下标
 * @param expand 1 展开、0 折叠；越界忽略
 * @return void 无返回值
 */
void cg_tree_expand(cg_widget_t *w, int item, int expand);

/* ---------------------------------------------------------------- 表格 */
/**
 * @brief 给表格加一列
 * @param w table 控件
 * @param title 列标题，函数内部拷贝
 * @param width 列宽（逻辑像素）；传 0 表示按控件宽度均分
 * @return int 列号；失败返回 -1
 */
int  cg_table_add_column(cg_widget_t *w, const char *title, int width);
/**
 * @brief 给表格加一行
 * @param w table 控件
 * @param cells 列数个字符串；不足的列按空串处理，不释放也不接管这些指针
 * @param ncells cells 的个数
 * @return int 行号；失败返回 -1
 */
int  cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells);
/**
 * @brief 清空表格全部行
 * @param w table 控件
 * @return void 无返回值（列保留）
 */
void cg_table_clear(cg_widget_t *w);
/**
 * @brief 开始批量更新表格：到 end 之前只记脏，不做任何重建
 * @param w table 控件
 * @return void 无返回值
 * @note **填表前一定成对用上这两个**：表格每次加行 / 清空都会**整表重建**（删列、删全部行再重插），
 *       所以逐行加 200 行 = 200 次整表重建 —— 肉眼看到的是一片连续闪烁，耗时也随行数涨得很快。
 *       begin 之后中间一次都不重建，end 时只重建一次，看着就是"一下换好"。
 * @note 它与 end 必须**在同一个消息处理里成对**出现：期间控件显示的还是旧内容。可嵌套，按计数配平
 * @code
 *   cg_table_begin_update(tbl);
 *   cg_table_clear(tbl);
 *   for (i = 0; i < n; i++) cg_table_add_row(tbl, cells[i], ncols);
 *   cg_table_end_update(tbl);
 * @endcode
 */
void cg_table_begin_update(cg_widget_t *w);
/**
 * @brief 结束批量更新：计数配平归零时把积累的改动一次重建出来
 * @param w table 控件
 * @return void 无返回值（没有未配平的 begin 时是空操作）
 */
void cg_table_end_update(cg_widget_t *w);
/**
 * @brief 表格行数
 * @param w table 控件
 * @return int 行数；非 table 控件返回 0
 */
int  cg_table_row_count(const cg_widget_t *w);
/**
 * @brief 取某行某列的文字
 * @param w table 控件
 * @param row 行号，0 起
 * @param col 列号，0 起
 * @return const char* 控件内部的字符串，别 free；越界返回 NULL
 */
const char *cg_table_cell(const cg_widget_t *w, int row, int col);
/**
 * @brief 取当前选中行
 * @param w table 控件
 * @return int 最后一次点击的行号；没选中返回 -1
 */
int  cg_table_selected(const cg_widget_t *w);
/**
 * @brief 最近一次在表格上双击的那一列（0 起）
 * @param w table 控件；非 table 控件或还没双击过返回 -1
 * @return int 列号；-1 = 没双击过
 * @note 表格的双击事件只带行号（行用 `cg_table_selected` 取），列号由后端在双击时记在控件上，
 *       业务在 dblclick 回调里现问现用 —— "双击哪一格就编辑哪一格"靠它才做得到
 */
int  cg_table_last_col(const cg_widget_t *w);

/* ------------------------------------------------------------ 表格（增强） */

/**
 * @brief 给表格某行绑一个整数值（行数据，不显示）
 * @param w table 控件
 * @param row 行号，0 起
 * @param value 要存的整数
 * @return int 0 成功、-1 参数不对
 */
int  cg_table_set_row_value(cg_widget_t *w, int row, int value);
/**
 * @brief 取表格某行绑的整数值
 * @param w table 控件
 * @param row 行号，0 起
 * @return int 之前存的整数；参数不对返回 0
 */
int  cg_table_row_value(const cg_widget_t *w, int row);
/**
 * @brief 设置表格某行的勾选状态
 * @param w table 控件（.cgui 里 table 要开 check 才有复选框列）
 * @param row 行号，0 起
 * @param on 1 勾上、0 取消
 * @return void 无返回值
 * @note 程序设置的勾选**不触发** check 事件；勾选变化触发 check 事件
 */
void cg_table_set_checked(cg_widget_t *w, int row, int on);
/**
 * @brief 取表格某行的勾选状态
 * @param w table 控件
 * @param row 行号，0 起
 * @return int 1 勾选、0 没勾或参数不对
 */
int  cg_table_checked(const cg_widget_t *w, int row);
/**
 * @brief 设置表格某行行首的图标
 * @param w table 控件
 * @param row 行号，0 起
 * @param path 图片路径（png/jpg/bmp/gif，查找规则同 image 控件）；NULL 清除
 * @return void 无返回值
 */
void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path);
/**
 * @brief 多选表格当前选中了几行
 * @param w table 控件（.cgui 里 table 开了 multi）
 * @return int 选中行数（单选表格最多 1）
 */
int  cg_table_selected_count(const cg_widget_t *w);
/**
 * @brief 取第 index 个选中的行号（配合 cg_table_selected_count 遍历）
 * @param w table 控件
 * @param index 第几个选中行，0 起
 * @return int 行号；没有了返回 -1
 */
int  cg_table_selected_row(const cg_widget_t *w, int index);

/* ------------------------------------------------------------ 状态栏 */
/**
 * @brief 改状态栏第 part 段的文字
 * @param w statusbar 控件
 * @param part 段号，0 起
 * @param text 文字，函数内部拷贝；段不够且正好差一段时相当于追加，差得多就补空段
 * @return void 无返回值
 */
void cg_status_set(cg_widget_t *w, int part, const char *text);
/**
 * @brief cg_status_set 的 printf 版本
 * @param w statusbar 控件
 * @param part 段号，0 起
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...);

/* ------------------------------------------------------------ 标签页 */
/**
 * @brief 标签页页数
 * @param w tabs 控件
 * @return int 页数；非 tabs 控件返回 0
 */
int  cg_tabs_count(const cg_widget_t *w);
/**
 * @brief 切到第 index 页（子控件按各自 page 自动显隐）
 * @param w tabs 控件
 * @param index 页号，0 起；越界忽略
 * @return void 无返回值
 */
void cg_tabs_select(cg_widget_t *w, int index);
/**
 * @brief 取当前页下标
 * @param w tabs 控件
 * @return int 当前页（0 起）；w 为 NULL 返回 0
 */
int  cg_tabs_selected(const cg_widget_t *w);

/* ------------------------------------------------------------ 图片 */
/**
 * @brief 换图片文件
 * @param w image 控件；**也接受 canvas（画板）**
 * @param path 图片路径（png/jpg/bmp/gif）；路径不存在时 image 显示成占位框
 *             （canvas 没有占位框：读不到就退回底色，不报错）
 * @return void 无返回值
 * @note 设在 **canvas** 上 = 画板**背景图**：按画板大小拉伸打底，绘制命令照常
 *       叠在它上面回放 —— "截图当底 + 标注当命令，成品抓画板自己"就是靠这条
 *       （示例见 `examples\shottool`）
 */
void cg_widget_set_image(cg_widget_t *w, const char *path);
/**
 * @brief 取控件当前的图片路径
 * @param w image / canvas 控件
 * @return const char* 控件内部的字符串，别 free；未设置返回 NULL
 */
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
/**
 * @brief 取最近点中的那个菜单项的 id
 * @param w menu / menubutton 控件
 * @return const char* 控件内部的字符串，别 free；项没写 id 或还没点过返回 NULL
 */
const char *cg_menu_last_id(const cg_widget_t *w);
/**
 * @brief 取最近点中的那个菜单项的文字
 * @param w menu / menubutton 控件
 * @return const char* 控件内部的字符串，别 free；还没点过返回 NULL
 * @note 没写 id 的项只能按文字判断
 */
const char *cg_menu_last_text(const cg_widget_t *w);
/**
 * @brief 数菜单里有多少个可点的项
 * @param w menu / menubutton 控件
 * @return int 项数，不含分隔线与顶级弹出项；调试用
 */
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
/**
 * @brief 往菜单里插一项
 * @param w menu / menubutton 控件
 * @param parent_id 父项 id；NULL 或 "" 表示插在顶层
 * @param pos 插在父项的第几个位置（0 起）；越界按末尾算
 * @param text 项文字；写 "-" 就是分隔线
 * @param id 项标识（回调里用 cg_menu_last_id 认它）；可传 NULL
 * @return int 新项在行表里的下标；失败返回 -1
 */
int  cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos,
                      const char *text, const char *id);
/**
 * @brief 按 id 删掉一个菜单项（连同它的整棵子树）
 * @param w menu / menubutton 控件
 * @param id 要删的项 id
 * @return int 删掉的行数（含子树）；失败返回 -1
 */
int  cg_menu_remove_item(cg_widget_t *w, const char *id);
/**
 * @brief 清空某个下拉里的全部项（下拉项自身保留）
 * @param w menu / menubutton 控件
 * @param parent_id 下拉项的 id；NULL 或 "" 表示顶层
 * @return int 删掉的行数（含子树）；失败返回 -1
 */
int  cg_menu_clear_items(cg_widget_t *w, const char *parent_id);
/**
 * @brief 改某个菜单项的文字
 * @param w menu / menubutton 控件
 * @param id 项 id
 * @param text 新文字，函数内部拷贝
 * @return void 无返回值（对**动作项**不生效，只记一条 WARN）
 */
void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text);
/**
 * @brief 灰掉 / 恢复某个菜单项
 * @param w menu / menubutton 控件
 * @param id 项 id
 * @param enabled 1 可用、0 灰掉
 * @return void 无返回值（对**动作项**不生效 —— 那归动作管，只记一条 WARN）
 */
void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled);
/**
 * @brief 给某个菜单项打勾 / 去掉勾
 * @param w menu / menubutton 控件
 * @param id 项 id
 * @param checked 1 打勾、0 去掉
 * @return void 无返回值（对**动作项**不生效 —— 那归动作管，只记一条 WARN）
 */
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

/**
 * @brief 触发一个动作：快捷键、菜单项点击、控件点击最终都走到这里
 * @param app 应用
 * @param name 动作名，就是 .cgui 里 actions 对象的键
 * @return int 1 真的调用了回调；0 没有这个动作 / 动作被禁用 / handler 没登记
 */
int cg_action_invoke(cg_app_t *app, const char *name);

/**
 * @brief 启用 / 禁用一个动作，并把结果同步到所有引用它的 UI（菜单项灰掉、控件灰掉）
 * @param app 应用
 * @param name 动作名
 * @param enabled 1 启用、0 禁用
 * @return void 无返回值（动作不存在时空操作）
 */
void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled);
/**
 * @brief 动作当前是否启用
 * @param app 应用
 * @param name 动作名
 * @return int 1 启用、0 禁用；动作不存在返回 0
 */
int  cg_action_is_enabled(const cg_app_t *app, const char *name);

/**
 * @brief 勾选 / 取消勾选一个动作，并同步到所有引用它的**菜单项**（打勾或去掉）
 * @param app 应用
 * @param name 动作名
 * @param checked 1 勾选、0 取消
 * @return void 无返回值（动作不存在时空操作）
 */
void cg_action_set_checked(cg_app_t *app, const char *name, int checked);
/**
 * @brief 动作当前是否勾选
 * @param app 应用
 * @param name 动作名
 * @return int 1 勾选、0 未勾选；动作不存在返回 0
 */
int  cg_action_is_checked(const cg_app_t *app, const char *name);

/**
 * @brief 取最近一次被激活的动作名
 * @param self 动作回调里的 self（恒为主窗口伪控件）
 * @return const char* 运行时内部的字符串，别 free；没有则返回 NULL
 */
const char *cg_action_name(const cg_widget_t *self);

/**
 * @brief 数有几个动作
 * @param app 应用
 * @return int 动作个数；调试与自检用
 */
int         cg_action_count(const cg_app_t *app);
/**
 * @brief 取第 i 个动作名（遍历用）
 * @param app 应用
 * @param i 序号，0 起
 * @return const char* 运行时内部的字符串，别 free；越界返回 NULL
 */
const char *cg_action_at(const cg_app_t *app, int i);
/**
 * @brief 取动作的快捷键（规范化后的显示形式）
 * @param app 应用
 * @param name 动作名
 * @return const char* 如 "Ctrl+S"；动作不存在或没配 accel 返回 ""
 */
const char *cg_action_accel(const cg_app_t *app, const char *name);

/* ------------------------------------------------------------ 提示与时钟 */

/**
 * @brief 设鼠标悬停提示（tooltip）
 * @param w 控件（对所有控件生效）
 * @param text 提示文字，函数内部拷贝；传空串清除
 * @return void 无返回值
 */
void cg_widget_set_hint(cg_widget_t *w, const char *text);
/**
 * @brief 取控件当前的悬停提示文字
 * @param w 控件
 * @return const char* 控件内部的字符串，别 free；没设过返回空串（只有 w 为 NULL 才返回 NULL）
 */
const char *cg_widget_hint(const cg_widget_t *w);

/**
 * @brief 改时钟控件的触发周期
 * @param w timer 控件
 * @param period_ms 毫秒；0 = 停止，> 0 = 立即按新周期重新计时
 * @return void 无返回值
 */
void cg_widget_set_period(cg_widget_t *w, int period_ms);
/**
 * @brief 取时钟控件的触发周期
 * @param w timer 控件
 * @return int 毫秒；0 表示已停止
 */
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
/**
 * @brief 取分隔条当前位置
 * @param w splitter 控件
 * @return int 逻辑像素（水平分栏时相对容器左边、垂直分行时相对上边）；w 为 NULL 返回 -1（本函数不校验控件类型）
 */
int  cg_splitter_pos(const cg_widget_t *w);
/**
 * @brief 设分隔条位置（撑开它前面那一块）
 * @param w splitter 控件
 * @param pos 目标位置，逻辑像素；两侧各自最小 16px，越界会被夹住
 * @return void 无返回值
 */
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

/**
 * @brief 往终端控件写一段数据（一次调用 = 一段，内部按 \r\n 自动切成多行）
 * @param w terminal 控件
 * @param dir 方向，取 CG_TERM_RX / CG_TERM_TX / CG_TERM_SYS（决定行首标记与颜色）
 * @param data 字节流，不用以 0 结尾
 * @param len 字节数；传 -1 表示按字符串取长度（遇到 0 结束）
 * @return void 无返回值（一次调用只刷新一次界面）
 */
void cg_term_write(cg_widget_t *w, int dir, const void *data, int len);
/**
 * @brief 清空终端控件的全部行
 * @param w terminal 控件
 * @return void 无返回值
 */
void cg_term_clear(cg_widget_t *w);
/**
 * @brief 切换 HEX / 文本显示
 * @param w terminal 控件
 * @param on 1 = HEX、0 = 文本
 * @return void 无返回值（只影响之后写入的行）
 */
void cg_term_set_hex(cg_widget_t *w, int on);
/**
 * @brief 当前是不是 HEX 显示
 * @param w terminal 控件
 * @return int 1 = HEX、0 = 文本
 */
int  cg_term_hex(const cg_widget_t *w);
/**
 * @brief 开关行首时间戳
 * @param w terminal 控件
 * @param on 1 = 每行前加 [HH:MM:SS]、0 = 不加
 * @return void 无返回值
 */
void cg_term_set_timestamp(cg_widget_t *w, int on);
/**
 * @brief 改最大保留行数
 * @param w terminal 控件
 * @param lines 行数上限，超出后从最老的丢
 * @return void 无返回值
 */
void cg_term_set_max_lines(cg_widget_t *w, int lines);
/**
 * @brief 当前保留了多少行
 * @param w terminal 控件
 * @return int 行数
 */
int  cg_term_count(const cg_widget_t *w);
/**
 * @brief 把终端内容存成文本文件（UTF-8 + CRLF，不含方向标记）
 * @param w terminal 控件
 * @param path 输出文件路径（UTF-8，目录必须已存在）
 * @return int 0 成功、非 0 失败
 */
int  cg_term_save(cg_widget_t *w, const char *path);

/* ------------------------------------------------------------ 图标列表（iconlist）
 * 一列可滚动的行列表：〔复选框〕+ 左图标 + 主标题（大字）+ 副标题（第二行小字）+ 右侧状态，
 * 对标设置页那种"图标 + 两行说明 + 右边一句状态"的行。全自绘（Win32 原生没有这种控件）。
 *
 *   cg_iconlist_add(il, "assets/app.png", "复制文件路径", "\"...\\x.exe\" copypath", "在用 · 用户区");
 *
 * 这几个字段在这个控件上有专门含义：
 *   check   = 1 时每行左侧显示复选框（.cgui 写 "check": true，与 table 的同名字段同义）
 *   items   = 初始行（每个字符串是一行的**主标题**，图标/副标题/状态留空，运行时再灌）
 *   value   = 当前选中行下标（读 cg_widget_value，写 cg_widget_set_value）
 * 勾选态是**每行**的（cg_iconlist_set_row 一族），与 checkbox 控件的整控件 checked 无关。
 * 双击由运行时按系统双击间隔从 click 合成 —— 业务只绑 dblclick 即可，不用自己判时间。
 */
/**
 * @brief 往图标列表末尾加一行
 * @param w iconlist 控件；类型不符返回 -1
 * @param icon 图标路径（png/jpg/bmp/gif，相对 .cgui 目录或绝对；NULL / 空 = 无图标画占位框）
 * @param title 主标题（单行，超长省略号裁剪）
 * @param sub 副标题（第二行小灰字；NULL / 空 = 不画）
 * @param status 右侧状态文字（右对齐；NULL / 空 = 不画）
 * @return int 新行号；内存不足或类型不符返回 -1
 */
int  cg_iconlist_add(cg_widget_t *w, const char *icon, const char *title,
                     const char *sub, const char *status);
/**
 * @brief 清空全部行
 * @param w iconlist 控件；类型不符忽略
 * @return void 无返回值
 */
void cg_iconlist_clear(cg_widget_t *w);
/**
 * @brief 当前有多少行
 * @param w iconlist 控件；NULL 或类型不符返回 0
 * @return int 行数
 */
int  cg_iconlist_count(const cg_widget_t *w);
/**
 * @brief 改一行的勾选态
 * @param w iconlist 控件
 * @param row 行号（越界忽略）
 * @param checked 1 = 勾选，0 = 取消
 * @return void 无返回值
 */
void cg_iconlist_set_checked(cg_widget_t *w, int row, int checked);
/**
 * @brief 读一行的勾选态
 * @param w iconlist 控件
 * @param row 行号
 * @return int 1 = 已勾选；行无效返回 0
 */
int  cg_iconlist_checked(const cg_widget_t *w, int row);
/**
 * @brief 改写一行的内容（勾选态保持不变）
 * @param w iconlist 控件
 * @param row 行号（越界忽略）
 * @param icon 新图标路径；NULL / 空 = 清成无图标
 * @param title 新主标题；NULL / 空 = 清空
 * @param sub 新副标题；NULL / 空 = 清空
 * @param status 新状态文字；NULL / 空 = 清空
 * @return void 无返回值
 */
void cg_iconlist_set_row(cg_widget_t *w, int row, const char *icon, const char *title,
                         const char *sub, const char *status);

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
/**
 * @brief 给某个系列推一个采样点（窗口满了整体左移一格）
 * @param w chart 控件
 * @param series 系列下标，0 起；越界忽略
 * @param value 采样值，超出 min/max 的按边界裁剪后画
 * @return void 无返回值
 */
void cg_chart_push(cg_widget_t *w, int series, int value);
/**
 * @brief 一次给所有系列各推一个点（多通道采样最常用）
 * @param w chart 控件
 * @param values 各系列的值，按系列下标排列
 * @param nvalues values 的个数；多余的系列本轮不动
 * @return void 无返回值
 */
void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues);
/**
 * @brief 清掉所有系列已填的点（系列本身保留）
 * @param w chart 控件
 * @return void 无返回值
 */
void cg_chart_clear(cg_widget_t *w);
/**
 * @brief 运行时加一个系列
 * @param w chart 控件
 * @param name 系列名，函数内部拷贝
 * @return int 新系列下标；到上限或没内存返回 -1
 */
int  cg_chart_add_series(cg_widget_t *w, const char *name);
/**
 * @brief 当前有几个系列
 * @param w chart 控件
 * @return int 系列数
 */
int  cg_chart_series_count(const cg_widget_t *w);
/**
 * @brief 某系列已经填了多少点
 * @param w chart 控件
 * @param series 系列下标，0 起
 * @return int 已填点数；越界返回 0
 */
int  cg_chart_filled(const cg_widget_t *w, int series);
/**
 * @brief 读回某个采样点
 * @param w chart 控件
 * @param series 系列下标，0 起
 * @param index 点下标，0 = 最老的点
 * @return int 该点原始值；越界返回 0
 */
int  cg_chart_point(const cg_widget_t *w, int series, int index);
/**
 * @brief 改 Y 轴范围
 * @param w chart 控件
 * @param min 下限
 * @param max 上限，<= min 时按 range = 1 处理
 * @return void 无返回值
 */
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
/**
 * @brief 改状态灯的状态
 * @param w led 控件
 * @param state 0 灭 / 1 亮 / 2 闪，越界夹到 0~2；闪烁由控件自己定时重绘
 * @return void 无返回值
 */
void cg_led_set(cg_widget_t *w, int state);
/**
 * @brief 取状态灯当前状态
 * @param w led 控件
 * @return int 0 灭 / 1 亮 / 2 闪
 */
int  cg_led_state(const cg_widget_t *w);
/**
 * @brief 改状态灯亮起的颜色
 * @param w led 控件
 * @param rgb 0xRRGGBB，或 CG_RGB(r,g,b)
 * @return void 无返回值（等价于 cg_widget_set_colors(w, rgb, CG_COLOR_NONE)）
 */
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
/**
 * @brief 改仪表盘的读数
 * @param w meter 控件
 * @param value 目标值，按 min/max 夹（和滑块同一套语义）
 * @return void 无返回值
 */
void cg_meter_set(cg_widget_t *w, int value);
/**
 * @brief 改仪表盘量程
 * @param w meter 控件
 * @param min 下限
 * @param max 上限，<= min 时按 range = 1 处理（不会除零）
 * @return void 无返回值
 */
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
 * **帧格式（数据位/停止位/校验）是真支持的**：`.cgui` 里给 serial 写 `bits` / `stop` / `parity`
 * （画布属性面板有这三个下拉），或运行期用 `cg_widget_serial_set_format()` 改 —— 底层走的是
 * `cg_serial_open_ex()`（7E1 这类老设备直接选就行）。**流控仍然没有**（RTS/CTS、XON/XOFF 都没接）。
 * 其余边界（有意不做，别猜）：一个句柄一条线；不自动重连；`cg_serial_write` 是同步写（带超时），
 * 不是写队列。
 */
typedef struct cg_serial_t cg_serial_t;

/**
 * @brief 枚举本机串口：每行一个端口名写入 buf（"COM3\nCOM7\n"），返回写入字节数。
 *  没有端口时返回 0（不是错误）。走注册表，不引任何额外链接依赖。 *
 * @param buf 输出缓冲，函数写进以换行分隔的端口名
 * @param cap buf 的字节数
 * @return int 写入的字节数；没有端口时返回 0（不是错误）
 * @note 走注册表，不引任何额外链接依赖
 */
int  cg_serial_list(char *buf, int cap);

/**
 * @brief 打开串口（"COM3" 或 "\\\\.\\COM3" 都收），固定 8N1。失败返回 NULL；
 *
 * @param port 端口名，两种写法都收
 * @param baud 波特率，如 115200；固定 8N1（别的帧格式用 cg_serial_open_ex）
 * @return cg_serial_t* 句柄（关掉用 cg_serial_close）；失败返回 NULL，原因用
 *         cg_serial_last_error(NULL) 取（打开失败时没有句柄可问）
 */
cg_serial_t *cg_serial_open(const char *port, int baud);

/**
 * @brief 打开串口（全参数版）
 * @param port 端口名，语义同 cg_serial_open
 * @param baud 波特率
 * @param bits 数据位，5..8
 * @param stop 停止位，1..2
 * @param parity 校验，0=无 / 1=奇 / 2=偶
 * @return cg_serial_t* 句柄；失败返回 NULL
 * @note 参数不合法时**干净失败**：不合法的参数不往驱动送，原因见 cg_serial_last_error(NULL)；
 *       cg_serial_open() 等价于 open_ex(port, baud, 8, 1, 0)
 */
cg_serial_t *cg_serial_open_ex(const char *port, int baud, int bits, int stop, int parity);

/**
 * @brief 关掉串口并回收读线程
 * @param s cg_serial_open / cg_serial_open_ex 返回的句柄；NULL 安全（没连上时传 NULL 也行）
 * @return void 无返回值
 * @note 它会等读线程真的退出再释放缓冲，所以最坏会卡一个读超时周期（约 50ms）
 * @note **不是"幂等"**（2026-09-27 真机实测更正，原先这行写着"幂等"，是错的）：
 *        这一调会把句柄结构**释放掉**，指针当场失效 —— **同一个句柄关两次就是 use-after-free**，
 *        实测表现是进程以 `0xC0000374`（堆损坏）退出，而且崩点离关的地方可能很远。
 *        正确写法是**关完立刻置空**，这一行别省：
 *            cg_serial_close(g_ser);
 *            g_ser = NULL;
 *        控件形态（serial 组件）没有这个坑：`cg_widget_serial_close()` 内部就把 native2 置空，
 *        重复调它真的安全（要防呆就用组件形态，或者自己照着上面置空）。 */
void cg_serial_close(cg_serial_t *s);

/**
 * @brief 往串口发数据（同步写、带超时，不是写队列）
 * @param s 串口句柄
 * @param data 要发的字节
 * @param len 字节数
 * @return int 实际写出的字节数；< 0 表示失败（原因见 cg_serial_last_error）
 */
int  cg_serial_write(cg_serial_t *s, const void *data, int len);

/**
 * @brief 从接收缓冲取数据（**非阻塞**：没数据返回 0，不是错误）
 * @param s 串口句柄
 * @param buf 输出缓冲
 * @param cap buf 的字节数
 * @return int 实际取到的字节数，最多 cap
 * @note 返回的可能以**半个 UTF-8 序列**结尾 —— 要按文本显示的话，把不满意的尾巴留到下次
 *       （终端控件是按字节灌的，所以直接 cg_term_write 也行，只是那一帧可能显示成截断的）
 */
int  cg_serial_read(cg_serial_t *s, void *buf, int cap);

/**
 * @brief 接收缓冲里还有多少字节没取走
 * @param s 串口句柄
 * @return int 未读字节数
 */
int  cg_serial_available(const cg_serial_t *s);

/**
 * @brief 丢掉接收缓冲里还没取走的数据
 * @param s 串口句柄
 * @return void 无返回值（"清空接收"按钮用）
 */
void cg_serial_flush_rx(cg_serial_t *s);

/**
 * @brief 累计收了多少字节
 * @param s 串口句柄
 * @return long long 只增不减；要归零就重连
 */
long long cg_serial_rx_bytes(const cg_serial_t *s);
/**
 * @brief 累计发了多少字节
 * @param s 串口句柄
 * @return long long 只增不减；要归零就重连
 */
long long cg_serial_tx_bytes(const cg_serial_t *s);

/**
 * @brief 取最后一次错误的中文描述
 * @param s 串口句柄；传 NULL 时返回**最近一次 cg_serial_open 失败**的原因（打开失败时用得上）
 * @return const char* 如"找不到这个端口"；没出错返回 ""
 * @note 指向内部缓冲，下次报错会被覆盖；要留就自己拷走
 */
const char *cg_serial_last_error(const cg_serial_t *s);

/* ---- 串口组件（serial 控件）的控件侧 API：参数见上面"串口组件"一段 ---- */

/**
 * @brief 取串口组件的链路句柄
 * @param w serial 组件控件
 * @return cg_serial_t* 链路句柄；未打开或类型不对返回 NULL（回调里先判 NULL）
 */
cg_serial_t *cg_widget_serial(cg_widget_t *w);

/**
 * @brief 按控件属性打开链路（已打开则先关旧的再开）
 * @param w serial 组件控件
 * @param port 端口名；传 NULL / 空串表示用控件属性里配的
 * @return int 0 成功、-1 失败（原因见 cg_serial_last_error(NULL)，不影响界面运行）
 */
int  cg_widget_serial_open(cg_widget_t *w, const char *port);

/**
 * @brief 关掉串口组件的链路（内部把句柄置空，重复调真的安全）
 * @param w serial 组件控件
 * @return void 无返回值（未打开时调它也没事）
 */
void cg_widget_serial_close(cg_widget_t *w);

/**
 * @brief 改串口组件的波特率（界面上的波特率下拉就是给这用的）
 * @param w serial 组件控件
 * @param baud 目标波特率；链路开着时按新参数原样重开，没开就只记住等下次打开用
 * @return void 无返回值
 */
void cg_widget_serial_set_baud(cg_widget_t *w, int baud);

/**
 * @brief 改串口组件的数据位 / 停止位 / 校验（界面上的三个下拉就是给这用的）
 * @param w serial 组件控件
 * @param bits 数据位，5..8
 * @param stop 停止位，1..2
 * @param parity 校验，0=无 / 1=奇 / 2=偶
 * @return void 无返回值
 * @note 链路开着就按新参数**重开**（先摘干净再改，避免"旧链路按新参数"的错觉）；
 *       任一个越界就整个忽略并记一条警告 —— 只改一半会造出谁也没要的组合
 */
void cg_widget_serial_set_format(cg_widget_t *w, int bits, int stop, int parity);

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
 * v1 的边界（有意不做，别猜）：**流那套只有 TCP**；UDP 走另一套拉取式接口
 * （cg_net_udp_open / cg_net_sendto / cg_net_recvfrom，见下面「网口：UDP」一节）。不做 TLS；
 * 不自动重连；写是同步的（带超时，不是写队列）；接收缓冲 64KB 满了丢最老的（与串口同策略）。
 * 链接：需要 winsock2（`-lws2_32`，构建脚本里已经加上）。
 */
typedef struct cg_net_t cg_net_t;

/**
 * @brief 以客户端身份连到一台主机（TCP）
 * @param host 主机名或 IP；传 NULL / 空串 = 127.0.0.1（本机自测最常用）
 * @param port 目标端口
 * @param timeout_ms 连接超时毫秒，<= 0 按 3000 算
 * @return cg_net_t* 连接句柄（用完 cg_net_close）；失败返回 NULL，原因见 cg_net_last_error(NULL)
 * @note 用非阻塞 connect + select 实现超时 —— 坏地址不会把界面冻住
 */
cg_net_t *cg_net_connect(const char *host, int port, int timeout_ms);

/**
 * @brief 监听端口（TCP 服务端）
 * @param port 本地端口
 * @param backlog 等待队列长度，<= 0 按 4
 * @return cg_net_t* 监听句柄；绑不上（端口被占）返回 NULL，原因见 cg_net_last_error(NULL)
 */
cg_net_t *cg_net_listen(int port, int backlog);

/**
 * @brief 从监听句柄收一个连接
 * @param srv cg_net_listen 返回的监听句柄
 * @param timeout_ms 等待毫秒；**<= 0 = 不等，立刻返回**（适合放在时钟里轮询）
 * @return cg_net_t* 新连接的独立句柄（用完 cg_net_close）；没有连接返回 NULL —— 那是正常情况，不是错误
 */
cg_net_t *cg_net_accept(cg_net_t *srv, int timeout_ms);

/**
 * @brief 发数据（同步，带 1 秒超时，不是写队列）
 * @param n 连接句柄
 * @param data 要发的字节
 * @param len 字节数
 * @return int 实际写出的字节数；< 0 失败
 */
int  cg_net_write(cg_net_t *n, const void *data, int len);
/**
 * @brief 从接收缓冲取数据（**非阻塞**）
 * @param n 连接句柄
 * @param buf 输出缓冲
 * @param cap buf 的字节数
 * @return int 实际取到的字节数，最多 cap；没数据返回 0，对端已关闭时也能把剩下的取完
 */
int  cg_net_read(cg_net_t *n, void *buf, int cap);
/**
 * @brief 接收缓冲里还有多少字节没取走
 * @param n 连接句柄
 * @return int 未读字节数
 */
int  cg_net_available(const cg_net_t *n);
/**
 * @brief 对端还在吗
 * @param n 连接句柄
 * @return int 1 还在、0 对端已关闭（监听句柄恒为 0）
 * @note 这是判断"掉线"的唯一信号
 */
int  cg_net_connected(const cg_net_t *n);
/**
 * @brief 累计收了多少字节
 * @param n 连接句柄
 * @return long long 只增不减；状态栏用
 */
long long cg_net_rx_bytes(const cg_net_t *n);
/**
 * @brief 累计发了多少字节
 * @param n 连接句柄
 * @return long long 只增不减；状态栏用
 */
long long cg_net_tx_bytes(const cg_net_t *n);
/**
 * @brief 取对端地址
 * @param n 连接句柄
 * @return const char* "ip:port"（监听句柄返回"（监听中）"）；指向内部缓冲，别 free
 */
const char *cg_net_peer(const cg_net_t *n);
/**
 * @brief 取最后一次错误的中文描述
 * @param n 连接句柄；传 NULL 时给"最近一次连接 / 监听失败"的原因
 * @return const char* 指向内部缓冲，下次报错会被覆盖；没出错返回 ""
 */
const char *cg_net_last_error(const cg_net_t *n);
/**
 * @brief 关掉连接或监听
 * @param n 连接 / 监听句柄；NULL 安全，但**不是幂等**：这一调把句柄结构释放掉，
 *          同一个指针重复调就是释放后使用（关完请立刻把变量置空，与串口关流同一个坑）
 * @return void 无返回值
 * @note 会等读线程退出，所以最坏卡一个收超时周期（约 50ms）
 */
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
/**
 * @brief 开一个 UDP 句柄（拉取式，没有读线程）
 * @param local_port 本地绑定端口
 * @param broadcast 非 0 允许发广播（板子回包就知道它的 IP 那种发现用法）
 * @return cg_net_t* UDP 句柄（用完 cg_net_close）；失败返回 NULL
 */
cg_net_t *cg_net_udp_open(int local_port, int broadcast);
/**
 * @brief 发一个 UDP 数据报
 * @param n UDP 句柄
 * @param host 目标主机（每次调用解析一次主机名：填 IP 无所谓，填域名会多花一点时间）
 * @param port 目标端口
 * @param data 数据报内容
 * @param len 字节数
 * @return int 实际发出的字节数；< 0 失败
 */
int  cg_net_sendto(cg_net_t *n, const char *host, int port, const void *data, int len);
/**
 * @brief 收一个 UDP 数据报（非阻塞，一次一个，带出来源）
 * @param n UDP 句柄
 * @param ip_out 来源 IP 的输出缓冲；传 NULL 表示不关心来源
 * @param ip_cap ip_out 的字节数
 * @param port_out 来源端口的出参；传 NULL 表示不关心
 * @param buf 数据报内容的输出缓冲
 * @param cap buf 的字节数
 * @return int 语义见上面那段：> 0 收到的字节数、= 0 收到空数据报、= -1 暂时没数据（不是错误）
 */
int  cg_net_recvfrom(cg_net_t *n, char *ip_out, int ip_cap, int *port_out, void *buf, int cap);

/* ------------------------------------------------------------ 通用对话框
 * 都不是控件，是弹系统对话框的函数。parent 传控件（定位用）或 NULL。
 * 路径缓冲区至少 1024 字节。返回 1 = 用户确定，0 = 取消/失败。
 */
/**
 * @brief 弹"打开文件"对话框
 * @param parent 定位用的控件，可为 NULL
 * @param title 对话框标题，可为 NULL
 * @param filter 过滤器串，如 "文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0"；可为 NULL
 * @param out_path 用户选中路径的输出缓冲，至少 1024 字节
 * @param out_size out_path 的字节数
 * @return int 1 用户确定、0 取消或失败
 */
int  cg_open_file_dialog(cg_widget_t *parent, const char *title,
                         const char *filter, char *out_path, int out_size);
/**
 * @brief 弹"保存文件"对话框
 * @param parent 定位用的控件，可为 NULL
 * @param title 对话框标题，可为 NULL
 * @param filter 过滤器串，写法同 cg_open_file_dialog；可为 NULL
 * @param default_ext 缺省扩展名（不带点），可为 NULL
 * @param out_path 用户选中路径的输出缓冲，至少 1024 字节
 * @param out_size out_path 的字节数
 * @return int 1 用户确定、0 取消或失败
 */
int  cg_save_file_dialog(cg_widget_t *parent, const char *title,
                         const char *filter, const char *default_ext,
                         char *out_path, int out_size);
/**
 * @brief 弹取色对话框
 * @param parent 定位用的控件，可为 NULL
 * @param rgb 入出参：进来是初始色、确认时写回用户选的色，编码都是 0xRRGGBB
 * @return int 1 用户确定、0 取消或失败
 */
int  cg_choose_color(cg_widget_t *parent, unsigned *rgb);
/**
 * @brief 弹消息框（只有一个确定钮）
 * @param parent 定位用的控件，可为 NULL
 * @param text 正文
 * @param title 标题，可为 NULL
 * @param icon 图标：0=信息 1=警告 2=错误 3=问号
 * @return void 无返回值
 */
void cg_message_box(cg_widget_t *parent, const char *text, const char *title, int icon);
/**
 * @brief 弹一个单行输入框，等用户点确定 / 取消（模态）
 * @param parent 定位用的控件，可为 NULL
 * @param title 标题，NULL 用 "输入"
 * @param prompt 输入框上方的提示，可为 NULL
 * @param value 初始值，可为 NULL —— 弹出来就**全选**，直接敲字等于替换
 * @param out 输出缓冲，用户点确定时写进 UTF-8 文本（按 cap 截断，总以 '\0' 结尾）
 * @param cap out 的字节数
 * @return int 1 = 用户点了确定（out 里有值）；0 = 取消 / 失败 / 当前没有窗口
 * @note **无窗口（自检）时不弹**，直接返回 0 —— 业务据此走"没改"的分支，别在这条路径上读盘写盘
 * @note 模态期间应用自己的定时器与窗口消息仍会被派发（与本工程其它通用对话框一致）
 * @note 外观用的是系统对话框：本工程没有 .rc 资源，模板在内存里拼；字体 / 字号取属主窗口的，
 *       所以与主界面一致（换字号不会错位：DLU 按同一套字体算）
 */
int cg_input_box(cg_widget_t *parent, const char *title, const char *prompt,
                 const char *value, char *out, int cap);

/* ------------------------------------------------------------ 画板
 * canvas 控件上用代码绘制。坐标系是控件内左上角 (0,0)，
 * x 向右 y 向下；控件重绘（被盖住再露出来、改大小）时按记录重画。
 */
/**
 * @brief 清掉画板上的全部图形（连带绘制记录）
 * @param w canvas 控件
 * @return void 无返回值
 */
void cg_canvas_clear(cg_widget_t *w);
/**
 * @brief 定下后续图形的颜色
 * @param w canvas 控件
 * @param rgb 0xRRGGBB，或 CG_RGB(r,g,b)
 * @return void 无返回值（对之后画的图形生效，已画的不动）
 */
void cg_canvas_set_color(cg_widget_t *w, unsigned rgb);
/**
 * @brief 定下后续图形的线宽
 * @param w canvas 控件
 * @param px 线宽（逻辑像素）
 * @return void 无返回值（对之后画的图形生效）
 */
void cg_canvas_set_line_width(cg_widget_t *w, int px);
/**
 * @brief 画一条直线（用当前颜色与线宽）
 * @param w canvas 控件
 * @param x1 起点 x（控件内坐标，逻辑像素）
 * @param y1 起点 y
 * @param x2 终点 x
 * @param y2 终点 y
 * @return void 无返回值
 */
void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2);
/**
 * @brief 画一个矩形
 * @param w canvas 控件
 * @param x 左上角 x
 * @param y 左上角 y
 * @param wd 宽
 * @param ht 高
 * @param fill 0 只描边、非 0 实心
 * @return void 无返回值
 */
void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
/**
 * @brief 画一个椭圆（矩形内切）
 * @param w canvas 控件
 * @param x 外接矩形左上角 x
 * @param y 外接矩形左上角 y
 * @param wd 外接矩形宽
 * @param ht 外接矩形高
 * @param fill 0 只描边、非 0 实心
 * @return void 无返回值
 */
void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
/**
 * @brief 在画板上写一段文字
 * @param w canvas 控件
 * @param x 文字左上角 x
 * @param y 文字左上角 y
 * @param text UTF-8 文字，函数内部拷贝
 * @return void 无返回值
 */
void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text);

/* ------------------------------------------------------------ 网页浏览 */
/**
 * @brief 让 browser 控件打开一个地址
 * @param w browser 控件
 * @param url 网址或本地文件路径（http / https / file 均可）
 * @return void 无返回值
 * @note 实验性功能（宿主是系统的 WebBrowser 控件）
 */
void cg_browser_navigate(cg_widget_t *w, const char *url);

/* ------------------------------------------------------------ 日志
 * 为什么需要它：GUI 版（-mwindows）没有控制台，printf / stderr 用户一个字都看不见。
 * 运行时启动时会自己开一份日志（%LOCALAPPDATA%\<exe名>\app.log），这里让业务代码
 * 也能往同一个文件里补记录 —— 用户报"点了没反应"时，让他把日志发过来就能查。
 */
#define CG_LOG_INFO  0
#define CG_LOG_WARN  1
#define CG_LOG_ERROR 2

/**
 * @brief 写一条日志（printf 风格）
 * @param level 等级，CG_LOG_INFO / CG_LOG_WARN / CG_LOG_ERROR
 * @param fmt printf 风格格式串
 * @return void 无返回值（始终镜像到 stderr，并追加到日志文件）
 */
void cg_log(int level, const char *fmt, ...);
/**
 * @brief 设日志文件的过滤等级
 * @param level 只记 >= 它的条目
 * @return void 无返回值（默认 CG_LOG_INFO）
 */
void cg_log_set_level(int level);

/* ------------------------------------------------------------ 基础能力（cg_kit）
 * 和界面无关，但做真工具一定会用到：读写文件、配置持久化、文本与编码、剪贴板、
 * 打开外部程序、注册表。**凡是返回 char* 的函数都是堆内存，用完请 cg_free()**。
 * 这样业务代码不必 include windows.h / stdlib.h，仍然只认本头文件。
 */

/**
 * @brief 释放上面这些函数返回的堆内存（等价 free()）
 * @param p 堆指针；NULL 安全
 * @return void 无返回值
 * @note 省得业务代码去 include stdlib.h
 */
void cg_free(void *p);

/* ---- 文件 ---- */
/**
 * @brief 文件是否存在（目录不算）
 * @param path 路径（UTF-8）
 * @return int 1 存在、0 不存在或路径上是个目录
 */
int   cg_file_exists(const char *path);
/**
 * @brief 读整个文件成字符串（去掉 UTF-8 BOM）
 * @param path 路径（UTF-8）
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 * @warning **含 0x00 的二进制不要用它读** —— 它返回的是字符串，`strlen` 会在第一个 0
 *          处截断。固件/镜像/抓包请用下面的 cg_file_read_bytes
 */
char *cg_file_read(const char *path);

/**
 * @brief 读整个**二进制**文件
 * @param path 路径（UTF-8）
 * @param out_len 长度出口，可传 NULL；失败时不动
 * @return unsigned char * 堆缓冲，用完 cg_free —— **0 字节文件也返回非 NULL**
 *         （靠这个区分"空文件"与"打不开"）；失败返回 NULL
 * @note **不去 BOM**：二进制一个字节都不许动
 * @note 长度出口是 int，超过 2GB 的文件会报失败（记一条日志）
 */
unsigned char *cg_file_read_bytes(const char *path, int *out_len);

/**
 * @brief 覆盖写**二进制**文件
 * @param path 路径（UTF-8）
 * @param data 要写的字节
 * @param len 字节数；可以为 0（建一个空文件）
 * @return int 0 成功、非 0 失败
 */
int cg_file_write_bytes(const char *path, const void *data, int len);

/**
 * @brief 取文件大小
 * @param path 路径（UTF-8）
 * @return long long 字节数；不存在 / 目录 / 调不动返回 -1
 * @note 想"不读内容先问大小"时用它
 */
long long cg_file_size(const char *path);
/**
 * @brief 覆盖写文件（UTF-8 文本）
 * @param path 路径（UTF-8）
 * @param text 文本内容，函数内部写盘
 * @return int 0 成功、非 0 失败
 */
int   cg_file_write(const char *path, const char *text);
/**
 * @brief 追加到文件末尾（文件不存在就新建）
 * @param path 路径（UTF-8）
 * @param text 追加的内容
 * @return int 0 成功、非 0 失败
 */
int   cg_file_append(const char *path, const char *text);
/**
 * @brief 删文件
 * @param path 路径（UTF-8）
 * @return int 0 成功、非 0 失败
 */
int   cg_file_delete(const char *path);
/**
 * @brief 拷文件（覆盖已存在的目标）
 * @param from 源路径（UTF-8）
 * @param to 目标路径（UTF-8）
 * @return int 0 成功、非 0 失败
 */
int   cg_file_copy(const char *from, const char *to);
/**
 * @brief 目录是否存在
 * @param path 路径（UTF-8）
 * @return int 1 存在、0 不存在
 */
int   cg_dir_exists(const char *path);
/**
 * @brief 建目录
 * @param path 路径（UTF-8）；父目录必须已存在
 * @return int 0 成功、非 0 失败
 */
int   cg_dir_create(const char *path);
/**
 * @brief 枚举目录内容（一次一个名字，配 index 从 0 递增着调）
 * @param dir 目录路径（UTF-8）
 * @param filter 通配符，如 "*.txt"；传 NULL 或 "*" 表示所有文件
 * @param index 第几个条目，0 起
 * @param out 名字的输出缓冲
 * @param out_size out 的字节数
 * @return int 1 = 拿到一个名字（写进 out）、0 = 没有了
 * @note 不返回 "." 与 ".."
 */
int   cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size);

/* ---- 路径 ---- */
/**
 * @brief 拼路径（自动补一个分隔符）
 * @param a 前半段
 * @param b 后半段
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 */
char *cg_path_join(const char *a, const char *b);
/**
 * @brief 取路径的目录部分（不含末尾分隔符）
 * @param path 路径
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 */
char *cg_path_dir(const char *path);
/**
 * @brief 取路径的文件名部分
 * @param path 路径
 * @return const char* **path 内部的指针，不用释放**；path 为 NULL 返回空串
 */
const char *cg_path_name(const char *path);
/**
 * @brief 取路径的扩展名（含点，如 ".txt"）
 * @param path 路径
 * @return const char* **path 内部的指针，不用释放**；没有扩展名返回 ""
 */
const char *cg_path_ext(const char *path);
/**
 * @brief 取 exe 所在目录
 * @return char * 堆串，用完 cg_free
 * @note 写日志、读同目录配置文件用
 */
char *cg_app_dir(void);
/**
 * @brief 取本程序的用户数据目录（会自动建）
 * @return char * 堆串（形如 %LOCALAPPDATA%\<exe名>），用完 cg_free
 * @note 存配置 / 缓存用
 */
char *cg_app_data_dir(void);

/* ---- 配置（INI 文本格式） ---- */
/**
 * @brief 读 INI 里的一个键
 * @param path ini 文件路径（UTF-8）
 * @param section 节名（方括号里的那个）
 * @param key 键名
 * @param def 文件 / 节 / 键不存在时用它的副本顶上；可传 NULL 表示返回 NULL
 * @return char * 堆串，用完 cg_free；键不存在时是 def 的副本
 */
char *cg_ini_read(const char *path, const char *section, const char *key, const char *def);
/**
 * @brief 写一个键（文件里其它内容保持不动，文件没有就新建）
 * @param path ini 文件路径（UTF-8）
 * @param section 节名
 * @param key 键名
 * @param value 要写的值
 * @return int 0 成功、非 0 失败
 */
int   cg_ini_write(const char *path, const char *section, const char *key, const char *value);

/* ---- 文本与编码 ---- */
/**
 * @brief 拼字符串（printf 风格）
 * @param fmt printf 风格格式串
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 */
char *cg_str_format(const char *fmt, ...);
/**
 * @brief 复制一个字符串
 * @param s 源串；为 NULL 时返回 NULL
 * @return char * 堆串，用完 cg_free
 * @note 与 cg_str_format("%s", s) 等价，但不用过一遍 printf —— 想留下借用串的副本时用它
 */
char *cg_str_dup(const char *s);
/**
 * @brief 去掉首尾空白（空格 / 制表 / 换行）
 * @param s 源串；为 NULL 时返回 NULL
 * @return char * 堆串，用完 cg_free
 */
char *cg_str_trim(const char *s);
/**
 * @brief 比较字符串（忽略英文大小写）
 * @param a 串一；可传 NULL —— NULL 只与 NULL 相等，与 "" 不相等
 * @param b 串二；可传 NULL —— 同上
 * @return int 1 相等、0 不等
 */
int   cg_str_equal(const char *a, const char *b);
/**
 * @brief 把 src 里出现的 from 全部换成 to
 * @param src 源串
 * @param from 被替换的子串；为空则返回 src 的副本
 * @param to 替换成的子串
 * @return char * 堆串，用完 cg_free
 */
char *cg_str_replace(const char *src, const char *from, const char *to);
/**
 * @brief UTF-8 -> GBK
 * @param utf8 UTF-8 文本
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 * @note 读老式 ANSI 文件后转成界面能显示的
 */
char *cg_utf8_to_gbk(const char *utf8);
/**
 * @brief GBK -> UTF-8
 * @param gbk GBK 文本
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 * @note 要把界面里的中文写进老式 ANSI 文件时用
 */
char *cg_gbk_to_utf8(const char *gbk);

/* ---- 编码：十六进制 / Base64 / URL ----
 * 都是**调用方给缓冲**（与 cg_open_file_dialog / cg_serial_read 同一口径），
 * 不在这里 malloc —— 业务多是"把结果喂给控件或直接发出去"，少一次分配少一处泄漏面。
 * 返回值统一是**写出的长度**（不含结尾 '\0'）：参数或内容非法返回 -1；
 * 解码类缓冲放不下返回 -2（编码类需要的长度能事先算出来，放不下也归 -1）。
 */

/**
 * @brief 字节 -> 十六进制文本
 * @param data 源字节
 * @param len 字节数
 * @param sep 每字节之间的分隔串；NULL 或 "" 表示不隔（显示常用 " "）
 * @param upper 非 0 用大写字母
 * @param out 输出缓冲，需要 len * (2 + strlen(sep)) + 1 字节
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；参数非法或放不下返回 -1
 */
int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size);

/**
 * @brief 十六进制文本 -> 字节
 * @param text 十六进制文本；空格 / 制表 / 换行 / 逗号 / 连字符都当分隔符，大小写都认，
 *             "1A2B" 这种不写分隔的也算对
 * @param out 输出缓冲
 * @param out_size out 的字节数
 * @return int 解出的字节数；非法字符或只剩半个字节返回 -1，out 放不下返回 -2
 * @note 语义与 examples\serialterm、netterm 里那两段手写实现一致
 */
int cg_hex_decode(const char *text, void *out, int out_size);

/**
 * @brief Base64 编码（RFC 4648，带 '=' 填充）
 * @param data 源字节
 * @param len 字节数
 * @param out 输出缓冲，需要 ((len + 2) / 3) * 4 + 1 字节
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；参数非法或放不下返回 -1
 */
int cg_base64_encode(const void *data, int len, char *out, int out_size);

/* ---- 字节操作（拼帧 / 拆帧的基元）
 * 来历：精易 `字节集_` 那一族（27 条：反转 / 取左右 / 找子集 / 替换 …）在 C 侧**不需要一个
 * 新的"字节缓冲类型"** —— 本工程的房屋风格是 `(data, len, …, out, out_size)` 进、返回写出的
 * 长度，再加个会分配内存的类型等于多一套所有权模型；而那一族里最常用的两个
 * （字节 ↔ 十六进制文本）早就有 `cg_hex_decode` / `cg_hex_encode` 了。
 * 所以这里只补**缺的基元**：反转、切片、查找、按字节序读写 16/32 位整数
 * （最后两个是工控拼帧/拆帧的真正核心：报文里就是大端小端的 u16/u32）。
 * 字节序参数统一：`big_endian` 非 0 = 大端（网络序 / Modbus 的寄存器序），0 = 小端。
 */

/**
 * @brief 原地反转字节序（精易的"字节集_反转"）
 * @param data 目标缓冲（会被就地改写）
 * @param len 字节数；len <= 1 或 data 为 NULL 时什么都不做
 * @return void 无返回值
 */
void cg_bytes_reverse(void *data, int len);

/**
 * @brief 取一段字节（"取左边 / 取右边 / 取中间"）：从 from 起取 count 个
 * @param data 源数据
 * @param len 源长度
 * @param from 起点，0 起；越界（< 0 或 >= len）返回 -1，什么都不写
 * @param count 取多少个；**传 -1 = 取到底**（最常见的那一种）
 * @param out 输出缓冲
 * @param out_size out 的字节数
 * @return int 写出的字节数；from 非法 -1、out 放不下 -2、out 为 NULL 或 out_size <= 0 -1
 * @note count 超出剩余长度时**按剩余算**（不报错）—— 这正是"取右边"想要的语义
 */
int cg_bytes_slice(const void *data, int len, int from, int count, void *out, int out_size);

/**
 * @brief 在数据里找子串第一次出现的位置
 * @param data 源数据
 * @param len 源长度
 * @param needle 要找的子串
 * @param nlen 子串长度
 * @param from 从哪个下标开始找，0 起
 * @return int 下标（0 起）；没找到 / 参数非法 / needle 为空返回 -1
 */
int cg_bytes_find(const void *data, int len, const void *needle, int nlen, int from);

/**
 * @brief 从 offset 读一个 16 位无符号整数
 * @param data 源数据
 * @param len 源长度
 * @param offset 起始下标，0 起
 * @param big_endian 非 0 大端（网络序 / Modbus 寄存器序）、0 小端
 * @return int 0..65535；**越界或参数非法返回 -1**（16 位值不可能到 -1，所以能这么用）
 */
int cg_bytes_get_u16(const void *data, int len, int offset, int big_endian);

/**
 * @brief 从 offset 读一个 32 位无符号整数
 * @param data 源数据
 * @param len 源长度
 * @param offset 起始下标，0 起
 * @param big_endian 非 0 大端、0 小端
 * @return long long 0..4294967295；越界或参数非法返回 -1
 * @note 返回值用 long long 而不是 int：`unsigned` 的 0xFFFFFFFF 转成 int 正好是 -1，
 *       那样"错误"会与一个合法值撞车、调用方没法分辨
 */
long long cg_bytes_get_u32(const void *data, int len, int offset, int big_endian);

/**
 * @brief 把 16 位整数写进 out 的 offset 处
 * @param out 目标缓冲
 * @param out_size out 的字节数
 * @param offset 起始下标，0 起
 * @param v 要写的值（只取低 16 位）
 * @param big_endian 非 0 大端、0 小端
 * @return int 1 成功；0 越界 / 参数非法（什么都不写）
 */
int cg_bytes_put_u16(void *out, int out_size, int offset, unsigned v, int big_endian);

/**
 * @brief 把 32 位整数写进 out 的 offset 处
 * @param out 目标缓冲
 * @param out_size out 的字节数
 * @param offset 起始下标，0 起
 * @param v 要写的值
 * @param big_endian 非 0 大端、0 小端
 * @return int 1 成功；0 越界 / 参数非法（什么都不写）
 */
int cg_bytes_put_u32(void *out, int out_size, int offset, unsigned v, int big_endian);

/**
 * @brief Base64 解码
 * @param text Base64 文本；空白与换行忽略，**带不带 '=' 填充都认**（JWT 那类不填充的也能解）
 * @param out 输出缓冲，需要 (strlen + 3) / 4 * 3 字节
 * @param out_size out 的字节数
 * @return int 解出的字节数；长度不合法（不含规填充、尾部有多余位）返回 -1，放不下返回 -2
 */
int cg_base64_decode(const char *text, void *out, int out_size);

/**
 * @brief URL 编码（百分号）
 * @param text 源文本
 * @param space_plus 非 0 时空格编成 '+'（表单风格）
 * @param out 输出缓冲，需要 strlen * 3 + 1 字节
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；参数非法或放不下返回 -1
 * @note 不编码 A-Za-z0-9-_.~，其余按**字节**编成 %XX —— 所以 UTF-8 中文会变成多个 %XX，这是对的
 */
int cg_url_encode(const char *text, int space_plus, char *out, int out_size);

/**
 * @brief URL 解码（'%XX' 与 '+'，'+' 一律还原成空格）
 * @param text 源文本
 * @param out 输出缓冲，需要 strlen + 1 字节
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；'%' 后面不是两位十六进制返回 -1，放不下返回 -2
 */
int cg_url_decode(const char *text, char *out, int out_size);

/* ---- 进制转换与补码 ----
 * 给调试工具用的：寄存器值在十六进制/二进制/十进制之间来回看，是嵌入式与工控的日常
 * （位段看二进制、报文看十六进制、协议文档写十进制）。输出一律**大写**（与数据手册、
 * 寄存器表的写法一致）；要小写十六进制用 cg_hex_encode()。
 */

/**
 * @brief 整数 -> 任意进制文本
 * @param v 待转换的整数
 * @param base 进制 2..36
 * @param out 输出缓冲
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；base 非法或放不下返回 -1
 * @note 负数带前导 '-'；输出一律大写
 */
int cg_int_to_base(long long v, int base, char *out, int out_size);

/**
 * @brief 任意进制文本 -> 整数
 * @param s 待解析文本
 * @param base 进制 2..36
 * @param out 结果出口；可传 NULL（只想问"合不合法"）
 * @return int 1 成功、0 失败（此时 out 不动）
 * @note 认前导 '-'/'+'，也认与进制相符的 0x / 0b / 0o 前缀（调试工具里到处是 `0x1A`）；
 *       允许首尾空白，但**中间有空白即失败**（`"1A 2B"` 是两段，不该被拼成一段）；
 *       整串必须合法、溢出也算失败
 */
int cg_base_to_int(const char *s, int base, long long *out);

/**
 * @brief 进制互转（字符串进、字符串出）
 * @param s 源串
 * @param from_base 源进制 2..36
 * @param to_base 目标进制 2..36
 * @param out 输出缓冲
 * @param out_size out 的字节数
 * @return int 写入的长度（不含结尾 0）；源串非法或放不下返回 -1（此时 out 是空串）
 * @note 例：cg_base_convert("FF", 16, 2) -> "11111111"
 */
int cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size);

/**
 * @brief 把低 bits 位按**补码**解释成有符号数
 * @param v 原始值（只取低 bits 位）
 * @param bits 位宽，取 1..63；给了 64 或越界就直接按 long long 原样返回（不做符号扩展）
 * @return long long 带符号的结果
 * @note 工控里天天要用：报文给的是 0x…，协议说的是 12 位有符号；
 *       例：cg_to_signed(0xFFF6, 12) -> -10
 */
long long cg_to_signed(unsigned long long v, int bits);

/**
 * @brief 反向：把一个有符号数写成 bits 位补码（只保留低 bits 位）
 * @param v 有符号数
 * @param bits 位宽，取 1..63；越界按原样返回
 * @return unsigned long long bits 位补码
 * @note 拼报文时用它 —— 负数不能直接塞进帧里；例：cg_from_signed(-10, 12) -> 0xFF6
 */
unsigned long long cg_from_signed(long long v, int bits);

/* ---- 校验（CRC） ----
 * 逐位计算、没有静态查表 —— 好处是**天然线程安全**、代码短；代价是比查表慢约 8 倍
 * （实测对协议帧与几 MB 的文件都够快；上百 MB 的大文件不划算，那种场合另说）。
 */

/**
 * @brief 算 CRC32（IEEE 802.3 / zlib 同一套）
 * @param data 数据
 * @param len 字节数
 * @return unsigned 校验值；空输入返回 0
 * @note 多项式 0xEDB88320 反射、初值全 1、末尾取反；文本 "123456789" 的校验值是 0xCBF43926
 */
unsigned cg_crc32(const void *data, int len);

/**
 * @brief 算 CRC16/Modbus（工控设备协议帧最常用的那个）
 * @param data 数据
 * @param len 字节数
 * @return unsigned 校验值（低 16 位有效）；空输入返回 0xFFFF
 * @note 多项式 0xA001 反射、初值 0xFFFF、**不**取反；文本 "123456789" 的校验值是 0x4B37
 */
unsigned cg_crc16(const void *data, int len);

/**
 * @brief 算整个文件的 CRC32（分块读，不把文件整读进内存 —— 固件镜像动辄几十上百 MB）
 * @param path 文件路径（UTF-8）
 * @param out 校验值出口；成功才写入
 * @return int 1 成功；0 打不开或中途读出错
 * @note 宁可报失败，也不给一个"看着像但不完整"的值
 */
int cg_crc32_file(const char *path, unsigned *out);

/* ---- Modbus 帧编解码（RTU / TCP） ----
 * 工控上位机十有八九要读写寄存器，而"拼请求帧 / 认响应帧 / 认出异常帧"这段与界面、与传输
 * 都无关，**每个项目都要重写一遍** —— 这里给现成的。**纯函数：不持状态、不碰 I/O**，
 * 连接 / 超时 / 重试 / 轮询节奏仍归应用（框架给原料，事务归业务）。
 *
 *   unsigned char frame[32];
 *   int n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 1, 3, 0x006B, 3, frame, sizeof frame);
 *   cg_serial_write(ser, frame, n);                     // 01 03 00 6B 00 03 76 87
 *
 *   cg_modbus_rsp_t r;
 *   int pr = cg_modbus_parse(CG_MODBUS_RTU, buf, n2, &r);
 *   if (pr == 0) {
 *       if (r.exception) cg_log(CG_LOG_ERROR, "从站报异常码 %d", r.exception);
 *       else for (i = 0; i < r.nvalues; i++) cg_log(CG_LOG_INFO, "寄存器 %d = %u",
 *                                                   r.addr < 0 ? i : r.addr, r.values[i]);
 *   } else if (pr == -2) cg_log(CG_LOG_ERROR, "帧 CRC 不对 —— 线上的数据坏了");
 *
 * 两条替你钉住的约定（手写时最容易错的两处）：**RTU 的 CRC 低字节在前**；
 * **异常响应的功能码在线上是 `原功能码 | 0x80`**，解析后 `out->func` 给的是原功能码、
 * `out->exception` 给异常码。另外 0x05 在线路上是 `FF00`/`0000` 而不是 1/0：
 * 拼装时给 1/0 也认（替你换算），解析时原样给出（调试工具该显示线上真实的东西）。
 */
#define CG_MODBUS_RTU  0   /* 帧尾带 CRC16（低字节在前） */
#define CG_MODBUS_TCP  1   /* 带 7 字节 MBAP 头，帧尾无 CRC */

/** 响应帧最多解出几个值：规范上限是 250 字节数据 ⇒ 125 个寄存器（位类最多解到这么多为止） */
#define CG_MODBUS_MAX_VALUES 125

/** 解析结果。异常响应时 nvalues = 0、exception 非 0；写响应把回显的地址与值填进来 */
typedef struct {
    int            slave;      /* 从站地址（TCP 时取自 MBAP 的单元标识） */
    int            func;       /* 功能码（异常响应给的是**原**功能码，不是 |0x80 那个） */
    int            exception;  /* 0 = 正常；非 0 = 异常码（1 非法功能 / 2 非法地址 / 3 非法数据 / 4 从站故障 …） */
    int            addr;       /* 写响应的地址回显；读响应 / 异常响应为 -1 */
    int            nvalues;    /* values 里有效个数；异常响应为 0 */
    unsigned short values[CG_MODBUS_MAX_VALUES];   /* 读响应：寄存器值 / 位类的 0|1；写响应：回显值 */
} cg_modbus_rsp_t;

/**
 * @brief 拼"读类"请求帧：**线圈 01 / 离散输入 02 / 保持寄存器 03 / 输入寄存器 04**
 * @param tcp CG_MODBUS_RTU（0）或 CG_MODBUS_TCP（1，带 7 字节 MBAP 头）
 * @param tid 事务号，TCP 用；RTU 忽略，随便传 0
 * @param slave 从站地址 0..255
 * @param func 功能码：1 / 2 / 3 / 4
 * @param start 起始地址 0..65535
 * @param count 个数：位类 1..2000、寄存器类 1..125（规范上限）
 * @param out 输出缓冲，帧写在这里
 * @param out_size out 的字节数
 * @return int 写入 out 的**帧长度**（RTU 8 字节 / TCP 12 字节）；参数非法或放不下返回 -1
 */
int cg_modbus_build_read(int tcp, int tid, int slave, int func, int start, int count,
                         void *out, int out_size);

/**
 * @brief 拼"写类"请求帧：**单线圈 05 / 单寄存器 06 / 多线圈 0F / 多寄存器 10**
 * @param tcp CG_MODBUS_RTU（0）或 CG_MODBUS_TCP（1）
 * @param tid 事务号，TCP 用；RTU 忽略，随便传 0
 * @param slave 从站地址 0..255
 * @param func 功能码：5 / 6 / 15 / 16
 * @param start 起始地址 0..65535
 * @param vals 要写的值数组；05/06 只看第一个，0F/10 最多读前 123 个
 * @param nvalues vals 的个数：05/06 必须为 1（1/0 会替你换成 FF00/0000），0F/10 最多 123
 * @param out 输出缓冲，帧写在这里
 * @param out_size out 的字节数
 * @return int 帧长度；参数非法或放不下返回 -1
 */
int cg_modbus_build_write(int tcp, int tid, int slave, int func, int start,
                          const unsigned short *vals, int nvalues, void *out, int out_size);

/**
 * @brief 解析响应帧（读响应 / 写响应 / 异常响应都认）
 * @param tcp 帧格式，必须与拼请求帧时一致
 * @param frame 收到的整帧（先切帧再解析）
 * @param len 帧长度
 * @param out 解析结果出口；成功才写
 * @return int 0 成功；-1 帧太短或参数非法；-2 RTU 的 CRC 不对（线上数据坏了）；
 *             -3 格式不对（协议标识 / 长度字段 / 功能码 / 字节数跟规范对不上）
 * @note 负值分得这么细，是为了让日志能直接说清"是坏了还是我拼错了"；
 *       `out->func` 在异常响应里给的是**原**功能码，异常码在 `out->exception`
 */
int cg_modbus_parse(int tcp, const void *frame, int len, cg_modbus_rsp_t *out);

/* ---- 字节流切帧（串口 / TCP 都适用） ----
 * 串口与 TCP 给的都是**字节流**：一次 cg_serial_read 拿到的可能是半帧、一整帧，也可能两帧
 * 粘在一起（"粘包"）。现场协议的定界办法基本跑不出四种（静默 / 定长 / 分隔符 / 头里带长度），
 * 这里四种收在同一个状态机里 —— 别在每个项目里再手写一遍。
 *
 *   static cg_frame_t    g_fr;                    // 一条链路一份，别共享
 *   static unsigned char g_buf[512];
 *
 *   void on_open(...)  { cg_frame_init(&g_fr, g_buf, sizeof g_buf, CG_FRAME_SILENCE, 4); }
 *
 *   void on_arrive(...) {                                    // 串口的"数据到达"事件
 *       char          buf[4096];
 *       unsigned char frame[512];
 *       int           n = cg_serial_read(cg_widget_serial(self), buf, sizeof buf);
 *       int           flen;
 *
 *       if (n <= 0) return;
 *       cg_frame_feed(&g_fr, buf, n, cg_now_ms());            // 静默模式：这句会把上一帧收尾
 *       while ((flen = cg_frame_take(&g_fr, frame, sizeof frame)) > 0) {
 *           cg_modbus_rsp_t r;                                // 切出整帧，才谈得上解析
 *           if (cg_modbus_parse(CG_MODBUS_RTU, frame, flen, &r) == 0) { ... }
 *       }
 *   }
 *
 * **时间由调用方给**（cg_now_ms()）：于是它是纯状态机，能用"任意切法的字节序列 + 任意时间戳"
 * 严格测，不需要 sleep、不需要真串口。
 * 静默模式下没有定时器也能用（下一个字节到来时同样会发现空档）；但**想让最后一帧马上出来**
 * （对端只发一帧就不发了），就在定时器里调 cg_frame_tick()。
 *
 * 一条物理事实：两帧之间一旦短于 gap 就**无法**再分开（RTU 要求帧间 ≥ t3.5，从站违反它时
 * 任何切帧器都没辙）⇒ 静默模式必须配 CRC 用（cg_crc16 / cg_modbus_parse），坏了丢帧重来。
 */
#define CG_FRAME_SILENCE  0   /* 帧间静默：隔 gap_ms 没新字节算一帧结束（Modbus RTU 的 t3.5） */
#define CG_FRAME_FIXED    1   /* 定长：每 N 字节一帧 */
#define CG_FRAME_DELIM    2   /* 分隔符：读到该字节算一帧结束（**分隔符留在帧里**） */
#define CG_FRAME_LENFIELD 3   /* 头里带长度：第 off 字节起 width 字节是长度 */

/** 队列上限：一次最多排这么多整帧等调用方取走，再多就丢掉并计数（**不静默丢**） */
#define CG_FRAME_QUEUE 8

/** 切帧状态。**自己声明一份、别共享**：一条链路（一个串口 / 一个 socket）一份。
 *  缓冲由调用方给（与 cg_serial_read / 编码那批同一口径：这里不 malloc）。 */
typedef struct {
    int            mode;                    /* CG_FRAME_* */
    unsigned long  gap_ms;                  /* SILENCE：静默多久算一帧结束 */
    int            fixed;                   /* FIXED：每帧字节数 */
    int            delim;                   /* DELIM：结束字节 */
    int            lf_off;                  /* LENFIELD：长度字段在帧内偏移 */
    int            lf_width;                /* LENFIELD：1 或 2 字节 */
    int            lf_le;                   /* LENFIELD：小端？ */
    int            lf_add;                  /* LENFIELD：帧总长 = 长度值 + lf_add */
    int            min_len;                 /* 少于这么多字节不算一帧（噪声） */
    int            max_len;                 /* 一帧上限：超了整帧丢掉并计数 */
    unsigned char *buf;                     /* 调用方给的攒帧缓冲 */
    int            cap;                     /* 缓冲大小 */
    int            len;                     /* 当前正在攒的那帧已有多少字节 */
    unsigned long  last_ms;                 /* 收到最后一个字节的时刻 */
    unsigned long  dropped;                 /* 累计丢掉的字节数（诊断用） */
    unsigned long  frames;                  /* 累计切出来的整帧数 */
    int            q_base;                  /* 队首那帧在缓冲里的偏移 */
    int            q_head;                  /* 队列头下标 */
    int            q_count;                 /* 队列里几帧 */
    int            q_len[CG_FRAME_QUEUE];   /* 各帧长度（位置由 q_base 累加得出） */
} cg_frame_t;

/**
 * @brief 初始化切帧器
 * @param f 切帧状态（自己声明一份、别共享）
 * @param buf 调用方给的攒帧缓冲，够放"最大的一帧"即可（这里不 malloc）
 * @param buf_size buf 的字节数
 * @param mode 定界方式，取 CG_FRAME_SILENCE / FIXED / DELIM / LENFIELD
 * @param arg 按 mode 解释：SILENCE = 静默毫秒 / FIXED = 每帧字节数 / DELIM = 结束字节 /
 *            LENFIELD = 长度字段的字节偏移（缺省宽 2、大端、帧总长 = 长度值 + 偏移 + 2）
 * @return int 0 成功；-1 参数不合法（mode 不认识 / 缓冲太小 / arg 越界）
 */
int cg_frame_init(cg_frame_t *f, void *buf, int buf_size, int mode, int arg);

/**
 * @brief 配 LENFIELD 模式的长度字段细节
 * @param f 切帧状态
 * @param width 长度字段宽度，1 或 2 字节
 * @param little_endian 非 0 小端、0 大端
 * @param add 帧总长 = 长度值 + add；传 > 0 才生效
 * @return void 无返回值
 */
void cg_frame_lenfield(cg_frame_t *f, int width, int little_endian, int add);

/**
 * @brief 设帧长的上下界
 * @param f 切帧状态
 * @param min_len 少于这么多字节当噪声丢掉；传 > 0 才生效
 * @param max_len 高于这么多字节整帧丢掉；传 > 0 才生效，且不许超过缓冲大小
 * @return void 无返回值（丢掉的字节累加进 dropped）
 */
void cg_frame_limits(cg_frame_t *f, int min_len, int max_len);

/**
 * @brief 喂入刚收到的字节
 * @param f 切帧状态
 * @param data 刚收到的字节（可以是半帧、整帧或几帧粘在一起）
 * @param len 字节数
 * @param now_ms 当前毫秒时基（传 cg_now_ms()；时间由调用方给，所以它是纯状态机）
 * @return int 0 已收下；-1 参数非法，或缓冲被没取走的帧占满（先 cg_frame_take）
 */
int cg_frame_feed(cg_frame_t *f, const void *data, int len, unsigned long now_ms);

/**
 * @brief 取一帧切好的整帧（按到达顺序）
 * @param f 切帧状态
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return int > 0 帧长（已写进 out）；0 还没有完整帧；-1 out 放不下（帧仍在队列里）
 */
int cg_frame_take(cg_frame_t *f, void *out, int cap);

/**
 * @brief 推进时间：静默到点就把攒着的收尾
 * @param f 切帧状态
 * @param now_ms 当前毫秒时基（传 cg_now_ms()）
 * @return void 无返回值（没有定时器也能用，但最后一帧要立刻出来就得靠它）
 */
void cg_frame_tick(cg_frame_t *f, unsigned long now_ms);

/**
 * @brief 丢掉没攒完的半帧与队列里没取走的帧
 * @param f 切帧状态
 * @return void 无返回值（丢字节的统计量不清零；"清空接收"按钮用）
 */
void cg_frame_reset(cg_frame_t *f);

/**
 * @brief 取累计丢掉的字节数（噪声 / 超上限 / 队列满）
 * @param f 切帧状态
 * @return unsigned long 字节数 —— **丢要丢得看得见**
 */
unsigned long cg_frame_dropped(const cg_frame_t *f);

/**
 * @brief 取累计切出来的整帧数
 * @param f 切帧状态
 * @return unsigned long 帧数，含还没取走的
 */
unsigned long cg_frame_frames(const cg_frame_t *f);

/**
 * @brief 取毫秒时基（系统 uptime）
 * @return unsigned long 毫秒数；切帧、超时判定、去抖都用它
 * @note 与 cg_time_now()（格式化成"2026-09-27 12:00:00"那种给人看的）不是一回事
 */
unsigned long cg_now_ms(void);

/* ---- 通用报文组装（帧头 / 地址 / 功能码 / 数据 / 校验 / 帧尾） ----
 * 工控、仪表、模块的私有协议基本都长成这个骨架，区别只在"哪几段有、校验怎么算"。
 * 与同族两个的分工：`cg_modbus_*` 是**一种**具体协议；`cg_frame_*` 管把**收到的**字节流切成帧；
 * 这里管按骨架**拼出去的**帧、并按同一骨架**验收到的**帧。都是纯函数，不持状态、不碰 I/O。
 *
 *   static cg_proto_t p;                      // 例：AA 55 | 01 | 03 | 数据 | 累加和 | 0D 0A
 *   unsigned char     frame[64], data[2] = { 0x00, 0x6B };
 *   memset(&p, 0, sizeof p);
 *   p.head = "\xAA\x55"; p.head_len = 2;
 *   p.addr = 0x01;  p.func = 0x03;
 *   p.data = data;  p.data_len = 2;
 *   p.sum  = CG_SUM_ADD8;                     // 校验 = 帧头+地址+功能码+数据的累加和
 *   p.tail = "\r\n"; p.tail_len = 2;
 *   n = cg_proto_build(&p, frame, sizeof frame);      // AA 55 01 03 00 6B 71 0D 0A
 *
 * 三条约定（手写时最容易错）：
 *   1. **校验覆盖"校验值之前的全部字节"**（帧头 + 地址 + 功能码 + [长度] + 数据）；
 *   2. 多字节校验的**字节序**是开关（`sum_big_endian`）：Modbus 那套是低字节在前（传 0）；
 *   3. **地址 / 功能码传 -1 = 这一段不写**；帧头 / 帧尾 / 数据长度传 0 = 这一段没有。
 */
#define CG_SUM_NONE         0   /* 不校验 */
#define CG_SUM_ADD8         1   /* 累加和取低 8 位 */
#define CG_SUM_XOR8         2   /* 逐字节异或 */
#define CG_SUM_ADD16        3   /* 累加和取低 16 位 */
#define CG_SUM_CRC16_MODBUS 4   /* CRC16/Modbus（与 cg_crc16 同一套）*/

/** 报文骨架。**先 memset 成 0** 再填要用的字段（没填的段就是"没有"）。 */
typedef struct {
    const void *head;             /* 帧头（可空，如 AA 55）*/
    int         head_len;
    int         addr;             /* 地址字节 0..255；**-1 = 不写这一段** */
    int         func;             /* 功能码 0..255；**-1 = 不写这一段** */
    int         with_len;         /* 1 = 在功能码后插一个"数据长度"字节（很多私有协议有）*/
    const void *data;             /* 数据区 */
    int         data_len;         /* 数据区长度（with_len 时不能超过 255）*/
    int         sum;              /* CG_SUM_* */
    int         sum_big_endian;   /* 多字节校验：0 = 低字节在前（Modbus 那套），1 = 高字节在前 */
    const void *tail;             /* 帧尾（可空，如 0D 0A）*/
    int         tail_len;
} cg_proto_t;

/**
 * @brief 按报文骨架拼一帧（帧头 / 地址 / 功能码 / [长度] / 数据 / 校验 / 帧尾）
 * @param p 骨架，**先 memset 成 0** 再填要用的字段
 * @param out 输出缓冲，帧写在这里
 * @param out_size out 的字节数
 * @return int 帧长（写进 out）；-1 参数非法或 out 放不下
 */
int cg_proto_build(const cg_proto_t *p, void *out, int out_size);

/**
 * @brief 按同一骨架验一帧，并把字段拆出来
 * @param p 骨架，必须与拼帧时一致
 * @param frame 收到的整帧
 * @param len 帧长度
 * @param out_addr 地址出口，可传 NULL（不想看就不给）
 * @param out_func 功能码出口，可传 NULL
 * @param out_data 数据区指针出口（指向 frame 内部，不用释放），可传 NULL
 * @param out_data_len 数据区长度出口，可传 NULL
 * @return int 0 通过；-1 帧太短或参数非法；-2 校验不对（线上数据坏了）；
 *             -3 对不上骨架（帧头 / 地址 / 功能码 / 帧尾 / 长度字节 不一致）
 * @note 数据长度：`with_len` 时取长度字节（并与实际长度核对），否则由帧长推算
 *       （所以解析时**不看** `cg_proto_t.data_len`）
 */
int cg_proto_parse(const cg_proto_t *p, const void *frame, int len,
                   int *out_addr, int *out_func, const unsigned char **out_data, int *out_data_len);

/* ---- 哈希（摘要） ----
 * MD5 / SHA-1 / SHA-256 / SHA-512，**手写实现**，不调系统的 CryptoAPI。
 * 理由有两条（原先写的"会改掉用户的链接命令"是错的，已按实验更正 —— advapi32 本来就能
 * 解析，见 runtime/src/kit/cg_kit_check.c 文件头）：
 *   1. **FIPS 策略下 CryptoAPI 会拒绝 CALG_MD5**，而"核对固件 MD5"是本功能的高频用法；
 *   2. 结果不依赖客户机器上的 crypto 配置，同一个文件在哪台机器上算出来都一样。
 * 正确性靠公开向量（RFC 1321 / FIPS 180-1,2,4）+ 由 Node/OpenSSL 独立派生的检验值双重钉死。
 */

/**
 * @brief 算一段内存的摘要
 * @param data 数据
 * @param len 字节数
 * @param algo 算法名："md5" / "sha1" / "sha256" / "sha512"，大小写不敏感
 * @return char * **小写十六进制**堆串（MD5 32 字符、SHA-1 40、SHA-256 64、SHA-512 128），
 *         用完 cg_free；算法名不认识或内存不足返回 NULL
 */
char *cg_hash(const void *data, int len, const char *algo);
/**
 * @brief 算一个文件的摘要（分块读，不把文件整读进内存）
 * @param path 文件路径（UTF-8）
 * @param algo 算法名，取值同 cg_hash
 * @return char * **小写十六进制**堆串，用完 cg_free；打不开或算法名不认识返回 NULL
 */
char *cg_hash_file(const char *path, const char *algo);

/* ---- 剪贴板 / 外部程序 / 时间 ---- */
/**
 * @brief 取剪贴板文本
 * @return char * 堆串，用完 cg_free；剪贴板里没有文本返回 NULL
 */
char *cg_clip_get_text(void);
/**
 * @brief 写剪贴板文本
 * @param text UTF-8 文字，函数内部拷贝
 * @return int 0 成功、非 0 失败
 */
int   cg_clip_set_text(const char *text);
/**
 * @brief 把剪贴板里的**图片**存成 PNG（示波器 / 相机截的图直接落进调试记录）
 * @param path 输出 PNG 路径（UTF-8，目录须已存在）
 * @return int 1 成功、0 失败（剪贴板里没图片 / 打不开剪贴板 / 存盘失败，都会写日志）
 * @note 优先取 CF_BITMAP、没有就取 CF_DIB 自己拼一张；实现在 win32 层（要走 GDI+），
 *       所以**需要 exe 真的在跑**（`--selftest` 里 GDI+ 还没起，会返回 0）
 */
int   cg_clip_get_image(const char *path);
/**
 * @brief 把一张**图片文件**写进剪贴板（截图工具"复制"那一半）
 * @param path 图片路径（UTF-8；png/jpg/bmp/gif 都行）
 * @return int 1 成功；0 失败（读不到图 / 打不开剪贴板 / 一个格式都没写进去，都写日志）
 * @note 写 **CF_DIB（24 位）+ CF_BITMAP** 两个格式：CF_DIB 各类软件都认，
 *       CF_BITMAP 给只认位图句柄的老程序。与 `cg_clip_get_image` 对称，同样**需要
 *       exe 真的在跑**（`--selftest` 下 GDI+ 还没起，返回 0）；剪贴板是系统级共享
 *       状态，自检里先问 `cg_app_has_window()` 再调
 */
int   cg_clip_set_image(const char *path);
/**
 * @brief 用系统默认程序打开文件 / 文件夹 / 网址
 * @param path_or_url 路径或网址
 * @return int 0 成功、非 0 失败
 */
int   cg_shell_open(const char *path_or_url);
/**
 * @brief 启动外部程序
 * @param cmd_line 完整命令行（含程序名与参数）
 * @param wait 1 = 等它结束并返回它的退出码、0 = 立刻返回 0
 * @return int wait 时为子进程退出码，否则 0；失败返回非 0
 */
int   cg_run_process(const char *cmd_line, int wait);
/**
 * @brief 取当前时间字符串
 * @param fmt strftime 格式；传 NULL 用 "2026-09-21 02:33:53"
 * @return char * 堆串，用完 cg_free；失败返回 NULL
 */
char *cg_time_now(const char *fmt);

/* ---- 注册表（HKEY_CURRENT_USER） ---- */
/**
 * @brief 读注册表字符串值（HKEY_CURRENT_USER 下）
 * @param subkey 子键路径，如 "Software\\MyApp"
 * @param name 值名
 * @param def 没有这个值时用它的副本顶上；可传 NULL
 * @return char * 堆串，用完 cg_free；值不存在时返回 def 的副本
 */
char *cg_reg_read(const char *subkey, const char *name, const char *def);
/**
 * @brief 写注册表字符串值（没有就新建）
 * @param subkey 子键路径，如 "Software\\Microsoft\\Windows\\CurrentVersion\\Run" 可做开机自启
 * @param name 值名
 * @param value 要写的字符串
 * @return int 0 成功、非 0 失败
 */
int   cg_reg_write(const char *subkey, const char *name, const char *value);
/**
 * @brief 枚举一个键的直接子键名（HKCU 下），'\n' 分隔
 * @param subkey 子键路径，如 "Software\\Classes\\Directory\\shell"
 * @param out 输出缓冲，由调用方提供并保证至少 cap 字节可写
 * @param cap 输出缓冲容量（字节），必须 > 1
 * @return int 写入的字节数（不含结尾 \0）；参数不合法、键不存在或一行都装不下时返回 0 ——
 *         键不存在是"没有"，不是错误
 * @note 只列**直接**子键、不递归；装不下时宁可少列也不截断半行（与 cg_serial_list 同口径）。
 *       配合 cg_reg_read 能做"扫出右键菜单项再逐项读 MUIVerb / command"这类管理工具。
 */
int   cg_reg_subkeys(const char *subkey, char *out, int cap);
/**
 * @brief 删 HKCU 下的字符串值
 * @param subkey 子键路径；NULL 直接失败
 * @param name 值名；NULL 直接失败；空串 "" 删默认值
 * @return int 0 = 调用之后这个值一定不存在（删掉了，或本来就「没有」—— 幂等）；
 *             -1 = 参数为空、编码转换失败，或键在但删不掉
 * @note 和 cg_reg_write 配对用：写上 = 打开开关，删掉 = 恢复原样。
 *       恢复走「删」而不是「写空串」——空值在注册表里仍是一条**存在**的记录，扫出来就是残渣。
 *       右键菜单的 LegacyDisable 尤其如此：写空串是「禁用」，删掉才叫「恢复」。
 */
int   cg_reg_del(const char *subkey, const char *name);
/**
 * @brief 删 HKCU 下一整棵键（连子键带值，等效 reg delete /f）
 * @param subkey 子键路径；NULL 或空串直接失败
 * @return int 0 = 调用之后这棵键一定不存在（删掉了，或本来就「没有」）；
 *             -1 = 参数为空、编码转换失败或删不掉
 * @note **破坏性**：删之前自己先备份（示例里走 `reg.exe export`）。
 *       空串被挡掉是有意的 —— RegDeleteTreeW(HKCU, NULL) 会删掉 HKCU 整个子树，
 *       「参数没填就删掉一大片」这种事不能指望调用方自觉。
 */
int   cg_reg_del_key(const char *subkey);
/* ---- 注册表（跨根 *_ex）----
 * 右键菜单管理这类工具要扫 HKLM（系统区）、读 HKCR（取 CLSID 名字），只认 HKCU
 * 就只能绕 reg.exe 拼命令行。上面 5 个老函数 = 下面 *_ex(root = NULL) 的薄壳，
 * 语义一字不变。 */
/**
 * @brief 读注册表字符串值（指定根）
 * @param root 注册表根，"HKCU" / "HKLM" / "HKCR"（大小写不敏感，也接受 "HKEY_CURRENT_USER" 等全称）；
 *             NULL 或空串 = HKCU；认不出的串 = 参数不合法，直接走回退
 * @param subkey 子键路径，如 "Software\\Classes\\CLSID"
 * @param name 值名
 * @param def 取不到时用它的副本顶上；可传 NULL
 * @return char * 堆串，用完 cg_free；取不到时返回 def 的副本
 * @note 保持系统默认注册表视图（不加 KEY_WOW64_64KEY）：64 位进程就是原生视图。
 */
char *cg_reg_read_ex(const char *root, const char *subkey, const char *name, const char *def);
/**
 * @brief 写注册表字符串值（指定根，没有就新建）
 * @param root 同 cg_reg_read_ex
 * @param subkey 子键路径
 * @param name 值名
 * @param value 要写的字符串；NULL 当空串写
 * @return int 0 成功、-1 失败
 * @note 写 HKLM 需要进程已提权，否则 -1（先查 cg_reg_is_admin()）。
 */
int   cg_reg_write_ex(const char *root, const char *subkey, const char *name, const char *value);
/**
 * @brief 枚举一个键的直接子键名（指定根），'\n' 分隔
 * @param root 同 cg_reg_read_ex
 * @param subkey 子键路径
 * @param out 输出缓冲，由调用方提供并保证至少 cap 字节可写
 * @param cap 输出缓冲容量（字节），必须 > 1
 * @return int 写入的字节数（不含结尾 \0）；参数不合法、键不存在或一行都装不下时返回 0
 * @note 边界语义与 cg_reg_subkeys 完全一致：键不存在是"没有"，不是错误；
 *       装不下宁可少列也不截断半行。
 */
int   cg_reg_subkeys_ex(const char *root, const char *subkey, char *out, int cap);
/**
 * @brief 删注册表字符串值（指定根；cg_reg_write_ex 的"恢复"半边）
 * @param root 同 cg_reg_read_ex
 * @param subkey 子键路径；NULL 直接失败
 * @param name 值名；NULL 直接失败；空串 "" 删默认值
 * @return int 0 = 调用之后这个值一定不存在（删掉了，或本来就「没有」—— 幂等）；
 *             -1 = 参数不合法、编码转换失败，或键在但删不掉
 * @note 恢复走「删」而不是「写空串」—— 空值在注册表里仍是一条**存在**的记录。
 */
int   cg_reg_del_ex(const char *root, const char *subkey, const char *name);
/**
 * @brief 删一整棵键（指定根，连子键带值，等效 reg delete /f）
 * @param root 同 cg_reg_read_ex
 * @param subkey 子键路径；NULL 或空串直接失败
 * @return int 0 = 调用之后这棵键一定不存在（删掉了，或本来就「没有」）；
 *             -1 = 参数不合法 / 编码失败 / 删不掉
 * @note **破坏性**：删之前自己先备份（示例里走 `reg.exe export`）。空串被挡掉是有意的：
 *       RegDeleteTreeW(HKEY_CURRENT_USER, NULL) 会删掉整个 HKCU 子树。
 */
int   cg_reg_del_key_ex(const char *root, const char *subkey);
/**
 * @brief 问一棵键在不在（指定根）
 * @param root 同 cg_reg_read_ex
 * @param subkey 子键路径；NULL 或空串返回 0
 * @return int 1 = 打得开（有读权限且存在）；0 = 不存在 / 参数不合法 / 打不开
 */
int   cg_reg_key_exists(const char *root, const char *subkey);
/**
 * @brief 当前进程是否已提权（管理员）
 * @return int 1 = 已提权（能改 HKLM 系统区）；0 = 没提权
 * @note 用来在改 HKLM 之前给出明确提示，而不是让注册表操作静默返回失败。
 */
int   cg_reg_is_admin(void);

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

/**
 * @brief 读环境变量（UTF-8，值可以是很长的字符串）
 * @param name 变量名；大小写不敏感（Windows 环境变量本就如此）
 * @return char * 堆串，用完 cg_free；没有这个变量返回 NULL
 * @note 走 GetEnvironmentVariableW + 内部 UTF-8 转换，**不用 getenv()** ——
 *       CRT 的窄接口给的是 ANSI/MBCS，中文用户名或路径（`C:\Users\张三`）会变成乱码，
 *       而本工程内部一律 UTF-8
 */
char *cg_env(const char *name);

/**
 * @brief 读环境变量当整数
 * @param name 变量名
 * @param fallback 没有这个变量 / 值不是合法整数（或溢出）时返回它
 * @return int 解析出来的整数
 * @note 例：cg_env_int("NUMBER_OF_PROCESSORS", 1)
 */
int cg_env_int(const char *name, int fallback);

/**
 * @brief 这台机器是不是 64 位 Windows
 * @return int 1 是、0 不是
 * @note **不随本进程的位数骗人**：32 位程序跑在 64 位系统上照样返回 1。
 *       环境变量 PROCESSOR_ARCHITECTURE 做不到这一点 —— 它给的是进程视角
 */
int cg_sys_is64(void);

/**
 * @brief 取操作系统可读名
 * @return char * 形如 "Windows 10 (10.0.19045)" 的堆串，用完 cg_free；失败返回 NULL
 * @note 日志头 / 工单信息用；走 ntdll 的 RtlGetVersion（GetProcAddress 取，不新增链接库）——
 *       GetVersionEx 会被 exe 清单里的兼容性声明骗，本工程没有那条声明，
 *       但别人复制这段代码时多半有
 */
char *cg_sys_version(void);

/**
 * @brief 取路径所在磁盘的剩余 / 总容量（字节）
 * @param path 盘符或盘上任意路径（`"C:\\"` 或 cg_app_dir() 都行）
 * @param free_bytes 剩余（可用）字节数出口；可传 NULL（只问其中一个）
 * @param total_bytes 总容量字节数出口；可传 NULL
 * @return int 1 成功、0 路径不存在或调不动（此时两个出口都不动）
 * @note 剩余量给的是**当前用户可用**的那个（写日志前问"还写得下吗"才准）
 */
int cg_disk_space(const char *path, unsigned long long *free_bytes, unsigned long long *total_bytes);

/**
 * @brief 按可执行文件名查进程是否在跑（大小写不敏感）
 * @param exe_name 可执行文件名，如 "explorer.exe"
 * @return int 1 在跑、0 没找到
 * @note 现场排查"设备厂商的服务 / 驱动在不在"用得上
 */
int cg_process_running(const char *exe_name);

/**
 * @brief 取本机 IPv4 地址（点分十进制，如 "192.168.1.20"）
 * @return char * 堆串，用完 cg_free；拿不到返回 NULL
 * @note 拿到的是**系统默认出口网卡**的地址：内部挑一条默认路由再问本地端地址，
 *       **不发任何数据包**、也不需要 DNS（离线工控机上也能用）。多网卡时未必是
 *       "连设备的那张"—— 要枚举网卡得用 iphlpapi 的 GetAdaptersInfo，那会改掉用户的
 *       链接命令，所以不做。实现在 cg_net.c（winsock2.h 必须在 windows.h 之前，
 *       那里已经处理好了）
 */
char *cg_local_ip(void);

/* ---- 无线状态（WiFi / 蓝牙） ----
 * IoT 上位机第一步不是发命令，是**先排除"我这边的无线状态不对"**：设备连不上时，
 * 要能一眼看到本机有没有无线网卡、连的是哪个 SSID、信号多少、目标 AP 在不在列表里、
 * 蓝牙开着没有、板子在不在已知道设备列表里（这些今天只能去 Windows 设置里翻）。
 *
 *   cg_wifi_info_t w;
 *   for (i = 0; i < cg_wifi_count(); i++)
 *       if (cg_wifi_info(i, &w) == 0)
 *           cg_log(CG_LOG_INFO, "%s：%s  信号 %d%%", w.description,
 *                  w.ssid[0] ? w.ssid : "(未连接)", w.signal);
 *
 *   cg_wifi_net_t nets[24]; int found = 0;
 *   if (cg_wifi_networks(nets, 24, &found) == 0)
 *       for (i = 0; i < found; i++) cg_log(CG_LOG_INFO, "  %s  %d%%%s",
 *                                         nets[i].ssid, nets[i].signal, nets[i].connected ? "（当前）" : "");
 *
 * **零新增链接依赖**：符号全部在运行期从 `wlanapi.dll` / `BluetoothApis.dll` 取
 * （实测用工程现有链接参数直接链会 undefined reference ⇒ 那会让所有既有用户项目的
 * 链接命令都得改）。两个 DLL 都是系统自带，取不到就如实返回"0 个 / -1"，不假装成功；
 * 原因写进日志（本机没网卡 / WLAN 服务没起 / 系统太老，是三种不同的说法）。
 * **不用初始化，也不持有可变状态**：WLAN 的客户端句柄框架自己**在进程内开一次并复用**
 * （原来每次调用都开-关，实测会让 `WlanCloseHandle` 间歇性崩在 rpcrt4 —— 位置随机、
 * 看日志尾部永远找不到病根，2026-09-27 用 gdb 抓到栈才对上 ✓）；调用方不必管句柄。
 */
#define CG_WIFI_SSID_MAX 33      /* 802.11 的 SSID 上限 32 字节 + 终止符 */

/** 一块无线网卡的状态。没连接时 ssid 为空、signal 为 -1（别的字段照给） */
typedef struct {
    char description[256];       /* 网卡描述，如 "Intel(R) Wi-Fi 6 AX201 160MHz" */
    int  state;                  /* = WLAN_INTERFACE_STATE 原值：0 未就绪 / 1 已连接 / 4 已断开 / 5 正在关联 … */
    char ssid[CG_WIFI_SSID_MAX]; /* 当前连接的 SSID；没连是空串 */
    int  signal;                 /* 信号质量 0..100；没连是 -1（不是 0 —— 0 是"信号极差"，两回事）*/
} cg_wifi_info_t;

/** 周边可见的一个无线网络（同名的多个 BSSID 已经合并，取信号最强的那个） */
typedef struct {
    char ssid[CG_WIFI_SSID_MAX];
    int  signal;                 /* 0..100 */
    int  secure;                 /* 1 = 要密码/加密 */
    int  connected;              /* 1 = 当前连的就是它 */
} cg_wifi_net_t;

/**
 * @brief 数本机有几块无线网卡
 * @return int 块数；0 = 本机没有无线网卡，或 WLAN 服务没起（原因在日志里）
 */
int cg_wifi_count(void);
/**
 * @brief 取第 idx 块无线网卡的状态
 * @param idx 网卡下标，0 起
 * @param out 状态出口；**进来先清零并把 signal 置 -1**（失败时也如此），成功才填 description / state / ssid / signal
 * @return int 0 成功；-1 没有这么多块 / wlanapi 取不到
 */
int cg_wifi_info(int idx, cg_wifi_info_t *out);
/**
 * @brief 让系统做一次无线扫描（**异步**：本函数不等待）
 * @return int 0 已提交；-1 没有无线网卡 / 取不到 / 这块不支持（老系统没有 WlanScan）
 * @note 几秒后结果自己出现在 cg_wifi_networks() 里
 * @note 2026-09-27 更正：早先这里写着"`WlanScan` 实测返回 87（参数无效），已撤掉" ——
 *       真相是**本框架自己的 typedef 漏了参数**（见 cg_kit_wireless.c 里 `p_wlan_scan_t` 的注释），
 *       不是 Windows 的行为。接口已加回，实测提交成功
 */
int cg_wifi_scan(void);
/**
 * @brief 取周边可见网络列表（同一块网卡；**同名的多个 BSSID 已合并**，取信号最强的）
 * @param out 结果数组
 * @param cap out 能放几个
 * @param count 实际写进 out 的条数出口（同名 SSID 已合并，最多 cap；放不下的丢掉、不计数）
 * @return int 0 成功；-1 没有无线网卡或取不到
 * @note 数据来自系统最近一次扫描的结果 ⇒ **要立刻刷新就先调 cg_wifi_scan()**（几秒后再读）；
 *       不调也行，Windows 自己会定期重扫
 */
int cg_wifi_networks(cg_wifi_net_t *out, int cap, int *count);

/** 本机蓝牙无线电 */
typedef struct {
    char name[256];              /* 无线电名字，如 "Intel(R) Wireless Bluetooth(R)" */
    char address[18];            /* 本机蓝牙地址 "AA:BB:CC:DD:EE:FF" */
} cg_ble_radio_t;

/** 一个"已知设备"（已配对 / 记得过的；**不做主动搜索** —— 那要几十秒且属于"发现"不是"状态"） */
typedef struct {
    char name[248];              /* 设备名（可能为空：没取到名字的设备） */
    char address[18];            /* 设备地址，与 Windows 设置里显示的一致 */
    int  connected;              /* 1 = 当前连着 */
    int  remembered;             /* 1 = 配对过 / 记得过 */
} cg_ble_device_t;

/* ---- BLE 基元：地址 / UUID / 属性 ----
 * 这一段**不需要蓝牙外设**就能用，而且正是写 BLE 工具最容易踩的三处坑：
 *   - **地址字节序**：对外统一用**一个 64 位整数**（`0xAABBCCDDEEFF` 就是 AA:BB:CC:DD:EE:FF），
 *     与 `BLUETOOTH_ADDRESS.rgBytes` 的换算由框架负责 —— 那个数组是**小端**，
 *     摆错一次就永远连不上，而报错只会说"设备没找到"；
 *   - **UUID 的两种写法**：设备手册写 "FFF1"、协议栈写
 *     "0000fff1-0000-1000-8000-00805f9b34fb" —— `cg_ble_uuid_equal()` 认为它们相等；
 *   - **属性位**：能不能读 / 写 / 订阅全在那一个 properties 字里，`cg_ble_attr_flags()` 解出来。
 * **连接与读写不在这一版**：GATT 的入口要一个设备句柄，而 `bluetoothleapis.h` 里**没有**
 * 任何获取它的函数（纯 C 得靠 SetupAPI 枚举设备接口 + CreateFile，而那必须有真外设才枚举得到）
 * —— 等有板子时再做，见 .codebuddy/plans/wireless-ble.md。
 */
#define CG_BLE_ATTR_READ      0x01   /* 可读 */
#define CG_BLE_ATTR_WRITE_NR  0x02   /* 可写（无需响应） */
#define CG_BLE_ATTR_WRITE     0x04   /* 可写（要响应） */
#define CG_BLE_ATTR_NOTIFY    0x08   /* 通知 */
#define CG_BLE_ATTR_INDICATE  0x10   /* 指示（要确认的通知） */

/**
 * @brief 蓝牙地址 -> 文本（与 Windows 设置里一致：大写、冒号分隔）
 * @param addr 地址，一个 64 位整数（0xAABBCCDDEEFF 就是 AA:BB:CC:DD:EE:FF）
 * @param out 输出缓冲
 * @param out_size out 的字节数，需要 18 字节
 * @return void 无返回值（放不下就不写）
 */
void cg_ble_addr_str(unsigned long long addr, char *out, int out_size);
/**
 * @brief 文本 -> 蓝牙地址
 * @param text 地址文本；分隔符随便（冒号 / 连字符 / 空格 / 不写），大小写都认
 * @param out 地址出口；成功才写
 * @return int 0 成功；-1 格式不对
 * @note **必须正好 6 字节** —— 少一位算写错，不"尽量猜"（猜错了表现为"设备找不到"，
 *       比当场报错难查得多）
 */
int  cg_ble_addr_from_str(const char *text, unsigned long long *out);

/**
 * @brief 16 位短 UUID -> 标准基写法的文本
 * @param uuid16 16 位短 UUID
 * @param out 输出缓冲
 * @param out_size out 的字节数，需要 36 + 1 字节
 * @return int 写入长度（36）；放不下返回 -1
 * @note 输出**小写** —— 蓝牙文档与 Windows 都这么写
 */
int cg_ble_uuid16_str(unsigned short uuid16, char *out, int out_size);
/**
 * @brief 文本 -> 16 位短 UUID
 * @param text 认 "fff1" / "0xfff1" / "0000fff1-0000-1000-8000-00805f9b34fb"（带不带大括号都行）
 * @param out16 结果出口；成功才写
 * @return int 0 成功；-1 认不出来
 * @note **不是标准基形态的 128 位 UUID 返回 -1** —— 那不是 16 位能表达的
 */
int cg_ble_uuid16_parse(const char *text, unsigned short *out16);
/**
 * @brief 两个 UUID 是不是同一个
 * @param a UUID 文本（16 位短写法或 128 位长写法）
 * @param b 另一个 UUID 文本
 * @return int 1 相等、0 不等或认不出来
 * @note 能当 16 位解的两边按 16 位比 —— 所以 "fff1" 与它那个长写法**相等**
 *       （现场最常见的用法：拿手册上的短 UUID 去比对设备报出来的长 UUID）
 */
int cg_ble_uuid_equal(const char *a, const char *b);
/**
 * @brief 把特征的 properties 字解成能力位
 * @param properties GATT 特征的 properties 原值
 * @return int CG_BLE_ATTR_* 的按位或（位定义与 GATT 规范一致）
 */
int cg_ble_attr_flags(unsigned properties);
/**
 * @brief 在已知设备里按**地址**或**名字**找
 * @param addr_or_name 地址文本或设备名
 * @param out 找到的设备；成功才写
 * @return int 0 找到、-1 没找到
 * @note 能解析成地址就按地址精确比，否则按名字做不区分大小写的**子串**匹配
 *       （现场记的名字常常只是设备名的一段）
 */
int cg_ble_find_device(const char *addr_or_name, cg_ble_device_t *out);

/**
 * @brief 数有几个蓝牙无线电
 * @return int 个数；0 = 没有蓝牙或没开（蓝牙关了也算 0）
 */
int cg_ble_radio_count(void);
/**
 * @brief 取第 idx 个蓝牙无线电
 * @param idx 下标，0 起
 * @param out 结果出口；成功才写
 * @return int 0 成功；-1 越界 / 取不到
 */
int cg_ble_radio_info(int idx, cg_ble_radio_t *out);
/**
 * @brief 数有几个已知蓝牙设备
 * @return int 个数（已配对 / 记得过的；**不做主动搜索**）
 */
int cg_ble_device_count(void);
/**
 * @brief 取第 idx 个已知蓝牙设备
 * @param idx 下标，0 起
 * @param out 结果出口；成功才写
 * @return int 0 成功；-1 越界 / 取不到
 */
int cg_ble_device_info(int idx, cg_ble_device_t *out);

/* ------------------------------------------------------------ 动态扩展 */

/**
 * @brief 运行时用代码追加控件（不进 .cgui）
 * @param app 应用
 * @param parent 父控件；NULL 表示挂在窗口上
 * @param type 控件类型名
 * @param id 控件 id（也参与事件名查找，所以可以先在 .cgui 里写好事件名再动态造控件）
 * @param x 初始左边界（逻辑像素）
 * @param y 初始上边界
 * @param w 初始宽度
 * @param h 初始高度
 * @param text 初始文字；可为 NULL
 * @return cg_widget_t* 新控件；失败返回 NULL
 */
cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent,
                           const char *type, const char *id,
                           int x, int y, int w, int h, const char *text);

/**
 * @brief 直接在 C 里给控件绑回调（覆盖 .cgui 里的名字绑定）
 * @param w 控件
 * @param ev 事件类型
 * @param fn 回调函数
 * @return int 0 成功、非 0 失败
 */
int cg_on(cg_widget_t *w, cg_event_t ev, cg_event_fn fn);
/**
 * @brief 按 .cgui 里写的事件回调名绑定
 * @param w 控件
 * @param ev 事件类型
 * @param callback_name C 侧 CG_BIND 表里登记的名字
 * @return int 0 成功、非 0 失败（名字没登记会失败并写警告）
 */
int cg_on_named(cg_widget_t *w, cg_event_t ev, const char *callback_name);

/**
 * @brief 一次性定时器：延迟若干毫秒后在消息循环里执行一件事
 * @return void 无返回值
 * @note 在回调内部「稍后执行」，避免在事件里立刻销毁控件
 */
typedef struct cg_timer cg_timer_t;
/**
 * @brief 建一个一次性定时器，delay_ms 毫秒后在消息循环里调 fn(user)
 * @param app 所属应用（tick 时只触发同一 app 的定时器）
 * @param delay_ms 延迟毫秒数，负数按 0 处理
 * @param fn 到点要执行的函数；置 NULL 等价于取消
 * @param user 传给 fn 的业务数据
 * @return cg_timer_t* 定时器句柄（可传给 cg_kill_timer）；失败返回 NULL
 */
cg_timer_t  *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user);
/**
 * @brief 取消一个定时器
 * @param t cg_set_timer 返回的句柄；NULL 安全
 * @return void 无返回值（已被取消的句柄绝不再回调）
 */
void         cg_kill_timer(cg_timer_t *t);

#ifdef __cplusplus
}
#endif
#endif /* CSTACKGUI_H */
