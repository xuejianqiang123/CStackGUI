---
name: designer-modify
description: 修改 CStackGUI 的 UI 设计器（designer/ —— Electron + React 19 + Vite + TS 的 .cgui 画布）。触发条件：改画布外观/面板/交互、给 .cgui 加字段、改事件绑定表、新增一种控件、改导出 C 骨架或 Electron IPC（打开/保存/导出/一键运行）时自动触发。先按「意图识别」把使用者的日常表述翻译成改动路线，再按清单执行。改 C 运行时（runtime/）不属于本技能，但两边共有约定见「跨端铁律」。
argument-hint: [要改的东西，可以用日常语言，例如 "画布选中框太丑"、"想给控件加个提示"、"像 XX 软件那样能多选"]
---

# 修改 UI 设计器（designer/）

**改动目标**：$ARGUMENTS

本技能把 `designer/` 的结构、`.cgui` 语法契约、双端同步铁律和验证入口固化下来，
让你改画布时不需要先通读全部源码，也不会把「画布能存、运行时读不了」的坑再踩一遍。

## 核心原则

1. **`runtime/src/core/cg_doc.c` 是 .cgui 的真相**：画布的 `model.ts` 必须跟着它走，不是反过来。
2. **控件类型表有两份**：`model.ts::KINDS`（+`KIND_EVENTS`）与 `runtime/src/core/cg_doc_kind.c::k_kinds`。加一个 = 各加一行 + 多处补分支（清单见下）。
3. **doc 是唯一真相**：所有编辑都是「structuredClone → 改 → 放回」，禁止在原对象上直接改。

## 文件地图（`designer/`）

| 文件 | 职责 | 改这里通常是因为… |
| --- | --- | --- |
| `src/model.ts` | **最重要**。CgNode/CgDoc 类型、`KINDS`、`KIND_EVENTS`、`CG_EVENTS`、结构化内容 ↔ 文本 DSL、`parseDoc`/`serializeDoc`（含 `coerceNode`/`cleanNode`）、树遍历工具 | 加字段、加控件、改文件格式 |
| `src/App.tsx` | 状态中枢：doc/selection/undo/zoom/toast/运行面板、菜单与快捷键、文件与导出动作 | 加功能、加快捷键、加面板 |
| `src/components/Canvas.tsx` | 画布：选中、拖动、8 向 resize、拖入/拖出容器、吸附、菜单栏渲染 | 改交互/坐标/容器归属逻辑 |
| `src/components/WidgetView.tsx` | 单个节点的**外观**（`inner()` 的 switch）；递归画子控件 | 新控件的外观、tabs 分页显示 |
| `src/components/Inspector.tsx` | 属性面板（按 `node.type` 显示字段；多行内容用纯文本 `Area` 编辑） | 加/改属性字段与编辑器 |
| `src/components/EventPanel.tsx` | 事件页签：只列 `eventsOf(type)` 支持的事件，自动命名 `on_<id>_<ev>`，标出「不支持」和「共用」；「代码」按钮走 `cgui:openCallback` 在编辑器里打开 `*_ui_events.c` 的对应函数（VS Code 用 `code -g` 跳行） | 事件相关改动 |
| `src/components/NewProjectDialog.tsx` | 「新建工程」模板选择（空白 / 表单 / 托盘小工具）；模板数据在 `model.ts::templateDoc`，只是普通 CgDoc | 加/改工程模板 |
| `src/components/Palette.tsx` / `LayerTree.tsx` / `Toolbar.tsx` | 控件面板（按 `KINDS[].group` 自动分组）/ 层级（数组顺序 = 层叠）/ 顶部工具条 | UI 排布、层级操作 |
| `src/components/SupportPanel.tsx` + `src/supportlib.ts` | 「支持库」页签：公共 C API 的可搜索中文索引（分类树 + 帮助卡片 + 双击复制调用代码）。数据是**手写**的，靠 `tools\check-support.mjs` 与 `cstackgui.h` 对齐 | 增改运行时公共 API、新手引导 |
| `src/codegen.ts` | 导出 C 骨架：`<name>_ui_events.h`、`.c`、`main.c`、`build.bat`、`.cgui` 五件套 | 改导出产物模板 |
| `src/bridge.ts` | Electron 能力封装 + **浏览器降级**（无磁盘：改成下载文件） | 加新 IPC 能力时同步两端 |
| `electron/main.mjs` | 主进程：IPC handler、项目根定位、导出目录规则、F5 编译运行 | 加 IPC / 改磁盘行为 |
| `electron/preload.cjs` | contextBridge 暴露白名单（新 IPC **必须**在此登记） | 加 IPC |
| `src/styles.css` | 全部样式（无 CSS-in-JS）。`:root` 是 M3 变量；控件外观类名 `.glyph-<type>`（图标）与 `.ctl-<type>`（控件本体） | 改视觉、新增控件补类名 |
| `scripts/dev.mjs` / `vite.config.ts` / `tsconfig.json` | 起电子 + vite（端口 5273）/ 构建目标 chrome120 / strict + noUnusedLocals | 一般不动 |

