# SDD：多窗体（F2）—— 顶层 `dialog` 节点 + 模态子窗体

> 前置：`.codebuddy/plans/f2-multi-window-prd.md`（D1 模态优先 / D2 顶层 dialog 节点 / D3 两个 API，均已按推荐定案）。
> 本文件是实现契约：**照它写代码，不用再猜**。与落盘代码不一致就报回来，以代码为准。
> 「地基调研」与「新增一种类型的既有套路」两份调研结论已并入本文（证据行在引用处）。

---

## 0. 本轮已完成的前置（T1.0，已有红绿证据）

写 SDD 时测红基线，抓到一个**真缺陷**并已修掉：

- **画布对不认识的控件类型会静默降级成 `label`**（`designer/src/model/parse.ts` 的兜底）——
  `label` 不是容器，`children` 连整棵子树带事件一起消失；用户打开再保存 = **永久删除**。
  而运行时对未知类型是"整份文件加载失败"（实测：`unknown widget type "dialog"`，exit=3），
  所以那份文件**本来就跑不起来**，降级救不回任何东西。
- **修法**：与运行时同口径——未知类型拒绝整份文件，错误信息说清楚（`parseDoc` 收成 error，
  画布已有的"打开失败"提示路径直接显示）。别名（multiedit/spinner）的宽容保留。
- **证据**：`tools/probe-dialog.mjs`。反例（退回旧兜底）→ 2 条红，
  且打印出损失：fixture 读进来只剩 `label,button,label`（`dlg_settings` 变 label、4 个子控件全没）。
- 这一步是 T2 的**前置**：没有它，"新画布写的 dialog 文件被旧画布打开再保存"就是数据丢失事故。

## 1. 契约（`.cgui`）：`dialog` 节点

```jsonc
{
  "id": "dlg_settings",
  "type": "dialog",
  "rect": { "x": 0, "y": 0, "w": 320, "h": 180 },  // x/y = 位置（center=true 时忽略），w/h = 窗口客户区
  "text": "端口设置",                               // = 窗口标题（复用，不新增字段）
  "center": false,                                  // 唯一新字段：相对 owner 居中，默认 true（不写 = 居中）
  "events": { "close": "on_dlg_close" },            // 窗口级事件，与 app.events 同一套名字
  "children": [ /* 这个窗口里的控件，坐标相对对话框客户区 */ ]
}
```

**设计原则：能复用就复用。** `rect`/`text`/`events`/`children` 全是既有字段，**只新增 `center` 一个**
（模态性是**调用点**的属性、不是窗口的属性 → 不做字段；边框 T1 一律 fixed + 只关按钮 → 不做字段；
图标复用窗口类自带的 app 图标 → 不做字段）。`serialize.ts`"等于默认值不写"的约定使得
`center:true` 的文件里根本看不到这个键。

`k_kinds` 行（插在 `group/box/card` 容器组之后；枚举值插在 `CG_K_COUNT` 前——`.cgui` 存的是
类型**名字符串**不是枚举值，所以枚举数值变化无影响）：

```c
{ "dialog", CG_K_DIALOG, 1, 360, 240, "", <窗口级事件位掩码，逐字照抄 "window" 行（cg_doc_kind.c:92-98）> },
```

**三表必须逐字一致**（本工程硬约定，`check-examples.mjs` 回校类型总数）：
`k_kinds`（C）↔ `KINDS`（`designer/src/model/kinds.ts:27-82`）↔ `KIND_EVENTS`（`events.ts:131-192`）。

## 2. 运行时设计（本 SDD 的核心决策）

### 决策 W：`app->widgets` **保持一棵树，不分区**

| | 方案 A：`windows[]` 数组 + app 分区（调研建议的"最小改动集"） | **方案 B（选定）**：窗口 = window 伪控件，树不分区 |
|---|---|---|
| 依据 | — | core 的 `build_node` **无条件递归**（`cg_widget.c:403-405`）→ dialog 子树的 widget 对象**已经建好**；"建不建原生"是 win32 侧按类型名早退（`cg_win_create.c:52-69`） |
| `cg_app_t` | 大改（`doc/widgets/window/hwnd/natives` 全部数组化） | **几乎不动** |
| id / ctrl_id | 要按窗口分区，否则串号 | **天然全局唯一**（节点本来就在同一棵树）；`ctrl_id` 建 widget 时已分配（`next_ctrl_id`），懒建原生不改变它 → 无串号 |
| `cg_find` 跨窗口 | 要设计跨窗查找 | **免费**：主窗口回调里 `cg_find(app,"edt_port")` 直接找到对话框里的控件（PRD A4 直达） |
| 事件表 | 一张 | 一张（本来按名全局查，`cg_resolve_binding`） |
| 剩余风险 | 全仓单例假设逐一排查 | 集中在 win32 层（见 §4） |

