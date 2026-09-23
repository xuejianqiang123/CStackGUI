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
