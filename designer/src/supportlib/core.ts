/* supportlib/core.ts -- 「运行时核心支持库」页签的索引数据
 *
 * 数据唯一真相是 runtime/include/cstackgui.h —— 本文件只做分组与中文说明。
 * 改完跑 node tools\check-support.mjs 对齐头文件（表里写了不存在的函数会红）。
 *
 * 2026-10-31 从 designer/src/supportlib.ts（1757 行）按顶层库拆出：
 * 单文件 131KB 时定位一条命令要滚很久，而三个库之间没有任何耦合。
 */
import { c } from "./types.js";
import type { SupLib } from "./types.js";

export const CORE_LIB: SupLib = {
    key: "core",
    label: "运行时核心支持库",
    hint: "启动、事件、窗口、托盘、定时器、动态控件",
    groups: [
      {
        label: "程序启动",
        cmds: [
          c("cg_app_run", "启动程序", "int cg_app_run(const cg_app_desc_t *desc)", "建窗口 + 进消息循环，main() 里就这么一句", {
            sample: `int main(void) {
    cg_app_desc_t d = {0};
    d.cgui_path = "myapp.cgui";
    d.bindings  = k_bindings;
    return cg_app_run(&d);
}`,
          }),
          c("cg_app_selftest", "自检（不开窗）", "int cg_app_selftest(const cg_app_desc_t *desc)", "解析界面 + 合成一遍事件，返回 0 = 回调名都登记了", {
            sample: `/* 命令行加 --selftest 时走这条：CI 里就能发现「事件名写错」 */
if (argc > 1 && strcmp(argv[1], "--selftest") == 0) return cg_app_selftest(&d);`,
          }),
        ],
      },
      {
        label: "日志",
        cmds: [
          c("cg_log", "写日志", "void cg_log(int level, const char *fmt, ...)", "往日志文件补一行；GUI 版没有控制台，这是用户报障时唯一的线索", {
            params: [
              "level：CG_LOG_INFO / CG_LOG_WARN / CG_LOG_ERROR",
              "fmt, ...：和 printf 一样（支持中文）",
            ],
            sample: `cg_log(CG_LOG_INFO, "保存到 %s（%d 字节）", path, n);
if (!ok) cg_log(CG_LOG_ERROR, "写文件失败：%s", path);`,
          }),
          c("cg_logm", "写日志（带模块标签）", "void cg_logm(int level, const char *mod, const char *fmt, ...)", "和 cg_log 同一条落地路径，行里多一个 [模块] —— 几十万行的现场日志按子系统一键 grep，不用逐行读文案猜是谁写的", {
            params: [
              "level：CG_LOG_INFO / CG_LOG_WARN / CG_LOG_ERROR",
              "mod：短 ASCII 模块名（\"serial\" / \"net\" / \"gif\"…）；NULL 或空串 = 不标",
              "fmt, ...：和 printf 一样（支持中文）",
            ],
            sample: `cg_logm(CG_LOG_ERROR, "serial", "打开 %s 失败：%s", port, cg_serial_last_error(ser));
cg_logm(CG_LOG_INFO, "net", "收到 %d 字节", n);`,
          }),
          c("cg_log_set_level", "设日志等级", "void cg_log_set_level(int level)", "只记 >= 该等级的条目（默认 CG_LOG_INFO，全记）"),
        ],
      },
      {
        label: "事件",
        cmds: [
          c(
            "cg_event_t",
            "事件类型表",
            "typedef enum { CG_EVENT_CLICK, CG_EVENT_CHANGE, ... } cg_event_t;",
            "回调第二个参数 ev 就是它；一个函数绑多个事件时靠它分支",
            {
              sample: `/* 控件事件：click dblclick change submit select release expand collapse
              tick check gotfocus lostfocus
   窗口事件：ready close destroy move size activate deactivate firstactivate
              idle tray show hide closequery mousedown mouseup rdown rup
              mousemove keydown keyup char wheel */
if (ev == CG_EVENT_CLICK) { /* ... */ }`,
            },
          ),
          c("cg_event_name", "事件名", "const char *cg_event_name(cg_event_t ev)", "把 ev 转成 .cgui 里那种名字（\"click\"），打日志用"),
          c("cg_event_from_name", "名字反查事件", "int cg_event_from_name(const char *name)", "名字转枚举，不认识返回 -1"),
          c("cg_event_x", "鼠标 X 坐标", "int cg_event_x(const cg_widget_t *self)", "最近一次事件的鼠标 X（相对窗口客户区，逻辑像素）", {
            sample: `char buf[64];
snprintf(buf, sizeof(buf), "点在这里 (%d, %d)", cg_event_x(self), cg_event_y(self));
cg_widget_set_text(cg_find(cg_app_of(self), "lab_tip"), buf);`,
          }),
          c("cg_event_y", "鼠标 Y 坐标", "int cg_event_y(const cg_widget_t *self)", "最近一次事件的鼠标 Y"),
          c("cg_event_keys", "功能键状态", "int cg_event_keys(const cg_widget_t *self)", "1=Ctrl 2=Shift 4=Alt，可位或"),
          c("cg_event_key", "按键代码", "int cg_event_key(const cg_widget_t *self)", "keydown/keyup 的虚拟键码，char 是字符码"),
          c("cg_event_delta", "滚轮量", "int cg_event_delta(const cg_widget_t *self)", "滚轮滚动量，正数向上"),
          c("cg_tray_action", "托盘动作", "int cg_tray_action(const cg_widget_t *self)", "托盘事件里问是哪种动作：1=单击 2=双击 3=右键", {
            sample: `int a = cg_tray_action(self);
if (a == 1) cg_win_show(cg_app_of(self), 1);`,
          }),
        ],
      },
      {
        label: "动作 / 快捷键",
        cmds: [
          c(
            "cg_action_invoke",
            "触发动作",
            "int cg_action_invoke(cg_app_t *app, const char *name)",
            "按名字触发一个动作（.cgui 顶层 actions 里的键）；快捷键、菜单项、控件点击最终都走到这里",
            {
              params: [
                "app：应用（cg_app_of(self)）",
                "name：动作名，例如 \"save\"",
              ],
              ret: "1 = 真的调用了回调；0 = 没这个动作 / 被禁用 / handler 没登记",
              sample: `/* 例：某个按钮的 click 里手动触发「保存」动作 */
cg_action_invoke(cg_app_of(self), "save");`,
            },
          ),
          c(
            "cg_action_set_enabled",
            "启用 / 禁用一个动作",
            "void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled)",
            "一处调用就把所有引用它的菜单项和控件一起变灰 / 恢复（动作不存在时是空操作）",
            {
              sample: `/* 没有选中内容时：「删除」这个动作整体灰掉，菜单项和按钮一起 */
cg_action_set_enabled(cg_app_of(self), "delete", 0);`,
            },
          ),
          c(
            "cg_action_is_enabled",
            "动作是否启用",
            "int cg_action_is_enabled(const cg_app_t *app, const char *name)",
            "查动作的启用状态；动作不存在返回 0",
          ),
          c(
            "cg_action_set_checked",
            "勾选 / 取消勾选动作",
            "void cg_action_set_checked(cg_app_t *app, const char *name, int checked)",
            "勾选状态同步给所有引用它的菜单项（打勾 / 去掉）；菜单项自己不用再维护状态",
            {
              sample: `/* 比如「显示网格」这种开关型菜单项 */
cg_action_set_checked(cg_app_of(self), "grid", !cg_action_is_checked(cg_app_of(self), "grid"));`,
            },
          ),
          c(
            "cg_action_is_checked",
            "动作是否勾选",
            "int cg_action_is_checked(const cg_app_t *app, const char *name)",
            "查动作的勾选状态；动作不存在返回 0",
          ),
          c(
            "cg_action_name",
            "当前动作名",
            "const char *cg_action_name(const cg_widget_t *self)",
            "在动作的回调里问「这次是哪个动作被激活」；一个函数挂多个动作时用它分支",
            {
              sample: `void on_commands(cg_widget_t *self, cg_event_t ev, void *user) {
    const char *name = cg_action_name(self);   /* "save" / "grid" ... */
    if (strcmp(name, "save") == 0) { /* 保存 */ }
}`,
            },
          ),
          c(
            "cg_action_count",
            "动作个数",
            "int cg_action_count(const cg_app_t *app)",
            "文档里一共有多少个动作（调试与自检用）",
          ),
          c(
            "cg_action_at",
            "第 i 个动作名",
            "const char *cg_action_at(const cg_app_t *app, int i)",
            "按顺序取动作名，配合 cg_action_count 遍历；参数越界返回 NULL",
            {
              sample: `for (int i = 0; i < cg_action_count(app); i += 1) {
    cg_log(CG_LOG_INFO, "动作 %s", cg_action_at(app, i));
}`,
            },
          ),
          c(
            "cg_action_accel",
            "动作的快捷键",
            "const char *cg_action_accel(const cg_app_t *app, const char *name)",
            "取规范化后的快捷键显示串（如 \"Ctrl+S\"）；没写或动作不存在返回空串",
          ),
          c(
            "cg_hotkey_set",
            "注册全局热键",
            "int cg_hotkey_set(cg_app_t *app, const char *combo, const char *action)",
            "按下组合键 = 触发同名动作；**后台 / 托盘里也按得到**（与动作 accel 的区别：accel 只在本窗口有焦点时生效）。组合键被别的程序占用会注册失败，业务要留按钮 / 托盘兜底",
            {
              params: [
                "combo：\"Ctrl+Alt+A\" 这类写法（与动作 accel 相同）；传 NULL = 解绑全部",
                "action：.cgui 顶层 actions 里的动作名",
              ],
              ret: "1 = 成功；0 = 失败（没窗口 / 组合键看不懂 / 动作不存在 / 键被占用，原因写日志）",
              sample: `/* 就绪钩子里注册；失败只写日志，面板按钮是兜底入口 */\nif (!cg_hotkey_set(app, "Ctrl+Alt+A", "shot"))\n    cg_log(CG_LOG_WARN, "热键被占用，用按钮触发");`,
            },
          ),
        ],
      },
      {
        label: "应用与窗口",
        cmds: [
          c("cg_app_of", "取所属应用", "cg_app_t *cg_app_of(const cg_widget_t *w)", "从任意控件拿到 app，回调里第一步基本都写它", {
            sample: `cg_app_t *app = cg_app_of(self);`,
          }),
          c("cg_find", "按 id 找控件", "cg_widget_t *cg_find(cg_app_t *app, const char *id)", "用画布「属性」里填的 id 找别的控件", {
            params: ["app：应用（cg_app_of(self)）", "id：控件 id，例如 \"lab_status\""],
            ret: "找到的控件；没找到返回 NULL（并在 stderr 打警告）——用之前先判空",
            sample: `cg_widget_t *lab = cg_find(cg_app_of(self), "lab_status");
if (lab) cg_widget_set_text(lab, "已处理");`,
          }),
          c("cg_quit", "退出程序", "void cg_quit(cg_app_t *app)", "结束消息循环，进程随后退出"),
          c("cg_cancel_close", "拦下这次关闭", "void cg_cancel_close(cg_app_t *app)", "只在窗口 close 回调里有效：这次不关了", {
            sample: `static void on_close(cg_widget_t *self, cg_event_t ev, void *user) {
    if (有没保存的内容) cg_cancel_close(cg_app_of(self));
}`,
          }),
          c("cg_set_title", "改窗口标题", "void cg_set_title(cg_app_t *app, const char *title)", "运行时换标题栏文字"),
          c("cg_app_has_window", "有没有真窗口", "int cg_app_has_window(const cg_app_t *app)", "--selftest 不开窗时返回 0，弹对话框之前先问一句"),
          c("cg_dump_doc", "打印控件树", "void cg_dump_doc(cg_app_t *app)", "把 .cgui 解析结果打到 stdout，排查界面用"),
          c("cg_win_set_pos", "移动窗口", "void cg_win_set_pos(cg_app_t *app, int x, int y)", "左上角屏幕坐标"),
          c("cg_win_set_size", "改窗口尺寸", "void cg_win_set_size(cg_app_t *app, int width, int height)", "客户区尺寸，逻辑像素（和 .cgui 的 width/height 同单位）"),
          c("cg_win_center", "窗口居中", "void cg_win_center(cg_app_t *app)", "按当前显示器工作区居中"),
          c("cg_win_show", "显示/隐藏窗口", "void cg_win_show(cg_app_t *app, int show)", "show=1 显示，0 隐藏（配合 app.visible=false 做托盘程序）"),
          c("cg_win_set_state", "最小化/最大化", "void cg_win_set_state(cg_app_t *app, const char *cmd)", "cmd 传 \"min\" / \"max\" / \"normal\""),
          c("cg_win_set_topmost", "总在最前", "void cg_win_set_topmost(cg_app_t *app, int on)", "1 置顶，0 取消"),
          c("cg_win_set_icon", "换窗口图标", "void cg_win_set_icon(cg_app_t *app, const char *ico_path)", "传 .ico 路径；NULL 还原自带图标"),
          c("cg_drop_count", "拖进来几个文件", "int cg_drop_count(cg_app_t *app)",
            "在 `app.events.drop` 的回调里用；不在回调里调用返回 0。拖放用的是 Windows 的 WM_DROPFILES（不用 OLE 那套），文件拖进窗口就会派发 drop", {
              ret: "本次拖进来的文件个数",
              sample: `/* .cgui: { "app": { "events": { "drop": "on_files_drop" } } } */\nvoid on_files_drop(cg_widget_t *self, cg_event_t ev, void *user) {\n    cg_app_t *app = cg_app_of(self);\n    int n = cg_drop_count(app);\n    for (int i = 0; i < n; i++) {\n        char *p = cg_drop_file(app, i);   /* 堆串，用完 cg_free */\n        cg_log(CG_LOG_INFO, "拖进来：%s", p);\n        cg_free(p);\n    }\n}`,
            }),
          c("cg_drop_file", "取第 i 个拖入文件的路径", "char *cg_drop_file(cg_app_t *app, int index)",
            "返回 UTF-8 堆串（用完 cg_free）；越界返回 NULL。**路径只在 drop 回调期间有效**（回调返回后清单就清掉了），要留着就在回调里拷走", {
              ret: "路径（堆串）；越界 / 不在回调里返回 NULL",
            }),
          c("cg_win_screenshot", "把整个窗口存成 PNG", "int cg_win_screenshot(cg_app_t *app, const char *path)",
            "工单取证用：走 PrintWindow，所以窗口**被挡住、不在前台**也拿得到正确内容；少数控件不响应时退回抓屏并在日志里说明。GDI+ 起不来的机器返回 0", {
              params: ["path：输出路径（UTF-8，扩展名随意，编码固定 PNG）", "整个窗口，含标题栏"],
              ret: "1 = 成功；0 = 失败（原因写进日志）",
              sample: `/* 现场出问题时一键取证：存到 exe 同目录，路径贴进工单 */\nchar *p = cg_path_join(cg_app_dir(), "screenshot.png");\nif (cg_win_screenshot(app, p)) cg_status_set(sb, 0, p);\ncg_free(p);`,
            }),
          c("cg_widget_screenshot", "把某个控件存成 PNG", "int cg_widget_screenshot(cg_widget_t *w, const char *path)",
            "「这个框里到底显示成什么样」取证用；日期时间这类两个原生窗口拼的控件存的是主窗口那个", {
              ret: "1 = 成功；0 = 失败（原因写进日志）",
            }),
          c("cg_gif_open", "打开一个 GIF 准备写", "cg_gif_t *cg_gif_open(const char *path, int w, int h)",
            "把一串 24 位 RGB 帧写成动画 GIF（GIF89a、永远循环）。**自己写的编码器**：LZW 压缩 + 固定 256 色调色板 + 脏矩形优化（只编码变化的包围盒），零新增链接依赖、可单测。已有像素缓冲时用这一族", {
              params: ["path：输出路径（UTF-8；目录须已存在）", "w / h：画布尺寸（1..65535）"],
              ret: "编码器；失败返回 NULL",
            }),
          c("cg_gif_add", "向 GIF 追加一帧", "int cg_gif_add(cg_gif_t *g, const void *rgb, int stride, int delay_ms)",
            "rgb 是 w*h 个像素、每像素 R,G,B 三字节（自上而下）。**只编码变化的那一块**：框内没变的像素走透明索引让上一帧透出来；整帧没变则跳过（不占地方）", {
              params: ["stride：每行字节数；<= 0 按 w*3 算", "delay_ms：这一帧的延时"],
              ret: "1 = 已写入；2 = 整帧没变、跳过；0 = 失败",
            }),
          c("cg_gif_close", "收尾并关掉 GIF", "int cg_gif_close(cg_gif_t *g)",
            "写 trailer + fclose + 释放。**一帧都没写时算失败并删掉文件**（空 GIF 没用）", {
              ret: "1 = 成功；0 = 失败",
            }),
          c("cg_gif_abort", "放弃这次 GIF 编码", "void cg_gif_abort(cg_gif_t *g)",
            "关文件、删掉半成品、释放；NULL 时什么都不做"),
          c("cg_gif_rec_start", "开始录一块屏幕区域（GIF 录屏）", "int cg_gif_rec_start(cg_app_t *app, int x, int y, int w, int h, int fps)",
            "定时按 fps 抓屏、逐帧喂给 GIF 编码器；先写在用户数据目录的临时文件里。一个进程同时只录一条。**不弹任何界面**，进度用 cg_gif_rec_info 自己显示", {
              params: ["x / y：区域左上角（逻辑像素，可为负 —— 副屏）", "w / h：区域尺寸", "fps：名义帧率（1..30，超出夹进去）"],
              ret: "1 = 成功；0 = 失败（没窗口 / 已在录 / 尺寸非法 / 打不开临时文件）",
              sample: `/* 框选结束 -> 开始录；窗口别盖住选区，否则录到的是自己的窗口底 */\nif (!cg_gif_rec_start(app, dx, dy, w, h, 10))\n    cg_log(CG_LOG_WARN, "录屏起不来，回面板");`,
            }),
          c("cg_gif_rec_stop", "停止录屏", "int cg_gif_rec_stop(cg_app_t *app, const char *path)",
            "给路径就落盘（先 fclose 再改名，顺序反了会丢尾巴 / 被系统拒绝），给 NULL 就丢弃。录制期间**绝不能**让窗口盖住被录的区域", {
              params: ["path：成品路径（UTF-8）；NULL = 丢弃"],
              ret: "1 = 成功；0 = 失败（没在录 / 收尾出错 / 一帧都没录到）",
            }),
          c("cg_gif_rec_info", "查录屏进度", "void cg_gif_rec_info(const cg_app_t *app, int *frames, int *elapsed_ms)",
            "调用方拿它刷新工具条上的计时 / 帧数；没在录时两个出参都是 0", {
              params: ["frames：出参，已写帧数；可为 NULL", "elapsed_ms：出参，已录毫秒；可为 NULL"],
            }),
          c("cg_screen_size", "虚拟桌面尺寸", "int cg_screen_size(const cg_app_t *app, int *w, int *h)",
            "逻辑像素；口径是**整个虚拟桌面**（多屏拼起来那一整块），副屏区域同样算在内。区域截图铺满全屏前先问它", {
              params: ["w / h：出参，可为 NULL"],
              ret: "1 = 成功；0 = 失败",
            }),
          c("cg_screen_origin", "虚拟桌面原点", "int cg_screen_origin(const cg_app_t *app, int *x, int *y)",
            "逻辑像素；全屏 overlay 要摆的位置。**可能为负**（左边竖了屏时）；配 cg_win_set_pos（按物理像素收参）要过 cg_scale_px", {
              params: ["x / y：出参，可为 NULL"],
              ret: "1 = 成功；0 = 失败（与 cg_screen_size 对称，当前不失败）",
            }),
          c("cg_screen_capture", "屏幕区域截图", "int cg_screen_capture(const cg_app_t *app, int x, int y, int w, int h, const char *path)",
            "抓**屏幕**（不是窗口）一块存 PNG：坐标是逻辑像素、DPI 内部换算，越界自动夹进虚拟桌面（副屏区域也抓得到）", {
              params: ["x, y：区域左上角屏幕坐标（逻辑像素，可为负）", "w, h：区域宽高", "path：输出 PNG 路径（目录须已存在）"],
              ret: "1 = 成功；0 = 失败（原因写进日志）",
              sample: `/* 截图当画板背景 + 标注，成品抓画板 —— 完整链路见 examples\\shottool */\ncg_screen_capture(app, x, y, w, h, "cap.png");\ncg_widget_set_image(cv_shot, "cap.png");`,
            }),
          c("cg_scale_px", "逻辑像素换算物理像素", "int cg_scale_px(const cg_app_t *app, int logical)",
            "只为 cg_win_set_pos（按**物理**像素收参）这类接口换算；尺寸类接口收的本来就是逻辑像素，别多此一举"),
          c("cg_win_set_border", "运行时改窗口边框", "int cg_win_set_border(cg_app_t *app, const char *mode)",
            "mode：\"sizable\" / \"fixed\" / \"dialog\" / \"none\"；只换边框样式，位置与尺寸自己再设 —— 面板 ↔ 全屏 overlay 的开关", {
              params: ["mode：边框名（与 .cgui 的 app.border 同一套取值）"],
              ret: "1 = 成功；0 = 失败（没窗口 / mode 认不出来）",
            }),
          c("cg_win_focus", "把主窗口提到前台", "int cg_win_focus(cg_app_t *app)",
            "最小化时先还原再抢前台。截图工具这类\"弹 overlay 等着按键\"的程序必须调它：只 show 不抢前台，"
            + "键盘消息不进本进程（表现为\"按 Esc 没反应\"，点过画布又好了）；系统前台锁生效时会被拒绝（返回 0）", {
              ret: "1 = 成功；0 = 没有窗口 / 系统拒绝",
            }),
        ],
      },
      {
        label: "程序配置",
        cmds: [
          c("cg_app_info", "读程序信息", "const char *cg_app_info(const cg_app_t *app, const char *key)", "读画布「程序配置」里填的内容（编译进 exe 版本信息）", {
            params: [`key："product" "desc" "comments" "version" "build" "author" "company"
       "copyright" "address" "postcode" "phone" "fax" "email" "homepage"`],
            ret: "字符串；没填过的 key 返回 NULL",
            sample: `/* 「关于」对话框最简单的写法 */
cg_message_box(self, cg_app_info(cg_app_of(self), "version"), "版本", 0);`,
          }),
        ],
      },
      {
        label: "托盘",
        cmds: [
          c("cg_tray_set", "建/改托盘图标", "void cg_tray_set(cg_app_t *app, const char *ico_path, const char *tip)", "ico_path/tip 传 NULL 分别用窗口图标/窗口标题"),
          c("cg_tray_remove", "移除托盘图标", "void cg_tray_remove(cg_app_t *app)", "去掉任务栏通知区的小图标"),
          c("cg_tray_balloon", "气泡提示", "void cg_tray_balloon(cg_app_t *app, const char *tip_title, const char *text)", "在托盘图标上冒个气泡"),
        ],
      },
      {
        label: "时钟与定时器",
        cmds: [
          c("cg_set_timer", "延迟/定时执行", "cg_timer_t *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user)", "过 delay_ms 毫秒执行一次 fn（不是周期，要循环就再挂一次）", {
            params: ["delay_ms：毫秒", "fn：void fn(void *user)，不能是成员函数", "user：原样传给 fn 的数据"],
            ret: "定时器句柄，可用来 cg_kill_timer",
            sample: `static void later(void *user) {
    cg_widget_set_text((cg_widget_t *)user, "延迟到点了");
}
/* 按钮回调里：过 1 秒改标签 */
cg_set_timer(cg_app_of(self), 1000, later, cg_find(cg_app_of(self), "lab_status"));`,
          }),
          c("cg_kill_timer", "取消定时器", "void cg_kill_timer(cg_timer_t *t)", "还没到点就不执行了"),
          c("cg_widget_set_period", "设时钟周期", "void cg_widget_set_period(cg_widget_t *w, int period_ms)", "timer 控件的 tick 间隔（毫秒）；0 = 停止", {
            sample: `cg_widget_t *tm = cg_find(cg_app_of(self), "tm_clock");
cg_widget_set_period(tm, sw ? 500 : 0);   /* 开关控制时钟走不走 */`,
          }),
          c("cg_widget_period", "读时钟周期", "int cg_widget_period(const cg_widget_t *w)", "当前 tick 间隔，0 表示停着"),
        ],
      },
      {
        label: "动态扩展",
        cmds: [
          c("cg_widget_add", "运行时加控件", "cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent, const char *type, const char *id, int x, int y, int w, int h, const char *text)", "代码里现造控件（不进 .cgui）；parent=NULL 挂窗口上", {
            params: ["type：控件类型名（\"button\"/\"edit\"…，见画布控件面板）", "id：新控件 id，同样参与事件名查找", "x/y/w/h：逻辑像素；容器内相对容器客户区"],
            sample: `cg_widget_t *b = cg_widget_add(app, NULL, "button", "btn_dyn", 20, 200, 120, 40, "动态加的");
cg_on_named(b, CG_EVENT_CLICK, "on_dyn_click");`,
          }),
          c("cg_on", "给控件绑函数", "int cg_on(cg_widget_t *w, cg_event_t ev, cg_event_fn fn)", "直接给函数指针，不用在 .cgui 里写名字"),
          c("cg_on_named", "按名字绑函数", "int cg_on_named(cg_widget_t *w, cg_event_t ev, const char *callback_name)", "按 .cgui 里那套名字绑定（能复用 CG_BIND 表）"),
        ],
      },
      {
        label: "常量",
        cmds: [
          c("CG_WINDOW_ID", "窗口伪控件 id", "#define CG_WINDOW_ID \"__window\"", "窗口事件的\"控件\" id；要给 close 回调挂数据就 cg_find(app, CG_WINDOW_ID)", {
            sample: `cg_widget_set_user(cg_find(app, CG_WINDOW_ID), &g_ctx);`,
          }),
          c("CG_BIND", "绑定表写法", "#define CG_BIND(name, fn) { (name), (fn) }  /* 表尾 CG_BIND_END */", "事件名 → C 函数的登记表，main.c 里那张", {
            sample: `static const cg_binding_t k_bindings[] = {
    CG_BIND("on_ok_click",  on_ok_click),
    CG_BIND("on_name_change", on_name_change),
    CG_BIND_END
};`,
          }),
          c("CG_RGB", "颜色字面量", "#define CG_RGB(r, g, b)  /* 0xRRGGBB */", "颜色统一用 0xRRGGBB；也可以直接写 0x6750A4", {
            sample: `cg_widget_set_colors(w, CG_RGB(0x1D, 0x1B, 0x20), CG_RGB(0xEA, 0xDD, 0xFF));`,
          }),
          c("CG_COLOR_NONE", "不设置颜色", "#define CG_COLOR_NONE 0xFFFFFFFFu", "传这个值 = 该控件/窗口交回系统默认配色", {
            sample: `/* 只改字色，底色仍跟系统 */
cg_widget_set_colors(w, CG_RGB(0x67, 0x50, 0xA4), CG_COLOR_NONE);`,
          }),
          c("CG_VERSION_STRING", "运行时版本", "#define CG_VERSION_STRING \"0.1.0\"", "当前运行时库版本号，显示在「关于」里也行"),
        ],
      },
    ],
};
