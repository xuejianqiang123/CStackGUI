/* TableImportDialog.tsx -- 「导入点表」对话框（工具条「导入点表」按钮）
 *
 * 工控上位机的真实重复劳动在点表搬运：几百个信号名一个个拖控件、改文字。
 * 这里粘一段点表（每行 `文字,类型[,值]`）一次生成一网格控件 —— 生成走命令层
 * （model/tableimport.ts → ops.ts 的批量 insert）：**一步撤销**、命令进 journal。
 *
 * 契约与解析全在 model/tableimport.ts（纯函数、headless 测试钉死）；对话框只做三件事：
 *   1. 粘贴框 + live 预览（将导入 N 个 / 坏行 M 条逐行原因——导入前就看得见）；
 *   2. 坏行**不阻塞**导入（好行照进，这正是契约"逐行报错不停导入"的界面表达）；
 *   3. 确认 = 一次性把合法行交给 App（构造命令 + dispatch 在那边，对话框不碰 doc）。
 * 外壳复用 modal-backdrop / modal-card（与「动作」对话框同一套骨架与观感）。
 */
import { useMemo, useState } from "react";
import { parseTableCsv, type ImportRow } from "../model";

type Props = {
  open: boolean;
  onClose: () => void;
  onImport: (rows: ImportRow[]) => void;
};

/** 粘贴框的占位说明：契约就在用户眼皮底下，不用翻文档 */
const PLACEHOLDER = [
  "每行一条：文字,类型[,值]",
  "例：",
  "  启动,button",
  "  温度,progress,60",
  "  停止,button",
  "",
  "# 开头是注释；值只对进度条/滑块/数值框等有效",
].join("\n");

export default function TableImportDialog({ open, onClose, onImport }: Props) {
  const [text, setText] = useState("");
  const parsed = useMemo(() => parseTableCsv(text), [text]);

  if (!open) return null;

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>导入点表</strong>
          <span className="muted small">
            {parsed.rows.length > 0 ? `将导入 ${parsed.rows.length} 个控件` : "粘贴点表，一次生成一网格控件"}
          </span>
        </div>

        <textarea
          className="input"
          style={{ width: "100%", minHeight: 220, fontFamily: "Consolas, monospace", resize: "vertical" }}
          spellCheck={false}
          placeholder={PLACEHOLDER}
          value={text}
          onChange={(e) => setText(e.target.value)}
        />

        {parsed.issues.length ? (
          /* 坏行逐行列出（行号 + 原因）：契约"逐行报错、不停导入"——好行照进，
             这些行被跳过，导入前先看明白。 */
          <div className="act-note warn" style={{ maxHeight: 120, overflow: "auto" }}>
            {parsed.issues.slice(0, 20).map((is) => (
              <div key={is.line}>
                第 {is.line} 行：{is.reason}
              </div>
            ))}
            {parsed.issues.length > 20 ? <div>……共 {parsed.issues.length} 条问题行</div> : null}
          </div>
        ) : null}

        <div className="modal-foot">
          <p className="muted small">坏行会被跳过、好行照进；导入是一步可撤销的。</p>
          <button onClick={onClose}>取消</button>
          <button
            className="primary"
            disabled={parsed.rows.length === 0}
            onClick={() => onImport(parsed.rows)}
            title={parsed.rows.length === 0 ? "还没有可导入的合法行" : "把上面的合法行生成控件（一步可撤销）"}
          >
            导入 {parsed.rows.length > 0 ? `${parsed.rows.length} 个` : ""}
          </button>
        </div>
      </div>
    </div>
  );
}
