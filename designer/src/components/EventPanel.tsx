/* EventPanel.tsx -- 事件绑定：把控件事件和 C 函数名对上
 *
 * 这里填的名字会被写进 .cgui 的 events 对象，运行时启动时按名字在
 * CG_BIND 表里查函数。填错名字不会编译报错，但运行时会打警告 —— 所以
 * 面板上顺手给出「自动命名」和「C 侧是否存在」的提示。
 *
 * 只列出**这个控件真正支持**的事件（eventsOf）：标签没有交互、进度条只能被程序写、
 * 只有树才有展开折叠。C 侧的类型表也会校验，两边是同一份约定。
 *
 * 没选中控件时（node 为 null），这里显示「主窗口（window）」的事件，
 * 它们写在 .cgui 的 app.events 里，和「启动窗口」那张事件表对齐。
 */
import { CG_EVENTS, EVENT_HINT, EVENT_LABEL, eventsOf, WINDOW_EVENTS, type CgDoc, type CgEvent, type CgNode } from "../model";
import { collectCallbacks } from "../codegen";

type Props = {
  doc: CgDoc;
  node: CgNode | null;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

export default function EventPanel({ doc, node, onBeginHistory, onApply }: Props) {
  const isWindow = !node;
  const supported = isWindow ? WINDOW_EVENTS : eventsOf(node!.type);
  const curEvents: Partial<Record<CgEvent, string>> = isWindow
    ? doc.app.events ?? {}
    : node!.events ?? {};

  const setEvent = (ev: CgEvent, value: string) => {
    onBeginHistory();
    onApply((d) => {
      if (isWindow) {
        const cur = { ...(d.app.events ?? {}) };
        const trimmed = value.trim();
        if (trimmed) cur[ev] = trimmed;
        else delete cur[ev];
        d.app.events = Object.keys(cur).length ? cur : undefined;
        return;
      }
      const walkFind = (nodes: CgNode[]): CgNode | null => {
        for (const n of nodes) {
          if (n.id === node!.id) return n;
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

  const unsupported = isWindow
    ? []
    : CG_EVENTS.filter((e) => !supported.includes(e) && node!.events?.[e]);

  return (
    <div className="panel-body">
      {isWindow ? (
        <div className="node-head">
          <span className="glyph glyph-window" />
          <strong>主窗口</strong>
          <span className="muted small">window</span>
        </div>
      ) : (
        <div className="node-head">
          <span className={`glyph glyph-${node!.type}`} />
          <strong>{node!.id}</strong>
          <span className="muted">({node!.type})</span>
        </div>
      )}

      {isWindow ? (
        <p className="muted small">
          这些是「启动窗口（主窗口）」的事件，绑在 <code>app.events</code> 上。回调里 <code>self</code> 就是窗口本身，
          用 <code>cg_app_of(self)</code> 拿 <code>cg_app_t *</code>；和控件事件同一套 <code>CG_BIND</code> 机制。
        </p>
      ) : null}

      {!isWindow && unsupported.length > 0 ? (
        <div className="hint-box warn-box">
          <div className="hint-title">有事件这个控件不支持</div>
          <ul className="hint-list">
            {unsupported.map((e) => (
              <li key={e}>
                <code>{e}</code> → <code>{node!.events?.[e]}</code>
                <span className="muted small"> 运行时不会触发，请删掉或换个控件</span>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {supported.length === 0 ? (
        <p className="muted">
          {isWindow
            ? "主窗口没有可绑的事件。"
            : "这个控件没有事件可绑：它只用来显示或被程序写入。"}
        </p>
      ) : null}

      <table className="ev-table">
        <tbody>
          {supported.map((ev) => {
            const value = curEvents[ev] ?? "";
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
                    placeholder={`on_${isWindow ? "window" : node!.id}_${ev}`}
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
                    title="用 窗口/控件 + 事件名 生成规范的回调名"
                    onClick={() => setEvent(ev, `on_${isWindow ? "window" : node!.id.replace(/[^0-9a-zA-Z_]/g, "_")}_${ev}`)}
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

      {Object.keys(curEvents).length > 0 ? (
        <div className="hint-box">
          <div className="hint-title">已绑定</div>
          <ul className="hint-list">
            {supported.filter((e) => curEvents[e]).map((e) => {
              const name = curEvents[e] ?? "";
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
            点「导出 C 骨架」会在导出目录里生成同名空函数和绑定表；每个控件只导出它自己支持的事件。
          </div>
        </div>
      ) : (
        <p className="muted small">还没有绑定事件。填上函数名后导出 C 骨架，C 侧就有对应的空函数等你实现。</p>
      )}
    </div>
  );
}