### 窗口与伪控件

- **每个窗口一个"window 伪控件"**：主窗口沿用 `app->window`（`cg_build_widgets` 的 `is_window=1`）；
  对话框**用 dialog 节点自己充当**其窗口的事件宿主（它是 `CG_K_DIALOG`，窗口级事件挂它身上）。
  WndProc 从 `GWLP_USERDATA` 取到"本窗口的窗口对象"后，`app = win->app` 反查（`cg_widget` 已有
  `cg_app_t *app` 字段，`cg_internal.h:176`）→ **单 app 的全部既有全局（`g_win_app` 等）不用动**。
- **懒建 / 销毁**：启动时 `cgw_create_native` 遇到 `dialog` → **早退**（子树原生不建，同
  `menu/timer/serial` 的先例 `cg_win_create.c:54/58/65`）；`cg_dialog_open` 时才对子树建原生
  （父 HWND = 对话框 HWND），关闭时逐个销毁。**`win_shutdown` 按 `app->widgets` 统一释放的假设
  因此失效** → 关闭路径必须显式销毁（§4 R1，这是最重要的一条）。

### 新 API（D3 定案）

```c
/* 打开模态对话框。返回 0 = 取消/Esc/直接关掉，1 = 回调里 cg_dialog_close(self,1) 过 */
int  cg_dialog_open(cg_widget_t *dlg);
void cg_dialog_close(cg_widget_t *self, int result);   /* 只能在对话框自己的回调里调 */
```

- 模态泵：`EnableWindow(owner, FALSE)` + 内层 `PeekMessage` 循环 + 结束恢复。现有运行时**没有**
  自建模态泵（现有嵌套循环只来自系统对话框，`cg_win_dialog.c:77/120/149/173/426`）→ 净新增。
- **Esc = 关闭（result 0）**（Win32 对话框惯例）；Tab 导航由泵里的 `IsDialogMessageW` 免费给
  （`win_run` 现在按单 HWND 做，`cg_win_window.c:456`，改成按消息所属窗口）。
- `WM_DESTROY`：**只有被销毁的 HWND == `app->hwnd`（主窗口）才 `PostQuitMessage`**
  （现状无条件退出，`cg_win_main.c:470` —— 这是 PRD A5 要治的硬伤）。
- `cgw_parent_hwnd_of`（`cg_win_create.c:33`）：顶层控件挂**所属窗口**的 HWND——
  判据 = 沿父链找最近的 `CG_K_DIALOG` 祖先。

## 3. 画布设计（T2，运行时就绪后才放开）

| 改动点 | 位置 | 说明 |
|---|---|---|
| `KINDS` 加 `dialog` | `designer/src/model/kinds.ts:27-82` | `container:true`，`group:"容器"`（跟 group/box/card 一组），`w:360,h:240`，顺序对齐 `k_kinds` |
| `KIND_EVENTS` 加 dialog | `designer/src/model/events.ts:131-192` | 窗口级事件集，与 `k_kinds` 位掩码逐字一致 |
| 容器外观 | `designer/src/components/WidgetView.tsx:602` | `isGroup` 硬编码列表加 `dialog`（画布上 = 带标题的虚线容器，子控件照常拖） |
| 调色板图标 | `designer/src/styles.css:321+` | `.glyph-dialog` |
| `center` 字段 | `parse.ts coerceNode` + `serialize.ts cleanNode` **各补一行** | 白名单式序列化：不补就**静默丢字段** |
| 事件面板 | `eventsOf`（`events.ts:196`） | `type === "dialog"` 也走窗口级事件表（现在只认 `"window"`） |
| Inspector | `TypeSection.tsx` | 一个"对话框"分节：居中开关 + 一句说明（"运行时 open 才出现；模态"） |
| codegen | **不用改** | 容器照常被 `walk` 遍历，dialog 的 `events` 自动进 `CG_BIND` 表 |
| 限制 | 画布禁止把 `menu` 拖进 dialog | 菜单栏是"每窗口一根 HMENU"且 `SetMenu` 只在 win_init 做一次（`cg_win_window.c:374-379`） |

## 4. 风险与限制（逐条给证据）

