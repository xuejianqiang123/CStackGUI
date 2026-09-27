/* model/textcodec.ts -- 结构化内容 ↔ 多行文本 的互转
 *
 * 树 / 菜单 / 表格（列与行）/ 条目列表在画布上都用多行文本编辑，这里放两个方向的编解码。
 *
 * 依赖：只依赖 model/types.ts（CgTreeItem / CgColumn）。
 */

import type { CgColumn, CgTreeItem } from "./types.js";

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

/** 菜单：两空格一层；`文本 | 菜单id`；`-` 单独一行是分隔线；`| v` 表示勾选；
 *  `| @动作名` 表示这一项引用顶层 actions 里的动作（写在哪一段都认，画布写在最后）。
 *
 *  文字可以只写在动作里，这时这一项自己的 text 是空串（notepad.cgui 就是这么写的），
 *  编出来是 ` | @name` 这种「第一段为空」的行 —— 不是坏了，见 Inspector 里那份清单。
 *  **不要**把它编成只看得到 `@name` 的一行：那样 `textToMenu` 只能靠"以 @ 开头"猜，
 *  一条文字真的以 `@` 开头的菜单项就会被吞掉（比如 "@2x 缩放"）。 */
export function menuToText(nodes: CgTreeItem[] | undefined, depth = 0): string {
  if (!nodes?.length) return "";
  const pad = "  ".repeat(depth);
  const lines: string[] = [];
  for (const n of nodes) {
    if (n.text === "-") {
      lines.push(`${pad}-`);
      continue;
    }
    /* 行尾的几段（顺序不能变）：第二字段的位置留给菜单 id（勾选但没 id 时也必须占位，
     * 否则 "| v" 会被读成 id），然后是勾选、最后是动作 */
    const fields: string[] = [];
    if (n.id || n.checked) fields.push(n.id ?? "");
    if (n.checked) fields.push("v");
    if (n.action) fields.push(`@${n.action}`);
    /* 文字为空（文字在动作里）时**不留行首那一格**：`  | @new` 才和同级的 `  -` / `  退出 | m_quit`
     * 对齐；留了就是 `   | @new`，看着像嵌到了下一层。缩进量仍只由 pad 决定，textToMenu 不受影响。 */
    const line = fields.length
      ? `${pad}${n.text}${n.text ? " | " : "| "}${fields.join(" | ")}`
      : `${pad}${n.text}`;
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
      /* 逐字段认，位置语义和以前一致（第 2 段 = id、第 3 段 = 勾选），
         只是**任何一段**以 `@` 开头都当作动作名 —— 老写法读出来完全不变 */
      for (let i = 1; i < parts.length; i += 1) {
        const p = parts[i];
        if (!p) continue;
        if (p.startsWith("@")) {
          const name = p.slice(1).trim();
          if (name) item.action = name;
        } else if (i === 1) {
          item.id = p;
        } else if (i === 2 && /^(v|✓|1|yes)$/i.test(p)) {
          item.checked = true;
        }
      }
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
