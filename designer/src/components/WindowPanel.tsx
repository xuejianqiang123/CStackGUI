/* WindowPanel.tsx -- 主窗口属性（画布没选中控件时，属性面板显示的就是这些）
 *
 * 对齐「属性」面板里"启动窗口"那一栏：位置尺寸、边框、按钮、图标、鼠标指针、
 * 回车/Esc 行为、任务栏、总在最前、托盘……每一项都写进 .cgui 的 app 节点，
 * 由运行时读取（runtime/src/win32/cg_win_window.c）。缺省不写 = 保持 Windows 默认观感。
 *
 * 「程序配置」（版本信息 / 作者 / 图标）在工具栏的对话框里，这里只给个入口和摘要。
 */
import { BORDER_LABEL, CURSORS, type CgApp, type CgBorder, type CgDoc } from "../model";

type Props = {
  doc: CgDoc;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  onOpenInfo: () => void;
  /** 打开系统文件选择框挑一个 .ico，返回写进 app.icon 的路径（取消返回 null） */
  onPickIcon: () => Promise<string | null>;
};

function Row({
  label,
  hint,
  right,
  children,
}: {
  label: string;
  hint?: string;
  /** 勾选框之类的短控件靠右对齐（标签名长时不挤在一起） */
  right?: boolean;
  children: React.ReactNode;
}) {
  return (
    <div className="insp-row">
      <span className="insp-label" title={hint}>
        {label}
      </span>
      <span className={right ? "insp-value right" : "insp-value"}>{children}</span>
    </div>
  );
}

function Flag({
  label,
  hint,
  checked,
  onChange,
}: {
  label: string;
  hint?: string;
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <Row label={label} hint={hint} right>
      <label className="check-inline">
        <input type="checkbox" checked={checked} onChange={(e) => onChange(e.target.checked)} />
      </label>
    </Row>
  );
}

