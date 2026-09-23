/* model/tree.ts -- 控件树的查询与几何（遍历 / 增删 / 命中测试）
 *
 * 模型层的「树操作」：遍历与查找（walk / findNode / findParent / siblings / listNodes）、
 * 增删（detach / removeNode）、从属判定（isInside）、绝对坐标与容器命中（absRect / containerAt）。
 *
 * 依赖：model/types.ts + model/kinds.ts（isContainer）。
 */

import { isContainer } from "./kinds.js";
import type { CgDoc, CgNode, CgRect } from "./types.js";

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
