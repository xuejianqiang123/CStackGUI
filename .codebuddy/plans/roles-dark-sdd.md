# SDD：完整深色皮肤 —— G2 内置皮肤深色化（T1）+ G1 色彩角色（T2/T3）

> PRD：`.codebuddy/plans/roles-dark-prd.md`（决策 D1 劈两刀 / D2 roles{} + @引用 / D3 挂引用+钩子求值）。
> 本文件把三片钉成"照着抄就能做"。地基证据行不再重复引用处均出自 PRD §0 与本轮补充调研
> （调研记录见对话归档；关键点在文内就地标注 文件:行）。

---

## 1. 契约形状（T2 落地时写进 CGUI-FORMAT §1 与新章节）

顶层新增可选键 `roles`（与 `styles`/`actions` 平级）：

```json
"roles": {
  "accent": { "light": "#6750A4", "dark": "#D0BCFF" },
  "danger": { "light": "#B3261E" }
}
```

| 规则 | 内容 |
|---|---|
| 形状 | 键 = 角色名（非空字符串）；值 = 对象，字段只有 `light` / `dark` 两个颜色字符串（均可选，但至少一个有意义） |
| 引用 | 写在**现有颜色字段的位置**：`"fg": "@accent"` / `"bg": "@accent"` / `"app.bg": "@surface"`。`@` 开头即引用，其余仍按颜色字面量解析（颜色字面量只认 hex，`@` 无歧义、无转义问题） |
| 求值 | 运行时按**当前模式**取 `light`/`dark`：`app.dark` 显式 true/false 定模式；`"auto"` 跟随系统（WM_THEMECHANGED 已有重算钩子） |
| 容错（与 styles{} 同口径，不报错不中断） | `@` 指向不存在的角色 = 该字段当没写；角色值非法 = 该字段跳过；角色的某模式值缺失 = 该模式下的引用当没写；**重名后者覆盖**（倒序查找，仿 `style_find` `cg_doc.c:181-188`） |
| alpha | 角色值接受 `#AARRGGBB`，消费端与现有 fg/bg 完全同口径（GDI 大多不透明），**角色不做例外** |
| 画布预览 | 画布按**当前模式**显示对应值（`app.dark` 显式值定模式；`auto` 用 `matchMedia('(prefers-color-scheme: dark)')`）；深色下的**运行时真实观感**仍以跑起来为准（§1.1 取舍不变） |
| 不覆盖 | colorpicker 的色块画的是 `w->value`（当前颜色语义），不读 fg/bg——**明确排除**（`cg_win_ownerdraw.c:96-103`） |

## 2. T1：G2 内置皮肤深色化（纯运行时，零契约）

**深色系既有 6 值**（提案必须优先复用）：面 `0x141218`、面上 `0x1D1B20`、字 `0xE6E0E9`、
槽/框 `0x2E2B33`、描边 `0x3D3945`、禁用/点 `0x49454F`（+led 灭点 `0x4A4750`）。
品牌紫**保持不跟随**（`cg_skin.h:64-65` 口径）：button/check/switch 选中态、slider 拇指、progress/圆环值弧默认紫。

### 2.1 深色提案对照表（✅=既有值，🆕=需新定）

