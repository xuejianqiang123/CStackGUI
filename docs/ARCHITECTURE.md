# CStackGUI 架构设计

> 一句话：**画布负责长相，C 负责行为，两边只通过一个 `.cgui` 文件和一组回调名字耦合。**

目标运行形态：

| 产物 | 是什么 | 怎么来的 |
| --- | --- | --- |
| `CStackGUI.exe` | 画布编辑器，双击就能画界面、存 `.cgui`、导出 C 骨架 | Electron 包 React 画布，`electron-builder` 打包 |
| `xx.cgui` | 界面文件（JSON），一个文件描述一整个窗口 | 画布保存 / 人手写 |
| `<app>.exe` | 业务程序，**单个 exe，无运行库依赖** | `gcc` 把运行时静态库 + 你的 `main.c` 链接起来 |

关键点：**画布和业务 exe 之间没有任何运行时依赖**。画布退出后业务程序照样跑；业务程序换台机器复制过去就能跑。

---

## 1. 总体结构

```
        ┌───────────────────────────────┐
        │  CStackGUI.exe  (画布)         │
        │  Electron + React + TS         │
        │  ┌─────────┐ ┌─────────────┐   │
        │  │ 控件面板 │ │ 画布/属性/事件│   │
        │  └─────────┘ └─────────────┘   │
        │        模型层 model.ts          │  ← .cgui 的唯一真相
        │        codegen.ts（导出 C 骨架）│
        └───────────────┬───────────────┘
                        │ 写
                    xx.cgui  (JSON：控件树 + rect/style/events)
                        │ 读
        ┌───────────────┴───────────────┐
        │  运行时库 libcstackgui.a (C)   │
        │  cg_json/cg_doc/cg_event       │  ← 解析、控件树、事件派发
        │  cg_widget/cg_widget_data      │  ← 控件模型与公共 API
        │  cg_win_* (Win32 原生后端)      │
        └───────────────┬───────────────┘
                        │ 链接
        ┌───────────────┴───────────────┐
        │  你的业务代码 main.c            │
        │  CG_BIND("on_ok_click", ...)   │  ← 只有行为，没有界面代码
        └───────────────┬───────────────┘
                        ↓
                   <app>.exe（单文件）
```

### 为什么这样切

界面的「长相」和「行为」是两类变化频率完全不同的东西：调一个按钮的位置不该重编译 C 代码，改一段业务逻辑也不该动界面文件。所以：

- **长相**改在画布里 → 存 `.cgui` → 业务 exe 不用重新编译，重新运行即可（`.cgui` 是运行时读的，不是编译期嵌进去的）。
- **行为**改在 C 里 → 只重编译业务 exe，界面文件不动。
- 两边唯一的契约是**事件名字符串**，写在 `.cgui` 的 `events` 里，在 C 侧用 `CG_BIND("同一个名字", 函数)` 登记。

---

## 2. 事件模型

### 2.1 名字绑定，而不是指针绑定

`.cgui` 里存的是字符串：

```json
{ "id": "btn_ok", "type": "button", "events": { "click": "on_ok_click", "dblclick": "on_ok_dblclick" } }
```

C 侧登记名字到函数指针的映射：

```c
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_ok_click",    on_ok_click),
    CG_BIND("on_ok_dblclick", on_ok_dblclick),
    CG_BIND_END
};
```

启动时（`cg_build_widgets`）按名字查表，把结果塞进控件的 `handlers[]`。这样做的好处：

- 画布生成的文件里没有地址、没有内存布局，**跨进程/跨版本都安全**；
- 名字写错时不会编译失败，但运行时会明确报警：
  `[cstackgui] 警告: .cgui 引用了回调 "on_xxx"，但 C 代码里没有 CG_BIND 它`；
- **一个函数可以被多个控件/多个事件共用**（画布上会标「共用」）。

### 2.2 事件按控件特性分配

**事件不是每个控件发一套全套**，而是按控件特性分配：标签没有交互、进度条只能被程序写、
只有树才有展开折叠。这件事由类型表（`k_kinds[].events` 位掩码）声明，三方共用同一份约定：

- C 运行时：绑定和派发都先过 `cg_kind_supports_event()`，不支持的事件直接丢弃并警告；
- 画布：事件面板只列出该控件支持的事件（`model.ts` 的 `KIND_EVENTS`）；
- 自检：`.cgui` 绑了不支持的事件算失败（返回码 5）。

| `cg_event_t` | `.cgui` 里的键 | 触发来源（Win32 后端） | 谁支持（以 `k_kinds[].events` 掩码为准） |
| --- | --- | --- | --- |
| `CG_EVENT_CLICK` | `click` | `BN_CLICKED` / `STN_CLICKED`（button / iconbutton / link / tag）、菜单与工具栏的 `WM_COMMAND`；`edit` 与 `image` / `canvas` 由窗口过程在 `WM_LBUTTONUP` 补派发（EDIT 自己既不发 `BN_CLICKED`、也没有 `WM_LBUTTONDBLCLK`） | button、iconbutton、link、edit、image、canvas、tag、menu、menubutton、toolbar |
| `CG_EVENT_DBLCLICK` | `dblclick` | 运行时按系统双击间隔合成（见下）；listbox / tree / table 另有原生 `LBN_DBLCLK` / `NM_DBLCLK` | button、iconbutton、link、edit、image、canvas、listbox、tree、table（**月历没有**：它从不发 `NM_DBLCLK`，点击语义由 `select` 覆盖） |
| `CG_EVENT_CHANGE` | `change` | `EN_CHANGE` / `BN_CLICKED`(勾选类) / `WM_HSCROLL`(滑块) / `DTN_DATETIMECHANGE` / `UDN_DELTAPOS`(数值框箭头) / 取色·评分·分页的点击 | switch、edit（含多行与数值框）、richtext、datepicker、checkbox、radio、slider、colorpicker、rate、pagination |
| `CG_EVENT_SUBMIT` | `submit` | 回车（`IsDialogMessage` → `IDOK`，落在当前焦点的输入框上） | 只有 edit |
| `CG_EVENT_SELECT` | `select` | `LBN_SELCHANGE` / `CBN_SELCHANGE` / `TCN_SELCHANGE` / `TVN_*` / `LVN_ITEMCHANGED` / `MCN_SELECT` | listbox、combo、tabs、tree、table、monthcal |
| `CG_EVENT_RELEASE` | `release` | 滑块 `TB_ENDTRACK`（拖动中只发 `change`）/ 分隔条拖完的 `WM_LBUTTONUP` | slider、splitter |
| `CG_EVENT_SPIN` | `spin` | 数值框的 `UDN_DELTAPOS`（`cg_event_delta()` 给 +1 / -1；`spin:1` 之后还会补一次 `change`） | 只有 edit（`.cgui` 里开了 `spin`） |
| `CG_EVENT_EXPAND` / `COLLAPSE` | `expand` / `collapse` | `TVN_ITEMEXPANDEDW`（回调里 `cg_tree_selected()` 能拿到是哪个节点） | 只有 tree |
| `CG_EVENT_TICK` | `tick` | 后端 50ms 心跳按 `period` 计时 | 只有 timer |
| `CG_EVENT_CHECK` | `check` | 表格勾选列 `LVN_ITEMCHANGED` | 只有 table（`.cgui` 里开了 `check`） |
| `CG_EVENT_GOTFOCUS` / `LOSTFOCUS` | `gotfocus` / `lostfocus` | 焦点子类的 `WM_SETFOCUS` / `WM_KILLFOCUS` | button、switch、edit、richtext、datepicker、checkbox、radio、listbox、combo、slider、tabs、tree、table |
| `CG_EVENT_MOUSEDOWN` / `MOUSEUP` / `RDOWN` / `RUP` / `MOUSEMOVE` / `KEYDOWN` / `KEYUP` / `CHAR` / `WHEEL` | 与枚举名同小写 | 控件内的鼠标 / 键盘 / 滚轮消息（坐标相对**控件客户区**） | 控件级只有 button、edit |
| `CG_EVENT_CLOSE` / `READY` / `DESTROY` / `MOVE` / `SIZE` / `ACTIVATE` / `DEACTIVATE` / `FIRSTACTIVATE` / `IDLE` / `TRAY` / `SHOW` / `HIDE` / `CLOSEQUERY`（+ 上面那批鼠标 / 键盘事件） | 同名（写在 `app.events` 里） | 主窗口过程；`close` / `closequery` 回调里可 `cg_cancel_close()` 拦下 | 只有窗口（伪控件 `CG_WINDOW_ID`） |

