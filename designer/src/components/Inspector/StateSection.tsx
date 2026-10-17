/* Inspector/StateSection.tsx -- 「状态」里的通用三项 + 两种按控件特性分流的取值
 *
 * 可见 / 可用（所有类型都有）；「勾选」按 KindInfo.checklist、「值 / 范围」按 KindInfo.ranged
 * 出现。类型专属的字段（周期、颜色、形状…）在 TypeSection.tsx，不在这里。
 *
 * 2026-10-18（G5）：三个勾选框 + 三个数字输入改用 PanelUi 的 CheckField / NumField。
 * **两个原语义必须逐字保住**（用原语的参数表达，不能靠"统一一下更整齐"顺手改掉）：
 *   - 这里的 value/min/max **不取整**（原代码是 `Number(v) || 0`）→ round={false}；
 *   - max 的兜底是 **1 不是 0**（原代码 `Number(v) || 1`）→ fallback={1}。
 */
import type { CgNode, KindInfo } from "../../model";
import { CheckField, NumField, Row, type Patch } from "./PanelUi";

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
        <CheckField
          checked={node.visible !== false}
          label="显示"
          onChange={(v) =>
            patch((n) => {
              n.visible = v ?? true;
            })
          }
        />
      </Row>
      <Row label="可用">
        <CheckField
          checked={node.enabled !== false}
          label="可点击"
          onChange={(v) =>
            patch((n) => {
              n.enabled = v ?? true;
            })
          }
        />
      </Row>
      {info?.checklist ? (
        <Row label="勾选">
          <CheckField
            checked={!!node.checked}
            label="初始选中"
            onChange={(v) =>
              patch((n) => {
                n.checked = v ?? false;
              })
            }
          />
        </Row>
      ) : null}
      {info?.ranged ? (
        <>
          <Row label="值">
            <NumField
              value={node.value}
              round={false}
              onChange={(v) =>
                patch((n) => {
                  n.value = v ?? 0;
                })
              }
            />
          </Row>
          <Row label="范围">
            <NumField
              value={node.min}
              round={false}
              onChange={(v) =>
                patch((n) => {
                  n.min = v ?? 0;
                })
              }
            />
            <span className="muted">—</span>
            <NumField
              value={node.max ?? 100}
              round={false}
              fallback={1}
              onChange={(v) =>
                patch((n) => {
                  n.max = v ?? 1;
                })
              }
            />
          </Row>
        </>
      ) : null}
    </>
  );
}
