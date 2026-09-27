# 实现计划：动作层（actions）—— 命名命令 + 快捷键

## 目标

给 `.cgui` 加一层**命名动作**：一条命令 = 文字 + 快捷键 + 一个回调名。菜单项与控件用
`"action": "save"` 引用它，于是**一个回调、一处启用状态**同时管住所有引用点。

解决的是三个已经存在的具体问题（都核实过）：

1. **Ctrl+S 在输入框里按不出来**。runtime 里 `LoadAccelerators` / `TranslateAccelerator`
   一次都没出现，消息循环只做了 `IsDialogMessageW`（`runtime/src/win32/cg_win_window.c:334-345`）。
   焦点在 EDIT 里时 `WM_KEYDOWN` 被子控件吃掉、不冒泡，窗口级 `keydown` 根本不触发。
2. **菜单运行时只读**：公开 API 只有 `cg_menu_last_id/text/count`
   （`runtime/include/cstackgui.h:385-388`），没有改启用/勾选的手段。
3. **同一命令绑多处**：菜单项 + 按钮各绑一份回调，禁用时要改 N 处，迟早不同步。

## 前置依赖 / 先例（已探明，直接继承）

- 事件靠**名字**绑定：`CG_BIND("on_save", on_save)` → `cg_resolve_binding()`
  （`runtime/src/core/cg_event.c:122`）在构建期查表。**动作不新增注册机制，handler 就是同一张表里的名字。**
- 派发中枢在 core：`cg_dispatch()`（`runtime/src/core/cg_event.c:178`）。所有后端都走它。
- 菜单项：`cg_item_t`（`cg_doc.h:162`）→ 运行时 `cg_menu_row_t`（`cg_internal.h:42`）→
  `cgw_menu_build()`（`cg_win_menu.c:42`）→ 点击 `cg_dispatch_menu_cmd()`
  （`cg_event.c:239`）给菜单控件派发 `CG_EVENT_CLICK`。
- 「窗口级事件」的既有形状：伪控件 `app->window`（type `"window"`、id `"__window"`，
  `cg_widget.c:339`），事件载荷靠「派发前写控件 `ev_*` 字段 + getter」（`cg_event.c:76-86`）。
- core 调后端的既有形状：`cg_win_set_hint()` / `cg_win_restart_timer()`
  （`cg_internal.h:303-306`）—— 动作的菜单同步走同一套路。
- 门禁会红的两处（**必须一起做**）：
  - `tools/check-layers.mjs` 第 6 条：`runtime/src/` 下每个 `.c/.h` 都必须在
    `docs/ARCHITECTURE.md` 的文件清单里出现 → 新增 `cg_action.c` 同步补 ARCHITECTURE.md。
  - `tools/check-layers.mjs` 第 5 条：四层 `.c` 基名唯一 → `cg_action.c` 不撞名（已核，没有同名）。

## 逐字契约

### 1. `.cgui` schema

顶层新增 `actions`（与 `app` / `widgets` 同级，可选）：

```json
"actions": {
  "save": { "text": "保存(&S)", "accel": "Ctrl+S", "handler": "on_save" },
  "grid": { "text": "显示网格", "handler": "on_grid_toggle", "checked": true }
}
```

| 键 | 类型 | 缺省 | 语义 |
| --- | --- | --- | --- |
| （对象键）| string | — | 动作名。**必须非空**；空名条目**直接跳过、不报错**（画布也没法表示一个没有名字的动作）。重名条目不报错，**后者覆盖**（实现注明的行为） |
| `text` | string | `""` | 引用它的菜单项/控件显示的文字 |
| `accel` | string | `""` | 快捷键，语法见下。空 = 无 |
| `handler` | string | `""` | C 侧 `CG_BIND` 的名字。**空 → 该动作被忽略** + 启动 WARN + `--selftest` 报错 |
| `checked` | bool | `false` | 初始勾选状态（同步给引用它的菜单项） |
| `enabled` | bool | `true` | 初始启用状态 |

未知键忽略（与其它字段一致）。**缺 `actions` = 无动作，老 `.cgui` 行为完全不变。**

### 2. 引用

**引用点一：菜单项** —— `menu` / `menubutton` 的 `nodes[]` 里任一项加 `"action": "save"`。

- 该菜单项显示的文字 = `action.text` 非空 ? `action.text` : 该项自己的 `text`
- 点击该菜单项 → 触发 action（**不再**给菜单控件派发 click）
- 有 `action` 的项，`id`（`cg_menu_last_id`）不再有意义（所以示例里干脆不写 `id`）
- 菜单项**本来就写不出自己的事件**（`cg_item_t` 没有 `events` 字段），所以"动作项与普通项混用一根菜单"
  是正常写法：普通项照旧走菜单控件的 `events.click`（`examples\notepad` 就是这样）
  引用了**不存在的动作名** → 启动 WARN + 自检报错
- 启用/勾选跟随 action

**引用点二：控件** —— 节点上加 `"action": "save"`。

