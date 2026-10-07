/* bridge.ts -- 把 Electron 的能力包一层，浏览器里跑也有降级方案
 *
 * 用 Electron 跑（正常交付形态）：真正读写磁盘、导出到 apps/ 目录。
 * 直接用浏览器打开 dist/index.html 时：退化成「选文件 / 下载文件」，
 * 方便在没有 Electron 的机器上看画布。
 */
export type SavePayload = { filePath?: string; text: string; suggestName?: string };
/** 骨架语言：C（main.c）还是 C++（main.cpp）。首次导出才有得选 —— 工程里已有骨架时按磁盘现状走。 */
export type GenLang = "c" | "cpp";
export type ExportPayload = {
  baseName: string;
  files: { name: string; content: string }[];
  filePath?: string;
  /** 这次按哪种语言生成（省得主进程再去猜 —— files 的文件名已经决定了） */
  lang?: GenLang;
  /** 目标目录：由 `exportPlan` 先问出来的，带上它就不再弹一次"导到哪" */
  targetDir?: string;
};
/** 导出落点探测（真正写盘之前）：目录在哪、工程里现在用的是哪种语言 */
export type ExportPlan = {
  canceled: boolean;
  dir?: string;
  /** "" = 这个目录里还没有骨架（首次导出，该问用户选语言了）；否则是磁盘现状 */
  lang?: GenLang | "";
};
export type ExportResult = {
  canceled: boolean;
  dir?: string;
  written?: string[];
  skipped?: string[];
  projectRoot?: string;
  appName?: string;
  /** 这次实际用的语言（可能由磁盘现状决定而非入参）—— 渲染端拿它拼文件名做提示 */
  lang?: GenLang;
  /** 这次导出为「新绑的事件」自动补上的空函数名。
   *  `<工程>_ui_events.c` 是"只补不改"的：缺的回调补桩、已有的原样留着（见 merge-events.mjs）。 */
  addedCallbacks?: string[];
  /** 画布上**已不再绑定**、而 .c 里还留着的回调：**不会自动删**（里面可能有逻辑），只提醒一句。
   *  判据是"老 .h 声明过、新 .h 里没有了"（不能拿 .c 的函数名比 —— 手写辅助函数会被误算）。 */
  unusedCallbacks?: string[];
  /** 工程里的 `main.c` 与当前画布样板已不同步（画布**不会**覆盖它，只提醒）。 */
  staleMainC?: boolean;
};

/** 一键运行的事件流：line = 编译输出行，done = 成功启动，fail = 失败 */
export type RunOutput = { kind: "line" | "done" | "fail"; text?: string; code?: number };
export type RunResult = { ok: boolean; message: string; exePath?: string };

export type Bridge = {
  isDesktop: boolean;
  projectRoot: string;
  info: () => Promise<{ version: string; projectRoot: string; currentFile: string }>;
  /** 命令行指定要打开的 .cgui（CStackGUI.exe xxx.cgui），没有就返回 canceled */
  initialFile: () => Promise<{ canceled: boolean; path?: string; text?: string }>;
  /** 让用户指定 CStackGUI 项目根（含 runtime\ 和 apps\ 的那一层） */
  locateProject: () => Promise<{ canceled: boolean; projectRoot?: string }>;
  openFile: () => Promise<{ canceled: boolean; path?: string; text?: string }>;
  saveFile: (p: SavePayload) => Promise<{ canceled: boolean; path?: string }>;
  /** 选一个 .ico 当窗口图标；rel 是相对 .cgui 目录的路径（拿不到就给绝对 path） */
  pickIcon: () => Promise<{ canceled: boolean; path?: string; rel?: string }>;
  /** 选一张图片（png/jpg/bmp/gif）当按钮背景等；返回值口径同 pickIcon */
  pickImage: () => Promise<{ canceled: boolean; path?: string; rel?: string }>;
  /** 读一张图给画布预览（`button` / `iconbutton` / `image` 的 src、窗口图标）。
   *  路径由主进程按当前 `.cgui` 目录解析（与运行时同一个基准）；返回 dataURL 供 `<img>` 直接用。 */
  readImage: (p: { src: string }) => Promise<{ ok: boolean; path?: string; dataUrl?: string; tooBig?: boolean }>;
  /** 导出前先问一句"导到哪 + 这个工程现在用的是哪种语言"（首次导出据此问用户选语言） */
  exportPlan: (p: { baseName: string; filePath?: string }) => Promise<ExportPlan>;
  exportC: (p: ExportPayload) => Promise<ExportResult>;
  /** F5 一键运行：调项目根的 build-app.bat 编译，成功后把 exe 跑起来 */
  buildAndRun: (p: { filePath: string }) => Promise<RunResult>;
  publish: (p: { filePath: string }) => Promise<RunResult>;
  /** 打开某个回调所在的 C 函数（装了 VS Code 会直接跳到那一行） */
  openCallback: (p: { filePath?: string; callback?: string }) => Promise<{ ok: boolean; message: string }>;
  reveal: (target: string) => void;
  setTitle: (title: string) => void;
  confirmDiscard: (message?: string) => Promise<boolean>;
  /** 复制文本到系统剪贴板（QQ 群号等） */
  copyText: (text: string) => Promise<boolean>;
  onMenu: (handler: (action: string) => void) => void;
  onRunOutput: (handler: (o: RunOutput) => void) => void;
  /** 外部把当前 .cgui 改了（编辑器 / AI 技能 / git checkout）：画布据此热重载 */
  onFileChanged: (handler: (o: { path: string; text: string }) => void) => void;
};

