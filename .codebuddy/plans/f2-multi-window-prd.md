# PRD：多窗体（F2）—— 第一个切片：模态子窗体

> 来源：向 py880 偷学清单里的 F2「窗体引用 / 子窗体组合」（`.codebuddy/plans/py880-absorption.md` §1）。
> 那一轮判定是 **defer（大 epic）**：要动 `.cgui` 顶层结构 + 运行时窗口模型 + 画布 + codegen，
> 必须单独走 PRD → SDD → 切片。**本文件是它的 PRD（第 1 步）。**

---

## 0. 地基事实（已调研，不是假设）

只读调研结论（证据见各行；调研覆盖 `runtime/` 四层 + 画布模型 + 示例）：

| 事实 | 证据 |
|---|---|
| 一条 `.cgui` **恒定 = 一个 HWND + 一棵控件树** | `cg_doc_t` 内嵌在 `cg_app_t` 里（`runtime/src/core/cg_internal.h:311`）；`app->widgets` 单链表（`:312`）；`app->hwnd` 单个（`:348`） |
| 全仓只有**一处**建顶层窗 | `CreateWindowExW` 仅 `runtime/src/win32/cg_win_window.c:297`（`cgw_win_init`，`:155`） |
| 消息泵只有一个，且**没有自建模态泵** | `win_run`（`cg_win_window.c:438`，`PeekMessageW` 在 `:447`）；现有嵌套循环只来自系统对话框（`cg_win_dialog.c:77/120/149/173/426`） |
| `k_kinds` 里**没有** `dialog`/`form`，但**已有** `"window"` 伪控件（`doc->root`，id `__window`） | `runtime/src/core/cg_doc_kind.c:24-99`（`"window"` 在 `:92`）；`cg_doc.c:882-883` |
| 现有 "dialog" 是**系统通用对话框**封装（文件/颜色/消息框/输入框），不是自建窗口 | `runtime/src/win32/cg_win_dialog.c:1`（文件头自述） |
| **事件表本来就是全局按名查的一张** → 多窗口回调**不需要**新机制 | `cg_binding_t`/`CG_BIND`（`runtime/include/cstackgui.h:181-187`）、`cg_resolve_binding`（`cg_internal.h:359`） |
| `tabs` 是"一个 HWND 内换页"（不是独立窗口） | `page_visible`/`cgw_sync_tabs_pages`（`runtime/src/win32/cg_win_sync.c:165/197`） |
| 画布的"主窗口"是**隐式伪控件**（不是 widgets 里的节点） | `WindowPanel.tsx`、`EventPanel.tsx` 的伪 id `__window`、`codegen.ts` 判 `widgetType === "window"` |

**结论：缺口真实且大；但"第二个窗口"的绝大部分难度在运行时，画布与 codegen 反而轻。**

## 1. 需求与场景

今天要做一个"设置/参数/关于"界面，只有两条路：挤进主窗口（`tabs`/`group` 折叠）或不做。
两条路都坏：

1. **串口/网口工具的"端口参数"**（真实：`examples/serialterm`、`examples/netterm`）：
   波特率/数据位/停止位/校验/流控五六个字段，今天只能塞在主界面里占一大块，或者开一个 tab
   切过去 —— 而 **tab 一切就生效，用户没有"取消"的地方**。
2. **任何"填一堆参数 → 确定/取消"的表单**：今天没有返回值语义的落点。
3. **"关于/授权/帮助"**：这类窗口只该被临时看一眼，不该占主界面面积、也不该出现在任务栏里。

**要的是**：从主窗口（或任意回调里）打开一个**自己的窗口**，它能被拖动/有标题栏、模态时挡住主窗口、
用户点"确定/取消"后把结果交回给调用方。

## 2. 范围决策（请重点看这三条；括号里是我的推荐与理由）

### D1 先只做**模态**，非模态推后（**推荐**）
- 场景 90% 是"填参数 → 确定/取消"，模态语义**清晰且无害**：Owner 禁用、独立生命周期、有返回结果、
  不需要解决"两个窗口同时改一份数据谁刷新谁"。
- 非模态多顶层窗口要带来的问题（数据同步、关闭顺序、任务栏一堆项）**与本次难度无关但风险更高**。
- 实现上两者共用"第二个 HWND + 独立控件树 + 模态泵"，所以以后放非模态是**加一个开关**，不是重做。

