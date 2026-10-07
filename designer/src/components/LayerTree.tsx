/* LayerTree.tsx -- 层级：谁盖在谁上面、谁在谁里面
 *
 * .cgui 没有独立的 z 字段：数组顺序就是层叠顺序，后面的盖在前面的上面。
 * 所以「上移/下移」就是改数组顺序。
 */
import { KIND_MAP, type CgDoc, type CgNode } from "../model";

type Props = {
  doc: CgDoc;
  /** 选中集：层级面板也支持 Shift/Ctrl 加选，所以拿整个集合 */
  selectedIds: string[];
  /** 行点击：additive = Shift/Ctrl 加选 */
  onSelect: (id: string | null, additive?: boolean) => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  onRemove: (id: string) => void;
};

export default function LayerTree({ doc, selectedIds, onSelect, onBeginHistory, onApply, onRemove }: Props) {
  const reorder = (node: CgNode, dir: -1 | 1) => {
    onBeginHistory();
    onApply((d) => {
      const move = (list: CgNode[]): boolean => {
        const i = list.findIndex((n) => n.id === node.id);
        if (i >= 0) {
          const j = i + dir;
          if (j < 0 || j >= list.length) return true;
          const [item] = list.splice(i, 1);
          list.splice(j, 0, item);
          return true;
        }
        for (const n of list) {
          if (n.children?.length && move(n.children)) return true;
        }
        return false;
      };
      move(d.widgets);
    });
  };

  const render = (nodes: CgNode[], depth: number): React.ReactNode =>
    nodes.map((n, i) => (
      <div key={n.id}>
        <div
          className={`layer-row${selectedIds.includes(n.id) ? " selected" : ""}`}
          style={{ paddingLeft: 8 + depth * 14 }}
          onClick={(e) => onSelect(n.id, e.shiftKey || e.ctrlKey || e.metaKey)}
          title={`${n.type} #${n.id}`}
        >
          <span className={`glyph glyph-${n.type}`} />
          <span className="layer-id">{n.id}</span>
          <span className="muted small">{KIND_MAP[n.type]?.label ?? n.type}</span>
          {n.grow ? (
            <span className="muted small" title="容器变大时按权重吃多出来的空间">
              余量 {n.grow}
            </span>
          ) : null}
          <span className="layer-actions">
            <button
              className="mini"
              title="上移（盖住前面的）"
              disabled={i === nodes.length - 1}
              onClick={(e) => {
                e.stopPropagation();
                reorder(n, 1);
              }}
            >
              ↑
            </button>
            <button
              className="mini"
              title="下移"
              disabled={i === 0}
              onClick={(e) => {
                e.stopPropagation();
                reorder(n, -1);
              }}
            >
              ↓
            </button>
            <button
              className="mini danger"
              title="删除"
              onClick={(e) => {
                e.stopPropagation();
                onRemove(n.id);
              }}
            >
              ×
            </button>
          </span>
        </div>
        {n.children?.length ? render(n.children, depth + 1) : null}
      </div>
    ));

  return (
    <div className="panel layers">
      <div className="panel-head">
        层级
        <span className="muted small">（下面的盖在上面）</span>
      </div>
      <div className="layer-body">
        {doc.widgets.length === 0 ? <p className="muted small">画布还是空的，从左边拖一个控件进来。</p> : render(doc.widgets, 0)}
      </div>
    </div>
  );
}
