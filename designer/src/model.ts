/* model.ts -- .cgui 文档模型
 *
 * 这里的类型必须和 runtime/src/core/cg_doc.c 的解析逻辑一一对应：
 * 改这里就要同步改那边，否则「画布能存、运行时读不了」。
 */

/* 事件名一份总表：控件事件 + 窗口事件。
 * 必须和 runtime/src/core/cg_event.c 的 k_ev_names[] 一一对应（名字写错 C 侧不报错、只是不触发）。 */
export const CG_EVENTS = [
  /* --- 控件事件 --- */
  "click", "dblclick", "change", "submit", "select", "release",
  "expand", "collapse", "tick", "check", "gotfocus", "lostfocus",
  /* --- 窗口事件（绑在窗口上，对应「启动窗口」那张事件表） --- */
  "ready", "close", "closequery", "destroy", "move", "size",
  "activate", "deactivate", "firstactivate", "idle", "tray", "show", "hide",
  "mousedown", "mouseup", "rdown", "rup", "mousemove",
  "keydown", "keyup", "char", "wheel",
] as const;
export type CgEvent = (typeof CG_EVENTS)[number];

/** 窗口支持的事件（必须和 runtime/src/core/cg_doc_kind.c 里 "window" 那行的位掩码一致）。
 *  gotfocus/lostfocus/dblclick 是和控件共用的名字。 */
export const WINDOW_EVENTS: CgEvent[] = [
  "ready", "close", "closequery", "destroy", "move", "size",
  "activate", "deactivate", "firstactivate", "idle", "tray", "show", "hide",
  "mousedown", "mouseup", "dblclick", "rdown", "rup", "mousemove",
  "gotfocus", "lostfocus", "keydown", "keyup", "char", "wheel",
];

export const EVENT_LABEL: Record<CgEvent, string> = {
  click: "单击",
  dblclick: "双击",
  change: "内容/值变化",
  submit: "回车提交",
  select: "选择变化",
  release: "拖动结束",
  expand: "展开节点",
  collapse: "折叠节点",
  tick: "时钟周期到",
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
};

export const EVENT_HINT: Record<CgEvent, string> = {
  click: "点一下",
  dblclick: "单击后系统双击间隔内再点一次",
  change: "输入框改字、勾选框切换、滑块拖动、日期改值",
  submit: "单行输入框里按回车",
  select: "选中项变化（列表 / 标签页 / 树 / 表格 / 日历）",
  release: "滑块松手才算，适合「拖完再保存」",
  expand: "树节点展开",
  collapse: "树节点折叠",
  tick: "时钟控件的周期到了（period 毫秒一次）",
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
  mousedown: "在窗口空白处按下左键；坐标用 cg_event_x / cg_event_y",
  mouseup: "在窗口空白处松开左键",
  rdown: "在窗口空白处按下右键",
  rup: "在窗口空白处松开右键",
  mousemove: "鼠标在窗口空白处移动（触发很频繁，别做重活）",
  keydown: "按下按键；cg_event_key 给键代码、cg_event_keys 给 Ctrl/Shift/Alt",
  keyup: "松开按键",
  char: "输入了可打印字符（中文输入法也走这里）",
  wheel: "滚轮滚动；cg_event_delta 给滚动量（正数向上）",
};

/** 每种控件支持哪些事件 —— 必须和 runtime/src/core/cg_doc_kind.c 的 k_kinds 表一一对应。
 *  事件是按控件特性设计的：标签没有交互、进度条只能被程序写、只有树有展开折叠。
 *  画布只列这里声明的事件；C 侧也会校验，绑错了启动时会警告。 */
export const KIND_EVENTS: Record<string, CgEvent[]> = {
  button: ["click", "dblclick", "gotfocus", "lostfocus"],
  iconbutton: ["click", "dblclick"],
  link: ["click", "dblclick"],
  label: [],
  switch: ["change", "gotfocus", "lostfocus"],
  edit: ["change", "submit", "gotfocus", "lostfocus"],
  multiedit: ["change", "gotfocus", "lostfocus"],
  richtext: ["change", "gotfocus", "lostfocus"],
  spinner: ["change", "gotfocus", "lostfocus"],
  datepicker: ["change", "gotfocus", "lostfocus"],
  monthcal: ["select", "dblclick"],
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
  statusbar: [],
  menu: ["click"],
  menubutton: ["click"],
  toolbar: ["click"],
  group: [],
  box: [],
  separator: [],
  image: ["click", "dblclick"],
  timer: ["tick"],
  canvas: ["click", "dblclick"],
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
};

/** 这个控件支持的事件；没登记的按"不支持任何事件"处理（C 侧同样会拒绝）。
 *  "window" 是窗口伪控件，走另一张表（app.events）。 */
export const eventsOf = (type: string): CgEvent[] =>
  type === "window" ? WINDOW_EVENTS : (KIND_EVENTS[type] ?? []);

export type CgAlign = "left" | "center" | "right";

export type CgStyle = {
  font?: string;
  size?: number;
  bold?: boolean;
  fg?: string;
  bg?: string;
  radius?: number;
  align?: CgAlign;
};

export type CgRect = { x: number; y: number; w: number; h: number };

/** 树节点 / 菜单项（对应 .cgui 的 nodes 字段，可嵌套）。
 *  和 C 侧 cg_item_t 一样共用一个结构：树用 text/expanded，菜单用 text/id/checked。
 *  菜单里 text 写 "-" 表示分隔线。 */
export type CgTreeItem = {
  text: string;
  /** 菜单项标识（回调里用 cg_menu_last_id 取回） */
  id?: string;
  /** 树：初始展开 */
  expanded?: boolean;
  /** 菜单：勾选标记 */
  checked?: boolean;
  children?: CgTreeItem[];
};
/** 表格列 */
export type CgColumn = { title: string; w?: number };

/** 控件绑定（对应运行时 cg_bind.c）：本控件的值/文字/勾选一直跟着 src 走。
 *  抄什么由**本控件的类型**决定，不需要额外配置：
 *    滑块/进度/圆环/微调/评分/分页/标签页/列表/下拉/工具栏 -> 抄值
 *    复选框/单选钮/开关                                    -> 抄勾选状态
 *    其它（能显示文字的）                                  -> 源是输入框抄文字，否则显示源的数值
 *  典型用法：滑块拖动时旁边的标签显示数字、微调框与滑块互相同步。 */
