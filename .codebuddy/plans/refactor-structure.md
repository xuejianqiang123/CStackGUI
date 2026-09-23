# 结构审查与拆分计划（2026-09-23）

> 触发：用户要求以架构师视角审视 `runtime/` `tests/` `designer/`（"拆长子模块、分层、建合理文件夹"）。
> 方法：两份只读结构测绘（runtime 一份、designer 一份，逐条给 `文件:行号`）+ 我核对关键结论。
> **口径说明**：本文行数 = 文件总行数（含空行）。`Get-Content | Measure-Object -Line` 得到的是
> "非空行数"，会低 10%~12%（如 `cg_win_draw.c` 总 1227 / 非空 1098）。

## 0. 结论（先看这段，后面的都是论据）

1. **目录结构不用动。** 四层 `core / win32 / skin / kit` + 公共头 + 资源的分法是对的，
   而且**实测经得起检查**：core 的 .c 没有一个 include win32/skin 的头（反向依赖零）；
   15 个 win32 文件一律只 include `cg_win_internal.h`，没有互相 include 兄弟头。
   跨层调用全走内部契约头（`cg_internal.h` / `cg_win_internal.h` / `cg_skin.h`），
   core 里对 `cg_win_*` / `cg_serial_*` 的引用是**受控端口**（后端 vtable + 钩子声明）。→ **不新建目录。**
2. **该拆的只有 3 个文件**（一文件多职责），其余大文件是"单一职责只是长"，**拆了反而更差**。
3. **比拆文件更值的是两类收敛**：① 后端用字符串比 `w->type` 分派约 **200+ 处**，而 `CG_K_*` 枚举
   与类型表**早就存在**（且 `w->kind` 字段还不存在）；② 基础原语重复实现（UTF-8↔宽字符 **6 份**、
   RGB 打包 4 份、`* scale / 100` 内联 **17 处**）。
4. **分层现在是"约定"没有"门禁"** → 加一道静态检查（本轮已加，§4）。
5. `tests/` 无需动作（14 文件 / ~2.8k 行，最大 419）。
6. **顺带发现一个真 bug**（画布菜单项点了没反应，§5.1）。

---

## 1. 该拆的（按风险从低到高）

> **利好前提（关键）**：`build-runtime.bat` 用通配 `for %%f in ("runtime\src\<层>\*.c")` 遍历源码，
> `*.h` 也走通配 → **新增 .c 不需要改任何清单**。唯一硬约束：目标 `.o` 用文件基名（`build-runtime.bat:62`），
> 所以**四个目录下不得出现同名文件**，新文件名要全局唯一。

### 1.1 `win32/cg_win_draw.c`（1227 行，**9 个自绘窗口类**）—— 风险最低，建议先做
纯搬移：**零接口改动、零 static 助手提升**（跨文件符号早就在 `cg_win_internal.h:57-65,163-173`）。

| 目标文件 | 收哪些（总行号区间） |
|---|---|
| `cg_win_draw_canvas.c` | Image 9-110、Ring 111-177、Progress 178-225、Canvas 226-358 |
| `cg_win_draw_panel.c` | Panel（group/box/card/shape）359-537、Splitter 538-657 |
| `cg_win_draw_chart.c` | Chart 658-913 |
| `cg_win_draw_gauge.c` | Led 914-1035、Meter 1036-1227 |

风险点：`s_split_origin`（:538）是文件级 static，必须随 Splitter 段走（只有 Splitter 用，安全）；
各注册函数里的 `static int done` 是函数内 static，随函数走即可；基名唯一性已满足。

### 1.2 `core/cg_widget_data.c`（1149 行，**≥12 个内容组**）—— 风险中
| 目标文件 | 收哪些 |
|---|---|
| `cg_widget_list.c` | list 26-103 + status 341-379 + menu 425-447 + title/quit/has_window/cancel_close/dump_doc 449-479 + hint/period 482-522 + `cg_widget_add` 525-576 + image 407-424 |
| `cg_widget_tree.c` / `cg_widget_table.c` / `cg_widget_tabs.c` | tree 119-211 / table 212-340 / tabs 381-405 |
| `cg_widget_split.c` | splitter 578-680 |
| `cg_widget_term.c` | terminal 682-907 |
| `cg_widget_gauge.c` | chart 909-1052 + led/meter 1054-1149 |