### D2 契约形状：**顶层 `dialog` 控件节点**，不是顶层 `windows[]` 数组（**推荐**）
| | (a) `windows: [ {app, widgets} ]` | (b) `widgets` 里一个 `dialog` 节点 |
|---|---|---|
| 形状 | 每个窗口一整份（含自己的 `app`） | 节点**本身就是窗口**，`children` 是窗口里的控件，`rect` 是窗口位置尺寸 |
| 像不像 py880 | 像（"子窗体独立设计"） | 不像，但它要的是**能力**不是形状 |
| 解析器 | 要产出多根、要动顶层键 | **不用动**：`group`/`tabs`/`card` 的 children 早就是树 |
| 运行时 | 要按窗口分区 id/ctrl_id | 复用**已有**的 `"window"` 伪控件概念（`k_kinds` 里就有） |
| 画布 | 要新增整套"窗口集合"UI（窗口页签 + 每窗口 app 面板） | **零新增概念**：dialog 就是一个节点，层级/属性/事件面板照用 |
| codegen | 要按窗口出多份 `_ui_events` | 一张 `CG_BIND` 表照样够（本来就全局按名查） |
| 代价 | — | 不能"一个子窗体一个独立设计文件"（py880 标原创的那部分）→ **列为非目标，等有真实需求再做"引用另一个 .cgui"** |

**(b) 让第一个切片就能跑出真窗口，且三端改动面最小。** 这是本 PRD 最关键的一条。

### D3 "确定/取消"的语义：两个 API，画布**不发明**新绑定机制（**推荐**）
```c
/* 打开（模态 = 1）。返回 0 = 取消/直接关掉，1 = 确定 */
int cg_dialog_open(cg_widget_t *dlg, int modal);
/* 在回调里关掉自己（result 就是上面那个返回值） */
void cg_dialog_close(cg_widget_t *self, int result);
```
- 按钮上就写一行 `cg_dialog_close(self, 1);`（"取消"写 0），**不需要**"自动识别哪个是确定按钮"
  （那要靠猜用户意图）。
- 可选的便利：复用已有的 `actions` 机制，画布能给"确定/取消"各配一个动作。

## 3. 验收标准（每条都要能失败）

| # | 验收 | 怎么验（会红的方式） |
|---|---|---|
| A1 | `dialog` 节点在运行时开出一个**真的独立 HWND**：有标题栏、能拖动、不模态时 Alt+Tab 里出现 | 真跑一个示例；用窗口枚举/截图取证（不是"代码里看像"） |
| A2 | 模态时 **Owner 被禁用**：主窗口点不动 | 真机点它，必须无反应 |
| A3 | `cg_dialog_open` 的返回值正确反映"确定/取消" | 真机点两个按钮，打印/断点各验一次 |
| A4 | 对话框里控件的值能**读回去**（`cg_find` 在对话框树里也找得到，id 不串） | 对话框里放一个 edit，确定后主窗口把它的值打印出来 |
| A5 | **关对话框不会退出整个应用**（这是现状的硬伤：`WM_DESTROY` 无条件 `PostQuitMessage`，`cg_win_main.c:470`） | 关对话框后主窗口必须还活着 |
| A6 | 老 `.cgui`（不含 `dialog`）行为**一个字节都不变** | `tools\check-format.bat` 全部往返探针绿 + 现有示例跑一遍 |
| A7 | 画布端到端：加 dialog 节点 → 拖控件进去 → 导出 → 编译 → 跑起来看到窗口 | 一条命令链走完 |
| A8 | 单文件发布不受影响（`.cgui` 仍是一份 PE 资源，不多出文件） | 发布后把 `.cgui` 挪走，exe 仍能跑出对话框 |

## 4. 非目标（明确不做）

- **非模态多顶层窗口**（D1 已说明）。
- **"引用另一个 `.cgui` 文件"**（py880 标原创的"窗体引用"核心）：那是"多文件组装"契约，另一个 epic。
- 窗口间数据绑定/联动、每窗口独立主题、可停靠面板、多显示器定位策略。

## 5. 切片预案（每个都能独立交付 + 独立验证）

| 票 | 内容 | 验收 |
|---|---|---|
| **T1 运行时地基 + 模态** | parse 认 `dialog` 节点；`cg_app_t` 支持第二棵窗口树；建窗抽成"对任意窗口可用"的函数；消息泵只跑主窗、模态走内层泵；`WM_DESTROY` 只主窗退出；id/ctrl_id 按窗口分区；`cg_dialog_open/close` | **手写**一个带 `dialog` 的 `.cgui` 就能看到窗口 → A1/A2/A3/A5（不碰画布，风险面最小） |
| **T2 画布** | 控件箱加 `dialog`；能往它里面拖控件；属性面板给窗口级字段（title/modal/center/border/icon）；层级面板正常 | A7 |
| **T3 示例与文档** | `examples/` 加一个对话框示例（给 `serialterm` 加"端口设置"最贴真实场景）；`docs/CGUI-FORMAT.md` 补 `dialog` 字段；`docs/ARCHITECTURE.md` 补"多窗口"一节；`README.md` 已知边界销账 | 示例能跑 + 文档口径门禁绿 |
| **T4（可选，推后）** | 非模态、`dialog` 里的 `tabs`、按需"引用外部 .cgui" | — |