export type CgBind = {
  /** 源控件 id（同一份 .cgui 里的另一个控件） */
  src: string;
  /** 双向：本控件被用户改动时也推回源（两个入口改同一个值） */
  two?: boolean;
};

export type CgNode = {
  id: string;
  type: string;
  rect: CgRect;
  text?: string;
  style?: CgStyle;
  events?: Partial<Record<CgEvent, string>>;
  /** 控件绑定：跟着哪个控件走（见 CgBind） */
  bind?: CgBind;
  visible?: boolean;
  enabled?: boolean;
  checked?: boolean;
  value?: number;
  min?: number;
  max?: number;
  /** 属于标签页的第几页（tabs 的子控件） */
  page?: number;
  /** image 的图片路径（相对 .cgui 所在目录） */
  src?: string;
  /** datepicker 的细分格式：date / time / datetime；shape：rect / roundrect / ellipse */
  format?: string;
  /** 鼠标悬停提示（tooltip），所有控件通用 */
  hint?: string;
  /** timer 的触发周期（毫秒），0 = 停 */
  period?: number;
  /** table：允许多选行 */
  multi?: boolean;
  /** table：显示行首复选框列 */
  check?: boolean;
  /** listbox / combo / tabs / statusbar 的文本条目 */
  items?: string[];
  /** 树 */
  nodes?: CgTreeItem[];
  /** 表格 */
  columns?: CgColumn[];
  rows?: string[][];
  children?: CgNode[];
};

/** 边框（对齐「属性」面板的"边框"下拉） */
export type CgBorder = "sizable" | "fixed" | "dialog" | "none";

/** 鼠标指针名（对应 Win32 的 IDC_*，见 cursor_id_of） */
export const CURSORS = [
  "", "arrow", "hand", "ibeam", "wait", "cross",
  "sizeall", "sizens", "sizewe", "sizenesw", "sizenwse", "no", "help", "uparrow",
] as const;

export const BORDER_LABEL: Record<CgBorder, string> = {
  sizable: "可调整边框（默认）",
  fixed: "普通固定边框",
  dialog: "固定对话框边框",
  none: "无边框",
};

/** 程序配置（画布工具栏「程序配置」对话框）。
 *  编译时写进 exe 的版本信息（<工程>.rc），运行时可用 cg_app_info() 读回来。 */
export type CgInfo = {
  /** 程序名称 -> ProductName */
  product?: string;
  /** 程序描述 -> FileDescription */
  desc?: string;
  /** 程序备注 -> Comments */
  comments?: string;
  /** 程序版本号（四段 "1.0.0.0"） */
  version?: string;
  /** 创建号（那个 0.0，仅供对照） */
  build?: string;
  /** 作者 -> CompanyName 之外的 Author 字段 */
  author?: string;
  /** 单位 / 公司 -> CompanyName */
  company?: string;
  /** 版权声明 -> LegalCopyright */
  copyright?: string;
  address?: string;
  postcode?: string;
  phone?: string;
  fax?: string;
  email?: string;
  homepage?: string;
};

/** 程序配置里会被写进版本信息的键（顺序 = 画布对话框里的顺序） */
export const INFO_KEYS = [
  "product", "desc", "comments", "version", "build",
  "author", "company", "copyright",
  "address", "postcode", "phone", "fax", "email", "homepage",
] as const;

/** 窗口级属性（画布「窗口属性」面板 = .cgui 的 app 节点）。
 *  全部可选：不写的按 C 侧默认值走，和加这些属性之前的行为一致。 */
export type CgApp = {
  title: string;
  width: number;
  height: number;
  bg: string;
  /** 窗口左边 / 顶边（屏幕坐标）；不填交给系统默认位置 */
  x?: number;
  y?: number;
  /** 位置=居中（盖过 x/y） */
  center?: boolean;
  border?: CgBorder;
  /** 控制按钮（系统菜单 + 关闭钮），默认 true */
  controlBox?: boolean;
  minimizeBox?: boolean;
  maximizeBox?: boolean;
  /** 总在最前 */
  topmost?: boolean;
  /** 在任务栏中显示，默认 true */
  taskbar?: boolean;
  /** 可视，默认 true */
  visible?: boolean;
  /** 禁止（整窗禁用），默认 true 即可用 */
  enabled?: boolean;
  /** 回车下移焦点（默认 false = 回车当 submit） */
  enterTab?: boolean;
  /** Esc 键关闭窗口，默认 true */
  escClose?: boolean;
  /** 建托盘图标 */
  tray?: boolean;
  /** 窗口图标（.ico） */
  icon?: string;
  /** 鼠标指针 */
  cursor?: string;
  /** 窗口类名，默认 CStackGUIMain */
  class?: string;
  /** 备注（画布记录，运行时不用） */
  note?: string;
  /** 程序配置 -> exe 版本信息 */
  info?: CgInfo;
  /** 窗口级事件：名字写在 app.events 上（伪控件 __window） */
  events?: Partial<Record<CgEvent, string>>;
};

export type CgDoc = {
  cgui: 1;
  app: CgApp;
  widgets: CgNode[];
};

export type KindInfo = {
  type: string;
  label: string;
  group: string;
  container: boolean;
  w: number;
  h: number;
  text: string;
  /** 有值的控件才显示 value/min/max 输入框 */
  ranged?: boolean;
  checklist?: boolean;
  /** 用文本行编辑内容的控件（items 编辑器） */
  listLike?: boolean;
};