| 部位 | 现状浅色 | 深色提案 |
|---|---|---|
| button 默认填充/字 | 0xEADDFF / 0x1D1B20 | **保持**（品牌紫） |
| button 禁用 fill/空心底/字 | 0xE2E2E4 / 0xECECEE / 0x9A9A9E | 🆕 `0x2E2B33` / `0x2E2B33` / `0x6E6E72`（禁用字有先例 sk_default_text，`cg_skin.c:33-39`） |
| check/radio 常态框/边 | 0xFFFFFF / 0x79747E | ✅ `0x1D1B20` / `0x49454F`；禁用框 ✅ `0x2E2B33`；选中紫保持 |
| switch off 轨/禁用轨 | 0x9A949E / 0xC9C5D0 | ✅ `0x49454F` / `0x3D3945`（钮保持白，画在紫上） |
| progress 槽/描边 | 0xE6E1E9 / 0xCAC4D0 | ✅ `0x2E2B33` / `0x3D3945`（填充紫或 style.fg 保持） |
| 圆环轨 | 0xE0DAE6 | ✅ `0x2E2B33` |
| meter 底弧/刻度/指针帽 | B4B0BB / 9A96A2 / 49454F | ✅ `0x3D3945` / `0x49454F`；🆕 指针+帽 → `0xE6E0E9`（深底上原深灰不可见） |
| led 灭点 | 0xC0BCC6 | ✅ `0x4A4750` |
| chart 网格/外框 | 0xE7E0EC / 0xB4B0BB | ✅ `0x2E2B33` / `0x3D3945` |
| slider 槽/拇指 | 0xE7E0EC / 0x6750A4 | 槽 ✅ `0x2E2B33`；拇指保持紫 |
| edit 边框 缺省/soft/line | B8B2BD / CAC4D0 / 79747E | 🆕 缺省灰 → `0x49454F`；soft/line ✅ `0x3D3945` / `0x49454F`；聚焦紫环保持 |
| edit 客户区底/字 | COLOR_WINDOW 白 / 系统 | 🆕 底 `0x1D1B20`（richedit 先例 theme.c:184）+ 字 `0xE6E0E9`——**两处拦截**见 2.2 |

实现方式一律走 `cg_skin_neutral(app, light, dark)` 的既有模式（新增 dark 值塞进该对），
**不改函数签名、不加新宏文件**；`CG_DARK_*` 三宏不动。

### 2.2 edit 的两处拦截（缺一不可）

1. `cgw_ctlcolor_brush`（`cg_win_ctl.c:534-568`）：edit 无 style.bg 时今天 `return NULL`（系统白底）。
   深色下改为返回 `cgw_brush_for(SURFACE_2)` + `SetTextColor(ON_SURFACE)`；有 style.bg/角色引用的照旧走用户值。
2. `cg_skin_edit.c:455-457`：`s->bg` 建控件时缓存（边框带补底用），取色同步改（dark → SURFACE_2）；
   且它是**应用期缓存**——`cgw_theme_apply` 钩子里重调 `cg_skin_apply_edit`（或直接刷 s->bg），否则切深浅后边框带与客户区两截对不上。

### 2.3 缓存与浅色零变化核对单（T1 验收清单）

- behind 采样缓存：apply 里已有 `cgw_clear_behind_cache`（theme.c:227）✓ 不新增
- `cgw_brush_for` 按值缓存：新色=新 key ✓ 不失效
- 字体缓存无颜色成分 ✓ 不动
- **浅色零变化**：所有改动都在 `dark` 分支 / neutral 对的 dark 槽内；浅色分支一行不碰（A3）
- 验收：`examples/` 里带自绘控件的工程（gallery）深浅各一张截图对照；lesson01（edit 全功能）深浅各一张

## 3. T2：G1 运行时（契约 + 解析 + 求值）

### 3.1 结构与解析

- `cg_role_t { char *name; uint32_t light, dark; uint8_t has_light, has_dark; }`；
  `cg_doc_t` 增 `cg_role_t *roles; size_t nroles;`——**数组形态，仿 `cg_named_style_t`**（`cg_doc.h:192-196`）。
- 解析 `parse_roles(const cj_val_t *o, size_t *count)`：仿 `parse_named_styles`（`cg_doc.c:274-297`）——
  非对象/空 → NULL；空名跳过；值非对象跳过；light/dark 过 `cj_color`，解析失败 = 该槽"没有"；
  重名不查重，靠 `role_find` **倒序查找**实现后者覆盖。
- `cg_doc_free` 增 roles 释放（仿 styles 的 `cg_doc.c:963-969`）。
- selftest 计数：`role_missing` / `role_missing_name`（仿 `style_missing`，`cg_doc.h:394-400`）——
  只 WARN 不失败，与 styles 同口径。

### 3.2 引用的挂载与合并

