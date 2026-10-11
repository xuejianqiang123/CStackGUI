/* model/serialize.ts -- 写出 .cgui（去空字段、可读、diff 友好）
 *
 * serializeDoc 把 CgDoc 变成 JSON 文本；clean* 系列负责把 undefined 和「等于 C 侧默认值」
 * 的字段剔掉，让存出来的文件干净、老运行时读新文件也不被多余字段干扰。
 *
 * 依赖：model/types.ts + model/kinds.ts（KIND_MAP / isNumEdit）+ model/events.ts（WINDOW_EVENTS）。
 */

import { WINDOW_EVENTS, type CgEvent } from "./events.js";
import { KIND_MAP, isNumEdit } from "./kinds.js";
import { isSafeStyleName } from "./style.js";
import {
  INFO_KEYS,
  isSafeActionName,
  type CgAction,
  type CgApp,
  type CgDoc,
  type CgInclude,
  type CgInfo,
  type CgNode,
  type CgRole,
  type CgStyle,
  type CgStyleRef,
  type CgStyleValue,
  type CgTreeItem,
} from "./types.js";

export function serializeDoc(doc: CgDoc): string {
  return `${JSON.stringify(cleanDoc(doc), null, 2)}\n`;
}

function cleanTree(item: CgTreeItem): CgTreeItem {
  const out: CgTreeItem = { text: item.text };
  if (item.id) out.id = item.id;
  if (item.action) out.action = item.action;
  if (item.expanded) out.expanded = true;
  if (item.checked) out.checked = true;
  if (item.children?.length) out.children = item.children.map(cleanTree);
  return out;
}

/** 动作表：只写填过的字段（缺省值不写，和 cleanNode 一个口径）。
 *  一个字段都没填的动作仍然写出 `{}` —— 它可能已经被菜单项引用着，
 *  整条丢掉会让引用变成「不存在的动作」。 */
function cleanActions(actions: Record<string, CgAction> | undefined): Record<string, CgAction> | undefined {
  if (!actions) return undefined;
  const out: Record<string, CgAction> = {};
  for (const [name, a] of Object.entries(actions)) {
    if (!isSafeActionName(name.trim())) continue;
    const item: CgAction = {};
    if (a.text) item.text = a.text;
    if (a.accel) item.accel = a.accel;
    if (a.handler) item.handler = a.handler;
    if (a.checked === true) item.checked = true;
    if (a.enabled === false) item.enabled = false;
    out[name] = item;
  }
  return Object.keys(out).length ? out : undefined;
}

/** 控件 style：原样保住三种形态（内联 / "名字" / { use, ...覆盖 }），只剔掉空字符串与不合法字段。
 *  - 字符串形态：空串丢掉（"没有名字" = 没写）；
 *  - **带 use 的对象**：写了的字段一律保留（含 bold:false / size:0 / align:"left" / radius:0）——
 *    它们是"覆盖"语义，丢了会被命名样式里的值顶回去（契约 §2 的合并规则）；
 *  - 不带 use 的内联对象：等于缺省值的字段不写（老口径，保持文件干净、老文件行为不变）。
 *  注意：这里**不做形态归并**（{ "use": "x" } 不会写成 "x"），否则手写文件的往返会变。 */
function cleanStyleValue(style: CgStyleValue | undefined): CgStyleValue | undefined {
  if (style === undefined) return undefined;
  if (typeof style === "string") {
    const s = style.trim();
    return s || undefined;
  }
  const use = typeof style.use === "string" ? style.use.trim() : "";
  const keepDefaults = !!use;
  const out: CgStyleRef = {};
  if (use) out.use = use;
  if (style.font) out.font = style.font;
  /* "写了就覆盖"（有 use）：任何写了的数字/布尔都要落盘，包括 0、负数、false —— 运行时靠
   * 字段存在位掩码判（cg_doc.c 的 style_overlay），少了这一条命名样式的值会顶回来。 */
  if (style.size !== undefined && (style.size > 0 || keepDefaults)) out.size = style.size;
  if (style.bold === true || (keepDefaults && style.bold === false)) out.bold = style.bold === true;
  if (style.fg) out.fg = style.fg;
  if (style.bg) out.bg = style.bg;
  if (style.align === "center" || style.align === "right" || (keepDefaults && style.align === "left")) {
    out.align = style.align;
  }
  if (style.radius !== undefined && (style.radius > 0 || keepDefaults)) {
    out.radius = style.radius;
  }
  return Object.keys(out).length ? out : undefined;
}

/** 顶层命名样式表：非法名丢掉；一个字段都没有的条目**保留**（可能已被控件引用着）；
 *  字段按"缺省不写"剔（命名样式是打底，缺省值运行时自己兜底，写不写结果一样）。 */
function cleanStyles(styles: Record<string, CgStyle> | undefined): Record<string, CgStyle> | undefined {
  if (!styles) return undefined;
  const out: Record<string, CgStyle> = {};
  for (const [name, s] of Object.entries(styles)) {
    const key = name.trim();
    if (!isSafeStyleName(key)) continue;
    const item: CgStyle = {};
    if (s.font) item.font = s.font;
    if (s.size) item.size = s.size;
    if (s.bold === true) item.bold = true;
    if (s.fg) item.fg = s.fg;
    if (s.bg) item.bg = s.bg;
    if (s.align && s.align !== "left") item.align = s.align;
    if (s.radius) item.radius = s.radius;
    out[key] = item;
  }
  return Object.keys(out).length ? out : undefined;
}

/** 顶层色彩角色表（roles{}）：非法名丢掉；light/dark 字符串**原样保留**（画布不做色值校验，
 *  运行时容错同口径）；两个槽都没写的条目丢掉（运行时也是跳过它）。 */