/* 顺序与分组要和 runtime/src/core/cg_doc_kind.c 的 k_kinds 表保持一致 */
export const KINDS: KindInfo[] = [
  { type: "button", label: "按钮", group: "基础", container: false, w: 120, h: 40, text: "按钮" },
  { type: "iconbutton", label: "图标按钮", group: "基础", container: false, w: 48, h: 48, text: "" },
  { type: "label", label: "标签", group: "基础", container: false, w: 160, h: 24, text: "标签" },
  { type: "link", label: "链接", group: "基础", container: false, w: 160, h: 24, text: "打开链接" },
  { type: "switch", label: "开关", group: "基础", container: false, w: 160, h: 32, text: "开关", checklist: true },
  { type: "edit", label: "单行输入", group: "输入", container: false, w: 200, h: 32, text: "" },
  { type: "multiedit", label: "多行输入", group: "输入", container: false, w: 240, h: 120, text: "" },
  { type: "spinner", label: "数字微调", group: "输入", container: false, w: 110, h: 28, text: "", ranged: true },
  { type: "checkbox", label: "复选框", group: "输入", container: false, w: 160, h: 28, text: "选项", checklist: true },
  { type: "radio", label: "单选钮", group: "输入", container: false, w: 160, h: 28, text: "选项", checklist: true },
  { type: "listbox", label: "列表框", group: "输入", container: false, w: 220, h: 140, text: "", listLike: true },
  { type: "combo", label: "下拉框", group: "输入", container: false, w: 200, h: 200, text: "", listLike: true },
  { type: "richtext", label: "富文本", group: "输入", container: false, w: 320, h: 160, text: "" },
  { type: "datepicker", label: "日期时间", group: "输入", container: false, w: 140, h: 28, text: "" },
  { type: "menu", label: "菜单栏", group: "菜单", container: false, w: 0, h: 0, text: "" },
  { type: "menubutton", label: "菜单按钮", group: "菜单", container: false, w: 130, h: 32, text: "菜单" },
  { type: "toolbar", label: "工具栏", group: "菜单", container: false, w: 400, h: 34, text: "", listLike: true },
  { type: "monthcal", label: "月历", group: "输入", container: false, w: 230, h: 180, text: "" },
  { type: "slider", label: "滑块", group: "数值", container: false, w: 200, h: 32, text: "", ranged: true },
  { type: "progress", label: "进度条", group: "数值", container: false, w: 200, h: 20, text: "", ranged: true },
  { type: "progressring", label: "圆环进度", group: "数值", container: false, w: 120, h: 120, text: "", ranged: true },
  { type: "tabs", label: "标签页", group: "数据", container: true, w: 320, h: 220, text: "", listLike: true },
  { type: "tree", label: "树", group: "数据", container: false, w: 220, h: 180, text: "" },
  { type: "table", label: "表格", group: "数据", container: false, w: 360, h: 180, text: "" },
  { type: "statusbar", label: "状态栏", group: "数据", container: false, w: 400, h: 24, text: "", listLike: true },
  { type: "group", label: "分组框", group: "容器", container: true, w: 260, h: 180, text: "分组" },
  { type: "box", label: "容器块", group: "容器", container: true, w: 200, h: 120, text: "" },
  { type: "card", label: "卡片", group: "容器", container: true, w: 280, h: 180, text: "卡片" },
  { type: "splitter", label: "分隔条", group: "容器", container: false, w: 6, h: 200, text: "" },
  { type: "terminal", label: "终端", group: "数据", container: false, w: 360, h: 200, text: "", listLike: true },
  /* 图表：listLike 是为了用「条目」编辑系列名；**不标 ranged** —— value 是窗口点数，
     不该被 min/max（Y 轴范围）夹，标了 ranged 画布就会拿 min/max 去夹 value */
  { type: "chart", label: "图表", group: "数据", container: false, w: 320, h: 160, text: "", listLike: true },
  /* 状态灯：20×20 的小灯，text 为空时灯占满整个矩形、有文字时灯在左文字在右。
     不标 ranged/checklist/listLike —— 三种都不是它的行为。 */
  { type: "led", label: "状态灯", group: "其它", container: false, w: 20, h: 20, text: "" },
  /* 仪表盘：和滑块一样是被 min/max 夹的值控件，画布显示「值/最小/最大」三行是对的。
     text 这里留空是为了和 k_kinds 的 def_text 逐字一致（上面那句"顺序与分组要和 k_kinds
     保持一致"是这张表的硬约定）；新控件的单位 "°C" 由 makeNode 的种子给，效果一样。 */
  { type: "meter", label: "仪表盘", group: "数值", container: false, w: 140, h: 140, text: "", ranged: true },
  { type: "separator", label: "分割线", group: "其它", container: false, w: 200, h: 2, text: "" },
  { type: "image", label: "图片", group: "其它", container: false, w: 240, h: 140, text: "" },
  { type: "shape", label: "外形框", group: "其它", container: false, w: 120, h: 120, text: "" },
  { type: "timer", label: "时钟", group: "其它", container: false, w: 80, h: 24, text: "" },
  { type: "canvas", label: "画板", group: "其它", container: false, w: 240, h: 160, text: "" },
  { type: "colorpicker", label: "颜色选择", group: "其它", container: false, w: 64, h: 28, text: "" },
  { type: "browser", label: "网页浏览", group: "其它", container: false, w: 320, h: 240, text: "" },
  { type: "badge", label: "徽章", group: "装饰", container: false, w: 48, h: 24, text: "99+" },
  { type: "tag", label: "标签片", group: "装饰", container: false, w: 72, h: 28, text: "标签" },
  { type: "rate", label: "评分", group: "装饰", container: false, w: 140, h: 32, text: "", ranged: true },
  { type: "pagination", label: "分页", group: "装饰", container: false, w: 180, h: 32, text: "", ranged: true },
];

export const KIND_MAP: Record<string, KindInfo> = Object.fromEntries(KINDS.map((k) => [k.type, k]));

export const isContainer = (type: string) => KIND_MAP[type]?.container === true;

/* --------------------------------------------------- 结构化内容的文本编辑
 * 画布上用多行文本编辑树/表格/条目，比做一套树形/表格编辑 UI 划算得多，
 * 而且用户一眼能看懂格式。
 */

/** 树：两空格缩进一层 */
export function treeToText(nodes: CgTreeItem[] | undefined, depth = 0): string {
  if (!nodes?.length) return "";
  const lines: string[] = [];
  for (const n of nodes) {
    lines.push(`${"  ".repeat(depth)}${n.text}`);
    const sub = treeToText(n.children, depth + 1);
    if (sub) lines.push(sub);
  }
  return lines.join("\n");
}

