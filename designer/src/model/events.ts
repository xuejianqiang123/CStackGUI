/* model/events.ts -- 事件名总表、中文标签与「控件支持哪些事件」
 *
 * 模型层里「事件」这一维：事件全表（CG_EVENTS）、窗口事件子集（WINDOW_EVENTS）、
 * 中文名/提示、以及按控件类型查可用事件（KIND_EVENTS / eventsOf）。
 *
 * 本文件不依赖模型层其它文件（它是依赖链的最底层之一）。
 */

/* 事件名一份总表：控件事件 + 窗口事件。
 * 必须和 runtime/src/core/cg_event.c 的 k_ev_names[] 一一对应（名字写错 C 侧不报错、只是不触发）。 */
export const CG_EVENTS = [
  /* --- 控件事件 --- */
  "click", "dblclick", "change", "submit", "select", "release",
  "expand", "collapse", "tick", "arrive", "check", "gotfocus", "lostfocus",
  /* --- 窗口事件（绑在窗口上，对应「启动窗口」那张事件表） --- */
  "ready", "close", "closequery", "destroy", "move", "size",
  "activate", "deactivate", "firstactivate", "idle", "tray", "show", "hide",
  "mousedown", "mouseup", "rdown", "rup", "mousemove",
  "keydown", "keyup", "char", "wheel",
  /* 输入框右侧调节器的点击（只有 edit 支持） */
  "spin",
  /* 文件被拖进窗口（窗口级；回调里用 cg_drop_count / cg_drop_file 取路径） */
  "drop",
] as const;
export type CgEvent = (typeof CG_EVENTS)[number];

/** 窗口支持的事件（必须和 runtime/src/core/cg_doc_kind.c 里 "window" 那行的位掩码一致）。
 *  gotfocus/lostfocus/dblclick 是和控件共用的名字。 */
export const WINDOW_EVENTS: CgEvent[] = [
  "ready", "close", "closequery", "destroy", "move", "size",
  "activate", "deactivate", "firstactivate", "idle", "tray", "show", "hide",
  "mousedown", "mouseup", "dblclick", "rdown", "rup", "mousemove",
  "gotfocus", "lostfocus", "keydown", "keyup", "char", "wheel",
  "drop",
];

export const EVENT_LABEL: Record<CgEvent, string> = {
  drop: "文件拖入（窗口）",
  click: "单击",
  dblclick: "双击",
  change: "内容/值变化",
  submit: "回车提交",
  select: "选择变化",
  release: "拖动结束",
  expand: "展开节点",
  collapse: "折叠节点",
  tick: "时钟周期到",
  arrive: "数据到达",
  check: "行勾选变化",
  gotfocus: "获得焦点",
  lostfocus: "失去焦点",
  /* 窗口事件：名字对着「启动窗口」的事件表 */
  ready: "创建完毕",
  close: "可否被关闭",
  closequery: "窗口可否被关闭",
  destroy: "将被销毁",
  move: "位置被改变",
  size: "尺寸被改变",
  activate: "被激活",
  deactivate: "被取消激活",
  firstactivate: "首次激活",
  idle: "空闲",
  tray: "托盘事件",
  show: "被显示",
  hide: "被隐藏",
  mousedown: "鼠标左键被按下",
  mouseup: "鼠标左键被放开",
  rdown: "鼠标右键被按下",
  rup: "鼠标右键被放开",
  mousemove: "鼠标位置被移动",
  keydown: "按下某键",
  keyup: "放开某键",
  char: "字符输入",
  wheel: "滚轮被滚动",
  spin: "调节钮被按下",
};

/** 同一个事件在不同控件上叫法不同时的覆盖表（对齐属性面板的习惯叫法）：
 *  edit 的 change 是「内容被改变」，而滑块/勾选上它更像「值变化」，所以不能改公共标签。 */
export const EVENT_LABEL_BY_TYPE: Record<string, Partial<Record<CgEvent, string>>> = {
  edit: { change: "内容被改变" },
};

/** 取某个控件上这个事件的中文名（没覆盖就用公共名） */
export const eventLabel = (type: string, ev: CgEvent): string =>
  EVENT_LABEL_BY_TYPE[type]?.[ev] ?? EVENT_LABEL[ev];