type WinWithBridge = Window & {
  cstackgui?: Omit<Bridge, "isDesktop" | "projectRoot"> & { onMenu: (h: (a: string) => void) => void };
};

function pickBrowserFile(accept: string): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = accept;
    input.onchange = () => resolve(input.files?.[0] ?? null);
    input.oncancel = () => resolve(null);
    input.click();
  });
}

function download(name: string, content: string) {
  const url = URL.createObjectURL(new Blob([content], { type: "text/plain;charset=utf-8" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

let cached: Bridge | null = null;

export function getBridge(): Bridge {
  if (cached) return cached;

  const w = window as WinWithBridge;
  if (w.cstackgui) {
    const api = w.cstackgui;
    cached = {
      isDesktop: true,
      projectRoot: "",
      info: () => api.info(),
      initialFile: () => api.initialFile(),
      locateProject: () => api.locateProject(),
      openFile: () => api.openFile(),
      saveFile: (p) => api.saveFile(p),
      pickIcon: () => api.pickIcon(),
      pickImage: () => api.pickImage(),
      readImage: (p) => api.readImage(p),
      exportPlan: (p) => api.exportPlan(p),
      exportC: (p) => api.exportC(p),
      buildAndRun: (p) => api.buildAndRun(p),
      publish: (p) => api.publish(p),
      openCallback: (p) => api.openCallback(p),
      reveal: (t) => void api.reveal(t),
      setTitle: (t) => void api.setTitle(t),
      confirmDiscard: (m) => api.confirmDiscard(m),
      copyText: (t) => api.copyText(t),
      onMenu: (h) => api.onMenu(h),
      onRunOutput: (h) => api.onRunOutput(h),
      onFileChanged: (h) => api.onFileChanged(h),
    };
    return cached;
  }

  cached = {
    isDesktop: false,
    projectRoot: "",
    info: async () => ({ version: "0.1.0", projectRoot: "", currentFile: "" }),
    initialFile: async () => ({ canceled: true }),
    locateProject: async () => ({ canceled: true }),   /* 浏览器里没法访问磁盘目录 */
    openFile: async () => {
      const file = await pickBrowserFile(".cgui,application/json");
      if (!file) return { canceled: true };
      return { canceled: false, text: await file.text(), path: file.name };
    },
    saveFile: async ({ text, suggestName }) => {
      const name = `${suggestName || "untitled"}.cgui`;
      download(name, text);
      return { canceled: false, path: name };
    },
    pickIcon: async () => {
      /* 浏览器拿不到真实磁盘路径，只能给个文件名（仅供预览，运行时用不到） */
      const file = await pickBrowserFile(".ico");
      return file ? { canceled: false, path: file.name } : { canceled: true };
    },
    pickImage: async () => {
      const file = await pickBrowserFile("image/png,image/jpeg,image/bmp,image/gif");
      return file ? { canceled: false, path: file.name } : { canceled: true };
    },
    /* 浏览器模式读不到磁盘上的图（只有文件名，没有目录基准）：一律"读不到"，
       画布上会显示"保存后才能预览"那类提示，不会崩。 */
    readImage: async () => ({ ok: false }),
    /* 浏览器模式读不到磁盘，自然认不出"工程里已经导出过什么"：一律当作首次导出，
       渲染端会照常弹一次语言选择（选了才决定下载哪些文件）。 */
    exportPlan: async () => ({ canceled: false, lang: "" }),
    exportC: async ({ baseName, files }) => {
      files.forEach((f, i) => setTimeout(() => download(f.name, f.content), i * 250));
      return { canceled: false, dir: `（浏览器模式：${baseName} 的 ${files.length} 个文件已逐个下载）`, written: files.map((f) => f.name) };
    },
    buildAndRun: async () => ({
      ok: false,
      message: "浏览器模式没法编译运行，请用打包好的 CStackGUI.exe（或 npm run dev）。",
    }),
    publish: async () => ({
      ok: false,
      message: "浏览器模式没法编译发布，请用打包好的 CStackGUI.exe（或 npm run dev）。",
    }),
    openCallback: async () => ({
      ok: false,
      message: "浏览器模式打不开本地编辑器，请用打包好的 CStackGUI.exe（或 npm run dev）。",
    }),
    reveal: () => undefined,
    setTitle: (t) => {
      document.title = t ? `CStackGUI — ${t}` : "CStackGUI";
    },
    confirmDiscard: async (m) => window.confirm(m || "当前改动还没保存，确定要丢弃吗？"),
    copyText: async (text) => {
      try {
        await navigator.clipboard.writeText(text);
        return true;
      } catch {
        /* 老内核 / 非安全上下文没有 clipboard API：textarea + execCommand 兜底 */
        const ta = document.createElement("textarea");
        ta.value = text;
        ta.style.position = "fixed";
        ta.style.opacity = "0";
        document.body.appendChild(ta);
        ta.select();
        const ok = document.execCommand("copy");
        ta.remove();
        return ok;
      }
    },
    onMenu: () => undefined,
    onRunOutput: () => undefined,
    /* 浏览器模式没有"外部改文件"这回事（文件是下载出去的），空实现即可 */
    onFileChanged: () => undefined,
  };
  return cached;
}