## `.cgui` 语法契约

顶层：`{ cgui:1, app:{ title,width,height,bg, events?:{close?:string} }, widgets: CgNode[] }`

`CgNode` 字段（`cleanNode` 决定什么会被写出去，写出去的和读进来的是同一套）：

- `id`（唯一，Inspector 里被强制成 `[0-9a-zA-Z_]`）、`type`、`rect{x,y,w,h}`（整数；容器内相对父控件）
- `text`、`style{font,size,bold,fg,bg,radius,align}`（`align` 非 `left` 才写）
- `events{<事件名>: <C 函数名>}`；窗口级 `close` 只存在于 `app.events`
- 状态（`cleanDoc` 里**等于默认值就不写**）：`visible`(默认 true)、`enabled`(默认 true)、`checked`(默认 false)；`value/min/max` 仅在 `KIND_MAP[type].ranged` 时写
- 内容：`items`（listbox/combo/tabs/statusbar/toolbar）、`nodes`（tree/menu/menubutton）、`columns[{title,w}]`+`rows[][]`（table）、`src`（image/iconbutton）、`format`（datepicker）、`page`（tabs 子控件归属第几页，>0 才写）
- `bind{src,two?}`：**控件绑定** —— 本控件的值/文字/勾选一直跟着 `src`（另一个控件的 id）走，`two:true` 为双向。
  抄什么由**目标控件类型**决定（值型抄值、勾选型抄勾选、其余显示源的数值或文字），
  同步**不触发**目标的 `change` 回调。画布「属性」里有下拉选源；改 id 时 Inspector 会连带改掉指向它的 `bind.src`。
  C 侧实现见 `runtime/src/core/cg_bind.c` + `docs/CGUI-FORMAT.md` 第 2.8 节。
- `children`（仅容器 `group`/`box`/`card`/`tabs`；非容器带 children 读入时被挂到同级，不丢）

读入（`parseDoc`/`coerceNode`）宽容：脏数据按默认值兜底、`type` 不认识降级成 `label`、兼容扁平 `x/y/w/h` 和 `state{}` 旧写法、`cgui > 1` 直接报错。
结构化内容的**文本 DSL**（Inspector 用多行 textarea 编辑，务必保持这套格式）：

| 类型 | 格式 |
| --- | --- |
| items | 每行一条 |
| tree | 两空格缩进一层 |
| menu | 两空格一层；`文本 \| id`；单独一行 `-` 是分隔线；第三字段 `v/✓/1/yes` 表示勾选 |
| table 列 | 逗号分隔，可写 `标题:宽度` |
| table 行 | 每行一条，单元格逗号分隔 |

## 流程

### 步骤 -1：先读懂使用者想要什么（意图识别）

使用者经常用**结果语言**而不是实现语言说话。先把你听到的话翻译成改动路线，再动手；
翻译不出来才允许提问，而且**最多问一个**问题（把选项和你的倾向一起给）。

**口语 → 路线对照表**：