function cleanRoles(roles: Record<string, CgRole> | undefined): Record<string, CgRole> | undefined {
  if (!roles) return undefined;
  const out: Record<string, CgRole> = {};
  for (const [name, r] of Object.entries(roles)) {
    const key = name.trim();
    if (!isSafeStyleName(key)) continue;
    const item: CgRole = {};
    if (typeof r.light === "string" && r.light) item.light = r.light;
    if (typeof r.dark === "string" && r.dark) item.dark = r.dark;
    if (item.light || item.dark) out[key] = item;
  }
  return Object.keys(out).length ? out : undefined;
}

/** 顶层 include[]（F3）：path 必须非空（trim 后），pick 可选。
 *  这层**只保真**：展开由运行时在加载期做（f3-window-ref-prd.md）—— 画布在这里展开的话，
 *  保存时就得想办法把引用来的子树摘掉，反而会引入"静默丢 / 重复一份"的风险。 */
function cleanInclude(include: CgInclude[] | undefined): CgInclude[] | undefined {
  if (!include || !include.length) return undefined;
  const out: CgInclude[] = [];
  for (const it of include) {
    const path = typeof it?.path === "string" ? it.path.trim() : "";
    if (!path) continue;
    const one: CgInclude = { path };
    if (typeof it.pick === "string" && it.pick.trim()) one.pick = it.pick;
    out.push(one);
  }
  return out.length ? out : undefined;
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
  /* 余量分配：0 与不写等价（缺省不写，和本项目其它缺省字段一个口径） */
  if (node.grow && node.grow > 0) out.grow = Math.max(1, Math.min(99, Math.round(node.grow)));
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
  /* serial：非缺省才写（与 C 侧写回口径一致；auto 只有显式 === true 才写） */
  if (node.port && node.port !== "COM1") out.port = node.port;
  if (node.baud && node.baud !== 9600) out.baud = node.baud;
  if (node.bits && node.bits !== 8) out.bits = node.bits;
  if (node.stop && node.stop !== 1) out.stop = node.stop;
  if (node.parity && node.parity !== 0) out.parity = node.parity;
  if (node.auto === true) out.auto = true;
  if (node.multi === true) out.multi = true;
  if (node.check === true) out.check = true;
  /* dialog：缺省居中不写；只有"不居中"才落盘（同一句"缺省不写"的口径） */
  if (node.center === false) out.center = false;
  /* 数值框（edit + 调节器）：value 是真相、文本由它派生，所以值/范围/文本一起写出去。
   * edit 本身不是 ranged 控件（上面那段会跳过 min/max），漏了这三行运行时夹取范围
   * 就永远是缺省的 0~100，而且显示的还是空的。 */
  if (isNumEdit(node)) {
    out.value = Math.round(node.value ?? 0);
    out.min = Math.round(node.min ?? 0);
    out.max = Math.round(node.max ?? 100);
  }
  /* edit 的输入框行为：等于默认（单行 / 无滚动条 / 不限长 / 不转换 / 通常方式）就不写 */
  if (node.multiline === true) out.multiline = true;
  if (node.scroll) out.scroll = node.scroll;
  if (node.maxlen) out.maxlen = node.maxlen;
  if (node.conv) out.conv = node.conv;
  if (node.input) out.input = node.input;
  if (node.border) out.border = node.border;
  if (node.spin) out.spin = node.spin;
  /* 引用动作：只有真填了才写（旧的 .cgui 里没有这个字段，行为完全不变） */
  if (node.action) out.action = node.action;
  if (node.bind?.src) {
    out.bind = node.bind.two ? { src: node.bind.src, two: true } : { src: node.bind.src };
  }
  if (node.items && node.items.length) out.items = [...node.items];
  if (node.nodes && node.nodes.length) out.nodes = node.nodes.map(cleanTree);
  if (node.columns && node.columns.length) {
    out.columns = node.columns.map((c) => (c.w && c.w > 0 ? { title: c.title, w: Math.round(c.w) } : { title: c.title }));
  }
  if (node.rows && node.rows.length) out.rows = node.rows.map((r) => [...r]);
  const style = cleanStyleValue(node.style);
  if (style !== undefined) out.style = style;

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
  /* 只给容器写 children。正常情况下画布内存里不会有"非容器带 children"
   * （拖放走 isContainer 判断、读入侧也会丢掉，见 model/parse.ts），
   * 但这一条是**文件能不能被运行时读进去**的底线：运行时对"非容器带 children"
   * 是硬错误，会把**整个控件**丢掉（cg_doc.c 的节点解析）。
   * 万一哪天上游的不变量破了，这里退化成"少几个子节点"而不是"整份文件读不进"。 */
  if (node.children?.length && KIND_MAP[node.type]?.container) {
    out.children = node.children.map(cleanNode);
  } else if (KIND_MAP[node.type]?.container) {
    out.children = [];
  }
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
  /* 界面主题（契约 §3）：false = 缺省（浅色），不写；老 .cgui 读进来外观完全不变 */
  if (a.dark === true) app.dark = true;
  else if (a.dark === "auto") app.dark = "auto";
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

  /* 动作表、样式表紧跟在 app 后面：顶层四个东西，读起来是「窗口 → 命令 → 样式 → 界面」。
   * include[]（F3）放 widgets 前面：它展开出来的就是 widgets 的一部分。 */
  const actions = cleanActions(doc.actions);
  const styles = cleanStyles(doc.styles);
  const roles = cleanRoles(doc.roles);
  const include = cleanInclude(doc.include);
  return {
    cgui: 1,
    app,
    ...(actions ? { actions } : {}),
    ...(styles ? { styles } : {}),
    ...(roles ? { roles } : {}),
    ...(include ? { include } : {}),
    widgets: doc.widgets.map(cleanNode),
  };
}
