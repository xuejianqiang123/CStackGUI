# 实现计划：运行时菜单增删与状态 API

> 这是**契约**，不是实现记录。改动与本文不符时，先改本文再改代码（或者反过来停下来说清）。
> 来源：项目自己的 `README.md`「下一步的候选」第 4 条 ——「运行时菜单增删 API（目前菜单是声明式的，运行时只读）」。
> 也是「从 Qt/GTK 学功能」清单里唯一一条**项目自己点过名**的（那两家的菜单天生动态）。

## 1. 要解决的具体问题（都核实过）

1. **菜单是只读的**：公开 API 只有 `cg_menu_last_id` / `cg_menu_last_text` / `cg_menu_count`
   （`runtime/include/cstackgui.h`），运行时**改不了菜单**。
2. 于是"最近打开的文件""端口: COM3"这类**天然要变的菜单项**做不到 —— 只能开窗时一次性写死。
   这对本工程的主力用户（写串口/网口调试小工具的人）是明显缺口：
   菜单里显示当前端口、动态列出最近文件，都是这类工具的常规做法。
3. 已有的"只读"边界写在 `README.md` 与 `docs/CGUI-FORMAT.md` 里，做完要一并划掉。
4. 「启用 / 勾选」这一半**动作项已经有了**（`cg_action_set_enabled/checked` 会同步到菜单），
   但**普通菜单项没有**任何手段 —— 这次一并补上，两条路走同一个落点。

## 2. 现状（已核实的实现事实）

- 运行时行表：`cg_widget_t.menu[]`，`cg_menu_row_t` = `{ text, id, action, cmd, checked, parent, popup }`
  （`runtime/src/core/cg_internal.h:42` 附近）。**`parent` 是父项的数组下标**，且建表时保证
  `parent < 自身下标`（`cg_widget_list`/`cg_widget.c` 的 `menu_push_doc()` 递归摊平）。
- 建 HMENU 是**纯函数**：`cgw_menu_build(w, as_bar)`（`runtime/src/win32/cg_win_menu.c:42`）——
  按行表现场建一棵 HMENU，调用方负责挂载或 `DestroyMenu`。
  - 窗口菜单栏：建窗时建一次 → `SetMenu` → 再按 `GetWindowRect-GetClientRect` 的差值调高窗口
    （**这段校准内联在 `cg_win_window.c:370-390`**，不是函数 —— 重建要复用它，得先抽出来）。
  - `menubutton` 的弹出菜单：**每次点击现场重建**（`cgw_popup_menu_for`），所以改行表后**不需要**任何同步。
- 命令 id 由 `cg_alloc_ctrl_id(app)` 分配；点击回来走 `cg_dispatch_menu_cmd(app, cmd)`
  （按 `menu[].cmd` 反查行，找不到 = 空操作，安全）。
- 行表释放：`cg_widget.c:97-102` 逐个 `free(text/id/action)`。

## 3. 逐字契约（v1）

```c
/* 运行时改菜单：改完自动重建（menubutton 本来就是每次弹出重建，不需要同步）
 * 定位一律用 .cgui 里的项 id；顶层用 parent_id = NULL 或 ""。 */

int  cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos,
                      const char *text, const char *id);
int  cg_menu_remove_item(cg_widget_t *w, const char *id);
int  cg_menu_clear_items(cg_widget_t *w, const char *parent_id);
void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text);
void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled);
void cg_menu_set_item_checked(cg_widget_t *w, const char *id, int checked);
```

### 3.1 语义

| 参数 | 规则 |
|---|---|
| `parent_id` | 父项（下拉）的 id；`NULL` / `""` = 顶层。**找不到 → 失败**（不猜、不退回顶层） |
| `pos` | 该父项的**直接子项**里的插入位置（0 = 最前）；`-1` 或越界 = 追加到末尾 |
| `text` | 显示文字；**`"-"` = 分隔线**（与 `.cgui` 同一套判定，不新造记号） |
| `id` | 该项的标识（`cg_menu_last_id` 取回、后续增删定位用）；**可空** —— 空 = 该项不可寻址 |
| 返回值 | `add` = 新行下标（≥0）／失败 `-1`；`remove`/`clear` = 删掉的行数（**含子树**）／失败 `-1` |

- `cg_menu_clear_items(w, NULL)` = 清空整个菜单（顶层全删）；`clear(w, "menu_file")` = 只清那个下拉里的项，
  **下拉项本身保留**（否则它就不是个下拉了）。
