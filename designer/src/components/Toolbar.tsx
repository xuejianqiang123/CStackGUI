/* Toolbar.tsx -- 顶部工具条：文件操作、画布尺寸、缩放、撤销重做 */
import type { CgDoc } from "../model";

type Props = {
  doc: CgDoc;
  filePath: string;
  dirty: boolean;
  /** 保存可用 = 有工程（导出过 C 骨架）或打开过文件；否则禁用并提示先导出 */
  canSave: boolean;
  zoom: number;
  showGrid: boolean;
  snapOn: boolean;
  canUndo: boolean;
  canRedo: boolean;
  onNew: () => void;
  onOpen: () => void;
  onSave: () => void;
  onSaveAs: () => void;
  onExportC: () => void;
  onOpenInfo: () => void;
  /** 「动作」对话框：管理命名命令（文字 + 快捷键 + 回调名），菜单项/控件引用它 */
  onOpenActions: () => void;
  /** 「样式」对话框：管理命名样式（字体/颜色字段），控件用 style 引用它 */
  onOpenStyles: () => void;
  /** 「色彩角色」对话框：管理 roles{}（深浅两份语义色），颜色字段用 @引用 */
  onOpenRoles: () => void;
  running: boolean;
  onRun: () => void;
  publishing: boolean;
  onPublish: () => void;
  onZoom: (z: number) => void;
  onToggleGrid: () => void;
  onToggleSnap: () => void;
  /** 预演模式：按 grow 契约模拟"窗口拉伸后"的布局（只读，不改设计坐标） */
  preview: boolean;
  onTogglePreview: () => void;
  /** 预演窗口尺寸 = 设计尺寸 × 百分比（宽/高独立） */
  previewPct: { w: number; h: number };
  onPreviewPct: (p: { w: number; h: number }) => void;
  onUndo: () => void;
  onRedo: () => void;
  projectRoot: string;
  onLocateProject: () => void;
  /** QQ 交流群号：常驻显示，点击复制 */
  qqGroup: string;
  onQqGroup: () => void;
};

