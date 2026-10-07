/* model/parse.ts -- 读入 .cgui（粘贴克隆 + 脏数据容错）
 *
 * 两件事：①粘贴/复制用的节点克隆与 id 重排（uniquifyIds / cloneNode）；
 * ②parseDoc 把 JSON 文本读成 CgDoc，中间一串 coerce* 负责把手工编辑出来的脏值收敛回合法形状。
 *
 * 依赖：model/types.ts + model/kinds.ts（KIND_MAP 兜底类型）+ model/events.ts（事件白名单）。
 */

import { CG_EVENTS, WINDOW_EVENTS, type CgEvent } from "./events.js";
import { KIND_MAP } from "./kinds.js";
import { STYLE_KEYS, isSafeStyleName } from "./style.js";
import {
  INFO_KEYS,
  isSafeActionName,
  type CgAction,
  type CgApp,
  type CgBind,
  type CgColumn,
  type CgDoc,
  type CgInfo,
  type CgNode,
  type CgRect,
  type CgStyle,
  type CgStyleRef,
  type CgStyleValue,
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
    /* 菜单项引用动作（运行时同样对树节点读它，树不会去看这个字段） */
    if (typeof o.action === "string" && o.action.trim()) item.action = o.action.trim();
    if (o.checked === true) item.checked = true;                /* 菜单勾选态同理 */
    if (Array.isArray(o.children)) item.children = coerceTree(o.children);
    out.push(item);
  }
  return out;
}

/* 动作表：动作名 -> { text / accel / handler / checked / enabled }。
 * 宽容读入：值不是对象就丢掉这一条；字段类型不对按缺省值兜底（空串 = 没填）。
 * 空名条目**直接丢**（运行时也会忽略并写 WARN，画布上没法表示一个没有名字的动作）。 */
function coerceAction(raw: unknown): CgAction | undefined {
  if (!raw || typeof raw !== "object") return undefined;
  const o = raw as Record<string, unknown>;
  const a: CgAction = {};
  if (typeof o.text === "string" && o.text.trim()) a.text = o.text.trim();
  if (typeof o.accel === "string" && o.accel.trim()) a.accel = o.accel.trim();
  if (typeof o.handler === "string" && o.handler.trim()) a.handler = o.handler.trim();
  if (o.checked === true) a.checked = true;
  if (o.enabled === false) a.enabled = false;
  return a;
}

function coerceActions(raw: unknown): Record<string, CgAction> | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const out: Record<string, CgAction> = {};
  for (const [key, value] of Object.entries(raw as Record<string, unknown>)) {
    const name = key.trim();
    if (!isSafeActionName(name)) continue;
    const a = coerceAction(value);
    if (a) out[name] = a;
  }
  return Object.keys(out).length ? out : undefined;
}

/* ---------------------------------------------------------------- 字段取值
 * **运行时是权威**：这里是 cg_json.c 的 cj_num / cj_int / cj_bool / cj_color 四套取值函数的
 * 逐一对齐版（`numOf` / `intOk` / `boolOk` / `colorOf`）。手写 .cgui 里运行时**认**的值，
 * 画布不能丢 —— 打开再存就抹掉 = 静默删数据
 * （`"size": "11"` / `"bold": "true"` / `"fg": 16711680` 运行时都认）。
 * 写回时按**规范形态**（数字 / 布尔 / "#RRGGBB"）落盘，语义不变即可（不要求字节恒等）。
 */

/** = cj_num：数字 / 布尔（0|1）/ 字符串按 strtod（前缀数字，含 0x 十六进制） */
function numOf(v: unknown, def: number): number {
  if (typeof v === "boolean") return v ? 1 : 0;
  if (typeof v === "number") return Number.isFinite(v) ? v : def;
  if (typeof v === "string") {
    const hex = /^\s*([+-]?)0[xX]([0-9a-fA-F]+)/.exec(v);
    if (hex) {
      const n = parseInt(hex[2], 16);
      return hex[1] === "-" ? -n : n;
    }
    const d = parseFloat(v); /* strtod：前缀是数字就取，认不出来才是 def */
    return Number.isNaN(d) ? def : d;
  }
  return def;
}

/** 值取不到就返回 undefined（= 这个字段**没写**）。
 *  与运行时同口径：`style_overlay` 读不出 `size`/`bold`/`radius` 时**不置掩码**
 *  （它用哨兵判断，见 cg_doc.c），所以画布也必须"丢键"，**不能**写成 0 / false ——
 *  写成 0 会顶掉命名样式里的字号（`{"use":"log","size":"abc"}` 里那个字号本该保住）。 */
