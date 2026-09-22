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

  /** 同名函数被哪些控件用着。注意「一个控件把自己的多个事件绑到同一个函数」是常规写法
   *  （examples\lesson01_edit_button 就是 13 个事件共用一个 on_full_event），不该提示；
   *  只有**跨控件**共用才值得提醒：C 侧只会存在一个函数，回调里得靠 cg_widget_id(self) 分辨是谁。 */
  const widgetsByName = new Map<string, Set<string>>();
  for (const u of collectCallbacks(doc)) {
    const set = widgetsByName.get(u.name) ?? new Set<string>();
    set.add(u.widgetId);
    widgetsByName.set(u.name, set);
  }
  /** 当前这行属于谁（主窗口在 collectCallbacks 里用的是伪 id `__window`） */
  const selfId = isWindow ? "__window" : node!.id;
  /** 这个函数名还被哪些**别的**控件绑着。返回的是控件 id，直接显示出来比「共用」两个字
   *  好懂得多；空数组 = 只有当前控件在用。 */
  const othersOf = (name: string): string[] =>
    name ? [...(widgetsByName.get(name) ?? [])].filter((id) => id !== selfId) : [];
  /** 给用户看的名字：窗口事件挂在伪控件 `__window` 上，别把内部 id 露出去 */
  const labelOf = (id: string) => (id === "__window" ? "主窗口" : id);

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
        <div className="muted small ev-note">
          <div>下面是「启动窗口（主窗口）」的事件，绑在 <code>app.events</code> 上。</div>
          <div>
            回调里 <code>self</code> 就是窗口本身，用 <code>cg_app_of(self)</code> 拿 <code>cg_app_t *</code>。
          </div>
          <div>和控件事件是同一套 <code>CG_BIND</code> 机制。</div>
        </div>
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

      {supported.length > 0 ? (
        <div className="muted small ev-legend">
          <div>填上函数名即完成绑定，下面是这一页上各种标记的意思：</div>
          {/* 两栏清单：左边是「你会看到的东西」，右边是说人话的解释，一眼一行 */}
          <dl className="ev-legend-list">
            <dt>自动</dt>
            <dd>按 <code>on_控件id_事件</code> 生成规范回调名</dd>
            <dt>×</dt>
            <dd>清空这一行的绑定</dd>
            <dt>行首圆点</dt>
            <dd>变粉 = 这一行已经绑上了函数</dd>
            <dt>还被 xxx 用着</dt>
            <dd>
              同一个函数名别的控件也在绑：C 侧只会存在一个函数，回调里用 <code>cg_widget_id(self)</code> 分辨是谁
            </dd>
          </dl>
          <div>
            名字填错不会编译报错，但运行时启动会警告、<code>--selftest</code> 会报错。
          </div>
        </div>
      ) : null}

      <div className="ev-list">
        {supported.map((ev) => {
          const value = curEvents[ev] ?? "";
          const others = othersOf(value);
          const suggested = `on_${(isWindow ? "window" : node!.id).replace(/[^0-9a-zA-Z_]/g, "_")}_${ev}`;
          return (
            <div key={ev} className={value ? "ev-row bound" : "ev-row"}>
              {/* 第一行：事件名 + 操作按钮（名字长、按钮也不能折行，索性各占一行的两侧）；
                  第二行：输入框独占整行 —— 回调名再长也完整可见，不会被按钮挤掉 */}
              <div className="ev-head">
                <span className="ev-name" title={EVENT_HINT[ev]}>
                  <span className="ev-dot" />
                  {EVENT_LABEL[ev]}
                </span>
                <span className="ev-tools">
                  {others.length > 0 ? (
                    <span
                      className="ev-others"
                      title={
                        `同一个函数名还被这些控件绑着：${others.map(labelOf).join("、")}。\n` +
                        "C 侧只会存在一个函数，回调里用 cg_widget_id(self) 分辨是谁。"
                      }
                    >
                      还被 {others.map(labelOf).join("、")} 用着
                    </span>
                  ) : null}
                  <button
                    className="mini"
                    title={`自动命名：按「控件 id + 事件名」生成规范回调名 ${suggested}`}
                    onClick={() => setEvent(ev, suggested)}
                  >
                    自动
                  </button>
                  {value ? (
                    <button className="mini" title="清除这一行的绑定（等于把输入框清空）" onClick={() => setEvent(ev, "")}>
                      ×
                    </button>
                  ) : null}
                </span>
              </div>
              <input
                className="input mono ev-input"
                value={value}
                placeholder={suggested}
                onChange={(e) => setEvent(ev, e.target.value)}
                spellCheck={false}
              />
            </div>
          );
        })}
      </div>

      {Object.keys(curEvents).length > 0 ? (
        <div className="hint-box">
          <div className="hint-title">已绑定</div>
          <ul className="hint-list">
            {supported.filter((e) => curEvents[e]).map((e) => {
              const name = curEvents[e] ?? "";
              const others = othersOf(name);
              return (
                <li key={e}>
                  <code>{EVENT_LABEL[e]}</code> → <code>{name}</code>
                  {others.length > 0 ? <span className="muted"> 同一函数还被 {others.map(labelOf).join("、")} 绑着</span> : null}
                </li>
              );
            })}
          </ul>
          <div className="hint-foot">
            点「导出 C 骨架」会在导出目录里生成同名空函数和绑定表；每个控件只导出它自己支持的事件，
            函数就在 <code>&lt;工程名&gt;_ui_events.c</code> 里，按名字搜即可。
          </div>
        </div>
      ) : (
        <p className="muted small">还没有绑定事件。填上函数名后导出 C 骨架，C 侧就有对应的空函数等你实现。</p>
      )}
    </div>
  );
}
