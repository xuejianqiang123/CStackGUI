/* Inspector/GeometrySection.tsx -- 位置与尺寸（含余量分配 grow）
 *
 * .cgui 里就是 rect.x/y/w/h，只是面板上用了中文标签（左边 / 顶边 / 宽度 / 高度）。
 * 菜单栏没有矩形，所以这里只给一句说明。
 * 末尾的「余量分配」是 grow 字段：容器变大时的空间权重，只给数值 + 说明，
 * 不在画布里预览（那等于在画布实现一遍布局引擎，会毁掉所见即所得）。
 * 说明文案按契约 §2 的**按带分配**语义写（位置重叠 = 同一行/列算一条带；带内设了
 * 余量的各自就地变大、没设的原地不动；只有排在这一带之后的兄弟整体平移）。
 */
import type { CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

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
      <Row
        label="余量分配"
        hint="容器变大时，多出来的空间按这个权重分给同一容器里的兄弟控件；0 = 不参与。横向竖向都用这一个权重"
      >
        <input
          className="input narrow"
          type="number"
          min={0}
          max={99}
          value={node.grow ?? 0}
          onChange={(e) =>
            patch((n) => {
              const v = Math.max(0, Math.min(99, Math.round(Number(e.target.value) || 0)));
              if (v > 0) n.grow = v;
              else delete n.grow;
            })
          }
        />
      </Row>
      <p className="muted small">
        0 = 不参与。窗口或父容器变大时，多出来的空间按权重分给同一容器里设了余量的控件：
        它自己变大；与它位置重叠的兄弟（同一行 / 同一列，也就是文档说的「同一条带」）原地不动，
        排在这一行 / 列之后的兄弟整体平移。
      </p>
      <p className="muted small">
        两个方向都会吃 —— 只想竖向跟着变，就给一块右（下）边界已经是这条带最外沿的控件（通常就是全宽那块）：
        它变宽 / 变高只是往外长，压不到带内兄弟。给边界还在带内的那块，它就会长到带内更靠外的兄弟身上。
      </p>
      <p className="muted small">画布里不预览效果，运行时才生效。</p>
      {node.type === "menu" || node.type === "statusbar" || node.type === "toolbar" ? (
        <p className="muted small">菜单栏 / 状态栏 / 工具栏的位置尺寸由系统或内容决定，通常别给余量分配。</p>
      ) : null}
    </>
  );
}
