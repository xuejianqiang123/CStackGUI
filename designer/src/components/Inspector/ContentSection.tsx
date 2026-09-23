/* Inspector/ContentSection.tsx -- 「内容」整段
 *
 * 标签页归属、当前页/初始选中、图片路径、条目（items）、菜单项/树节点（nodes）、
 * 富文本内容（text）、表格列与行（columns/rows），以及容器子控件提示与窗口尺寸脚注。
 * 多行文本编辑器一律走 patchSoft（不进撤销栈，靠聚焦时压一次快照）。
 */
import {
  itemsToText,
  menuToText,
  tableColumnsToText,
  tableRowsToText,
  textToItems,
  textToMenu,
  textToTableColumns,
  textToTableRows,
  textToTree,
  treeToText,
  type CgDoc,
  type CgNode,
  type KindInfo,
} from "../../model";
import { Area, Row, type Patch } from "./PanelUi";

export type ContentSectionProps = {
  doc: CgDoc;
  node: CgNode;
  info: KindInfo | undefined;
  /** 父控件是 tabs（决定「所在页」那一行） */
  inTabs: boolean;
  patch: Patch;
  patchSoft: Patch;
  onBeginHistory: () => void;
};

export default function ContentSection({
  doc,
  node,
  info,
  inTabs,
  patch,
  patchSoft,
  onBeginHistory,
}: ContentSectionProps) {
  return (
    <>
      <div className="insp-section">内容</div>
      {inTabs ? (
        <Row label="所在页">
          <input
            className="input narrow"
            type="number"
            min={0}
            value={node.page ?? 0}
            onChange={(e) =>
              patch((n) => {
                const v = Number(e.target.value) || 0;
                n.page = v > 0 ? v : undefined;
              })
            }
          />
          <span className="muted small">第几页（0 起）</span>
        </Row>
      ) : null}

      {node.type === "tabs" || node.type === "listbox" || node.type === "combo" ? (
        <Row
          label={node.type === "tabs" ? "当前页" : "初始选中"}
          hint={node.type === "tabs" ? undefined : "运行时启动后默认选中第几项（0 起）；选项来自下面的「条目」"}
        >
          <select
            className="input"
            value={node.value ?? 0}
            onChange={(e) =>
              patch((n) => {
                n.value = Number(e.target.value);
              })
            }
          >
            {(node.items ?? []).map((t, i) => (
              <option key={i} value={i}>
                {i + 1}. {t || (node.type === "tabs" ? `页面 ${i + 1}` : `第 ${i + 1} 项`)}
              </option>
            ))}
          </select>
        </Row>
      ) : null}

      {node.type === "image" ? (
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
        </Row>
      ) : null}
      {node.type === "image" ? (
        <p className="muted small">
          路径相对 .cgui 所在目录；支持 png / jpg / bmp / gif。画布上只显示文件名，运行时才画真图。
        </p>
      ) : null}

      {info?.listLike ? (
        <Area
          label={
            node.type === "statusbar" ? "分段文字"
              : node.type === "tabs" ? "页签文字"
              : node.type === "toolbar" ? "按钮文字"
              : node.type === "chart" ? "系列名"
              : "条目"
          }
          hint="每行一条"
          value={itemsToText(node.items)}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.items = textToItems(v); })}
        />
      ) : null}

      {node.type === "menu" || node.type === "menubutton" ? (
        <>
          <Area
            label="菜单项"
            hint="两空格一层 · 文本 | id · - 是分隔线 · 末尾 | v 表示勾选"
            rows={8}
            value={menuToText(node.nodes)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.nodes = textToMenu(v); })}
          />
          <p className="muted small">
            {node.type === "menu"
              ? "菜单栏没有矩形：运行时挂在窗口上（一根窗口只能有一根）。点击某一项时给这个控件派发 click，用 cg_menu_last_id() 取回 id。"
              : "点按钮会在下方弹出这个菜单；选中项同样回到控件的 click 回调。"}
          </p>
        </>
      ) : null}

      {node.type === "datepicker" ? (
        <Row label="格式">
          <select
            className="input"
            value={node.format ?? "date"}
            onChange={(e) => patch((n) => { n.format = e.target.value; })}
          >
            <option value="date">日期 2026-09-19</option>
            <option value="time">时间 14:30</option>
            <option value="datetime">日期 + 时间（两个控件拼的）</option>
          </select>
        </Row>
      ) : null}

      {node.type === "richtext" ? (
        <Area
          label="内容"
          hint="富文本可以放多行"
          rows={4}
          value={node.text ?? ""}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.text = v; })}
        />
      ) : null}


      {node.type === "tree" ? (
        <Area
          label="树节点"
          hint="两个空格缩进一层"
          rows={6}
          value={treeToText(node.nodes)}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.nodes = textToTree(v); })}
        />
      ) : null}

      {node.type === "table" ? (
        <>
          <Area
            label="列"
            hint="逗号分隔，可写 标题:宽度"
            rows={2}
            value={tableColumnsToText(node.columns)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.columns = textToTableColumns(v); })}
          />
          <Area
            label="行"
            hint="每行一条，单元格用逗号分隔"
            rows={5}
            value={tableRowsToText(node.rows)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.rows = textToTableRows(v); })}
          />
        </>
      ) : null}

      {node.children?.length ? (
        <p className="muted small">
          {node.type === "tabs" ? "标签页" : "这是容器"}，里面还有 {node.children.length} 个子控件
          （坐标相对本控件）。
        </p>
      ) : null}
      <p className="muted small">
        窗口：{doc.app.width}×{doc.app.height} · 缩放以画布顶部工具条为准
      </p>
    </>
  );
}