设计动机：把事件和控件类型绑死，用户在画布上就不会给进度条绑出永远不触发的
`click`；手写 `.cgui` 绑错了，启动时就有明确警告，而不是"点了没反应"自己猜。

**双击怎么判定**：标准 Win32 按钮并不会发 `BN_DOUBLECLICKED`，所以运行时自己记时——每次 `click` 时和上次点击时间比，落在 `GetDoubleClickTime()` 之内就算双击。语义：

- 默认（`dblclick_suppress_click = 0`）：第一次点击 → `click`；第二次（间隔内）→ 再 `click` 一次，然后 `dblclick`。
- 设成 `1`：第二次只发 `dblclick`，不发第二个 `click`（写业务时更省事）。

### 2.3 回调签名

```c
void cb(cg_widget_t *self, cg_event_t ev, void *user);
```

- `self`：触发事件的控件，用 `cg_widget_text/checked/value` 读它当前状态；
- `ev`：事件类型，一个函数挂多个事件时靠它分支；
- `user`：`cg_widget_set_user()` 挂的业务数据，默认 `NULL`。

一个函数被多个控件共用时，用 `cg_widget_id(self)` 区分是谁点的。

---

## 3. 控件模型

### 3.1 坐标

- 窗口里的顶层控件：坐标相对**客户区**左上角，单位是逻辑像素（96dpi 下的 px）。
- 容器控件（`group` / `box`）里的子控件：坐标相对**该容器的客户区**。
- 运行时按 `app->scale`（= `dpi * 100 / 96`，百分比）换算成物理像素再建原生控件，所以在 125% / 150% 缩放的屏幕上界面不会被拉糊。

### 3.2 控件类型表

类型表在 C 侧是 `cg_doc_kind.c` 的 `k_kinds`，在画布侧是 `model.ts` 的 `KINDS`——**两处必须同步**，新增控件就是各加一行 + 后端补一个分支。

| type | 中文 | 容器 | Win32 实现 |
| --- | --- | --- | --- |
| `button` | 按钮 | 否 | `BUTTON` + `BS_OWNERDRAW`（M3 自绘，见 M3-SKIN）|
| `label` | 标签 | 否 | `STATIC`（`SS_LEFT/CENTER/RIGHT`）|
| `edit` | 输入框 | 否 | `EDIT`；多行加 `ES_MULTILINE`，带调节器（`spin`）时加 `msctls_updown32`（**此时一个控件两个 HWND**，见 4.1）。原来的 `multiedit`/`spinner` 已合并进它 |
| `checkbox` / `radio` | 复选/单选 | 否 | `BUTTON` + `BS_OWNERDRAW`，父窗口处理 `WM_DRAWITEM` 自绘 |
| `switch` | 开关 | 否 | `BUTTON` + `BS_OWNERDRAW`，父窗口处理 `WM_DRAWITEM` 自绘药丸 |
| `listbox` | 列表框 | 否 | `LISTBOX` + `LBS_NOTIFY` |
| `combo` | 下拉框 | 否 | `COMBOBOX`（下拉列表样式）|
| `slider` | 滑块 | 否 | `msctls_trackbar32` |
| `progress` | 进度条 | 否 | 自绘窗口类 `CG_PROGRESS_CLASS`（原生 `msctls_progress32` 不支持 custom draw）|
| `tabs` | 标签页 | **是** | `SysTabControl32`；子控件按 `page` 显隐（切页由运行时管）|
| `tree` | 树 | 否 | `SysTreeView32`；`item_map` 保存 逻辑下标 → `HTREEITEM` |
| `table` | 表格 | 否 | `SysListView32` 报表模式（列 + 行）|
| `statusbar` | 状态栏 | 否 | `msctls_statusbar32`，分段宽度按控件宽度均分 |
| `menu` | 菜单栏 | 否 | 窗口 `HMENU`：递归建菜单并 `SetMenu`，用户点中后 `WM_COMMAND`（`lParam==0`）回来 |
| `menubutton` | 菜单按钮 | 否 | `BUTTON` + `TrackPopupMenu(TPM_RETURNCMD)`，直接用返回值，不走消息路由 |
| `toolbar` | 工具栏 | 否 | `ToolbarWindow32`：文字池 + `BTNS_SHOWTEXT` 做纯文字按钮，点击走 `WM_COMMAND` |
| `richtext` | 富文本 | 否 | `RICHEDIT50W`（`Msftedit.dll`，缺了就退 `RichEdit20W` 再退普通 `EDIT`）|
| `datepicker` | 日期时间 | 否 | `SysDateTimePick32`；`datetime` 就是两个 DTP 并排（用 `native2`）|
| `group` / `box` / `card` | 分组框/容器块/卡片 | **是** | 自注册窗口类 `CStackGUIPanel`（转发子控件通知、画背景；组框边框标题完全自绘）|
| `separator` | 分割线 | 否 | `STATIC` + `SS_ETCHEDHORZ` |
| `image` | 图片 | 否 | 自注册窗口类 `CStackGUIImage`，GDI+ 读图 + 自绘缩放 |

