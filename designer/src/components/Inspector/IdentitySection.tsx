/* Inspector/IdentitySection.tsx -- 身份与显示文字
 *
 * 面板最上面那一段：控件图标 + 类型名（只读）、id（可改，含查重与改绑）、
 * 以及「文字 / 单位」一行。
 * 状态不在本文件：doc / onApply / onBeginHistory / onSelect 都由 Inspector.tsx 传进来。
 */
import { isNumEdit, type CgDoc, type CgNode, type KindInfo } from "../../model";
import { Row, type Patch } from "./PanelUi";

/** 内容来自 items、运行时不读 w->text 的类型（白名单见 cg_win_sync.c:520-524）：
 *  填了「文字」也不会出现，所以干脆不显示这一行。 */
const ITEMS_DRIVEN = new Set(["listbox", "combo", "tabs", "toolbar", "statusbar", "tree", "table"]);

export type IdentitySectionProps = {
  doc: CgDoc;
  node: CgNode;
  info: KindInfo | undefined;
  patch: Patch;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  onSelect: (id: string | null) => void;
};

export default function IdentitySection({
  doc,
  node,
  info,
  patch,
  onBeginHistory,
  onApply,
  onSelect,
}: IdentitySectionProps) {
  return (
    <>
      <div className="node-head">
        <span className={`glyph glyph-${node.type}`} />
        <strong>{info?.label ?? node.type}</strong>
        <span className="muted small">{node.type}</span>
      </div>

      <Row label="id">
        <input
          className="input mono"
          value={node.id}
          spellCheck={false}
          onChange={(e) => {
            const raw = e.target.value.replace(/[^0-9a-zA-Z_]/g, "_");
            if (!raw) return; /* 清空输入的过程不改 id，空 id 会让运行时查找出问题 */
            /* 查重：id 是 cg_find 的凭据，重名会让「找控件」命中错的那个。
             * 冲突时自动加后缀，输入过程依然顺畅。 */
            const taken = new Set<string>();
            const collect = (ns: CgNode[]) =>
              ns.forEach((n) => {
                taken.add(n.id);
                if (n.children) collect(n.children);
              });
            collect(doc.widgets);
            taken.delete(node.id);
            let unique = raw;
            let k = 2;
            while (taken.has(unique)) unique = `${raw}_${k++}`;
            const oldId = node.id;
            onBeginHistory();
            onApply((d) => {
              /* 别人指向这个 id 的绑定一起改掉，否则运行时找不到源、绑定静默失效 */
              if (unique !== oldId) {
                const fix = (ns: CgNode[]) =>
                  ns.forEach((n) => {
                    if (n.bind?.src === oldId) n.bind = { ...n.bind, src: unique };
                    if (n.children) fix(n.children);
                  });
                fix(d.widgets);
              }
              const t = findAny(d.widgets, oldId);
              if (t) t.id = unique;
            });
            onSelect(unique);
          }}
        />
      </Row>
      {/* 同样是 text 字段：仪表盘上是单位（画在中心数值下面），灯上是灯右边的说明文字。
          数值框（edit + 调节器）没有这一行 —— 它的显示文本由「值」派生，写了也不生效。
          items 驱动的类型（列表框/下拉框/标签页/工具栏/状态栏/树/表格）也没有这一行 ——
          运行时不读它们的 w->text（cg_win_sync.c:520-524 白名单不含），填了也不会出现。 */}
      {isNumEdit(node) || ITEMS_DRIVEN.has(node.type) ? null : (
        <Row label={node.type === "meter" ? "单位" : "文字"}>
          <input
            className="input"
            value={node.text ?? ""}
            placeholder={node.type === "meter" ? "°C" : undefined}
            onChange={(e) =>
              patch((n) => {
                n.text = e.target.value;
              })
            }
          />
        </Row>
      )}
    </>
  );
}

/** 按 id 深度优先找节点（改 id 时要在**旧的**文档里找回目标节点） */
function findAny(nodes: CgNode[], id: string): CgNode | null {
  for (const n of nodes) {
    if (n.id === id) return n;
    const hit = n.children ? findAny(n.children, id) : null;
    if (hit) return hit;
  }
  return null;
}
