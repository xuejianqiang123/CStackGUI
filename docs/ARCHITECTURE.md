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
        │  cg_json / cg_doc / cg_app     │  ← 解析、控件树、事件派发
        │  cg_win32 (后端：窗口+原生控件) │
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

| `cg_event_t` | `.cgui` 里的键 | 触发来源（Win32 后端） | 谁支持 |
| --- | --- | --- | --- |
| `CG_EVENT_CLICK` | `click` | `BN_CLICKED` / `STN_CLICKED` | button、iconbutton、link、image、菜单类 |
| `CG_EVENT_DBLCLICK` | `dblclick` | 运行时自己判定（见下）；列表/树/表格用原生 `*_DBLCLK` | 同上 + listbox、tree、table、monthcal |
| `CG_EVENT_CHANGE` | `change` | `EN_CHANGE` / `BN_CLICKED`(勾选类) / `WM_HSCROLL`(滑块) / `UDN_DELTAPOS`(微调) / `DTN_DATETIMECHANGE` | edit、multiedit、richtext、spinner、datepicker、checkbox、radio、switch、slider |
| `CG_EVENT_SUBMIT` | `submit` | 回车（`IsDialogMessage` → `IDOK`，落在当前焦点的输入框上） | 只有 edit |
| `CG_EVENT_SELECT` | `select` | `LBN_SELCHANGE` / `CBN_SELCHANGE` / `TCN_SELCHANGE` / `TVN_*` / `LVN_ITEMCHANGED` / `MCN_SELECT` | listbox、combo、tabs、tree、table、monthcal |
| `CG_EVENT_RELEASE` | `release` | 滑块 `TB_ENDTRACK`（拖动中只发 `change`） | 只有 slider |
| `CG_EVENT_EXPAND` / `COLLAPSE` | `expand` / `collapse` | `TVN_ITEMEXPANDEDW`（回调里 `cg_tree_selected()` 能拿到是哪个节点） | 只有 tree |
| `CG_EVENT_CLOSE` | `close`（写在 `app.events`） | `WM_CLOSE`，回调里可 `cg_cancel_close()` 拦下 | 只有窗口 |

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

类型表在 C 侧是 `cg_doc.c` 的 `k_kinds`，在画布侧是 `model.ts` 的 `KINDS`——**两处必须同步**，新增控件就是各加一行 + 后端补一个分支。

| type | 中文 | 容器 | Win32 实现 |
| --- | --- | --- | --- |
| `button` | 按钮 | 否 | `BUTTON` + `BS_PUSHBUTTON` |
| `label` | 标签 | 否 | `STATIC`（`SS_LEFT/CENTER/RIGHT`）|
| `edit` | 单行输入 | 否 | `EDIT` + `ES_AUTOHSCROLL` |
| `multiedit` | 多行输入 | 否 | `EDIT` + `ES_MULTILINE` |
| `spinner` | 数字微调 | 否 | `EDIT` + `msctls_updown32`（**一个控件两个 HWND**，见 4.1）|
| `checkbox` / `radio` | 复选/单选 | 否 | `BUTTON` + `BS_AUTOCHECKBOX` / `BS_AUTORADIOBUTTON` |
| `switch` | 开关 | 否 | `BUTTON` + `BS_OWNERDRAW`，父窗口处理 `WM_DRAWITEM` 自绘药丸 |
| `listbox` | 列表框 | 否 | `LISTBOX` + `LBS_NOTIFY` |
| `combo` | 下拉框 | 否 | `COMBOBOX`（下拉列表样式）|
| `slider` | 滑块 | 否 | `msctls_trackbar32` |
| `progress` | 进度条 | 否 | `msctls_progress32` |
| `tabs` | 标签页 | **是** | `SysTabControl32`；子控件按 `page` 显隐（切页由运行时管）|
| `tree` | 树 | 否 | `SysTreeView32`；`item_map` 保存 逻辑下标 → `HTREEITEM` |
| `table` | 表格 | 否 | `SysListView32` 报表模式（列 + 行）|
| `statusbar` | 状态栏 | 否 | `msctls_statusbar32`，分段宽度按控件宽度均分 |
| `menu` | 菜单栏 | 否 | 窗口 `HMENU`：递归建菜单并 `SetMenu`，用户点中后 `WM_COMMAND`（`lParam==0`）回来 |
| `menubutton` | 菜单按钮 | 否 | `BUTTON` + `TrackPopupMenu(TPM_RETURNCMD)`，直接用返回值，不走消息路由 |
| `toolbar` | 工具栏 | 否 | `ToolbarWindow32`：文字池 + `BTNS_SHOWTEXT` 做纯文字按钮，点击走 `WM_COMMAND` |
| `richtext` | 富文本 | 否 | `RICHEDIT50W`（`Msftedit.dll`，缺了就退 `RichEdit20W` 再退普通 `EDIT`）|
| `datepicker` | 日期时间 | 否 | `SysDateTimePick32`；`datetime` 就是两个 DTP 并排（用 `native2`）|
| `group` / `box` | 分组框/容器块 | **是** | 自注册窗口类 `CStackGUIPanel`（转发子控件通知、画背景）|
| `separator` | 分割线 | 否 | `STATIC` + `SS_ETCHEDHORZ` |
| `image` | 图片 | 否 | 自注册窗口类 `CStackGUIImage`，GDI+ 读图 + 自绘缩放 |

