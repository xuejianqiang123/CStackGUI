/* App.tsx -- 画布主界面
 *
 * 数据流很简单：doc 是唯一真相，所有编辑都是「克隆一份改完放回去」，
 * 顺手把旧的一份压进撤销栈。文件读写全部走 bridge（Electron 或浏览器降级）。
 */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import ActionsDialog from "./components/ActionsDialog";
import Canvas from "./components/Canvas";
import EventPanel from "./components/EventPanel";
import Inspector from "./components/Inspector";
import InfoDialog from "./components/InfoDialog";
import LayerTree from "./components/LayerTree";
import NewProjectDialog from "./components/NewProjectDialog";
import Palette from "./components/Palette";
import StyleDialog from "./components/StyleDialog";
import SupportPanel from "./components/SupportPanel";
import Toolbar from "./components/Toolbar";
import WindowPanel from "./components/WindowPanel";
import { getBridge } from "./bridge";
import { setImageBase } from "./imagecache";
import { collectCallbacks, generateC, uniqueCallbackNames } from "./codegen";
import {
  absRect,
  clamp,
  cloneNode,
  emptyDoc,
  findNode,
  findParent,
  isContainer,
  makeNode,
  moveNodes,
  parseDoc,
  removeNode,
  serializeDoc,
  templateDoc,
  topLevelOnly,
  uniquifyIds,
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
  /* 选择集：数组顺序 = 点选顺序，**最后一个是主选框**（属性面板显示它、拖动吸附以它为准）。
     单选就是长度为 1 的集合，所以老的 setSelectedId(id) 语义由下面的 selectOne 原样保留。 */
  const [selectedIds, setSelectedIds] = useState<string[]>([]);
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
  /** 「动作」对话框开关（工具栏按钮 / electron 菜单都走这里打开） */
  const [actionsOpen, setActionsOpen] = useState(false);
  /** 「样式」对话框开关（工具栏按钮打开）：管理顶层 styles{}（命名样式） */
  const [stylesOpen, setStylesOpen] = useState(false);
  /** 外部把 .cgui 改了、但画布有未保存改动时挂在这里：底部出通知条问一句，绝不自动覆盖 */
  const [external, setExternal] = useState<CgDoc | null>(null);
  /** 「新建工程」模板选择对话框 */
  const [newOpen, setNewOpen] = useState(false);

  const past = useRef<CgDoc[]>([]);
  const future = useRef<CgDoc[]>([]);
  const [histVersion, setHistVersion] = useState(0);
  const docRef = useRef(doc);
  docRef.current = doc;

  /** 主选框 = 最后点的那个（数组顺序就是点选顺序） */
  const selectedId = selectedIds.length > 0 ? selectedIds[selectedIds.length - 1] : null;
  const selected = selectedId ? findNode(doc, selectedId) : null;

  /** 替换整个选择集（= 老的单选语义，全部老调用点都落这里） */
  const setSelectedId = useCallback((id: string | null) => setSelectedIds(id ? [id] : []), []);
  /** 加选 / 减选（Shift / Ctrl + 点击） */
  const toggleSelect = useCallback((id: string) => {
    setSelectedIds((cur) => (cur.includes(id) ? cur.filter((x) => x !== id) : [...cur, id]));
  }, []);
  /** 点选：普通点击替换整个选择集，带 Shift/Ctrl 加选；点空白只在不加选时清空 */
  const selectNode = useCallback(
    (id: string | null, additive = false) => {
      if (id === null) {
        if (!additive) setSelectedIds([]);
        return;
      }
      if (additive) toggleSelect(id);
      else setSelectedIds([id]);
    },
    [toggleSelect],
  );
  /** 框选：一次性给一串 id（Canvas 已经归一化过，这里不再去重） */
  const selectMany = useCallback((ids: string[]) => setSelectedIds(ids), []);

  /* 撤销 / 删除 / 换文件之后，选择集里会留着已经不存在的 id —— 清掉，
     否则「选中 N 个」的数对不上，Inspector 还会拿着空节点空转。
     数组没变就原样返回：拖动时 onApply 每帧都换一份 doc，不这样写会白白重渲染。 */
  useEffect(() => {
    setSelectedIds((cur) => {
      if (cur.length === 0) return cur;
      /* 一次遍历收齐全部 id，别对每个选中 id 各走一遍树：拖动时这里是每帧都跑的，
         全选一个上百控件的文档会变成每帧上百次树遍历。 */
      const alive = new Set<string>();
      walk(doc.widgets, (n) => alive.add(n.id));
      const next = cur.filter((id) => alive.has(id));
      return next.length === cur.length ? cur : next;
    });
  }, [doc]);

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

  /** 批量删除：一次操作 = 一步撤销。层级面板的 × 仍传单个 id，所以留 deleteNode 这层壳。 */
  const deleteNodes = useCallback(
    (ids: string[]) => {
      const targets = topLevelOnly(docRef.current, ids);
      if (targets.length === 0) return;
      edit((d) => {
        for (const id of targets) removeNode(d, id);
      });
      setSelectedIds([]);
    },
    [edit],
  );

  const deleteNode = useCallback((id: string) => deleteNodes([id]), [deleteNodes]);

  /** 批量复制：走 model/parse 的 cloneNode + uniquifyIds —— 它们会把**子控件的 id 也重排**。
   *  （老实现只给顶层节点改名，复制一个分组框会造出一堆重复 id。） */
  const duplicateSelected = useCallback(() => {
    const targets = topLevelOnly(docRef.current, selectedIds);
    if (targets.length === 0) return;
    const newIds: string[] = [];
    edit((d) => {
      const taken = new Set<string>();
      walk(d.widgets, (n) => taken.add(n.id));
      for (const id of targets) {
        const node = findNode(d, id);
        if (!node) continue;
        const copy = cloneNode(node);
        uniquifyIds(copy, taken);
        const parent = findParent(d, id);
        if (parent) {
          parent.children = parent.children ?? [];
          parent.children.push(copy);
        } else {
          d.widgets.push(copy);
        }
        newIds.push(copy.id);
      }
    });
    setSelectedIds(newIds);
  }, [edit, selectedIds]);

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
      /* 「另存为」放最前面处理：它是逃生门，**全新文档**（没导出过、也没保存过）也必须能选
       * 位置存下来 —— 原来 !target 判断在前，那种情况下「另存为」是死的（只打印一句提示）。 */
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
        setExternal(null); /* 自己刚写的那一份就是最新真相，挂起的"外部改动"已经过时 */
        say(`已保存 ${res.path ?? ""}`.trim());
        return { canceled: false as const, path: res.path };
      }
      if (!target) {
        say("先点「导出 C 骨架」：保存位置跟随工程目录，不用自己选");
        return { canceled: true as const };
      }
      const res = await bridge.saveFile({
        filePath: target,
        text: serializeDoc(docRef.current),
        suggestName: suggestedBaseName(docRef.current.app.title),
      });
      if (res.canceled) return { canceled: true as const };
      setFilePath(res.path ?? target);
      setDirty(false);
      setExternal(null); /* 同上：这次保存已经把磁盘写成了画布的样子 */
      say(`已保存 ${res.path ?? target}`);
      return { canceled: false as const, path: res.path ?? target };
    },
    [bridge, filePath, lastExport, say],
  );

  /** 导出 C 骨架。返回导出到哪（发布要拿这个结果去编译），取消返回 null。
   *  工程名优先取**当前文件名**：.cgui 的文件名决定 exe 名与工程目录名
   *  （运行时默认就在 exe 旁边找同名 .cgui）。用窗口标题推会出事 ——
   *  "串口助手（cg_serial 示例）"会推成 cg_serial，导出的目录就与工程目录对不上，
   *  后面编译的还是旧目录，等于什么都没更新。 */
  const doExportC = useCallback(async (): Promise<{ dir: string; appName: string } | null> => {
    const fromFile = filePath ? (filePath.split(/[\\/]/).pop() ?? "").replace(/\.cgui$/i, "") : "";
    const baseName = suggestedBaseName(fromFile || docRef.current.app.title);
    const files = generateC(docRef.current, baseName);
    /* 带上当前文件路径：主进程好从文件位置向上认出真正的项目根（便携版画布尤甚） */
    const res = await bridge.exportC({ baseName, files, filePath: filePath || undefined });
    if (res.canceled) return null;
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
    return { dir: res.dir ?? "", appName: baseName };
  }, [bridge, docRef, say, filePath]);

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
    let target = filePath;
    if (dirty || !filePath) {
      const saved = await doSave(!filePath);
      if (saved.canceled) {
        say("先保存才能运行");
        setRunOpen(false);
        return;
      }
      /* 要用**这次保存返回的路径**：闭包里的 filePath 还是旧的（新建文档时甚至是空），
       * 拿它去 buildAndRun，主进程会驳回"先把 .cgui 保存到磁盘再运行"。 */
      if (saved.path) target = saved.path;
    }

    setRunning(true);
    try {
      const res = await bridge.buildAndRun({ filePath: target });
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

    /* 发布前先落盘：导出要按"当前文件名"推工程名与工程目录，路径得先定下来 */
    if (dirty || !filePath) {
      const saved = await doSave(!filePath);
      if (saved.canceled) {
        say("先保存才能发布");
        setRunOpen(false);
        return;
      }
    }

    /* 发布必须**重新导出**一次 C：界面的正文是要编译进 exe 的
     * （<名字>_cgui.c）。不重导出，打进去的就是上次导出时的旧界面 ——
     * 这种"exe 里的界面跟画布上看到的不一样"最难查。
     * 导出会覆盖 main.c / <名字>_cgui.c，跳过用户写过的 _ui_events.c。 */
    const exp = await doExportC();
    if (!exp) {
      say("发布前需要先导出 C 骨架");
      setRunOpen(false);
      return;
    }

    setPublishing(true);
    try {
      /* 必须用**导出返回的目录与工程名**去发布：导出可能落在别的目录
       * （或用户这次选了别的目录），拿事先算好的旧路径去编译会编到旧工程上，
       * 表现就是"改了界面却没生效、内嵌界面也没进去"。 */
      const res = await bridge.publish({ filePath: `${exp.dir}\\${exp.appName}.cgui` });
      if (res.ok) say(res.message);
      else setRunLines((ls) => [...ls, "", `# ${res.message}`]);
    } finally {
      setPublishing(false);
    }
  }, [bridge, dirty, doSave, doExportC, filePath, say]);

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
      else if (action === "actions") setActionsOpen(true);
      else if (action === "styles") setStylesOpen(true);
      else if (action === "undo") undo();
      else if (action === "redo") redo();
      else if (action === "duplicate") duplicateSelected();
      else if (action === "delete" && selectedIds.length > 0) deleteNodes(selectedIds);
      else if (action === "zoomIn") setZoom((z) => Math.min(3, Math.round((z + 0.1) * 10) / 10));
      else if (action === "zoomOut") setZoom((z) => Math.max(0.2, Math.round((z - 0.1) * 10) / 10));
      else if (action === "zoomFit") setZoom(1);
    });
  }, [bridge, doNew, doOpen, doSave, doExportC, doRun, undo, redo, duplicateSelected, deleteNodes, selectedIds, infoOpen]);

  /* 编译/运行输出流：注册一次即可，只碰 setState */
  useEffect(() => {
    bridge.onRunOutput((o) => {
      if (o.kind === "line") setRunLines((ls) => [...ls.slice(-300), o.text ?? ""]);
      else if (o.kind === "done") setRunLines((ls) => [...ls, "", `# 已启动 ${o.text ?? ""}`]);
      else if (o.kind === "fail") setRunLines((ls) => [...ls, "", `# 编译失败（退出码 ${o.code ?? "?"}）`]);
    });
  }, [bridge]);

  /** 用磁盘上的版本替换画布内容。两个入口共用这段：外部改动（画布干净时）与
   *  通知条上的「载入外部版本」。撤销栈必须清掉 —— 那份历史属于另一条血脉，
   *  留着它 Ctrl+Z 会回到「外部改动之前」，是最难解释的一种状态。 */
  const applyExternal = useCallback(
    (next: CgDoc) => {
      past.current = [];
      future.current = [];
      setDoc(next);
      setDirty(false);
      setSelectedIds([]);
      setExternal(null);
      setHistVersion((v) => v + 1);
      say("已载入外部改动（撤销历史已重置）");
    },
    [say],
  );

  /* 外部程序改了当前 .cgui（编辑器 / 发布包里那套 AI 技能 / git checkout）：画布自己跟上。
     有未保存改动时**只挂起、不覆盖** —— 自动覆盖等于替使用者把他正在改的东西丢掉。 */
  useEffect(() => {
    bridge.onFileChanged((o) => {
      const parsed = parseDoc(o.text);
      if (!parsed.doc) {
        say(".cgui 在外部被改过，但内容读不懂，已忽略");
        return;
      }
      if (dirty) {
        setExternal(parsed.doc);
        return;
      }
      applyExternal(parsed.doc);
    });
  }, [bridge, dirty, say, applyExternal]);

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
      if (mod && e.key.toLowerCase() === "a") {
        e.preventDefault();
        /* 全选**顶层**控件：把子控件也一起选上的话，框线糊成一片、计数也和实际对不上 */
        setSelectedIds(docRef.current.widgets.map((n) => n.id));
        return;
      }
      if (mod && e.key.toLowerCase() === "d") {
        e.preventDefault();
        duplicateSelected();
        return;
      }
      if (e.key === "Delete" || e.key === "Backspace") {
        if (selectedIds.length > 0) {
          e.preventDefault();
          deleteNodes(selectedIds);
        }
        return;
      }
      if (e.key === "Escape") {
        setSelectedIds([]);
        return;
      }
      if (e.key.startsWith("Arrow") && selectedIds.length > 0) {
        e.preventDefault();
        const step = e.shiftKey ? 8 : 1;
        const dx = e.key === "ArrowLeft" ? -step : e.key === "ArrowRight" ? step : 0;
        const dy = e.key === "ArrowUp" ? -step : e.key === "ArrowDown" ? step : 0;
        edit((d) => {
          moveNodes(d, selectedIds, dx, dy);
        });
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [doSave, doOpen, doExportC, doRun, undo, redo, duplicateSelected, deleteNodes, selectedIds, edit]);

  /* -------------------------------------------------- 标题 & 项目路径 */
  useEffect(() => {
    bridge.setTitle(`${filePath ? filePath.split(/[\\/]/).pop() : doc.app.title || "main"}${dirty ? " *" : ""}`);
  }, [bridge, filePath, dirty, doc.app.title]);

  /* 图片预览的相对路径以当前 .cgui 所在目录为基准（和运行时 `cgw_resolve_asset` 同一个基准）：
     换文件时同步给缓存 —— 缓存键里带它，于是同名相对路径会重新读，不会串成上一个工程的图。 */
  useEffect(() => {
    setImageBase(filePath);
  }, [filePath]);

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
    const actionNames = Object.keys(doc.actions ?? {});
    const styleNames = Object.keys(doc.styles ?? {});
    return { count, containers, callbacks: callbacks.length, uses, actionNames, styleNames };
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
        onOpenActions={() => setActionsOpen(true)}
        onOpenStyles={() => setStylesOpen(true)}
        onZoom={(z) => setZoom(clamp(z, 0.2, 3))}
        onToggleGrid={() => setShowGrid((v) => !v)}
        onToggleSnap={() => setSnapOn((v) => !v)}
        onUndo={undo}
        onRedo={redo}
        projectRoot={projectRoot}
        onLocateProject={() => void doLocateProject()}
      />

      <div className="body">
        <div className="left">
          <Palette onAdd={addNode} onStartDrop={(t) => setPendingDrop(t)} />
          <LayerTree
            doc={doc}
            selectedIds={selectedIds}
            onSelect={selectNode}
            onBeginHistory={onBeginHistory}
            onApply={onApply}
            onRemove={deleteNode}
          />
        </div>

        <Canvas
          doc={doc}
          selectedIds={selectedIds}
          zoom={zoom}
          snapOn={snapOn}
          showGrid={showGrid}
          onSelect={selectNode}
          onSelectMany={selectMany}
          onBeginHistory={onBeginHistory}
          onApply={onApply}
          pendingDrop={pendingDrop}
          onDropDone={() => setPendingDrop(null)}
          externalPending={external !== null}
          onLoadExternal={() => {
            if (external) applyExternal(external);
          }}
          onDismissExternal={() => setExternal(null)}
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
                    动作 {stats.actionNames.length} 个
                    {stats.actionNames.length ? `（${stats.actionNames.join("、")}）` : ""}
                    <span className="muted small">
                      {stats.actionNames.length
                        ? " —— 菜单项/控件用 action 引用它们，一处禁用就一起灰"
                        : " —— 工具条「动作」按钮里可以新建"}
                    </span>
                  </li>
                  <li>
                    命名样式 {stats.styleNames.length} 个
                    {stats.styleNames.length ? `（${stats.styleNames.join("、")}）` : ""}
                    <span className="muted small">
                      {stats.styleNames.length
                        ? " —— 控件用 style 引用它们，改一处全变"
                        : " —— 工具条「样式」按钮里可以新建"}
                    </span>
                  </li>
                  <li>
                    窗口 {doc.app.width}×{doc.app.height}，底色 {doc.app.bg}
                    {doc.app.dark === true ? "，深色" : doc.app.dark === "auto" ? "，跟随系统主题" : ""}
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
                          /* 只增删 close 这一个键。早先清空时写的是 `delete d.app.events`，
                           * 会把「事件」页签里绑好的 ready/size/… 全部静默清掉（数据丢失）。 */
                          const ev = { ...(d.app.events ?? {}) };
                          if (v) ev.close = v;
                          else delete ev.close;
                          d.app.events = Object.keys(ev).length > 0 ? ev : undefined;
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
        /* 有通知条时把 toast 抬起来：它是 position:fixed 贴底的，否则正好压住通知条上的按钮 */
        <div className={external ? "toast toast-raised" : "toast"} onClick={() => setToast("")}>
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
      <ActionsDialog
        open={actionsOpen}
        doc={doc}
        onClose={() => setActionsOpen(false)}
        onBeginHistory={onBeginHistory}
        onApply={onApply}
      />
      <StyleDialog
        open={stylesOpen}
        doc={doc}
        onClose={() => setStylesOpen(false)}
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
