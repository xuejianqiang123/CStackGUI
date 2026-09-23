/* model/math.ts -- 画布用的小数值工具（夹取 / 吸附网格）
 *
 * 模型层最末的小函数：clamp 夹到区间、snap 拖拽时对齐网格。
 * 不依赖模型层其它文件。
 */

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
export const snap = (v: number, grid: number, on: boolean) => (on ? Math.round(v / grid) * grid : Math.round(v));