> 这里只是常用控件的速览。**完整 43 行类型表**（含 `progressring` / `timer` / `canvas` / `colorpicker` /
> `browser` / `shape` / `badge` / `tag` / `rate` / `pagination`）见
> [docs/CGUI-FORMAT.md](CGUI-FORMAT.md) 第 4 节。

菜单栏**没有矩形**：它是挂在窗口上的 `HMENU`，所以 `cg_doc` 里 `def_w/def_h` 为 0，
`create_native` 直接跳过它，改在 `win_init` 里 `SetMenu`，之后再加一次客户区尺寸校准
（菜单栏会占掉客户区顶部，不校准的话 `app.height` 就对不上了）。

`image` 用 GDI+ 读 png/jpg/bmp/gif，路径按「原样 → 相对 `.cgui` 目录 → 相对 exe 目录」依次找，
第 2 条最符合直觉（`.cgui` 和 `assets\` 一起搬走就能跑）。GDI+ 只用 4 个函数，
`cg_win_internal.h` 里按官方 ABI 手写声明（MinGW 的 `gdiplusflat.h` 在 C 下编不过）；
`gdiplus.dll` 从 XP 起就是系统组件，不破坏「单 exe、零依赖」。

### 3.3 状态与运行时 API

业务代码能做的基本操作（完整列表见 `runtime/include/cstackgui.h`）：

```c
cg_widget_t *w = cg_find(cg_app_of(self), "lab_status");
cg_widget_set_text(w, "已保存");
cg_widget_set_enabled(w, 0);
cg_widget_set_value(prg, 60);
cg_widget_list_addf(log, "第 %d 次点击", n);   /* 列表框当日志用，自动滚到底 */
cg_widget_add(app, NULL, "button", "btn_dyn", 20, 200, 120, 40, "动态加的");
cg_set_timer(app, 2000, 回调, app);            /* 延迟做一件事，避开当前事件栈 */
cg_quit(app);
```

界面之外的能力（`cg_kit`，见 §4 的 `kit/` 层）——**这些让"业务代码只 include cstackgui.h"
真的成立**：不用再为了读个配置就去 include `windows.h`：

```c
char *cfg  = cg_path_join(cg_app_data_dir(), "config.ini");
char *w    = cg_ini_read(cfg, "窗口", "宽", "800");     /* 返回堆串，用完 cg_free */
char *txt  = cg_file_read("data.txt");                  /* UTF-8 BOM 自动去掉 */
char *u8   = cg_gbk_to_utf8(txt);                       /* 老 GBK 文件也能读 */
cg_file_append("log.txt", cg_str_format("%s 启动\r\n", cg_time_now(NULL)));
cg_log(CG_LOG_INFO, "配置 %s 读出来是 %s", cfg, w);
cg_shell_open("https://example.com");
cg_free(w); cg_free(txt); cg_free(u8); cg_free(cfg);
```

`cg_widget_add` 可以运行时造控件，id 同样参与事件名查找——先在 `.cgui` 里写好事件名、再在运行时造控件也行。

**控件绑定（`cg_bind.c`）**把"两个控件要保持一致"从"自己写两个回调互相 set"变成一行声明：

```c
cg_bind(cg_find(app, "sld_level"), cg_find(app, "lab_value"), 0);   /* 滑块 -> 标签显示数字 */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "spn_level"), 1);   /* 双向：两个入口改同一个值 */
```
```json
{ "id": "lab_value", "type": "label", "bind": { "src": "sld_level" } }
```

同步规则很薄：**按目标的类型决定抄 value / checked / text**（值型控件抄值、勾选型抄勾选、
其余能显示文字的控件抄源的文字或十进制数值），列表类控件存的是条目下标所以不按 `min`/`max` 夹。
传播只有两个入口——`cg_dispatch()` 派发前（后端刚写好后端字段）与公共 `set_*`，
且**同步本身不派发目标的 `change` 回调**：镜像是"跟着变"不是用户操作，因此串联与双向都不会
形成回调互推（`cg_bind_propagate()` 有深度上限兜底）。没有任何绑定时传播直接返回，老工程零开销。

**动作层（`cg_action.c`）**把"同一个命令挂在多个入口"从"给菜单项和按钮各绑一份回调、各改一次灰不灰"
变成一条**命名命令**：`.cgui` 顶层写 `actions`（文字 + 快捷键 + 回调名），菜单项与控件用
`"action": "save"` 引用它 —— 于是**一个回调、一处启用状态**管住所有引用点：

```json
"actions": { "save": { "text": "保存(&S)", "accel": "Ctrl+S", "handler": "on_save" } }
```
```json
{ "action": "save" }
```

三条刻意的做法：**用"名字查表"、不引入注册机制** —— `handler` 就是 `CG_BIND` 那张既有表里的名字，
动作只在构建期多查一次表（`cg_resolve_binding()`），所以没有给运行时长出第二套"注册 / 注销"；
**动作回调的 `self` 固定是主窗口伪控件**（`cg_widget_id(self) == "__window"`）而不是"被点的那一项" ——
一个动作可能有好几个入口，"从哪个入口点的"不是业务该依赖的信息，要知道是哪个动作就 `cg_action_name(self)`；
**快捷键必须走消息循环里的加速键表**（`HACCEL` + `TranslateAcceleratorW`，在消息进窗口前截住）而不是窗口级
`keydown` —— 焦点在输入框里时按键被 EDIT 吃掉、不冒泡，窗口根本收不到，所以 `Ctrl+S` 在输入框里照样生效。

启用 / 勾选是**动作的**：`cg_action_set_enabled()` / `cg_action_set_checked()` 会把所有引用点一起变灰 /
恢复 / 打勾；反向手动 `cg_widget_set_enabled()` 某个按钮不会改动作，还会在下一次动作同步时被覆盖回去。

### 3.4 生命周期

```
main() -> cg_app_run(desc)
           ├─ 读 .cgui -> 解析 -> 文档树
           ├─ 建运行时控件树（同时把事件名解析成函数指针）
           ├─ 后端 init：建窗口 + 建原生控件 + 同步初始状态
           ├─ desc->on_ready(app, desc->ready_user)   ← 挂业务数据 / 摆初始界面
           ├─ 消息循环：原生事件 -> cg_dispatch -> 你的回调（可随时改界面、cg_quit）
           └─ 退出：销毁控件 -> 释放文档