菜单栏**没有矩形**：它是挂在窗口上的 `HMENU`，所以 `cg_doc` 里 `def_w/def_h` 为 0，
`create_native` 直接跳过它，改在 `win_init` 里 `SetMenu`，之后再加一次客户区尺寸校准
（菜单栏会占掉客户区顶部，不校准的话 `app.height` 就对不上了）。

`image` 用 GDI+ 读 png/jpg/bmp/gif，路径按「原样 → 相对 `.cgui` 目录 → 相对 exe 目录」依次找，
第 2 条最符合直觉（`.cgui` 和 `assets\` 一起搬走就能跑）。GDI+ 只用 4 个函数，
`cg_win32.c` 里按官方 ABI 手写声明（MinGW 的 `gdiplusflat.h` 在 C 下编不过）；
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

`cg_widget_add` 可以运行时造控件，id 同样参与事件名查找——先在 `.cgui` 里写好事件名、再在运行时造控件也行。

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
    cg_json.{h,c}       只读 JSON DOM 解析（支持 \u 转义与代理对、UTF-8 原文）
    cg_doc.{h,c}        .cgui -> 控件文档树；类型表；打印
    cg_internal.h       运行时内部结构 + 后端接口（业务代码不该看）
    cg_app.c            控件树构建、名字绑定解析、事件派发、公共 API、自检
    cg_timer.c          一次性定时器（后端 50ms 心跳驱动）
    cg_win32.c          Win32 后端：窗口、原生控件、消息 -> 事件映射
  res/                  清单（v6 通用控件 + 每显示器 DPI）与版本资源
```

### 4.1 四条容易踩的实现约束

1. **原生控件必须按树序创建（父先于子）。** `app->widgets` 是按创建顺序头插的链表，
   它和树序相反；照链表建控件会出现"子控件的父 HWND 还是 NULL"，`CreateWindowEx` 静默失败，
   整个分组框里的控件全丢。所以 `win_init` 用 `create_native_tree()` 从 `app->window` 递归下去建。
2. **同步逻辑要防重入。** 回调里改控件会触发原生通知（`EN_CHANGE` 等），再回到回调就递归了。
   `cg_win32.c` 用 `g_in_notify` 挡住这种嵌套：`sync_widget()` 外层包一层（保存/恢复原值），
   `notify_from()` / `notify_common()` 也各包一层。
3. **一个控件可能有第二个 HWND。** `spinner` = 输入框 + 上下箭头两个窗口，
   所以按控件句柄反查控件要用 `widget_by_hwnd()`（同时比对 `native` 和 `native2`），
   只按 `GetDlgCtrlID()` 找会漏掉箭头那条消息。
4. **子窗口的通知先到父窗口。** 控件把 `WM_COMMAND` / `WM_NOTIFY` / `WM_DRAWITEM` / `WM_HSCROLL`
   发给**直接父窗口**；分组框里的树、表格、开关要靠 `PanelProc` 把这几种消息转发回主窗口，
   少转发一种，那种控件在分组框里就会"没反应"。