function intOk(v: unknown): number | undefined {
  const n = numOf(v, Number.NaN);
  return Number.isFinite(n) ? (n < 0 ? Math.ceil(n - 0.5) : Math.floor(n + 0.5)) : undefined;
}

/** 同 intOk，用于 bold（认不出来 -> undefined = 没写） */
function boolOk(v: unknown): boolean | undefined {
  if (typeof v === "boolean") return v;
  if (typeof v === "number") return Number.isFinite(v) ? v !== 0 : undefined;
  if (typeof v === "string") {
    if (v === "true" || v === "1") return true;
    if (v === "false" || v === "0") return false;
  }
  return undefined;
}

/** = cj_color：数字（0xAARRGGBB，含负数的补码低位）/ "#RRGGBB" / "#RGB" / 8 位 "#AARRGGBB"。
 *  0xFFFFFFFF（CG_COLOR_NONE）与认不出来的都返回 undefined（= 没设色）。
 *  写回规范形态：低 24 位装得下就写 6 位，否则写 8 位（保住 alpha）。 */
function colorOf(v: unknown): string | undefined {
  let n: number;
  if (typeof v === "boolean") n = v ? 1 : 0;
  else if (typeof v === "number") n = Number.isFinite(v) ? v >>> 0 : 0xffffffff;
  else if (typeof v === "string") {
    const body = v.startsWith("#") ? v.slice(1) : v;
    if (!/^[0-9a-fA-F]*$/.test(body)) return undefined; /* 非 hex 字符 -> 不符 */
    if (body.length === 3) {
      const d = parseInt(body, 16);
      n = ((((d >> 8) & 0xf) * 0x11) << 16) | ((((d >> 4) & 0xf) * 0x11) << 8) | ((d & 0xf) * 0x11);
    } else if (body.length === 6) n = parseInt(body, 16) & 0xffffff;
    else if (body.length === 8) n = parseInt(body, 16) >>> 0;
    else return undefined;
  } else return undefined;

  if (n === 0xffffffff) return undefined; /* CG_COLOR_NONE = 没设色 */
  return n <= 0xffffff
    ? `#${(n & 0xffffff).toString(16).toUpperCase().padStart(6, "0")}`
    : `#${(n >>> 0).toString(16).toUpperCase().padStart(8, "0")}`;
}

/* 样式：命名样式表（styles{}）的一条，或控件 style 的对象形态。契约 §1/§2。
 *
 * 读入宽容：值不是对象、名字为空 —— 跳过该条，不报错、不中断加载；字段值按上面那四套
 * （与运行时同款）取值，认不出来就当缺省（align 只认 left/center/right 三个名字，
 * 与运行时的字符串比较一致）。
 *
 * keepDefaults：**有 use 的覆盖对象**要把"写了"的字段都留下（bold:false / size:0 / align:"left" /
 * radius:0，甚至认不出来的值取到的 0/false）—— 合并规则是"写了就覆盖"，丢了这些字段会让命名样式
 * 里的值顶回来（"画布显示的和跑起来不一样"）。
 * **位掩码口径两边一致**（cg_doc.c 的 style_overlay）：**七个字段一样 —— 值取不到就算"没写"**
 * （font/fg/bg 靠返回值判断，size/bold/radius 靠哨兵；2026-09-27 两边对齐后定下的口径）。
 * 所以这里对认不出来的值一律**丢键**，别再写成"只要键在就置位"或"垃圾值取 0/false"。
 * 内联对象（没有 use、没有可覆盖的打底）则按老口径：等于缺省值的字段不写（位掩码没有意义）。 */
function coerceStyleObject(raw: unknown, keepDefaults: boolean): CgStyle | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const o = raw as Record<string, unknown>;
  const has = (k: string) => Object.prototype.hasOwnProperty.call(o, k);
  const out: CgStyle = {};

  /* font：运行时用 cj_str（只认字符串；非串得 NULL） */
  if (has("font")) {
    const font = typeof o.font === "string" && o.font.trim() ? o.font : undefined;
    if (font) out.font = font;
  }
  if (has("size")) {
    const n = intOk(o.size);
    if (n !== undefined && (n > 0 || keepDefaults)) out.size = n;
  }
  if (has("bold")) {
    const b = boolOk(o.bold);
    if (b !== undefined && (b || keepDefaults)) out.bold = b;
  }
  if (has("fg")) {
    const c = colorOf(o.fg);
    if (c) out.fg = c;
  }
  if (has("bg")) {
    const c = colorOf(o.bg);
    if (c) out.bg = c;
  }
  /* align：运行时只在字符串命中 left/center/right 时才置位（"middle" 当没写） */
  if (o.align === "center" || o.align === "right" || (keepDefaults && o.align === "left")) {
    out.align = o.align;
  }
  if (has("radius")) {
    const n = intOk(o.radius);
    if (n !== undefined && (n > 0 || keepDefaults)) out.radius = n;
  }
  return out;
}

