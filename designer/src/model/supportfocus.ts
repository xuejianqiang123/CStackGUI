/* supportfocus.ts -- 选中控件 -> 相关支持库关键词 / 点到的支持库函数反查用到它的控件
 *
 * 为什么做它（2026-10-18 路线图 G4'）：HMI 最被低估的差评是"按钮按了没反应"。画布侧的
 * 解药是**双向连接**——选中一个控件时，「支持库」页签直接高亮"你该查的那几个函数"
 * （cg_widget_set_* / cg_chart_push / cg_table_add_row…），不用新手自己背函数名去搜。
 *
 * 两向都纯 doc 派生，**不需要新增跨组件状态**：
 *   正向 (b)：控件类型 -> 关键词 -> filterSupportLibs 过滤出相关命令；
 *   反向 (c)：点某个函数 -> collectCallbacks(doc) 反查哪些控件/事件用到它 -> 高亮那些控件。
 * 数据是**关键词**不是函数名：支持库的真相是 cstackgui.h，函数会增减，按"用途词"过滤更稳。
 *
 * ⚠ 相对说明符带 .js 后缀（同 store.ts / tableimport.ts）。
 */
import { isNumEdit } from "./kinds.js";
import { walk } from "./tree.js";
import type { CgDoc, CgNode } from "./types.js";
import { CG_EVENTS, WINDOW_EVENTS, type CgEvent } from "./events.js";

/** 一个事件绑定点（本地收窄版；刻意不复用 codegen 的 CallbackUse，避免 barrel 重名，
 *  也让本文件保持零 codegen 依赖）。 */
export type FocusHit = {
  name: string;
  widgetId: string;
  widgetType: string;
  event: CgEvent;
};

/**
 * @brief 控件类型 -> 「支持库」里该查的关键词（用途词，不是函数名）
 * @param node 选中的控件
 * @return string[] 关键词（空数组 = 这个控件没什么可查的，如 label/box）
 * @note 命中口径 = 命令的 fn/name/brief/decl 任一含关键词（filterSupportLibs 的四选一）。
 *       覆盖主干类型；没列到的类型（label/box/separator…）返回空数组，页签不预置搜索词。
 */
export function supportKeywordsFor(node: CgNode | null): string[] {
  if (!node) return [];
  if (isNumEdit(node)) return ["数值", "取值", "设值"];
  switch (node.type) {
    case "button":
    case "iconbutton":
      return ["按钮", "启用", "禁用"];
    case "edit":
      return ["输入框", "文本"];
    case "checkbox":
    case "radio":
    case "switch":
      return ["勾选", "选中"];
    case "slider":
    case "progressbar":
    case "meter":
      return ["值", "范围"];
    case "listbox":
    case "combo":
    case "iconlist":
      return ["列表", "项"];
    case "table":
      return ["表格", "行", "列"];
    case "tree":
      return ["树", "节点"];
    case "tabs":
      return ["标签页", "页"];
    case "terminal":
      return ["终端", "写入"];
    case "chart":
      return ["图表", "曲线"];
    case "led":
      return ["状态灯", "闪烁"];
    case "timer":
      return ["定时器"];
    case "serial":
      return ["串口"];
    case "browser":
      return ["浏览器"];
    case "datepicker":
      return ["日期"];
    default:
      return [];
  }
}

/**
 * @brief 反查：某个支持库函数被哪些控件/事件用着（支持库 -> 画布 的那一头）
 * @param doc 界面文档
 * @param fn 支持库函数名（如 `cg_widget_set_value`）
 * @return CallbackUse[] 用到它的绑定点（空 = 只在文档里出现过调用，没有控件事件直接绑它）
 * @note 只看**事件绑定**（`doc.widgets` 的 events + `app.events`）；动作 handler 也算，
 *       但它的 self 是主窗口伪控件，调用方用 labelOf 之类的口径区分显示。
 */
export function callbacksNamed(doc: CgDoc, fn: string): FocusHit[] {
  if (!fn) return [];
  const out: FocusHit[] = [];
  walk(doc.widgets, (n) => {
    for (const ev of CG_EVENTS) {
      const name = n.events?.[ev];
      if (name === fn) out.push({ name, widgetId: n.id, widgetType: n.type, event: ev });
    }
  });
  for (const ev of WINDOW_EVENTS) {
    const name = doc.app.events?.[ev];
    if (name === fn) out.push({ name, widgetId: "__window", widgetType: "window", event: ev });
  }
  return out;
}