export function textToTree(text: string): CgTreeItem[] {
  const roots: CgTreeItem[] = [];
  const stack: { depth: number; item: CgTreeItem }[] = [];
  for (const raw of text.split(/\r?\n/)) {
    if (!raw.trim()) continue;
    const indent = raw.match(/^[ \t]*/)?.[0]?.replace(/\t/g, "  ").length ?? 0;
    const depth = Math.floor(indent / 2);
    const item: CgTreeItem = { text: raw.trim(), expanded: true };
    while (stack.length && stack[stack.length - 1].depth >= depth) stack.pop();
    if (!stack.length) roots.push(item);
    else {
      const parent = stack[stack.length - 1].item;
      parent.children = parent.children ?? [];
      parent.children.push(item);
    }
    stack.push({ depth, item });
  }
  return roots;
}

/** 菜单：两空格一层；`文本 | 菜单id`；`-` 单独一行是分隔线；末尾再加 `| v` 表示勾选 */
export function menuToText(nodes: CgTreeItem[] | undefined, depth = 0): string {
  if (!nodes?.length) return "";
  const pad = "  ".repeat(depth);
  const lines: string[] = [];
  for (const n of nodes) {
    if (n.text === "-") {
      lines.push(`${pad}-`);
      continue;
    }
    let line = `${pad}${n.text}`;
    if (n.id || n.checked) line += ` | ${n.id ?? ""}`;
    if (n.checked) line += " | v";
    lines.push(line);
    const sub = menuToText(n.children, depth + 1);
    if (sub) lines.push(sub);
  }
  return lines.join("\n");
}

export function textToMenu(text: string): CgTreeItem[] {
  const roots: CgTreeItem[] = [];
  const stack: { depth: number; item: CgTreeItem }[] = [];
  for (const raw of text.split(/\r?\n/)) {
    if (!raw.trim()) continue;
    const indent = (raw.match(/^[ \t]*/)?.[0] ?? "").replace(/\t/g, "  ").length;
    const depth = Math.floor(indent / 2);
    const body = raw.trim();
    let item: CgTreeItem;
    if (body === "-") {
      item = { text: "-" };
    } else {
      const parts = body.split("|").map((s) => s.trim());
      item = { text: parts[0] };
      if (parts.length > 1 && parts[1]) item.id = parts[1];
      if (parts.length > 2 && /^(v|✓|1|yes)$/i.test(parts[2])) item.checked = true;
    }
    while (stack.length && stack[stack.length - 1].depth >= depth) stack.pop();
    if (!stack.length) roots.push(item);
    else {
      const parent = stack[stack.length - 1].item;
      parent.children = parent.children ?? [];
      parent.children.push(item);
    }
    stack.push({ depth, item });
  }
  return roots;
}

/** 表格：第一行是列标题，其余是数据行，都用逗号分隔；列宽可写成 "标题:120" */
const splitCells = (line: string) => line.split(",").map((s) => s.trim());

export function tableColumnsToText(cols: CgColumn[] | undefined): string {
  if (!cols?.length) return "";
  return cols.map((c) => (c.w && c.w > 0 ? `${c.title}:${c.w}` : c.title)).join(",");
}

export function textToTableColumns(text: string): CgColumn[] {
  return splitCells(text)
    .filter((s) => s.length > 0)
    .map((s) => {
      const m = s.match(/^(.*):(\d+)$/);
      return m ? { title: m[1].trim(), w: Number(m[2]) } : { title: s };
    });
}

export function tableRowsToText(rows: string[][] | undefined): string {
  if (!rows?.length) return "";
  return rows.map((r) => r.join(",")).join("\n");
}

export function textToTableRows(text: string): string[][] {
  return text
    .split(/\r?\n/)
    .filter((l) => l.trim().length > 0)
    .map(splitCells);
}

export const itemsToText = (items: string[] | undefined) => (items ?? []).join("\n");
export const textToItems = (text: string) => text.split(/\r?\n/).filter((l) => l.length > 0);

/* ------------------------------------------------------------ 新建 */

let seed = 0;
export const newId = (base = "w") => {
  seed += 1;
  return `${base}_${seed}`;
};


export function makeNode(type: string, x: number, y: number): CgNode {
  const info = KIND_MAP[type] ?? KINDS[0];
  const node: CgNode = {
    id: `${type}_${++seed}`,
    type: info.type,
    rect: { x: Math.round(x), y: Math.round(y), w: info.w, h: info.h },
  };
  if (info.text) node.text = info.text;
  if (info.ranged) {
    node.value = type === "spinner" ? 5 : 40;
    node.min = 0;
    node.max = type === "spinner" ? 99 : 100;
  }
  if (info.container) node.children = [];

  /* 各类型的「内容」给个能看见的种子，空控件在画布上没法用 */
  if (type === "tabs") {
    node.items = ["页面一", "页面二"];
    node.value = 0;
  } else if (type === "statusbar") {
    node.items = ["就绪"];
  } else if (type === "listbox" || type === "combo") {
    node.items = ["第一项", "第二项", "第三项"];
  } else if (type === "tree") {
    node.nodes = [
      { text: "根节点", expanded: true, children: [{ text: "子节点" }] },
    ];
  } else if (type === "table") {
    node.columns = [{ title: "列 1", w: 120 }, { title: "列 2" }];
    node.rows = [
      ["A", "B"],
      ["C", "D"],
    ];
  } else if (type === "image" || type === "iconbutton") {
    node.src = "assets/picture.png";
  } else if (type === "monthcal") {
    node.text = todayText();
  } else if (type === "menu") {
    node.nodes = [
      { text: "文件", children: [{ text: "新建", id: "m_new" }, { text: "-" }, { text: "退出", id: "m_quit" }] },
      { text: "帮助", children: [{ text: "关于", id: "m_about" }] },
    ];
  } else if (type === "menubutton") {
    node.nodes = [
      { text: "第一项", id: "item_1" },
      { text: "第二项", id: "item_2" },
    ];
  } else if (type === "toolbar") {
    node.items = ["新建", "打开", "保存"];
    node.value = -1;
  } else if (type === "datepicker") {
    node.format = "date";
    node.text = todayText();
  } else if (type === "richtext") {
    node.text = "富文本内容";
  } else if (type === "terminal") {
    /* 给几行初始内容，画布上才看得出这是什么控件（运行时按 format/checked 重新着色） */
    node.items = [
      "[12:00:00] -- 就绪",
      "[12:00:01] TX AT+GMR",
      "[12:00:01] RX OK",
    ];
    node.checked = true;      /* 默认显示时间戳 */
  } else if (type === "chart") {
    node.value = 120;         /* 注意：value 在图表上是"窗口点数"，不是当前值 */
    node.min = 0;
    node.max = 100;
    node.items = ["系列一", "系列二"];   /* 两个系列，颜色按调色板自动分配 */
  } else if (type === "timer") {
    node.period = 1000;
  } else if (type === "colorpicker") {
    node.value = 0x6750a4;
  } else if (type === "rate") {
    node.value = 3;
    node.min = 0;
    node.max = 5;
  } else if (type === "pagination") {
    node.value = 0;
    node.min = 0;
    node.max = 9;
  } else if (type === "browser") {
    node.src = "about:blank";
  } else if (type === "shape") {
    node.format = "roundrect";
  } else if (type === "led") {
    node.value = 0;            /* 0 灭 / 1 亮 / 2 闪 */
    node.format = "round";     /* 缺省圆形；"square" 才是方的 */
    node.period = 500;         /* 闪烁周期（仅"闪"时有意义） */
    node.text = "状态";
  } else if (type === "meter") {
    /* value/min/max 已由上面的 ranged 分支给成 40 / 0~100 */
    node.format = "arc";       /* 缺省 270° 圆弧；"half" 才是半圆 */
    node.text = "°C";
  }
  return node;
}

