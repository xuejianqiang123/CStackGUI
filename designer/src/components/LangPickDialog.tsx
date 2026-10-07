/* LangPickDialog.tsx -- 首次导出骨架时问一句：C 模板还是 C++ 模板
 *
 * 只在"工程目录里还没有骨架"时弹 —— 一旦 `<工程>_ui_events.c/.cpp` 落地，语言就定死了：
 * 同一个工程里并存两套源文件（main.c 与 main.cpp）是**重复定义**级别的坏结果（链接直接报错，
 * 而且用户的业务代码可能已经写在其中一份里），所以第二次导出连问都不问，按磁盘现状走。
 *
 * 样式沿用 NewProjectDialog 那套（.modal-* / .tpl-*），不新开一层视觉。
 */
import type { GenLang } from "../codegen";

type Props = {
  open: boolean;
  /** 工程名（写在提示里，让人知道这两个文件会叫什么） */
  baseName: string;
  onPick: (lang: GenLang) => void;
  onClose: () => void;
};

const CHOICES: { lang: GenLang; name: string; tag: string; desc: string; hint: string }[] = [
  {
    lang: "c",
    name: "C 模板",
    tag: "main.c",
    desc: "`<工程>_ui_events.c` + `main.c`",
    hint: "默认写法：教程、示例、绝大多数工程都是 C，拿不准就选它",
  },
  {
    lang: "cpp",
    name: "C++ 模板",
    tag: "main.cpp",
    desc: "`<工程>_ui_events.cpp` + `main.cpp`",
    hint: "MinGW-w64 自带 g++：构建脚本自动换 g++ -std=c++17，STL / 类随便用",
  },
];

export default function LangPickDialog({ open, baseName, onPick, onClose }: Props) {
  if (!open) return null;

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>导出骨架：用哪种语言？</strong>
          <span className="muted small">只问这一次</span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-body">
          <ul className="tpl-list">
            {CHOICES.map((c) => (
              <li key={c.lang}>
                <button className="tpl-item" onClick={() => onPick(c.lang)} title={c.hint}>
                  <span className="tpl-name">
                    {c.name}
                    <span className="tpl-tag">{c.tag}</span>
                  </span>
                  <span className="tpl-desc">{c.desc}</span>
                </button>
              </li>
            ))}
          </ul>
          <p className="muted small">
            会生成 <code>{baseName}_ui_events.&#123;c|cpp&#125;</code> 与对应的入口文件；
            界面（<code>{baseName}.cgui</code>）、<code>.rc</code>、<code>build.bat</code> 两种语言共用。
            <strong>已经导过的工程不再问</strong> —— 换语言得另建工程，不会去动你已经在写的代码。
          </p>
        </div>

        <div className="modal-foot">
          <p className="muted small">选完按 Ctrl+S 存完，`build-app.bat {baseName}` 就能编。</p>
          <button onClick={onClose}>取消</button>
        </div>
      </div>
    </div>
  );
}
