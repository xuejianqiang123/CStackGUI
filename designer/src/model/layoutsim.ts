/* model/layoutsim.ts -- grow 预演：把 runtime 的带分配算法移植到画布端做只读模拟
 *
 * 为什么存在（2026-10-18 路线图 G3'）：grow 是窗口拉伸的核心机制，但设计时看不见效果 ——
 * 只能导出 + F5 编译运行才能确认（30 秒反馈回路）。本模块把 `runtime/src/core/cg_layout.c`
 * 的算法**逐语义移植**成 TS 纯函数，画布端就能在"预演"模式下模拟"窗口变成 WxH 之后
 * 界面长什么样"，设计坐标一个字节都不动。
 *
 * 与运行时的一致性纪律：算法是**契约的镜像**，不是独立实现 ——
 * 契约原文见 `.codebuddy/plans/grow-layout.md` §2 与 CGUI-FORMAT §2.1.1。
 * 改那边的算法必须同步这里（check-canvas-ops.mjs 钉了同一批性质断言）。
 *
 * 语义要点（移植时逐条对齐过的）：
 *   - 两个轴独立处理（先横后竖）；每次**从基准重算**，不做增量累加（来回拉伸不漂移）；
 *   - 单轴按"带"分配：位置区间重叠的兄弟 = 同一条带（同一行/列），带与带之间按权重分，
 *     带内每个 grow>0 的成员**各自**把这一带填满；
 *   - 分配沿轴累积（前面的带吃了，后面的带整体平移）；带内成员先按"更早的带"的累计位移摆正；
 *   - 最后一条带吃余数（一个像素都不许丢）；缩小压到 0 为止，不出负数；
 *   - 整数除法向零截断（与 C 的 int 除法一致：Math.trunc）。
 *
 * ⚠ 相对说明符必须带 `.js` 后缀（同 store.ts 的说明）。
 */
import { KIND_MAP } from "./kinds.js";
import type { CgDoc, CgNode, CgRect } from "./types.js";

/** .cgui 的 grow 上限（与 runtime CG_LAYOUT_MAX_WEIGHT 一致） */
export const SIM_MAX_WEIGHT = 99;

/** 预演用的布局单元：进来是基准坐标，出去是算好的坐标 */
export type SimBox = { x: number; y: number; w: number; h: number; grow: number };

/** 夹取 grow 到合法范围（契约容错口径：负数/超上限归边界） */
function weightOf(grow: number): number {
  if (!Number.isFinite(grow)) return 0;
  const g = Math.trunc(grow);
  return g < 0 ? 0 : g > SIM_MAX_WEIGHT ? SIM_MAX_WEIGHT : g;
}

/**
 * @brief 在**一个轴**上分配余量（与 runtime layout_axis 同构）
 * @param boxes 布局单元（原地改；进基准、出结果）
 * @param axis 0 = 横轴（x/w），1 = 竖轴（y/h）
 * @param delta 该轴上"容器变大了多少"（负 = 缩小）；0 直接返回
 */
