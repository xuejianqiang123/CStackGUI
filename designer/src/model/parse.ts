/* model/parse.ts -- 读入 .cgui（粘贴克隆 + 脏数据容错）
 *
 * 两件事：①粘贴/复制用的节点克隆与 id 重排（uniquifyIds / cloneNode）；
 * ②parseDoc 把 JSON 文本读成 CgDoc，中间一串 coerce* 负责把手工编辑出来的脏值收敛回合法形状。
 *
 * 依赖：model/types.ts + model/kinds.ts（KIND_MAP 兜底类型）+ model/events.ts（事件白名单）。
 */

import { CG_EVENTS, WINDOW_EVENTS, type CgEvent } from "./events.js";
import { KIND_MAP } from "./kinds.js";
import {
  INFO_KEYS,
  type CgApp,
  type CgBind,
  type CgColumn,
  type CgDoc,
  type CgInfo,
  type CgNode,
  type CgRect,
  type CgStyle,
  type CgTreeItem,
} from "./types.js";

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
  /* 老类型名自动升级：multiedit / spinner 已合并进 edit（2026-09-22）。
     映射必须和运行时 cg_doc.c 的 parse_node 完全一致 —— 否则老 .cgui 在画布上会被
     下面那句兜底静默降级成 label（结构和事件全丢，还不报错）。 */
  let rawType = typeof o.type === "string" ? o.type : "";
  const aliasMulti = rawType === "multiedit";
  const aliasSpin = rawType === "spinner";
  if (aliasMulti || aliasSpin) rawType = "edit";
  const type = KIND_MAP[rawType] ? rawType : "label";
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
  /* edit 的输入框行为：只收合法档位，其余一律当没填（旧文件 / 手写脏值都读得进来） */
  if (o.multiline === true) node.multiline = true;
  const scroll = Number(o.scroll);
  if (o.scroll !== undefined && Number.isFinite(scroll) && scroll >= 1 && scroll <= 3) node.scroll = Math.round(scroll);
  const maxlen = Number(o.maxlen);
  if (o.maxlen !== undefined && Number.isFinite(maxlen) && maxlen > 0) node.maxlen = Math.round(maxlen);
  const conv = Number(o.conv);
  if (o.conv !== undefined && (conv === 1 || conv === 2)) node.conv = conv;
  const input = Number(o.input);
  if (o.input !== undefined && Number.isFinite(input) && input >= 1 && input <= 11) node.input = Math.round(input);
  const border = Number(o.border);
  if (o.border !== undefined && Number.isFinite(border) && border >= 1 && border <= 6) node.border = Math.round(border);
  const spin = Number(o.spin);
  if (o.spin !== undefined && (spin === 1 || spin === 2)) node.spin = spin;

  /* 旧类型名补齐字段（见函数开头 alias*）：写了的字段优先，没写的按新语义给默认 */
  if (aliasMulti) {
    node.multiline = true;                       /* 多行输入：以前写死 ES_MULTILINE */
    if (!node.scroll) node.scroll = 2;           /* 纵向滚动条 */
  }
  if (aliasSpin) {
    if (!node.spin) node.spin = 1;               /* 自动 ±1 */
    if (!node.input) node.input = 7;             /* 整数：按键过滤 + 失焦夹取 */
  }

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
        /* 非容器带了 children：**只能丢**，而且这是"保住控件本身"的代价。
         * 运行时对"非容器带 children"是硬错误 —— cg_doc.c 解析节点时直接
         * `widget "x" (button) cannot hold children` 并 node_free() 掉**整个控件**，
         * 所以把子节点留着会让整个文件读不进去（丢的就不止子节点了）。
         * 手写 .cgui 时把子控件挂在非容器下是无效写法：改挂到容器（group / card /
         * panel / splitter …，看 model/kinds.ts 的 container 标记）里即可。
         * 注：早先这里的注释写的是"挂到同级，别丢用户的控件"—— 代码从来没实现过，
         * 是条骗人的注释（2026-09-24 结构审查时改正）。 */
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