export const EVENT_HINT: Record<CgEvent, string> = {
  drop: "把文件从资源管理器拖到窗口上（窗口事件）",
  click: "点一下",
  dblclick: "单击后系统双击间隔内再点一次",
  change: "输入框改字、勾选框切换、滑块拖动、日期改值",
  submit: "单行输入框里按回车",
  select: "选中项变化（列表 / 标签页 / 树 / 表格 / 日历）",
  release: "滑块松手才算，适合「拖完再保存」",
  expand: "树节点展开",
  collapse: "树节点折叠",
  tick: "时钟控件的周期到了（period 毫秒一次）",
  arrive: "串口收到数据时触发（无界面控件，数据落到哪个控件由你的代码决定）",
  check: "表格里某一行的复选框被点了",
  gotfocus: "控件拿到键盘焦点（输入校验常用）",
  lostfocus: "控件失去键盘焦点（输入校验常用）",
  ready: "窗口和控件都建好、还没显示出来；启动初始化放这里",
  close: "用户点关闭（或 Alt+F4）。要拦就用 cg_cancel_close(app)",
  closequery: "和「可否被关闭」同一时机，两个名字都能拦关闭",
  destroy: "窗口已经销毁、进程马上退出",
  move: "窗口位置变了，cg_event_x / cg_event_y 给新坐标",
  size: "窗口尺寸变了，cg_event_x / cg_event_y 给新的客户区宽高",
  activate: "从别的程序切回来",
  deactivate: "切到别的程序去了",
  firstactivate: "启动后第一次被激活",
  idle: "消息队列空着的时候；不要在这里做耗时的活",
  tray: "托盘图标被点，cg_tray_action 给 1=单击 / 2=双击 / 3=右键",
  show: "窗口被显示出来",
  hide: "窗口被隐藏",
  mousedown: "按下左键（窗口=客户区，控件=控件内）；坐标用 cg_event_x / cg_event_y",
  mouseup: "松开左键（位置口径同上）",
  rdown: "按下右键",
  rup: "松开右键",
  mousemove: "鼠标移动（触发很频繁，别做重活）",
  spin: "输入框右侧的上下箭头被点/被拖；cg_event_delta 给 +1 / -1（自动方式下文本已改好）",
  keydown: "按下按键；cg_event_key 给键代码、cg_event_keys 给 Ctrl/Shift/Alt",
  keyup: "松开按键",
  char: "输入了可打印字符（中文输入法也走这里）",
  wheel: "滚轮滚动；cg_event_delta 给滚动量（正数向上）",
};

/** 每种控件支持哪些事件 —— 必须和 runtime/src/core/cg_doc_kind.c 的 k_kinds 表一一对应。
 *  事件是按控件特性设计的：标签没有交互、进度条只能被程序写、只有树有展开折叠。
 *  画布只列这里声明的事件；C 侧也会校验，绑错了启动时会警告。 */
