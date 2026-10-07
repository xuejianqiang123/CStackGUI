/* bridge.ts -- 把 Electron 的能力包一层，浏览器里跑也有降级方案
 *
 * 用 Electron 跑（正常交付形态）：真正读写磁盘、导出到 apps/ 目录。
 * 直接用浏览器打开 dist/index.html 时：退化成「选文件 / 下载文件」，
 * 方便在没有 Electron 的机器上看画布。
 */
export type SavePayload = { filePath?: string; text: string; suggestName?: string };
export type ExportPayload = { baseName: string; files: { name: string; content: string }[]; filePath?: string };
export type ExportResult = {
  canceled: boolean;
  dir?: string;
  written?: string[];
  skipped?: string[];
  projectRoot?: string;
  appName?: string;
  /** 这次导出为「新绑的事件」自动补上的空函数名。
   *  `<工程>_ui_events.c` 是"只补不改"的：缺的回调补桩、已有的原样留着（见 merge-events.mjs）。 */
  addedCallbacks?: string[];
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
  exportC: (p: ExportPayload) => Promise<ExportResult>;
  /** F5 一键运行：调项目根的 build-app.bat 编译，成功后把 exe 跑起来 */
  buildAndRun: (p: { filePath: string }) => Promise<RunResult>;
  publish: (p: { filePath: string }) => Promise<RunResult>;
  /** 打开某个回调所在的 C 函数（装了 VS Code 会直接跳到那一行） */
  openCallback: (p: { filePath?: string; callback?: string }) => Promise<{ ok: boolean; message: string }>;
  reveal: (target: string) => void;
  setTitle: (title: string) => void;
  confirmDiscard: (message?: string) => Promise<boolean>;
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
      exportC: (p) => api.exportC(p),
      buildAndRun: (p) => api.buildAndRun(p),
      publish: (p) => api.publish(p),
      openCallback: (p) => api.openCallback(p),
      reveal: (t) => void api.reveal(t),
      setTitle: (t) => void api.setTitle(t),
      confirmDiscard: (m) => api.confirmDiscard(m),
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
    onMenu: () => undefined,
    onRunOutput: () => undefined,
    /* 浏览器模式没有"外部改文件"这回事（文件是下载出去的），空实现即可 */
    onFileChanged: () => undefined,
  };
  return cached;
}
