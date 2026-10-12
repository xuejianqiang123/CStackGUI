# 计划：新增自绘控件 `iconlist`（列表行带图标）

> 状态：P1 计划已定稿 → P2 实现中 →（完成后在此追加「已完成 / 下一步 / 遗留」进度段）
> 创建：2026-10-12 ｜ 路线：SOP §2 标准路径（C→P0→P1→P2→P3→P4→P5→P7）

---

## 1. Spec（要什么 / 为什么 / 验收 / 非目标 / 边界）

### 1.1 要什么

新增控件类型 **`iconlist`**：一列可滚动的行列表，每行 = 〔复选框（可开关）〕+ 左图标 + 主标题（大字）+ 副标题（第二行小字，可空）+ 右侧状态文字（可空，右对齐）。支持点行选中、点复选框勾选、双击、鼠标滚轮与垂直滚动条、Tab 焦点与键盘上下导航。对标用户给的截图形态（mxx1-toolbox 右键菜单管理的行列表）。

### 1.2 为什么

- `listbox` 是原生 ListBox，原生不支持每行图标；`table` 的 row_icon 只支持单行文字，复刻不了「主标题+副标题两行高」的行。
- 用户已拍板：新增自绘控件（方案 A），纯新增、零回归。

### 1.3 验收标准（每条都可失败）

1. `build-all.bat` 全绿：runtime + 全部 examples 编译并逐个 `--selftest` 通过。
2. `tools\run-tests.bat` 全绿：含新增 `tests\unit\test_iconlist.c`（正例 + 反例：对 `listbox` 调 `cg_iconlist_*` 必须被类型校验拒绝）。
3. `tools\check-format.bat` 全绿：roundtrip（gallery 在名单内）/ check-support / check-examples（每类型有用例 + 文档数字）/ check-dispatch / check-layers。
4. `cd designer && npx tsc --noEmit && npx vite build` 全绿。
5. `examples\gallery\gallery.exe --trace` 打印的控件树里有 `iconlist`，绑的 select/check/dblclick 事件与 k_kinds 承诺一致（无「不支持」警告）。
6. 目视取证：`tools\shot-widget.ps1`（或 shot.ps1）截 iconlist 放大图 —— 图标、两行文字、右侧状态、勾选框、选中高亮、深浅主题切换都正确，无残影/垃圾像素（双缓冲画满规矩）。
7. 文档定量事实：`44 种控件 → 45` 三处同步、支持库条数同步，由 check-examples / check-support 机器判定（不是手写核对）。

### 1.4 非目标（本轮不做）

- 分组标题行（截图里灰底「程序装的（5）…收起」那种）、行内按钮/超链接、图标缓存 LRU 全局表、水平滚动、多选（ctrl/shift 范围选）、拖拽排序。
- 从 exe/DLL 提取图标（`ExtractIconEx`）——不引入；图标只认 png/jpg/bmp/gif 路径。
- toolbox 示例接入（gallery 先落地用例；toolbox 换装放遗留，见 §6）。
- 画布上按真实数据渲染两行文字的像素级还原 —— 画布只要「壳 + 前 N 行示意」与类型/事件/字段契约齐。

### 1.5 能改什么 / 不能改什么

- **能改**：`runtime/src/core`（新文件 + 类型表/枚举加行）、`runtime/src/win32`（新文件 + class_of/style_of/注册加行）、`runtime/include/cstackgui.h`（新 API 分节）、`designer/src`（kinds/events/WidgetView/supportlib/glyph 样式）、`docs/`（类型表行/字段表/数字口径）、`examples/gallery`（用例）、`tests/unit`（新测试文件）、支持库条数所在文档。
- **不能改**：现有控件的类型表行与行为（listbox/table/tree 一律不动）；`check-dispatch.mjs` 锚定的既有 token（`sync_widget_body`、`ImageProc` 等函数体内不许删被 must 的词）；不删任何现有文件；不动 `.codebuddy/skills/`；不碰 `packaging\check-format.bat`（打包副本）。

---

## 2. 改动清单（分步 · 每步自带可失败的验证）

> 编辑纪律（memory：同轮多编辑同一文件会丢改动）：同一文件的多处改动放同一轮做完，每步结束用 `git status --short` + 抽查行数确认落盘，再进下一步。

### S1 runtime 类型注册（骨架）

