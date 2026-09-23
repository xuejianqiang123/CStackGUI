/* Inspector/BindSection.tsx -- 控件绑定（node.bind）
 *
 * 选源控件（bindIds 由 Inspector.tsx 收集好传进来）、双向开关，以及一行「抄的是值还是勾选」的说明。
 * 说明文字按 KindInfo 分流（checklist / ranged / listLike），所以要 info。
 */
import type { CgNode, KindInfo } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type BindSectionProps = {
  node: CgNode;
  info: KindInfo | undefined;
  /** 可以当绑定源的控件 id（已排序、已排除自己和自己的子树） */
  bindIds: string[];
  patch: Patch;
};

export default function BindSection({ node, info, bindIds, patch }: BindSectionProps) {
  return (
    <>
      <Row label="绑定">
        <select
          className="input"
          value={node.bind?.src ?? ""}
          onChange={(e) =>
            patch((n) => {
              const src = e.target.value;
              if (!src) {
                n.bind = undefined;
                return;
              }
              n.bind = { ...(n.bind ?? { src }), src };
            })
          }
        >
          <option value="">不绑定</option>
          {bindIds.map((id) => (
            <option key={id} value={id}>
              {id}
            </option>
          ))}
        </select>
      </Row>
      {node.bind?.src ? (
        <>
          <Row label="">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={node.bind.two === true}
                onChange={(e) =>
                  patch((n) => {
                    if (n.bind) n.bind = { ...n.bind, two: e.target.checked || undefined };
                  })
                }
              />
              双向（本控件也能改源）
            </label>
          </Row>
          <p className="muted small">
            本控件的{info?.checklist ? "勾选状态" : info?.ranged || (info?.listLike && node.type !== "statusbar") ? "值" : "显示内容"}
            跟着 <code>{node.bind.src}</code> 走。
            {info?.checklist || info?.ranged || (info?.listLike && node.type !== "statusbar")
              ? ""
              : "源是输入框时显示它的文字，否则显示它的数值。"}
            {node.bind.two ? "双向：用户改这里也会推回源。" : "单向：只有源变化时才同步。"}
            {" 同步不会触发 change 回调。"}
          </p>
        </>
      ) : null}
    </>
  );
}