- `cg_style_t` 增 `char *fg_role, *bg_role;`；`style_mask` 取 **bit7=FG_ROLE、bit8=BG_ROLE**（bit7 起空闲，`cg_doc.h:182-190`）。
- 解析：`style_overlay` 的 fg/bg（`cg_doc.c:137-152`）在 `cj_color` 失败且值以 `@` 开头时 →
  存角色名进 `*_role` + 置对应 ROLE 位（值槽留 0）；`app.bg`（:782）同型处理（doc 级引用）。
- **合并传播**：`style_merge_named`（:226-239）是"没写才填"按位判断——扩展为：
  `if (!(mask & FG) && !(mask & FG_ROLE))` 时，若 base 带角色引用则**连串带位一起落**到合并结果
  （角色引用语义上就是"写了 fg"）。字符串复制/free 与 font 同路径。
- free：`cg_style_free`（若无可先确认现有 font 的释放点）增两个角色名串；动态控件路径同样核对。

### 3.3 求值（挂进现成钩子，消费端零改动）

- 新函数 `cgw_role_eval(const cg_app_t *app, const char *name)` → `uint32_t`（取不到返回 `CG_COLOR_NONE`）：
  查 `app->doc.roles`（倒序）→ 按 `app->dark` 取 light/dark。
- 插入点：`cgw_theme_apply`（`cg_win_theme.c:213-236`）**开头**加一遍"全控件角色求值"：
  遍历 `app->widgets`，`w->style` 带 ROLE 位的字段写回具体值（**引用串保留**，下次 apply 再求值——
  这样 set_dark/WM_THEMECHANGED/首建都自然正确）。首次建窗（`cgw_win_init` 建树后、READY 前）调一次 apply 同款求值。
- **消费端（皮肤 15+ 读取点）零改动**——D3 决策的核心收益。
- app.bg 的角色引用：`cgw_theme_window_bg`（:123-128）的 doc.bg 取值处补"是引用则求值"。

### 3.4 单测 `tests/unit/test_roles.c`（自动进 run-tests，仿 test_style.c 四套件）

- 形状解析（条数/空名跳过/非法值跳过/重名后者覆盖）
- 引用挂载（mask 位、串、@不存在=当没写、值非法=跳过）
- 合并穿透（命名样式带 @、内联覆盖角色、内联值压过引用）
- 求值（显式 true/false 各取对应槽；缺槽=当没写）

## 4. T3：G1 画布

| 步 | 文件:位置 | 改什么 | 验收 |
|---|---|---|---|
| T3.0 | 新 `build/probe-style-consistency.mjs` | resolveStyle（画布）vs 模拟运行时合并（按契约同规则跑一遍）对拍——**先补网再动 parse**（A8） | 探针绿 |
| T3.1 | `parse.ts:179-198`（colorOf） | `@名字` 放行为字符串值保留；其余非 hex 仍丢（A6 红绿：放行前保存一次即丢=红） | 探针/往返 |
| T3.2 | `types.ts` CgStyle | 无需改类型（fg/bg 本就是 string，`@x` 是合法字符串）；补注释"颜色值可为 @角色引用" | tsc |
| T3.3 | `WidgetView.tsx` | `st.fg/bg` 命中 `@` 时查 `doc.roles` + 当前模式（`doc.dark` true/false；`auto`/缺省用 matchMedia）取 CSS 色；**缺角色 = 当没写**（与运行时同口径） | A7 截图 |
| T3.4 | `StyleSection.tsx:307/333/351`、`StyleDialog.tsx:144/156`、`WindowPanel.tsx:155` | 颜色输入旁加"角色"下拉（roles{} 的键 + 无）；选定即把字段写成 `@名`；清空恢复字面色 | 目视 |
| T3.5 | 往返门禁 | `format-roundtrip.mjs` 给 styles 探针控件挂一条 @角色引用（roles{} 一并塞进文档），恒等回写核对 | A6 绿 |

## 5. 验收映射（PRD §3 → 切片）

