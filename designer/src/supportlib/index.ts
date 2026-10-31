/* supportlib/index.ts -- 「支持库」页签的对外入口
 *
 * 调用方只认这个文件（`from "../supportlib"`），内部按库拆了哪些文件与它无关。
 * 2026-10-31 从单文件 131KB 拆成 types / core / widget / kit + 本文件；
 * 导出面与拆分前逐字相同（SupCmd / SupGroup / SupLib / SUPPORT_LIBS /
 * allCommands / countCommands / filterSupportLibs / firstCommand）。
 */
import type { SupCmd, SupGroup, SupLib } from "./types.js";
import { CORE_LIB } from "./core.js";
import { WIDGET_LIB } from "./widget.js";
import { KIT_LIB } from "./kit.js";

export type { SupCmd, SupGroup, SupLib };

export const SUPPORT_LIBS: SupLib[] = [CORE_LIB, WIDGET_LIB, KIT_LIB];

export function allCommands(): SupCmd[] {
  const out: SupCmd[] = [];
  const walkGroup = (g: SupGroup) => {
    out.push(...g.cmds);
    g.children?.forEach(walkGroup);
  };
  SUPPORT_LIBS.forEach((lib) => lib.groups.forEach(walkGroup));
  return out;
}

export function countCommands(): number {
  return allCommands().length;
}

function cmdMatches(cmd: SupCmd, kw: string): boolean {
  return (
    cmd.fn.toLowerCase().includes(kw) ||
    cmd.name.toLowerCase().includes(kw) ||
    cmd.brief.toLowerCase().includes(kw) ||
    cmd.decl.toLowerCase().includes(kw)
  );
}

/** 按关键字过滤。
 *  命中规则：命令的 函数名/中文名/说明/原型 任一命中；
 *  **分类名或库名命中时，整棵子树都算命中**（新手常按「信息框」「表格」这种分类词找，
 *  而不是按 cg_message_box 这种函数名找）。 */
export function filterSupportLibs(libs: SupLib[], keyword: string): SupLib[] {
  const kw = keyword.trim().toLowerCase();
  if (!kw) return libs;

  const filterGroup = (g: SupGroup): SupGroup | null => {
    const keepAll = g.label.toLowerCase().includes(kw);
    const cmds = keepAll ? g.cmds : g.cmds.filter((x) => cmdMatches(x, kw));
    const children = keepAll
      ? g.children ?? []
      : (g.children ?? []).map(filterGroup).filter((x): x is SupGroup => !!x);
    if (!cmds.length && !children.length) return null;
    return { label: g.label, cmds, children: children.length ? children : undefined };
  };

  const out: SupLib[] = [];
  for (const lib of libs) {
    if (lib.label.toLowerCase().includes(kw)) {
      out.push(lib);
      continue;
    }
    const groups = lib.groups.map(filterGroup).filter((x): x is SupGroup => !!x);
    if (groups.length) out.push({ ...lib, groups });
  }
  return out;
}

/** 库里第一个命令（搜索后自动展开/定位用） */
export function firstCommand(lib: SupLib): SupCmd | null {
  const walk = (g: SupGroup): SupCmd | null => {
    if (g.cmds.length) return g.cmds[0];
    for (const child of g.children ?? []) {
      const hit = walk(child);
      if (hit) return hit;
    }
    return null;
  };
  for (const g of lib.groups) {
    const hit = walk(g);
    if (hit) return hit;
  }
  return null;
}

