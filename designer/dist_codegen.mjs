// src/model.ts
var CG_EVENTS = [
  /* --- 控件事件 --- */
  "click",
  "dblclick",
  "change",
  "submit",
  "select",
  "release",
  "expand",
  "collapse",
  "tick",
  "check",
  "gotfocus",
  "lostfocus",
  /* --- 窗口事件（绑在窗口上，对应「启动窗口」那张事件表） --- */
  "ready",
  "close",
  "closequery",
  "destroy",
  "move",
  "size",
  "activate",
  "deactivate",
  "firstactivate",
  "idle",
  "tray",
  "show",
  "hide",
  "mousedown",
  "mouseup",
  "rdown",
  "rup",
  "mousemove",
  "keydown",
  "keyup",
  "char",
  "wheel"
];
var WINDOW_EVENTS = [
  "ready",
  "close",
  "closequery",
  "destroy",
  "move",
  "size",
  "activate",
  "deactivate",
  "firstactivate",
  "idle",
  "tray",
  "show",
  "hide",
  "mousedown",
  "mouseup",
  "dblclick",
  "rdown",
  "rup",
  "mousemove",
  "gotfocus",
  "lostfocus",
  "keydown",
  "keyup",
  "char",
  "wheel"
];
var EVENT_LABEL = {
  click: "\u5355\u51FB",
  dblclick: "\u53CC\u51FB",
  change: "\u5185\u5BB9/\u503C\u53D8\u5316",
  submit: "\u56DE\u8F66\u63D0\u4EA4",
  select: "\u9009\u62E9\u53D8\u5316",
  release: "\u62D6\u52A8\u7ED3\u675F",
  expand: "\u5C55\u5F00\u8282\u70B9",
  collapse: "\u6298\u53E0\u8282\u70B9",
  tick: "\u65F6\u949F\u5468\u671F\u5230",
  check: "\u884C\u52FE\u9009\u53D8\u5316",
  gotfocus: "\u83B7\u5F97\u7126\u70B9",
  lostfocus: "\u5931\u53BB\u7126\u70B9",
  /* 窗口事件：名字对着「启动窗口」的事件表 */
  ready: "\u521B\u5EFA\u5B8C\u6BD5",
  close: "\u53EF\u5426\u88AB\u5173\u95ED",
  closequery: "\u7A97\u53E3\u53EF\u5426\u88AB\u5173\u95ED",
  destroy: "\u5C06\u88AB\u9500\u6BC1",
  move: "\u4F4D\u7F6E\u88AB\u6539\u53D8",
  size: "\u5C3A\u5BF8\u88AB\u6539\u53D8",
  activate: "\u88AB\u6FC0\u6D3B",
  deactivate: "\u88AB\u53D6\u6D88\u6FC0\u6D3B",
  firstactivate: "\u9996\u6B21\u6FC0\u6D3B",
  idle: "\u7A7A\u95F2",
  tray: "\u6258\u76D8\u4E8B\u4EF6",
  show: "\u88AB\u663E\u793A",
  hide: "\u88AB\u9690\u85CF",
  mousedown: "\u9F20\u6807\u5DE6\u952E\u88AB\u6309\u4E0B",
  mouseup: "\u9F20\u6807\u5DE6\u952E\u88AB\u653E\u5F00",
  rdown: "\u9F20\u6807\u53F3\u952E\u88AB\u6309\u4E0B",
  rup: "\u9F20\u6807\u53F3\u952E\u88AB\u653E\u5F00",
  mousemove: "\u9F20\u6807\u4F4D\u7F6E\u88AB\u79FB\u52A8",
  keydown: "\u6309\u4E0B\u67D0\u952E",
  keyup: "\u653E\u5F00\u67D0\u952E",
  char: "\u5B57\u7B26\u8F93\u5165",
  wheel: "\u6EDA\u8F6E\u88AB\u6EDA\u52A8"
};
var INFO_KEYS = [
  "product",
  "desc",
  "comments",
  "version",
  "build",
  "author",
  "company",
  "copyright",
  "address",
  "postcode",
  "phone",
  "fax",
  "email",
  "homepage"
];
var KINDS = [
  { type: "button", label: "\u6309\u94AE", group: "\u57FA\u7840", container: false, w: 120, h: 40, text: "\u6309\u94AE" },
  { type: "iconbutton", label: "\u56FE\u6807\u6309\u94AE", group: "\u57FA\u7840", container: false, w: 48, h: 48, text: "" },
  { type: "label", label: "\u6807\u7B7E", group: "\u57FA\u7840", container: false, w: 160, h: 24, text: "\u6807\u7B7E" },
  { type: "link", label: "\u94FE\u63A5", group: "\u57FA\u7840", container: false, w: 160, h: 24, text: "\u6253\u5F00\u94FE\u63A5" },
  { type: "switch", label: "\u5F00\u5173", group: "\u57FA\u7840", container: false, w: 160, h: 32, text: "\u5F00\u5173", checklist: true },
  { type: "edit", label: "\u5355\u884C\u8F93\u5165", group: "\u8F93\u5165", container: false, w: 200, h: 32, text: "" },
  { type: "multiedit", label: "\u591A\u884C\u8F93\u5165", group: "\u8F93\u5165", container: false, w: 240, h: 120, text: "" },
  { type: "spinner", label: "\u6570\u5B57\u5FAE\u8C03", group: "\u8F93\u5165", container: false, w: 110, h: 28, text: "", ranged: true },
  { type: "checkbox", label: "\u590D\u9009\u6846", group: "\u8F93\u5165", container: false, w: 160, h: 28, text: "\u9009\u9879", checklist: true },
  { type: "radio", label: "\u5355\u9009\u94AE", group: "\u8F93\u5165", container: false, w: 160, h: 28, text: "\u9009\u9879", checklist: true },
  { type: "listbox", label: "\u5217\u8868\u6846", group: "\u8F93\u5165", container: false, w: 220, h: 140, text: "", listLike: true },
  { type: "combo", label: "\u4E0B\u62C9\u6846", group: "\u8F93\u5165", container: false, w: 200, h: 200, text: "", listLike: true },
  { type: "richtext", label: "\u5BCC\u6587\u672C", group: "\u8F93\u5165", container: false, w: 320, h: 160, text: "" },
  { type: "datepicker", label: "\u65E5\u671F\u65F6\u95F4", group: "\u8F93\u5165", container: false, w: 140, h: 28, text: "" },
  { type: "menu", label: "\u83DC\u5355\u680F", group: "\u83DC\u5355", container: false, w: 0, h: 0, text: "" },
  { type: "menubutton", label: "\u83DC\u5355\u6309\u94AE", group: "\u83DC\u5355", container: false, w: 130, h: 32, text: "\u83DC\u5355" },
  { type: "toolbar", label: "\u5DE5\u5177\u680F", group: "\u83DC\u5355", container: false, w: 400, h: 34, text: "", listLike: true },
  { type: "monthcal", label: "\u6708\u5386", group: "\u8F93\u5165", container: false, w: 230, h: 180, text: "" },
  { type: "slider", label: "\u6ED1\u5757", group: "\u6570\u503C", container: false, w: 200, h: 32, text: "", ranged: true },
  { type: "progress", label: "\u8FDB\u5EA6\u6761", group: "\u6570\u503C", container: false, w: 200, h: 20, text: "", ranged: true },
  { type: "progressring", label: "\u5706\u73AF\u8FDB\u5EA6", group: "\u6570\u503C", container: false, w: 120, h: 120, text: "", ranged: true },
  { type: "tabs", label: "\u6807\u7B7E\u9875", group: "\u6570\u636E", container: true, w: 320, h: 220, text: "", listLike: true },
  { type: "tree", label: "\u6811", group: "\u6570\u636E", container: false, w: 220, h: 180, text: "" },
  { type: "table", label: "\u8868\u683C", group: "\u6570\u636E", container: false, w: 360, h: 180, text: "" },
  { type: "statusbar", label: "\u72B6\u6001\u680F", group: "\u6570\u636E", container: false, w: 400, h: 24, text: "", listLike: true },
  { type: "group", label: "\u5206\u7EC4\u6846", group: "\u5BB9\u5668", container: true, w: 260, h: 180, text: "\u5206\u7EC4" },
  { type: "box", label: "\u5BB9\u5668\u5757", group: "\u5BB9\u5668", container: true, w: 200, h: 120, text: "" },
  { type: "card", label: "\u5361\u7247", group: "\u5BB9\u5668", container: true, w: 280, h: 180, text: "\u5361\u7247" },
  { type: "separator", label: "\u5206\u5272\u7EBF", group: "\u5176\u5B83", container: false, w: 200, h: 2, text: "" },
  { type: "image", label: "\u56FE\u7247", group: "\u5176\u5B83", container: false, w: 240, h: 140, text: "" },
  { type: "shape", label: "\u5916\u5F62\u6846", group: "\u5176\u5B83", container: false, w: 120, h: 120, text: "" },
  { type: "timer", label: "\u65F6\u949F", group: "\u5176\u5B83", container: false, w: 80, h: 24, text: "" },
  { type: "canvas", label: "\u753B\u677F", group: "\u5176\u5B83", container: false, w: 240, h: 160, text: "" },
  { type: "colorpicker", label: "\u989C\u8272\u9009\u62E9", group: "\u5176\u5B83", container: false, w: 64, h: 28, text: "" },
  { type: "browser", label: "\u7F51\u9875\u6D4F\u89C8", group: "\u5176\u5B83", container: false, w: 320, h: 240, text: "" },
  { type: "badge", label: "\u5FBD\u7AE0", group: "\u88C5\u9970", container: false, w: 48, h: 24, text: "99+" },
  { type: "tag", label: "\u6807\u7B7E\u7247", group: "\u88C5\u9970", container: false, w: 72, h: 28, text: "\u6807\u7B7E" },
  { type: "rate", label: "\u8BC4\u5206", group: "\u88C5\u9970", container: false, w: 140, h: 32, text: "", ranged: true },
  { type: "pagination", label: "\u5206\u9875", group: "\u88C5\u9970", container: false, w: 180, h: 32, text: "", ranged: true }
];
var KIND_MAP = Object.fromEntries(KINDS.map((k) => [k.type, k]));
function walk(nodes, visit, parent = null) {
  for (const n of nodes) {
    visit(n, parent);
    if (n.children?.length) walk(n.children, visit, n);
  }
}
function serializeDoc(doc) {
  return `${JSON.stringify(cleanDoc(doc), null, 2)}
`;
}
function cleanTree(item) {
  const out = { text: item.text };
  if (item.id) out.id = item.id;
  if (item.expanded) out.expanded = true;
  if (item.checked) out.checked = true;
  if (item.children?.length) out.children = item.children.map(cleanTree);
  return out;
}
function cleanNode(node) {
  const out = {
    id: node.id,
    type: node.type,
    rect: {
      x: Math.round(node.rect.x),
      y: Math.round(node.rect.y),
      w: Math.round(node.rect.w),
      h: Math.round(node.rect.h)
    }
  };
  if (node.text) out.text = node.text;
  if (node.page !== void 0 && node.page > 0) out.page = Math.round(node.page);
  if (node.src) out.src = node.src;
  if (node.format && (node.type === "datepicker" && node.format !== "date" || node.type === "shape")) {
    out.format = node.format;
  }
  if (node.hint) out.hint = node.hint;
  if (node.period) out.period = Math.max(0, Math.round(node.period));
  if (node.multi === true) out.multi = true;
  if (node.check === true) out.check = true;
  if (node.items && node.items.length) out.items = [...node.items];
  if (node.nodes && node.nodes.length) out.nodes = node.nodes.map(cleanTree);
  if (node.columns && node.columns.length) {
    out.columns = node.columns.map((c) => c.w && c.w > 0 ? { title: c.title, w: Math.round(c.w) } : { title: c.title });
  }
  if (node.rows && node.rows.length) out.rows = node.rows.map((r) => [...r]);
  if (node.option !== void 0) out.option = node.option;
  const style = node.style ?? {};
  const kept = {};
  if (style.font) kept.font = style.font;
  if (style.size) kept.size = style.size;
  if (style.bold) kept.bold = true;
  if (style.fg) kept.fg = style.fg;
  if (style.bg) kept.bg = style.bg;
  if (style.align && style.align !== "left") kept.align = style.align;
  if (style.radius) kept.radius = style.radius;
  if (Object.keys(kept).length) out.style = kept;
  const flagged = (v, d) => v !== void 0 && v !== d;
  if (flagged(node.visible, true)) out.visible = node.visible;
  if (flagged(node.enabled, true)) out.enabled = node.enabled;
  if (flagged(node.checked, false)) out.checked = node.checked;
  if (KIND_MAP[node.type]?.ranged) {
    out.value = node.value ?? 0;
    out.min = node.min ?? 0;
    out.max = node.max ?? 100;
  } else if (node.type === "colorpicker") {
    out.value = node.value ?? 0;
  } else if (KIND_MAP[node.type]?.listLike && node.value) {
    out.value = Math.max(0, Math.round(node.value));
  }
  if (node.events && Object.keys(node.events).length) {
    out.events = { ...node.events };
  }
  if (node.children?.length) out.children = node.children.map(cleanNode);
  else if (KIND_MAP[node.type]?.container) out.children = [];
  return out;
}
function cleanDoc(doc) {
  const a = doc.app;
  const app = {
    title: a.title,
    width: Math.round(a.width),
    height: Math.round(a.height),
    bg: a.bg
  };
  if (a.x !== void 0) app.x = Math.round(a.x);
  if (a.y !== void 0) app.y = Math.round(a.y);
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
    const info = {};
    for (const key of INFO_KEYS) {
      const v = a.info[key];
      if (v) info[key] = v;
    }
    if (Object.keys(info).length) app.info = info;
  }
  if (a.events) {
    const ev = {};
    for (const key of WINDOW_EVENTS) {
      const v = a.events[key];
      if (v) ev[key] = v;
    }
    if (Object.keys(ev).length) app.events = ev;
  }
  return { cgui: 1, app, widgets: doc.widgets.map(cleanNode) };
}

