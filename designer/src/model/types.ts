/* model/types.ts -- .cgui 文档的类型总集（模型层最底层）
 *
 * 这里放文档模型的所有「形状定义」：文档 / 节点 / 窗口属性 / 样式 / 边框 / 程序配置，
 * 外加几张纯常量表（CURSORS / BORDER_LABEL / INFO_KEYS）。模型层其它文件都 import 它。
 *
 * 这里的类型必须和 runtime/src/core/cg_doc.c 的解析逻辑一一对应：
 * 改这里就要同步改那边，否则「画布能存、运行时读不了」。
 *
 * 依赖说明：本文件对模型层其它文件没有运行时依赖。唯一一处 import 是下面 CgEvent 的
 * type-only 引入 —— 它编译后被完全擦除（types.ts 的产物里没有任何 import），
 * 因此既无运行时依赖、也无依赖环（见 model/events.ts 顶部说明）。
 */

import type { CgEvent } from "./events.js";

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

/** 动作（actions）：一条命名命令 = 文字 + 快捷键 + 一个回调名。
 *  菜单项与控件用 `action` 引用它，于是「一个回调、一处启用状态」同时管住所有引用点。
 *  真相在 runtime/src/core/cg_action.c；字段语义见 cstackgui.h 的「动作（actions）」一节。 */
export type CgAction = {
  /** 引用它的菜单项 / 控件显示的文字（空 = 用引用点自己的 text） */
  text?: string;
  /** 快捷键，如 "Ctrl+S"（空 = 无）。不允许无修饰键的单个字母/数字 */
  accel?: string;
  /** C 侧 CG_BIND 的名字（空 → 该动作被忽略 + 启动 WARN + --selftest 报错） */
  handler?: string;
  /** 初始勾选状态（同步给引用它的菜单项），缺省 false */
  checked?: boolean;
  /** 初始启用状态，缺省 true */
  enabled?: boolean;
};

/** 能安全当记录键（`actions` 的键）的名字：`__proto__` 这类赋值会改到原型上，一律当非法名 */
export const isSafeActionName = (name: string): boolean =>
  !!name && name !== "__proto__" && name !== "constructor" && name !== "prototype";

/** 树节点 / 菜单项（对应 .cgui 的 nodes 字段，可嵌套）。
 *  和 C 侧 cg_item_t 一样共用一个结构：树用 text/expanded，菜单用 text/id/checked。
 *  菜单里 text 写 "-" 表示分隔线。 */
export type CgTreeItem = {
  text: string;
  /** 菜单项标识（回调里用 cg_menu_last_id 取回） */
  id?: string;
  /** 菜单：引用顶层 actions 里的动作名（树节点不用这个字段） */
  action?: string;
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
  /** 引用顶层 actions 里的动作名：点击走动作的回调。
   *  **只有支持 click 的类型**能用（看 model/events.ts 的 KIND_EVENTS）；
   *  写了它就不能再绑 events.click（运行时忽略 click，--selftest 还会报错）。 */
  action?: string;
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
  /** serial 的串口名（如 "COM1"），缺省 "COM1" */
  port?: string;
  /** serial 的波特率，缺省 9600 */
  baud?: number;
  /** serial 数据位（5..8），缺省 8 */
  bits?: number;
  /** serial 停止位（1..2），缺省 1 */
  stop?: number;
  /** serial 校验 0=无 / 1=奇 / 2=偶，缺省 0 */
  parity?: number;
  /** serial 自动启动（缺省 false；JSON 里写 auto: true 才算开） */
  auto?: boolean;
  /** table：允许多选行 */
  multi?: boolean;
  /** table：显示行首复选框列 */
  check?: boolean;
  /** edit：允许多行（Enter 变换行） */
  multiline?: boolean;
  /** edit：滚动条 1 横向 / 2 纵向 / 3 双向（缺省 0 = 无） */
  scroll?: number;
  /** edit：最多允许输入多少字符（缺省 0 = 不限制） */
  maxlen?: number;
  /** edit：输入时的大小写转换 1 大写转小写 / 2 小写转大写（缺省 0 = 不转换） */
  conv?: number;
  /** edit：输入方式 0 通常 / 1 只读 / 2 密码 / 3 整数文本 / 4 小数文本 /
   *  5 字节 / 6 短整数 / 7 整数 / 8 长整数 / 9 小数 / 10 双精度小数 / 11 日期时间 */
  input?: number;
  /** edit：边框 1 无边框 / 2 凹入式 / 3 凸出式 / 4 浅凹入式 / 5 镜框式 / 6 单线边框式
   *  （缺省 0 = 本工程原本的 M3 圆角描边） */
  border?: number;
  /** edit：右侧上下调节器 1 自动（按 ±1 改文本并夹到 min/max）/ 2 手动（只发 spin 事件） */
  spin?: number;
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
  /** 命名动作表：动作名 -> 动作（文字 / 快捷键 / 回调）。缺省 = 没有动作，老 .cgui 行为不变 */
  actions?: Record<string, CgAction>;
  widgets: CgNode[];
};