```

`on_ready` 存在的意义：`cg_app_run` 会一直阻塞到窗口关闭，业务代码没有别的机会在「界面已存在、事件还没来」的时刻挂数据（`cg_widget_set_user`）——这个钩子补上了这个缺口，`--selftest` 里也会调用它，所以挂了数据的逻辑在 CI 里同样被覆盖。

---

## 4. 运行时模块划分

```
runtime/
  include/cstackgui.h   业务代码唯一需要 include 的头
  src/
    core/               后端无关层：不知道 Win32 的存在
      cg_json.{h,c}     只读 JSON DOM 解析（支持 \u 转义与代理对、UTF-8 原文）
      cg_doc.{h,c}      .cgui -> 控件文档树；解析 / 释放 / 打印
      cg_doc_kind.c     控件类型表 k_kinds 与 cg_kind_* 查询
      cg_internal.h     运行时内部结构 + 后端接口 + 跨模块内部声明
      cg_event.c        事件名表、回调绑定解析、事件派发中枢
      cg_widget.c       运行时控件模型：构建、查找、公共属性 API
      cg_widget_data.c  路标：七个文件各管什么（原 1149 行按控件族拆开，别往里加实现）
      cg_widget_list.c  列表 / 状态栏 / 菜单 / 提示·周期 / 图片，外加 cg_widget_add（零散属主）
      cg_widget_tree.c  树，外加跨文件共用的 want_type
      cg_widget_table.c 表格
      cg_widget_tabs.c  标签页
      cg_widget_split.c 分隔条
      cg_layout.c       余量分配：按 .cgui 的 grow，把容器尺寸变化分给兄弟控件（纯整数，可纯单测）
      cg_widget_term.c  终端（日志视图）
      cg_widget_gauge.c 图表 + 状态灯 + 仪表盘
      cg_bind.c         控件绑定：值/文字/勾选自动同步到另一个控件
      cg_action.c       动作层：命名命令（文字/快捷键/回调名）+ 加速键表 + 启用与勾选同步
      cg_app.c          应用生命周期（定位 .cgui / 打开 / 消息循环 / 收尾）与自检
      cg_timer.c        一次性定时器（后端 50ms 心跳驱动）
      cg_log.c          日志：stderr 镜像 + 追加写文件（CG_LOG_INFO/WARN/ERROR 宏的落点）
      cg_ringbuf.c      环形字节缓冲（串口/网口的读线程往里扔，业务在时钟里取走）
    win32/              Win32 原生后端：不知道业务的存在
      cg_win_internal.h 后端内部共享接口（各 cg_win_*.c 之间）
      cg_win_util.c     工具：UTF-8 / 字体缓存 / 颜色刷 / 路径与资源 / 时基
      cg_win_ctl.c      控件 -> 窗口类与样式、背景色推导、WM_CTLCOLOR
      cg_win_create.c   原生控件创建与销毁
      cg_win_sync.c     控件状态同步与条目重建
      cg_win_layout.c   把 core 算好的 grow 布局落到原生窗口（基准取自第一次 WM_SIZE）
      cg_win_shot.c     截图：窗口 / 单个控件存成 PNG（PrintWindow 优先，抓屏兜底）
      cg_win_drop.c     文件拖放：WM_DROPFILES -> 路径填进 app 清单 -> 派发窗口级 drop 事件
      cg_win_theme.c    深色模式：底色 / 无底色控件的字色 / RichEdit 底色。
                        注意**清单控件（listbox/table/tree）实测在 Win11 上不跟随深色** ——
                        代码里试过系统未公开的深色开关（uxtheme 序号 135/133/104）也无效，
                        所以面向用户的覆盖表按"不跟随"写（见 CGUI-FORMAT 的 app.dark 一节）
      cg_win_notify.c   原生通知 -> 事件映射
      cg_win_event.c    焦点子类与窗口级事件
      cg_win_main.c     主窗口过程
      cg_win_draw.c     自绘窗口类的**路标**（9 个类 2026-09-23 拆到下面四个文件了，别往这里加代码）
      cg_win_draw_canvas.c  image / 圆环 / 进度条 / 画板（各自的窗口类与窗口过程）
      cg_win_draw_panel.c   面板（group/box/card/shape）与分隔条
      cg_win_draw_chart.c   图表
      cg_win_draw_gauge.c   状态灯与仪表盘
      cg_win_ownerdraw.c owner-draw 项绘制与 WM_DRAWITEM 分发
      cg_win_menu.c     菜单与工具栏
      cg_win_window.c   窗口生命周期与窗口/托盘 API
      cg_win_dialog.c   通用对话框（打开/保存/选色/消息框）
      cg_win_api.c      画板 / 表格 API、browser 导航
      cg_win_diag.c     诊断：日志落点、致命错误弹窗、未处理异常兜底
    skin/              自绘外观层（GDI+，M3 风格）：换 D2D 内核时整目录替换
      cg_skin.{h,c}     按钮/开关/勾选/滑块/进度/圆环
      cg_skin_edit.c    输入框圆角裁切与单行文字垂直居中（EDIT 子类）
    kit/               平台服务层：跟界面无关的那一半 Win32（不属于任何后端）
      cg_kit.c          路标：四个子文件各管什么（原 857 行按子系统拆开，别往里加实现）
      cg_kit_fs.c       文件 / 目录 / 路径 / 程序目录 / 数据目录，外加内存桥 cg_free
      cg_kit_ini.c      INI 配置读写
      cg_kit_str.c      文本与编码：格式化 / 裁剪 / 替换 / 比较、UTF-8 ↔ GBK、
                        十六进制 / Base64 / URL 编解码、进制转换与补码（工控看寄存器值用）
      cg_kit_bytes.c    字节操作：反转 / 切片 / 查找 / 按字节序读写 u16·u32（拼帧拆帧的基元）
      cg_kit_check.c    校验：CRC32（IEEE 802.3）/ CRC16-Modbus（逐位算不建表 ⇒ 线程安全）、
                        MD5 / SHA-1 / SHA-256 / SHA-512（手写，零新增链接依赖；含分块文件摘要）
      cg_kit_wireless.c 无线状态：WiFi 网卡/当前 SSID·信号/周边网络（可用 cg_wifi_scan 主动刷）、
                        蓝牙无线电与已知设备（IoT 上位机先看这一眼）。
                        **零新增链接依赖**：符号全部运行期从 wlanapi.dll / BluetoothApis.dll
                        取 —— 直接链会让既有用户项目的链接命令都得改（实测 undefined reference）。
                        **注意**：运行期取符号 + 自制 typedef 时**必须逐个参数对官方声明** ——
                        漏一个参数在 x64 上是从栈上捡垃圾当指针用（野写），2026-09-27 为此
                        追了一整天（见 .codebuddy/plans/memcheck-flake.md）
      cg_kit_modbus.c   Modbus 帧编解码（RTU / TCP）：拼读·写请求、解响应与异常帧
                        （RTU 自动挂 CRC 并校验，异常响应替你剥 0x80）。
                        **纯函数、不持状态、不碰 I/O** —— 连接 / 超时 / 重试仍归业务
      cg_kit_frame.c    字节流切帧：把"半帧 / 一整帧 / 两帧粘一起"切成整帧。四种定界办法收在
                        一个状态机里（帧间静默 = Modbus RTU 的 t3.5 / 定长 / 分隔符 / 头里带长度
                        = Modbus TCP 的 MBAP）。**时间由调用方给** ⇒ 纯状态机，单测不需要真串口
      cg_kit_sys.c      剪贴板 / 外部程序 / 时间 / 注册表 /
                        系统信息（cg_env 环境变量、系统版本与位数、磁盘剩余、进程在不在）
      cg_kit_internal.h kit 层内部头：跨文件共用的三个助手（kit_w / kit_u8 / kit_lower）
                        有了它业务代码才真的"只 include cstackgui.h"
      cg_serial.c       串口：读线程 + 环形缓冲；业务在时钟里 cg_serial_read 取走（docs/COMM.md）
      cg_net.c          网口 TCP：连接 / 监听 / 接受 + 读线程 + 环形缓冲，形状同串口；
                        网口 UDP：**不开读线程**（报文边界与来源地址必须原样交给业务），
                        业务在时钟里 cg_net_recvfrom 一次取一个数据报（docs/COMM.md）
  res/                  清单（v6 通用控件 + 每显示器 DPI）与版本资源
