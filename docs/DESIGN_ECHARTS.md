# 设计文档:画布新增 ECharts 图表控件

> 状态:设计阶段(待用户确认渲染后端方案)
> 作者:AI
> 日期:2026-09-21

## 1. 背景与目标

当前画布有 39 种原生 Win32 控件,但没有图表控件。用户希望在画布里拖出一个 **ECharts 图表组件**,既能设计期预览真实图表,也能在编译出的 exe 里显示真实图表。

ECharts 是 Apache-2.0 的 **JavaScript** 可视化库,**必须在 HTML/DOM 环境里运行**(核心 API `echarts.init(dom)` + `setOption(option)`)。因此原生 GDI 控件无法直接渲染它,运行时必须内嵌一个浏览器内核来宿主 ECharts。

## 2. 渲染后端决策(待确认)

| 方案 | 可行性 | 代价 |
| --- | --- | --- |
| **A. WebView2(Edge Chromium)** | ✅ 推荐。现代内核,ECharts 5 完美支持;中文/字体/性能都好 | exe 运行需系统装有 WebView2 Runtime(Win10/11 基本自带,缺失时可随包附带 `WebView2Loader.dll` + 引导安装)。**打破"单文件零依赖"原则**——但图表类控件本质就是 Web 技术,属合理例外 |
| B. 复用现有 `browser` 的 IE 内核 | ❌ 不推荐。ECharts 5 已弃用 IE,需降级到极老的 ECharts 3/4 且加 polyfill,中文/字体/体验差 | 几乎不可行 |
| C. 仅画布预览,不进运行时 | ⚠️ 设计期可见真实图表,但编译出的 exe 里该控件只显示占位/降级 | 违背"画布描述真实 UI"的核心定位 |

**结论倾向 A(WebView2)**:它是唯一能出真实图表的方案。代价是把"零依赖"原则在图表控件上放宽为"依赖系统 WebView2 Runtime"。

## 3. 新增控件设计

- **控件类型名**:`echarts`(enum `CG_K_ECHARTS`,插在 `cg_doc.h` 的 `CG_K_SHAPE` 之后、`CG_K_WINDOW` 之前)。
- **`.cgui` 节点新增字段**:
  - `option`(object/JSON):ECharts 配置(`title/tooltip/legend/xAxis/yAxis/series`...),核心字段。
  - `theme`(string,可选):主题名,如 `"dark"`。
  - `data`(可选):预留运行时数据绑定入口(JSON)。
- **事件**:MVP 暂不引入新事件(复用现有 `click` 等不需要);后续可加 `chartclick`(需四处登记:`CG_EVENTS`/`k_ev_names`/`KIND_EVENTS`/`k_kinds` 掩码)。

## 4. 两端改动清单(复用 `browser` 控件链路)

### 画布端(designer,React/Electron —— 这里 ECharts 原生可跑)
1. `model.ts`:`KINDS` 加一行(`type:"echarts"`,group 可放"其它"或新增"图表")→ Palette 自动出现,但需补 `styles.css` 的 `.glyph-echarts`;可选 `KIND_EVENTS` 项;`makeNode` 种子;`CgNode` 加 `option?` 字段;`coerceNode`/`cleanNode` 读写 `option`。
2. `components/WidgetView.tsx`:`inner()` 的 `switch` 加 `case "echarts"`,**直接用真实 `echarts` 包渲染到 `<div>`**(设计期就是真图表,WYSIWYG),读 `node.option`。
3. `components/Inspector.tsx`:加 `option` 的 JSON 多行编辑行(仿 browser 的"地址"编辑器)。
4. `styles.css`:`.glyph-echarts` 图标 + 预览样式。
5. `supportlib.ts`:仅当运行时新增 `cg_*` 函数时才加一行。

### 运行时端(runtime,C/Win32 —— 内嵌 WebView2)
6. `cg_doc.h`:`cg_kind_t` 加 `CG_K_ECHARTS`。
7. `cg_doc_kind.c`:`k_kinds[]` 加一行(事件掩码)。
8. `cg_win_ctl.c`:`cgw_class_of`/`cgw_style_of` 各加 `echarts` 分支,返回承载 WebView2 的宿主窗口类。
9. `cg_win_window.c`:初始化期 `LoadLibrary` + 创建 `ICoreWebView2Environment`(仿 `browser` 的 `AtlAxWinInit`)。
10. `cg_win_create.c`:创建时按类型特殊处理(仿 browser 的 `AtlAxWin` 创建)。
11. `cg_win_sync.c`:`option` 变化时经 `ExecuteScript` 把 `setOption` 注入;resize 时 `chart.resize()`(仿 `browser_recreate`)。
12. `cg_win_api.c`(可选):暴露 `cg_echarts_set_option(self, json)` 运行时 API,在 `cstackgui.h` 声明;经 `PostWebMessageAsJson` 通道下发。

### 校验
13. `tools/check-support.mjs` 自动比对 `supportlib.ts`↔`cstackgui.h`(仅当 #5/#12 加 API 时相关);`k_kinds↔KINDS` 靠人工同步 + 运行时 `unknown widget type` 报错兜底。

## 5. 数据流

```
.cgui 里 node.option(JSON)
   ├─ 设计期: designer 用真实 echarts 包 → <div> 渲染(真图表)
   └─ 运行期: WebView2 宿主加载本地 echarts.min.js + 注入 option → chart.setOption → 真图表
窗口 resize → chart.resize()  (运行期经 ExecuteScript)
运行时改数据 → cg_echarts_set_option() → PostWebMessageAsJson → 页面里 setOption
```

## 6. 最小可跑闭环(MVP,分阶段)

- **阶段 0(设计文档)**:本文。
- **阶段 1(画布可设计)**:加 `echarts` 控件类型、Palette 出现、Inspector 编辑 `option`、WidgetView 用真实 echarts 渲染预览。`.cgui` 能存 `option`。
- **阶段 2(运行期出图)**:WebView2 宿主 + 注入本地 echarts + `setOption(option)`;resize 自适应。出 `examples/echarts_demo`(柱状图)。
- **阶段 3(运行时可更新)**:`cg_echarts_set_option()` API + 消息通道,支持运行时换数据。

## 7. 主要风险

- **WebView2 C SDK 在 MinGW 下的集成**(COM、`ICoreWebView2Environment`/`ICoreWebView2Controller`)是最大工作量;需把 `WebView2.h`/`WebView2Loader.dll` 纳入构建(走 `CSTACKGUI_HOME` 或 vendored)。
- 本地 echarts 资源如何随 exe 分发:可内嵌为资源(res),WebView2 用 `data:` URI 或 `SetVirtualHostNameToFolderMapping` 加载。
- 无 WebView2 Runtime 时的降级:显示"需要 WebView2"占位,不崩。

## 8. 未决

- 渲染后端方案(A/B/C)需用户拍板(见对话)。
- 是否本期就引入 `chartclick` 等图表事件。
- echarts 资源随包方式(res 内嵌 vs 外部文件)。
