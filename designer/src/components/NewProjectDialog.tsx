/* NewProjectDialog.tsx -- 「新建」模板选择
 *
 * 「新建」原来直接给一块空画布，新手第一眼不知道从哪下手。这里摆三个能直接跑的起点：
 * 空白 / 表单 / 托盘小工具，每个都带好 id、中文文字和已经绑好的事件名。
 * 选了之后仍然是普通的 .cgui，随便改 —— 顺便让人明白"画布产物就是一份 JSON"。
 */
import type { TemplateKind } from "../model";

type Props = {
  open: boolean;
  onClose: () => void;
  onPick: (kind: TemplateKind) => void;
};

const TEMPLATES: { kind: TemplateKind; name: string; desc: string; hint: string }[] = [
  {
    kind: "blank",
    name: "空白",
    desc: "什么都不放，自己从零摆",
    hint: "想完全自己设计时选它（窗口 700×500，底色淡紫）",
  },
  {
    kind: "form",
    name: "表单",
    desc: "分组框 + 姓名/备注输入 + 确定/取消",
    hint: "最常见的「填个表提交」套路：容器怎么用、id 怎么起名、事件怎么先绑好",
  },
  {
    kind: "tray",
    name: "托盘小工具",
    desc: "日志列表 + 开始/停止/清空 + 状态栏",
    hint: "带托盘图标；点关闭先收进托盘不退出（app.events.close 拦一下）",
  },
];

export default function NewProjectDialog({ open, onClose, onPick }: Props) {
  if (!open) return null;

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>新建工程</strong>
          <span className="muted small">先挑一个起点，之后随便改</span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-body">
          <ul className="tpl-list">
            {TEMPLATES.map((t) => (
              <li key={t.kind}>
                <button className="tpl-item" onClick={() => onPick(t.kind)} title={t.hint}>
                  <span className="tpl-name">{t.name}</span>
                  <span className="tpl-desc">{t.desc}</span>
                </button>
              </li>
            ))}
          </ul>
          <p className="muted small">
            选完记得 <strong>Ctrl+S</strong> 存下来；之后点「导出 C 骨架」才会生成 C 文件。
          </p>
        </div>

        <div className="modal-foot">
          <p className="muted small">
            当前画布如果改了没保存，会先问一句再新建。
          </p>
          <button onClick={onClose}>取消</button>
        </div>
      </div>
    </div>
  );
}
