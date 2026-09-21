/* supportlib.ts -- 画布「支持库」页签的数据：运行时公共 API 的中文索引
 *
 * 数据唯一真相是 runtime/include/cstackgui.h —— 这里只做三件事：
 *   1. 分组（库 → 分类 → [子分类] → 命令），让新手按用途找函数而不是背名字；
 *   2. 写中文一句话说明 + 调用示例（复制就能粘进 *_ui_events.c）；
 *   3. 提供搜索过滤。
 *
 * 改了 cstackgui.h 的公共 API，记得同步这张表；否则画布列出的函数编译时会找不到。
 */

export interface SupCmd {
  /** C 函数名（也是树的 key 与搜索主键） */
  fn: string;
  /** 中文短名，列表里当副标题 */
  name: string;
  /** 原型或宏定义，帮助卡片里展示 */
  decl: string;
  /** 一句话说明（新手看这个就够了） */
  brief: string;
  /** 参数逐条说明（可省） */
  params?: string[];
  /** 返回值说明（可省） */
  ret?: string;
  /** 复制用的调用代码（可省，缺省用 decl） */
  sample?: string;
}

export interface SupGroup {
  label: string;
  cmds: SupCmd[];
  /** 二级分类（如表格的「基本 / 行状态」、对话框的「信息框 / 打开文件」） */
  children?: SupGroup[];
}

export interface SupLib {
  key: string;
  label: string;
  hint: string;
  groups: SupGroup[];
}

/** 一行一条命令的简写构造器（保持数据表可读） */
const c = (fn: string, name: string, decl: string, brief: string, extra: Partial<SupCmd> = {}): SupCmd => ({
  fn,
  name,
  decl,
  brief,
  ...extra,
});