**必须提升到内部头的 static 助手：`want_type`（`cg_widget_data.c:105`）** —— 被 8 个组共 22 处调用。
其余组内 helper（`term_*` / `chart_*` / `split_*`）留各自文件私有。

### 1.3 `kit/cg_kit.c`（857 行，**≥4 个子系统**）—— 风险中低
| 目标文件 | 收哪些 |
|---|---|
| `cg_kit_fs.c` | 文件/目录/路径/程序目录/数据目录 79-365 |
| `cg_kit_ini.c` | INI 370-560 |
| `cg_kit_str.c` | 文本与编码 574-693 |
| `cg_kit_sys.c` | 剪贴板/外部程序/时间/注册表 695-857 |

**需新建 `cg_kit_internal.h`** 并提升 4 个 static 助手：`kit_w` :28、`kit_u8` :43、`kit_base` :58、
`kit_lower` :71（被多个目标文件共用）。

## 2. **不该拆**的（单一职责只是长 —— 这条和"该拆"一样重要）

| 文件 | 行数 | 理由 |
|---|---|---|
| `core/cg_doc.c` | 747 | 一个递归下降解析器；拆开会让解析上下文跨文件跳 |
| `core/cg_json.c` | 571 | 单一 JSON DOM（parser + emit 天然对偶） |
| `win32/cg_win_sync.c` | 693 | 单一"逻辑→原生同步"；`sync_widget_body` 是长分派但共享大量局部状态 → **只建议把类型分支收敛成表**，别拆文件 |
| `skin/cg_skin.c` | 755 | 单一"外观绘制内核"（6 种 M3 画法 + 共享 `sk_*`）；拆了会破坏"整目录换渲染内核"的约定 |
| `core/cg_widget.c` 550 / `win32/cg_win_window.c` 590 | | 单一职责 + 公共 API 天然聚合 |
| `kit/cg_net.c` 605 / `kit/cg_serial.c` | | 各自"链路 + 读线程 + 缓冲"单一职责；可抽公共（§3.2），不宜拆 |

## 3. 比拆文件更值：两类收敛

### 3.1 `w->type` 字符串分派 → `w->kind` 枚举（**最大的一处架构味道**）
实测 **200+ 处**：`cg_win_ctl.c` 79、`cg_win_sync.c` 23、`cg_win_notify.c` 13、`cg_widget_data.c` 4、
`cg_bind.c` 3 …… 另有包装器 `cgw_is_t` 被调 ~54 次、`want_type` 被调 22 次。
而 `cg_doc_kind.c:24-93` 早有类型表 + `cg_kind_lookup(type)`；`struct cg_widget`（`cg_internal.h:52-178`）
**只有 `char *type`，没有 kind 字段**（`->kind` 全仓仅 1 处，是 `cg_kind_info_t*` 上的，与控件无关）。

收敛点很集中（2-3 个文件）：`cg_win_ctl.c` 的 `cgw_class_of`(:25-64) / `cgw_style_of`(:205…) /
`cgw_exstyle_of` 是三张几乎 1:1 的表 → 可一次性变成"按 kind 查表"；
`cg_win_sync.c` 的 `sync_widget_body`(:477-682) 是一个 23 分支巨型 if/else → 按类型拆表/拆函数。

**为什么值得**：这一轮修掉的四个"声明了却不生效"里，有一半正是"某个类型漏了一条路径"——
字符串分派让"新类型/新路径"没有单一落点，编译器也帮不上忙。

### 3.2 重复原语（收敛到低层，顺手消灭一整类 bug）
- **UTF-8 ↔ UTF-16：6 份独立实现**（`cg_win_util.c:13` `cgw_wc_from_utf8`、`cg_skin.c:97` `sk_to_wcs`、
  `cg_kit.c:28/43` `kit_w`/`kit_u8`、`cg_serial.c:56/71` `ser_wide`/`ser_u8`、`cg_win_diag.c:18`），
  另有 7 处内联调用 → 建议下沉到一层（如 `core/cg_text.c`）。
