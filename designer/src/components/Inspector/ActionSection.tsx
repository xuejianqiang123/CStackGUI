/* Inspector/ActionSection.tsx -- 「动作」那一栏（控件引用动作 / 菜单项引用动作）
 *
 * 动作是顶层 actions 里的「一条命名命令 = 文字 + 快捷键 + 一个回调名」。
 * 这里只做**引用**这一半：
 *   - 控件：只有支持 click 的类型才给下拉（不支持 click 还写了动作，运行时 WARN、自检报错）；
 *   - 菜单项 / 菜单按钮：每一项一个下拉，并告诉用户这一项**最终显示的是哪段文字**
 *     （action.text 非空时覆盖菜单项自己的 text —— 不说清楚就成了"画布显示的和跑起来不一样"）。
 *
 * 名字改成不存在的动作时这里不自动改引用（v1 的约定）：会当场标出来 + 自检会报错，
 * 想删干净就在下拉里选「（无）」。
 */
import { eventsOf, flattenItems, type CgDoc, type CgNode, type CgTreeItem } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type ActionSectionProps = {
  doc: CgDoc;
  node: CgNode;
  patch: Patch;
};

/** 按路径写回某一项的 action（空串 = 取消引用） */
function writeItemAction(items: CgTreeItem[], path: number[], action: string): boolean {
  const [i, ...rest] = path;
  const item = items[i];
  if (!item) return false;
  if (!rest.length) {
    if (action) item.action = action;
    else item.action = undefined;
    return true;
  }
  return writeItemAction(item.children ?? [], rest, action);
}

export default function ActionSection({ doc, node, patch }: ActionSectionProps) {
  const actions = doc.actions ?? {};
  const actionNames = Object.keys(actions);
  const clickable = eventsOf(node.type).includes("click");
  const isMenu = node.type === "menu" || node.type === "menubutton";
  const menuRows = isMenu ? flattenItems(node.nodes).filter((r) => r.item.text !== "-") : [];
  /** 下拉里一项的写法：动作名 + 它的文字 + 快捷键（不写出来就不知道引用的到底是哪条命令） */
  const optionLabel = (name: string, text?: string, accel?: string) =>
    `${name}${text ? ` — ${text}` : ""}${accel ? `  [${accel}]` : ""}`;
  const own = node.action ? actions[node.action] : undefined;
  /* "同时绑了 click"**只在控件自己引用了动作时**才算错配置：运行时只检查 w->action
   * （见 runtime/src/core/cg_action.c 的 action_validate_refs()），菜单项各自引用动作
   * 并不影响菜单控件自己的 click 绑定。
   * 反过来说，"文件菜单四项走动作 + 编辑/帮助仍走菜单自己的 click"是完全正常的写法
   * （examples/notepad 就是这样）。这里误判会给出一个「清掉 click 绑定」的按钮 ——
   * 用户照做就把那几个菜单项一起弄废了，所以这条判据必须与运行时逐字对齐。 */
  const conflict = clickable && !!node.events?.click && !!node.action;
  const clearClick = () =>
    patch((n) => {
      const ev = { ...(n.events ?? {}) };
      delete ev.click;
      n.events = Object.keys(ev).length ? ev : undefined;
    });

  if (!isMenu && !clickable && !node.action) return null;

  return (
    <>
      <div className="insp-section">动作</div>

      {isMenu ? (
        menuRows.length ? (
          menuRows.map((row) => {
            const a = row.item.action ? actions[row.item.action] : undefined;
            const missing = !!row.item.action && !a;
            const shown = a?.text && a.text !== row.item.text ? a.text : "";
            return (
              <div className="act-item" key={row.path.join(".")}>
                <div className="act-item-head">
                  <span
                    className="act-item-text"
                    title={`${"  ".repeat(row.depth)}${row.item.text || "（无文字）"}`}
                  >
                    {row.depth ? <span className="muted small">{row.depth} 层 </span> : null}
                    {row.item.text || "（无文字）"}
                  </span>
                  <select
                    className="input"
                    value={row.item.action ?? ""}
                    onChange={(e) =>
                      patch((n) => {
                        writeItemAction(n.nodes ?? [], row.path, e.target.value);
                      })
                    }
                  >
                    <option value="">（无动作）</option>
                    {actionNames.map((name) => (
                      <option key={name} value={name}>
                        {optionLabel(name, actions[name].text, actions[name].accel)}
                      </option>
                    ))}
                  </select>
                </div>
                {missing ? (
                  <p className="act-note warn">
                    引用了不存在的动作「{row.item.action}」：动作改名不会自动更新引用，自检会报这一条。
                  </p>
                ) : null}
                {shown ? (
                  <p className="act-note muted small">
                    显示「{shown}」（动作的文字），不是它自己的「{row.item.text}」。
                  </p>
                ) : null}
              </div>
            );
          })
        ) : (
          <p className="muted small">这个菜单还没有菜单项。先在「内容 → 菜单项」里写几项，再回来挂动作。</p>
        )
      ) : null}

      {clickable ? (
        <Row
          label={isMenu ? "整个菜单" : "动作"}
          hint={
            isMenu
              ? "菜单栏本身也可以引用一个动作（点任意一项都会触发它）；一般用不这么写，逐项挂更清楚"
              : "点击这个控件时触发的命名命令（文字 / 快捷键 / 回调都在动作上）"
          }
        >
          <select
            className="input"
            value={node.action ?? ""}
            onChange={(e) =>
              patch((n) => {
                n.action = e.target.value || undefined;
              })
            }
          >
            <option value="">（无）</option>
            {actionNames.map((name) => (
              <option key={name} value={name}>
                {optionLabel(name, actions[name].text, actions[name].accel)}
              </option>
            ))}
          </select>
        </Row>
      ) : null}

      {!clickable && node.action ? (
        <div className="hint-box warn-box">
          <div className="hint-title">「{node.type}」不支持点击</div>
          <p className="muted small">
            它引用了动作「{node.action}」，但这类控件不支持 click：运行时不会触发，启动时会警告、
            <code>--selftest</code> 会报错。
          </p>
          <button
            className="mini"
            onClick={() =>
              patch((n) => {
                n.action = undefined;
              })
            }
          >
            清掉这个引用
          </button>
        </div>
      ) : null}

      {!isMenu && !!own?.text && !!node.text && own.text !== node.text ? (
        <p className="muted small">
          运行时显示的是动作的文字「{own.text}」，不是控件自己的「{node.text}」。
        </p>
      ) : null}

      {conflict ? (
        <div className="hint-box warn-box">
          <div className="hint-title">还绑着 click 事件</div>
          <p className="muted small">
            已经由动作「{node.action}」接管，同时绑 <code>click</code> →{" "}
            <code>{node.events?.click}</code> 会被自检判为错误（运行时忽略这一份）。
          </p>
          <button className="mini" onClick={clearClick}>
            清掉 click 绑定
          </button>
        </div>
      ) : null}

      {actionNames.length === 0 ? (
        <p className="muted small">
          还没有动作。工具条上的「动作」按钮里可以新建（一条动作 = 文字 + 快捷键 + 一个回调名）。
        </p>
      ) : null}
    </>
  );
}