A1→T1/T2 全程老文件回归 ｜ A2/A3→T1 截图 ｜ A4→T2 真机 ｜ A5→T2 单测 ｜ A6→T3.1/T3.5 ｜ A7→T3.3 ｜ A8→T3.0

## 6. 边界（写进契约的话）

- 系统经典控件染色不到的部分：菜单（无色控）、listbox/table/tree 的 Win11 深色失效（`cg_win_theme.c:63-67`）——
  edit 通过 2.2 自染解决，其余明说"平台边界"。
- chart 系列/terminal 行色不在 G1 引用面（T4 可选：挂**内置**角色）。
- 画布深色**实时**预览仍不做；"按当前模式取值"是静态对齐。

## 7. 进度

- [x] P0/P1：PRD（`roles-dark-prd.md`）
- [x] P2 SDD：本文件（T1 对照表 + edit 双拦截 + T2 结构/合并/求值 + T3 接线点）
- [x] P3a **T1 落地（2026-10-07）**：按钮禁用态 / meter 底弧·刻度·指针帽 / edit 边框带·客户区底
      全部走 `cg_skin_neutral` 或深色分支（新定深值 4 处：0x2E2B33 禁用底、0xE6E0E9 指针帽、
      edit 边框 0x49454F、edit 底/字 0x1D1B20/0xE6E0E9）+ `cg_skin_edit_retheme`（s->bg 是建控件时
      缓存的，apply 钩子重算）+ WM_CTLCOLOR 深色 edit 分支（两处拦截，SDD §2.2）。
      **验收**：`tools\gen-darkaudit.mjs` 造一页深色审计工程，BEFORE/AFTER + 浅色变体三张截图
      —— BEFORE 里禁用按钮是深底上发光的浅灰块、edit 纯白、meter 指针不可见，AFTER 全部修复；
      浅色变体逐控件与既有浅色皮肤一致（A2/A3 ✓）。
      **并行协作记录**：所有者同日在库上提交了 `d9cb449`（补 retheme 头文件声明——与本工作区
      编辑收敛到同一行）及 `6ba4374`/`98f8d0b`（画布功能）；本片提交 `6f8c67d` 只含运行时实现
      与调用 + 重编的 darkterm/gallery exe，零冲突。README/契约深色口径已按事实改写（剩余边界：
      品牌紫有意保持、用户写死色等 G1、菜单/清单平台边界）。
- [x] P3b **T2 落地（2026-10-07）**：`cg_role_t`（数组仿 named_style）+ `parse_roles`（容错同口径，
      两槽都缺的条目跳过）+ `style_overlay` 的 `@引用` 分支（值槽 NONE + 串 + mask 位 7/8）+
      `style_merge_named` 传播扩展（内联值/引用都没写时，命名样式的引用**连串带位**落下）+
      `cg_role_lookup` 纯函数（core 层，单测可测）+ 求值挂 `cgw_theme_widget`（建控件/切主题同一条路）
      与 `cgw_theme_apply` 顶部的 app.bg/窗口同步 + 失效引用 WARN（含 app.bg）。
      **验收**：`test_roles.c` 四套件（形状/引用挂载/合并穿透/求值口径）全绿——首跑红过一次
      （表含 dup 两条、覆盖发生在查找时——断言写错，与 test_style 的 dup×2 同口径修正）；
      真机 A4：审计工程加 `roles` + 三个 @控件，深浅截图各取对应槽
      （accent 浅紫/深紫、danger 红/浅红、@按钮同拍）。**A4/A5 ✓**。
      run-tests `all checks passed` + check-format `BAT_EXIT=0`。
