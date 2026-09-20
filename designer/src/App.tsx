/* App.tsx -- 画布主界面
 *
 * 数据流很简单：doc 是唯一真相，所有编辑都是「克隆一份改完放回去」，
 * 顺手把旧的一份压进撤销栈。文件读写全部走 bridge（Electron 或浏览器降级）。
 */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import Canvas from "./components/Canvas";
import EventPanel from "./components/EventPanel";
import Inspector from "./components/Inspector";
import InfoDialog from "./components/InfoDialog";
import LayerTree from "./components/LayerTree";
import NewProjectDialog from "./components/NewProjectDialog";
import Palette from "./components/Palette";
import SupportPanel from "./components/SupportPanel";
import Toolbar from "./components/Toolbar";
import WindowPanel from "./components/WindowPanel";
import { getBridge } from "./bridge";
import { collectCallbacks, generateC, uniqueCallbackNames } from "./codegen";
import {
  absRect,
  clamp,
  emptyDoc,
  findNode,
  findParent,
  isContainer,
  makeNode,
  parseDoc,
  removeNode,
  serializeDoc,
  templateDoc,
  walk,
  type CgDoc,
  type CgNode,
  type TemplateKind,
} from "./model";

type Tab = "props" | "events" | "doc" | "support";

/** F5 一键运行的输出面板：把 build-app.bat 的输出流式显示出来 */
function RunPanel({ lines, running, onClose }: { lines: string[]; running: boolean; onClose: () => void }) {
  const bodyRef = useRef<HTMLPreElement | null>(null);

  useEffect(() => {
    if (bodyRef.current) bodyRef.current.scrollTop = bodyRef.current.scrollHeight;
  }, [lines]);

  return (
    <div className="run-panel">
      <div className="run-head">
        <span className={running ? "run-state busy" : "run-state"}>{running ? "正在编译…" : "运行输出"}</span>
        <button className="mini" onClick={onClose} title="收起">
          ×
        </button>
      </div>
      <pre ref={bodyRef} className="run-body">
        {lines.length ? lines.join("\n") : "# 正在启动编译…"}
      </pre>
    </div>
  );
}