- 改：`runtime/src/core/cg_doc.h`（`CG_K_DIALOG` 后、`CG_K_COUNT` 前加 `CG_K_ICONLIST`）
- 改：`runtime/src/core/cg_doc_kind.c`（k_kinds 表尾 dialog 之后加一行：`iconlist / CG_K_ICONLIST / 非容器 / 240×180 / "" / CLICK|DBLCLICK|SELECT|CHECK|GOTFOCUS|LOSTFOCUS`）
- 改：`runtime/src/win32/cg_win_internal.h`（`CG_ICONLIST_CLASS` 类名常量 + `cgw_register_iconlist_class` 声明）
- 改：`runtime/src/win32/cg_win_ctl.c`（`class_of` 加 `iconlist → CG_ICONLIST_CLASS`；`style_of` 加 `WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL`）
- 改：`runtime/src/win32/cg_win_create.c`（懒注册：`if type=="iconlist"` 调注册函数）
- 新：`runtime/src/win32/cg_win_draw_iconlist.c`（空壳：类注册 + WndProc 只处理 `WM_ERASEBKGND return 1` / `WM_PAINT` 画清底帧）
- **验证**：`build-runtime.bat` 编译过；`tools\run-tests.bat` 里 `test_kind`（按 `CG_K_COUNT` 自动遍历）全绿 —— 反例：故意写错枚举名应编译失败（由编译器兜）。

### S2 数据模型 + C API（core 侧）

- 新：`runtime/src/core/cg_widget_iconlist.c`（行结构数组 + `cg_iconlist_add / addf / clear / count / set_checked / checked / set_status`；类型校验用 `want_type`，失败记日志返回；每次变更 `items_dirty=1` + `cg_widget_sync(w)`）
- 改：`runtime/src/core/cg_internal.h`（iconlist 行结构 `cg_ilrow_t {icon,title,sub,status,checked}` + widget 字段 `il_rows/il_n/il_cap`，插在 per-type 字段段；复选框开关 `il_checkbox`）
- 改：`runtime/src/win32/cg_win_sync.c`（`items_dirty` 分支加 `else if "iconlist"`：清 dirty + `InvalidateRect`，仿 progress「自绘只触发重绘」先例）
- 改：`runtime/include/cstackgui.h`（新分节注释 + 6 个原型，Doxygen 风格；速查清单段补一行）
- **验证**：新 `tests/unit/test_iconlist.c` —— 正例（add/count/checked/clear 后归零）、反例（对 `listbox` 调 `cg_iconlist_add` 不写脏数据）；`tools\run-tests.bat --grep=iconlist` 必须红→绿各一次（先不写测试跑一次确认能红）。

### S3 自绘绘制

- 改：`runtime/src/win32/cg_win_draw_iconlist.c`
  - `WM_PAINT` → `cgw_paint_buffered` 双缓冲；**先 `cgw_clear_to_behind` 画满 rc**（memory 规矩，留白/行间距否则垃圾像素）
  - 每行：checkbox（矩形+勾，皮肤取色）/ 图标（`cgw_load_image_file` 惰性加载，行内缓存 HBITMAP + `bmp_src` 路径判重载，失败画占位框）/ 主标题（单行 `DT_END_ELLIPSIS`）/ 副标题（灰色小字）/ 状态（右对齐）/ 选中高亮行背景 / 焦点框
  - 主题色一律绘制时从 skin getter 取（深浅主题切换自动正确）
  - DPI：尺寸/坐标走现有 scale 折算（抄 chart 的做法）
- 改：`runtime/src/core/cg_widget_iconlist.c` 或控件销毁路径（`cgw` 层）—— 释放行字符串 + `DeleteObject` 全部缓存位图
- **验证**：S4 的 gallery 用例 + `tools\shot-widget.ps1` 放大截图目视（图标不糊、两行不重叠、状态右贴边、无残影）；`tools\run-tests.bat --memcheck`（动了 HBITMAP/strdup，必须叠堆记账）。

### S4 交互与事件派发

- 改：`runtime/src/win32/cg_win_draw_iconlist.c` WndProc
  - `WM_LBUTTONDOWN`：命中测试（checkbox 区 → 切 `checked` + `cg_dispatch(CHECK)`；行 → 写选中下标 + `cg_dispatch(SELECT)`，`SetFocus`）；`WM_LBUTTONUP` → `cg_dispatch(CLICK)`（`cg_dispatch` 按系统双击间隔自动合成 DBLCLICK）；坐标折算逻辑像素 `*100/scale`（抄 canvas:523-553，**必须 `g_win_in_notify` 包住**）
  - `WM_VSCROLL` / `WM_MOUSEWHEEL`：`SetScrollInfo` + 位移 + `InvalidateRect`（自绘 WS_VSCROLL 无先例，DefWindowProc 不滚内容，这是本计划最大不确定点 → 见 §5 回滚）
  - `WM_KEYDOWN`：VK_UP/VK_DOWN 移动选中（滚动保证可见）+ VK_SPACE 切换勾选 + VK_RETURN 派发 CLICK；`WM_SETFOCUS/WM_KILLFOCUS` 若子类统一路径未覆盖则自行派发 gotfocus/lostfocus