export default function Toolbar(p: Props) {
  /* 没保存过时按默认标题显示目标文件名，所见即所得（保存建议名就是它） */
  const name = p.filePath ? p.filePath.split(/[\\/]/).pop() : `${p.doc.app.title || "main"}.cgui`;

  return (
    <div className="toolbar">
      <div className="brand">
        <span className="brand-mark">C</span>
        <span className="brand-name">CStackGUI</span>
      </div>

      <div className="tool-group">
        <button onClick={p.onNew}>新建</button>
        <button onClick={p.onOpen}>打开</button>
        <button
          className="primary"
          onClick={p.onSave}
          disabled={!p.canSave}
          title={p.canSave ? "Ctrl+S：保存到工程的 .cgui" : "先导出 C 骨架，保存位置会自动跟随工程目录"}
        >
          保存
        </button>
        <button onClick={p.onSaveAs}>另存为</button>
        <button onClick={p.onOpenInfo} title="程序名称 / 版本号 / 作者 / 版权……写进编译后的 exe 版本信息">
          程序配置
        </button>
        <button onClick={p.onOpenActions} title="动作：一条命名命令（文字 + 快捷键 + 回调名），菜单项和控件引用它">
          动作
        </button>
        <button
          onClick={p.onOpenStyles}
          title="样式：一条命名样式（字体 / 字号 / 颜色 / 圆角），控件用 style 引用它（“style”: “名字” 或 { use: “名字”, ...覆盖 }）"
        >
          样式
        </button>
        <button
          onClick={p.onOpenRoles}
          title="色彩角色：一个语义色的浅色 / 深色两份值，颜色字段写 “@名字” 引用——切主题全套跟着走"
        >
          色彩
        </button>
      </div>

      <div className="tool-group">
        <button className="accent" onClick={p.onExportC} title="生成 C 事件骨架，落到 apps/<名字>/">
          导出 C 骨架
        </button>
        <button
          className="run-btn"
          disabled={p.running}
          onClick={p.onRun}
          title="F5：保存 → 编译 → 启动 exe。编译在 CStackGUI 内部完成（只要装了 MinGW-w64），工程需已导出 C 骨架"
        >
          {p.running ? "编译中…" : "▶ 运行"}
        </button>
        <button
          disabled={p.publishing || p.running}
          onClick={p.onPublish}
          title="发布：保存 → release 编译 → 界面在编译时编进 exe，产物就是工程目录里那个独立 exe（.cgui 旁边）。对方拿走一个文件双击就能跑，不需要 .cgui"
        >
          {p.publishing ? "发布中…" : "⇪ 发布"}
        </button>
      </div>

      <div className="tool-group">
        <button onClick={p.onUndo} disabled={!p.canUndo} title="Ctrl+Z">
          撤销
        </button>
        <button onClick={p.onRedo} disabled={!p.canRedo} title="Ctrl+Shift+Z">
          重做
        </button>
      </div>

      <div className="tool-group grow" />

      <div className="tool-group">
        <button onClick={() => p.onZoom(Math.round((p.zoom - 0.1) * 10) / 10)}>−</button>
        <span className="zoom-readout">{Math.round(p.zoom * 100)}%</span>
        <button onClick={() => p.onZoom(Math.round((p.zoom + 0.1) * 10) / 10)}>+</button>
        <button onClick={() => p.onZoom(1)}>100%</button>
        <button className={p.showGrid ? "on" : ""} onClick={p.onToggleGrid} title="显示 16px 网格">
          网格
        </button>
        <button className={p.snapOn ? "on" : ""} onClick={p.onToggleSnap} title="拖动时吸附到 4px 栅格与邻近控件">
          吸附
        </button>
        <button
          className={p.preview ? "on" : ""}
          onClick={p.onTogglePreview}
          title="预演：按 grow 契约模拟窗口拉伸后的布局（只读视图，设计坐标一个字节都不动；对照 F5 真机看的 30 秒反馈回路，这里是 0 秒）"
        >
          预演
        </button>
        {p.preview ? (
          <>
            <input
              className="sim-slider"
              type="range"
              min={50}
              max={200}
              step={5}
              value={p.previewPct.w}
              onChange={(e) => p.onPreviewPct({ ...p.previewPct, w: Number(e.target.value) })}
              title="模拟窗口宽度（相对设计尺寸的百分比）"
            />
            <input
              className="sim-slider"
              type="range"
              min={50}
              max={200}
              step={5}
              value={p.previewPct.h}
              onChange={(e) => p.onPreviewPct({ ...p.previewPct, h: Number(e.target.value) })}
              title="模拟窗口高度（相对设计尺寸的百分比）"
            />
            <span className="sim-readout">
              {Math.round((p.doc.app.width * p.previewPct.w) / 100)}×{Math.round((p.doc.app.height * p.previewPct.h) / 100)}
            </span>
          </>
        ) : null}
      </div>

      <button onClick={p.onQqGroup} title="QQ 交流群：点击复制群号，打开 QQ 搜索即可加入">
        QQ 群 {p.qqGroup}
      </button>

      <div className="file-state">
        <span className={p.dirty ? "dot dirty" : "dot"} />
        <span className="file-name">{name}</span>
        {p.dirty ? <span className="muted small">未保存</span> : null}
        {p.projectRoot ? (
          <span className="muted small" title={`导出 C 骨架会落到 ${p.projectRoot}\\apps\\<名字>\\`}>
            项目: {p.projectRoot.split(/[\\/]/).pop()}
          </span>
        ) : (
          <button className="mini" onClick={p.onLocateProject} title="打包后的画布需要你指一次 C 项目根，之后会记住">
            定位项目根
          </button>
        )}
      </div>
    </div>
  );
}
