/* model/ops.ts -- 文档命令层：可序列化的命令对象 + 确定性 apply
 *
 * 为什么命令对象而不是任意闭包（2026-10-18 路线图 G1，协同的架构前置）：
 *   命令 = { type, payload } 的**纯数据** —— JSON 序列化后天然就是操作日志
 *   （journal）：远程重放、录制宏、会话 diff、per-user undo 全挂在同一形态上；
 *   闭包形态下这些能力未来都要重写操作层。协同是本产品的长期方向（团队型开发），
 *   架构前置现在留好，基础设施（CRDT/同步服务）等需求出现再建。
 *
 * 两条纪律（测试钉死，见 tools/check-canvas-ops.mjs）：
 *   1. **构造与应用分离**：make*Cmd 可以读 doc、可以掷随机数（克隆重命名 id）；
 *      applyCmd 必须确定 —— 同一命令在同构 doc 上重放得到同构结果；
 *   2. **命令自包含**：applyCmd 不生成随机数、不读全局状态；id / 坐标全在 payload 里。
 *
 * ⚠ 相对说明符必须带 `.js` 后缀（同 store.ts 的说明）。
 */
import { alignNodes, moveNodes, topLevelOnly, type CgAlignMode } from "./align.js";
import { makeNode } from "./factory.js";
import { cloneNode, uniquifyIds } from "./parse.js";
import { findNode, findParent, removeNode, walk } from "./tree.js";
import type { CgDoc, CgNode } from "./types.js";

/** insert 的一条：节点本体 + 它该挂到哪个容器（null = 顶层） */
export type InsertItem = { node: CgNode; parentId: string | null };

export type CanvasCmd =
  | { type: "insert"; items: InsertItem[] }
  | { type: "remove"; ids: string[] }
  | { type: "move"; ids: string[]; dx: number; dy: number }
  | { type: "align"; ids: string[]; mode: CgAlignMode };

/**
 * @brief 确定性地把一条命令应用到 doc（原地改，调用方负责快照/克隆）
 * @param d 目标文档（已被调用方克隆，见 store.edit）
 * @param cmd 命令对象（应当是 JSON 可往返的纯数据）
 * @return void 无返回值
 * @note 父位不存在（把命令重放到另一份异构 doc）时退顶层，**不丢节点**；
 *       remove 的 id 不存在时由 removeNode 自行跳过 —— 重放幂等。
 */
export function applyCmd(d: CgDoc, cmd: CanvasCmd): void {
  switch (cmd.type) {
    case "insert": {
      for (const it of cmd.items) {
        /* 深拷贝后入树：命令是**纯数据**，绝不与 doc 共享对象 —— 否则应用一次后
         * 命令就被"污染"（节点挂进 doc 后被后续编辑改的其实是命令本体），
         * 重放同一命令对象会携带脏状态（2026-10-18 契约测试抓到的真实缺陷）。 */
        const node = structuredClone(it.node);
        const parent = it.parentId ? findNode(d, it.parentId) : null;
        if (parent) {
          parent.children = parent.children ?? [];
          parent.children.push(node);
        } else {
          d.widgets.push(node);
        }
      }
      return;
    }
    case "remove": {
      for (const id of cmd.ids) removeNode(d, id);
      return;
    }
    case "move": {
      moveNodes(d, cmd.ids, cmd.dx, cmd.dy);
      return;
    }
    case "align": {
      alignNodes(d, cmd.ids, cmd.mode);
      return;
    }
  }
}

/**
 * @brief 构造"在顶层添加一个控件"的命令（id 与位置都在这里定死）
 * @param d 构造时点的文档（只用于避让已有 id）
 * @param type 控件类型
 * @param x 新控件左上角 x（逻辑像素）
 * @param y 新控件左上角 y（逻辑像素）
 * @return CanvasCmd insert 命令（items[0].node 即新控件，调用方可拿它去选中）
 */
export function makeInsertCmd(d: CgDoc, type: string, x: number, y: number): CanvasCmd {
  const node = makeNode(type, x, y);
  const taken = new Set<string>();
  walk(d.widgets, (n) => taken.add(n.id));
  /* 防御：makeId 撞车概率可忽略（毫秒时间戳 + 随机），撞了退序号形态 */
  let i = 2;
  while (taken.has(node.id)) node.id = `${type}_${i++}`;
  return { type: "insert", items: [{ node, parentId: null }] };
}

/**
 * @brief 构造"删除一批控件"的命令（选中集收敛为顶层；无可删目标返回 null）
 * @param d 构造时点的文档
 * @param ids 用户选中的控件 id（子控件与顶层一起选时只删顶层，与旧行为一致）
 * @return CanvasCmd | null remove 命令；没有可删目标时返回 null
 */
export function makeRemoveCmd(d: CgDoc, ids: string[]): CanvasCmd | null {
  const targets = topLevelOnly(d, ids);
  return targets.length ? { type: "remove", ids: targets } : null;
}

/**
 * @brief 构造"复制一批控件"的命令（克隆、子树 id 重排、父位随原控件）
 * @param d 构造时点的文档
 * @param ids 用户选中的控件 id（自动收敛为顶层集合，与旧行为一致）
 * @return { cmd, newIds } | null 没有可复制的目标时返回 null
 * @note 走 parse 的 cloneNode + uniquifyIds —— 子控件的 id 也会重排
 *       （老实现只改顶层，复制一个分组框会造出一堆重复 id）。
 */
export function makeDuplicateCmd(
  d: CgDoc,
  ids: string[],
): { cmd: CanvasCmd; newIds: string[] } | null {
  const targets = topLevelOnly(d, ids);
  if (targets.length === 0) return null;
  const taken = new Set<string>();
  walk(d.widgets, (n) => taken.add(n.id));
  const items: InsertItem[] = [];
  const newIds: string[] = [];
  for (const id of targets) {
    const node = findNode(d, id);
    if (!node) continue;
    const copy = cloneNode(node);
    uniquifyIds(copy, taken); /* taken 会被补进新 id：本批克隆之间也不撞 */
    const parent = findParent(d, id);
    items.push({ node: copy, parentId: parent ? parent.id : null });
    newIds.push(copy.id);
  }
  if (!items.length) return null;
  return { cmd: { type: "insert", items }, newIds };
}