/** 今天的日期，格式和运行时一致：2026-09-19 */
function todayText(): string {
  const d = new Date();
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

export function emptyDoc(): CgDoc {
  return {
    cgui: 1,
    /* 默认标题用 main：保存/导出的文件名都从它推（suggestedBaseName），
     * 所见即所得，避免中文标题推出乱七八糟的文件名 */
    app: { title: "main", width: 700, height: 500, bg: "#F3EDF7" },
    widgets: [],
  };
}

/* ------------------------------------------------------------ 新工程模板
 * 新手最怕的是一块空画布：给三个能直接跑的起点，每个都带好 id、中文文字和已绑好的事件。
 * 模板只是普通 CgDoc，建完可以随便改（也解释了"画布产物就是一份 JSON"）。
 */

export type TemplateKind = "blank" | "form" | "tray";

/** 造一个节点：id / 文字 / 位置尺寸一次给全 */
function tpl(type: string, id: string, x: number, y: number, w: number, h: number, text?: string): CgNode {
  const n = makeNode(type, x, y);
  n.id = id;
  n.rect = { x, y, w, h };
  if (text !== undefined) n.text = text;
  return n;
}

/** 按模板建一个新文档 */
export function templateDoc(kind: TemplateKind): CgDoc {
  if (kind === "blank") return emptyDoc();

  if (kind === "form") {
    /* 表单型：分组框里几行「标签 + 输入」，底下确定/取消。
     * 教的是最常用的套路：分组框当容器、id 用 控件名_用途、事件先绑好名字。 */
    const doc = emptyDoc();
    doc.app = { ...doc.app, title: "form", width: 520, height: 300 };
    const box = tpl("group", "grp_form", 20, 16, 480, 190, "填写信息");
    box.children = [
      tpl("label", "lab_name", 20, 34, 60, 24, "姓名"),
      tpl("edit", "edt_name", 90, 30, 360, 30),
      tpl("label", "lab_memo", 20, 84, 60, 24, "备注"),
      tpl("multiedit", "edt_memo", 90, 80, 360, 92),
    ];
    const ok = tpl("button", "btn_ok", 290, 226, 100, 38, "确定");
    ok.events = { click: "on_ok_click" };
    const cancel = tpl("button", "btn_cancel", 400, 226, 100, 38, "取消");
    cancel.events = { click: "on_cancel_click" };
    doc.widgets = [box, ok, cancel];
    return doc;
  }

  /* 托盘 + 日志型：常驻小工具的样子 —— 日志列表 + 开始/停止/清空 + 状态栏，
   * 窗口开 tray（托盘图标）和 close（点关闭先收进托盘，而不是退出）。 */
  const doc = emptyDoc();
  doc.app = {
    ...doc.app,
    title: "tool",
    width: 560,
    height: 380,
    tray: true,
    events: { close: "on_close" },
  };
  const start = tpl("button", "btn_start", 12, 12, 92, 32, "开始");
  start.events = { click: "on_start_click" };
  const stop = tpl("button", "btn_stop", 112, 12, 92, 32, "停止");
  stop.events = { click: "on_stop_click" };
  const clear = tpl("button", "btn_clear", 212, 12, 92, 32, "清空");
  clear.events = { click: "on_clear_click" };
  const sw = tpl("switch", "sw_auto", 440, 14, 108, 28, "自动滚动");
  sw.checked = true;
  const log = tpl("listbox", "lst_log", 12, 56, 536, 280);
  const status = tpl("statusbar", "sb_main", 0, 356, 560, 24);
  doc.widgets = [start, stop, clear, sw, log, status];
  return doc;
}

/* ------------------------------------------------------------ 遍历 */

export function walk(nodes: CgNode[], visit: (n: CgNode, parent: CgNode | null) => void, parent: CgNode | null = null) {
  for (const n of nodes) {
    visit(n, parent);
    if (n.children?.length) walk(n.children, visit, n);
  }
}

export function findNode(doc: CgDoc, id: string): CgNode | null {
  let hit: CgNode | null = null;
  walk(doc.widgets, (n) => {
    if (n.id === id) hit = n;
  });
  return hit;
}

export function findParent(doc: CgDoc, id: string): CgNode | null {
  let hit: CgNode | null = null;
  walk(doc.widgets, (n) => {
    if (n.children?.some((c) => c.id === id)) hit = n;
  });
  return hit;
}

export function siblings(doc: CgDoc, parent: CgNode | null): CgNode[] {
  return parent ? (parent.children ?? []) : doc.widgets;
}

export function listNodes(doc: CgDoc, parent: CgNode | null = null): CgNode[] {
  return parent ? (parent.children ?? []) : doc.widgets;
}

/** 从文档里摘掉一个节点，返回它（找不到返回 null） */
export function detach(doc: CgDoc, id: string): CgNode | null {
  const parent = findParent(doc, id);
  const list = siblings(doc, parent);
  const i = list.findIndex((n) => n.id === id);
  if (i < 0) return null;
  return list.splice(i, 1)[0];
}

/** 删除节点（含其子控件） */
export function removeNode(doc: CgDoc, id: string): boolean {
  return detach(doc, id) !== null;
}

/** 判断 nodeId 是否在 ancestorId 内部（含自身） */
export function isInside(doc: CgDoc, nodeId: string, ancestorId: string): boolean {
  if (nodeId === ancestorId) return true;
  const a = findNode(doc, ancestorId);
  if (!a?.children) return false;
  let found = false;
  walk(a.children, (n) => {
    if (n.id === nodeId) found = true;
  });
  return found;
}

/** 绝对坐标（把容器的偏移累加上去） */
export function absRect(doc: CgDoc, node: CgNode): CgRect {
  let x = node.rect.x;
  let y = node.rect.y;
  let p = findParent(doc, node.id);
  while (p) {
    x += p.rect.x;
    y += p.rect.y;
    p = findParent(doc, p.id);
  }
  return { x, y, w: node.rect.w, h: node.rect.h };
}

/** 命中测试：返回落点最深的容器（用于把控件放进分组框里） */
export function containerAt(doc: CgDoc, x: number, y: number, excludeId?: string): CgNode | null {
  let hit: CgNode | null = null;
  let bestDepth = -1;
  const visit = (nodes: CgNode[], depth: number) => {
    for (const n of nodes) {
      if (n.id === excludeId) continue;
      if (!isContainer(n.type)) continue;
      const r = absRect(doc, n);
      if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h && depth > bestDepth) {
        hit = n;
        bestDepth = depth;
      }
      if (n.children?.length) visit(n.children, depth + 1);
    }
  };
  visit(doc.widgets, 0);
  return hit;
}

