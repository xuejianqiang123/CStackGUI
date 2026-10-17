/* model/store.ts -- 文档 store：doc 唯一真相 + 快照栈撤销（不依赖 React）
 *
 * 为什么独立成模块（2026-10-18 路线图 G1）：
 *   1. 文档操作层要能 **headless 测试**（tools/check-canvas-ops.mjs 直接跑它，
 *      不需要 DOM / Electron）—— 画布此前 0 自动验证，这是第一块验证资产；
 *   2. 协同是长期方向：命令日志（journal）的宿主就是这个 store，它必须独立于
 *      任何 UI 框架存在；
 *   3. App.tsx 只留 UI 胶水（useSyncExternalStore 接回），1030 行的组件体瘦身。
 *
 * 语义与改造前的 App.tsx 逐条对齐（行为零变化）：
 *   - edit = 快照入栈（上限 HISTORY_LIMIT，新编辑清空 redo 分支）+ 应用 mutator；
 *   - beginHistory = 只压栈不改 doc（拖动首帧用，随后逐帧走 apply）；
 *   - apply 只应用 mutator（拖动预览高频路径，每帧一份新 doc）；
 *   - undo/redo 互推快照；replaceDoc 换整份文档（打开/新建/载入外部），历史清空、
 *     dirty 归零。
 *
 * ⚠ 相对说明符必须带 `.js` 后缀（本目录会被 tools/check-format.bat 用 tsc 编到
 *   build 下交给 Node 跑门禁；tsc 不改写说明符、Node ESM 不解析无后缀路径）。
 */
import type { CgDoc } from "./types.js";

/** 撤销栈上限（一步 = 一次编辑；200 步 ≈ 覆盖一次正常的设计会话） */
export const HISTORY_LIMIT = 200;

/** 组件侧一次读走的完整状态。snapshot 对象在无变更时引用稳定
 *  （useSyncExternalStore 的 getSnapshot 必须如此，否则死循环）。 */
export type StoreSnapshot = {
  doc: CgDoc;
  dirty: boolean;
  canUndo: boolean;
  canRedo: boolean;
  /** 每次变更 +1（组件不必比较快照内容也能感知变化） */
  version: number;
};

export type DocStore = {
  /** 当前快照（引用稳定，变更后换新对象） */
  snapshot(): StoreSnapshot;
  /** 订阅变更；返回退订函数 */
  subscribe(fn: () => void): () => void;
  /** 只压撤销栈，不改 doc（拖动首帧记历史用） */
  beginHistory(): void;
  /** 应用一次变更，不压栈（拖动逐帧预览等高频路径） */
  apply(mutator: (d: CgDoc) => void): void;
  /** 一步编辑 = 压栈 + 应用（新编辑清空 redo 分支） */
  edit(mutator: (d: CgDoc) => void): void;
  undo(): void;
  redo(): void;
  /** 换整份文档（打开 / 新建 / 载入外部）：历史清空、dirty 归零 */
  replaceDoc(doc: CgDoc): void;
  /** 保存成功后调：dirty 归零 */
  markSaved(): void;
};

export function newStore(initial: CgDoc): DocStore {
  let doc = initial;
  let dirty = false;
  const past: CgDoc[] = [];
  const future: CgDoc[] = [];
  let version = 0;
  let snap: StoreSnapshot = { doc, dirty, canUndo: false, canRedo: false, version: 0 };
  const listeners = new Set<() => void>();

  function publish() {
    version += 1;
    snap = { doc, dirty, canUndo: past.length > 0, canRedo: future.length > 0, version };
    for (const fn of listeners) fn();
  }

  return {
    snapshot: () => snap,
    subscribe(fn) {
      listeners.add(fn);
      return () => {
        listeners.delete(fn);
      };
    },
    beginHistory() {
      past.push(structuredClone(doc));
      if (past.length > HISTORY_LIMIT) past.shift();
      future.length = 0;
      publish();
    },
    apply(mutator) {
      const next = structuredClone(doc);
      mutator(next);
      doc = next;
      dirty = true;
      publish();
    },
    edit(mutator) {
      past.push(structuredClone(doc));
      if (past.length > HISTORY_LIMIT) past.shift();
      future.length = 0;
      const next = structuredClone(doc);
      mutator(next);
      doc = next;
      dirty = true;
      publish();
    },
    undo() {
      const prev = past.pop();
      if (!prev) return;
      future.push(structuredClone(doc));
      doc = prev;
      dirty = true;
      publish();
    },
    redo() {
      const next = future.pop();
      if (!next) return;
      past.push(structuredClone(doc));
      doc = next;
      dirty = true;
      publish();
    },
    replaceDoc(next) {
      doc = next;
      dirty = false;
      past.length = 0;
      future.length = 0;
      publish();
    },
    markSaved() {
      if (!dirty) return;
      dirty = false;
      publish();
    },
  };
}
