/* Toolbar.tsx -- 顶部工具条：文件操作、画布尺寸、缩放、撤销重做 */
import type { CgDoc } from "../model";

type Props = {
  doc: CgDoc;
  filePath: string;
  dirty: boolean;
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
  running: boolean;
  onRun: () => void;
  onZoom: (z: number) => void;
  onToggleGrid: () => void;
  onToggleSnap: () => void;
  onUndo: () => void;
  onRedo: () => void;
  onPatchApp: (patch: Partial<CgDoc["app"]>) => void;
  projectRoot: string;
  onLocateProject: () => void;
};

export default function Toolbar(p: Props) {
  const name = p.filePath ? p.filePath.split(/[\\/]/).pop() : "未命名.cgui";

  return (
    <div className="toolbar">
      <div className="brand">
        <span className="brand-mark">C</span>
        <span className="brand-name">CStackGUI</span>
      </div>

      <div className="tool-group">
        <button onClick={p.onNew}>新建</button>
        <button onClick={p.onOpen}>打开</button>
        <button className="primary" onClick={p.onSave}>
          保存
        </button>
        <button onClick={p.onSaveAs}>另存为</button>
      </div>

      <div className="tool-group">
        <button className="accent" onClick={p.onExportC} title="生成 C 事件骨架，落到 apps/<名字>/">
          导出 C 骨架
        </button>
        <button
          className="run-btn"
          disabled={p.running}
          onClick={p.onRun}
          title="F5：保存 → build-app.bat 编译 → 启动 exe（工程要在 apps\\ 下且已导出 C 骨架）"
        >
          {p.running ? "编译中…" : "▶ 运行"}
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

      <div className="tool-group grow">
        <label className="field">
          <span>标题</span>
          <input className="input" value={p.doc.app.title} onChange={(e) => p.onPatchApp({ title: e.target.value })} />
        </label>
        <label className="field narrow">
          <span>宽</span>
          <input
            className="input"
            type="number"
            value={p.doc.app.width}
            onChange={(e) => p.onPatchApp({ width: Number(e.target.value) || 100 })}
          />
        </label>
        <label className="field narrow">
          <span>高</span>
          <input
            className="input"
            type="number"
            value={p.doc.app.height}
            onChange={(e) => p.onPatchApp({ height: Number(e.target.value) || 100 })}
          />
        </label>
        <label className="field narrow">
          <span>底色</span>
          <input
            className="input color"
            type="color"
            value={p.doc.app.bg}
            onChange={(e) => p.onPatchApp({ bg: e.target.value })}
          />
        </label>
      </div>

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
      </div>

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