| # | 风险 | 处置 |
|---|---|---|
| R1 | **资源释放**：`win_shutdown` 按 `app->hwnd`+`app->widgets` 统一释放（`cg_win_window.c:476-528`），而对话框子树原生是动态生死 | 关闭路径显式 `cgw_destroy_native_tree`；程序退出时若还有打开的对话框，先走同一路径收尾 |
| R2 | `g_win_in_notify` 防重入标志跨窗共享（`cg_win_event.c:10`） | 单 app 下它仍指"正在回调"，语义不变；实现注释里写明 |
| R3 | 菜单栏单例（`cg_win_window.c:374-379`） | T1 对话框**不支持菜单**：画布 T2 拦；运行时遇 menu 挂 dialog 下打 WARN 忽略 |
| R4 | `IsDialogMessageW` 按单 HWND 导航（`cg_win_window.c:456`） | 泵里按 `msg.hwnd` 所属窗口调用 |
| R5 | **别误改**进程级单例：GDI+/富文本/ATL（`cg_win_internal.h:323/329/331`）、字体缓存/`bg_brush`/tooltip/accel（`cg_win_state_t`，`cg_win_internal.h:41-60`） | **保持进程级**，不 per-window 化（调研明说"别误改"） |
| R6 | 对话框里再嵌 dialog | T1 支持（懒建/模态逻辑天然递归）；树结构无环 ✓ |

## 5. 跨端顺序（硬约束）

**T1（运行时）→ T2（画布）→ T3（示例/文档）**，理由都有证据：

1. `check-format.bat` 的往返探针是"画布写 → 运行时读 → 比"；运行时还不认 `dialog` 时改示例就红。
2. 三表（k_kinds/KINDS/KIND_EVENTS）必须逐字一致——单边先加就是"画布能存、运行时读不了"。
3. **门禁缺口**：`tools/format-roundtrip.mjs` 只守 bind/action/styles/app.dark（`:213-291`），
   **新字段 `center` 丢了它不报错** → T1 必须同步给它加 dialog 往返探针，否则契约没人守。
4. T2 之前，画布对含 dialog 的文件**报错拒绝**（T1.0 已修的行为）→ 不存在"画布有入口、运行时不行"的窗口期。
   所以 **T1 全程不碰画布**，fixture 手写（`tools/fixtures/dialog.cgui` 已备好）。

## 6. T1 拆解（每步带验证）

| 步 | 内容 | 验证（会红的方式） |
|---|---|---|
| T1.1 | `cg_doc.h` 枚举 `CG_K_DIALOG` + `k_kinds` 行（容器、窗口级事件掩码）；解析自然通（children 递归已通用）；`cgw_create_native` 对 dialog **早退**（先保证"主窗口里不冒出对话框内容"） | `apps\main\main.exe --selftest build\fixtures\dialog.cgui` **通过**（今天是 exit=3），且真跑起来主窗口看不到对话框内容 |
| T1.2 | `cg_dialog_open/close` + 独立 HWND + 懒建/销毁子树 + Owner 禁用 + 模态泵 + `WM_DESTROY` 判据 + `cgw_parent_hwnd_of` 按窗口取 | PRD A1/A2/A3/A4/A5 真机各验一次（fixture 的按钮回调里打日志） |
| T1.3 | `format-roundtrip.mjs` 加 dialog 往返探针（守 `center` 与 children 无损）+ 基线翻转（`probe-dialog-baseline.mjs` 的两条基线断言反向） | `check-format.bat` 绿 + 基线探针红转绿 |
| T2 | §3 画布全套 | PRD A7（画布拖一个 dialog → 导出 → 编译 → 跑） |
| T3 | `serialterm` 加"端口设置"对话框示例 + `docs/CGUI-FORMAT.md`（类型表 + children 说明 + 计数）+ `docs/ARCHITECTURE.md`（多窗口一节）+ README 销账 | 示例真机可跑 + 文档口径门禁绿 + PRD A8（单文件发布） |

## 7. 验收映射（PRD A1~A8 落在哪一步）

A1/A2/A3/A5 → T1.2（真机）｜ A4 → T1.2（`cg_find` 免费达成，但要真测）｜ A6 → 每步的 `check-format.bat` ｜
A7 → T2 ｜ A8 → T3。

## 8. 进度

- [x] P0 地基调研（PRD §0）+ 既有套路调研（本文引用的证据行）
- [x] P1 PRD
- [x] P2 SDD：本文件
- [x] P2.5 T1.0 画布未知类型静默丢数据（顺手修掉，红绿证据齐）
- [ ] P3 实现 T1.1 → T1.2 → T1.3（下一轮）
- [ ] P4 T2 画布
- [ ] P5 T3 示例与文档