export const SUPPORT_LIBS: SupLib[] = [
  {
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
  },
  {
    key: "widget",
    label: "界面控件支持库",
    hint: "读写控件、列表/树/表格、菜单、画板、对话框",
    groups: [
      {
        label: "通用读取",
        cmds: [
          c("cg_widget_id", "取控件 id", "const char *cg_widget_id(const cg_widget_t *w)", "一个函数被多个控件共用时，靠它区分是谁触发的", {
            sample: `/* on_row_click 同时绑在三个按钮上 */
const char *who = cg_widget_id(self);
if (strcmp(who, "btn_add") == 0) { /* ... */ }`,
          }),
          c("cg_widget_type", "取控件类型", "const char *cg_widget_type(const cg_widget_t *w)", "返回 \"button\"/\"edit\"… 这类类型名"),
          c("cg_widget_text", "读文字", "const char *cg_widget_text(const cg_widget_t *w)", "当前显示/输入的文字", {
            sample: `const char *s = cg_widget_text(cg_find(cg_app_of(self), "edt_name"));`,
          }),
          c("cg_widget_x", "读 X", "int cg_widget_x(const cg_widget_t *w)", "相对父容器的逻辑像素"),
          c("cg_widget_y", "读 Y", "int cg_widget_y(const cg_widget_t *w)", "相对父容器的逻辑像素"),
          c("cg_widget_w", "读宽度", "int cg_widget_w(const cg_widget_t *w)", "逻辑像素"),
          c("cg_widget_h", "读高度", "int cg_widget_h(const cg_widget_t *w)", "逻辑像素"),
          c("cg_widget_visible", "读可见", "int cg_widget_visible(const cg_widget_t *w)", "1=显示，0=隐藏"),
          c("cg_widget_enabled", "读可用", "int cg_widget_enabled(const cg_widget_t *w)", "1=可交互，0=变灰点不动"),
          c("cg_widget_checked", "读勾选", "int cg_widget_checked(const cg_widget_t *w)", "checkbox / radio / switch 的勾选状态"),
          c("cg_widget_value", "读值", "int cg_widget_value(const cg_widget_t *w)", "滑块位置 / 列表框与下拉框选中项 / 标签页页码 / 表格选中行 / 工具栏按钮序号", {
            sample: `int page = cg_widget_value(cg_find(cg_app_of(self), "pages_main"));`,
          }),
          c("cg_widget_count", "控件总数", "int cg_widget_count(const cg_app_t *app)", "不含窗口伪控件，调试用"),
          c("cg_widget_get_user", "读挂载数据", "void *cg_widget_get_user(const cg_widget_t *w)", "拿回 cg_widget_set_user 挂上去的业务数据"),
        ],
      },
      {
        label: "通用修改",
        cmds: [
          c("cg_widget_set_text", "改文字", "void cg_widget_set_text(cg_widget_t *w, const char *text)", "标签/按钮/输入框都能改，立即生效", {
            sample: `cg_widget_set_text(cg_find(cg_app_of(self), "lab_status"), "已保存");`,
          }),
          c("cg_widget_set_rect", "改位置+尺寸", "void cg_widget_set_rect(cg_widget_t *w, int x, int y, int width, int height)", "一次改四个（逻辑像素）"),
          c("cg_widget_set_pos", "改位置", "void cg_widget_set_pos(cg_widget_t *w, int x, int y)", "相对父容器"),
          c("cg_widget_set_size", "改尺寸", "void cg_widget_set_size(cg_widget_t *w, int width, int height)", "逻辑像素"),
          c("cg_widget_set_visible", "显示/隐藏", "void cg_widget_set_visible(cg_widget_t *w, int visible)", "1 显示 0 隐藏", {
            sample: `cg_widget_set_visible(cg_find(cg_app_of(self), "prg_level"), busy);`,
          }),
          c("cg_widget_set_enabled", "启用/禁用", "void cg_widget_set_enabled(cg_widget_t *w, int enabled)", "0 = 变灰点不动（适合「提交中」防连点）"),
          c("cg_widget_set_checked", "设勾选", "void cg_widget_set_checked(cg_widget_t *w, int on)", "checkbox / radio / switch"),
          c("cg_widget_set_value", "设值", "void cg_widget_set_value(cg_widget_t *w, int value)", "进度条/滑块/列表框选中项等（会按 min/max 截断；颜色选择器别用这个）", {
            sample: `cg_widget_set_value(cg_find(cg_app_of(self), "prg_level"), 60);`,
          }),
          c("cg_widget_set_value_range", "设取值范围", "void cg_widget_set_value_range(cg_widget_t *w, int min, int max)", "滑块/进度条/spinner/评分/分页的取值范围"),
          c("cg_widget_set_colors", "改颜色", "void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg)", "前景/背景色，0xRRGGBB；bg 传 CG_COLOR_NONE 表示跟系统", {
            sample: `cg_widget_set_colors(w, CG_RGB(0x1D, 0x1B, 0x20), CG_RGB(0xEA, 0xDD, 0xFF));`,
          }),
          c("cg_widget_set_font", "改字体", "void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold)", "字号单位是磅，和画布一致"),
          c("cg_widget_set_hint", "设悬停提示", "void cg_widget_set_hint(cg_widget_t *w, const char *text)", "鼠标停上去的小提示；空串清除", {
            sample: `cg_widget_set_hint(cg_find(cg_app_of(self), "btn_save"), "保存到文件");`,
          }),
          c("cg_widget_hint", "读悬停提示", "const char *cg_widget_hint(const cg_widget_t *w)", "没设过返回 NULL"),
          c("cg_widget_set_user", "挂业务数据", "void cg_widget_set_user(cg_widget_t *w, void *user)", "把结构体/上下文挂到控件上，回调里用第三个参数 user 拿到", {
            sample: `/* on_ready 里挂一次 */
cg_widget_set_user(cg_find(app, "lst_log"), &g_ctx);`,
          }),
          c("cg_bind", "控件绑定", "int cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way)", "dst 的值/文字/勾选一直跟着 src 走，省掉手写同步回调（画布「属性」里也能配）", {
            sample: `/* 滑块拖动时旁边的标签显示数字（等价于 .cgui 里给标签写 bind.src） */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "lab_value"), 0);

/* 双向：滑块和微调框两个入口改同一个值 */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "spn_level"), 1);`,
          }),
          c("cg_splitter_pos", "读分隔条位置", "int cg_splitter_pos(const cg_widget_t *w)", "相对父容器的像素（竖条是 x，横条是 y）；配 cg_ini_write 存布局", {
            sample: `int pos = cg_splitter_pos(cg_find(app, "sp_main"));
cg_ini_write(cfg, "布局", "分隔条", cg_str_format("%d", pos));`,
          }),
          c("cg_splitter_set", "设分隔条位置", "void cg_splitter_set(cg_widget_t *w, int pos)", "两侧面板跟着重排（会被夹到最小 16px）；启动时恢复布局用", {
            sample: `/* on_ready 里恢复上次拖出来的比例 */
int pos = atoi(cg_ini_read(cfg, "布局", "分隔条", "320"));
cg_splitter_set(cg_find(app, "sp_main"), pos);`,
          }),
        ],
      },
      {
        label: "图表（chart）",
        cmds: [
          c("cg_chart_push", "推一个点", "void cg_chart_push(cg_widget_t *w, int series, int value)", "给某个系列追加一个点（满了丢最老的）；series 从 0 起", {
            sample: `/* 建议在时钟回调里按采样节奏推，不要另开线程碰控件 */
void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_chart_push(cg_find(app, "cht_temp"), 0, read_sensor());
}`,
          }),
          c("cg_chart_push_all", "一次推所有系列", "void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues)", "多通道采样最常用：一次给每个系列各一个点，只刷新一次界面", {
            sample: `int vals[2];
vals[0] = read_temp();
vals[1] = read_humi();
cg_chart_push_all(cg_find(app, "cht_env"), vals, 2);`,
          }),
          c("cg_chart_clear", "清空曲线", "void cg_chart_clear(cg_widget_t *w)", "点位归零，系列与范围保留"),
          c("cg_chart_add_series", "加一个系列", "int cg_chart_add_series(cg_widget_t *w, const char *name)", "运行时加曲线，返回系列下标（到上限返回 -1）", {
            sample: `cg_chart_add_series(cg_find(app, "cht_env"), "压力");`,
          }),
          c("cg_chart_series_count", "读系列数", "int cg_chart_series_count(const cg_widget_t *w)", "当前有几条曲线"),
          c("cg_chart_filled", "读已填点数", "int cg_chart_filled(const cg_widget_t *w, int series)", "这个系列已经攒了多少个点"),
          c("cg_chart_point", "读一个点", "int cg_chart_point(const cg_widget_t *w, int series, int index)", "下标 0 = 最老的点；越界返回 0"),
          c("cg_chart_set_range", "改 Y 轴范围", "void cg_chart_set_range(cg_widget_t *w, int min, int max)", "超范围的值按边界裁剪后画"),
        ],
      },
      {
        label: "终端 / 日志（terminal）",
        cmds: [
          c("cg_term_write", "写一行", "void cg_term_write(cg_widget_t *w, int dir, const void *data, int len)", "把收到/发出的数据灌进终端；方向决定行首标记与颜色（RX 绿 / TX 蓝 / SYS 灰）", {
            sample: `/* 串口/网络收到多少就灌多少（len 传 -1 表示按 C 字符串取长度） */
cg_term_write(cg_find(app, "term_log"), CG_TERM_RX, buf, n);

/* 发出去的也记一笔；数据里带 \\r\\n 会自动切成多行 */
cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, "AT+GMR\\r\\n", -1);`,
          }),
          c("cg_term_clear", "清空", "void cg_term_clear(cg_widget_t *w)", "清屏（日志区旁边常配一个「清空」按钮）"),
          c("cg_term_set_hex", "切 HEX / 文本", "void cg_term_set_hex(cg_widget_t *w, int on)", "1 = 十六进制（01 AB 20），0 = 文本；只影响**之后**写入的行", {
            sample: `/* HEX 开关：一个按钮切一下 */
cg_term_set_hex(term, !cg_term_hex(term));`,
          }),
          c("cg_term_hex", "读显示模式", "int cg_term_hex(const cg_widget_t *w)", "1 = 当前是 HEX 模式"),
          c("cg_term_set_timestamp", "时间戳开关", "void cg_term_set_timestamp(cg_widget_t *w, int on)", "每行前面带 [时:分:秒]（排查数据间隔很有用）"),
          c("cg_term_set_max_lines", "设行数上限", "void cg_term_set_max_lines(cg_widget_t *w, int lines)", "超了从最老的丢；传 0 恢复默认 500"),
          c("cg_term_count", "读行数", "int cg_term_count(const cg_widget_t *w)", "当前多少行（用来判断「到底收到东西没有」）"),
          c("cg_term_save", "存成文件", "int cg_term_save(cg_widget_t *w, const char *path)", "UTF-8 + CRLF，不含方向标记；成功返回 0", {
            sample: `cg_term_save(cg_find(app, "term_log"), "session.log");`,
          }),
        ],
      },
      {
        label: "状态灯 / 仪表盘（led / meter）",
        cmds: [
          c("cg_led_set", "设灯状态", "void cg_led_set(cg_widget_t *w, int state)", "0 灭 / 1 亮 / 2 闪（越界夹到 0..2）；状态只能由程序写，灯本身没有事件", {
            sample: `/* 连上就点亮、断了就灭 */
cg_led_set(cg_find(app, "led_link"), ok ? 1 : 0);

/* 2 = 一闪一闪；节奏在画布「闪烁周期」里配（缺省 500ms，最小 100） */
cg_led_set(cg_find(app, "led_busy"), 2);`,
          }),
          c("cg_led_state", "读灯状态", "int cg_led_state(const cg_widget_t *w)", "返回 0 灭 / 1 亮 / 2 闪"),
          c("cg_led_set_color", "改灯色", "void cg_led_set_color(cg_widget_t *w, unsigned rgb)", "亮起的颜色，0xRRGGBB（缺省绿 0x1B6E33）；画布「亮灯颜色」改的就是它", {
            sample: `cg_led_set_color(cg_find(app, "led_err"), 0xB3261E);   /* 报警用红 */`,
          }),
          c("cg_meter_set", "设仪表值", "void cg_meter_set(cg_widget_t *w, int value)", "超过量程按边界夹取（和滑块一样走 min/max）；只能由程序写", {
            sample: `/* 按采样节奏刷新，别另开线程碰控件 */
void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_meter_set(cg_find(app, "mtr_temp"), read_temp_c10() / 10);
}`,
          }),
          c("cg_meter_set_range", "设量程", "void cg_meter_set_range(cg_widget_t *w, int min, int max)", "改最小值 / 最大值；画布上就是「范围」那两格"),
        ],
      },
      {
        label: "列表框 / 下拉框",
        cmds: [
          c("cg_widget_list_add", "加一条", "int cg_widget_list_add(cg_widget_t *w, const char *item)", "往列表框/下拉框/标签页/状态栏追加一项，返回下标"),
          c("cg_widget_list_addf", "加一条（格式化）", "int cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...)", "日志类最常用：自动滚到最后一行", {
            sample: `static int n = 0;
cg_widget_list_addf(cg_find(cg_app_of(self), "lst_log"), "第 %d 次点击", ++n);`,
          }),
          c("cg_widget_list_clear", "清空", "void cg_widget_list_clear(cg_widget_t *w)", "把内容全删掉"),
          c("cg_widget_list_count", "条目数", "int cg_widget_list_count(const cg_widget_t *w)", "当前有多少条"),
          c("cg_widget_list_item", "读第 i 条", "const char *cg_widget_list_item(const cg_widget_t *w, int index)", "下标从 0 起；越界返回 NULL"),
          c("cg_widget_list_scroll_bottom", "滚到最后", "void cg_widget_list_scroll_bottom(cg_widget_t *w)", "日志追加后手动滚到底（addf 已自动滚）"),
        ],
      },
      {
        label: "状态栏",
        cmds: [
          c("cg_status_set", "改第 N 段", "void cg_status_set(cg_widget_t *w, int part, const char *text)", "分段下标从 0 起；等于段数时相当于追加", {
            sample: `cg_status_set(cg_find(cg_app_of(self), "sb_main"), 0, "就绪");`,
          }),
          c("cg_status_setf", "改第 N 段（格式化）", "void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...)", "像 printf 那样拼好再写，省一个临时缓冲"),
        ],
      },
      {
        label: "标签页",
        cmds: [
          c("cg_tabs_count", "页数", "int cg_tabs_count(const cg_widget_t *w)", "标签页一共有几页"),
          c("cg_tabs_select", "切到某页", "void cg_tabs_select(cg_widget_t *w, int index)", "程序主动切页（子控件随之显示/隐藏）"),
          c("cg_tabs_selected", "当前页", "int cg_tabs_selected(const cg_widget_t *w)", "从 0 起"),
        ],
      },
      {
        label: "树",
        cmds: [
          c("cg_tree_add", "加节点", "int cg_tree_add(cg_widget_t *w, int parent, const char *text)", "parent 传 -1 表示加在根上，返回节点下标", {
            sample: `int root = cg_tree_add(tree, -1, "项目");
cg_tree_add(tree, root, "runtime");
cg_tree_add(tree, root, "designer");`,
          }),
          c("cg_tree_addf", "加节点（格式化）", "int cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...)", "像 printf 那样拼好节点文字再加"),
          c("cg_tree_clear", "清空整棵树", "void cg_tree_clear(cg_widget_t *w)", "删掉所有节点"),
          c("cg_tree_count", "节点数", "int cg_tree_count(const cg_widget_t *w)", "含所有层级"),
          c("cg_tree_item_text", "读节点文字", "const char *cg_tree_item_text(const cg_widget_t *w, int item)", "item 是 cg_tree_add 返回的下标"),
          c("cg_tree_selected", "当前选中节点", "int cg_tree_selected(const cg_widget_t *w)", "没选中返回 -1", {
            sample: `static void on_tree_select(cg_widget_t *self, cg_event_t ev, void *user) {
    int item = cg_tree_selected(self);
    if (item < 0) return;
    cg_widget_set_text(cg_find(cg_app_of(self), "lab_tip"), cg_tree_selected_text(self));
}`,
          }),
          c("cg_tree_selected_text", "选中节点文字", "const char *cg_tree_selected_text(const cg_widget_t *w)", "等价于 item_text(selected)"),
          c("cg_tree_expand", "展开/折叠", "void cg_tree_expand(cg_widget_t *w, int item, int expand)", "expand=1 展开，0 折叠"),
        ],
      },
      {
        label: "表格",
        cmds: [],
        children: [
          {
            label: "基本",
            cmds: [
              c("cg_table_add_column", "加列", "int cg_table_add_column(cg_widget_t *w, const char *title, int width)", "width 传 0 表示按控件宽度均分"),
              c("cg_table_add_row", "加行", "int cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells)", "不足的列按空串处理，返回行号", {
                sample: `const char *cells[] = { "张三", "上海" };
cg_table_add_row(cg_find(cg_app_of(self), "tbl_data"), cells, 2);`,
              }),
              c("cg_table_clear", "清空表格", "void cg_table_clear(cg_widget_t *w)", "清行（列保留）"),
              c("cg_table_row_count", "行数", "int cg_table_row_count(const cg_widget_t *w)", "当前有多少行"),
              c("cg_table_cell", "读单元格", "const char *cg_table_cell(const cg_widget_t *w, int row, int col)", "行/列都从 0 起"),
              c("cg_table_selected", "当前选中行", "int cg_table_selected(const cg_widget_t *w)", "没选中返回 -1"),
            ],
          },
          {
            label: "行状态（多选 / 勾选 / 图标）",
            cmds: [
              c("cg_table_set_checked", "勾选某行", "void cg_table_set_checked(cg_widget_t *w, int row, int on)", "需要 .cgui 里 table 开了 check（画布属性里勾）", {
                sample: `for (int r = 0; r < cg_table_row_count(tbl); r++) cg_table_set_checked(tbl, r, 1);`,
              }),
              c("cg_table_checked", "读某行勾选", "int cg_table_checked(const cg_widget_t *w, int row)", "1=该行被勾选"),
              c("cg_table_set_row_value", "存行数值", "int cg_table_set_row_value(cg_widget_t *w, int row, int value)", "给每行挂一个业务整数（如数据库主键），界面不显示"),
              c("cg_table_row_value", "读行数值", "int cg_table_row_value(const cg_widget_t *w, int row)", "读回上面存的整数"),
              c("cg_table_set_row_icon", "设行图标", "void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path)", "png/jpg/bmp/gif 路径；NULL 清除"),
              c("cg_table_selected_count", "选中行数", "int cg_table_selected_count(const cg_widget_t *w)", "多选模式下才有意义"),
              c("cg_table_selected_row", "第 i 个选中行", "int cg_table_selected_row(const cg_widget_t *w, int index)", "配合 selected_count 遍历多选的行号"),
            ],
          },
        ],
      },
      {
        label: "图片",
        cmds: [
          c("cg_widget_set_image", "换图片", "void cg_widget_set_image(cg_widget_t *w, const char *path)", "image / iconbutton 都行（png/jpg/bmp/gif）；路径不存在显示占位框", {
            sample: `cg_widget_set_image(cg_find(cg_app_of(self), "img_logo"), "assets/logo.png");`,
          }),
          c("cg_widget_image", "读图片路径", "const char *cg_widget_image(const cg_widget_t *w)", "当前显示的图片文件路径"),
        ],
      },
      {
        label: "菜单",
        cmds: [
          c("cg_menu_last_id", "刚点的菜单项 id", "const char *cg_menu_last_id(const cg_widget_t *w)", "只能在自己的 click 回调里问；菜单项没写 id 返回 NULL", {
            sample: `static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user) {
    const char *id = cg_menu_last_id(self);
    if (id && strcmp(id, "m_quit") == 0) cg_quit(cg_app_of(self));
}`,
          }),
          c("cg_menu_last_text", "刚点的菜单项文字", "const char *cg_menu_last_text(const cg_widget_t *w)", "菜单项没写 id 时按文字判断"),
          c("cg_menu_count", "可点项个数", "int cg_menu_count(const cg_widget_t *w)", "不含分隔线与下拉项，调试用"),
        ],
      },
      {
        label: "画板",
        cmds: [
          c("cg_canvas_clear", "擦除画板", "void cg_canvas_clear(cg_widget_t *w)", "清掉之前画的所有图形"),
          c("cg_canvas_set_color", "设画笔颜色", "void cg_canvas_set_color(cg_widget_t *w, unsigned rgb)", "影响后续图形，0xRRGGBB"),
          c("cg_canvas_set_line_width", "设线宽", "void cg_canvas_set_line_width(cg_widget_t *w, int px)", "影响后续描边"),
          c("cg_canvas_line", "画线", "void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2)", "坐标相对控件左上角", {
            sample: `cg_canvas_clear(cv);
cg_canvas_set_color(cv, 0x6750A4);
cg_canvas_set_line_width(cv, 2);
cg_canvas_line(cv, 0, 0, 100, 80);`,
          }),
          c("cg_canvas_rect", "画矩形", "void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill)", "fill=0 描边，1 实心"),
          c("cg_canvas_ellipse", "画椭圆", "void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill)", "fill=0 描边，1 实心"),
          c("cg_canvas_text", "写文字", "void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text)", "在指定位置画一行文字"),
        ],
      },
      {
        label: "网页浏览",
        cmds: [
          c("cg_browser_navigate", "打开网页", "void cg_browser_navigate(cg_widget_t *w, const char *url)", "http/https 或本地路径；实验性（走系统 IE 内核）"),
        ],
      },
      {
        label: "对话框",
        cmds: [],
        children: [
          {
            label: "信息框",
            cmds: [
              c("cg_message_box", "弹消息框", "void cg_message_box(cg_widget_t *parent, const char *text, const char *title, int icon)", "只有一个「确定」钮；icon：0=信息 1=警告 2=错误 3=问号", {
                params: ["parent：用来定位的控件，传 self 即可", "text：正文", "title：标题（可 NULL）", "icon：0/1/2/3"],
                sample: `if (!cg_app_has_window(cg_app_of(self))) return;   /* 自检时别弹 */
cg_message_box(self, "保存成功", "提示", 0);`,
              }),
            ],
          },
          {
            label: "打开文件",
            cmds: [
              c("cg_open_file_dialog", "选文件打开", "int cg_open_file_dialog(cg_widget_t *parent, const char *title, const char *filter, char *out_path, int out_size)", "系统「打开」对话框；返回 1 = 用户确定，路径写进 out_path", {
                params: ["filter 写法：\"文本文件(*.txt)|*.txt|所有文件(*.*)|*.*\"", "out_path：至少 1024 字节的缓冲区"],
                sample: `char path[1024];
if (cg_open_file_dialog(self, "选择文件", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*", path, sizeof(path))) {
    cg_widget_set_text(cg_find(cg_app_of(self), "lab_path"), path);
}`,
              }),
            ],
          },
          {
            label: "保存文件",
            cmds: [
              c("cg_save_file_dialog", "选路径保存", "int cg_save_file_dialog(cg_widget_t *parent, const char *title, const char *filter, const char *default_ext, char *out_path, int out_size)", "系统「另存为」对话框；返回 1 = 用户确定", {
                sample: `char path[1024];
if (cg_save_file_dialog(self, "保存到", "文本文件(*.txt)|*.txt", "txt", path, sizeof(path))) {
    FILE *f = fopen(path, "wb");
    if (f) { fputs("内容", f); fclose(f); }
}`,
              }),
            ],
          },
          {
            label: "选择颜色",
            cmds: [
              c("cg_choose_color", "选颜色", "int cg_choose_color(cg_widget_t *parent, unsigned *rgb)", "系统取色器；确认时把选中的 0xRRGGBB 写回 rgb 并返回 1", {
                sample: `unsigned rgb = 0x6750A4;
if (cg_choose_color(self, &rgb)) {
    cg_widget_set_colors(cg_find(cg_app_of(self), "lab_tip"), rgb, CG_COLOR_NONE);
}`,
              }),
            ],
          },
        ],
      },
    ],
  },
  {
    key: "kit",
    label: "基础能力支持库",
    hint: "文件、路径、配置(INI)、文本编码、剪贴板、外部程序、注册表、串口",
    groups: [
      {
        label: "文件",
        cmds: [
          c("cg_file_exists", "文件是否存在", "int cg_file_exists(const char *path)", "1 = 存在（目录不算）", {
            sample: `if (!cg_file_exists("config.ini")) cg_log(CG_LOG_INFO, "还没有配置文件，用默认值");`,
          }),
          c("cg_file_read", "读整个文件", "char *cg_file_read(const char *path)", "返回堆串（自动去掉 UTF-8 BOM）；失败 NULL", {
            params: ["用完记得 cg_free()"],
            ret: "文件内容；打不开返回 NULL",
            sample: `char *text = cg_file_read(path);
if (text) {
    cg_widget_set_text(cg_find(cg_app_of(self), "edt_body"), text);
    cg_free(text);
}`,
          }),
          c("cg_file_write", "写文件（覆盖）", "int cg_file_write(const char *path, const char *text)", "整体覆盖成 UTF-8 文本；成功 0", {
            sample: `cg_file_write(path, cg_widget_text(cg_find(cg_app_of(self), "edt_body")));`,
          }),
          c("cg_file_append", "追加到文件", "int cg_file_append(const char *path, const char *text)", "加到末尾，文件不存在就新建（写日志常用）", {
            sample: `cg_file_append("log.txt", cg_str_format("%s 打开 %s\\r\\n", cg_time_now(NULL), path));`,
          }),
          c("cg_file_delete", "删文件", "int cg_file_delete(const char *path)", "成功返回 0"),
          c("cg_file_copy", "拷文件", "int cg_file_copy(const char *from, const char *to)", "覆盖目标；成功返回 0"),
          c("cg_dir_exists", "目录是否存在", "int cg_dir_exists(const char *path)", ""),
          c("cg_dir_create", "建目录", "int cg_dir_create(const char *path)", "父目录必须已存在；已存在也算成功"),
          c("cg_dir_list", "枚举目录", "int cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size)", "index 从 0 递增取文件名；返回 0 表示没有了", {
            params: ["filter：\"*.txt\" 这种；传 NULL 或 \"*\" 表示全部"],
            sample: `char name[256];
for (int i = 0; cg_dir_list("C:\\\\data", "*.txt", i, name, sizeof(name)); i++) {
    cg_widget_list_add(cg_find(cg_app_of(self), "lst_files"), name);
}`,
          }),
        ],
      },
      {
        label: "路径",
        cmds: [
          c("cg_path_join", "拼路径", "char *cg_path_join(const char *a, const char *b)", "自动补分隔符，返回堆串"),
          c("cg_path_dir", "取目录", "char *cg_path_dir(const char *path)", "去掉文件名部分，返回堆串"),
          c("cg_path_name", "取文件名", "const char *cg_path_name(const char *path)", "**返回内部指针，不用 cg_free**", {
            sample: `cg_widget_set_text(lab, cg_path_name("C:\\\\data\\\\a.txt"));   /* a.txt */`,
          }),
          c("cg_path_ext", "取扩展名", "const char *cg_path_ext(const char *path)", "含点（\".txt\"）；没有返回空串。内部指针，不用释放"),
          c("cg_app_dir", "程序所在目录", "char *cg_app_dir(void)", "exe 所在目录，读同目录的配置文件用"),
          c("cg_app_data_dir", "用户数据目录", "char *cg_app_data_dir(void)", "%LOCALAPPDATA%\\<exe名>（会自动建），存配置/缓存用", {
            sample: `char *dir = cg_app_data_dir();
char *ini = cg_path_join(dir, "config.ini");
cg_ini_write(ini, "窗口", "宽", "800");
cg_free(ini);
cg_free(dir);`,
          }),
        ],
      },
      {
        label: "配置（INI）",
        cmds: [
          c("cg_ini_read", "读配置项", "char *cg_ini_read(const char *path, const char *section, const char *key, const char *def)", "读 [节] 下的 键=值；没有就用 def（返回的堆串要 cg_free）", {
            sample: `char *w = cg_ini_read("config.ini", "窗口", "宽", "800");
int width = atoi(w);
cg_free(w);`,
          }),
          c("cg_ini_write", "写配置项", "int cg_ini_write(const char *path, const char *section, const char *key, const char *value)", "改一项（文件里其它内容保持不动），节/键不存在就补上；成功 0", {
            sample: `/* 关窗时记住窗口大小 */
cg_ini_write("config.ini", "窗口", "宽", cg_str_format("%d", cg_widget_w(w)));`,
          }),
        ],
      },
      {
        label: "文本与编码",
        cmds: [
          c("cg_str_format", "拼字符串", "char *cg_str_format(const char *fmt, ...)", "printf 风格拼好返回堆串（省一个临时缓冲）", {
            sample: `char *msg = cg_str_format("共 %d 个文件，合计 %d KB", n, kb);
cg_message_box(self, msg, "统计", 0);
cg_free(msg);`,
          }),
          c("cg_str_trim", "去首尾空白", "char *cg_str_trim(const char *s)", "去掉两端空格/制表/换行，返回堆串"),
          c("cg_str_equal", "比较字符串", "int cg_str_equal(const char *a, const char *b)", "忽略英文大小写；相等返回 1"),
          c("cg_str_replace", "替换子串", "char *cg_str_replace(const char *src, const char *from, const char *to)", "把 src 里所有 from 换成 to，返回堆串"),
          c("cg_utf8_to_gbk", "UTF-8 转 GBK", "char *cg_utf8_to_gbk(const char *utf8)", "写老式 ANSI/GBK 文件前转一下，否则中文会乱"),
          c("cg_gbk_to_utf8", "GBK 转 UTF-8", "char *cg_gbk_to_utf8(const char *gbk)", "读进来的 GBK 老文件转成界面能显示的中文", {
            sample: `char *raw = cg_file_read("老文件.txt");       /* 可能是 GBK */
char *u8  = cg_gbk_to_utf8(raw);
cg_widget_set_text(edt, u8);
cg_free(u8);
cg_free(raw);`,
          }),
          c("cg_free", "释放内存", "void cg_free(void *p)", "上面这些返回的字符串都要用它释放（等价 free）"),
        ],
      },
      {
        label: "剪贴板与系统",
        cmds: [
          c("cg_clip_get_text", "读剪贴板", "char *cg_clip_get_text(void)", "剪贴板里的文字；没有返回 NULL（堆串要 cg_free）"),
          c("cg_clip_set_text", "写剪贴板", "int cg_clip_set_text(const char *text)", "成功返回 0", {
            sample: `cg_clip_set_text(cg_widget_text(cg_find(cg_app_of(self), "edt_body")));
cg_message_box(self, "已复制到剪贴板", "提示", 0);`,
          }),
          c("cg_shell_open", "用默认程序打开", "int cg_shell_open(const char *path_or_url)", "打开文件 / 文件夹 / 网址（浏览器、记事本……都行）", {
            sample: `cg_shell_open("https://www.example.com");
cg_shell_open(cg_app_data_dir());      /* 打开数据目录 */`,
          }),
          c("cg_run_process", "启动外部程序", "int cg_run_process(const char *cmd_line, int wait)", "跑一条命令行；wait=1 时返回它的退出码"),
          c("cg_time_now", "取当前时间", "char *cg_time_now(const char *fmt)", "fmt 传 NULL 用 \"2026-09-21 02:33:53\"（strftime 格式）", {
            sample: `char *now = cg_time_now("%H:%M:%S");
cg_status_set(cg_find(cg_app_of(self), "sb_main"), 1, now);
cg_free(now);`,
          }),
        ],
      },
      {
        label: "注册表",
        cmds: [
          c("cg_reg_read", "读注册表", "char *cg_reg_read(const char *subkey, const char *name, const char *def)", "读 HKCU\\subkey 的字符串值；没有返回 def", {
            sample: `char *v = cg_reg_read("Software\\\\MyApp", "install_dir", "(未安装)");
cg_free(v);`,
          }),
          c("cg_reg_write", "写注册表", "int cg_reg_write(const char *subkey, const char *name, const char *value)", "写 HKCU 下的字符串值；做开机自启就写到 ...\\CurrentVersion\\Run", {
            sample: `/* 开机自启：把 exe 路径写进 Run */
char *dir = cg_app_dir();
cg_reg_write("Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run", "MyApp",
             cg_path_join(dir, "MyApp.exe"));
cg_free(dir);`,
          }),
        ],
      },
      {
        label: "串口（cg_serial）",
        cmds: [
          c("cg_serial_list", "枚举串口", "int cg_serial_list(char *buf, int cap)", "把本机可用的串口名写进 buf（每行一个，如 \"COM3\\nCOM7\\n\"），返回写入的字节数；一个都没有返回 0（不是错误）。只给端口名，不给「USB-SERIAL CH340 (COM3)」那种友好名 —— 那必须走 SetupAPI，而系统库是在 exe 链接那步才连的，用它等于给每个应用的链接依赖加 -lsetupapi，这点好看不值得", {
            params: ["buf：够大的缓冲区，每行一个端口名", "cap：buf 的容量"],
            ret: "写入的字节数（按 '\\n' 切行就是端口名）；装不下时宁可少列也不截断半行",
            sample: `char buf[4096];
int  n = cg_serial_list(buf, (int)sizeof(buf));
if (n <= 0) cg_term_write(term, CG_TERM_SYS, "没找到任何串口", -1);
/* 内容形如 "COM3\\nCOM7\\n" —— 按 '\\n' 切行填进下拉框就行 */`,
          }),
          c("cg_serial_open", "打开串口", "cg_serial_t *cg_serial_open(const char *port, int baud)", "打开一条串口链路（固定 8N1），内部起一个读线程把字节搬进接收缓冲；失败返回 NULL", {
            params: ["port：\"COM3\" 或 \"\\\\\\\\.\\\\COM3\" 都收；空串算失败", "baud：波特率，<= 0 算失败"],
            ret: "链路句柄（用完必须 cg_serial_close）；失败返回 NULL，原因用 cg_serial_last_error(NULL) 取",
            sample: `/* 打开失败最要紧的是"为什么"，别只报一句"失败了" */
g_ser = cg_serial_open("COM3", 115200);
if (!g_ser) {
    cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS,
                  cg_serial_last_error(NULL), -1);   /* 端口被占用 / 找不到… */
    return;
}
cg_set_timer(app, 50, poll, app);                    /* 连着就用时钟去取数据 */`,
          }),
          c("cg_serial_close", "关闭串口", "void cg_serial_close(cg_serial_t *s)", "关掉链路并回收读线程；**幂等且 NULL 安全**，没连上时直接调也没事", {
            params: ["s：cg_serial_open 的返回值（可以是 NULL）"],
            ret: "void 无返回值",
            sample: `/* 断开按钮和窗口 destroy 回调里都调它，不用自己判"到底连没连" */
cg_serial_close(g_ser);
g_ser = NULL;`,
          }),
          c("cg_serial_write", "发送数据", "int cg_serial_write(cg_serial_t *s, const void *data, int len)", "同步写（带 1s 超时），不是写队列；返回实际写出的字节数，< 0 表示失败", {
            params: ["data：要发的字节；文本直接传字符串", "len：字节数，**必须 > 0**（和 cg_term_write 不一样，它不认 -1）"],
            ret: "实际写出的字节数；失败返回 < 0（原因见 cg_serial_last_error）",
            sample: `const char *cmd = "AT+GMR\\r\\n";       /* 长度得自己算，len 不支持传 -1 */
if (cg_serial_write(g_ser, cmd, (int)strlen(cmd)) < 0) {
    cg_log(CG_LOG_ERROR, "发送失败：%s", cg_serial_last_error(g_ser));
}`,
          }),
          c("cg_serial_read", "取数据（时钟里读）", "int cg_serial_read(cg_serial_t *s, void *buf, int cap)", "**非阻塞**取最多 cap 字节；没数据返回 0（不是错误）", {
            params: ["buf：接收缓冲区", "cap：buf 的容量"],
            ret: "取到的字节数；0 = 这一拍没有数据",
            sample: `/* ★ 唯一被支持的读法：在时钟回调里主动"取"，而不是等着被"推"。
   原因：cg_serial_open 的读线程只把字节搬进环形缓冲，**它绝不碰控件**
   （控件只能在建它的线程里碰，这是 Win32 硬约束）。
   cg_set_timer 是一次性的，"收尾再挂一次"才是周期。 */
cg_set_timer(app, 50, poll, app);

static void poll(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term_log");
    unsigned char buf[4096];
    int           n;

    if (!g_ser) return;                                  /* 已断开：就此停表 */
    while (cg_serial_available(g_ser) > 0) {
        n = cg_serial_read(g_ser, buf, (int)sizeof(buf)); /* 非阻塞，没数据返回 0 */
        if (n <= 0) break;
        cg_term_write(term, CG_TERM_RX, buf, n);          /* 收到多少灌多少，会按 \\n 切行 */
    }
    cg_set_timer(app, 50, poll, app);                     /* 再挂一次 = 50ms 周期 */
}`,
          }),
          c("cg_serial_available", "还有多少没取", "int cg_serial_available(const cg_serial_t *s)", "接收缓冲里还剩多少字节 —— 用来判断「这一拍到底有没有东西」，省一次注定返回 0 的 read", {
            ret: "还没被取走的字节数；s 为 NULL 返回 0",
            sample: `/* 取空缓冲的标准循环：先问一句再读，读到空就收工 */
while (cg_serial_available(g_ser) > 0) {
    int n = cg_serial_read(g_ser, buf, (int)sizeof(buf));
    if (n <= 0) break;
    cg_term_write(term, CG_TERM_RX, buf, n);
}`,
          }),
          c("cg_serial_flush_rx", "丢掉未取数据", "void cg_serial_flush_rx(cg_serial_t *s)", "把接收缓冲里还没取走的字节扔掉（「清空接收」按钮用）；NULL 安全", {
            sample: `/* 清空按钮：缓冲清掉，日志视图也一起清 */
cg_serial_flush_rx(g_ser);
cg_term_clear(cg_find(cg_app_of(self), "term_log"));`,
          }),
          c("cg_serial_rx_bytes", "累计收到字节", "long long cg_serial_rx_bytes(const cg_serial_t *s)", "从打开到现在一共收到多少字节（只增不减，要归零就重连）", {
            ret: "累计字节数；s 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 1, "RX %lld B", cg_serial_rx_bytes(g_ser));`,
          }),
          c("cg_serial_tx_bytes", "累计发出字节", "long long cg_serial_tx_bytes(const cg_serial_t *s)", "从打开到现在一共发出多少字节（同样只增不减）", {
            ret: "累计字节数；s 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 2, "TX %lld B", cg_serial_tx_bytes(g_ser));`,
          }),
          c("cg_serial_last_error", "最后一次错误", "const char *cg_serial_last_error(const cg_serial_t *s)", "最后一次出错的中文描述（\"端口被占用…\"这种）；没出错返回空串", {
            params: ["s：链路句柄；**传 NULL 表示问「最近一次打开为什么失败」**（打开失败时没有句柄可问）"],
            ret: "指向内部缓冲的字符串，不用 free；下次报错会被覆盖，要留就自己拷走",
            sample: `/* 打开失败：没有句柄，只能问 NULL */
if (!(g_ser = cg_serial_open(port, baud))) {
    cg_message_box(self, cg_serial_last_error(NULL), "打不开串口", 2);
    return;
}
/* 写入失败：手里的句柄上有原因 */
if (cg_serial_write(g_ser, buf, n) < 0) {
    cg_term_write(term, CG_TERM_SYS, cg_serial_last_error(g_ser), -1);
}`,
          }),
        ],
      },
      {
        label: "网口（cg_net）",
        cmds: [
          c("cg_net_connect", "连接服务器", "cg_net_t *cg_net_connect(const char *host, int port, int timeout_ms)", "以客户端身份连到 host:port（TCP），内部起读线程把字节搬进接收缓冲；失败返回 NULL", {
            params: ["host：主机名或 IP；**NULL / 空串 = 127.0.0.1**（本机自测最常用）", "port：端口（1~65535）", "timeout_ms：连接超时；**<= 0 按 3000 算**（内部用非阻塞 connect + select，坏地址不会冻住界面）"],
            ret: "连接句柄（用完必须 cg_net_close）；失败返回 NULL，原因用 cg_net_last_error(NULL) 取",
            sample: `/* 连接失败最要紧的是"为什么"，别只报一句"失败了" */
g_cli = cg_net_connect("192.168.1.10", 8080, 3000);
if (!g_cli) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);  /* 拒绝 / 超时 / 找不到主机… */
    return;
}
cg_set_timer(app, 50, poll, app);        /* 连着就用时钟去取数据（见 cg_net_read） */`,
          }),
          c("cg_net_listen", "监听端口", "cg_net_t *cg_net_listen(int port, int backlog)", "以服务端身份监听本机端口（TCP），等客户端连进来；**backlog <= 0 按 4 算**", {
            params: ["port：要监听的本机端口", "backlog：等待队列长度；<= 0 按 4"],
            ret: "监听句柄（它只是「等连接」，不收发数据；用完必须 cg_net_close）；绑不上（端口被占用）返回 NULL，原因见 cg_net_last_error(NULL)",
            sample: `/* 绑定失败最常见的原因就是端口被占用 */
g_srv = cg_net_listen(8080, 4);
if (!g_srv) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);
    return;
}
cg_set_timer(app, 50, poll, app);        /* accept 在时钟里做，见 cg_net_accept */`,
          }),
          c("cg_net_accept", "收一个连接（时钟里轮询）", "cg_net_t *cg_net_accept(cg_net_t *srv, int timeout_ms)", "从监听句柄上收一条新连接；**timeout_ms <= 0 = 不等、立刻返回**，没连接时返回 NULL —— 那是正常情况，不是错误", {
            params: ["srv：cg_net_listen 的返回值", "timeout_ms：等多久；**传 0 = 立刻返回**（正因如此它才适合放在时钟里轮询）"],
            ret: "新连接的句柄（**一个句柄 = 一条连接**，用完 cg_net_close）；没连接返回 NULL（正常，不是错误）",
            sample: `/* ★ 服务端要同时管多个客户端，就自己拿一个句柄数组，在时钟里轮询 accept ——
   本层故意不做"客户端列表"这个概念，API 面保持一条线。 */
static cg_net_t *g_peer[8];
static int       g_npeer;

static void poll(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_net_t *c;
    int       k;

    for (k = 0; k < 4; k++) {                        /* 一拍最多收几个，防"连接风暴" */
        c = cg_net_accept(g_srv, 0);                  /* 0 = 不等，立刻返回 */
        if (!c) break;                                /* NULL = 这拍没人连，不是错误 */
        if (g_npeer >= 8) { cg_net_close(c); break; } /* 数组满了就拒收，别漏了 close */
        g_peer[g_npeer++] = c;                        /* 存进自己的数组，稍后逐个 read */
        cg_term_write(cg_find(app, "term"), CG_TERM_SYS, cg_net_peer(c), -1);  /* 谁来了 */
    }
    cg_set_timer(app, 50, poll, app);                 /* 一次性定时器：再挂一次 = 周期 */
}`,
          }),
          c("cg_net_write", "发送数据", "int cg_net_write(cg_net_t *n, const void *data, int len)", "同步写（带 1s 超时，不是写队列）；返回实际写出的字节数，< 0 表示失败", {
            params: ["n：连接句柄（监听句柄不能写）", "data：要发的字节；文本直接传字符串", "len：字节数，**必须 > 0**（和 cg_term_write 不一样，它不认 -1）"],
            ret: "实际写出的字节数；失败返回 < 0（原因见 cg_net_last_error）",
            sample: `const char *cmd = "GET / HTTP/1.0\\r\\n\\r\\n";   /* 长度得自己算，len 不支持传 -1 */
if (cg_net_write(g_cli, cmd, (int)strlen(cmd)) < 0) {
    cg_log(CG_LOG_ERROR, "发送失败：%s", cg_net_last_error(g_cli));
}`,
          }),
          c("cg_net_read", "取数据（时钟里读）", "int cg_net_read(cg_net_t *n, void *buf, int cap)", "**非阻塞**取最多 cap 字节；没数据返回 0（不是错误）", {
            params: ["n：连接句柄", "buf：接收缓冲区", "cap：buf 的容量"],
            ret: "取到的字节数；0 = 这一拍没有数据；对端已关闭时也能把剩下的取完",
            sample: `/* ★ 唯一被支持的读法：在时钟回调里主动"取"，而不是等着被"推"。
   原因：cg_net_connect / cg_net_accept 的读线程只把字节搬进环形缓冲，
   **它绝不碰控件**（控件只能在建它的线程里碰，这是 Win32 硬约束）。
   cg_set_timer 是一次性的，"收尾再挂一次"才是周期。 */
cg_set_timer(app, 50, poll, app);

static void poll(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term");
    unsigned char buf[4096];
    int           n, i;

    if (g_cli) {                                       /* 客户端那条连接 */
        if (!cg_net_connected(g_cli) && cg_net_available(g_cli) <= 0) {
            cg_term_write(term, CG_TERM_SYS, "对端关闭了连接", -1);   /* 掉线了 */
            cg_net_close(g_cli);
            g_cli = NULL;
        } else while (cg_net_available(g_cli) > 0) {
            n = cg_net_read(g_cli, buf, (int)sizeof(buf));  /* 非阻塞，没数据返回 0 */
            if (n <= 0) break;
            cg_term_write(term, CG_TERM_RX, buf, n);        /* 收到多少灌多少，会按 \\n 切行 */
        }
    }
    for (i = 0; i < g_npeer; i++) {                    /* 服务端收上来的每条连接 */
        while (cg_net_available(g_peer[i]) > 0) {
            n = cg_net_read(g_peer[i], buf, (int)sizeof(buf));
            if (n <= 0) break;
            cg_term_write(term, CG_TERM_RX, buf, n);
        }
    }
    cg_set_timer(app, 50, poll, app);                  /* 再挂一次 = 50ms 周期 */
}`,
          }),
          c("cg_net_available", "还有多少没取", "int cg_net_available(const cg_net_t *n)", "接收缓冲里还剩多少字节 —— 用来判断「这一拍到底有没有东西」，省一次注定返回 0 的 read", {
            ret: "还没被取走的字节数；n 为 NULL 返回 0",
            sample: `/* 取空一条连接的标准循环：先问一句再读，读到空就收工 */
while (cg_net_available(g_cli) > 0) {
    int n = cg_net_read(g_cli, buf, (int)sizeof(buf));
    if (n <= 0) break;
    cg_term_write(term, CG_TERM_RX, buf, n);
}`,
          }),
          c("cg_net_connected", "对端还在吗", "int cg_net_connected(const cg_net_t *n)", "对端还在返回 1，对方关闭后变 0 —— **这是判断「掉线」的唯一信号**（监听句柄恒为 0）", {
            ret: "1 = 连接还在；0 = 对端已关闭（或这是监听句柄）",
            sample: `/* 掉线检测：先把缓冲里剩下的取干净，再回收句柄 */
if (!cg_net_connected(g_cli) && cg_net_available(g_cli) <= 0) {
    cg_term_write(term, CG_TERM_SYS, "对端关闭了连接", -1);
    cg_net_close(g_cli);
    g_cli = NULL;
}`,
          }),
          c("cg_net_rx_bytes", "累计收到字节", "long long cg_net_rx_bytes(const cg_net_t *n)", "从连上到现在这条连接一共收到多少字节（只增不减，要归零就重连）", {
            ret: "累计字节数；n 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 2, "RX %lld B", cg_net_rx_bytes(g_cli));`,
          }),
          c("cg_net_tx_bytes", "累计发出字节", "long long cg_net_tx_bytes(const cg_net_t *n)", "从连上到现在这条连接一共发出多少字节（同样只增不减）", {
            ret: "累计字节数；n 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 3, "TX %lld B", cg_net_tx_bytes(g_cli));`,
          }),
          c("cg_net_peer", "对端地址", "const char *cg_net_peer(const cg_net_t *n)", "对端的 \"ip:port\"；监听句柄返回 \"（监听中）\"", {
            ret: "指向内部缓冲的字符串，不用 free；下次调用可能被覆盖，要留就自己拷走",
            sample: `/* 服务端最想知道"谁来了"：收上新连接时打一行 */
cg_term_write(term, CG_TERM_SYS, cg_net_peer(g_peer[g_npeer - 1]), -1);`,
          }),
          c("cg_net_last_error", "最后一次错误", "const char *cg_net_last_error(const cg_net_t *n)", "最后一次出错的中文描述（\"连接被拒绝…\"这种）；没出错返回空串", {
            params: ["n：连接句柄；**传 NULL 表示问「最近一次连接/监听为什么失败」**（失败时没有句柄可问）"],
            ret: "指向内部缓冲的字符串，不用 free；下次报错会被覆盖，要留就自己拷走",
            sample: `/* 连接 / 监听失败：没有句柄，只能问 NULL */
if (!(g_srv = cg_net_listen(port, 4))) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);
    return;
}
/* 写入失败：手里的句柄上有原因 */
if (cg_net_write(g_cli, buf, n) < 0) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(g_cli), -1);
}`,
          }),
          c("cg_net_close", "关闭连接", "void cg_net_close(cg_net_t *n)", "关掉连接或监听并回收读线程（会等它退出，最坏约 50ms）；**幂等且 NULL 安全**", {
            params: ["n：cg_net_connect / cg_net_listen / cg_net_accept 的返回值（可以是 NULL）"],
            ret: "void 无返回值",
            sample: `/* 断开按钮和窗口 destroy 回调里，把每一个句柄都关掉 —— 不用自己判"到底连没连" */
cg_net_close(g_cli);   g_cli = NULL;
for (i = 0; i < g_npeer; i++) cg_net_close(g_peer[i]);
g_npeer = 0;
cg_net_close(g_srv);   g_srv = NULL;`,
          }),
        ],
      },
    ],
  },
];