```

**目录即分层**：四层各自在 include 路径上
（`-Iruntime\src\core -Iruntime\src\win32 -Iruntime\src\skin -Iruntime\src\kit`），层内互相 include 直接用裸文件名；
`runtime\src` 本身**故意不上**路径——跨层引用写错就编不过，而不是被悄悄找到。
对象名取文件名（`build\obj\<名字>.o`），所以各层里的 `.c` 文件名必须唯一，
命名约定已经保证（core 用 `cg_*`、后端 `cg_win_*`、外观 `cg_skin_*`、平台服务 `cg_kit_*`，
串口与网口按业务域命名成 `cg_serial_*` / `cg_net_*`，同样落在 `kit/` 里）。
`kit` 和 `win32` 一样知道 Win32，但**不知道任何界面概念**——它只管文件、配置、编码、通信这些事，
所以画布和 skin 都不依赖它，把 `runtime\src\kit` 整个删掉也能编过（只剩 public API 声明）。

### 4.1 四条容易踩的实现约束

1. **原生控件必须按树序创建（父先于子）。** `app->widgets` 是按创建顺序头插的链表，
   它和树序相反；照链表建控件会出现"子控件的父 HWND 还是 NULL"，`CreateWindowEx` 静默失败，
   整个分组框里的控件全丢。所以 `cgw_win_init` 用 `cgw_create_native_tree()` 从 `app->window` 递归下去建。
2. **同步逻辑要防重入。** 回调里改控件会触发原生通知（`EN_CHANGE` 等），再回到回调就递归了。
   `cg_win_event.c` 用 `g_win_in_notify` 挡住这种嵌套：`cgw_sync_widget()` 外层包一层（保存/恢复原值），
   `cgw_notify_from()` / `cgw_notify_common()` 也各包一层。
3. **一个控件可能有第二个 HWND。** 数值输入框（`edit` + `spin`）= 输入框 + 上下箭头两个窗口，
   所以按控件句柄反查控件要用 `cgw_widget_by_hwnd()`（同时比对 `native` 和 `native2`），
   只按 `GetDlgCtrlID()` 找会漏掉箭头那条消息。
4. **子窗口的通知先到父窗口。** 控件把 `WM_COMMAND` / `WM_NOTIFY` / `WM_DRAWITEM` / `WM_HSCROLL` /
   `WM_VSCROLL` **和 `WM_CTLCOLORSTATIC/EDIT/BTN`** 发给**直接父窗口**；放进分组框（`cgw_PanelProc`）
   或系统容器如标签页（`cgw_FocusProc`）的控件，这些消息都要被转发回主窗口 —— 少转发一种，
   那种控件进容器就"没反应"（标签则是露出系统灰底）。
5. **菜单栏必须用 `CreateMenu()` 建，弹出菜单必须用 `CreatePopupMenu()`。** 给 `SetMenu()` 传一张
   `CreatePopupMenu()` 的表**不会报错**，只是窗口上永远看不到菜单栏（`GetMenu()` 回 0）；
   gallery 曾因此长时间"菜单栏其实是工具栏在冒充"。所以 `cgw_menu_build(w, as_bar)` 分了两种顶层容器，
   调用点一处传 1（窗口菜单栏）、一处传 0（`TrackPopupMenu`）。
   注意菜单栏会占客户区顶部的高度：挂上之后要按 `GetWindowRect - GetClientRect` 的差值把窗口调高，
   客户区才是 `.cgui` 里写的尺寸。

**分层规矩**：`cg_app.c` / `cg_widget*.c` 及以上不知道 Win32 的存在，`cg_win_*.c` 不知道业务的存在。两者之间是 `cg_backend_t` 接口：

```c
typedef struct cg_backend {
    const char *name;
    int  (*init)(cg_app_t *app);       /* 建窗口 + 原生控件 */
    int  (*run)(cg_app_t *app);        /* 消息循环 */
    void (*shutdown)(cg_app_t *app);
    void (*sync)(cg_widget_t *w);      /* 逻辑状态 -> 原生控件 */
    void (*attach)(cg_widget_t *w);    /* 运行时新增控件的创建 */
    void (*detach)(cg_widget_t *w);
    void (*set_title)(cg_app_t *app, const char *title);
} cg_backend_t;
```

换一套外观（比如 M3 那种自绘风格）就是再加一个 backend，`cg_doc.c` / `cg_app.c` / 业务代码一行都不用改。

---

## 5. 关键取舍

| 决定 | 为什么 | 代价 |
| --- | --- | --- |
| 运行时用 **Win32 原生控件**，不用 SDL/GTK | 业务 exe 必须是**单文件**、零依赖；原生控件白送输入法、无障碍、系统主题 | 系统控件（列表框/树/表格/标签页/月历/下拉框）不给画，自定义外观不生效；要吃圆角与配色得走自绘 —— 已落地为 `runtime/src/skin/` 的 GDI+ 外观层（按钮/开关/勾选/输入框/滑块/进度条/圆环）|
| 用 **MinGW-w64 + windres** | 免装 Visual Studio，`gcc` 一条命令出 exe；`-mwindows` 就是 GUI 程序 | 需把 `mingw64\bin` 加进 PATH（脚本里已内置默认路径） |
| `.cgui` 用 **JSON** | 人能读能改、diff 友好、画布侧零解析成本 | C 侧要自带一个 JSON 解析器（约 400 行，已实现） |
| **绝对坐标**，没有布局引擎 | C 侧实现量小、所见即所得、和 Win32 的心智模型一致 | 窗口拉伸不会自动重排（v1 不做自适应） |
| 事件靠**名字**绑定 | 解耦、可共用、跨版本安全 | 拼错名字不会编译报错（用 `--selftest` 和运行警告兜住）|
| 动作（`actions`）用**命名命令 + 回调名**，不引入注册机制 | 同一命令常挂在菜单项与按钮等多处，逐处绑回调会变成"N 份回调、N 处启用状态"迟早不同步；回调名直接复用既有的"按名字查 `CG_BIND` 表"，只在构建期多查一次 | toolbar 项不支持引用动作（它的条目模型是纯文字列表，要支持得先换模型）；没有运行时增删菜单项的 API；动作的 `text` 不能在运行时改 |
| 画布用 **Electron 包 React** | 直接复用 m3e-canvas 那套拖拽/属性/图层交互，开发最快 | 安装包约 100MB 起 |
| 双击由**运行时合成** | Win32 标准按钮不发 `BN_DOUBLECLICKED` | 与原生语义略有差别（见 2.2，有开关）|

### 与 m3e-canvas 的关系

画布的交互骨架（拖拽、选中、8 向缩放、图层面板、属性面板、吸附参考线）沿用了 m3e-canvas 的思路，但**数据模型换掉了**：

| | m3e-canvas | CStackGUI |
| --- | --- | --- |
| 产出 | 给 AI 编程工具的提示词 | 运行时能直接读的 `.cgui` + C 骨架 |
| 控件语言 | Material 3 网页组件 | Win32 原生控件（画布上按原生外观预览）|
| 交互 | 跳转/滑动/联动多屏 | 事件绑定到 C 回调 |
| 持久化 | 浏览器 localStorage | 真实文件系统（Electron IPC）|
| 材质/主题 | M3 四轴主题 | 单控件级样式（字体/颜色/对齐）|

---

## 6. 构建流程

两条**互不依赖**的流水线。

### 6.1 业务 exe（C）

```bat
build-all.bat                        :: 运行时 + examples\ 下所有示例
build-app.bat gallery                :: 刷新运行时库 + 链接 examples\gallery\ -> examples\gallery\gallery.exe
build-app.bat gallery --release      :: -mwindows，无控制台，交付用
build-app.bat myapp                  :: 你自己的工程 apps\myapp\（画布导出的默认位置）
build-runtime.bat                    :: 只编译运行时库（平时由 build-app 代劳）
```

**产物落在工程自己的目录里，不留构建痕迹**：

```
build\               纯构建目录（各 .o + 运行时库源头，可随时删）
examples\gallery\
  gallery.exe          双击即跑：界面文件就在旁边
  lib\libcstackgui.a   运行时库副本，整个目录搬走也能重编译
  build-app.bat        本工程的编译入口（生成物，双击即可重编译这一个）
  gallery.cgui / main.c / assets\...