- `remove` / `set_*` 按 id **取第一个匹配**（`.cgui` 本来就允许重名 id，这里不新增判重）。
- `set_item_text` 对**动作项无效**：文字归动作管，这里只记一条 WARN（不悄悄改，
  与本工程"声明了却不生效要说出来"的一贯口径一致）。
- `set_item_enabled/checked` 对动作项同样**不生效**（归动作管），记 WARN。

### 3.2 生效点（"声明了必须真落地"）

| 控件类型 | 增删改之后 |
|---|---|
| `menu`（窗口菜单栏） | core 改行表 → 后端 `cgw_menu_rebuild(w)`：建新 HMENU → `SetMenu` → `DestroyMenu` 旧表 → **重算客户区**（复用从 init 抽出来的那个函数）→ `DrawMenuBar` |
| `menubutton` | 只改行表，**不做任何同步**（它每次弹出都重建，下次打开自然是最新的） |
| 没有窗口（`--selftest` / 单测） | 只改行表，后端调用是空操作（与 `cg_win_action_sync` 同一套守卫） |

> 行表是**唯一的真相**：HMENU 只是一次投影。所以"改完重建"是唯一需要的动作，
> 不需要维护两套状态 —— 这是这次能做得这么小的原因。

### 3.3 索引平移（最容易错的地方，单测必须钉）

`parent` 存的是**下标**，所以：

- **插入**到下标 `k`：所有 `parent >= k` 的行必须 `+1`；
- **删除** `[k, k+n)`：所有 `parent > k+n-1` 的行必须 `-n`，落在删除区间内的行不可能存在
  （删的是子树，即一个闭区间）。

在子菜单**中间**插/删之后，兄弟项仍要挂在正确的父项下 —— 这条单测必须覆盖
（否则症状是"加了一项，另一个下拉里的项跑到顶层去了"）。

### 3.4 边界（v1 明确不做）

1. **新增的项不支持引用动作**（`action`）：那要连带重建 `HACCEL`（加速键表是建窗时一次性的）
   与动作名校验两条链路。要快捷键，就在 `.cgui` 里写固定菜单项。
   → 需要动态项 + 快捷键的场合，用 `cg_menu_last_id` 分发。
2. **工具栏按钮不支持**（它的条目模型是纯文字 `items`，与 `nodes` 不同）。
3. **不改 `.cgui`**：运行时改的只是内存里的行表，**不会写回界面文件**（重开就回到文件里那份）。
   这是有意的：`.cgui` 是设计期产物，运行时状态不该改它。
4. **子菜单必须先在 `.cgui` 里有 id** 才能往里加项（顶层子菜单"文件"默认没有 id）——
   画布上的菜单项 DSL 是 `文本 | id`，给父项写上即可。文档里要讲清，否则用户会奇怪"为什么加不进去"。
5. **别在下拉展开时改**：`TrackPopupMenu` 有自己的模态循环，期间 `idle`/定时器回调仍会跑。
   真发生时的表现是**安全的**：那次点击落到已删的行 → `cg_dispatch_menu_cmd` 查不到 → 空操作。

## 4. 落地清单

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `runtime/include/cstackgui.h` | 6 个 API + 注释（含边界） |
| 2 | `runtime/src/core/cg_internal.h` | `cg_menu_row_t` 加 `enabled` 字段；声明后端钩子 `cgw_menu_rebuild` |
| 3 | `runtime/src/core/cg_widget_list.c` | 行表增删改（含索引平移）+ 6 个公共 API（放 `cg_menu_count` 旁边） |
| 4 | `runtime/src/core/cg_widget.c` | `menu_push_doc()` 里 `enabled = 1`（memset 出来的 0 是"禁用"，反了） |
| 5 | `runtime/src/win32/cg_win_menu.c` | `cgw_menu_rebuild()`；把 init 里那段客户区校准抽成 `cgw_menu_fit_client()`；`cgw_menu_build` 用 `r->enabled` |
| 6 | `runtime/src/win32/cg_win_window.c` | init 改为调用抽出来的 `cgw_menu_fit_client()`（同一规则一份实现） |
| 7 | `runtime/src/win32/cg_win_internal.h` | 两个新函数的声明 |
| 8 | `tests/unit/test_menu.c`（新） | 插入/删除/清空的**索引平移**、pos 语义、分隔线、按 id 找第一个、动作项只读（WARN）、空 parent 找不到即失败 |
| 9 | `designer/src/supportlib.ts` | 6 条「支持库」词条（画布上查得到） |
| 10 | `docs/CGUI-FORMAT.md` | 菜单节（2.7）补"运行时可增删"；API 速查加这一块 |
| 11 | `README.md` | 划掉「当前边界」里"没有运行时增删菜单项的 API"；`cg_widget_list.c` 那行补一句 |