- 改：`tools/check-dispatch.mjs`（RULES 加一条锚定 `IconListProc` 必含 `CG_EVENT_SELECT`/`CG_EVENT_CHECK`/`CG_EVENT_CLICK`，让事件落地被机器兜住）
- **验证**：`gallery.exe --trace` 看控件树与事件绑定；真机探针（serial_verify 口径）：点行出 select、点勾选框出 check、双击出 dblclick —— 由 gallery 日志/grep 断言，正反例（不点就不该有输出）。

### S5 .cgui 解析 + 字段往返

- 改：`runtime/src/core/cg_doc.c`（`parse_node` 读 `checkbox` 布尔 → `il_checkbox`；`items` 字符串数组**复用现成 `parse_str_array:838`** 建 title 行；dump 自检段 `:1198` 补 `ilrows=` 计数）
- 改：`designer/src/model/serialize.ts`（写出 `checkbox`；`items` 已有 `:212` 自动带）
- 改：`designer/src/model/parse.ts`（对称读入 `checkbox`）
- **验证**：`tools\check-format.bat` 的 gallery roundtrip（恒等回写必须不丢 `checkbox`）—— 反例：故意不改 serialize 跑一次应红。

### S6 designer 画布三表 + 渲染 + 支持库

- 改：`designer/src/model/kinds.ts`（listbox 附近加 `iconlist` 条目：label 列表（带图标）/ group 与 k_kinds 对齐 / 240×180 / `listLike:true`）
- 改：`designer/src/model/events.ts`（`KIND_EVENTS` 加 `iconlist: ["click","dblclick","select","check","gotfocus","lostfocus"]`，与 k_kinds 逐字一致）
- 改：`designer/src/components/WidgetView.tsx`（`case "iconlist"`：壳 + 前 6 行示意，抄 table 的做法）
- 改：`designer/src/supportlib.ts`（6 条 `cg_iconlist_*` 条目，抄 `cg_table_set_row_icon:569` 结构）
- 改：glyph 样式文件（`.glyph-iconlist`，先 grep `glyph-meter` 定位 css）
- 改：`examples/教学使用说明.md`（「支持库（N 条）」数字 —— check-support 机器判，见 memory：手写定量事实必须绑门禁）
- **验证**：`cd designer && npx tsc --noEmit && npx vite build`；`node tools\check-support.mjs`（反例：条目写了头文件没有的函数名应红）。

### S7 文档

- 改：`docs/CGUI-FORMAT.md`（§4 控件类型表加 `iconlist` 行；字段表加 `checkbox` 行；`items` 字段说明补 iconlist）
- 改：数字口径三处 `44 → 45`：`packaging/README.release.md:19`、`docs/ARCHITECTURE.md:167`、`examples/教学使用说明.md:353`（可选：`docs/articles` 两处旧「42」一并对齐）
- **验证**：`node tools\check-examples.mjs --docs`（数字不齐必红）。

### S8 示例落地（gallery 用例）

- 改：`examples/gallery/gallery.cgui`（加 `iconlist` 控件 + `checkbox:true` + `items` 预置几条标题 + bind select/check/dblclick）
- 改：`examples/gallery/main.c`（回调里 `cg_iconlist_add` 灌 icon/title/sub/status 完整行 + 日志输出事件，自检模式断言行数）
- **验证**：`check-examples`（每类型有用例）+ `gallery.exe --selftest examples\gallery\gallery.cgui` + `--trace` + 截图取证（验收 5、6）。

### S9 门禁全套 + 取证（P3）

- 顺序：`build-all.bat` → `tools\run-tests.bat`（含 `--memcheck`）→ `tools\check-format.bat` → `cd designer && npx tsc --noEmit && npx vite build` → 截图
- **每条判定读日志/退出码文件，不信回显**（SOP 三条铁律）；红一条回对应步骤修。

### S10 审查 + 提交（P4/P5）