```

两个约定值得说明：

1. **`build-app.bat` 每次都先刷新运行时库**。最初只在库不存在时才编译，结果改了 `runtime\`
   之后编译业务程序会报 `undefined reference to cg_xxx` —— 这个错误看着完全摸不着头脑。
   只有 5 个 C 文件，几秒钟，换来「改了就能编」的确定性（`build-all.bat` 统一刷一次后跳过）。
2. **每个工程里那个 `build-app.bat` 是薄壳**：它带着工程名去调项目根那个脚本。
   复制一份完整逻辑过去很诱人，但两份脚本迟早各改各的，所以只留一份真正的实现。

单独编译一个 `.c`（IDE 里配）：

```
gcc -std=c11 -O2 -Iruntime\include -c apps\myapp\main.c -o main.o
gcc -o myapp.exe main.o libcstackgui.a -lcomctl32 -luser32 -lgdi32 -lshell32
```

### 6.2 画布（CStackGUI.exe）

```bat
cd designer
npm install
npm run dev          :: 开发：vite + electron（一条命令，脚本自己等端口）
npm run build        :: 前端产物 -> designer\dist
npm run dist         :: electron-builder -> designer\release\CStackGUI.exe
```

`designer/electron/main.mjs` 按下面的顺序找**项目根**（找到哪个算哪个）：

1. 便携版环境变量 `PORTABLE_EXECUTABLE_DIR`（release\ 里那个 exe 一启动就能认出自己所在的发布包）
2. 环境变量 `CSTACKGUI_HOME`
3. 上次用户手动指定的目录（存在 Electron 的 userData 里，画布里有「定位项目根」按钮）
4. 从程序位置往上找含 `runtime/include/cstackgui.h` 的目录

找到后「导出 C 骨架」默认落 `<root>\apps\<名字>\`，右侧面板也能给出准确的编译命令。

### 6.3 发布包

```bat
make-release.bat     :: 全量构建 -> 打包画布 -> 组装 release\ -> 在 release\ 里真编译真跑一遍自检
```

发布包里的 `build-app.bat` 和开发仓库里那个是**两份不同的脚本**：发布包的用
`runtime\lib\libcstackgui.a` 预编译库（用户没有也不需要运行时源码），开发仓库的从
`runtime\src` 现编。物料模板放在 `packaging\`，`make-release.bat` 负责拷进去 ——
改使用说明或编译脚本改模板，重跑 make-release 即可。

### 6.4 验证入口

```bat
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui  :: 解析 + 控件树 + 合成事件，不开窗
examples\gallery\gallery.exe --trace    examples\gallery\gallery.cgui  :: 开窗前打印一遍文档树
tools\run-tests.bat                                             :: C 层单测（tests\）：JSON / .cgui 解析 / 类型表 / 事件表
tools\run-tests.bat --memcheck                                  :: 同上，再叠一层堆泄漏记账（ld --wrap malloc/free）
tools\check-format.bat                                          :: 画布写的 .cgui 必须能被 C 侧读进来自检通过
tools\bat-utf8.ps1                                              :: 仓库 bat 的编码/前导契约（UTF-8 无 BOM + CRLF + ASCII 前导）
```

**为什么 bat 需要一段 ASCII 前导**：cmd 解码批处理用的是"**启动这个 cmd 进程时**"的控制台代码页
（中文 Windows 双击时是 936），而且此后 `chcp` 也改不了已经定下的解码口径。UTF-8 的文件里中文是
3 字节/字，被按 2 字节/字解码后 cmd 在文件里的行定位越读越偏，半句注释被当命令执行（双击时满屏
`"... is not recognized ..."`，但编译本身还能跑完，所以很容易被当成"只是显示问题"）。所以文件开头放
一段**纯 ASCII 前导**：先读当前代码页，不是 65001 就 `chcp 65001` 并用 `cmd /c "%~f0" %*` 把同一个
文件在新进程里重跑一遍 —— 那一趟是新进程、启动时就是 65001，整个文件从头按 UTF-8 解码且中途不再
切页，中文注释与中文输出都正常（双击路径与 UTF-8 终端都实测过）。三条实测铁律：带 BOM 会让 cmd
把首行报成 `'锘緻echo'`；脚本**内部**切代码页 + 后面有中文 + 括号块/标签会错位；带中文的 bat 里
`goto` / `call :label` 会让标签扫描漂移（结局是结尾一行 `"*"` 报错或莫名 `Press any key`），子过程
要拆成独立 .bat（见 `tools\gen-launcher.bat`）。`tools\bat-utf8.ps1 -Mode fix` 负责归位、
`-Mode check` 已并入 `check-format`；`designer\src\codegen.ts` 生成的导出脚本带同一段前导。

`tools\run-tests.bat` 编译 `tests\` 下全部 `.c`（套件用 `CG_TEST_SUITE` 自注册，新增测试文件
不用改任何清单），链接运行时库后运行。它盯的是**两处"同一个事实有两份"的地方**：

- `k_kinds`（C）↔ `KINDS`（画布）：类型表的每一行都要能被枚举反查、类型名不重复、事件位掩码
  与 `cg_kind_supports_event()` 完全一致（`tests\unit\test_kind.c`）；
- `.cgui` 字段语义（C 侧真相）↔ 画布的读写：解析出来的标题/尺寸/样式/事件名/结构化内容都是
  可执行的契约（`tests\unit\test_doc.c`）。

`--memcheck` 用 `ld --wrap` 拦截测试程序自己（含静态链接进来的运行时库）的
malloc/free/calloc/realloc，逐套件报告"跑完还剩多少存活堆块"。**不用 ASan 是因为本机
MinGW-w64 没有 libasan**（实测 `ld: cannot find -lasan`），Windows 上也没有 valgrind；
它只看得见本程序的分配，所以一旦出现"释放比分配多"就只提示记账不可信，不硬判泄漏。

`--selftest` 会调用 `desc.on_ready`，所以"启动时挂业务数据"那段逻辑也在覆盖范围内。

`--selftest` 是**不开窗口的端到端检查**：它会把每个绑定了回调的**控件**都合成一次事件并真的调用你的 C 函数（`on_ready` 也会被调一次），返回码非 0 表示有事件名在 C 侧没登记、或绑了该控件不支持的事件。**窗口级事件（写在 `app.events` 里）不合成触发** —— 合出来会真的关窗退出，所以只核对回调名有没有登记（见 `cg_app.c` 的 `selftest_walk()`）。放在 CI 里就能保证「画布改了事件名但忘了改代码」这类问题不漏。

---

## 7. 目录结构

**核心原则：把"仓库自带的示例"和"用户自己的工程"分开**，别让用户在删示例时把
自己的东西一起删了，也别让示例目录越堆越乱。

```
CStackGUI/
  README.md                   开发者视角的总览（使用者看 release\README.md）
  build-runtime.bat           编译运行时库 -> build\lib\libcstackgui.a
  build-app.bat <名字>        编译一个工程（按 apps\ -> examples\ 顺序找）
  build-all.bat               一条命令：运行时 + 所有示例
  make-release.bat            一键打发布包 -> release\（可随时删掉重打）
  runtime/                    C 运行时库（画布不依赖它，业务 exe 依赖它）
  examples/                   仓库自带的示例
    gallery/                  控件陈列室·三合一（覆盖 33 类控件；42 类齐全的是"六课 + 陈列室"整体）
  apps/                       用户自己的工程（画布「导出 C 骨架」默认导到这里）
    README.md                 说明这个目录是干嘛的
  designer/                   画布（Electron + React + TS）
    src/model.ts              .cgui 数据模型 + KIND_EVENTS 事件表（与 cg_doc.c 对应）
    src/codegen.ts            导出 C 骨架
    electron/main.mjs         文件读写、导出、菜单、项目根记忆（便携版自动识别）
  packaging/                  发布包里的物料模板（编译脚本、使用者说明）
  tests/                      C 层单元测试（框架 + 套件，套件自注册；入口 tools\run-tests.bat）
  tools/                      开发期检查工具（格式契约回归测试等）
  docs/                       设计文档（本文）+ 格式规范
  build/                      纯中间产物（各 .o + 运行时库，可随时删）
  release/                    发布包（make-release.bat 的产物，gitignore）