/** 控件 style 的三种形态（契约 §2）：字符串（纯命名引用）/ 带 use 的对象（命名+覆盖）/ 内联对象。
 *  认不出来的（不是字符串也不是对象）当没写 style。 */
function coerceStyleValue(raw: unknown): CgStyleValue | undefined {
  if (typeof raw === "string") {
    const s = raw.trim();
    return s || undefined;
  }
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const o = raw as Record<string, unknown>;
  const use = typeof o.use === "string" && o.use.trim() ? o.use.trim() : "";
  const fields = coerceStyleObject(o, !!use);
  if (!use) {
    return fields && Object.keys(fields).length ? fields : undefined;
  }
  const out: CgStyleRef = { use };
  for (const k of STYLE_KEYS) {
    const v = fields?.[k];
    if (v !== undefined) (out as unknown as Record<string, unknown>)[k] = v;
  }
  return out;
}

/** 顶层命名样式表：样式名 -> 样式。空名 / __proto__ 这类非法名直接丢；
 *  值不是对象也丢；一个字段都没有的 `{}` **保留**（可能已经被控件引用着）。 */
function coerceStyles(raw: unknown): Record<string, CgStyle> | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const out: Record<string, CgStyle> = {};
  for (const [key, value] of Object.entries(raw as Record<string, unknown>)) {
    const name = key.trim();
    if (!isSafeStyleName(name)) continue;
    const st = coerceStyleObject(value, false);
    if (!st) continue;
    out[name] = st;
  }
  return Object.keys(out).length ? out : undefined;
}

/** 画布不认识的控件类型：整份文件都读不得。抛出来由 parseDoc 收成 error 字符串。 */
class UnknownKindError extends Error {}