**顺序约束（跨端）：T1 必须早于 T3。** `tools\check-format.bat` 的往返探针是"画布写 → 运行时读 → 比"，
在运行时还不认识 `dialog` 时就把示例改成带 dialog，往返会红。T1 先落，示例才能改。

## 6. 风险（调研已列，逐条给证据）

| 风险 | 证据 |
|---|---|
| 单例全局 app：焦点子类/tooltip 反查依赖 | `g_win_app`（`cg_win_window.c:189`、`cg_win_event.c:18`） |
| 防重入标志跨窗共享，模态嵌套时语义混乱 | `g_win_in_notify`（`cg_win_event.c:10`） |
| 关闭语义：关任一窗口都会退整个应用 | `WM_DESTROY` → `PostQuitMessage`（`cg_win_main.c:470`） |
| id 命名空间全局：跨窗 `cg_find`/`WM_COMMAND` 可能串号 | `app->widgets` 单链表（`cg_internal.h:312`）+ `next_ctrl_id`（`:324`） |
| 菜单栏单例、菜单 cmd 计数全局 | `cg_win_window.c:374-379`、`cg_internal.h:325` |
| 资源归属单份（字体/背景刷/tooltip/accel/主题） | `cg_win_state_t`（`runtime/src/win32/cg_win_internal.h:41-60`）、`win_shutdown`（`cg_win_window.c:476-528`） |
| **不必**每窗口一份的东西（别误改） | 进程级 GDI+/富文本/ATL：`cg_win_internal.h:323/329/331` |

## 7. 待你拍板的三件事（我都给了推荐，说"继续"我就按推荐往下做）

1. **D1**：先只做模态子窗体？（推荐：是）
2. **D2**：契约用"顶层 `dialog` 节点"？（推荐：是）
3. **D3**：确定/取消用 `cg_dialog_open/cg_dialog_close` 两个 API，不回退到"自动识别确定按钮"？（推荐：是）

## 8. 进度

- [x] P0 地基调研（运行时窗口模型 / 消息泵 / 契约 / 画布模型四路，结论见 §0）
- [x] P1 PRD：本文件（§7 三条决策按推荐定案）
- [x] P2 SDD：`.codebuddy/plans/f2-multi-window-sdd.md`（核心决策 W：`app->widgets` 保持一棵树不分区，
      窗口 = window 伪控件、子树懒建；**本轮零新字段**——`center` 与 Inspector 开关同轮（T2-full）再加）
- [x] P2.5 T1.0 前置加固：画布对未知控件类型**静默降级成 label**（会连子树一起丢）→ 改成与运行时
      同口径"拒绝整份文件"。红绿证据：`tools/probe-dialog.mjs`（反例 2 条红，打印出
      `dlg_settings` 变 label、4 个子控件全没）；5 个真示例往返探针全绿（收紧没打破真文件）
- [x] P3 切片细化：SDD §6（T1.1/T1.2/T1.3 每步带验证）；fixture 已备好（`tools/fixtures/dialog.cgui`），
      红基线已实测（运行时 exit=3：`unknown widget type "dialog"`）
- [x] P3a **T1.1 + T2 最小集 + 示例落点 + 文档计数（2026-10-07 同轮落地，全绿）**
      —— 切片边界被门禁修正（SDD §8：check-examples 从 k_kinds 现读类型清单，
      单落运行时必红，且 --docs 先失败会遮住 coverage 步）。
      验收证据：`tools/probe-dialog.mjs` 全绿（画布不静默丢 + 运行时树 dump 可见 dialog 与子控件）+
      `run-tests.bat --no-kit` = `all checks passed` + `check-format.bat` = `BAT_EXIT=0`
      （5 个往返探针含带 dialog 的 gallery、`every kind has an example`、计数 44）+
      `tsc`/`vite build` 绿 + 画布截图 `build\canvas-dialog.png`。
- [x] P3b **T1.2（2026-10-07）**：`cg_dialog_open/close`（新 `cg_win_modal.c`：独立 HWND + 子树懒建/
      收尾 + 内层泵 + 防重入锁存/清/恢复）+ `cgw_MainProc` 宿主化 + `WM_DESTROY` 只主窗退（A5）+
      `cgw_destroy_native_tree`。真机 A1~A5 **15 项断言 ALL PASS**（`tools\probe-modal.c` 外部驱动 +
      被测程序自写 result.log 双证）