- 生效条件是**该类型支持 `click`**（真相在 `cg_kind_docs_kind.c` 的 `k_kinds[].events`）；
  不支持 click 的类型写了 `action` → 启动 WARN + 自检报错（照 `unsupported_events` 的做法）
- 文字：`action.text` 非空则**覆盖控件自己的 `text`**（`.cgui` 里写的仍在，运行时被盖掉）
- 点击 → 触发 action；不再走自己的 `click`/`dblclick`（`cg_dispatch` 顶层直接分流）
- 启用跟随 action

### 3. 快捷键语法（`accel`）

```
[Ctrl+][Shift+][Alt+]键        修饰键大小写不敏感、可任意顺序、重复算一次
```

- 键的取值：`A`–`Z`、`0`–`9`、`F1`–`F24`、`Esc`、`Enter`、`Tab`、`Space`、`Back`（退格）、
  `Delete`、`Insert`、`Home`、`End`、`PageUp`、`PageDown`、`Left`、`Right`、`Up`、`Down`
- **禁止无修饰键的单个字母/数字键**（裸 `S` 会让用户没法输入 s）→ 解析失败。
  裸功能键（`F5`）允许。
- 解析失败（含未知键、重复声明同一个快捷键）→ 忽略该 `accel` + 启动 WARN + 自检报错
- 规范化显示：修饰键固定按 `Ctrl+Shift+Alt` 顺序，如 `Ctrl+Shift+S`。
  菜单项的文字右侧自动拼 `\tCtrl+Shift+S`（Win32 自己右对齐显示）

### 4. 回调语义

- 触发路径统一走 `cg_action_invoke()`
- 回调签名不变：`void cb(cg_widget_t *self, cg_event_t ev, void *user)`
- **`self` 恒为主窗口伪控件**（`app->window`，`cg_widget_id(self) == "__window"`）
- **`ev` 恒为 `CG_EVENT_CLICK`**（与"菜单项点击 = click"同一套词汇；不新增事件枚举）
- 回调里用 `cg_action_name(self)` 取回是哪个动作被激活（一个函数挂多个动作时用）
- `user` = `app->window` 的 user 指针（窗口级事件本来就是这个）

### 5. 公共 API（逐字，已写入 `runtime/include/cstackgui.h`）

```c
int         cg_action_invoke(cg_app_t *app, const char *name);
void        cg_action_set_enabled(cg_app_t *app, const char *name, int enabled);
int         cg_action_is_enabled(const cg_app_t *app, const char *name);
void        cg_action_set_checked(cg_app_t *app, const char *name, int checked);
int         cg_action_is_checked(const cg_app_t *app, const char *name);
const char *cg_action_name(const cg_widget_t *self);
int         cg_action_count(const cg_app_t *app);
const char *cg_action_at(const cg_app_t *app, int i);
const char *cg_action_accel(const cg_app_t *app, const char *name);
```

`const cg_app_t *` 用于只读查询（照 `cg_widget_count(const cg_app_t *)` 的既有风格）。

### 6. 生效点（enable / checked 必须真落到原生侧）

- 菜单项：`EnableMenuItem(GetMenu(hwnd), row.cmd, MF_BYCOMMAND | ...)` / `CheckMenuItem(...)`；
  菜单栏句柄从 `GetMenu(app->hwnd)` 取。**这是 check-dispatch 精神里的"声明了却不生效"高危区，必须真同步。**
- 控件：`cg_widget_set_enabled(w, on)`（走正常路径 —— 它的契约要求两个 HWND 都覆盖）
- core 通过 `void cg_win_action_sync(cg_app_t *app, const char *name)` 通知后端
  （声明在 `cg_internal.h`，实现放 `cg_win_menu.c`；无窗口时是空操作）

### 7. `--selftest` 必须报错的四类（拼进退出码）

1. `actions[*].handler` 没在 C 侧 `CG_BIND`（走既有 `cg_resolve_binding` 的 unresolved 机制）
2. `accel` 解析失败
3. `action` 引用了不存在的动作名
4. 写了 `action` 却（a）该类型不支持 click，或（b）同时绑了 `events.click`

### 8. v1 明确不做（写进文档的边界）

- **toolbar 项引用 action**：toolbar 的条目模型是纯字符串 `items`，与 menu 的 `nodes` 不同；
  要支持得先给 toolbar 换条目模型 —— 单独一轮，本轮不做
- 运行时菜单增删 API（原 roadmap 项，另一个功能）
- action 的 `hint`、运行时改 `text`
- 画布侧：动作改名**不会**自动更新引用（引用处会变成"不存在的动作"，自检会报出来）

## 域划分（互不重叠）

| 域 | 独占文件 | 谁 |
| --- | --- | --- |
| 运行时 + 测试 + 门禁 | `runtime/**`、`tests/**`、`tools/format-roundtrip.mjs`、`tools/check-dispatch.mjs` | 我 |
| 画布 | `designer/src/**` | 队友 A |
| 文档 | `docs/CGUI-FORMAT.md`、`docs/ARCHITECTURE.md`、`README.md` | 队友 B |

