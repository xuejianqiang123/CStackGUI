/* Inspector/HintSection.tsx -- 悬停提示（node.hint）
 *
 * 所有类型通用（tooltip），只有 timer / menu 例外 —— 它们没有原生窗口，提示挂不上去。
 */
import type { CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type HintSectionProps = {
  node: CgNode;
  patch: Patch;
};

export default function HintSection({ node, patch }: HintSectionProps) {
  return (
    <>
      {/* timer / menu / serial 没有原生窗口（timer 见 cg_win_ctl.c:63 直接跳过；
          menu 无矩形、tooltip 靠窗口句柄挂，见 cg_win_create.c:107；
          serial 是无界面控件，运行时不建窗口），悬停提示永远挂不上，故不显示。 */}
      {node.type === "timer" || node.type === "menu" || node.type === "serial" ? null : (
        <Row label="悬停提示">
          <input
            className="input"
            value={node.hint ?? ""}
            placeholder="鼠标停上去显示的文字"
            onChange={(e) =>
              patch((n) => {
                n.hint = e.target.value || undefined;
              })
            }
          />
        </Row>
      )}
    </>
  );
}