```

发布包（`release\`）是**自包含**的，结构刻意和仓库不同：

```
release/
  CStackGUI.exe               画布（便携版，启动时自动把所在目录当项目根）
  build-app.bat               编译脚本：用 runtime\lib 预编译库，不需要源码
  README.md                   使用者说明（从双击 exe 到出 exe 的完整流程）
  runtime/include/ lib/       头文件 + 预编译运行时库
  examples/gallery/           可运行的示例（exe 都在里面）
  apps/                       用户导出的工程落这里
```

---

## 8. 已落地 / 已知限制 / 下一条路

**已经落地（本节曾列为"下一步"）**

- **自绘外观层**：没走 D2D（本机 MinGW 工具链没有 `d2d1.h` / `dwrite.h`），改用 **GDI+**，
  整层放在 `runtime/src/skin/`（`cg_skin.{h,c}` + `cg_skin_edit.c`）。按钮/开关/勾选/滑块/进度条/圆环
  都有 M3 观感，圆角与配色生效；将来换 D2D 内核时整目录替换，上层接入点不动（见 `docs/M3-SKIN.md`）。
- **画布内一键运行 / 发布**：工具条「运行」（F5）编译并启动工程；「发布」组装 `publish\<名字>\` 并打 zip。
- **控件补齐到 42 种**：含 `iconbutton`、`monthcal`、`progressring`、`canvas`、`colorpicker`、`browser`、
  `rate`、`pagination`、`badge`、`tag`、`card`、`shape`、`timer`。
- **分隔条（splitter）**："能拖动的分区边界"。拖它 = 撑开前一块、后面的兄弟整体
  让位、同一容器里最后一块吃掉余量 —— 所以 [日志区/分隔条/发送区] 这种布局不需要布局引擎（见 §3.3）。
  自绘窗口类 `CStackGUISplitter`；`format` 缺省 `col`（竖条），`row` 是横条。
- **终端（terminal）**：通信/调试工具最需要的那块"日志视图"。业务用
  `cg_term_write(w, CG_TERM_RX/TX/SYS, buf, len)` 灌内容，格式（时间戳、HEX）、行数上限、
  按方向着色都由控件管；后端是**只读 RichEdit + 逐行 CHARFORMAT**，所以"选择复制"与
  自动滚动是白捡的（日志最常被做的事就是复制出去）。行在 core 里存为 `items[]`，
  **首字节是方向标记**（裁剪/清空/保存只需面对一个数组）。
- **图表（chart）**：实时曲线。业务用 `cg_chart_push*` 推点，窗口、范围、网格与绘制
  都由控件管；两个刻意的做法：**窗口满了整体左移一格**（不做环形缓冲 —— cap 只有几十到几百，
  memmove 一下比维护读写指针便宜，也不会出现"绕回时算错下标"这种隐蔽 bug）、
  **点数超过绘图区像素宽度时按像素列取 min/max 画竖线**（LVGL 的做法，不抽点所以尖峰不丢）。
  自绘窗口类 `CStackGUIChart`（GDI：折线用一次 `Polyline` 批量提交，数据线不做抗锯齿）。
  `cg_widget_sync()` 会顺手把点缓冲对齐 `items`/`value`，所以查询与绘制看到的永远是与配置一致的状态。
- **状态灯（led）**："看一眼就知道通没通"的那种小灯。业务只给一个状态
  （`cg_led_set(w, 0/1/2)` = 灭 / 亮 / 闪，`cg_led_state` 读回、`cg_led_set_color` 换颜色），
  **闪烁由控件自己定时重绘**（按 `period` 的前后半个周期各亮/灭一次）——业务不用为"闪"另开时钟，
  `cg_led_set(w, 2)` 之后就不管了。两条边界：`period` 小于 100 会被**兜回缺省 500**
  （太快人眼不觉得在闪，只会像"暗的"）、`value` 超范围夹到 0..2。纯显示控件：没有任何事件。
  自绘窗口类 `CStackGUILed`（窗口过程自己按 `period` 定时重绘，所以"闪"不占用业务的消息与时钟）。
- **仪表盘（meter）**：一个刻度量表。业务用 `cg_meter_set(w, v)` 给值
  （被 `min`/`max` 夹，和滑块同一套语义）、`cg_meter_set_range(w, min, max)` 改量程；
  `format` 是形状，`arc`（缺省）从 135° 顺时针扫 270° 到 45°、`half` 从 180° 扫 180°（左端到右端）。
  表由底弧（灰）+ 值弧（彩色，随值增长）+ 刻度线 + 指针 + 中心大数值 + 单位文字一起画。
  值超量程按边界夹取后画，不会跑出弧外；`max <= min` 有两层：`cg_meter_set_range` 会纠正成
  `min+1`（与通用规矩一致），而角度函数拿到非法量程时**停在起点角**（arc 135° / half 180°）
  不除零。纯显示控件，没有事件。
  自绘窗口类 `CStackGUIMeter`。
- **控件绑定**：`.cgui` 的 `bind` / 运行时的 `cg_bind()`，值·文字·勾选自动同步（见 §3.3）。
  思路来自同类项目 YMGUI 的 subject-observer，但按我们"原生控件 + `.cgui` 描述界面"的路线
  简化成"值镜像"，不引入独立 subject 对象。
- **动作层（actions）**：`.cgui` 顶层 `actions` 把"一条命令"写成 文字 + 快捷键 + 回调名，菜单项与控件用
  `"action": "save"` 引用它 —— 一个回调、一处启用状态同时管住所有入口（见 §3.3）。快捷键走消息循环里的
  加速键表（`HACCEL` + `TranslateAcceleratorW`），所以**焦点在输入框里也按得出来** —— 窗口级 `keydown`
  做不到这一点（按键会被 EDIT 吃掉、不冒泡）。
- **通信层（串口 / 网口）**：`runtime/src/kit/cg_serial.c` 与 `cg_net.c`，业务用 `cg_serial_*` /
  `cg_net_*` 建链路与收发，业务代码照旧只 include `cstackgui.h`。**运行时的工作线程只出现在这两个文件里**
  （且只服务于串口与 TCP —— **UDP 不开读线程**）—— 读线程只把字节搬进 64KB 环形缓冲（满了丢最老），
  业务必须在 `cg_set_timer()` 的时钟回调里 `cg_serial_read()` / `cg_net_read()` 取走再灌进 `terminal`
  控件：控件不是线程安全的，这条线不能破。
  串口：帧格式（数据位/停止位/校验）与波特率都能配、一个句柄一条线、**没有流控**、不自动重连；
 网口 TCP 不做 TLS、不自动重连；
  UDP 是另一套句柄（`cg_net_udp_open` / `cg_net_sendto` / `cg_net_recvfrom`），**刻意不套"读线程 + 环形缓冲"**
  （报文边界与来源地址必须原样交给业务），也没有组播。
  这三套的取舍、可抄代码与排查表都在 `docs/COMM.md`（串口 §1~§6 / 网口 TCP §7~§9 / UDP §10~§11）。
- **网口（TCP）**：`cg_net_*` = 客户端 `cg_net_connect()` / 服务端 `cg_net_listen()` + `cg_net_accept()`，
  与串口**完全同构**（每条连接一个读线程 + 一份 64KB 环形缓冲），所以上一条"线程只搬字节"的约束原样适用。
  两处刻意的设计：**`accept` 放在时钟里轮询**（`cg_net_accept(srv, 0)` 不等、没有连接返回 `NULL`
  是正常情况而不是错误）、**一个句柄 = 一条连接**（多客户端由业务自己拿句柄数组，本层不引入"客户端列表"）；
  `connect` 用非阻塞 + select 实现超时，坏地址不会把界面冻住十几秒。链接需要 `-lws2_32`
  （四处构建脚本已加），这是本项目第一项"必须新增"的链接依赖 —— `ws2_32.dll` 是系统核心 DLL，
  不影响"单 exe、零依赖"。能自验到什么程度见 `tests/unit/test_net.c`（本机回环端到端）。

**限制（都是明确取舍，不是没做完）**

- **系统控件不吃自定义外观**：列表框 / 树 / 表格 / 标签页 / 月历 / 下拉框的外观由系统主题决定；
  只有自绘控件（按钮、开关、勾选、输入框、滑块、进度条、圆环）吃圆角与配色。
- 窗口大小固定，没有布局引擎，拉伸不重排。
- 画布是单选，没有多选、组合、对齐工具条（有网格吸附和邻近参考线）。
- 画布不预览真实图片（只显示 `src` 的文件名），运行时才画。
- 一键运行（F5）要求工程在项目根的 `apps\`/`examples\` 下且已导出过 C 骨架——
  这是 `build-app.bat` 的查找规则决定的，画布会在不满足时给出明确指引。
- **绑定不做格式换算**：只镜像值/文字/勾选三类语义，没有"0~100 映射到 0~5"、没有单位后缀、
  不做列表整行结构绑定；需要格式化仍然要写回调（回调绑在**源**控件上）。

**下一步的候选**

1. **D2D 外观内核**：拿到带 `d2d1.h` 的工具链后替换 `skin/`，换来波纹/阴影这类真特效。
2. **画布体验**：多选/组合、对齐工具条、画布内预览真实图片、`.cgui` 里加 `notes` 字段写行为说明。
3. **树形表格 / 更丰富的表格列**（图标列、进度列）。
4. **运行时菜单增删 API**（目前菜单是声明式的，运行时只读）。