| 使用者说（举例） | 实际是 | 走哪条 |
| --- | --- | --- |
| "XX 太丑 / 加圆角 / 换配色 / 字太小 / 图标难看" | 纯视觉 | 纯视觉 |
| "画布上能不能 XX（拖动、吸附、多选、框选、快捷键）" | 画布交互 | 步骤 4 或纯视觉（看是否动数据） |
| "控件要能设置 XX（提示、初值、格式、行图标…）" | 加属性字段（大概率**两端都要动**） | 步骤 1 + 铁律自查 |
| "XX 要能响应双击 / 我想在 XX 的时候收到通知" | 加事件 | 步骤 2 |
| "加一个 XX 控件 / 像易语言的 YY / 参考 XX 软件的 ZZ" | 新增控件 | 步骤 3 |
| "导出的代码要带 XX / 导出目录规则改一下 / F5 要 XX" | 导出 / IPC / 运行流程 | 步骤 4 |
| "存出来的文件里别出现 XX / .cgui 里要加 YY 字段" | 序列化格式（coerce/clean 对称） | 步骤 1 |
| "画布显示的和跑起来不一样" | 先查双端不同步（见陷阱清单），再修 | 先定位再选路线 |

**三个判断问题**（心里过一遍，答案决定改动面）：

1. **动不动 `.cgui` 数据？** 动 → coerceNode/cleanNode 必须成对改 + `check-format.bat` 是硬门禁。
2. **运行时认不认？** 凡是新增字段/控件/事件，`runtime\` 侧必须同步（铁律），否则画布能存程序读不懂。
   这时本技能管画布侧，C 侧按「跨端铁律」一起改掉，别只改一半。
3. **能不能肉眼验证？** 外观/交互类改动，stderr 干净不说明问题——跑 `npm run dev` 或
   `tools\shot.ps1` 截图看一眼再交付。

**什么时候才提问**：只有当说法同时可能映射两条以上路线、且选错会造成返工时
（例："给树加个展开事件"——树已有 expand；使用者到底想要什么新行为？）。
提问时给出你的默认倾向，使用者回一个字就能继续。

### 默认决策（使用者已授权 AI 直接定，定了要说理由和代价）

这些事**不要再问**，按默认做，交付时一句话说明你定了什么、为什么：

| 事项 | 默认 |
| --- | --- |
| 新字段名 | 跟 C 侧 `cg_node_t` 字段同名（如 `hint`/`period`/`multi`），短小写 |
| 新控件默认尺寸 | 参考同类控件（按钮类 32~40 高、输入类 28~32、容器 ≥180） |
| 事件命名 | 画布「自动」规则 `on_<id>_<事件>`，别发明第二套 |
| Palette 分组 | 按控件主要用途归入现有组（基础/输入/数值/数据/菜单/容器/装饰/其它），不新开组 |
| 视觉细节（圆角、间距、颜色） | 跟画布现有 M3 变量（`styles.css :root`），不硬编码 hex |
| 兼容旧 `.cgui` | 新字段必须有默认值兜底；旧文件读入不报错（coerce 宽容原则） |
| 改动范围 | 最小改动；不顺手重构、不整文件重写（用定向 `replace_in_file`） |

### 改完自查（交付前在心里核对，必要时向使用者报告）

按你实际走的路线，把这张表过一遍——**漏一项就是留给用户的一个坑**：

| 你动了 | 必须连带确认 |
| --- | --- |
| `CgNode` 字段 | `coerceNode` 读入 + `cleanNode` 写出成对；Inspector 有编辑入口；C 侧 `cg_node_t`/`parse_node`/`build_node` 同步 |
| `KINDS` | `k_kinds` 同步；`makeNode` 有种子；`WidgetView` 有外观；`styles.css` 有 glyph/ctl 类名；跑 `tools\run-tests.bat`（`test_kind.c` 会报不一致） |
| `KIND_EVENTS` / `CG_EVENTS` | `k_kinds[].events` / `CG_EV_*` 同步；`EVENT_LABEL`/`EVENT_HINT` 补中文 |
| IPC | `preload.cjs` 白名单 + `bridge.ts` 类型 + **浏览器降级分支** |
| 运行时公共 API（`cstackgui.h`） | `src/supportlib.ts`（「支持库」页签）同步加/改条目；`node tools\check-support.mjs` 必须过 |
| 纯视觉 | `tsc --noEmit` 过 + 截图肉眼确认 |

报告格式一句话即可："本次动了 X，同步改了 A/B/C，已过 tsc/vite/check-format，截图在 build\shot.png"。

### 步骤 0：判断改动类型（选一条往下走）

- **纯视觉**（改画布 UI 观感，不改数据）→ 只动 `styles.css` 和 JSX，跑类型检查 + 构建即可
- **加/改属性字段** → 步骤 1
- **加/改事件** → 步骤 2
- **新增控件类型** → 步骤 3
- **改导出产物 / IPC / 运行流程** → 步骤 4

### 步骤 1：加一个属性字段

- [ ] `model.ts`：`CgNode` 加字段 → `coerceNode()` 加兜底读入（包一层类型校验）→ `cleanNode()` 加写出条件（非默认值才写，保持文件干净）
- [ ] `Inspector.tsx`：加对应的 `Row`（一次编辑要走 `patch()` = `onBeginHistory()` + `onApply()`；多行文本框用 `Area` + `patchSoft`，靠 `onFocus` 压一次历史，避免每敲一个字进栈）
- [ ] 有外观含义 → `WidgetView.tsx`
- [ ] **同步 C 侧**：看改动是否为控件=M（继续）

### 步骤 2：加/改事件

- [ ] `model.ts::CG_EVENTS` 加名字 → `EVENT_LABEL` + `EVENT_HINT` 补中文
- [ ] `model.ts::KIND_EVENTS`：给**真正该有**这个事件的控件加上（原则：标签没交互、进度条只能被程序写、只有树有展开折叠）
- [ ] **同步 C 侧**（`runtime/include/cstackgui.h` 的 `cg_event_t`、`runtime/src/core/cg_doc.h` 的 `CG_EV_*` 宏、`runtime/src/core/cg_doc_kind.c::k_kinds[].events`）
- [ ] 注意：窗口 `close` 不进 `CG_EVENTS`，它是 `app.events.close` 伪控件 `CG_WINDOW_ID="__window"`
- [ ] `EventPanel.tsx` 无需改（它只读 `eventsOf(type)`）

### 步骤 3：新增一种控件类型（改动面最大，按序逐项）

画布侧：

- [ ] `model.ts::KINDS` 加一行 `{type,label,group,container,w,h,text,ranged?,checklist?,listLike?}` —— `group` 决定它在 Palette 哪一栏
- [ ] `model.ts::KIND_EVENTS` 加该控件支持的事件
- [ ] `model.ts::makeNode()`：给个「看得见」的种子内容，空控件在画布上没法用
- [ ] 有新字段 → 按步骤 1 补 `coerceNode`/`cleanNode`
- [ ] `WidgetView.tsx::inner()` 加 `case`（无需标准；tabs 的子控件只画当前 `page`）
- [ ] `styles.css` 补 `.glyph-<type>`（Palette/层级/面板头的小图标）和 `.ctl-<type>` 系列
- [ ] 需要特有属性编辑 → `Inspector.tsx`

运行时侧（**不同步 = 画布存了运行时读不懂**）：

- [ ] `runtime/src/core/cg_doc.h::cg_kind_t` 加枚举
- [ ] `runtime/src/core/cg_doc_kind.c::k_kinds` 加一行（名字/枚举/是否容器/默认宽高/默认文本/`CG_EV_*` 位掩码）
- [ ] `runtime/src/win32/cg_win_ctl.c`：`cgw_class_of()` + `cgw_style_of()`（必要时 `cgw_exstyle_of`、`cgw_create_native` 特殊处理）
- [ ] 有结构化内容 → `runtime/src/core/cg_doc.c` 的 `parse_*` 和 `runtime/src/core/cg_widget.c` 的 `build_node` 补字段
- [ ] 别在 C 里复制事件名表，用 `cg_event_name()`；`k_kinds`/`cg_kind_info()` 按 kind 字段线性查，**别用数组下标当枚举值**

### 步骤 4：改导出 / IPC / 运行

- [ ] `codegen.ts::generateC()` 改模板 → 记得 `${base}_ui_events.c` 在导出时**同名已存在就跳过**（用户业务代码不能被覆盖）
- [ ] **导出名必须 ASCII 标识符**：`App.tsx::suggestedBaseName` 只留 `[0-9a-zA-Z_]`、数字开头补 `_`、清空回退 `main`。
  原因：`codegen.ts::sanitizeIdent` 会把非 ASCII 全换成 `_`（纯中文标题曾生成 `_____ui_events.c`），
  且导出目录名 = `build-app.bat` 工程名 = exe 名，三处必须一致（踩过：2026-09-19）
- [ ] 导出目录规则在 `electron/main.mjs::cgui:exportC`：默认 `<项目根>/apps/<baseName>`；`<name>_ui_events.c` 存在则 skipped
- [ ] F5 一键运行（`run:buildAndRun`）前置条件：`.cgui` 已落盘、工程在 `apps\` 或 `examples\` 下、目录里有 `main.c`；工程名过 `safeName()` 后才拼进命令行；`chcp 65001` 保证输出 UTF-8
- [ ] 加新 IPC：`main.mjs` 加 `ipcMain.handle` → `preload.cjs` 白名单 → `bridge.ts::Bridge` 类型 + Electron 分支 + **浏览器降级分支**（缺降级会导致浏览器模式崩溃）
- [ ] 主进程菜单新增项：`Menu.buildFromTemplate` → `send("menu:<action>")` → `preload.cjs` 的 channels 列表 → `App.tsx::bridge.onMenu` 的分发

## 画布内部约定（改 JSX 前必看）

- **历史**：`onBeginHistory()`（压快照）+ `onApply(mutator)`（克隆后改），连续拖动只在开始压一次；`App.tsx` 里的 `edit()` 是两者合一版
- **坐标**：一律「窗口内逻辑像素」，容器内控件坐标相对父容器；`absRect()` 累加偏移，命中用 `containerAt()`（取最深的容器）
- **层叠**：没有 z 字段，`widgets`/`children` 数组顺序即层叠顺序（后面盖前面），LayerTree 的上下移就是 `splice`
- **tabs**：子控件按 `page` 分页；拖进 tabs 默认归到当前显示页（`(target.value ?? 0)`）
- **菜单**：`menu` 没有矩形（`def_w/h = 0`），由 `Canvas` 画在窗口框架上，一根窗口只认第一个（`doc.widgets.find`），`WidgetView` 直接 `return null`
- **事件徽标**：`WidgetView` 上 `bound.length` 的小角标来自 `node.events`
- **strict 模式**：`tsconfig.json` 开了 `noUnusedLocals`/`noUnusedParameters`，删代码要连变量一起删，否则 `tsc --noEmit` 报红
- **无障碍改名同步**：改了 IPC/行为记得同步 `App.tsx` 里的浏览器快捷键分支（浏览器没有 Electron 菜单）

## 验证（改完至少跑这些）

```bat
cd designer && npx tsc --noEmit && npx vite build     :: 类型检查 + 构建
tools\check-format.bat                                :: 画布写的 .cgui 必须被 C 侧读懂 + 支持库表对齐 cstackgui.h
tools\run-tests.bat                                   :: C 层单测（类型表/事件掩码/文档解析）
build-all.bat                                         :: 运行时 + 所有示例仍能编译
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui   :: 回 0 才算事件闭环
```

感官类改动（控件外观、画布视觉）光看 stderr 没用，**起 `npm run dev` 肉眼确认**，或
`tools\shot-canvas.ps1` 截画布窗口（它截的是启动默认画面；要截要点才能看到的页签，
先临时改 `App.tsx` 的默认 tab，截完改回来 —— 见脚本头注释）；
涉及 `.cgui` 字段/结构时，`check-format.bat` 是硬门禁。

## 陷阱清单

- 改了 `model.ts` 的字段却忘了 `cleanNode` → 字段永远存不进 `.cgui`
- 只改 `coerceNode` 不改 `cleanNode`（或反之）→ 存/读不对称，`.cgui` 语义漂移
- `KINDS` 加了、`cg_doc_kind.c::k_kinds` 没加 → 画布能画，运行时静默丢控件
- `KIND_EVENTS` 与 `k_kinds[].events` 不一致 → 绑了不支持的事件，运行时警告、`--selftest` 返回 5
- 新 IPC 没写浏览器降级分支 → 浏览器模式下 `bridge` 缺方法崩
- 改了 `cstackgui.h` 却没动 `supportlib.ts` → 「支持库」页签里点出来的函数不存在（`tools\check-support.mjs` 会拦）
- 绘制时对 `node.children` 直接递归忘了 tabs 的 `page` 过滤 → 画布显示与运行时不符
- `bind.src` 存的是**控件 id 字符串**（运行时按 `cg_find` 查）：手改 `.cgui` 里的 id 时忘了改 `bind.src` → 警告日志 + `--selftest` 返回 5（画布上改 id 会自动连带改，手改不会）
- 一次性替换大文件：这不是一个整文件重构型项目，用定向 `replace_in_file`，不要重写 `model.ts`/`App.tsx`