export const KIND_EVENTS: Record<string, CgEvent[]> = {
  /* 按钮是交互最全的控件：鼠标按下/放开/右键/移动、键盘按下/放开/字符、滚轮全套支持
   * （必须和 runtime/src/core/cg_doc_kind.c 的 button 行一致） */
  button: ["click", "mousedown", "mouseup", "dblclick", "rdown", "rup", "mousemove",
           "gotfocus", "lostfocus", "keydown", "keyup", "char", "wheel"],
  iconbutton: ["click", "dblclick"],
  link: ["click", "dblclick"],
  label: [],
  switch: ["change", "gotfocus", "lostfocus"],
  /* 单行输入框：自己的 change/submit + 鼠标/键盘全套（输入时拦按键、点一下做点事都要用）
   * click/dblclick 是 2026-09-23 补齐的：EDIT 类不发 BN_CLICKED 也不产生 WM_LBUTTONDBLCLK，
   * 由子类在左键抬起时补派发 click、双击再由 cg_dispatch 合成（见 cg_win_event.c）。
   * 必须和 runtime/src/core/cg_doc_kind.c 的 edit 行一致 */
  edit: ["click", "change", "submit", "mousedown", "mouseup", "dblclick", "rdown", "rup", "mousemove",
        "gotfocus", "lostfocus", "keydown", "keyup", "char", "wheel", "spin"],
  richtext: ["change", "gotfocus", "lostfocus"],
  datepicker: ["change", "gotfocus", "lostfocus"],
  /* 月历只给 select：MCN_SELECT 已含"点了哪一天"，而它从不发 NM_DBLCLK
   * （2026-09-23 真机连点实测只进 select），原 dblclick 是空头承诺，已随运行时撤掉 */
  monthcal: ["select"],
  checkbox: ["change", "gotfocus", "lostfocus"],
  radio: ["change", "gotfocus", "lostfocus"],
  listbox: ["select", "dblclick", "gotfocus", "lostfocus"],
  combo: ["select", "gotfocus", "lostfocus"],
  slider: ["change", "release", "gotfocus", "lostfocus"],
  progress: [],
  progressring: [],
  tabs: ["select", "gotfocus", "lostfocus"],
  tree: ["select", "dblclick", "expand", "collapse", "gotfocus", "lostfocus"],
  table: ["select", "dblclick", "check", "gotfocus", "lostfocus"],
  /* 图标列表：比 table 多 click —— dblclick 是 cg_dispatch 按系统双击间隔从 click 合成的 */
  iconlist: ["click", "dblclick", "select", "check", "gotfocus", "lostfocus"],
  statusbar: [],
  menu: ["click"],
  menubutton: ["click"],
  toolbar: ["click"],
  group: [],
  box: [],
  separator: [],
  image: ["click", "dblclick"],
  timer: ["tick"],
  serial: ["arrive"],
  /* 画板：2026-09-28 补上自己的鼠标三件套（拖拽作图只能在子控件自己的消息里做），
   * 事件里的坐标就是画板内的逻辑像素。必须和 runtime/src/core/cg_doc_kind.c 的 canvas 行一致 */
  canvas: ["click", "dblclick", "mousedown", "mouseup", "mousemove"],
  colorpicker: ["change"],
  browser: [],
  badge: [],
  tag: ["click"],
  rate: ["change"],
  pagination: ["change"],
  card: [],
  /* 终端是只读的：内容由 cg_term_write 灌进去，没有用户操作要通知业务 */
  terminal: [],
  /* 图表也是纯显示：数据由 cg_chart_push 推，控件本身没有用户操作 */
  chart: [],
  /* 拖完才发一次（拖动中不发）—— 适合在回调里把布局存进 INI */
  splitter: ["release"],
  shape: [],
  /* 状态灯是纯显示：状态由 cg_led_set 写，控件本身没有用户操作 */
  led: [],
  /* 仪表盘同理：值由 cg_meter_set 写，指针不给用户拖 */
  meter: [],
  /* 对话框：窗口级事件的**子集** —— 复用主窗口的窗口过程，能触发的都在这。
     ready/firstactivate/idle 是"整个应用启动/泵"级别的事，tray 要 per-窗口托盘图标，
     drop 要对对话框 HWND 调 DragAcceptFiles —— 这五个对话框都没有，先不承诺
     （monthcal 的教训：不承诺不触发的）。cg_dialog_open 落地后逐个核对，该补的**两边一起补**
     （文档/画布/掩码三方对账，check-examples 的 ④.6/④.7 会立刻抓不一致）。
     必须和 runtime/src/core/cg_doc_kind.c 的 dialog 行一致。 */
  dialog: [
    "close", "closequery", "destroy", "move", "size", "activate", "deactivate",
    "show", "hide", "mousedown", "mouseup", "dblclick", "rdown", "rup", "mousemove",
    "gotfocus", "lostfocus", "keydown", "keyup", "char", "wheel",
  ],
};

/** 这个控件支持的事件；没登记的按"不支持任何事件"处理（C 侧同样会拒绝）。
 *  "window" 是窗口伪控件，走另一张表（app.events）。 */
export const eventsOf = (type: string): CgEvent[] =>
  type === "window" ? WINDOW_EVENTS : (KIND_EVENTS[type] ?? []);