/** 遍历所有命令（含子分类），用于计数与全局搜索 */
export function allCommands(): SupCmd[] {
  const out: SupCmd[] = [];
  const walkGroup = (g: SupGroup) => {
    out.push(...g.cmds);
    g.children?.forEach(walkGroup);
  };
  SUPPORT_LIBS.forEach((lib) => lib.groups.forEach(walkGroup));
  return out;
}

export function countCommands(): number {
  return allCommands().length;
}

function cmdMatches(cmd: SupCmd, kw: string): boolean {
  return (
    cmd.fn.toLowerCase().includes(kw) ||
    cmd.name.toLowerCase().includes(kw) ||
    cmd.brief.toLowerCase().includes(kw) ||
    cmd.decl.toLowerCase().includes(kw)
  );
}

/** 按关键字过滤。
 *  命中规则：命令的 函数名/中文名/说明/原型 任一命中；
 *  **分类名或库名命中时，整棵子树都算命中**（新手常按「信息框」「表格」这种分类词找，
 *  而不是按 cg_message_box 这种函数名找）。 */
export function filterSupportLibs(libs: SupLib[], keyword: string): SupLib[] {
  const kw = keyword.trim().toLowerCase();
  if (!kw) return libs;

  const filterGroup = (g: SupGroup): SupGroup | null => {
    const keepAll = g.label.toLowerCase().includes(kw);
    const cmds = keepAll ? g.cmds : g.cmds.filter((x) => cmdMatches(x, kw));
    const children = keepAll
      ? g.children ?? []
      : (g.children ?? []).map(filterGroup).filter((x): x is SupGroup => !!x);
    if (!cmds.length && !children.length) return null;
    return { label: g.label, cmds, children: children.length ? children : undefined };
  };

  const out: SupLib[] = [];
  for (const lib of libs) {
    if (lib.label.toLowerCase().includes(kw)) {
      out.push(lib);
      continue;
    }
    const groups = lib.groups.map(filterGroup).filter((x): x is SupGroup => !!x);
    if (groups.length) out.push({ ...lib, groups });
  }
  return out;
}

/** 库里第一个命令（搜索后自动展开/定位用） */
export function firstCommand(lib: SupLib): SupCmd | null {
  const walk = (g: SupGroup): SupCmd | null => {
    if (g.cmds.length) return g.cmds[0];
    for (const child of g.children ?? []) {
      const hit = walk(child);
      if (hit) return hit;
    }
    return null;
  };
  for (const g of lib.groups) {
    const hit = walk(g);
    if (hit) return hit;
  }
  return null;
}
