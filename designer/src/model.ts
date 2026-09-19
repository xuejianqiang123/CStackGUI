/* model.ts -- .cgui 文档模型
 *
 * 这里的类型必须和 runtime/src/cg_doc.c 的解析逻辑一一对应：
 * 改这里就要同步改那边，否则「画布能存、运行时读不了」。
 */

export const CG_EVENTS = ["click", "dblclick", "change", "submit", "select", "release", "expand", "collapse"] as const;
export type CgEvent = (typeof CG_EVENTS)[number];

export const EVENT_LABEL: Record<CgEvent, string> = {
  click: "单击",
  dblclick: "双击",
  change: "内容/值变化",
  submit: "回车提交",
  select: "选择变化",
  release: "拖动结束",
  expand: "展开节点",
  collapse: "折叠节点",
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
};

/** 每种控件支持哪些事件 —— 必须和 runtime/src/cg_doc.c 的 k_kinds 表一一对应。
 *  事件是按控件特性设计的：标签没有交互、进度条只能被程序写、只有树有展开折叠。
 *  画布只列这里声明的事件；C 侧也会校验，绑错了启动时会警告。 */
export const KIND_EVENTS: Record<string, CgEvent[]> = {
  button: ["click", "dblclick"],
  iconbutton: ["click", "dblclick"],
  link: ["click", "dblclick"],
  label: [],
  switch: ["change"],
  edit: ["change", "submit"],
  multiedit: ["change"],
  richtext: ["change"],
  spinner: ["change"],
  datepicker: ["change"],
  monthcal: ["select", "dblclick"],
  checkbox: ["change"],
  radio: ["change"],
  listbox: ["select", "dblclick"],
  combo: ["select"],
  slider: ["change", "release"],
  progress: [],
  progressring: [],
  tabs: ["select"],
  tree: ["select", "dblclick", "expand", "collapse"],
  table: ["select", "dblclick"],
  statusbar: [],
  menu: ["click"],
  menubutton: ["click"],
  toolbar: ["click"],
  group: [],
  box: [],
  separator: [],
  image: ["click", "dblclick"],
};

/** 这个控件支持的事件；没登记的按"不支持任何事件"处理（C 侧同样会拒绝） */
export const eventsOf = (type: string): CgEvent[] => KIND_EVENTS[type] ?? [];

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

export type CgNode = {
  id: string;
  type: string;
  rect: CgRect;
  text?: string;
  style?: CgStyle;
  events?: Partial<Record<CgEvent, string>>;
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
  /** datepicker 的细分格式：date / time / datetime */
  format?: string;
  /** listbox / combo / tabs / statusbar 的文本条目 */
  items?: string[];
  /** 树 */
  nodes?: CgTreeItem[];
  /** 表格 */
  columns?: CgColumn[];
  rows?: string[][];
  children?: CgNode[];
};

export type CgDoc = {
  cgui: 1;
  app: {
    title: string;
    width: number;
    height: number;
    bg: string;
    /** 窗口级事件（目前只有 close）；绑定窗口关闭前要做的事 */
    events?: { close?: string };
  };
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

/* 顺序与分组要和 runtime/src/cg_doc.c 的 k_kinds 表保持一致 */
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
  { type: "separator", label: "分割线", group: "其它", container: false, w: 200, h: 2, text: "" },
  { type: "image", label: "图片", group: "其它", container: false, w: 240, h: 140, text: "" },
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

/** app.events：窗口级事件，目前只有 close */
function appEvents(raw: unknown): { close?: string } | undefined {
  if (!raw || typeof raw !== "object") return undefined;
  const close = (raw as Record<string, unknown>).close;
  return typeof close === "string" && close.trim() ? { close: close.trim() } : undefined;
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

  /* 标签页归属 / 图片路径 / 细分格式 */
  if (Number.isFinite(Number(o.page))) node.page = Number(o.page);
  if (typeof o.src === "string") node.src = o.src;
  if (typeof o.format === "string") node.format = o.format;

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

  const doc: CgDoc = {
    cgui: 1,
    app: {
      title: typeof app.title === "string" ? app.title : "main",
      width: Number(app.width) > 0 ? Number(app.width) : 700,
      height: Number(app.height) > 0 ? Number(app.height) : 500,
      bg: typeof app.bg === "string" ? app.bg : "#F3EDF7",
      ...(winEvents ? { events: winEvents } : {}),
    },
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
  if (node.format && node.type === "datepicker" && node.format !== "date") out.format = node.format;
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
  }
  if (node.events && Object.keys(node.events).length) {
    out.events = { ...node.events };
  }
  if (node.children?.length) out.children = node.children.map(cleanNode);
  else if (KIND_MAP[node.type]?.container) out.children = [];
  return out;
}

function cleanDoc(doc: CgDoc): CgDoc {
  return {
    cgui: 1,
    app: {
      title: doc.app.title,
      width: Math.round(doc.app.width),
      height: Math.round(doc.app.height),
      bg: doc.app.bg,
      ...(doc.app.events?.close ? { events: { close: doc.app.events.close } } : {}),
    },
    widgets: doc.widgets.map(cleanNode),
  };
}

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
export const snap = (v: number, grid: number, on: boolean) => (on ? Math.round(v / grid) * grid : Math.round(v));