// src/codegen.ts
function collectCallbacks(doc) {
  const uses = [];
  walk(doc.widgets, (n) => {
    for (const ev of CG_EVENTS) {
      const name = n.events?.[ev];
      if (name) uses.push({ name, widgetId: n.id, widgetType: n.type, event: ev });
    }
  });
  for (const ev of WINDOW_EVENTS) {
    const name = doc.app.events?.[ev];
    if (name) uses.push({ name, widgetId: "__window", widgetType: "window", event: ev });
  }
  return uses;
}
function uniqueCallbackNames(doc) {
  return [...new Set(collectCallbacks(doc).map((u) => u.name))].sort();
}
function widgetsWithoutEvents(doc) {
  const out = [];
  walk(doc.widgets, (n) => {
    if (n.type === "label" || n.type === "box" || n.type === "separator") return;
    if (!n.events || !Object.keys(n.events).length) out.push(n);
  });
  return out;
}
function sanitizeIdent(name) {
  const s = name.replace(/[^0-9a-zA-Z_]/g, "_").replace(/^([0-9])/, "_$1");
  return s || "on_event";
}
var macroName = (base) => `${sanitizeIdent(base).toUpperCase()}_BINDINGS`;
var guardName = (base) => `${sanitizeIdent(base).toUpperCase()}_UI_EVENTS_H`;
function callbackBriefs(uses) {
  const byName = {};
  for (const u of uses) {
    const label = EVENT_LABEL[u.event];
    const where = u.widgetType === "window" ? "\u4E3B\u7A97\u53E3" : u.widgetId;
    (byName[u.name] ??= []).push(`${where} \u7684${label}`);
  }
  return Object.fromEntries(Object.entries(byName).map(([k, v]) => [k, v.join("\u3001")]));
}
function fullVersion(info) {
  const parts = [];
  const push = (s) => {
    for (const raw of (s ?? "").split(/[.,]/)) {
      const t = raw.trim();
      if (t === "") continue;
      const n = Number(t);
      parts.push(Number.isFinite(n) && n >= 0 ? String(Math.round(n)) : "0");
    }
  };
  push(info?.version);
  push(info?.build);
  while (parts.length < 4) parts.push("0");
  return parts.slice(0, 4).join(".");
}
var rcStr = (s) => s.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
function rcFile(base, doc) {
  const info = doc.app.info ?? {};
  const ver = fullVersion(info);
  const comma = ver.replace(/\./g, ",");
  const icon = rcStr(doc.app.icon && doc.app.icon.trim() ? doc.app.icon.trim() : "cstackgui.ico");
  const values = [
    ["CompanyName", info.company || info.author || ""],
    ["FileDescription", info.desc || ""],
    ["FileVersion", ver],
    ["InternalName", base],
    ["LegalCopyright", info.copyright || ""],
    ["OriginalFilename", `${base}.exe`],
    ["ProductName", info.product || base],
    ["ProductVersion", ver],
    ["Comments", info.comments || ""],
    ["Author", info.author || ""],
    ["Address", info.address || ""],
    ["PostalCode", info.postcode || ""],
    ["Phone", info.phone || ""],
    ["Fax", info.fax || ""],
    ["Email", info.email || ""],
    ["HomePage", info.homepage || ""]
  ];
  const body = values.filter(([, v]) => v !== "").map(([k, v]) => `      VALUE "${k}", "${rcStr(v)}"`).join("\n");
  const content = `/* ${base}.rc -- \u7A0B\u5E8F\u56FE\u6807 + v6 \u63A7\u4EF6\u6E05\u5355 + \u7248\u672C\u4FE1\u606F
 * \u7531 CStackGUI \u753B\u5E03\u751F\u6210\uFF08\u5DE5\u5177\u680F\u300C\u7A0B\u5E8F\u914D\u7F6E\u300D\u5BF9\u8BDD\u6846\uFF09\uFF0C\u6539\u5185\u5BB9\u8BF7\u56DE\u753B\u5E03\u6539\u540E\u91CD\u65B0\u5BFC\u51FA\u3002
 * build-app.bat \u7528 windres \u7F16\u8BD1\u5B83\uFF1A\u6587\u4EF6\u662F UTF-8\uFF0C\u6240\u4EE5\u5E26 --codepage=65001\u3002
 */
#pragma code_page(65001)

#include <windows.h>

1 ICON "${icon}"

/* v6 \u901A\u7528\u63A7\u4EF6\u6E05\u5355\uFF1A\u5C11\u4E86\u5B83\u6807\u7B7E\u9875/\u8FDB\u5EA6\u6761\u662F\u8001\u5F0F\u5916\u89C2 */
1 RT_MANIFEST "cstackgui.manifest"

VS_VERSION_INFO VERSIONINFO
  FILEVERSION    ${comma}
  PRODUCTVERSION ${comma}
  FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
  FILEFLAGS      0
  FILEOS         VOS_NT_WINDOWS32
  FILETYPE       VFT_APP
  FILESUBTYPE    VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "080404B0"
    BEGIN
${body}
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x804, 1200
  END
END
`;
  return { name: `${base}.rc`, content };
}
function generateC(doc, baseName) {
  const base = sanitizeIdent(baseName);
  const cguiFile = baseName.endsWith(".cgui") ? baseName : `${baseName}.cgui`;
  const uses = collectCallbacks(doc);
  const names = uniqueCallbackNames(doc);
  const briefs = callbackBriefs(uses);
  const cbDoc = (n) => `/**
 * @brief ${briefs[n] ?? n}
 * @param self \u89E6\u53D1\u4E8B\u4EF6\u7684\u63A7\u4EF6
 * @param ev \u4E8B\u4EF6\u7C7B\u578B
 * @param user \u6302\u5728\u63A7\u4EF6\u4E0A\u7684\u4E1A\u52A1\u6570\u636E\uFF08cg_widget_set_user \u6302\u8FDB\u53BB\u7684\uFF09
 */`;
  const header = `/**
 * @file    ${base}_ui_events.h
 * @brief   \u4E8B\u4EF6\u56DE\u8C03\u539F\u578B\u4E0E\u7ED1\u5B9A\u8868\uFF08\u7531 CStackGUI \u753B\u5E03\u751F\u6210\uFF0C\u6539\u4E8B\u4EF6\u7ED1\u5B9A\u8BF7\u56DE\u753B\u5E03\u6539\u540E\u91CD\u65B0\u5BFC\u51FA\uFF09
 * @details \u754C\u9762\u6587\u4EF6: ${cguiFile}\uFF1B\u4E1A\u52A1\u903B\u8F91\u5199\u5728 ${base}_ui_events.c \u91CC
 */
#ifndef ${guardName(base)}
#define ${guardName(base)}

#include "cstackgui.h"

/* \u56DE\u8C03\u539F\u578B\uFF1A\u7B7E\u540D\u56FA\u5B9A\uFF0C\u4E0D\u8981\u6539 */
${names.length ? names.map((n) => `${cbDoc(n)}
void ${n}(cg_widget_t *self, cg_event_t ev, void *user);`).join("\n\n") : "/* \u753B\u5E03\u4E0A\u8FD8\u6CA1\u6709\u7ED1\u5B9A\u4EFB\u4F55\u4E8B\u4EF6 */"}

/* \u7ED1\u5B9A\u8868\uFF1Amain.c \u91CC\u76F4\u63A5\u7528\uFF0C\u6539\u4E8B\u4EF6\u540D\u53EA\u9700\u91CD\u65B0\u5BFC\u51FA\u672C\u6587\u4EF6 */
${names.length ? `#define ${macroName(base)} \\
${names.map((n) => `    CG_BIND("${n}", ${n}), \\`).join("\n")}` : `#define ${macroName(base)}`}

/* \u753B\u5E03\u4E0A\u58F0\u660E\u7684\u4E8B\u4EF6\u7ED1\u5B9A\u6E05\u5355\uFF08\u4F9B\u4EBA\u67E5\u5BF9\uFF1B\u8FD0\u884C\u65F6\u4EE5 .cgui \u4E3A\u51C6\uFF09
${uses.length ? uses.map((u) => ` *   ${u.widgetId.padEnd(18)} ${u.event.padEnd(9)} -> ${u.name}`).join("\n") : " *   \uFF08\u7A7A\uFF09"}
 */
#endif /* ${guardName(base)} */
`;
  const stubs = `/**
 * @file    ${base}_ui_events.c
 * @brief   \u4E8B\u4EF6\u56DE\u8C03\u5B9E\u73B0\uFF1A\u4E1A\u52A1\u903B\u8F91\u5199\u5728\u8FD9\u91CC
 * @details \u8FD9\u4EFD\u6587\u4EF6\u662F\u300C\u4F60\u7684\u4EE3\u7801\u300D\uFF0C\u91CD\u65B0\u5BFC\u51FA\u4E0D\u4F1A\u8986\u76D6\u5B83\uFF08\u753B\u5E03\u5BFC\u51FA\u65F6\u540C\u540D\u6587\u4EF6\u4F1A\u8DF3\u8FC7\uFF09\u3002
 *          \u6BCF\u4E2A\u56DE\u8C03\u90FD\u80FD\u62FF\u5230\uFF1A
 *          self \u89E6\u53D1\u4E8B\u4EF6\u7684\u90A3\u4E2A\u63A7\u4EF6\uFF0C\u7528 cg_widget_text/cg_widget_checked/cg_widget_value \u8BFB\u5B83\u7684\u72B6\u6001
 *          ev   \u4E8B\u4EF6\u7C7B\u578B\uFF08\u4E00\u4E2A\u51FD\u6570\u53EA\u6302\u4E00\u4E2A\u4E8B\u4EF6\u65F6\u53EF\u4EE5\u76F4\u63A5\u5FFD\u7565\uFF09
 *          user cg_widget_set_user() \u6302\u4E0A\u53BB\u7684\u4E1A\u52A1\u6570\u636E
 *          \u62FF\u522B\u7684\u63A7\u4EF6\u7528 cg_find(cg_app_of(self), "\u63A7\u4EF6id")\uFF1B\u6539\u754C\u9762\u7528 cg_widget_set_* \u7CFB\u5217\u3002
 */
#include "${base}_ui_events.h"

#include <stdio.h>

${names.length ? names.map((n) => `${cbDoc(n)}
void ${n}(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)user;
    printf("[%s] widget=%s event=%s\\n", "${base}", cg_widget_id(self), cg_event_name(ev));

    /* TODO: \u5728\u8FD9\u91CC\u5199\u4E1A\u52A1\u903B\u8F91 */
}
`).join("\n\n") : `/* \u753B\u5E03\u4E0A\u8FD8\u6CA1\u6709\u7ED1\u5B9A\u4E8B\u4EF6\u3002\u5728\u753B\u5E03\u53F3\u4FA7\u300C\u4E8B\u4EF6\u300D\u9875\u7B7E\u91CC\u586B\u4E0A C \u51FD\u6570\u540D\uFF0C\u518D\u91CD\u65B0\u5BFC\u51FA\u3002 */
`}`;
  const mainC = `/**
 * @file    main.c
 * @brief   \u5E94\u7528\u5165\u53E3\uFF08\u7531 CStackGUI \u753B\u5E03\u751F\u6210\uFF09
 * @details \u7F16\u8BD1\uFF1A\u672C\u76EE\u5F55\u8981\u653E\u5728 CStackGUI \u9879\u76EE\u7684 apps\\ \u4E0B\uFF0C\u7136\u540E\u5728\u9879\u76EE\u6839\u76EE\u5F55\u8DD1
 *          build-app.bat ${base}
 *          \u4EA7\u7269\uFF1Aapps\\${base}\\${base}.exe \u2014\u2014 \u5355\u4E2A exe\uFF0C\u65E0\u8FD0\u884C\u5E93\u4F9D\u8D56\uFF0C\u53CC\u51FB\u5C31\u80FD\u8DD1\u3002
 */
#include "${base}_ui_events.h"

#include <stdio.h>
#include <string.h>

static const cg_binding_t k_bindings[] = {
    ${macroName(base)}
    CG_BIND_END
};

/**
 * @brief \u7A0B\u5E8F\u5165\u53E3\uFF1A\u7EC4\u88C5\u542F\u52A8\u53C2\u6570\uFF08\u7ED1\u5B9A\u8868\u3001\u81EA\u52A8\u627E .cgui\u3001--selftest \u5206\u652F\uFF09
 * @param argc \u53C2\u6570\u4E2A\u6570
 * @param argv \u547D\u4EE4\u884C\u53C2\u6570\uFF08\u53EF\u5E26 .cgui \u8DEF\u5F84\u6216 --selftest\uFF09
 * @return int \u8FDB\u7A0B\u9000\u51FA\u7801\uFF080 \u6B63\u5E38\uFF09
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 1 = \u53CC\u51FB\u65F6\u4E0D\u518D\u56DE\u8C03 click */
    desc.auto_locate_cgui = 1;          /* \u53CC\u51FB exe \u4E5F\u80FD\u8DD1\uFF1A\u81EA\u52A8\u53BB exe \u9644\u8FD1\u627E .cgui */

    /* --selftest\uFF1A\u4E0D\u5F00\u7A97\u53E3\uFF0C\u53EA\u6821\u9A8C .cgui \u80FD\u5426\u89E3\u6790\u3001\u4E8B\u4EF6\u540D\u662F\u5426\u90FD\u767B\u8BB0\u8FC7 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "${cguiFile}";
        return cg_app_selftest(&desc);
    }

    desc.cgui_path = (argc > 1) ? argv[1] : "${cguiFile}";
    return cg_app_run(&desc);
}
`;
  const buildBat = `@echo off
rem Build this UI project into a standalone exe.
rem The folder can live anywhere: the script walks up from here to find the
rem CStackGUI root (the one holding build-app.bat + runtime\\lib), so the
rem project may sit under CStackGUI\\apps\\, beside CStackGUI.exe, or in any
rem folder of your own; CSTACKGUI_HOME can also point at the root directly.
rem Keep this file pure ASCII (see tools\\bat-gbk.ps1 for why).
setlocal enabledelayedexpansion
if not "%CSTACKGUI_HOME%"=="" goto have_home
set "SEARCH=%~dp0"
for /L %%%%i in (1,1,10) do (
    if not defined CSTACKGUI_HOME if exist "!SEARCH!build-app.bat" if exist "!SEARCH!runtime\\lib\\libcstackgui.a" set "CSTACKGUI_HOME=!SEARCH!"
    if not defined CSTACKGUI_HOME set "SEARCH=!SEARCH!..\\"
)
:have_home
if "%CSTACKGUI_HOME:~-1%" NEQ "\\" set "CSTACKGUI_HOME=%CSTACKGUI_HOME%\\"
if "%CSTACKGUI_HOME%"=="\\" (
    echo [fail] cannot find the CStackGUI root ^(a folder with build-app.bat + runtime\\lib^).
    echo        Walked up from: %~dp0
    echo        Put this folder beside CStackGUI.exe, under CStackGUI\\apps\\, or set
    echo        CSTACKGUI_HOME to the CStackGUI root.
    exit /b 1
)
rem build-app.bat takes an absolute project dir, so renaming or moving this
rem folder still rebuilds.
call "%CSTACKGUI_HOME%build-app.bat" "%~dp0."
`;
  return [
    { name: `${base}_ui_events.h`, content: header },
    { name: `${base}_ui_events.c`, content: stubs },
    { name: "main.c", content: mainC },
    /* 与画布「保存」同一套序列化（cleanNode 去默认值字段），保证导出即保存、两者字节一致 */
    { name: cguiFile, content: serializeDoc(doc) },
    /* 程序图标 + 版本信息：build-app.bat 见到这个 rc 就用它，版本信息不再是通用的 */
    rcFile(base, doc),
    { name: "build.bat", content: buildBat.replace(/\n/g, "\r\n") }
  ];
}
function describeDoc(doc) {
  let count = 0;
  let containers = 0;
  walk(doc.widgets, (n) => {
    count += 1;
    if (KIND_MAP[n.type]?.container) containers += 1;
  });
  return { count, containers, callbacks: uniqueCallbackNames(doc).length };
}
export {
  collectCallbacks,
  describeDoc,
  fullVersion,
  generateC,
  sanitizeIdent,
  uniqueCallbackNames,
  widgetsWithoutEvents
};