- [x] P3c **T1.3（2026-10-07）**：往返门禁加 dialog 子树探针（编辑塞探针按钮 + 恒等回写逐字核对），
      红证：serialize 临时丢 children → `4 -> 0` 两连红
- [x] P4 **T2-full（2026-10-07）**：`center` 字段五层管线（缺省居中，旧文件零变化）+
      Inspector「打开时居中」分节 + 画布「模态」徽章（同一画布浮着，不用遮罩）。
      自查修正：center:false 位置与主窗口 app.x/y 同口径（裸值不换算）
- [x] P5 **A7 端到端（2026-10-07）**：`tools\probe-dialog-e2e.mjs` —— 画布模型 → generateC →
      用户一行补桩 → cgbuild → 真窗口：对话框存在、`pos=40,40`（center:false 精确定位）、
      owner 禁用。**10 项断言 ALL PASS**。过程记录：PowerShell 的 Add-Type 委托两轮跑法下
      都静默失灵（连已知有窗口的 modaltest 都"枚举不到"）—— 换 C 驱动（probe-modal 同款）一次就对；
      **工具不可靠时换工具，别把"工具找不到"当成"应用没窗口"**。
- [x] P7 收敛：A1~A8 逐条对账，见下

### 验收对账（P7，2026-10-07）

| # | 结果 | 证据 |
|---|---|---|
| A1 独立真窗口（标题栏/可拖） | Converged | 真机：`WS_CAPTION`/`WS_SYSMENU`/hwnd≠主窗口（probe-modal 3 项）；`CStackGUIDialog` 独立窗口类 |
| A2 模态时 owner 禁用 | Converged | 真机两处：probe-modal（`IsWindowEnabled==0`）+ e2e（`owner-enabled=0`）；关闭后恢复 ✓ |
| A3 返回值反映确定/取消 | Converged | result.log：OK 路径 `result=1`、WM_CLOSE 路径 `result=0` |
| A4 对话框里的值读得回 | Converged | `cg_find` 读回 `port=COM3`（edt_port 在对话框子树里） |
| A5 关对话框不退应用 | Converged | `alive=1`（业务自证）+ `IsWindow(main)`（user32 复核）；`WM_DESTROY` 已只主窗退 |
| A6 老 .cgui 一个字节不变 | Converged | 全程未动任何老文件；5 个往返探针 + `run-tests` 全绿；旧文件无 center 字段 → 缺省居中零行为变化 |
| A7 画布端到端一条链 | Converged | `probe-dialog-e2e` 10 项 ALL PASS（模型→generateC→编译→真窗口→定位→模态） |
| A8 单文件发布不受影响 | Converged | generateC 文件清单与多窗体之前**逐字一致**（6 个文件、仍一份 .cgui）；对话框进同一份 PE 资源 |

**F2 epic 收敛**。切片：T1.0（画布不许静默丢）→ T1.1（类型）→ T1.2（模态 API）→ T1.3（往返守卫）→
T2-minimal（画布/示例/计数）→ T2-full（center/Inspector/徽章）→ A7 e2e。剩余 T4（可选）：
非模态、对话框里的 tabs、"引用另一个 .cgui"——**有意不做**（PRD §4 非目标）。

**后续补记（2026-10-09）：T4 的两项已落地并沉淀进示例。** 非模态（T4 前半）与对话框里的
tabs（T4 可选项）都已真机验证（tools\probe-modal.c 51 项 ALL PASS）。本日又发现**示例里的多窗体
是死的**：gallery 的 `dlg_about` 只躺在 .cgui 里，main.c 里零引用、确定取消也没绑事件 ——
用户跑 gallery 根本看不到这项能力。现已接通：「帮助 → 关于」走模态、「关于（非模态）」走
`cg_dialog_open_nonmodal`，对话框里装了**两页 tabs**；自检 74 控件 / 49 回调 / **未登记 0**，
往返探针守着这棵子树（含 tabs）无损，README 与格式文档的口径同步（含"对话框里可以放 tabs"）。
剩余只有 T4 后半（"引用另一个 .cgui"多文件组装）——那仍是另一个契约 epic，
**2026-10-10 已立为 F3 并出了 PRD**：`.codebuddy/plans/f3-window-ref-prd.md`
（顶层 `include` 数组 + 加载期按目录展开 + 发布态由编译编进 PE 资源；切片 P1~P6）。
F2 到此全部销账。
