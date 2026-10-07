/* model/align.ts -- 多选的对齐 / 分布（纯几何，只改 rect）
 *
 * 对齐基准是**选中项的外接框**（业界通例），不引入「主选框」概念 —— 省得解释基准是哪一个。
 *
 * 两条铁律（踩了会静默算错，不会有任何报错）：
 *  1. 先归一化：`topLevelOnly()` 剔掉「祖先也被选中」的那些。控件坐标是**相对父容器**的，
 *     容器和它的子控件同时被选中时若各写一次坐标，子控件会被父容器带着再动一次 = 双倍位移。
 *  2. 写回必须减掉**父容器当前的绝对原点**（`absRect` 的口径），不能把绝对坐标直接写进 rect。
 */
import { clamp } from "./math.js";
import { absRect, findNode, findParent, isInside } from "./tree.js";
import type { CgDoc, CgNode, CgRect } from "./types.js";

export type CgAlignMode =
  | "left" | "hcenter" | "right"
  | "top" | "vcenter" | "bottom"
  | "hdist" | "vdist"
  | "samew" | "sameh" | "samesize";

/** 只保留「最外层」的那几个：祖先也被选中的节点直接丢掉（见文件头铁律 1）。 */
export function topLevelOnly(doc: CgDoc, ids: string[]): string[] {
  const uniq = [...new Set(ids)];
  return uniq.filter((id) => !uniq.some((other) => other !== id && isInside(doc, id, other)));
}

/** 选中项的外接框（绝对坐标）；一个都找不到返回 null。 */
export function selectionBounds(doc: CgDoc, ids: string[]): CgRect | null {
  let x0 = Infinity;
  let y0 = Infinity;
  let x1 = -Infinity;
  let y1 = -Infinity;
  let any = false;
  for (const id of ids) {
    const n = findNode(doc, id);
    if (!n) continue;
    const r = absRect(doc, n);
    x0 = Math.min(x0, r.x);
    y0 = Math.min(y0, r.y);
    x1 = Math.max(x1, r.x + r.w);
    y1 = Math.max(y1, r.y + r.h);
    any = true;
  }
  return any ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : null;
}

/** 把绝对坐标写回成「相对父容器」的坐标：对齐/分布唯一的写入出口。 */
function putAbs(doc: CgDoc, node: CgNode, x: number, y: number) {
  const p = findParent(doc, node.id);
  const base = p ? absRect(doc, p) : { x: 0, y: 0 };
  node.rect.x = Math.round(x - base.x);
  node.rect.y = Math.round(y - base.y);
}

/**
 * 对选中项做一次对齐 / 分布 / 统一尺寸（**原地改 doc**，调用方放进 `onApply` 的 mutator 里）。
 *
 * @returns 真正被改动的节点数。0 表示选择不足（对齐需 ≥2、分布需 ≥3），调用方不必压历史。
 */
export function alignNodes(doc: CgDoc, ids: string[], mode: CgAlignMode): number {
  const sel = topLevelOnly(doc, ids);
  const nodes: CgNode[] = [];
  for (const id of sel) {
    const n = findNode(doc, id);
    if (n) nodes.push(n);
  }
  if (nodes.length < 2) return 0;
  if ((mode === "hdist" || mode === "vdist") && nodes.length < 3) return 0;

  const box = selectionBounds(doc, sel);
  if (!box) return 0;

  /* 先把绝对坐标一次性量完再写：写的过程中别的节点会动，边量边写会用到被改过的值 */
  const abs = new Map<string, CgRect>();
  for (const n of nodes) abs.set(n.id, absRect(doc, n));
  const at = (n: CgNode) => abs.get(n.id) as CgRect;

  switch (mode) {
    case "left":
      for (const n of nodes) putAbs(doc, n, box.x, at(n).y);
      break;
    case "hcenter":
      for (const n of nodes) putAbs(doc, n, box.x + (box.w - n.rect.w) / 2, at(n).y);
      break;
    case "right":
      for (const n of nodes) putAbs(doc, n, box.x + box.w - n.rect.w, at(n).y);
      break;
    case "top":
      for (const n of nodes) putAbs(doc, n, at(n).x, box.y);
      break;
    case "vcenter":
      for (const n of nodes) putAbs(doc, n, at(n).x, box.y + (box.h - n.rect.h) / 2);
      break;
    case "bottom":
      for (const n of nodes) putAbs(doc, n, at(n).x, box.y + box.h - n.rect.h);
      break;
    case "hdist": {
      /* 首尾两个原地不动，中间的按「间隙相等」重排（总空隙 = 外接框宽 - 所有宽之和） */
      const order = [...nodes].sort((a, b) => at(a).x - at(b).x);
      const total = order.reduce((s, n) => s + n.rect.w, 0);
      const gap = (box.w - total) / (order.length - 1);
      let cur = box.x;
      for (const n of order) {
        putAbs(doc, n, cur, at(n).y);
        cur += n.rect.w + gap;
      }
      break;
    }
    case "vdist": {
      const order = [...nodes].sort((a, b) => at(a).y - at(b).y);
      const total = order.reduce((s, n) => s + n.rect.h, 0);
      const gap = (box.h - total) / (order.length - 1);
      let cur = box.y;
      for (const n of order) {
        putAbs(doc, n, at(n).x, cur);
        cur += n.rect.h + gap;
      }
      break;
    }
    /* 统一尺寸取**最大值**，不是"以第一个选中的为准"：把小的放大比把大的缩小安全，
       不会把已经放好的文字切掉。按钮 tooltip 里写明这条。 */
    case "samew": {
      const w = Math.max(...nodes.map((n) => n.rect.w));
      for (const n of nodes) n.rect.w = w;
      break;
    }
    case "sameh": {
      const h = Math.max(...nodes.map((n) => n.rect.h));
      for (const n of nodes) n.rect.h = h;
      break;
    }
    case "samesize": {
      const w = Math.max(...nodes.map((n) => n.rect.w));
      const h = Math.max(...nodes.map((n) => n.rect.h));
      for (const n of nodes) {
        n.rect.w = w;
        n.rect.h = h;
      }
      break;
    }
  }
  return nodes.length;
}

/** 批量平移（方向键微移）：每个节点各自夹在自己的父容器内，越界即停。 */
export function moveNodes(doc: CgDoc, ids: string[], dx: number, dy: number): number {
  const sel = topLevelOnly(doc, ids);
  let moved = 0;
  for (const id of sel) {
    const n = findNode(doc, id);
    if (!n) continue;
    const p = findParent(doc, id);
    const maxW = p ? p.rect.w : doc.app.width;
    const maxH = p ? p.rect.h : doc.app.height;
    n.rect.x = clamp(n.rect.x + dx, 0, Math.max(0, maxW - n.rect.w));
    n.rect.y = clamp(n.rect.y + dy, 0, Math.max(0, maxH - n.rect.h));
    moved += 1;
  }
  return moved;
}