`.codebuddy/**` 任何人都不要动。

## 实现步骤（我的域，按序）

1. `cg_doc.h`：`cg_action_t`；`cg_doc_t.actions/nactions`；`cg_node_t.action`；`cg_item_t.action`
2. `cg_doc.c`：解析 `actions`（含 `text/accel/handler/checked/enabled`，空名/空 handler 记录问题）、
   `action` 字段解析、`cg_doc_release` 释放、`cg_doc_dump` 打印
3. `cg_action.c`（新，core）：accel 解析器 + 规范化显示；动作表构建（查 handler、分配命令 id、
   建 HACCEL 用的 vk/mods）；`cg_action_invoke` / `set_enabled` / `set_checked` / getter；
   引用点校验（不存在的动作名、类型不支持 click、同时绑 click）
4. `cg_internal.h`：`cg_action_row_t` + `cg_app_t.actions/nactions/action_errors/haccel_row`；
   `cg_widget_t.action/action_last`；`cg_win_action_sync()` 声明
5. `cg_widget.c`：构建时把 `node->action` / `item->action` 拷进运行时；释放
6. `cg_event.c`：`cg_dispatch()` 顶层 click→action 分流；`cg_dispatch_menu_cmd()` 的项级分流；
   `cg_action_name()` 从窗口控件的 `action_last` 读
7. `cg_win_menu.c`：菜单项文字走 action.text + `\t` 快捷键；`cg_win_action_sync()`（EnableMenuItem /
   CheckMenuItem）；`cg_menu_row_t.action`
8. `cg_win_window.c`：建 `HACCEL`、消息循环 `TranslateAcceleratorW`（在 `IsDialogMessageW` **之前**）、
   收尾 `DestroyAcceleratorTable`
9. `cg_win_main.c`：`WM_COMMAND` 里先判 action 的 accel id → `cg_action_invoke`
10. `cg_app.c`：`--selftest` 把 `action_errors` 拼进退出码
11. `tests/unit/test_doc.c`：解析/缺省/非法值/释放；`tests/unit/test_action.c`（新）：accel 解析表的
    正例反例、规范化显示、非法组合
12. `tools/check-dispatch.mjs`：加回归锁（`cg_dispatch` 的 click→action 分流、消息循环里的
    `TranslateAcceleratorW`）
13. `tools/format-roundtrip.mjs`：探针加"画布加一个 action + 引用它"，核对往返无损
14. 门禁全绿 → 分批提交

---

## 本轮完成（2026-09-27）

三个域并行推进（运行时我 own、画布与文档各一个队友），全部按上面的契约落地并分批提交。

**落地结果与契约的差异**（按"文档与代码不一致时按代码改文档"已回改本文件与 `docs/`）：

- 空名动作**不写 WARN**，直接跳过（上面 §1 字段表已改）。
- "菜单项同时绑 `events.click` → 报警"这条**不存在**：`cg_item_t` 没有 `events` 字段，
  菜单项本来就写不出自己的事件；运行时只校验"引用的动作名是否存在"。`cg_doc.h` 里那条
  与实现不符的注释已一并改掉（上面 §2 已改）。
- `cg_accel_parse` 还多了一条：**裸功能键（F1–F24）允许**，只有裸字母/数字被拒。

**验证到哪一层**（详细输出见各提交信息）：

- `build-all.bat`（10 个示例逐个 `--selftest`）、`tools\run-tests.bat`（71 套件 / 2063 断言 /
  `--memcheck` 零新增存活块）、`tools\check-format.bat`（往返探针含 action 与引用点、
  支持库、分层、43 种控件落点、18 条运行时契约）、`designer` 的 `tsc --noEmit` + `vite build` —— 全绿。
- **真机验证**（快捷键这条链没有任何单测能覆盖，`build\accel-verify.ps1` + `build\acttest\` 一次性探针）：
  焦点在主窗口按 Ctrl+S / **焦点在输入框里按 Ctrl+S** / Alt+F9（`WM_SYSCOMMAND` 分支）/
  F10 菜单导航选中菜单项 —— 四条都真的调到了动作回调；未注册的 Ctrl+Q 不触发；
  `cg_action_set_enabled` 禁用后快捷键与菜单都不再触发、恢复后又能用。

**没验到的**（如实记下）：菜单项在**画面上**显示的文字、以及 `cg_action_set_checked` 的打勾视觉 ——
只到"core 单测断言行文字 + 真机菜单点击能命中"两层，没有截图级证据（弹出菜单不在 `PrintWindow`
能截的范围里）。另外 `Ctrl+Shift+S` 在中文输入法下的行为没测。

**v1 边界**（已写进 `cstackgui.h` / CGUI-FORMAT / README）：toolbar 项不支持引用动作
（它的条目模型是纯文字列表）；没有运行时增删菜单项的 API；动作的 `text` 不能在运行时改。