/** 重新分配 id，保证全局唯一（粘贴/复制用） */
export function uniquifyIds(node: CgNode, taken: Set<string>) {
  if (taken.has(node.id) || !node.id) {
    const base = node.type;
    let n = 1;
    while (taken.has(`${base}_${n}`)) n += 1;
    node.id = `${base}_${n}`;
  }
  taken.add(node.id);
  for (const c of node.children ?? []) uniquifyIds(c, taken);
}

export function cloneNode(node: CgNode, dx = 16, dy = 16): CgNode {
  const copy = structuredClone(node);
  copy.rect = { ...copy.rect, x: copy.rect.x + dx, y: copy.rect.y + dy };
  return copy;
}

/* ------------------------------------------------------------ 读入校验
 * .cgui 是人手可编辑的文件，读到脏数据不能让画布崩。
 */
function coerceRect(raw: unknown, fallback: CgRect): CgRect {
  const o = (raw ?? {}) as Record<string, unknown>;
  const num = (v: unknown, d: number) => (typeof v === "number" && Number.isFinite(v) ? v : d);
  return {
    x: num(o.x, fallback.x),
    y: num(o.y, fallback.y),
    w: Math.max(0, num(o.w, fallback.w)),
    h: Math.max(0, num(o.h, fallback.h)),
  };
}

/** app.events：窗口级事件（ready/close/activate/keydown... 见 WINDOW_EVENTS） */
function appEvents(raw: unknown): Partial<Record<CgEvent, string>> | undefined {
  if (!raw || typeof raw !== "object") return undefined;
  const src = raw as Record<string, unknown>;
  const out: Partial<Record<CgEvent, string>> = {};
  for (const ev of WINDOW_EVENTS) {
    const v = src[ev];
    if (typeof v === "string" && v.trim()) out[ev] = v.trim();
  }
  return Object.keys(out).length ? out : undefined;
}

/** app.info：程序配置（版本信息）。只留非空字符串，其余丢掉 */
function coerceInfo(raw: unknown): CgInfo | undefined {
  if (!raw || typeof raw !== "object") return undefined;
  const src = raw as Record<string, unknown>;
  const out: CgInfo = {};
  for (const key of INFO_KEYS) {
    const v = src[key];
    if (typeof v === "string" && v.trim()) out[key] = v.trim();
  }
  return Object.keys(out).length ? out : undefined;
}

function coerceTree(raw: unknown[]): CgTreeItem[] {
  const out: CgTreeItem[] = [];
  for (const r of raw) {
    if (typeof r === "string") {
      out.push({ text: r });
      continue;
    }
    const o = (r ?? {}) as Record<string, unknown>;
    const item: CgTreeItem = { text: typeof o.text === "string" ? o.text : "" };
    if (o.expanded === true) item.expanded = true;
    if (typeof o.id === "string" && o.id) item.id = o.id;      /* 菜单项标识，丢了回调就拿不到 */
    if (o.checked === true) item.checked = true;                /* 菜单勾选态同理 */
    if (Array.isArray(o.children)) item.children = coerceTree(o.children);
    out.push(item);
  }
  return out;
}

