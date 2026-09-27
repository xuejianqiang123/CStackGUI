/* Inspector.tsx -- 属性面板：改 id、文字、位置尺寸、字体、颜色、状态
 *
 * 每个字段都直接改文档模型，改完即可保存，没有「应用」按钮。
 *
 * 拆分说明：大段渲染按字段族切到了 ./Inspector/*Section.tsx。
 * **状态与 hooks 全留在本文件**（唯一的 useState 是字体选择器的展开状态），
 * patch / patchSoft / pickImage / bindIds / inTabs 都在这儿算好，按需用显式 props 传给各 section。
 * 下面的组合顺序 = 面板从上到下的视觉顺序，与拆分前逐字一致。
 */
import { useState } from "react";
import { getBridge } from "../bridge";
import { KIND_MAP, findParent, type CgDoc, type CgNode } from "../model";
import ActionSection from "./Inspector/ActionSection";
import BindSection from "./Inspector/BindSection";
import ContentSection from "./Inspector/ContentSection";
import EditSection from "./Inspector/EditSection";
import GeometrySection from "./Inspector/GeometrySection";
import HintSection from "./Inspector/HintSection";
import IdentitySection from "./Inspector/IdentitySection";
import MediaSection from "./Inspector/MediaSection";
import StateSection from "./Inspector/StateSection";
import StyleSection from "./Inspector/StyleSection";
import TypeSection from "./Inspector/TypeSection";

type Props = {
  doc: CgDoc;
  node: CgNode | null;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  onSelect: (id: string | null) => void;
};

function findAny(nodes: CgNode[], id: string): CgNode | null {
  for (const n of nodes) {
    if (n.id === id) return n;
    const hit = n.children ? findAny(n.children, id) : null;
    if (hit) return hit;
  }
  return null;
}

/** 收集能当绑定源的控件 id（已排序）。
 *  排除自己和自己内部：绑自己没有意义，也不会是用户想要的结果。 */
function collectBindIds(nodes: CgNode[], skipId: string): string[] {
  const out: string[] = [];
  const walk = (ns: CgNode[]) => {
    for (const n of ns) {
      if (n.id === skipId) continue;   /* 连它内部一起跳过 */
      out.push(n.id);
      if (n.children) walk(n.children);
    }
  };
  walk(nodes);
  return out.sort();
}

export default function Inspector({ doc, node, onBeginHistory, onApply, onSelect }: Props) {
  /** 字体选择器展开状态：换控件时保持原状，点 Aa 图标收起/展开。
   *  **必须放在下面那个 `if (!node) return` 之前**：hook 不能条件调用，而"有没有选中"
   *  是调用方的条件（App.tsx 只在选中时挂载 Inspector）—— 一旦有人无条件挂载，
   *  放在 return 之后就是"Hooks 数量不一致"的运行期崩溃（2026-09-24 结构审查发现）。 */
  const [fontOpen, setFontOpen] = useState(false);

  if (!node) {
    return (
      <div className="panel-body">
        <p className="muted">选中一个控件后，这里显示它的属性。</p>
        <div className="hint-box">
          <div className="hint-title">画布在做什么</div>
          <ul className="hint-list">
            <li>这里的每个字段都会写进 .cgui</li>
            <li>.cgui 由 C 运行时解析，控件就是原生 Win32 控件</li>
            <li>事件名对应 C 侧的 <code>CG_BIND("名字", 函数)</code></li>
          </ul>
        </div>
      </div>
    );
  }

  const patch = (fn: (n: CgNode) => void) => {
    onBeginHistory();
    onApply((d) => {
      const t = findAny(d.widgets, node.id);
      if (t) fn(t);
    });
  };

  const info = KIND_MAP[node.type];
  /** 选背景图（按钮）：Electron 弹文件对话框，优先给相对 .cgui 的路径；浏览器模式只有文件名 */
  const pickImage = async () => {
    const res = await getBridge().pickImage();
    if (res.canceled) return;
    const p = res.rel || res.path;
    if (p) patch((n) => { n.src = p; });
  };
  const parent = findParent(doc, node.id);
  const inTabs = parent?.type === "tabs";
  /** 可以当绑定源的控件（别人指向我的 id 改了就一起改，见上面 id 那段） */
  const bindIds = collectBindIds(doc.widgets, node.id);
  /** 多行文本编辑：不进撤销栈，靠聚焦时压一次快照 */
  const patchSoft = (fn: (n: CgNode) => void) => {
    onApply((d) => {
      const t = findAny(d.widgets, node.id);
      if (t) fn(t);
    });
  };

  return (
    <div className="panel-body">
      <IdentitySection
        doc={doc}
        node={node}
        info={info}
        patch={patch}
        onBeginHistory={onBeginHistory}
        onApply={onApply}
        onSelect={onSelect}
      />
      <GeometrySection node={node} patch={patch} />
      <EditSection node={node} patch={patch} />
      <MediaSection node={node} patch={patch} onPickImage={pickImage} />
      <StyleSection
        doc={doc}
        node={node}
        patch={patch}
        fontOpen={fontOpen}
        onToggleFont={() => setFontOpen(!fontOpen)}
      />
      <StateSection node={node} info={info} patch={patch} />
      <TypeSection node={node} patch={patch} />
      <HintSection node={node} patch={patch} />
      <BindSection node={node} info={info} bindIds={bindIds} patch={patch} />
      <ActionSection doc={doc} node={node} patch={patch} />
      <ContentSection
        doc={doc}
        node={node}
        info={info}
        inTabs={inTabs}
        patch={patch}
        patchSoft={patchSoft}
        onBeginHistory={onBeginHistory}
      />
    </div>
  );
}
