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
  "center": false,                                  // T2-full 再加：相对 owner 居中，默认 true（不写 = 居中）
  "events": { "close": "on_dlg_close" },            // 窗口级事件，与 app.events 同一套名字
  "children": [ /* 这个窗口里的控件，坐标相对对话框客户区 */ ]
}
```

**设计原则：能复用就复用。** `rect`/`text`/`events`/`children` 全是既有字段；本轮（T1.1 + T2 最小集）
**一个新字段都不加** —— 模态性是**调用点**的属性（不做字段）、边框一律 fixed + 只关按钮（不做字段）、
图标复用窗口类自带的 app 图标（不做字段）、`center` 与 Inspector 的开关同轮再加
（没有消费者的字段会先造成"管线落了、往返门禁没守它"的缺口）。

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
- [x] P3a **T1.1 + T2 最小集 + 示例落点 + 文档计数（2026-10-07 同轮落地，全绿）**
      —— **切片边界被门禁修正**：`check-examples.mjs` 从 `k_kinds` **现读**类型清单（只排除
      `window`），单落运行时会让 `dialog` 进 "every kind has an example" 的 missing，且
      ④.5 计数 / ④.6 掩码↔文档 / ④.7 画布↔掩码三道对账全红；实测 `--docs` 先失败还会
      `exit /b 1` 遮住后面的 coverage 步。**绿的最小单位 = 运行时 + 画布类型表 + 示例 + 文档**。
      已落：`CG_K_DIALOG` + `k_kinds` 行（掩码裁到 21 个真会触发的，C/TS/文档三方同步）+
      建原生两处整树跳过；`KINDS`/`KIND_EVENTS`/`isGroup`/`.glyph-dialog`；
      gallery 落 `dlg_about` 示例 + exe 重编；文档计数 43→44 + CGUI-FORMAT 第 4 节 dialog 行 +
      children 说明。证据：审查报告 `2026-10-07-多窗体T1.1-T2最小集.md`（selftest 树 dump /
      run-tests / check-format 全绿 / 画布截图）。
- [x] P3b **T1.2（2026-10-07 落地，真机 A1~A5 全过）**：`cg_dialog_open/close`
      （新 `runtime/src/win32/cg_win_modal.c`：独立 HWND + 子树懒建/收尾 + 内层泵 +
      防重入锁存/清/恢复）+ `cgw_MainProc` 宿主化（按 HWND 认宿主，主窗口=__window、
      对话框=dialog 节点）+ `WM_DESTROY` 只主窗退（A5）+ `cgw_destroy_native_tree` +
      `cgw_layout_apply` 对话框跳过（它写死排 app->window 的孩子）。
      **真机证据**：`build\probe-modal.c`（外部驱动，MinGW 直编）15 项断言 ALL PASS、
      probe-exit=0 —— A1 独立 HWND（标题/WS_CAPTION/WS_SYSMENU/≠主窗口）、
      A2 owner 禁用/恢复、A3 返回值 OK=1/WM_CLOSE=0、A4 `cg_find` 读回 edt_port=COM3、
      A5 主窗口存活。**掩码核对**：裁掉的五个结构性不会发（ready=启动时只发 __window、
      firstactivate=per-app 一次、idle=主泵才有、tray/drop=对话框没这些资源），
      保留的经共享过程触发 ✓ 两边没动。
      **过程中抓到的真缺陷**：防重入锁会在嵌套泵期间吞掉对话框的所有控件通知
      （open 跑在按钮回调栈里）——按 tray_callback/菜单泵的先例存/清/恢复；
      `cgw_layout_apply` 写死排 app->window 的孩子，对话框 WM_SIZE 会把主窗口孩子
      按对话框尺寸重排 —— 加 `win == app->window` 守卫。
      **事故记录**：本轮工具回执出现大量与实际操作不符的内容（未发送的 old_str、
      不存在的文件路径、引用未创建函数的"成功"），一度用"并行代理"假设合理化 ——
      错误。以编译器/git/新鲜读取为唯一真相重新核实后继续；期间发出的两份废稿
      （PowerShell 探针）已废弃，改用百余行 C 探针（MinGW 直编，无编码坑）；另抓到探针与
      工程同目录会让 cgbuild 误编出双 main 的布局坑（已挪出）。
- [x] P3c **T1.3（2026-10-07 落地）**：`format-roundtrip.mjs` 编辑阶段往 dialog 塞探针按钮
      （事件复用已登记回调，不造"未登记"假故障），恒等回写阶段逐字核对子树
      （数量 / id / 类型 / 文字 / 事件）。gallery 实测：`dialog 子树无损（4 个子控件，含探针按钮）`
      + `selftest OK`；**红证**：serialize 临时丢 dialog children →
      `子控件 4 -> 0` 与 `探针子控件丢失` 两连红，还原回绿。其余 4 个示例无 dialog，
      走 `if (dlg)` 守卫静默跳过 —— 5 个 roundtrip 全过，`check-format` `BAT_EXIT=0`。
- [x] P4 **T2-full（2026-10-07 落地）**：`center` 字段走完五层管线
      （types/parse/serialize → TypeSection「打开时居中」分节 → `cg_doc.h`/`cg_doc.c`
      （`cj_bool` 缺省 1，旧文件零变化）→ `cg_widget.c` 拷贝 → `cg_win_modal.c` 开窗）。
      **画布叠画决断（按推荐定案）**：同一画布浮着 + 角上「模态」小徽章（`.dlg-badge`），
      不用遮罩 —— 画布还要编辑子控件。往返探针顺带守 `center:false`（写不出/读不回都会红）。
      **自查修正**：`center:false` 分支第一版做了 `cgw_scale_px`，与主窗口 `app.x/y`
      的裸赋值先例（cg_win_window.c has_pos）口径不一 —— 改成同一口径（不换算）。
      真机 A1~A5 全 PASS（缺省路径回归）+ 5 个 roundtrip / check-format `BAT_EXIT=0`。
      **如实记录**：`center:false` 的**开窗位置**没有真机断言（modaltest 只有一张
      居中的对话框；该分支与居中分支共用 CreateWindowExW，位置语义同主窗口先例，低风险）。
- [ ] P5 T3 尾：gallery 示例与类型计数已在 T2-minimal 落；本文档（CGUI-FORMAT）已随
      `center` 与函数索引更新。剩余：`format-roundtrip` 的 dialog 探针已在 T1.3 落。
      **画布叠画的设计决断**（同一画布浮着 vs 独立窗口视图，审查报告 R1 #4）+
      禁止把 `menu` 拖进 dialog
- [ ] P5 T3：教学/文档收尾（gallery 的对话框接上打开按钮）