function coerceNode(raw: unknown, index: number): CgNode | null {
  if (!raw || typeof raw !== "object") return null;
  const o = raw as Record<string, unknown>;
  /* 老类型名自动升级：multiedit / spinner 已合并进 edit（2026-09-22）。
     映射必须和运行时 cg_doc.c 的 parse_node 完全一致。 */
  let rawType = typeof o.type === "string" ? o.type : "";
  const aliasMulti = rawType === "multiedit";
  const aliasSpin = rawType === "spinner";
  if (aliasMulti || aliasSpin) rawType = "edit";
  /* 认不出来的类型**不兜底降级**（这里原来是 `KIND_MAP[t] ? t : "label"`）：
   *   - 运行时对未知类型是**整份文件加载失败**（cg_doc.c：unknown widget type "x"），
   *     所以这份文件本来就跑不起来 —— 降级成 label 救不回任何东西；
   *   - 而 label 不是容器，`children` 会连**整棵子树**一起被丢掉，用户打开再保存就是永久删除
   *     （2026-10-07 实测：带 dialog 节点的 .cgui 在画布上"读进来了、节点被静默丢了"，
   *      见 tools\probe-dialog.mjs）。
   * 所以跟运行时同口径：不给读，并把话说清楚。多窗体 epic 会让这种文件真的出现
   * （新画布写的 + 旧画布打开的），热重载也让外部编辑器/AI 写进来的文件成为一等场景。 */
  const info = KIND_MAP[rawType];
  if (!info) throw new UnknownKindError(typeof o.type === "string" ? o.type : "(缺少 type)");
  const type = rawType;
  const flat = { x: o.x, y: o.y, w: o.w, h: o.h } as Record<string, unknown>;
  const rectSource = (o.rect as unknown) ?? flat;
  const node: CgNode = {
    id: typeof o.id === "string" && o.id ? o.id : `${type}_${index + 1}`,
    type,
    rect: coerceRect(rectSource, { x: 0, y: 0, w: info.w, h: info.h }),
  };
  if (typeof o.text === "string") node.text = o.text;
  const style = coerceStyleValue(o.style);
  if (style !== undefined) node.style = style;
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
  /* serial：只收合法档位，其余一律当没填（旧文件 / 手写脏值都读得进来）；
     缺字段就回退缺省值，绝不往 node 上塞 undefined（写出时非缺省才写）。 */
  if (typeof o.port === "string" && o.port.trim()) node.port = o.port;
  const baud = Number(o.baud);
  if (o.baud !== undefined && Number.isFinite(baud) && baud > 0) node.baud = Math.round(baud);
  const bits = Number(o.bits);
  if (o.bits !== undefined && Number.isFinite(bits) && bits >= 5 && bits <= 8) node.bits = Math.round(bits);
  const stop = Number(o.stop);
  if (o.stop !== undefined && Number.isFinite(stop) && (stop === 1 || stop === 2)) node.stop = Math.round(stop);
  const parity = Number(o.parity);
  if (o.parity !== undefined && Number.isFinite(parity) && parity >= 0 && parity <= 2) node.parity = Math.round(parity);
  if (o.auto === true) node.auto = true;
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
  /* grow（余量权重 0..99）：口径**对齐运行时 cg_doc.c**（`cj_int(cj_get(o,"grow"),0)`，
     再 `grow<0 || grow>99` 归 0）—— 数字 / 布尔 / 数字字符串都收、四舍五入，只留 1..99。
     于是 "abc" / null / 对象 / 空串 / 0 / 负数 / 大于 99 一律当 0（= 没写），不报错、不中断加载。
     改这里要跟 runtime/src/core/{cg_doc.c,cg_json.c} 一起看，否则「画布显示的和跑起来不一样」。 */
  const rawGrow = o.grow;
  const grow =
    typeof rawGrow === "number" || typeof rawGrow === "string" || typeof rawGrow === "boolean"
      ? Math.round(Number(rawGrow))
      : NaN;
  if (grow >= 1 && grow <= 99) node.grow = grow;

  /* 旧类型名补齐字段（见函数开头 alias*）：写了的字段优先，没写的按新语义给默认 */  if (aliasMulti) {
    node.multiline = true;                       /* 多行输入：以前写死 ES_MULTILINE */
    if (!node.scroll) node.scroll = 2;           /* 纵向滚动条 */
  }
  if (aliasSpin) {
    if (!node.spin) node.spin = 1;               /* 自动 ±1 */
    if (!node.input) node.input = 7;             /* 整数：按键过滤 + 失焦夹取 */
  }

  /* 引用动作：只记名字。动作存不存在、这个类型支不支持 click 都留给运行时校验
     （画布在「动作」那一栏把同样的两件事提示给用户看） */
  if (typeof o.action === "string" && o.action.trim()) node.action = o.action.trim();

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
  /* 跳过 UTF-8 BOM：运行时那边（`cg_json.c`）明确"自动跳过 BOM，编辑器保存的 .cgui 常带"，
     画布原先不跳 —— 结果是同一份文件 exe 跑得好、画布却说"不是合法 JSON"。
     两端口径必须一致（热重载上线后，外部编辑器写的文件是一等场景，这个不一致会更常撞上）。 */
  const src = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
  let raw: unknown;
  try {
    raw = JSON.parse(src);
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

  /* 界面主题（契约 §3）：true=深色 / false=浅色 / "auto"=跟随系统。别的值当没写（缺省 = 浅色）。 */
  if (app.dark === true || app.dark === false || app.dark === "auto") appOut.dark = app.dark;

  const info = coerceInfo(app.info);
  if (info) appOut.info = info;
  if (winEvents) appOut.events = winEvents;

  /* 未知控件类型判死整份文件（与运行时同口径）。不能"只丢那一个节点"接着编辑：
     丢节点 = 用户下次保存就永久删掉了它，而且不会有任何提示。 */
  let widgets: CgNode[];
  try {
    widgets = o.widgets.map((w, i) => coerceNode(w, i)).filter((w): w is CgNode => w !== null);
  } catch (e) {
    if (e instanceof UnknownKindError) {
      return {
        error:
          `不认识的控件类型「${e.message}」：可能是更新版的画布（或手写）写出来的，` +
          `运行时同样会拒绝整份文件。为避免把你的界面改坏，画布不打开它。`,
      };
    }
    throw e;
  }

  const doc: CgDoc = {
    cgui: 1,
    app: appOut,
    widgets,
  };
  const actions = coerceActions(o.actions);
  if (actions) doc.actions = actions;
  const styles = coerceStyles(o.styles);
  if (styles) doc.styles = styles;
  return { doc };
}
