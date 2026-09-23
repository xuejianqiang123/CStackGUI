/* Inspector/MediaSection.tsx -- 按钮类控件的图片
 *
 * 只有 button / iconbutton 有：图片路径（相对 .cgui）+ 「更换」（Electron 文件对话框）+「清除」。
 * 路径的读写就是 node.src；按钮的图片是背景，iconbutton 是等比居中缩放。
 */
import type { CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type MediaSectionProps = {
  node: CgNode;
  patch: Patch;
  /** Electron 弹文件对话框选图（实现在 Inspector.tsx，因为要用 bridge + patch） */
  onPickImage: () => void;
};

export default function MediaSection({ node, patch, onPickImage }: MediaSectionProps) {
  if (node.type !== "button" && node.type !== "iconbutton") return null;
  return (
    <>
      <Row label="图片">
        <input
          className="input mono"
          value={node.src ?? ""}
          placeholder="assets/picture.png"
          spellCheck={false}
          onChange={(e) =>
            patch((n) => {
              n.src = e.target.value || undefined;
            })
          }
        />
        <button className="mini" title="选择图片文件" onClick={onPickImage}>
          更换
        </button>
        <button
          className="mini"
          title="清除图片"
          onClick={() => patch((n) => { n.src = undefined; })}
        >
          清除
        </button>
      </Row>
      <p className="muted small">
        {node.type === "iconbutton"
          ? "图标等比缩放居中显示（只缩不放，四周各留 4px）；路径相对 .cgui 所在目录，支持 png / jpg / bmp / gif。"
          : "图片拉伸铺满按钮作背景，文字画在图上；路径相对 .cgui 所在目录，支持 png / jpg / bmp / gif。"}
      </p>
    </>
  );
}
