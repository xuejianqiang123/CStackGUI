/* model/tableimport.ts -- 点表 CSV 批量导入：纯函数解析 + 批量导入命令
 *
 * 为什么做它（2026-10-18 路线图 G4，市面调研头号产品机会）：工控上位机的真实重复劳动
 * 在"点表搬运"——几百个信号名要一个个拖控件、改文字。组态王被竞品攻击的软肋正是
 * "所有操作必须在 IDE 里手动完成、无法批量修改"；MCGS 的强项之一是"通道连接方便"。
 * .cgui 是文本 JSON，批量生成天然可 diff、可版本管理 —— 这是本画布对传统组态的
 * 结构性差异化，本模块把它变成显性能力。
 *
 * CSV 契约（canvas-longterm-roadmap.md §G4 钉死，别悄悄改）：
 *   每行 `text,type[,值]` —— text = 控件文字（必填），type = k_kinds 合法类型名，
 *   值 = 可选整数（只对 ranged 控件有意义：进度条/滑块/数值框等的初始 value）。
 *   `#` 开头 = 注释行；空行跳过；坏行**逐行报错、不停导入**（好行照进，问题行进 issues）。
 *   一行一个控件（文字里不许有逗号 —— 列少而稳，不做引号转义：真要点表粘贴，逗号
 *   本来就是列分隔符，转义只会让"哪行坏了"更难看懂）。
 *
 * 生成走 G1 命令层（ops.ts）：构造 = makeImportCmd，应用 = 确定性 applyCmd ——
 * 一次导入 = **一步撤销**，命令对象天然进 journal（协同前置）。
 *
 * ⚠ 相对说明符必须带 `.js` 后缀（同 store.ts 的说明）。
 */
import { KIND_MAP } from "./kinds.js";
import { makeNode } from "./factory.js";
import { walk } from "./tree.js";
import type { CanvasCmd, InsertItem } from "./ops.js";
import type { CgDoc, CgNode } from "./types.js";

/** 一行合法的导入目标 */
export type ImportRow = { text: string; type: string; value?: number };

/** 一行问题（line = 1 起的行号，reason = 人话） */
export type ImportIssue = { line: number; reason: string };

/** 自动排版的网格参数（与 addNode 的 offset 启发式同口径：24px 间隔） */
const GRID_COLS = 8;
const GRID_GAP = 24;

/**
 * @brief 解析点表 CSV（纯函数：不碰 doc、不抛异常）
 * @param csv 点表文本（每行 text,type[,值]）
 * @return { rows, issues } 合法行（保序）+ 问题行（逐行带原因）
 */
export function parseTableCsv(csv: string): { rows: ImportRow[]; issues: ImportIssue[] } {
  const rows: ImportRow[] = [];
  const issues: ImportIssue[] = [];

  csv.split(/\r?\n/).forEach((raw, i) => {
    const line = raw.trim();
    if (!line || line.startsWith("#")) return; /* 空行与注释：静默跳过，不算问题 */

    const parts = line.split(",").map((s) => s.trim());
    if (parts.length < 2) {
      issues.push({ line: i + 1, reason: "至少要有「文字,类型」两列" });
      return;
    }
    if (parts.length > 3) {
      issues.push({ line: i + 1, reason: "多于 3 列（契约是 文字,类型[,值]，文字里不许有逗号）" });
      return;
    }
    const [text, type, valRaw] = parts;
    if (!text) {
      issues.push({ line: i + 1, reason: "第一列（文字）是空的" });
      return;
    }
    if (!KIND_MAP[type]) {
      issues.push({ line: i + 1, reason: `不认识的控件类型 "${type}"（要用 .cgui 里的类型名，如 button / label / edit）` });
      return;
    }
    let value: number | undefined;
    if (valRaw !== undefined && valRaw !== "") {
      const v = Number(valRaw);
      if (!Number.isFinite(v)) {
        issues.push({ line: i + 1, reason: `第三列（值）不是数字："${valRaw}"` });
        return;
      }
      value = Math.trunc(v);
    }
    rows.push(value === undefined ? { text, type } : { text, type, value });
  });

  return { rows, issues };
}

/**
 * @brief 构造"批量导入点表"的命令（一步 insert，锚定顶层）
 * @param d 构造时点的文档（只用于避让已有 id）
 * @param rows parseTableCsv 的合法行
 * @return { cmd, newIds } | null 没有合法行时返回 null
 * @note 自动排版：8 列网格，列宽/行高取全体控件的最大默认尺寸（对齐成真网格）；
 *       值只写给 ranged 控件（进度条/滑块/数值框…的初始 value）—— 其它类型设 value
 *       在运行时无意义（如 led 的 value 是状态机、chart 的 value 是窗口点数），忽略。
 *       与 makeInsertCmd 同口径：id 防御查重，命令自包含（位置/文字/id 全在 payload）。
 */
export function makeImportCmd(
  d: CgDoc,
  rows: ImportRow[],
): { cmd: CanvasCmd; newIds: string[] } | null {
  if (!rows.length) return null;

  const taken = new Set<string>();
  walk(d.widgets, (n) => taken.add(n.id));

  const nodes: CgNode[] = [];
  let colW = 0;
  let rowH = 0;
  for (const row of rows) {
    const node = makeNode(row.type, 0, 0);
    let n = 2;
    while (taken.has(node.id)) node.id = `${row.type}_${n++}`; /* 防御：makeId 撞车可忽略，撞了退序号形态 */
    taken.add(node.id);
    node.text = row.text;
    if (row.value !== undefined && (KIND_MAP[row.type]?.ranged ?? false)) node.value = row.value;
    colW = Math.max(colW, node.rect.w);
    rowH = Math.max(rowH, node.rect.h);
    nodes.push(node);
  }

  const items: InsertItem[] = nodes.map((node, i) => {
    node.rect.x = GRID_GAP + (i % GRID_COLS) * (colW + GRID_GAP);
    node.rect.y = GRID_GAP + Math.floor(i / GRID_COLS) * (rowH + GRID_GAP);
    return { node, parentId: null };
  });
  return { cmd: { type: "insert", items }, newIds: nodes.map((n) => n.id) };
}
