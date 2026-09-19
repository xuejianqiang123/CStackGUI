/* EventPanel.tsx -- 事件绑定：把控件事件和 C 函数名对上
 *
 * 这里填的名字会被写进 .cgui 的 events 对象，运行时启动时按名字在
 * CG_BIND 表里查函数。填错名字不会编译报错，但运行时会打警告 —— 所以
 * 面板上顺手给出「自动命名」和「C 侧是否存在」的提示。
 *
 * 只列出**这个控件真正支持**的事件（eventsOf）：标签没有交互、进度条只能被程序写、
 * 只有树才有展开折叠。C 侧的类型表也会校验，两边是同一份约定。
 */
import { CG_EVENTS, EVENT_HINT, EVENT_LABEL, eventsOf, type CgDoc, type CgEvent, type CgNode } from "../model";
import { collectCallbacks } from "../codegen";

type Props = {
  doc: CgDoc;
  node: CgNode | null;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

export default function EventPanel({ doc, node, onBeginHistory, onApply }: Props) {
  if (!node) {
    return (
      <div className="panel-body">
        <p className="muted">先在画布上选中一个控件，再给它绑事件。</p>
      </div>
    );
  }

  const setEvent = (ev: CgEvent, value: string) => {
    onBeginHistory();
    onApply((d) => {
      const walkFind = (nodes: CgNode[]): CgNode | null => {
        for (const n of nodes) {
          if (n.id === node.id) return n;
          const hit = n.children ? walkFind(n.children) : null;
          if (hit) return hit;
        }
        return null;
      };
      const target = walkFind(d.widgets);
      if (!target) return;
      const events = { ...(target.events ?? {}) };
      const trimmed = value.trim();
      if (trimmed) events[ev] = trimmed;
      else delete events[ev];
      target.events = Object.keys(events).length ? events : undefined;
    });
  };

  /** 同名函数被几个控件/事件共用：共用是合法的，但值得提示一下 */
  const useCount = new Map<string, number>();
  for (const u of collectCallbacks(doc)) useCount.set(u.name, (useCount.get(u.name) ?? 0) + 1);

  const supported = eventsOf(node.type);
  /* 手改 .cgui 可能绑上该控件不支持的事件：这里挑出来明说，别让它默默不生效 */
  const unsupported = CG_EVENTS.filter((e) => !supported.includes(e) && node.events?.[e]);

  return (
    <div className="panel-body">
      <div className="node-head">
        <span className={`glyph glyph-${node.type}`} />
        <strong>{node.id}</strong>
        <span className="muted">({node.type})</span>
      </div>

      {unsupported.length > 0 ? (
        <div className="hint-box warn-box">
          <div className="hint-title">有事件这个控件不支持</div>
          <ul className="hint-list">
            {unsupported.map((e) => (
              <li key={e}>
                <code>{e}</code> → <code>{node.events?.[e]}</code>
                <span className="muted small"> 运行时不会触发，请删掉或换个控件</span>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {supported.length === 0 ? (
        <p className="muted">
          这个控件没有事件可绑：它只用来显示或被程序写入。
        </p>
      ) : null}

      <table className="ev-table">
        <tbody>
          {supported.map((ev) => {
            const value = node.events?.[ev] ?? "";
            const dup = value !== "" && (useCount.get(value) ?? 0) > 1;
            return (
              <tr key={ev} className={value ? "bound" : ""}>
                <td className="ev-name" title={EVENT_HINT[ev]}>
                  <span className="ev-dot" />
                  {EVENT_LABEL[ev]}
                </td>
                <td>
                  <input
                    className="input mono"
                    value={value}
                    placeholder={`on_${node.id}_${ev}`}
                    onChange={(e) => setEvent(ev, e.target.value)}
                    spellCheck={false}
                  />
                </td>
                <td className="ev-shared">
                  {dup ? (
                    <span className="warn small" title="这个函数名同时被别的控件绑定了，C 侧只会有一个函数">
                      共用
                    </span>
                  ) : null}
                </td>
                <td className="ev-actions">
                  <button
                    className="mini"
                    title="用 id + 事件名生成一个规范的回调名"
                    onClick={() => setEvent(ev, `on_${node.id.replace(/[^0-9a-zA-Z_]/g, "_")}_${ev}`)}
                  >
                    自动
                  </button>
                  {value ? (
                    <button className="mini" title="清除" onClick={() => setEvent(ev, "")}>
                      ×
                    </button>
                  ) : null}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>

      {node.events && Object.keys(node.events).length > 0 ? (
        <div className="hint-box">
          <div className="hint-title">这个控件已绑定</div>
          <ul className="hint-list">
            {supported.filter((e) => node.events?.[e]).map((e) => {
              const name = node.events?.[e] ?? "";
              const shared = (useCount.get(name) ?? 0) > 1;
              return (
                <li key={e}>
                  <code>{EVENT_LABEL[e]}</code> → <code>{name}</code>
                  {shared ? <span className="warn"> 同名函数被多个控件共用</span> : null}
                </li>
              );
            })}
          </ul>
          <div className="hint-foot">
            点「导出 C 骨架」会在导出目录里生成同名空函数和绑定表；
            每个控件只导出它自己支持的事件。
          </div>
        </div>
      ) : (
        <p className="muted small">还没有绑定事件。填上函数名后导出 C 骨架，C 侧就有对应的空函数等你实现。</p>
      )}
    </div>
  );
}