export function simAxis(boxes: SimBox[], axis: 0 | 1, delta: number): void {
  const pos = (b: SimBox) => (axis ? b.y : b.x);
  const size = (b: SimBox) => (axis ? b.h : b.w);
  const setSize = (b: SimBox, v: number) => {
    if (axis) b.h = v;
    else b.w = v;
  };
  const setPos = (b: SimBox, v: number) => {
    if (axis) b.y = v;
    else b.x = v;
  };

  const count = boxes.length;
  if (count <= 0 || delta === 0) return;
  let sum = 0;
  for (const b of boxes) sum += weightOf(b.grow);
  if (sum === 0) return;

  /* 按该轴位置升序（稳定插入排序，与 C 版一致）—— 用索引数组，不重排调用方的数组 */
  const idx = boxes.map((_, i) => i);
  for (let i = 1; i < count; i++) {
    const key = idx[i];
    let j = i - 1;
    while (j >= 0 && pos(boxes[idx[j]]) > pos(boxes[key])) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = key;
  }

  let sumLeft = sum;
  let given = 0;
  let shift = 0;
  let i = 0;
  while (i < count) {
    /* 1) 找出这一带：起点落在带尾之前的都属于同一条带 */
    let bandEnd = pos(boxes[idx[i]]) + size(boxes[idx[i]]);
    let bandWeight = weightOf(boxes[idx[i]].grow);
    let j = i + 1;
    for (; j < count; j++) {
      const p = pos(boxes[idx[j]]);
      const e = p + size(boxes[idx[j]]);
      if (p >= bandEnd) break;
      if (e > bandEnd) bandEnd = e;
      bandWeight += weightOf(boxes[idx[j]].grow);
    }

    /* 2) 这一带吃多少：按剩余权重分，最后一条带吃余数（整数除法向零截断，同 C） */
    let take = 0;
    if (bandWeight > 0 && sumLeft > 0) {
      take =
        bandWeight >= sumLeft
          ? delta - given
          : Math.trunc(((delta - given) * bandWeight) / sumLeft);
    }

    /* 3) 带内成员先按"更早的带"累计的位移摆正（必须在累加本带 take 之前） */
    for (let k = i; k < j; k++) setPos(boxes[idx[k]], pos(boxes[idx[k]]) + shift);

    /* 4) 带内每个 grow 成员都吃这一份；实际吃到的量可能被"压到 0"夹过 */
    if (take !== 0) {
      let applied = take;
      for (let k = i; k < j; k++) {
        const b = boxes[idx[k]];
        if (weightOf(b.grow) === 0) continue;
        const newsz = Math.max(0, size(b) + take);
        const d = newsz - size(b);
        if (take < 0 ? d > applied : d < applied) applied = d;
        setSize(b, newsz);
      }
      shift += applied;
      given += applied;
    }

    sumLeft -= bandWeight;
    i = j;
  }
}

/**
 * @brief 与 runtime `cg_layout_apply` 对齐：两轴独立、从基准重算
 * @param boxes 布局单元（进基准坐标，出算好的坐标）
 * @param baseW 容器基准宽（设计宽）
 * @param baseH 容器基准高（设计高）
 * @param nowW 容器当前宽
 * @param nowH 容器当前高
 */
export function simApply(boxes: SimBox[], baseW: number, baseH: number, nowW: number, nowH: number): void {
  if (!boxes.length) return;
  simAxis(boxes, 0, nowW - baseW);
  simAxis(boxes, 1, nowH - baseH);
}

/**
 * @brief 模拟"窗口变成 nowW x nowH"之后整份文档的控件矩形（只读，不改 doc）
 * @param doc 设计文档（rect 即基准）
 * @param nowW 模拟后的窗口宽（逻辑像素）
 * @param nowH 模拟后的窗口高（逻辑像素）
 * @return Map<string, CgRect> 控件 id -> 新的**相对父容器**矩形
 * @note 与 runtime 基准口径一致（grow-layout.md §3）：基准 rects = .cgui 设计坐标，
 *       基准容器尺寸 = app.width/height；面板（group/box/card）用自己的基准尺寸递归。
 *       tabs/dialog 的子树不参与（它们的 children 语义特殊，预演保持保守）。
 */
export function simulateResize(doc: CgDoc, nowW: number, nowH: number): Map<string, CgRect> {
  const out = new Map<string, CgRect>();

  const simContainer = (nodes: CgNode[], baseW: number, baseH: number, w: number, h: number) => {
    const boxes: SimBox[] = nodes.map((n) => ({
      x: n.rect.x,
      y: n.rect.y,
      w: n.rect.w,
      h: n.rect.h,
      grow: weightOf(n.grow ?? 0),
    }));
    simApply(boxes, baseW, baseH, w, h);
    nodes.forEach((n, i) => {
      const b = boxes[i];
      out.set(n.id, { x: b.x, y: b.y, w: b.w, h: b.h });
      if (n.children?.length && (KIND_MAP[n.type]?.container ?? false) && n.type !== "tabs" && n.type !== "dialog") {
        simContainer(n.children, n.rect.w, n.rect.h, b.w, b.h);
      }
    });
  };

  simContainer(doc.widgets, doc.app.width, doc.app.height, nowW, nowH);
  return out;
}