- **`* scale / 100` 内联 17 处**（`cg_skin.c` 13、`cg_skin_edit.c` 4）；`scale = dpi*100/96` 内联 3 处。
  已有 `cgw_scale_px`（`win32/cg_win_util.c:59`）但 **skin 层用不到它**（win32 层符号）→
  建议在 `cg_internal.h` 提供与层无关的 `cg_scale(app, v)`。
  **这一条直接对应本轮修过的 DPI 缺陷**：所有"忘了乘 scale"的地方，都是内联副本。
- **RGB 打包 4 处**（`cgw_to_colorref` / `sk_colorref` / `sk_argb` / `cg_skin_edit.c:397` 内联）。
- 小项：`cg_str_dup` / `dup_str` / `dup_or_null` 三份；`cg_serial.c:51` 与 `cg_net.c:58` 各有一份
  `static char g_open_err[192]` + 同构 `*_fail`。

## 4. 分层目前只是约定 → 已加门禁（`tools/check-layers.mjs`）
三条不变量（都是实测**当前成立**的，门禁用来防止将来退化）：
1. `runtime/src/core/**` 不得 include win32/skin 的头（反向依赖）；
2. `runtime/src/win32/**` 只允许 include `cg_win_internal.h` + 系统头（不许 include 兄弟层/兄弟文件的头）；
3. `runtime/src/skin/**` 只允许 include `cg_skin.h` + `cg_internal.h` + 系统头；
4. 四个层目录下**不得出现同名 .c**（`build-runtime.bat:62` 用基名做 .o，撞名会互相覆盖）。

## 5. 画布（designer/）

### 5.1 真 bug（本轮已修）：`menu:programConfig` 通道漏配
`main.mjs:776` 发 `menu:programConfig`，而 `preload.cjs:30-44` 的通道清单**不含它** →
「程序配置」菜单项点了没反应。修法：把通道名集中成一份常量供 `preload` 与 `menu` 共用。

### 5.2 `model.ts`（1297 行，模型 god-module）→ 拆 9 个，保留 re-export 桶
`model/types.ts`(187-380，零依赖) / `events.ts`(1-185) / `kinds.ts`(386-465) / `textcodec.ts`(467-583) /
`factory.ts`(585-776) / `tree.ts`(778-870) / `parse.ts`(871-1128) / `serialize.ts`(1130-1294) /
`math.ts`(1296-1297)；`model.ts` 留作 `export * from …` **以免改 20+ 处调用点**。
风险：`types.ts` 必须无运行时依赖（否则成环）；桶的导出面要齐（`codegen.ts:7` 与 5 个组件从 `./model` 取）。

### 5.3 `Inspector.tsx`（1384 行，每类型字段编辑器集合）→ 按字段组拆
`inspector/Row.tsx`(58-136) / `constants.ts`(33-101) / `identity.ts`(197-236) / `geometry.ts`(256-276) /
`editFields.tsx`(278-503) / `buttonFields.tsx`(505-536) / `textStyle.tsx`(537-786) / `stateFields.tsx`(788-871) /
`formatFields.tsx`(873-1327) / `contentFields.tsx`(1218-1371) + 壳。
**只搬不改**：`patch`/`patchSoft` 与 props 契约要下传，别在拆分时顺手统一撤销粒度（会改变行为）。

### 5.4 `main.mjs`（815 行）→ `ipc/*.mjs`
`ipc/projectroot.mjs`(45-188) / `ipc/app.mjs` / `ipc/cgui.mjs` / `ipc/run.mjs`(498-622) /
`ipc/publish.mjs`(628-749) / `window.mjs`(190-238) / `menu.mjs`(765-804)。
风险：`__dirname` 相对路径与 `process.resourcesPath`（:16/147/164/200/209）——拆后要注入 `appRoot`；
`projectRootCache:53`、`win`、`currentFile`、`runBusy` 是跨 handler 单例状态，需集中导出。

### 5.5 撤销粒度不一致（要拍板，不是纯重构）
`onApply`（`App.tsx:107-119`）**内部已经压历史**，但 `Inspector.patch`(:156-162) 又自己调一次
`onBeginHistory()` → 文本框**每敲一个字两条撤销记录**；`WindowPanel` 的 `set()`(:67) 同样双压；
多行区走 `patchSoft` + `onFocus` 才是一次一条（**这才是想要的粒度**）。
→ 建议：文本类统一成"聚焦压一次 + 输入不压"（即 `patch` 不再自己压）；这是 UX 行为改动，需明确同意。