## 5. 验证（Validation-First：先写清怎么证明它工作）

| 级 | 手段 | 判据 |
|---|---|---|
| 1 | `build-all.bat` | 编译过；10 个示例 `--selftest` 回 0 |
| 2 | `tools\run-tests.bat`（+`--memcheck`） | 新套件通过；**零新增存活堆块**（增删都会 malloc/free，这条必看） |
| 3 | `tools\check-format.bat` | 全绿（含分层契约：新函数不得让 core 反向依赖后端） |
| 4 | 真机（`serial_verify` 第 3 级形态） | 起一个真窗口程序，回调里 `cg_menu_add_item` + `set_item_checked`，**用菜单栏点击验证新项真的能点**（读回程序写出的文件），并截图确认菜单栏高度/客户区没被重建搞坏 |
| 5 | 反例 | 每条新 API 的失败路径都要真跑一遍（parent 不存在 / id 不存在 / 空 id），确认返回 -1 且**界面没变** |

**"能失败"自检**：命令一律 `> log 2>&1` 后**读日志**判定，不信 `%errorlevel%` 回显
（cmd 在解析整行时就展开它 —— 本项目实测踩过，见 `piv-validate`）。

## 6. 完成记录（2026-09-27）

- [x] 落地清单 **1~11 全部完成**
- [x] 验证 1 `build-all.bat`：10 个示例编译 + 逐个 `--selftest` 回 0
- [x] 验证 2 `tools\run-tests.bat`：**74 套件 / 2141 断言 / 0 失败**；
      `--memcheck` 逐套件"没有新增存活堆块"（增删路径的 malloc/free 都对上了）
- [x] 验证 3 `tools\check-format.bat`：全绿（分层契约 195 项 —— 新加的 core→后端调用没造成反向依赖；
      支持库表与 `cstackgui.h` 一致；画布 `tsc --noEmit` + `vite build` 也过）
- [x] 验证 4 真机（`build\menu-verify.ps1` + 一次性探针）：`on_ready` 里往「文件」下拉插一项 →
      **F10/DOWN/ENTER 真的点到了它**（`dyn hits = 1`）；它触发后把自己灰掉 →
      同样导航**不再产生新的命中**（灰掉的项被菜单跳过）。正反例同一次运行里都验到。
- [x] 验证 5 反例：单测里覆盖了父项不存在 / 往非下拉项里加 / id 找不到 / 空 id，
      都返回失败**且行表一字节没变**
- [ ] **分批提交**：按 `git-redlines` 的口径**未提交** —— 只有用户当次显式说"提交"才授权

### 实施中被改掉的契约（两处，都是被证据推着改的）

1. **`pos = -1` 的落点**：第一版按"整张表末尾"落，被新单测当场抓出来 —— 行表是深度优先摊平的，
   把新项扔到表尾会**破坏子树的连续性**（`menu_subtree_end()` 靠连续区间认子树），
   于是"删那个下拉"会漏掉它，而界面上看不出任何异常。改成"该父项**子树的末尾**"。
2. **`cg_menu_row_t` 新增 `enabled` 字段**：原表只有 `checked`，非动作项**没有**"灰掉"这一档；
   而 `cgw_menu_build()` 建 HMENU 时才知道要不要 `MF_GRAYED`，所以状态必须落在行表上。
   注意 `memset` 出来的 0 是"禁用"，建表时要**显式置 1**（否则整根菜单一上来就是灰的）。

### 没验到的（如实记）

- **菜单栏重建后的客户区几何**：只做了"公式与建窗时那段完全一样 + 真机跑通"两层，
  **没有截图级证据**（探针的控件都在窗口上半部，尺寸差一行菜单栏也看不出来）。
  要真验它，得在窗口底部放一个控件再截一次图 —— 留给下一次动 `cgw_menu_fit_client` 时补。
- 顶层增删导致菜单栏**换行**（菜单栏变两行）时的客户区重算：同上，没有专门验。
- `menubutton` 的弹出菜单：单测覆盖了行表，真机只覆盖了窗口菜单栏那一份。
