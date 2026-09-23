/* model/kinds.ts -- 控件类型登记表（调色板 / 默认尺寸 / 容器判定）
 *
 * KINDS 是画布认可的全部控件类型及其缺省尺寸/文字/面板开关（ranged / checklist / listLike），
 * KIND_MAP 是它在类型名上的索引；isContainer / isNumEdit 是两处最常用的类型判定。
 *
 * 依赖：只依赖 model/types.ts（isNumEdit 要 CgNode）。
 */

import type { CgNode } from "./types.js";

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
  /* 唯一的输入框类型：多行与数值框都是它的预设（编辑区里两个开关），
     所以调色板只有这一项 —— 分成三种类型时它们的能力其实完全重叠。 */
  { type: "edit", label: "输入框", group: "输入", container: false, w: 200, h: 32, text: "" },
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

/** 是否数值输入框：`edit` + 调节器（spin），且不是多行。
 *  value 是它的真相、显示文本由 value 派生，调色板/属性面板/预览都按它分流。
 *  **必须和运行时的 cg_widget_is_numedit()（core/cg_widget.c）保持一致**。 */
export function isNumEdit(node: CgNode): boolean {
  return node.type === "edit" && !!node.spin && !node.multiline;
}

export const isContainer = (type: string) => KIND_MAP[type]?.container === true;
