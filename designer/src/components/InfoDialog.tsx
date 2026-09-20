/* InfoDialog.tsx -- 「程序配置」对话框（工具栏「程序配置」按钮 / WindowPanel 里的入口）
 *
 * 对齐「程序配置」对话框的两页：通常页（程序名称 / 描述 / 备注 / 版本号 / 创建号 /
 * 作者 / 版权）和作者信息页（公司 / 地址 / 邮编 / 电话 / 传真 / 邮箱 / 主页）。
 * 这些字段写进 .cgui 的 app.info，运行时编译时落到 exe 的版本信息（<工程>.rc 里的
 * VERSIONINFO），运行后也可以用 cg_app_info() 读回来。空字段不写（保持文件干净）。
 */
import { useState } from "react";
import { INFO_KEYS, type CgDoc, type CgInfo } from "../model";

type Props = {
  open: boolean;
  doc: CgDoc;
  onClose: () => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

/* 两页里各放哪些字段（顺序 = 对话框里的顺序） */
const PAGES: { title: string; keys: (keyof CgInfo)[] }[] = [
  {
    title: "通常",
    keys: ["product", "desc", "comments", "version", "build", "author", "copyright"],
  },
  {
    title: "作者信息",
    keys: ["company", "address", "postcode", "phone", "fax", "email", "homepage"],
  },
];

const LABELS: Record<keyof CgInfo, string> = {
  product: "程序名称",
  desc: "程序描述",
  comments: "程序备注",
  version: "程序版本号",
  build: "创建号",
  author: "作者",
  company: "单位 / 公司",
  copyright: "版权声明",
  address: "地址",
  postcode: "邮政编码",
  phone: "电话",
  fax: "传真",
  email: "电子邮箱",
  homepage: "主页",
};

const HINTS: Partial<Record<keyof CgInfo, string>> = {
  product: "写进 exe 的 ProductName",
  version: "四段式 1.0.0.0；和创建号拼成版本号",
  build: "那个 0.0，仅供对照",
  copyright: "写进 exe 的 LegalCopyright（建议带上 © 年份）",
  company: "写进 exe 的 CompanyName",
  author: "单独一个 Author 字段，运行时 cg_app_info 能取到",
  email: "建议填能联系到你的邮箱",
  homepage: "官网 / 项目主页地址",
};

export default function InfoDialog({ open, doc, onClose, onBeginHistory, onApply }: Props) {
  const [page, setPage] = useState(0);
  if (!open) return null;

  const info = doc.app.info ?? {};

  /** 改一个程序配置字段；空串 = 删掉该字段（回到默认，不写进文件/版本信息） */
  const set = (key: keyof CgInfo, value: string) => {
    onBeginHistory();
    onApply((d) => {
      const next = { ...(d.app.info ?? {}) };
      if (value.trim()) next[key] = value.trim();
      else delete next[key];
      d.app.info = Object.keys(next).length ? next : undefined;
    });
  };

  const filled = INFO_KEYS.filter((k) => info[k]).length;

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>程序配置</strong>
          <span className="muted small">写进编译后的 exe 版本信息</span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-tabs">
          {PAGES.map((p, i) => (
            <button
              key={p.title}
              className={i === page ? "modal-tab on" : "modal-tab"}
              onClick={() => setPage(i)}
            >
              {p.title}
            </button>
          ))}
          <span className="modal-count muted small">已填 {filled}/{INFO_KEYS.length}</span>
        </div>

        <div className="modal-body">
          {PAGES[page].keys.map((key) => (
            <div className="insp-row" key={key}>
              <span className="insp-label" title={HINTS[key]}>
                {LABELS[key]}
              </span>
              <span className="insp-value">
                <input
                  className="input"
                  value={info[key] ?? ""}
                  placeholder={HINTS[key] ? `（${HINTS[key]}）` : ""}
                  onChange={(e) => set(key, e.target.value)}
                />
              </span>
            </div>
          ))}
        </div>

        <div className="modal-foot">
          <p className="muted small">
            这些会生成到导出工程的 <code>&lt;工程名&gt;.rc</code>，编译进 exe 的版本信息；
            运行时还能用 <code>cg_app_info(app, "字段名")</code> 读回来（如 <code>product</code> / <code>version</code> / <code>author</code>）。
          </p>
          <button className="primary" onClick={onClose}>
            完成
          </button>
        </div>
      </div>
    </div>
  );
}
