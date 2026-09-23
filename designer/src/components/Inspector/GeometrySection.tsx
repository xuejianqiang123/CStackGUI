/* Inspector/GeometrySection.tsx -- 位置与尺寸
 *
 * .cgui 里就是 rect.x/y/w/h，只是面板上用了中文标签（左边 / 顶边 / 宽度 / 高度）。
 * 菜单栏没有矩形，所以这里只给一句说明。
 */
import type { CgNode } from "../../model";
import type { Patch } from "./PanelUi";

/* 位置与尺寸的中文标签（.cgui 里仍是 rect.x/y/w/h，只是面板上好认） */
const RECT_LABEL: Record<"x" | "y" | "w" | "h", string> = {
  x: "左边",
  y: "顶边",
  w: "宽度",
  h: "高度",
};

export type GeometrySectionProps = {
  node: CgNode;
  patch: Patch;
};

export default function GeometrySection({ node, patch }: GeometrySectionProps) {
  return (
    <>
      <div className="insp-section">位置与尺寸</div>
      {node.type === "menu" ? (
        <p className="muted small">菜单栏挂在窗口上，没有位置和尺寸可调。</p>
      ) : null}
      <div className="insp-grid4">
        {(["x", "y", "w", "h"] as const).map((k) => (
          <label key={k} className="insp-num">
            <span>{RECT_LABEL[k]}</span>
            <input
              className="input"
              type="number"
              value={Math.round(node.rect[k])}
              onChange={(e) =>
                patch((n) => {
                  n.rect[k] = Number(e.target.value) || 0;
                })
              }
            />
          </label>
        ))}
      </div>
    </>
  );
}