function coerceNode(raw: unknown, index: number): CgNode | null {
  if (!raw || typeof raw !== "object") return null;
  const o = raw as Record<string, unknown>;
  const type = typeof o.type === "string" && KIND_MAP[o.type] ? o.type : "label";
  const info = KIND_MAP[type];
  const flat = { x: o.x, y: o.y, w: o.w, h: o.h } as Record<string, unknown>;
  const rectSource = (o.rect as unknown) ?? flat;
  const node: CgNode = {
    id: typeof o.id === "string" && o.id ? o.id : `${type}_${index + 1}`,
    type,
    rect: coerceRect(rectSource, { x: 0, y: 0, w: info.w, h: info.h }),
  };
  if (typeof o.text === "string") node.text = o.text;
  if (o.style && typeof o.style === "object") node.style = o.style as CgStyle;
  if (o.events && typeof o.events === "object") {
    const ev: Partial<Record<CgEvent, string>> = {};
    for (const key of CG_EVENTS) {
      const v = (o.events as Record<string, unknown>)[key];
      if (typeof v === "string" && v.trim()) ev[key] = v.trim();
    }
    if (Object.keys(ev).length) node.events = ev;
  }
  const state = (o.state ?? {}) as Record<string, unknown>;
  const pick = <T,>(a: unknown, b: unknown, d: T): T => {
    const v = a !== undefined ? a : b;
    return (typeof v === typeof d ? (v as T) : d);
  };
  node.visible = pick(o.visible, state.visible, true);
  node.enabled = pick(o.enabled, state.enabled, true);
  node.checked = pick(o.checked, state.checked, false);
  node.value = Number(o.value ?? state.value ?? (info.ranged ? 40 : 0));
  node.min = Number(o.min ?? state.min ?? 0);
  node.max = Number(o.max ?? state.max ?? 100);

  /* 标签页归属 / 图片路径 / 细分格式 / 悬停提示 / 时钟周期 / 表格增强 */
  if (Number.isFinite(Number(o.page))) node.page = Number(o.page);
  if (typeof o.src === "string") node.src = o.src;
  if (typeof o.format === "string") node.format = o.format;
  if (typeof o.hint === "string" && o.hint.trim()) node.hint = o.hint;
  if (Number.isFinite(Number(o.period))) node.period = Math.max(0, Math.round(Number(o.period)));
  if (o.multi === true) node.multi = true;
  if (o.check === true) node.check = true;

  /* 控件绑定：src 空串或不是字符串一律当没绑（画布上「不绑定」就是这么存的） */
  if (o.bind && typeof o.bind === "object") {
    const b = o.bind as Record<string, unknown>;
    if (typeof b.src === "string" && b.src.trim()) {
      const bind: CgBind = { src: b.src.trim() };
      if (b.two === true) bind.two = true;
      node.bind = bind;
    }
  }

  /* 结构化内容：读进来时做形状校验，脏数据丢了总比画布崩了好 */
  if (Array.isArray(o.items)) {
    node.items = o.items.map((s) => (typeof s === "string" ? s : String(s ?? "")));
  }
  if (Array.isArray(o.nodes)) node.nodes = coerceTree(o.nodes);
  if (Array.isArray(o.columns)) {
    node.columns = o.columns.map((c) => {
      if (typeof c === "string") return { title: c };
      const cc = (c ?? {}) as Record<string, unknown>;
      const col: CgColumn = { title: typeof cc.title === "string" ? cc.title : "" };
      if (Number.isFinite(Number(cc.w))) col.w = Number(cc.w);
      return col;
    });
  }
  if (Array.isArray(o.rows)) {
    node.rows = o.rows.map((r) =>
      Array.isArray(r) ? r.map((c) => (typeof c === "string" ? c : String(c ?? ""))) : [],
    );
  }

  if (Array.isArray(o.children)) {
    const kids = o.children.map((c, i) => coerceNode(c, i)).filter((c): c is CgNode => c !== null);
    if (kids.length) {
      if (!info.container) {
        // 非容器带了 children：挂到同级，别丢用户的控件
        node.children = undefined;
        return node;
      }
      node.children = kids;
    }
  }
  return node;
}

export function parseDoc(text: string): { doc?: CgDoc; error?: string } {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch (e) {
    return { error: `不是合法的 JSON：${(e as Error).message}` };
  }
  if (!raw || typeof raw !== "object") return { error: "顶层必须是一个对象" };
  const o = raw as Record<string, unknown>;
  if (o.cgui !== undefined && Number(o.cgui) > 1) {
    return { error: `不支持 .cgui 版本 ${String(o.cgui)}（当前画布只认 1）` };
  }
  const app = (o.app ?? {}) as Record<string, unknown>;
  if (!Array.isArray(o.widgets)) return { error: "缺少 widgets 数组" };
  const winEvents = appEvents(app.events);

  const appOut: CgApp = {
    title: typeof app.title === "string" ? app.title : "main",
    width: Number(app.width) > 0 ? Number(app.width) : 700,
    height: Number(app.height) > 0 ? Number(app.height) : 500,
    bg: typeof app.bg === "string" ? app.bg : "#F3EDF7",
  };

  /* 窗口属性：只把 .cgui 里真写了的字段收进来，缺省留给 C 侧 */
  const numOr = (v: unknown): number | undefined => {
    if (v === undefined || v === null || v === "" || !Number.isFinite(Number(v))) return undefined;
    return Math.round(Number(v));
  };
  const boolOr = (v: unknown): boolean | undefined => (typeof v === "boolean" ? v : undefined);
  const strOr = (v: unknown): string | undefined =>
    typeof v === "string" && v.trim() ? v.trim() : undefined;

  const ax = numOr(app.x), ay = numOr(app.y);
  if (ax !== undefined) appOut.x = ax;
  if (ay !== undefined) appOut.y = ay;

  const border = app.border;
  if (border === "sizable" || border === "fixed" || border === "dialog" || border === "none") {
    appOut.border = border;
  }

  const flagKeys = [
    "center", "controlBox", "minimizeBox", "maximizeBox", "topmost", "taskbar",
    "visible", "enabled", "enterTab", "escClose", "tray",
  ] as const;
  for (const k of flagKeys) {
    const b = boolOr(app[k]);
    if (b !== undefined) Object.assign(appOut, { [k]: b });
  }

  const strKeys = ["icon", "cursor", "class", "note"] as const;
  for (const k of strKeys) {
    const s = strOr(app[k]);
    if (s !== undefined) Object.assign(appOut, { [k]: s });
  }

  const info = coerceInfo(app.info);
  if (info) appOut.info = info;
  if (winEvents) appOut.events = winEvents;

  const doc: CgDoc = {
    cgui: 1,
    app: appOut,
    widgets: o.widgets.map((w, i) => coerceNode(w, i)).filter((w): w is CgNode => w !== null),
  };
  return { doc };
}

export function serializeDoc(doc: CgDoc): string {
  return `${JSON.stringify(cleanDoc(doc), null, 2)}\n`;
}

function cleanTree(item: CgTreeItem): CgTreeItem {
  const out: CgTreeItem = { text: item.text };
  if (item.id) out.id = item.id;
  if (item.expanded) out.expanded = true;
  if (item.checked) out.checked = true;
  if (item.children?.length) out.children = item.children.map(cleanTree);
  return out;
}