### 5.6 孤儿文件
`designer/src/dist_codegen.mjs`（26.6KB，首行 `// src/model.ts`）**全库零引用**，
疑似把 `model.ts`+`codegen.ts` 打平的构建产物误提交 → 建议确认后删除（`.gitignore` 也补一条）。

## 6. 建议的执行顺序（按"风险 × 价值"）

| 序 | 事项 | 风险 | 验证方式 |
|---|---|---|---|
| 1 | ✅ 分层门禁（§4） | 无 | 门禁自跑 |
| 2 | ✅ `menu:programConfig` 漏配（§5.1） | 无 | 代码静态可核 |
| 3 | ✅ `cg_win_draw.c` 拆 4 个（§1.1）—— **已完成**（提交 `7d4314e` + 引用清理 `c1cc6d8`） | **低**（纯搬移、零接口改动） | 已验：函数名集合旧 31 = 新 31（不丢不多）+ 剩余文件非注释行 0 + 全量重编/单测/门禁三道全绿 |
| 4 | `cg_kit.c` 拆 4 个（§1.3） | 中低（需新建 `cg_kit_internal.h` + 提 4 助手） | 同上 + `test_*`（kit 单测覆盖文件/INI/文本） |
| 5 | `cg_widget_data.c` 拆 7 个（§1.2） | 中（提 `want_type`） | 同上 + `test_terminal/chart/led_meter/splitter` |
| 6 | `w->kind` 收敛（§3.1） | 中高（面广但可分批：先 `cg_win_ctl.c` 三张表） | 门禁 + 全量自检 |
| 7 | 重复原语下沉（§3.2） | 中（跨层，需定新低层头） | 单测（`test_json`/kit）+ 自检 |
| 8 | designer 三处拆分（§5.2-5.4） | 中 | `tsc` + `vite build` + 画布往返门禁（`format-roundtrip`） |

**不做**：新建目录、拆 §2 列出的文件、为"整齐"重命名现有文件。

## 7. 执行这一系列拆分时的运作规则（第一刀跑完总结的，照做可省掉返工）

1. **一次只跑一个"工具链"上的队友**：C 侧拆分要跑 `build-all`/`run-tests`/`check-format`，
   两名 C 队友并行会同时写 `build/obj/*.o`、互相破坏增量判断；TS 侧同理（`vite build` 都写
   `designer/dist/`）。**但 C 与 TS 可以并行**（不同工具链、不同目录），这是当前并行的上限。
2. **契约与清单都不该绑"文件路径/行号"**（第一刀踩过的坑，均已修）：
   - 运行时契约表（`tools/check-dispatch.mjs`）已改成**按函数名在 `runtime/src` 下定位**，
     函数搬家不再假红；规则里的 `file` 只是"它当初住在哪"的备注。
   - `docs/ARCHITECTURE.md` 的文件清单现在由 `tools/check-layers.mjs` **双向对账**（新增文件
     不补清单就红；清单里列了不存在的文件也红）。所以**清单由主负责人（我）同步**，队友不要动它，
     但必须在回执里给出新文件名。
   - 源码注释里**禁止再写"文件:行号"式引用**，改成函数名（行号会随每次搬移漂移）。
3. **"纯搬移"的验收要可核**，别只看"编译过了"：用 `git show HEAD:<旧文件>` 取旧版的函数名集合，
   与所有新文件的同名集合比对，报"旧 N = 新 N、不丢不多"；同时报新文件的文件头/include 行数，
   把行数差逐项解释掉。第一刀就是这么被独立验过的。
4. **队友自报不算数**：主负责人必须自己跑一遍 `build-all` / `run-tests` / `check-format`
   再提交（第一刀：队友报全绿 → 我复跑三道 → 才提交）。
5. 每刀都要顺带清掉指向旧文件/旧行号的引用（grep 旧文件名 + `旧文件名:[0-9]` 应 0 命中）。
