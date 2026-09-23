/* model/factory.ts -- 造节点与造文档（新建控件、新工程模板）
 *
 * 模型层的「工厂」：id 生成（newId）、按类型造出一个带可见种子的控件（makeNode）、
 * 空文档（emptyDoc）与三种新工程模板（templateDoc）。
 *
 * 依赖：model/types.ts + model/kinds.ts（默认尺寸/文字/容器与否都取自 KIND_MAP）。
 */

import { KIND_MAP, KINDS } from "./kinds.js";
import type { CgDoc, CgNode } from "./types.js";

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
    node.value = 40;
    node.min = 0;
    node.max = 100;
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
    /* 备注是多行输入框：edit + multiline + 纵向滚动（原来是独立的 multiedit 类型，
       2026-09-22 合并 —— 画布模板也跟着改成新写法，别让模板教老字段） */
    const memo = tpl("edit", "edt_memo", 90, 80, 360, 92);
    memo.multiline = true;
    memo.scroll = 2;
    box.children = [
      tpl("label", "lab_name", 20, 34, 60, 24, "姓名"),
      tpl("edit", "edt_name", 90, 30, 360, 30),
      tpl("label", "lab_memo", 20, 84, 60, 24, "备注"),
      memo,
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
