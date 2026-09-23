/* preload.cjs -- 只把需要的几个能力暴露给渲染进程（contextIsolation 打开） */
const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("cstackgui", {
  info: () => ipcRenderer.invoke("app:info"),
  initialFile: () => ipcRenderer.invoke("app:initialFile"),
  locateProject: () => ipcRenderer.invoke("app:locateProject"),
  openFile: () => ipcRenderer.invoke("cgui:open"),
  saveFile: (payload) => ipcRenderer.invoke("cgui:save", payload),
  pickIcon: () => ipcRenderer.invoke("app:pickIcon"),
  pickImage: () => ipcRenderer.invoke("cgui:pickImage"),
  exportC: (payload) => ipcRenderer.invoke("cgui:exportC", payload),
  buildAndRun: (payload) => ipcRenderer.invoke("run:buildAndLaunch", payload),
  publish: (payload) => ipcRenderer.invoke("publish:project", payload),
  openCallback: (payload) => ipcRenderer.invoke("cgui:openCallback", payload),
  reveal: (target) => ipcRenderer.invoke("app:reveal", target),
  setTitle: (title) => ipcRenderer.invoke("app:setTitle", title),
  confirmDiscard: (message) => ipcRenderer.invoke("app:confirmDiscard", message),

  /** 编译/运行输出流（主进程 spawn 的批处理输出）。
   *  渲染端 effect 会随依赖变化反复注册，先清旧监听再挂新的，
   *  否则监听器堆积、输出行成倍重复。 */
  onRunOutput: (handler) => {
    ipcRenderer.removeAllListeners("run:output");
    ipcRenderer.on("run:output", (_e, o) => handler(o));
  },

  /** 主进程菜单 -> 渲染进程（同上，注册前先清旧监听） */
  onMenu: (handler) => {
    const channels = [
      "menu:new",
      "menu:open",
      "menu:save",
      "menu:saveAs",
      "menu:exportC",
      "menu:run",
      "menu:undo",
      "menu:redo",
      "menu:duplicate",
      "menu:delete",
      "menu:zoomIn",
      "menu:zoomOut",
      "menu:zoomFit",
      /* 注意：main.mjs 的菜单里每多一个 send("menu:xxx")，这里必须同步加一行 ——
       * 漏了不会报错，只是那个菜单项静默失效（「程序配置」就这么漏过，2026-09-23 抓到）。
       * 结构性的修法（通道名单集中成一份常量、preload 与 menu 共用）记在
       * .codebuddy/plans/refactor-structure.md §5.1。 */
      "menu:programConfig",
    ];
    for (const ch of channels) {
      ipcRenderer.removeAllListeners(ch);
      ipcRenderer.on(ch, () => handler(ch.replace("menu:", "")));
    }
  },
});