export default function WindowPanel({ doc, onBeginHistory, onApply, onOpenInfo, onPickIcon }: Props) {
  const app = doc.app;

  /** 改一个窗口属性；undefined / 空串 = 删掉该字段，回到 C 侧默认值。
   *  （title/width/height/bg 是必填项，别拿 undefined 调这里。） */
  const set = <K extends keyof CgApp>(key: K, value: CgApp[K] | undefined) => {
    onBeginHistory();
    onApply((d) => {
      const bag = d.app as Record<string, unknown>;
      if (value === undefined || value === "") delete bag[key as string];
      else bag[key as string] = value;
    });
  };

  const info = doc.app.info ?? {};
  const configured = Object.keys(info).length;

  return (
    <div className="panel-body win-panel">
      <div className="node-head">
        <strong>主窗口</strong>
        <span className="muted small">window</span>
      </div>
      <p className="muted small">
        这里改的是窗口自身（启动窗口），不是某个控件。属性写进 .cgui 的 <code>app</code> 节点。
      </p>

      <div className="insp-section">基本</div>
      <Row label="标题">
        <input
          className="input"
          value={app.title}
          onFocus={onBeginHistory}
          onChange={(e) => onApply((d) => { d.app.title = e.target.value; })}
        />
      </Row>
      <Row label="备注" hint="只记在 .cgui 里方便自己看，不影响运行">
        <input
          className="input"
          value={app.note ?? ""}
          placeholder="这个窗口是干什么的"
          onFocus={onBeginHistory}
          onChange={(e) => set("note", e.target.value || undefined)}
        />
      </Row>

      <div className="insp-section">位置与尺寸</div>
      <Row label="左边 / 顶边" hint="不填交给 Windows 决定位置">
        <span className="pair">
          <input
            className="input narrow"
            type="number"
            value={app.x ?? ""}
            placeholder="自动"
            onFocus={onBeginHistory}
            onChange={(e) => set("x", e.target.value === "" ? undefined : Number(e.target.value))}
          />
          <input
            className="input narrow"
            type="number"
            value={app.y ?? ""}
            placeholder="自动"
            onFocus={onBeginHistory}
            onChange={(e) => set("y", e.target.value === "" ? undefined : Number(e.target.value))}
          />
        </span>
      </Row>
      <Flag
        label="位置居中"
        hint="按当前显示器工作区居中，盖过左边/顶边"
        checked={!!app.center}
        onChange={(v) => set("center", v || undefined)}
      />
      <Row label="宽度 × 高度" hint="客户区尺寸（不含标题栏边框）">
        <span className="pair">
          <input
            className="input narrow"
            type="number"
            value={app.width}
            onFocus={onBeginHistory}
            onChange={(e) => onApply((d) => { d.app.width = Number(e.target.value) || 100; })}
          />
          <input
            className="input narrow"
            type="number"
            value={app.height}
            onFocus={onBeginHistory}
            onChange={(e) => onApply((d) => { d.app.height = Number(e.target.value) || 100; })}
          />
        </span>
      </Row>

      <div className="insp-section">外观</div>
      <Row label="边框" hint="决定能不能拉伸、有没有标题栏">
        <select
          className="input"
          value={app.border ?? "sizable"}
          onChange={(e) => {
            const v = e.target.value as CgBorder;
            set("border", v === "sizable" ? undefined : v);
          }}
        >
          {(Object.keys(BORDER_LABEL) as CgBorder[]).map((k) => (
            <option key={k} value={k}>
              {BORDER_LABEL[k]}
            </option>
          ))}
        </select>
      </Row>
      <Row label="鼠标指针">
        <select
          className="input"
          value={app.cursor ?? ""}
          onChange={(e) => set("cursor", e.target.value || undefined)}
        >
          {CURSORS.map((c) => (
            <option key={c || "default"} value={c}>
              {c === "" ? "默认" : c}
            </option>
          ))}
        </select>
      </Row>
      <Row label="窗口图标" hint=".ico 文件；相对路径按 .cgui 所在目录找。留空用运行时自带图标">
        <input
          className="input mono icon-input"
          value={app.icon ?? ""}
          placeholder="assets\\app.ico"
          spellCheck={false}
          onFocus={onBeginHistory}
          onChange={(e) => set("icon", e.target.value || undefined)}
        />
        <button
          type="button"
          className="icon-pick"
          title="从磁盘选择 .ico 文件"
          onClick={async () => {
            const picked = await onPickIcon();
            if (picked) set("icon", picked);
          }}
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden>
            <path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
          </svg>
        </button>
      </Row>
      <Flag
        label="可视"
        checked={app.visible !== false}
        onChange={(v) => set("visible", v ? undefined : false)}
      />
      <Flag
        label="禁止"
        hint="整窗禁用：点了没反应"
        checked={app.enabled === false}
        onChange={(v) => set("enabled", v ? false : undefined)}
      />

      <div className="insp-section">按钮与行为</div>
      <Flag
        label="控制按钮"
        hint="标题栏右上角那一套（含关闭钮）"
        checked={app.controlBox !== false}
        onChange={(v) => set("controlBox", v ? undefined : false)}
      />
      <Flag
        label="最小化按钮"
        checked={app.minimizeBox !== false}
        onChange={(v) => set("minimizeBox", v ? undefined : false)}
      />
      <Flag
        label="最大化按钮"
        checked={app.maximizeBox !== false}
        onChange={(v) => set("maximizeBox", v ? undefined : false)}
      />
      <Flag
        label="在任务栏中显示"
        checked={app.taskbar !== false}
        onChange={(v) => set("taskbar", v ? undefined : false)}
      />
      <Flag label="总在最前" checked={!!app.topmost} onChange={(v) => set("topmost", v || undefined)} />
      <Flag
        label="Esc 键关闭"
        hint="关掉之后 Esc 会当成当前控件的 submit 事件"
        checked={app.escClose !== false}
        onChange={(v) => set("escClose", v ? undefined : false)}
      />
      <Flag
        label="回车下移焦点"
        hint="打开后回车是跳到下一个控件，不再当 submit"
        checked={!!app.enterTab}
        onChange={(v) => set("enterTab", v || undefined)}
      />
      <Row label="窗口类名" hint="一般不用改">
        <input
          className="input mono"
          value={app.class ?? ""}
          placeholder="CStackGUIMain"
          spellCheck={false}
          onFocus={onBeginHistory}
          onChange={(e) => set("class", e.target.value || undefined)}
        />
      </Row>

      <div className="insp-section">托盘</div>
      <Flag
        label="显示托盘图标"
        hint="托盘用窗口图标；左键单击显示窗口、右键有「显示/退出」菜单，并触发托盘事件"
        checked={!!app.tray}
        onChange={(v) => set("tray", v || undefined)}
      />

      <div className="insp-section">程序配置</div>
      <p className="muted small">
        {configured > 0
          ? `已填 ${configured} 项：${info.product ?? "未命名"}${info.version ? ` v${info.version}` : ""}`
          : "程序名称、版本号、作者、版权、图标……这些会写进编译后的 exe 版本信息。"}
      </p>
      <button className="primary" onClick={onOpenInfo}>
        打开「程序配置」
      </button>
      <p className="muted small">
        导出的工程里会带一份 <code>&lt;工程名&gt;.rc</code>，编译时用它，exe 的属性里就能看到这些信息。
      </p>
    </div>
  );
}
