/* Inspector/PanelUi.tsx -- 属性面板的通用渲染小件（各 section 共用的壳）
 *
 * Row = 「左标签 + 右控件」一行；Area = 树/表格/条目用的多行文本编辑器。
 * 这里不持有任何状态：状态一律留在 Inspector.tsx，靠 props 传进来。
 */
import type { CgNode } from "../../model";

/** 改文档的统一回调形状：Inspector.tsx 里实现（patch 先压撤销快照、patchSoft 不压） */
export type Patch = (fn: (n: CgNode) => void) => void;

/** @param hint 可选说明，鼠标停在标签上时显示（字段含义较长时用，别把面板挤成一团） */
export function Row({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
  return (
    <div className="insp-row">
      <span className="insp-label" title={hint}>
        {label}
      </span>
      <span className="insp-value">{children}</span>
    </div>
  );
}

/* ---------------------------------------------------------------- 字段原语
 * 这三个是「G5 Inspector 数据驱动」真正该交付的东西：各 section 里逐字重复的是
 * **解析与写入口径**（Number||0 / 夹取 / 0 就删字段），而不是字段清单本身。
 * 刻意**不**建 fieldSpecs 描述表：那会造出第二张手工镜像表，重蹈 supportlib.ts
 * 的漂移覆辙（8 类字段本来就禁止数据化，见 canvas-longterm-roadmap.md §G5）。
 * 原语只统一机械部分（解析/夹取/写还是删），字段语义仍留在各 section —— 那是真差异。
 */

/** 下拉选项：`value` 是字符串（HTML option 的口径），`label` 给人看 */
export type Opt = { value: string; label: string };

/** 数字：统一「解析 → 取整 → 夹取」。
 *  omitOnZero：值为 0 时回调 undefined —— 调用方据此 delete 字段
 *  （与 serialize 的"缺省不写"同一口径：0 和不存在必须等价）。 */
export function NumField({
  value,
  onChange,
  min,
  max,
  step,
  round = true,
  fallback = 0,
  className = "input narrow",
  disabled,
  omitOnZero,
}: {
  value: number | undefined;
  onChange: (v: number | undefined) => void;
  min?: number;
  max?: number;
  /** 原生 step（周期/点数这类按百、按十跳的字段要它） */
  step?: number;
  round?: boolean;
  /** 解析不出数字（或就是 0）时的兜底值。默认 0；**各 section 原来的兜底并不一致**
   *  （StateSection 的 max 是 1），保留这个口子才能逐字保住原行为。 */
  fallback?: number;
  className?: string;
  disabled?: boolean;
  omitOnZero?: boolean;
}) {
  return (
    <input
      className={className}
      type="number"
      value={value ?? 0}
      min={min}
      max={max}
      step={step}
      disabled={disabled}
      onChange={(e) => {
        let v = Number(e.target.value) || fallback;
        if (round) v = Math.round(v);
        if (min !== undefined && v < min) v = min;
        if (max !== undefined && v > max) v = max;
        onChange(omitOnZero && v === 0 ? undefined : v);
      }}
    />
  );
}

/** 下拉：统一「数字 → 字符串」的显示与「字符串 → 数字」的解析（含 omitOnZero）。 */
export function SelectField({
  value,
  options,
  onChange,
  className = "input",
  disabled,
  omitOnZero,
}: {
  value: number | string | undefined;
  options: ReadonlyArray<Opt>;
  onChange: (v: number | undefined) => void;
  className?: string;
  disabled?: boolean;
  omitOnZero?: boolean;
}) {
  return (
    <select
      className={className}
      value={String(value ?? 0)}
      disabled={disabled}
      onChange={(e) => {
        const v = Number(e.target.value) || 0;
        onChange(omitOnZero && v === 0 ? undefined : v);
      }}
    >
      {options.map((o) => (
        <option key={o.value} value={o.value}>
          {o.label}
        </option>
      ))}
    </select>
  );
}

/** 勾选：受控 checked。
 *  mode=always：true/false 都写（visible / enabled / checked 的口径）；
 *  mode=omitFalse：false 回调 undefined（multiline 这类"false 就是删字段"）。 */
export function CheckField({
  checked,
  onChange,
  label,
  disabled,
  mode = "always",
}: {
  checked: boolean;
  onChange: (v: boolean | undefined) => void;
  /** 勾选框右边的文字。**给 ReactNode 而不是 string**：各 section 原来的包法不一致
   *  （StateSection 是裸文字、EditSection 是 muted small），原语不该替调用方决定观感。 */
  label?: React.ReactNode;
  disabled?: boolean;
  mode?: "always" | "omitFalse";
}) {
  return (
    <label className="check-inline">
      <input
        type="checkbox"
        checked={checked}
        disabled={disabled}
        onChange={(e) => onChange(mode === "omitFalse" && !e.target.checked ? undefined : e.target.checked)}
      />
      {label ?? null}
    </label>
  );
}

/** 多行内容的编辑器：树/表格/条目都用纯文本编辑，格式简单、一眼能看懂。
 *  撤销栈只在获得焦点时压一次，避免每敲一个字都进历史。 */
export function Area({
  label,
  hint,
  value,
  rows = 4,
  onFocus,
  onChange,
}: {
  label: string;
  hint: string;
  value: string;
  rows?: number;
  onFocus: () => void;
  onChange: (v: string) => void;
}) {
  return (
    <div className="insp-area">
      <div className="insp-area-head">
        <span>{label}</span>
        <span className="muted small">{hint}</span>
      </div>
      <textarea
        className="input mono insp-textarea"
        rows={rows}
        value={value}
        spellCheck={false}
        onFocus={onFocus}
        onChange={(e) => onChange(e.target.value)}
      />
    </div>
  );
}