- [x] P3c **T3 落地（2026-10-08）**：T3.0 一致性探针（`tools\probe-style-consistency.mjs`，契约文字
      独立实现对拍 resolveStyle——首跑抓到**探针自己把优先级写反**，修正后全绿；红证：parse 修复前
      @ 被静默丢、roles{} 整表消失）→ T3.1 colorOf 放行 `@`（A6）→ **类型/parse/serialize 接 roles{}**
      （SDD 原表漏了这项：画布模型根本没有 roles 字段，手写 roles 的文件打开再保存会被**整表静默删**，
      比 @ 丢失更重——types/parse/serialize 三处补齐）→ T3.3 WidgetView 按模式取色
      （`doc.app.dark` 显式值定模式，auto 读 matchMedia——首写误作 `doc.dark`，tsc 抓住）→
      T3.4 角色下拉（StyleSection 文字/背景两行；**StyleDialog 与 WindowPanel 的下拉未做**，
      见残留）→ T3.5 往返门禁加 roles/@ 探针（gallery 实测：roles 与 @引用恒等无损 +
      `selftest OK`——运行时接受画布写出的带 roles 文件，契约闭环）。
      **验收**：A6 ✓（红绿）A7 ✓（深浅两态画布截图，@ 控件各取对应槽）A8 ✓（对拍探针）。
      run-tests / check-format / probe-dialog / 一致性探针全绿。
      **残留**：StyleDialog / WindowPanel 的角色下拉、roles{} 表的画布编辑器（手改 .cgui +
      热重载可用）、`doc.app.dark` 变化不触发画布重渲染（静态对齐的已知边界）。
- [x] P5 **角色编辑器收尾（2026-10-08，`be58e05`）**：SDD 残留全清——
      `RolesDialog.tsx`（roles 表 CRUD：改名连带 `renameRoleRefs`——控件 style / 命名样式表 /
      app.bg 三处 @引用一起改；引用计数 `countRefs().roles`；复制/删除/空态说明，
      手感与 StyleDialog 刻意一致）+ 工具栏「色彩」按钮 + StyleDialog 样式卡与 WindowPanel
      窗口底色的角色下拉（T3.4 残留清偿）。`roleColor` 收进模型层 style.ts（三处共用，
      Node 探针无 window 时当浅色）。
      **模型层事实修正**：画布模型没有（也不需要有）fg_role/bg_role 字段——@引用就是
      fg/bg 字符串值本身；C 侧的伴生字段是 uint32 存不下名字才需要的。refs.ts 第一版
      按错误形状写（tsc 抓住 11 处），重写为"@ 字符串判定"。
      **计数与改名口径对齐**：命名样式表里的 @引用也算引用点（否则卡片说「未被引用」、
      改名却动了样式表，自相矛盾）——countRefs 与 renameRoleRefs 同口径，探针断言钉死
      （accent=2：控件 fg + 命名样式 fg；danger=2：控件 fg + 命名样式 bg）。
      tsc / vite / 一致性探针 / probe-dialog / check-format 全绿。
      **未做**：RolesDialog 的截图验收（镜像 StyleDialog 的成熟模式，结构靠 tsc 保证）；
      Canvas.tsx / tree.ts 的未提交改动是所有者并行工作，未纳入本片提交。
- [x] P6 **F2-T4 前半：非模态子窗体（2026-10-08，`494b334`）**：加法式 API
      `cg_dialog_open_nonmodal`（不动已交付的模态签名；同一时间一个对话框、模态或非模态合计——
      `any_dialog_open` 守卫统一进两个 open）；非模态 = 不禁用主窗口、不进内层泵，生命周期到
      WM_DESTROY；宿主解析：模态走 modal_dlg 快路径，非模态在 WM_CLOSE/WM_DESTROY
      （低频消息）按 HWND 反查（`cgw_dialog_by_hwnd`，全树 O(n)）——**窗口级 move/size/activate
      v1 不派发**（控件级与关闭路径不受影响，契约 dialog 行已写明）。
      **验收**：modaltest 加 OpenNM 按钮 + `on_open_nm`，探针 phase3 六项
      （打开即返 ✓ owner 保持可用 = A2 反命题 ✓ OK 走 cg_dialog_close 关闭 ✓ 主窗存活 ✓
      nm result=1 ✓）——全序列 **27 项 ALL PASS**（模态 A1~A5 回归 + 非模态 A6）。
      run-tests / check-format / probe-dialog 全绿；darkterm/gallery 重编换新库。
      **T4 后半（"引用另一个 .cgui"多文件组装）仍推后**——那是另一个契约 epic。