/** 去掉 undefined 和空字段，让存出来的文件干净、可读、diff 友好 */
function cleanNode(node: CgNode): CgNode {
  const out: CgNode = {
    id: node.id,
    type: node.type,
    rect: {
      x: Math.round(node.rect.x),
      y: Math.round(node.rect.y),
      w: Math.round(node.rect.w),
      h: Math.round(node.rect.h),
    },
  };
  if (node.text) out.text = node.text;
  if (node.page !== undefined && node.page > 0) out.page = Math.round(node.page);
  if (node.src) out.src = node.src;
  if (
    node.format &&
    ((node.type === "datepicker" && node.format !== "date") ||
      node.type === "shape" ||
      /* 分隔条缺省就是左右分栏（竖条），只有 "row" 需要写出去 */
      (node.type === "splitter" && node.format !== "col") ||
      /* 终端缺省是文本模式，只有 "hex" 需要写出去 */
      (node.type === "terminal" && !!node.format) ||
      /* 状态灯缺省是圆的，只有 "square" 需要写出去 */
      (node.type === "led" && node.format !== "round") ||
      /* 仪表盘缺省是 270° 圆弧，只有 "half" 需要写出去 */
      (node.type === "meter" && node.format !== "arc"))
  ) {
    out.format = node.format;
  }
  if (node.hint) out.hint = node.hint;
  if (node.period) out.period = Math.max(0, Math.round(node.period));
  if (node.multi === true) out.multi = true;
  if (node.check === true) out.check = true;
  if (node.bind?.src) {
    out.bind = node.bind.two ? { src: node.bind.src, two: true } : { src: node.bind.src };
  }
  if (node.items && node.items.length) out.items = [...node.items];
  if (node.nodes && node.nodes.length) out.nodes = node.nodes.map(cleanTree);
  if (node.columns && node.columns.length) {
    out.columns = node.columns.map((c) => (c.w && c.w > 0 ? { title: c.title, w: Math.round(c.w) } : { title: c.title }));
  }
  if (node.rows && node.rows.length) out.rows = node.rows.map((r) => [...r]);
  const style = node.style ?? {};
  const kept: CgStyle = {};
  if (style.font) kept.font = style.font;
  if (style.size) kept.size = style.size;
  if (style.bold) kept.bold = true;
  if (style.fg) kept.fg = style.fg;
  if (style.bg) kept.bg = style.bg;
  if (style.align && style.align !== "left") kept.align = style.align;
  if (style.radius) kept.radius = style.radius;
  if (Object.keys(kept).length) out.style = kept;

  const flagged = (v: boolean | undefined, d: boolean) => v !== undefined && v !== d;
  if (flagged(node.visible, true)) out.visible = node.visible;
  if (flagged(node.enabled, true)) out.enabled = node.enabled;
  if (flagged(node.checked, false)) out.checked = node.checked;
  if (KIND_MAP[node.type]?.ranged) {
    out.value = node.value ?? 0;
    out.min = node.min ?? 0;
    out.max = node.max ?? 100;
  } else if (node.type === "colorpicker") {
    out.value = node.value ?? 0;   /* value 存颜色 0xRRGGBB */
  } else if (node.type === "chart") {
    /* 图表的 value 是窗口点数、min/max 是 Y 轴范围，三者都要存；而且 value 不能被 min/max
       夹 —— "窗口 120 个点 + Y 范围 0~100" 是正常配置，标成 ranged 就会被画布夹成 100 */
    out.value = Math.max(2, Math.round(node.value ?? 120));
    out.min = node.min ?? 0;
    out.max = node.max ?? 100;
  } else if (node.type === "led") {
    /* 状态灯的 value 是 0 灭 / 1 亮 / 2 闪 —— 0 也是合法状态，必须一直写出去，
       不能像 checked 那样"等于默认值就不写"（那会把"灭"变成"没配过"） */
    const st = typeof node.value === "number" && Number.isFinite(node.value) ? Math.round(node.value) : 0;
    out.value = Math.max(0, Math.min(2, st));
  } else if (KIND_MAP[node.type]?.listLike && node.value) {
    /* tabs/listbox/combo 的当前选中项：不存的话重开文件永远回第 0 页/第 0 项 */
    out.value = Math.max(0, Math.round(node.value));
  }
  if (node.events && Object.keys(node.events).length) {
    out.events = { ...node.events };
  }
  if (node.children?.length) out.children = node.children.map(cleanNode);
  else if (KIND_MAP[node.type]?.container) out.children = [];
  return out;
}

function cleanDoc(doc: CgDoc): CgDoc {
  const a = doc.app;
  const app: CgApp = {
    title: a.title,
    width: Math.round(a.width),
    height: Math.round(a.height),
    bg: a.bg,
  };

  /* 只写和 C 侧默认值不同的字段：文件干净、老运行时读新文件也不会被多余字段干扰 */
  if (a.x !== undefined) app.x = Math.round(a.x);
  if (a.y !== undefined) app.y = Math.round(a.y);
  if (a.center === true) app.center = true;
  if (a.border && a.border !== "sizable") app.border = a.border;
  if (a.controlBox === false) app.controlBox = false;
  if (a.minimizeBox === false) app.minimizeBox = false;
  if (a.maximizeBox === false) app.maximizeBox = false;
  if (a.topmost === true) app.topmost = true;
  if (a.taskbar === false) app.taskbar = false;
  if (a.visible === false) app.visible = false;
  if (a.enabled === false) app.enabled = false;
  if (a.enterTab === true) app.enterTab = true;
  if (a.escClose === false) app.escClose = false;
  if (a.tray === true) app.tray = true;
  if (a.icon) app.icon = a.icon;
  if (a.cursor) app.cursor = a.cursor;
  if (a.class) app.class = a.class;
  if (a.note) app.note = a.note;

  if (a.info) {
    const info: CgInfo = {};
    for (const key of INFO_KEYS) {
      const v = a.info[key];
      if (v) info[key] = v;
    }
    if (Object.keys(info).length) app.info = info;
  }

  if (a.events) {
    const ev: Partial<Record<CgEvent, string>> = {};
    for (const key of WINDOW_EVENTS) {
      const v = a.events[key];
      if (v) ev[key] = v;
    }
    if (Object.keys(ev).length) app.events = ev;
  }

  return { cgui: 1, app, widgets: doc.widgets.map(cleanNode) };
}

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
export const snap = (v: number, grid: number, on: boolean) => (on ? Math.round(v / grid) * grid : Math.round(v));
