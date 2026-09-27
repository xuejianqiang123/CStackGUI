/* InfoDialog.tsx -- 「程序配置」对话框（工具栏「程序配置」按钮 / WindowPanel 里的入口）
 *
 * 三页：
 *   通常页（程序名称 / 描述 / 备注 / 版本号 / 创建号 / 作者 / 版权）、
 *   作者信息页（公司 / 地址 / 邮编 / 电话 / 传真 / 邮箱 / 主页）——
 *   这些字段写进 .cgui 的 app.info，运行时编译时落到 exe 的版本信息（<工程>.rc 里的
 *   VERSIONINFO），运行后也可以用 cg_app_info() 读回来。空字段不写（保持文件干净）。
 *   界面页：app.dark（浅色 / 深色 / 跟随系统）—— 契约 §3。
 *
 * 深色那一页要**照实**写清能力的边界（哪些系统控件改不动），否则用户会以为深色是半成品。
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

/** 「界面」页签的序号（前两页是 app.info 的字段页） */
const THEME_PAGE = PAGES.length;

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

  /* 界面主题（契约 §3）：浅色(false/不写) / 深色(true) / 跟随系统("auto")，三个都写进 .cgui 的 app.dark */
  const themeValue = doc.app.dark === true ? "dark" : doc.app.dark === "auto" ? "auto" : "light";
  const setTheme = (v: string) => {
    onBeginHistory();
    onApply((d) => {
      if (v === "dark") d.app.dark = true;
      else if (v === "auto") d.app.dark = "auto";
      else delete d.app.dark; /* 浅色 = 缺省，不写进文件（不改变现有工程外观） */
    });
  };

  const filled = INFO_KEYS.filter((k) => info[k]).length;

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>程序配置</strong>
          <span className="muted small">exe 版本信息 / 窗口界面主题</span>
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
          <button
            className={page === THEME_PAGE ? "modal-tab on" : "modal-tab"}
            onClick={() => setPage(THEME_PAGE)}
          >
            界面
          </button>
          {page === THEME_PAGE ? (
            <span className="modal-count muted small">
              {themeValue === "dark" ? "深色" : themeValue === "auto" ? "跟随系统" : "浅色"}
            </span>
          ) : (
            <span className="modal-count muted small">
              已填 {filled}/{INFO_KEYS.length}
            </span>
          )}
        </div>

        <div className="modal-body">
          {page === THEME_PAGE ? (
            <>
              <div className="insp-row">
                <span className="insp-label" title="写进 .cgui 的 app.dark；运行时也能用 cg_theme_set_dark() 运行时切换">
                  界面主题
                </span>
                <span className="insp-value">
                  <select className="input" value={themeValue} onChange={(e) => setTheme(e.target.value)}>
                    <option value="light">浅色（缺省）</option>
                    <option value="dark">深色</option>
                    <option value="auto">跟随系统</option>
                  </select>
                </span>
              </div>
              <p className="muted small">
                写进 <code>.cgui</code> 的 <code>app.dark</code>：<code>true</code> 深色 / <code>"auto"</code>{" "}
                跟随系统 / <code>false</code>（不写）浅色。缺省是浅色，所以老工程打开后外观完全不变。
              </p>

              <div className="hint-box">
                <div className="hint-title">深色覆盖哪些、不覆盖哪些</div>
                <p className="muted small">
                  深色<strong>只改缺省值</strong>：控件自己写了文字色 / 底色（内联或命名样式）就以它为准，
                  深色不会覆盖它 —— 所以深色下照样可以有彩色的按钮和绿字日志区。
                </p>
                <p className="muted small">
                  <strong>跟随深色</strong>：窗口与面板 / 卡片 / 分组框 / 容器块 / 外形框的背景色，
                  以及标签、按钮、图标按钮、菜单按钮、复选框、单选钮、开关、颜色选择、徽章、标签片、评分、分页、
                  输入框、富文本、终端、进度条、圆环进度、状态灯、仪表盘、图表、画板、图片、列表框、表格、树。
                </p>
                <p className="muted small">
                  <strong className="warn">不跟随（仍是系统浅色）</strong>：标签页、下拉框弹出的那个列表、月历、
                  日期时间选择、状态栏、工具栏、滑块、滚动条箭头。这些是 Windows 自带的经典控件，
                  主题由系统内部绘制、没有公开 API 能改它们 —— <strong>这不是缺陷</strong>：
                  想让它们也变深，只能把这些控件全部重画，那是另一个量级的工程。
                </p>
                <p className="muted small">
                  「跟随系统」读的是 Windows 的"应用主题"（<code>AppsUseLightTheme</code>）；用户在系统里改主题后，
                  运行时会跟着重画一次。另外两句先说清楚：
                  <strong>画布本身不做深色预览</strong>（画布是浅色的设计界面，这里只是把开关写进 .cgui）；
                  颜色细节以运行时为准 —— 没写文字色的自绘文字（复选框 / 单选钮 / 开关）在深色下会变浅，
                  而按钮这类画在有色填充上的文字保持与填充搭配的深色；控件自己写了文字色 / 底色的一律以它为准。
                </p>
              </div>
            </>
          ) : (
            PAGES[page].keys.map((key) => (
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
            ))
          )}
        </div>

        <div className="modal-foot">
          {page === THEME_PAGE ? (
            <p className="muted small">
              深色的覆盖范围由运行时决定（见 <code>runtime/src/skin</code> 与 <code>runtime/src/win32</code>）；
              画布只负责把这个开关写进 <code>.cgui</code>，不做预览。
            </p>
          ) : (
            <p className="muted small">
              这些会生成到导出工程的 <code>&lt;工程名&gt;.rc</code>，编译进 exe 的版本信息；
              运行时还能用 <code>cg_app_info(app, "字段名")</code> 读回来（如 <code>product</code> /{" "}
              <code>version</code> / <code>author</code>）。
            </p>
          )}
          <button className="primary" onClick={onClose}>
            完成
          </button>
        </div>
      </div>
    </div>
  );
}