**分层规矩**：`cg_app.c` 及以上不知道 Win32 的存在，`cg_win32.c` 不知道业务的存在。两者之间是 `cg_backend_t` 接口：

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
| 运行时用 **Win32 原生控件**，不用 SDL/GTK | 业务 exe 必须是**单文件**、零依赖；原生控件白送输入法、无障碍、系统主题 | `style.radius`（圆角）和自定义配色大多不生效（原生控件不给画）；要 M3 外观得加自绘后端。个别控件（`switch`）用 owner-draw 自绘，算是这条路的样板 |
| 用 **MinGW-w64 + windres** | 免装 Visual Studio，`gcc` 一条命令出 exe；`-mwindows` 就是 GUI 程序 | 需把 `mingw64\bin` 加进 PATH（脚本里已内置默认路径） |
| `.cgui` 用 **JSON** | 人能读能改、diff 友好、画布侧零解析成本 | C 侧要自带一个 JSON 解析器（约 400 行，已实现） |
| **绝对坐标**，没有布局引擎 | C 侧实现量小、所见即所得、和 Win32 的心智模型一致 | 窗口拉伸不会自动重排（v1 不做自适应） |
| 事件靠**名字**绑定 | 解耦、可共用、跨版本安全 | 拼错名字不会编译报错（用 `--selftest` 和运行警告兜住）|
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
build-app.bat hello                  :: 刷新运行时库 + 链接 examples\hello\ -> examples\hello\hello.exe
build-app.bat hello --release        :: -mwindows，无控制台，交付用
build-app.bat myapp                  :: 你自己的工程 apps\myapp\（画布导出的默认位置）
build-runtime.bat                    :: 只编译运行时库（平时由 build-app 代劳）
```

**产物落在工程自己的目录里，不留构建痕迹**：

```
build\               纯构建目录（各 .o + 运行时库源头，可随时删）
examples\hello\
  hello.exe            双击即跑：界面文件就在旁边
  lib\libcstackgui.a   运行时库副本，整个目录搬走也能重编译
  build-app.bat        本工程的编译入口（生成物，双击即可重编译这一个）
  hello.cgui / main.c / assets\...
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

### 6.4 三道验证

```bat
examples\hello\hello.exe --selftest examples\hello\hello.cgui  :: 解析 + 控件树 + 合成事件，不开窗
examples\hello\hello.exe --trace    examples\hello\hello.cgui  :: 开窗前打印一遍文档树
tools\check-format.bat                                          :: 画布写的 .cgui 必须能被 C 侧读进来自检通过
```

`--selftest` 会调用 `desc.on_ready`，所以"启动时挂业务数据"那段逻辑也在覆盖范围内。

`--selftest` 是**不开窗口的端到端检查**：它会把每个绑定了回调的控件都合成一次事件并真的调用你的 C 函数，返回码非 0 表示有事件名在 C 侧没登记、或绑了该控件不支持的事件。放在 CI 里就能保证「画布改了事件名但忘了改代码」这类问题不漏。

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
    hello/                    最小闭环（8 个回调）
    gallery/                  29 种控件的陈列室（每种控件+事件都用到）
  apps/                       用户自己的工程（画布「导出 C 骨架」默认导到这里）
    README.md                 说明这个目录是干嘛的
  designer/                   画布（Electron + React + TS）
    src/model.ts              .cgui 数据模型 + KIND_EVENTS 事件表（与 cg_doc.c 对应）
    src/codegen.ts            导出 C 骨架
    electron/main.mjs         文件读写、导出、菜单、项目根记忆（便携版自动识别）
  packaging/                  发布包里的物料模板（编译脚本、使用者说明）
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
  examples/hello/ gallery/    可运行的示例（exe 都在里面）
  apps/                       用户导出的工程落这里
```

---

## 8. v1 的已知限制与下一条路

**限制（都是明确取舍，不是没做完）**

- 圆角 / 自定义控件配色在 Win32 后端大多不生效（`label` 能改文字与背景色，`switch` 因为是自绘所以吃配色）；
  按钮等系统控件不给画。
- 窗口大小固定，没有布局引擎，拉伸不重排。
- 画布是单选，没有多选、组合、对齐工具条（有网格吸附和邻近参考线）。
- 画布不预览真实图片（只显示 `src` 的文件名），运行时才画。
- 一键运行（F5）要求工程在项目根的 `apps\`/`examples\` 下且已导出过 C 骨架——
  这是 `build-app.bat` 的查找规则决定的，画布会在不满足时给出明确指引。

**建议的下一步排序**

1. **画布内运行**：把导出的目录直接编译并启动（或嵌入画布下方看运行结果）。改界面 → 一键看到真实效果，闭环才算真正短。
2. **自绘后端**（M3 外观）：加 `cg_backend_d2d.c`，实现圆角/波纹/主题色。`switch` 和 `image` 已经走通了
   自绘 + `WM_DRAWITEM` 这条通路，可以当起点。
3. **画布体验**：多选/组合、对齐工具条、画布内预览真实图片、`.cgui` 里加 `notes` 字段写行为说明。
4. **还缺的控件**：图标/图片按钮、树形表格、日历（`SysMonthCal32`）、进度环，
   以及 Material 那批纯视觉组件（FAB / chip / navRail / loading indicator 等 ——
   它们必须先有自绘后端才谈得上）。菜单、工具栏、富文本、日期时间已经做完了。
