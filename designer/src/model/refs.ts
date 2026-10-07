/* model/refs.ts -- 两张「按名字引用」的表的引用统计 + 改名连带更新的纯函数
 *
 * .cgui 里有两处以**名字**引用另一张表：
 *   styles{}  <- 控件的 style（三种形态里带名字的两种：`"名字"` 与 `{ "use": "名字", ...覆盖 }`）
 *   actions{} <- 控件的 action，以及树节点 / 菜单项的 action（CgTreeItem.action）
 *
 * 名字一改，引用就悬空：运行时不报错，而是按"这张表里没有这个名字"静默处理 ——
 * 表现是**外观悄悄变了**（或动作失效），只有 --selftest 才会报「引用失效」。
 * 所以改名必须把引用一起改掉，这里两个函数就是干这个的。
 *
 * 为什么两张表共用一套遍历：两个对话框（样式 / 动作）要的东西一模一样，
 * 各写一份迟早分叉 —— 分叉的那天就是"样式改了引用、动作忘了改"。
 *
 * 依赖：model/types.ts + model/tree.ts（walk / flattenItems）+ model/style.ts（styleUseName）。
 */
import { styleUseName } from "./style.js";
import { flattenItems, walk } from "./tree.js";
import type { CgDoc } from "./types.js";

/** 两张表各被引用了多少处（**引用点**口径：一个控件算一处、一个菜单项算一处）。 */
export type CgRefCounts = {
  /** 样式名 -> 引用它的控件数 */
  styles: Map<string, number>;
  /** 动作名 -> 引用它的地方数 */
  actions: Map<string, number>;
};

function bump(map: Map<string, number>, name: string) {
  if (!name) return;
  map.set(name, (map.get(name) ?? 0) + 1);
}

/** 扫一遍文档统计两张表的引用点。对话框用它显示「被 N 处引用」/「没人引用」，
 *  让使用者在改名或删除之前先看见影响面。 */
export function countRefs(doc: CgDoc): CgRefCounts {
  const styles = new Map<string, number>();
  const actions = new Map<string, number>();
  walk(doc.widgets, (node) => {
    bump(styles, styleUseName(node.style));
    bump(actions, (node.action ?? "").trim());
    /* 菜单栏 / 菜单按钮 / 树的节点都可能挂动作（flattenItems 已经递归进各层 children） */
    for (const { item } of flattenItems(node.nodes)) bump(actions, (item.action ?? "").trim());
  });
  return { styles, actions };
}

/** 把样式引用从旧名改到新名，返回改了几处。
 *  「内联对象」形态本来就没引用，不经手。 */
export function renameStyleRefs(doc: CgDoc, from: string, to: string): number {
  let n = 0;
  walk(doc.widgets, (node) => {
    const s = node.style;
    if (typeof s === "string") {
      if (s.trim() === from) {
        node.style = to;
        n += 1;
      }
      return;
    }
    if (s && typeof s === "object" && typeof s.use === "string" && s.use.trim() === from) {
      /* 原地改 `use`，不重建对象：保住 use 的位置与其余覆盖字段的键顺序（.cgui 往返稳定） */
      s.use = to;
      n += 1;
    }
  });
  return n;
}

/** 把动作引用从旧名改到新名，返回改了几处。
 *  控件的 action 与树节点/菜单项的 action 都算，两处口径必须一致。 */
export function renameActionRefs(doc: CgDoc, from: string, to: string): number {
  let n = 0;
  const swap = (holder: { action?: string }) => {
    if ((holder.action ?? "").trim() !== from) return;
    holder.action = to;
    n += 1;
  };
  walk(doc.widgets, (node) => {
    swap(node);
    for (const { item } of flattenItems(node.nodes)) swap(item);
  });
  return n;
}
