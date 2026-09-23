/* Inspector/StateSection.tsx -- 「状态」里的通用三项 + 两种按控件特性分流的取值
 *
 * 可见 / 可用（所有类型都有）；「勾选」按 KindInfo.checklist、「值 / 范围」按 KindInfo.ranged
 * 出现。类型专属的字段（周期、颜色、形状…）在 TypeSection.tsx，不在这里。
 */
import type { CgNode, KindInfo } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type StateSectionProps = {
  node: CgNode;
  info: KindInfo | undefined;
  patch: Patch;
};

export default function StateSection({ node, info, patch }: StateSectionProps) {
  return (
    <>
      <div className="insp-section">状态</div>
      <Row label="可见">
        <label className="check-inline">
          <input
            type="checkbox"
            checked={node.visible !== false}
            onChange={(e) =>
              patch((n) => {
                n.visible = e.target.checked;
              })
            }
          />
          显示
        </label>
      </Row>
      <Row label="可用">
        <label className="check-inline">
          <input
            type="checkbox"
            checked={node.enabled !== false}
            onChange={(e) =>
              patch((n) => {
                n.enabled = e.target.checked;
              })
            }
          />
          可点击
        </label>
      </Row>
      {info?.checklist ? (
        <Row label="勾选">
          <label className="check-inline">
            <input
              type="checkbox"
              checked={!!node.checked}
              onChange={(e) =>
                patch((n) => {
                  n.checked = e.target.checked;
                })
              }
            />
            初始选中
          </label>
        </Row>
      ) : null}
      {info?.ranged ? (
        <>
          <Row label="值">
            <input
              className="input"
              type="number"
              value={node.value ?? 0}
              onChange={(e) =>
                patch((n) => {
                  n.value = Number(e.target.value) || 0;
                })
              }
            />
          </Row>
          <Row label="范围">
            <input
              className="input narrow"
              type="number"
              value={node.min ?? 0}
              onChange={(e) =>
                patch((n) => {
                  n.min = Number(e.target.value) || 0;
                })
              }
            />
            <span className="muted">—</span>
            <input
              className="input narrow"
              type="number"
              value={node.max ?? 100}
              onChange={(e) =>
                patch((n) => {
                  n.max = Number(e.target.value) || 1;
                })
              }
            />
          </Row>
        </>
      ) : null}
    </>
  );
}