export default function App() {
  const bridge = useMemo(() => getBridge(), []);
  const [doc, setDoc] = useState<CgDoc>(() => emptyDoc());
  const [filePath, setFilePath] = useState("");
  const [dirty, setDirty] = useState(false);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [zoom, setZoom] = useState(1);
  const [showGrid, setShowGrid] = useState(true);
  const [snapOn, setSnapOn] = useState(true); // 拖动时吸附到 4px 栅格 + 邻近控件
  const [tab, setTab] = useState<Tab>("props");
  const [pendingDrop, setPendingDrop] = useState<string | null>(null);
  const [projectRoot, setProjectRoot] = useState("");
  const [toast, setToast] = useState("");
  /** 最近一次导出的结果：给右下角「下一步」用，把画布和命令行接起来 */
  const [lastExport, setLastExport] = useState<{ dir: string; appName: string } | null>(null);
  /** 一键运行（F5）：编译输出 + 面板开关 */
  const [runOpen, setRunOpen] = useState(false);
  const [runLines, setRunLines] = useState<string[]>([]);
  const [running, setRunning] = useState(false);
  /** 「程序配置」对话框开关（工具栏按钮 / WindowPanel 入口都能打开） */
  const [infoOpen, setInfoOpen] = useState(false);
  /** 「新建工程」模板选择对话框 */
  const [newOpen, setNewOpen] = useState(false);

  const past = useRef<CgDoc[]>([]);
  const future = useRef<CgDoc[]>([]);
  const [histVersion, setHistVersion] = useState(0);
  const docRef = useRef(doc);
  docRef.current = doc;

  const selected = selectedId ? findNode(doc, selectedId) : null;

  const say = useCallback((msg: string) => {
    setToast(msg);
    window.setTimeout(() => setToast((cur) => (cur === msg ? "" : cur)), 4000);
  }, []);

  /* -------------------------------------------------- 撤销 / 编辑 */
  const onBeginHistory = useCallback(() => {
    past.current.push(structuredClone(docRef.current));
    if (past.current.length > 200) past.current.shift();
    future.current = [];
    setHistVersion((v) => v + 1);
  }, []);

  const onApply = useCallback((mutator: (d: CgDoc) => void) => {
    setDoc((cur) => {
      const next = structuredClone(cur);
      mutator(next);
      return next;
    });
    setDirty(true);
  }, []);

  /** 一步到位的编辑：自己压撤销栈 */
  const edit = useCallback(
    (mutator: (d: CgDoc) => void) => {
      onBeginHistory();
      onApply(mutator);
    },
    [onBeginHistory, onApply],
  );

  const undo = useCallback(() => {
    const prev = past.current.pop();
    if (!prev) return;
    future.current.push(structuredClone(docRef.current));
    setDoc(prev);
    setDirty(true);
    setHistVersion((v) => v + 1);
  }, []);

  const redo = useCallback(() => {
    const next = future.current.pop();
    if (!next) return;
    past.current.push(structuredClone(docRef.current));
    setDoc(next);
    setDirty(true);
    setHistVersion((v) => v + 1);
  }, []);

  /* -------------------------------------------------- 控件增删 */
  const addNode = useCallback(
    (type: string) => {
      let created: CgNode | null = null;
      edit((d) => {
        /* 找一个不压住已有控件的位置放新控件 */
        const offset = (d.widgets.length % 8) * 24;
        const node = makeNode(type, 24 + offset, 24 + offset);
        const taken = new Set<string>();
        walk(d.widgets, (n) => taken.add(n.id));
        let i = 2;
        while (taken.has(node.id)) node.id = `${type}_${i++}`;
        d.widgets.push(node);
        created = node;
      });
      if (created) setSelectedId((created as CgNode).id);
      say(`已添加 ${type}`);
    },
    [edit, say],
  );

  const deleteNode = useCallback(
    (id: string) => {
      edit((d) => {
        removeNode(d, id);
      });
      setSelectedId(null);
    },
    [edit],
  );

  const duplicateSelected = useCallback(() => {
    if (!selectedId) return;
    let newId: string | null = null;
    edit((d) => {
      const node = findNode(d, selectedId);
      if (!node) return;
      const copy: CgNode = structuredClone(node);
      copy.rect.x += 16;
      copy.rect.y += 16;
      const taken = new Set<string>();
      walk(d.widgets, (n) => taken.add(n.id));
      let i = 2;
      while (taken.has(`${copy.type}_${i}`)) i += 1;
      copy.id = `${copy.type}_${i}`;
      const parent = findParent(d, selectedId);
      if (parent) {
        parent.children = parent.children ?? [];
        parent.children.push(copy);
      } else {
        d.widgets.push(copy);
      }
      newId = copy.id;
    });
    if (newId) setSelectedId(newId);
  }, [edit, selectedId]);

  /* -------------------------------------------------- 文件操作 */
  const doNew = useCallback(async (kind: TemplateKind = "blank") => {
    if (dirty && !(await bridge.confirmDiscard("当前改动还没保存，确定新建？"))) return;
    const fresh = templateDoc(kind);
    past.current = [];
    future.current = [];
    setDoc(fresh);
    setFilePath("");
    setDirty(false);
    setSelectedId(null);
    /* 不清 lastExport 的话，Ctrl+S 会把新画布内容静默覆盖进上一个工程的 .cgui */
    setLastExport(null);
    setHistVersion((v) => v + 1);
  }, [bridge, dirty]);

  const doOpen = useCallback(async () => {
    if (dirty && !(await bridge.confirmDiscard("当前改动还没保存，确定打开别的文件？"))) return;
    const res = await bridge.openFile();
    if (res.canceled || !res.text) return;
    const parsed = parseDoc(res.text);
    if (!parsed.doc) {
      say(`打开失败：${parsed.error}`);
      return;
    }
    past.current = [];
    future.current = [];
    setDoc(parsed.doc);
    setFilePath(res.path ?? "");
    setDirty(false);
    setSelectedId(null);
    /* 同 doNew：打开别的文件后，保存必须写它自己，而不是上一次导出的工程 */
    setLastExport(null);
    setHistVersion((v) => v + 1);
    say(`已打开 ${res.path ?? ""}`.trim());
  }, [bridge, dirty, say]);

  /* 保存目标：导出过 C 骨架 -> 固定写工程里的 .cgui（禁止再弹框选位置）；
   * 没导出过但打开过文件 -> 写原路径；两者都没有 -> 保存不可用。 */
  const cguiTarget = lastExport ? `${lastExport.dir}\\${lastExport.appName}.cgui` : filePath;
  const canSave = !!cguiTarget;

  const doSave = useCallback(
    async (forceDialog = false) => {
      const target = lastExport ? `${lastExport.dir}\\${lastExport.appName}.cgui` : filePath;
      if (!target) {
        say("先点「导出 C 骨架」：保存位置跟随工程目录，不用自己选");
        return { canceled: true as const };
      }
      if (forceDialog) {
        /* 「另存为」是逃生门，仍允许选位置 */
        const res = await bridge.saveFile({
          filePath: "",
          text: serializeDoc(docRef.current),
          suggestName: suggestedBaseName(docRef.current.app.title),
        });
        if (res.canceled) return { canceled: true as const };
        if (res.path) setFilePath(res.path);
        setDirty(false);
        say(`已保存 ${res.path ?? ""}`.trim());
        return { canceled: false as const, path: res.path };
      }
      const res = await bridge.saveFile({
        filePath: target,
        text: serializeDoc(docRef.current),
        suggestName: suggestedBaseName(docRef.current.app.title),
      });
      if (res.canceled) return { canceled: true as const };
      setFilePath(res.path ?? target);
      setDirty(false);
      say(`已保存 ${res.path ?? target}`);
      return { canceled: false as const, path: res.path ?? target };
    },
    [bridge, filePath, lastExport, say],
  );

  const doExportC = useCallback(async () => {
    const baseName = suggestedBaseName(docRef.current.app.title);
    const files = generateC(docRef.current, baseName);
    /* 带上当前文件路径：主进程好从文件位置向上认出真正的项目根（便携版画布尤甚） */
    const res = await bridge.exportC({ baseName, files, filePath: filePath || undefined });
    if (res.canceled) return;
    if (res.projectRoot) setProjectRoot(res.projectRoot);
    setLastExport({ dir: res.dir ?? "", appName: baseName });
    /* 导出时 .cgui 已经写进工程目录了：把「当前文件」指向它并清掉未保存标记，
     * 否则紧接着点保存又会弹框让你再选一次位置（导出即保存）。 */
    if (res.dir) {
      setFilePath(`${res.dir}\\${baseName}.cgui`);
      setDirty(false);
    }
    const skipped = res.skipped?.length ? `（跳过已存在的 ${res.skipped.join("、")}）` : "";
    say(`已导出到 ${res.dir}${skipped}，接着去填 ${baseName}_ui_events.c`);
  }, [bridge, docRef, projectRoot, say, filePath]);

  /* 「代码」按钮：把「导出骨架后要切编辑器找函数」这一跳补上 */
  const openCallback = useCallback(
    async (callback: string) => {
      const res = await bridge.openCallback({ filePath: filePath || undefined, callback });
      say(res.message);
    },
    [bridge, filePath, say],
  );

  const doLocateProject = useCallback(async () => {
    const res = await bridge.locateProject();
    if (res.canceled || !res.projectRoot) return;
    setProjectRoot(res.projectRoot);
    say(`已记住项目根：${res.projectRoot}`);
  }, [bridge, say]);

  /* -------------------------------------------------- 一键运行（F5） */
  const doRun = useCallback(async () => {
    setRunOpen(true);
    setRunLines([]);

    /* 跑之前必须先落盘：改过的界面不保存，跑的还是旧的 */
    if (dirty || !filePath) {
      const saved = await doSave(!filePath);
      if (saved.canceled) {
        say("先保存才能运行");
        setRunOpen(false);
        return;
      }
    }

    setRunning(true);
    try {
      const res = await bridge.buildAndRun({ filePath });
      if (res.ok) say(res.message);
      else setRunLines((ls) => [...ls, "", `# ${res.message}`]);
    } finally {
      setRunning(false);
    }
  }, [bridge, dirty, doSave, filePath, say]);

  /* -------------------------------------------------- 发布（运行按钮后面那个） */
  const [publishing, setPublishing] = useState(false);
  const doPublish = useCallback(async () => {
    setRunOpen(true);
    setRunLines([]);

    /* 发布前必须先落盘：zip 里要打进最新的 .cgui */
    if (dirty || !filePath) {
      const saved = await doSave(!filePath);
      if (saved.canceled) {
        say("先保存才能发布");
        setRunOpen(false);
        return;
      }
    }

    setPublishing(true);
    try {
      const res = await bridge.publish({ filePath });
      if (res.ok) say(res.message);
      else setRunLines((ls) => [...ls, "", `# ${res.message}`]);
    } finally {
      setPublishing(false);
    }
  }, [bridge, dirty, doSave, filePath, say]);

  /* -------------------------------------------------- 菜单 & 快捷键 */
  useEffect(() => {
    bridge.onMenu((action) => {
      if (action === "new") setNewOpen(true);
      else if (action === "open") void doOpen();
      else if (action === "save") void doSave();
      else if (action === "saveAs") void doSave(true);
      else if (action === "exportC") void doExportC();
      else if (action === "run") void doRun();
      else if (action === "programConfig") setInfoOpen(true);
      else if (action === "undo") undo();
      else if (action === "redo") redo();
      else if (action === "duplicate") duplicateSelected();
      else if (action === "delete" && selectedId) deleteNode(selectedId);
      else if (action === "zoomIn") setZoom((z) => Math.min(3, Math.round((z + 0.1) * 10) / 10));
      else if (action === "zoomOut") setZoom((z) => Math.max(0.2, Math.round((z - 0.1) * 10) / 10));
      else if (action === "zoomFit") setZoom(1);
    });
  }, [bridge, doNew, doOpen, doSave, doExportC, doRun, undo, redo, duplicateSelected, deleteNode, selectedId, infoOpen]);

  /* 编译/运行输出流：注册一次即可，只碰 setState */
  useEffect(() => {
    bridge.onRunOutput((o) => {
      if (o.kind === "line") setRunLines((ls) => [...ls.slice(-300), o.text ?? ""]);
      else if (o.kind === "done") setRunLines((ls) => [...ls, "", `# 已启动 ${o.text ?? ""}`]);
      else if (o.kind === "fail") setRunLines((ls) => [...ls, "", `# 编译失败（退出码 ${o.code ?? "?"}）`]);
    });
  }, [bridge]);

  /* 浏览器里跑时 Electron 菜单不存在，这里补一套等价快捷键 */
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null;
      const typing = target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT");
      const mod = e.ctrlKey || e.metaKey;

      if (mod && e.key.toLowerCase() === "s") {
        e.preventDefault();
        void doSave(e.shiftKey);
        return;
      }
      if (mod && e.key.toLowerCase() === "o") {
        e.preventDefault();
        void doOpen();
        return;
      }
      if (mod && e.key.toLowerCase() === "e") {
        e.preventDefault();
        void doExportC();
        return;
      }
      if (e.key === "F5") {
        e.preventDefault();
        void doRun();
        return;
      }
      if (mod && e.key.toLowerCase() === "z") {
        e.preventDefault();
        if (e.shiftKey) redo();
        else undo();
        return;
      }
      if (typing) return;
      if (mod && e.key.toLowerCase() === "d") {
        e.preventDefault();
        duplicateSelected();
        return;
      }
      if (e.key === "Delete" || e.key === "Backspace") {
        if (selectedId) {
          e.preventDefault();
          deleteNode(selectedId);
        }
        return;
      }
      if (e.key === "Escape") {
        setSelectedId(null);
        return;
      }
      if (e.key.startsWith("Arrow") && selectedId) {
        e.preventDefault();
        const step = e.shiftKey ? 8 : 1;
        const dx = e.key === "ArrowLeft" ? -step : e.key === "ArrowRight" ? step : 0;
        const dy = e.key === "ArrowUp" ? -step : e.key === "ArrowDown" ? step : 0;
        edit((d) => {
          const n = findNode(d, selectedId);
          if (!n) return;
          const parent = findParent(d, selectedId);
          const maxW = parent ? parent.rect.w : d.app.width;
          const maxH = parent ? parent.rect.h : d.app.height;
          n.rect.x = clamp(n.rect.x + dx, 0, Math.max(0, maxW - n.rect.w));
          n.rect.y = clamp(n.rect.y + dy, 0, Math.max(0, maxH - n.rect.h));
        });
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [doSave, doOpen, doExportC, doRun, undo, redo, duplicateSelected, deleteNode, selectedId, edit]);

  /* -------------------------------------------------- 标题 & 项目路径 */
  useEffect(() => {
    bridge.setTitle(`${filePath ? filePath.split(/[\\/]/).pop() : doc.app.title || "main"}${dirty ? " *" : ""}`);
  }, [bridge, filePath, dirty, doc.app.title]);

  useEffect(() => {
    void bridge.info().then((info) => {
      if (info.projectRoot) setProjectRoot(info.projectRoot);
    });
  }, [bridge]);

  /* 命令行带了 .cgui（CStackGUI.exe xxx.cgui）就直接打开 */
  useEffect(() => {
    let alive = true;
    void bridge.initialFile().then((res) => {
      if (!alive || res.canceled || !res.text) return;
      const parsed = parseDoc(res.text);
      if (!parsed.doc) {
        say(`打开失败：${parsed.error}`);
        return;
      }
      setDoc(parsed.doc);
      setFilePath(res.path ?? "");
      setDirty(false);
      setSelectedId(null);
      say(`已打开 ${res.path ?? ""}`.trim());
    });
    return () => {
      alive = false;
    };
  }, [bridge, say]);

  /* 拖进浏览器窗口的 .cgui 也能直接打开 */
  useEffect(() => {
    const onDrop = async (e: DragEvent) => {
      const file = e.dataTransfer?.files?.[0];
      if (!file || !file.name.endsWith(".cgui")) return;
      e.preventDefault();
      const parsed = parseDoc(await file.text());
      if (!parsed.doc) {
        say(`打开失败：${parsed.error}`);
        return;
      }
      setDoc(parsed.doc);
      setFilePath(file.name);
      setDirty(false);
      setSelectedId(null);
      setLastExport(null);
    };
    const prevent = (e: DragEvent) => e.preventDefault();
    window.addEventListener("dragover", prevent);
    window.addEventListener("drop", onDrop);
    return () => {
      window.removeEventListener("dragover", prevent);
      window.removeEventListener("drop", onDrop);
    };
  }, [say]);

  const stats = useMemo(() => {
    let count = 0;
    let containers = 0;
    walk(doc.widgets, (n) => {
      count += 1;
      if (isContainer(n.type)) containers += 1;
    });
    const callbacks = uniqueCallbackNames(doc);
    const uses = collectCallbacks(doc);
    return { count, containers, callbacks: callbacks.length, uses };
  }, [doc]);

  return (
    <div className="app">
      <Toolbar
        doc={doc}
        filePath={filePath}
        dirty={dirty}
        canSave={canSave}
        zoom={zoom}
        showGrid={showGrid}
        snapOn={snapOn}
        canUndo={past.current.length > 0}
        canRedo={future.current.length > 0}
        onNew={() => setNewOpen(true)}
        onOpen={() => void doOpen()}
        onSave={() => void doSave()}
        onSaveAs={() => void doSave(true)}
        onExportC={() => void doExportC()}
        running={running}
        onRun={() => void doRun()}
        publishing={publishing}
        onPublish={() => void doPublish()}
        onOpenInfo={() => setInfoOpen(true)}
        onZoom={(z) => setZoom(clamp(z, 0.2, 3))}
        onToggleGrid={() => setShowGrid((v) => !v)}
        onToggleSnap={() => setSnapOn((v) => !v)}
        onUndo={undo}
        onRedo={redo}
        onPatchApp={(patch) => edit((d) => Object.assign(d.app, patch))}
        projectRoot={projectRoot}
        onLocateProject={() => void doLocateProject()}
      />

      <div className="body">
        <div className="left">
          <Palette onAdd={addNode} onStartDrop={(t) => setPendingDrop(t)} />
          <LayerTree
            doc={doc}
            selectedId={selectedId}
            onSelect={setSelectedId}
            onBeginHistory={onBeginHistory}
            onApply={onApply}
            onRemove={deleteNode}
          />
        </div>

        <Canvas
          doc={doc}
          selectedId={selectedId}
          zoom={zoom}
          snapOn={snapOn}
          showGrid={showGrid}
          onSelect={setSelectedId}
          onBeginHistory={onBeginHistory}
          onApply={onApply}
          pendingDrop={pendingDrop}
          onDropDone={() => setPendingDrop(null)}
        />

        <div className="right">
          <div className="tabs">
            <button className={tab === "props" ? "tab on" : "tab"} onClick={() => setTab("props")}>
              属性
            </button>
            <button className={tab === "events" ? "tab on" : "tab"} onClick={() => setTab("events")}>
              事件
            </button>
            <button className={tab === "doc" ? "tab on" : "tab"} onClick={() => setTab("doc")}>
              文档
            </button>
            <button className={tab === "support" ? "tab on" : "tab"} onClick={() => setTab("support")}>
              支持库
            </button>
          </div>
          <div className="tab-body">
            {tab === "props" ? (
              selected ? (
                <Inspector doc={doc} node={selected} onBeginHistory={onBeginHistory} onApply={onApply} onSelect={setSelectedId} />
              ) : (
                <WindowPanel
                  doc={doc}
                  onBeginHistory={onBeginHistory}
                  onApply={onApply}
                  onOpenInfo={() => setInfoOpen(true)}
                  onPickIcon={async () => {
                    const res = await bridge.pickIcon();
                    if (res.canceled) return null;
                    return res.rel ?? res.path ?? null;
                  }}
                />
              )
            ) : null}
            {tab === "events" ? (
              <EventPanel
                doc={doc}
                node={selected}
                onBeginHistory={onBeginHistory}
                onApply={onApply}
                onOpenCallback={(name) => void openCallback(name)}
              />
            ) : null}
            {tab === "support" ? <SupportPanel onSay={say} /> : null}
            {tab === "doc" ? (
              <div className="panel-body">
                <div className="insp-section">概览</div>
                <ul className="hint-list">
                  <li>控件 {stats.count} 个（其中容器 {stats.containers} 个）</li>
                  <li>绑定事件 {stats.uses.length} 处，涉及 {stats.callbacks} 个 C 函数</li>
                  <li>
                    窗口 {doc.app.width}×{doc.app.height}，底色 {doc.app.bg}
                  </li>
                  <li>文件格式 .cgui（JSON，可手改，改完用画布打开即可）</li>
                </ul>

                <div className="insp-section">窗口事件</div>
                <div className="insp-row">
                  <span className="insp-label" title="用户点关闭按钮时触发，适合做「保存确认」">
                    关闭前
                  </span>
                  <span className="insp-value">
                    <input
                      className="input mono"
                      value={doc.app.events?.close ?? ""}
                      placeholder="on_close"
                      spellCheck={false}
                      onChange={(e) => {
                        const v = e.target.value.trim();
                        onApply((d) => {
                          if (v) d.app.events = { ...(d.app.events ?? {}), close: v };
                          else delete d.app.events;
                        });
                      }}
                    />
                  </span>
                </div>
                <p className="muted small">
                  绑定后回调里可以用 <code>cg_quit(app)</code> 决定到底关不关；不绑就是直接关。
                </p>

                <div className="insp-section">导出的 C 骨架</div>
                {stats.callbacks === 0 ? (
                  <p className="muted small">还没有绑定任何事件。</p>
                ) : (
                  <ul className="hint-list mono small">
                    {uniqueCallbackNames(doc).map((n) => (
                      <li key={n}>{n}</li>
                    ))}
                  </ul>
                )}

                <div className="insp-section">接口速查</div>
                <p className="muted small">
                  「支持库」页签里按用途列出了全部 C API（带说明和示例，双击即复制）。
                </p>
                <pre className="code-block">{`/* 每个回调长这样 */
void on_按钮事件(cg_widget_t *self, cg_event_t ev, void *user);

/* 最常用的四句 */
cg_widget_t *w = cg_find(cg_app_of(self), "控件id");  /* 找别的控件 */
cg_widget_set_text(w, "文本");                        /* 改文字 */
cg_widget_checked(w) / cg_widget_value(w);            /* 读状态 */
cg_set_timer(cg_app_of(self), 1000, 回调, user);       /* 延迟做一件事 */`}</pre>

                <div className="insp-section">当前选区</div>
                {selected ? (
                  <pre className="code-block">{`${selected.id} · ${selected.type}
rect = { x:${selected.rect.x}, y:${selected.rect.y}, w:${selected.rect.w}, h:${selected.rect.h} }
绝对位置 = { x:${absRect(doc, selected).x}, y:${absRect(doc, selected).y} }${findParent(doc, selected.id) ? `\n父容器 = ${findParent(doc, selected.id)?.id}` : ""}`}</pre>
                ) : (
                  <p className="muted small">没有选中控件。</p>
                )}
              </div>
            ) : null}
          </div>

          {/* 画布命令接不到下一环，这一步交给使用者；所以把「接着敲什么」直接摆在面板里 */}
          <div className="next-steps">
            <div className="next-head">
              下一步
              <span className="muted small">（画布只到 .cgui，编译要在项目根跑）</span>
            </div>
            {lastExport ? (
              <ol className="next-list">
                <li>
                  打开 <code>{`${lastExport.dir}\\${lastExport.appName}_ui_events.c`}</code>，把里面每个空函数填成业务逻辑
                </li>
                <li>
                  在项目根目录跑 <code>{`build-app.bat ${lastExport.appName}`}</code>
                  {!projectRoot ? <span className="warn">（还没定位项目根）</span> : null}
                </li>
                <li>
                  运行 <code>{`build\\${lastExport.appName}.exe apps\\${lastExport.appName}\\${lastExport.appName}.cgui`}</code>
                  ，或先跑 <code>--selftest</code> 只校验事件名
                </li>
              </ol>
            ) : (
              <div className="next-list muted small">
                先在「事件」页签给控件填上 C 函数名，再点工具条的「导出 C 骨架」。
              </div>
            )}
            <div className="next-foot">
              {projectRoot ? (
                <span className="muted small" title={projectRoot}>
                  项目根：{projectRoot}
                </span>
              ) : (
                <button className="mini" onClick={() => void doLocateProject()}>
                  定位 C 项目根
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {toast ? (
        <div className="toast" onClick={() => setToast("")}>
          {toast}
        </div>
      ) : null}

      {runOpen ? <RunPanel lines={runLines} running={running} onClose={() => setRunOpen(false)} /> : null}
      <NewProjectDialog
        open={newOpen}
        onClose={() => setNewOpen(false)}
        onPick={(kind) => {
          setNewOpen(false);
          void doNew(kind);
        }}
      />
      <InfoDialog
        open={infoOpen}
        doc={doc}
        onClose={() => setInfoOpen(false)}
        onBeginHistory={onBeginHistory}
        onApply={onApply}
      />
      <span style={{ display: "none" }}>{histVersion}</span>
    </div>
  );
}

/** 从窗口标题推一个合法的导出名：只留 C 标识符字符（导出目录名要和
 *  build-app.bat 的工程名、exe 名保持一致，必须 ASCII）；清完什么都不剩
 *  （比如纯中文标题）就回退 main，避免出现 _____ui_events.c 这种文件名。 */
export function suggestedBaseName(title: string): string {
  const s = title
    .replace(/[^0-9a-zA-Z_]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_|_$/g, "")
    .replace(/^(\d)/, "_$1"); /* codegen 的 sanitizeIdent 会给数字开头加 _，这里先统一，保证目录名 = 工程名 */
  return s || "main";
}