- `git diff HEAD` → 按 §4 路由加载审查技能（新增 .c：firmware_code_style、buffer/error/memory（HBITMAP/strdup）、struct_best_practice、design-taste（界面观感）、designer-modify（画布三表））→ 三角色审查 → 报告 `.codebuddy/code-reviews/2026-10-12-iconlist.md`
- 分批提交：①runtime 核心 ②designer+docs ③examples+tests ④门禁锚定（check-dispatch 规则），格式 `[类型][模块] 描述`

---

## 3. 边界 / 异常清单（实现时逐条过）

| # | 边界 | 处置 |
|---|---|---|
| 1 | icon 路径不存在 / 加载失败 | 画占位框（仿 `image_failed`），不崩不刷屏日志 |
| 2 | 空列表 | 双缓冲清底即可，不画任何行 |
| 3 | 超长 title / status | 单行 `DT_END_ELLIPSIS` 裁剪；status 右对齐不越界 |
| 4 | 行数 > 可视区 | 滚动条 range/page/pos 同步；选中项滚动保证可见 |
| 5 | API 越界行号 | 返回/忽略 + 不写脏内存 |
| 6 | 对非 iconlist 控件调 API | `want_type` 拒绝（反例进单测） |
| 7 | 控件销毁 | 行字符串全释放 + 全部缓存 HBITMAP `DeleteObject`（--memcheck 兜） |
| 8 | 深浅主题切换 | 色在绘制时取 skin getter，切主题后 Invalidate 即正确 |
| 9 | `.cgui` 只给 `items` 不给 icon/sub/status | 行只显示 title，图标占位框 → gallery 演示此最小用法 |
| 10 | `items` 预置 vs `cg_iconlist_add` | 语义 = parse 建 title 行、add 追加、clear 全清；文档写明 |
| 11 | 双击判定 | 依赖 `cg_dispatch` 的 click→dblclick 合成，自己不再判时间 |
| 12 | DPI 缩放 | 逻辑像素与物理像素换算抄 chart/canvas 现成做法，坐标入事件前 `*100/scale` |

---

## 4. 回滚方式

- 逐步提交（S1..S8 按批），任何一批门禁红 → 只 revert 该批涉及文件（`git checkout -- <file>` 未提交时；已提交则 `git revert <hash>`），不整树回退。
- 最大不确定点 S4 的自绘 `WS_VSCROLL`：若两天内啃不动 → 降级方案 = 不带原生滚动条，改用「行高×行数裁剪 + `WS_VSCROLL` 仅占位」或仿 terminal 的滚动实现；仍不行则回退 S4 只保留滚动条拖动（保留验收 1~3、5~7，滚动记遗留）—— **降级必须写回本计划并说明**。

## 5. 禁止改动面（重申）

现有类型表行 / listbox・table・tree 行为 / `check-dispatch` 既有锚定 token / 既有示例与 .cgui（gallery 只**加**控件不改旧控件）/ `packaging\check-format.bat` / 不删文件 / 不动 skills。

---

## 6. 进度段（换会话续跑先读这里）

- [x] P0 上下文：两轮 code-explorer 挖底
- [x] P1 计划：本文件
- [x] S1..S9 实现与门禁（全部通过；build-all/check-format 有 3 处**既有红**由 2026-10-12 `temp` 提交 657cc2c 的 examples/base 半迁移造成，与本改动无关，逐条证据见审查报告「既有红」节）
- [x] S10 审查 + 提交：报告 `.codebuddy/code-reviews/2026-10-12-iconlist.md`；提交 5 批
      6f56f94（运行时+单测+契约）/ 3bb2544（画布）/ 9c7e4d7（示例）/ b31a05e（文档）/ 7601b39（计划与报告）
- **已 Converged**（spec §1.3 逐条核对：1~7 全过，第 1/3 条带既有红说明）
- 计划偏离（2 处，均已就地说明）：
  1. API `set_status` → 实现为更通用的 `cg_iconlist_set_row`（一次改整行，勾选态保持）；
  2. 计划外新增 1 条 check-dispatch 锚定（`cgw_create_native` 的 GWLP_USERDATA 白名单）——
     实现时真踩了"新自绘控件漏登记 → 首帧兜底灰"（截图+探针日志定位），按机器兜底原则就地钉住。
- 遗留（本轮不做）：toolbox 接入 iconlist（`lst_recent` 换装）、exe/DLL 图标提取（ExtractIconEx）、
  分组标题行、ctrl/shift 多选；截图探针脚本在 `build\shot-page2.ps1` / `build\shot-ilpage.ps1`（含
  mouse_event 必须先 SetCursorPos、gallery.exe 单文件 RC 内嵌 .cgui 优先于磁盘文件两条坑）
