# .cgui 文件规范 v1

`.cgui` 就是一个 UTF-8 编码的 JSON 文件，描述一个窗口和它里面的控件。
文件可以由画布（CStackGUI.exe）保存，也可以人手写——两者格式完全一致。

解析实现：`runtime/src/core/cg_doc.c`（C 侧）、`designer/src/model.ts`（画布侧）。
**两处对字段的理解必须一致**，`tools/check-format.bat` 就是防它们漂移的回归测试。

---

## 1. 顶层

```json
{
  "cgui": 1,
  "app": {
    "title": "我的程序",
    "width": 700,
    "height": 500,
    "bg": "#F3EDF7"
  },
  "widgets": [ /* 控件数组 */ ]
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `cgui` | 整数 | 否 | 格式版本。缺省按 1 处理；大于 1 时运行时拒绝加载并报错，避免新版文件被旧运行时静默误读 |
| `app.title` | 字符串 | 否 | 窗口标题，缺省 `CStackGUI Application` |
| `app.width` / `app.height` | 整数 | 否 | 客户区尺寸（逻辑像素），缺省 640×420，最小值 80×60 |
| `app.bg` | 字符串 | 否 | 窗口底色 `#RRGGBB`；不写则用系统窗口色 |
| `widgets` | 数组 | 是 | 顶层控件。数组顺序 = 层叠顺序，**后面的盖在前面的上面** |

### 1.1 主窗口属性（`app` 的其余字段，全部可选）

对齐「启动窗口」的属性面板。**不写的字段一律保持 Windows 默认行为**，所以老文件完全兼容。

| 字段 | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `app.x` / `app.y` | 整数 | 系统决定 | 窗口左上角屏幕坐标（逻辑像素） |
| `app.center` | 布尔 | `false` | 启动时按当前显示器工作区居中（盖过 `x`/`y`） |
| `app.border` | 字符串 | `sizable` | 边框：`sizable` 可调整 / `fixed` 普通固定 / `dialog` 对话框固定 / `none` 无边框 |
| `app.controlBox` | 布尔 | `true` | 是否显示标题栏按钮组（系统菜单 + 关闭钮） |
| `app.minimizeBox` / `app.maximizeBox` | 布尔 | `true` | 是否显示最小化 / 最大化按钮 |
| `app.topmost` | 布尔 | `false` | 总在最前 |
| `app.taskbar` | 布尔 | `true` | 是否在任务栏显示 |
| `app.visible` | 布尔 | `true` | 启动即显示；`false` 时先隐藏，等业务代码 `cg_win_show()` |
| `app.enabled` | 布尔 | `true` | `false` = 整窗禁用（点了没反应） |
| `app.enterTab` | 布尔 | `false` | 回车当 Tab 下移焦点（默认回车触发 `submit`） |
| `app.escClose` | 布尔 | `true` | Esc 关闭窗口；关掉后 Esc 会当成当前控件的 `submit` |
| `app.tray` | 布尔 | `false` | 建系统托盘图标（用窗口图标）；左键显示、右键弹菜单，并触发 `tray` 事件 |
| `app.icon` | 字符串 | 运行时自带 | 窗口图标 `.ico` 路径，解析顺序同 `src`（见 2.4） |
| `app.cursor` | 字符串 | `arrow` | 窗口鼠标指针：`arrow`/`hand`/`ibeam`/`wait`/`cross`/`sizeall`/`sizens`/`sizewe`/`sizenesw`/`sizenwse`/`no`/`help`/`uparrow` |
| `app.class` | 字符串 | `CStackGUIMain` | 窗口类名（注册 Win32 窗口类用的，一般不用改） |
| `app.note` | 字符串 | — | 备注，**只给画布看**，运行时忽略 |

### 1.2 程序配置（`app.info`，写进 exe 版本信息）

画布工具栏「程序配置」对话框填的内容。编译时由画布生成的 `<工程名>.rc` 落进 exe 的
`VERSIONINFO`，运行时可用 `cg_app_info(app, "键")` 读回。空字段不写。

| 键 | 说明 | 键 | 说明 |
| --- | --- | --- | --- |
| `product` | 程序名称（ProductName）| `author` | 作者（单独的 Author 字段）|
| `desc` | 程序描述（FileDescription）| `company` | 单位 / 公司（CompanyName）|
| `comments` | 程序备注（Comments）| `copyright` | 版权声明（LegalCopyright）|
| `version` | 程序版本号（四段 `1.0.0.0`）| `address` | 地址 |
| `build` | 创建号（和 `version` 拼成完整版本号）| `postcode` | 邮政编码 |
| `phone` | 电话 | `fax` | 传真 |
| `email` | 电子邮箱 | `homepage` | 主页 |

```json
"app": {
  "title": "我的程序", "width": 700, "height": 500, "bg": "#F3EDF7",
  "center": true, "tray": true,
  "events": { "close": "on_close" },
  "info": { "product": "我的程序", "version": "1.0.0.0", "author": "作者" }
}
```

---

## 2. 控件节点

```json
{
  "id": "btn_ok",
  "type": "button",
  "rect": { "x": 348, "y": 372, "w": 200, "h": 44 },
  "text": "确定",
  "style": { "font": "Microsoft YaHei UI", "size": 10, "bold": false, "fg": "#1D1B20", "bg": "#EADDFF", "align": "center", "radius": 8 },
  "visible": true,
  "enabled": true,
  "checked": false,
  "value": 40,
  "min": 0,
  "max": 100,
  "events": { "click": "on_ok_click", "dblclick": "on_ok_dblclick" },
  "children": []
}
```

### 2.1 必填与可省

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | 字符串 | **是** | 见第 4 节类型表。不可识别 → 整个文件加载失败并报出类型名 |
| `id` | 字符串 | 否 | 事件查找、`cg_find()` 都用它。缺省自动生成 `类型_序号`。**同一文件内必须唯一** |
| `rect` | 对象 | 否 | 缺省用该类型的默认尺寸（见第 4 节）。也支持扁平的 `x/y/w/h` 四个顶层字段 |
| `text` | 字符串 | 否 | 显示文字。缺省用类型默认值（按钮 `按钮`、标签 `标签`、分组框 `分组`，输入类为空）|
| `style` | 对象 | 否 | 见 2.2 |
| `events` | 对象 | 否 | 见第 3 节 |
| `children` | 数组 | 否 | 只有容器类型（`group` / `box` / `card` / `tabs`）能带；**非容器带 children 会被运行时判为错误** |
| `page` | 整数 | 否 | 属于父 `tabs` 的第几页（0 起）。缺省 0 |
| `src` | 字符串 | 否 | `image` 的图片路径，见 2.4 |
| `format` | 字符串 | 否 | `datepicker` 的细分格式：`date`（缺省）/ `time` / `datetime`；`shape` 的外形：`rect`（缺省）/ `roundrect` / `ellipse` |
| `hint` | 字符串 | 否 | 鼠标悬停提示（tooltip），**所有控件通用**；不写 / 空 = 无提示 |
| `period` | 整数 | 否 | `timer` 的触发周期（毫秒）。**不写 = 0 = 不触发**（画布新建时钟时给 1000）|
| `multi` / `check` | 布尔 | 否 | `table`：`multi` = 允许整行多选，`check` = 显示行首复选框列（配合 `check` 事件）|
| `items` | 字符串数组 | 否 | `listbox` / `combo` / `tabs`（页签文字）/ `statusbar`（分段文字）/ `toolbar`（按钮文字）的内容 |
| `nodes` | 对象数组 | 否 | `tree` 的节点树（2.5）或 `menu`/`menubutton` 的菜单项（2.7）|
| `columns` / `rows` | 数组 | 否 | `table` 的列与行，见 2.6 |

### 2.2 `style`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `font` | 字符串 | 字体名，如 `Microsoft YaHei UI`。不写 = 系统 UI 字体 |
| `size` | 整数 | 字号（**磅**）。`0` / 不写 = 系统默认字号 |
| `bold` | 布尔 | 是否加粗 |
| `fg` | 字符串 | 文字色 `#RRGGBB`，也接受 `#RGB` / `#AARRGGBB` / 十进制 |
| `bg` | 字符串 | 背景色，同上。**`label`/`link`/`separator` 不写 = 透明（透出窗口底色）**；其它控件不写 = 跟随系统 |
| `align` | `left`\|`center`\|`right` | 文本对齐（`label` 生效）|
| `radius` | 整数 | 圆角半径。**自绘控件生效**（按钮/开关/勾选/输入框/进度条等，`runtime/src/skin/`），系统控件忽略；不写用默认值（按钮 6、输入框 2）|

### 2.3 状态字段

既可以直接写在节点上，也可以放进 `state` 对象里（两者同时存在时顶层优先，方便手写小文件）。

| 字段 | 类型 | 默认 | 生效控件 |
| --- | --- | --- | --- |
| `visible` | 布尔 | `true` | 全部 |
| `enabled` | 布尔 | `true` | 全部 |
| `checked` | 布尔 | `false` | `checkbox` / `radio` |
| `value` | 整数 | `0`（数值类默认 40）| `slider` / `progress` / `listbox`(选中项序号) |
| `min` / `max` | 整数 | `0` / `100` | `slider` / `progress` / `spinner` / `rate`（星数 = `max-min`，上限 10）/ `pagination`（页数 = `max-min+1`）。`max <= min` 时自动修正为 `min+1` |

### 2.4 `src`：图片路径

```json
{ "id": "img_logo", "type": "image", "rect": {...}, "src": "assets/logo.png" }
```

解析顺序（第一个存在的就用）：

1. 原样（相对进程当前目录）
2. **相对 `.cgui` 文件所在目录** ← 正常情况走这条
3. 相对 exe 所在目录

支持 png / jpg / bmp / gif（走 GDI+）。找不到文件时控件画成虚线占位框并在 stderr 报警，**不会导致加载失败**。
图片按控件大小拉伸（HALFTONE，不变形拉伸）。

### 2.5 `nodes`：树

```json
{
  "type": "tree",
  "nodes": [
    { "text": "项目", "expanded": true, "children": [
        { "text": "runtime" },
        { "text": "apps" }
    ]},
    { "text": "构建产物" }
  ]
}
```

`expanded` 缺省为 false（都折叠）。运行时也支持用 `cg_tree_add()` 在代码里往任意层追加。

### 2.6 `columns` / `rows`：表格

```json
{
  "type": "table",
  "columns": [ { "title": "姓名", "w": 140 }, { "title": "城市" } ],
  "rows": [ ["张三", "上海"], ["李四", "成都"] ]
}
```

`columns` 也接受纯字符串数组（`["姓名","城市"]`）。`w` 缺省或为 0 表示按控件宽度均分剩余空间。
`rows` 的单元格个数少于列数时按空串补齐。

### 2.7 `nodes`：菜单（`menu` / `menubutton`）

```json
{
  "type": "menu",
  "events": { "click": "on_menu_click" },
  "nodes": [
    { "text": "文件", "children": [
        { "text": "新建", "id": "m_new" },
        { "text": "打开", "id": "m_open" },
        { "text": "-" },
        { "text": "退出", "id": "m_quit" }
    ]},
    { "text": "视图", "children": [
        { "text": "状态栏", "id": "m_status", "checked": true }
    ]}
  ]
}
```

| 字段 | 说明 |
| --- | --- |
| `text` | 菜单项文字。**写 `"-"` 表示一条分隔线**（分隔线不占 id、不可点）|
| `id` | 菜单项标识。回调里用 `cg_menu_last_id()` 取回，用来判断点的是哪一项 |
| `checked` | 是否显示勾选标记 |
| `children` | 有子项就是下拉菜单（本身不可点）|

- `menu` **没有矩形**（`.cgui` 里可以写 `rect`，但运行时忽略）：它是挂在窗口上的菜单栏，一根窗口只能有一根，
  多写的会被忽略并警告。菜单栏会占掉客户区顶部的高度，运行时自动把窗口调大以保证客户区还是 `app.width × app.height`。
- `menubutton` 是普通按钮，`rect` 照常用；点它在按钮下方弹出同一个菜单。
- 点中任意一个叶子项时，运行时给**这个控件**派发 `click` 事件，并用下面两个函数告诉你是哪一项：

```c
static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *id = cg_menu_last_id(self);       /* .cgui 里写的 id；没写就是 NULL */
    const char *text = cg_menu_last_text(self);   /* 菜单项文字，按文字判断也行 */
    if (id && strcmp(id, "m_quit") == 0) cg_quit(cg_app_of(self));
}
```

菜单项在运行时是**只读**的：v1 没有「运行时增删菜单项」的 API（`cg_menu_count()` 只用来查个数）。

---

## 3. `events`：事件绑定

```json
"events": { "click": "on_ok_click", "dblclick": "on_ok_dblclick" }
```

- 键是事件名，值是 **C 函数名（字符串）**。
- 允许的键（窗口级事件写在 `app.events` 里，见第 4 节）：

| 事件 | 什么时候发 | 谁支持 |
| --- | --- | --- |
| `click` | 点了一下 | 按钮/图标按钮/链接/图片/标签片/菜单类 |
| `dblclick` | 系统双击间隔内点了第二下 | 同上 + 列表/树/表格/月历/画板 |
| `change` | 文字/勾选/数值/颜色变了 | 输入类、开关复选、滑块（拖动中）、颜色选择、评分、分页 |
| `submit` | 单行输入框里按了回车 | 只有单行输入 |
| `select` | 选中项变了 | 列表/下拉/标签页/树/表格/月历 |
| `release` | 滑块松手（拖完才发一次） | 只有滑块 |
| `expand` / `collapse` | 树节点展开/折叠 | 只有树 |
| `tick` | 时钟控件的周期到了（`period` 毫秒一次） | 只有时钟 |
| `check` | 表格勾选列的行勾选状态变了 | 只有表格（`.cgui` 里开 `check`） |
| `gotfocus` / `lostfocus` | 控件获得 / 失去键盘焦点 | 有焦点概念的原生控件（输入类、按钮、开关勾选、滑块、列表/下拉/树/表格/标签页）|
| `close` | 用户点了关闭按钮，窗口关之前 | 只有窗口 |
  不认识的键会被运行时**忽略**（不报错），方便向后兼容。
- 一个函数名可以被多个控件、多个事件共用。
- 运行时的查找规则：在 `cg_app_desc_t.bindings` 表里按名字找函数指针。
  找不到 → 打印警告，该事件不触发；`--selftest` 会把它算作失败（返回码 5）。
- 名字建议 `on_<控件id>_<事件>`（画布上的「自动」按钮就按这个规则生成）。

---

## 4. 控件类型表

| `type` | 中文名 | 容器 | 默认尺寸 | 默认文本 | 支持的 `events` | Win32 实现 |
| --- | --- | --- | --- | --- | --- | --- |
| `button` | 按钮 | 否 | 120×40 | `按钮` | `click`、`dblclick` | `BUTTON`+`BS_OWNERDRAW`（M3 自绘）|
| `iconbutton` | 图标按钮 | 否 | 48×48 | — | `click`、`dblclick` | `BUTTON` 自绘（`src` 指定图片）|
| `label` | 标签 | 否 | 160×24 | `标签` | **无**（纯显示） | `STATIC` |
| `link` | 链接 | 否 | 160×24 | `链接` | `click`、`dblclick` | `STATIC`+`SS_NOTIFY`（手型光标、蓝色下划线）|
| `switch` | 开关 | 否 | 56×32 | — | `change` | `BUTTON` 自绘（M3 胶囊）|
| `edit` | 单行输入 | 否 | 200×32 | — | `change`、`submit` | `EDIT`（圆角+垂直居中自绘）|
| `multiedit` | 多行输入 | 否 | 240×120 | — | `change` | `EDIT` 多行（圆角自绘）|
| `richtext` | 富文本 | 否 | 320×160 | — | `change` | `RICHEDIT50W`（缺库时降级 EDIT）|
| `spinner` | 数字微调 | 否 | 100×28 | — | `change` | `EDIT` + `msctls_updown32`（文本自管，不用 `UDS_SETBUDDYINT`）|
| `datepicker` | 日期时间 | 否 | 140×28 | — | `change` | `SysDateTimePick32`（datetime 用两个 HWND）|
| `monthcal` | 月历 | 否 | 230×180 | — | `select`、`dblclick` | `SysMonthCal32`（选中日期写回 `text`）|
| `checkbox` | 复选框 | 否 | 160×28 | `复选` | `change` | `BUTTON`+`BS_OWNERDRAW`（自绘）|
| `radio` | 单选钮 | 否 | 160×28 | `单选` | `change` | `BUTTON`+`BS_OWNERDRAW`（自绘，互斥靠兄弟遍历）|
| `listbox` | 列表框 | 否 | 220×140 | — | `select`、`dblclick` | `LISTBOX` |
| `combo` | 下拉框 | 否 | 200×200 | — | `select` | `COMBOBOX` |
| `slider` | 滑块 | 否 | 200×32 | — | `change`、`release` | `msctls_trackbar32`+`NM_CUSTOMDRAW`（自绘轨道/滑块）|
| `progress` | 进度条 | 否 | 200×20 | — | **无**（只被写） | 自绘窗口 `CG_PROGRESS_CLASS`（原生不支持 custom draw）|
| `progressring` | 圆环进度 | 否 | 120×120 | — | **无**（只被写） | 自绘窗口（GDI+ 轨道 + 圆头弧）|
| `tabs` | 标签页 | **是** | 320×220 | — | `select` | `SysTabControl32`；子控件按 `page` 显隐 |
| `tree` | 树 | 否 | 220×180 | — | `select`、`dblclick`、`expand`、`collapse` | `SysTreeView32` |
| `table` | 表格 | 否 | 360×180 | — | `select`、`dblclick`、`check` | `SysListView32` 报表模式（`check`/`multi` 开勾选列与多选）|
| `statusbar` | 状态栏 | 否 | 400×24 | — | **无** | `msctls_statusbar32` |
| `menu` | 菜单栏 | 否 | 无矩形 | — | `click` | 窗口 `HMENU`（一根窗口一根）|
| `menubutton` | 菜单按钮 | 否 | 130×32 | `菜单` | `click` | `BUTTON` + `TrackPopupMenu` |
| `toolbar` | 工具栏 | 否 | 400×34 | — | `click` | `ToolbarWindow32`（纯文字按钮）|
| `group` | 分组框 | **是** | 260×180 | `分组` | **无** | 自注册面板窗口（边框+标题完全自绘）|
| `box` | 容器块 | **是** | 200×120 | — | **无** | 自注册面板窗口 |
| `card` | 卡片 | **是** | 280×180 | `卡片` | **无** | 自注册面板窗口 |
| `separator` | 分割线 | 否 | 200×2 | — | **无** | `STATIC` + `SS_ETCHEDHORZ` |
| `image` | 图片 | 否 | 200×120 | — | `click`、`dblclick` | 自绘窗口（GDI+ 读图）|
| `shape` | 外形框 | 否 | 120×120 | — | **无** | 自注册面板窗口（自绘外形）|
| `timer` | 时钟 | 否 | 无矩形 | — | `tick` | **无窗口**，后端 50ms 心跳驱动（`period` 毫秒）|
| `canvas` | 画板 | 否 | 240×160 | — | `click`、`dblclick` | 自绘窗口（`cg_canvas_*` 画线/矩形/椭圆/文字）|
| `colorpicker` | 颜色选择 | 否 | 64×28 | — | `change` | `BUTTON` 自绘色块（点击弹系统取色器，值存 `value`）|
| `browser` | 网页浏览 | 否 | 320×240 | — | **无** | `AtlAxWin` 宿主系统 WebBrowser（起不来降级 `STATIC` 占位；实验性）|
| `badge` | 徽章 | 否 | 48×24 | `99+` | **无** | `BUTTON` 自绘小圆片 |
| `tag` | 标签片 | 否 | 72×28 | `标签` | `click` | `BUTTON` 自绘 |
| `rate` | 评分 | 否 | 140×32 | — | `change` | `BUTTON` 自绘五角星（星数 = `max-min`，上限 10）|
| `pagination` | 分页 | 否 | 180×32 | — | `change` | `BUTTON` 自绘（页数 = `max-min+1`）|

> 上表只列每种控件的**主要**事件。凡是有键盘焦点概念的原生控件
> （`button`/`switch`/`edit`/`multiedit`/`richtext`/`spinner`/`datepicker`/`checkbox`/`radio`/
> `listbox`/`combo`/`slider`/`tabs`/`tree`/`table`）都额外支持 `gotfocus` / `lostfocus`。

**窗口本身也是一个"控件"**（伪控件 id 固定为 `__window`，头文件里是 `CG_WINDOW_ID`）。
它的事件写在 `app.events` 里，和控件事件共用同一套 `CG_BIND` 机制：

```json
"app": { "title": "...", "events": { "ready": "on_ready", "close": "on_close", "tray": "on_tray" } }
```

窗口支持的事件（对齐「启动窗口」的事件表）：

| 事件 | 什么时候发 |
| --- | --- |
| `ready` | 窗口与控件都建好、还没显示；启动初始化放这里 |
| `close` | 用户点关闭（或 Alt+F4）；回调里 `cg_cancel_close(app)` 可拦下这次关闭 |
| `closequery` | 和 `close` 同一时机，两个名字都能拦关闭 |
| `destroy` | 窗口已销毁、进程马上退出 |
| `move` / `size` | 位置 / 客户区尺寸变了（新坐标/尺寸用 `cg_event_x` / `cg_event_y` 取）|
| `activate` / `deactivate` | 被激活 / 被取消激活 |
| `firstactivate` | 启动后第一次被激活 |
| `idle` | 消息队列空着时（别在这里做耗时活）|
| `tray` | 托盘图标被点，`cg_tray_action()` 给 1=单击 / 2=双击 / 3=右键 |
| `show` / `hide` | 被显示 / 被隐藏 |
| `mousedown`/`mouseup`/`rdown`/`rup`/`mousemove` | 在窗口空白处按/放左键、按/放右键、移动鼠标 |
| `dblclick` | 在窗口空白处双击 |
| `gotfocus` / `lostfocus` | 窗口获得 / 失去键盘焦点 |
| `keydown` / `keyup` | 按下 / 放开按键（`cg_event_key()` 键码、`cg_event_keys()` 功能键）|
| `char` | 输入了可打印字符（中文输入法也走这里）|
| `wheel` | 滚轮滚动（`cg_event_delta()` 滚动量，正数向上）|

鼠标/键盘/滚轮事件的参数因为回调签名固定（只有 `self`/`ev`/`user`），存在窗口对象上，
用 `cg_event_x/y/keys/key/delta` 和 `cg_tray_action` 读（`self` 传回调收到的那个控件即可）。

`close` 回调里可以调 `cg_cancel_close(app)` 取消这次关闭（比如还有没保存的内容）。

**事件是按控件特性分配的**，不是每个控件都有全套：

- 能点的才有 `click` / `dblclick`；能改内容的才有 `change`
- 有"选择"概念的才有 `select`；只有树有 `expand` / `collapse`
- 只有滑块有 `release`（拖动中发 `change`，松手才发 `release`）
- 纯显示或只被程序写的控件（标签/进度条/状态栏/容器）一个事件都没有

画布只列出某控件支持的事件；手写 `.cgui` 绑了不支持的事件时，运行时启动会打
警告、`--selftest` 以非 0 退出。两边的事件表是同一份约定：
C 侧 `runtime/src/core/cg_doc_kind.c` 的 `k_kinds`，画布侧 `designer/src/model.ts` 的 `KIND_EVENTS`。

容器里的子控件坐标**相对容器客户区**，不是相对窗口。

- `tabs` 的子控件用 `page` 指定属于第几页；切页时运行时自动显隐，**不需要你在回调里手动 ShowWindow**。
- `statusbar` 的分段按控件宽度均分；用 `cg_status_set(w, i, text)` 改某一段。
- `switch` 有 `text` 时，文字在左、开关在右；没有文字时整块就是开关。
- `toolbar` 的按钮文字来自 `items`；点了第几个用 `cg_widget_value(w)` 取（0 起）。
- `datepicker` 的值就是 `text`：`2026-09-19` / `14:30` / `2026-09-19 14:30`；`format` 决定用哪种。
  控件显示格式跟这个写法对齐，不受系统区域设置影响。
- `monthcal` 同样把选中的日期放在 `text` 里（`2026-09-19`），启动前给它 `text` 就定位到那天。
- `iconbutton` 用 `src` 指定图片（png/jpg/bmp/gif），等比缩放到控件内；没图就画个占位框。
- `timer` 不写 `period` 就等于关着（C 侧缺省 0）；画布新建时钟会给 1000，运行时用 `cg_widget_set_period()` 改。
- `colorpicker` 的当前颜色存在 `value`（`0xRRGGBB`），点色块弹系统取色器。**要程序改颜色别用
  `cg_widget_set_value()`** —— 那条路会按 `min`/`max` 截断，颜色不是区间值。
- `rate` 星数 = `max-min`（上限 10）；`pagination` 页数 = `max-min+1`；当前值都用 `cg_widget_value()` 读。
- `canvas` 用 `cg_canvas_clear/set_color/set_line_width/line/rect/ellipse/text` 在回调里画，
  绘制命令被录下来，重绘时按序回放。
- `badge` / `tag` / `shape` 只有 `text` 与样式（配色/圆角）能配；`tag` 能点（`click`），`badge` / `shape` 没有事件。
- `card` 与 `group` / `box` 同为容器，子控件坐标相对卡片客户区。
- 表格的**行级附加状态**（行勾选 / 行数值 / 行图标）**不是 `.cgui` 字段**，是运行时 API：
  `cg_table_set_checked()` / `cg_table_set_row_value()` / `cg_table_set_row_icon()`。

---

## 5. 一个完整的最小例子

```json
{
  "cgui": 1,
  "app": { "title": "最小示例", "width": 360, "height": 200, "bg": "#F3EDF7" },
  "widgets": [
    {
      "id": "lab_info",
      "type": "label",
      "rect": { "x": 24, "y": 24, "w": 312, "h": 24 },
      "text": "点一下按钮",
      "style": { "font": "Microsoft YaHei UI", "size": 9 }
    },
    {
      "id": "btn_go",
      "type": "button",
      "rect": { "x": 24, "y": 64, "w": 140, "h": 40 },
      "text": "点我",
      "events": { "click": "on_go_click", "dblclick": "on_go_dblclick" }
    },
    {
      "id": "chk_loud",
      "type": "checkbox",
      "rect": { "x": 24, "y": 120, "w": 200, "h": 28 },
      "text": "大声点",
      "checked": true,
      "events": { "change": "on_loud_change" }
    }
  ]
}
```

对应 C：

```c
#include "cstackgui.h"

static void on_go_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_set_text(cg_find(app, "lab_info"), "单击了");
}

static void on_go_dblclick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_widget_set_text(cg_find(cg_app_of(self), "lab_info"), "双击了");
}

static void on_loud_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    printf("loud = %d\n", cg_widget_checked(self));
}

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_go_click",    on_go_click),
    CG_BIND("on_go_dblclick", on_go_dblclick),
    CG_BIND("on_loud_change", on_loud_change),
    CG_BIND_END
};

int main(void)
{
    cg_app_desc_t d = {0};
    d.cgui_path = "minimal.cgui";
    d.bindings  = k_bindings;
    return cg_app_run(&d);
}
```

---

## 6. C API 速查

完整声明见 `runtime/include/cstackgui.h`。
画布右侧的「支持库」页签是这张表的**可搜索版本**（带中文说明与示例，双击复制调用代码），
数据在 `designer/src/supportlib.ts`，由 `tools/check-support.mjs` 保证它和 `cstackgui.h` 不漂移。

```c
/* 启动 */
int cg_app_run(const cg_app_desc_t *desc);
int cg_app_selftest(const cg_app_desc_t *desc);   /* 不开窗自检，CI 用 */
/* desc 里还能放：bindings / on_ready(界面就绪钩子) / ready_user /
 * dblclick_suppress_click / auto_locate_cgui / title_override / cgui_text(内嵌一段 .cgui) */

/* 找 / 读 */
cg_app_t    *cg_app_of(const cg_widget_t *w);
cg_widget_t *cg_find(cg_app_t *app, const char *id);
const char  *cg_widget_id/type/text(const cg_widget_t *w);
int          cg_widget_x/y/w/h(const cg_widget_t *w);
int          cg_widget_visible/enabled/checked/value(const cg_widget_t *w);

/* 改（立即生效，不需要手动刷新） */
void cg_widget_set_text(cg_widget_t *w, const char *text);
void cg_widget_set_rect/pos/size(...);
void cg_widget_set_visible/enabled/checked/value(...);
void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg);   /* CG_COLOR_NONE = 系统默认 */
void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold);

/* 列表类控件（listbox / combo / tabs 标签 / statusbar 分段） */
int  cg_widget_list_add(cg_widget_t *w, const char *item);
int  cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...);
void cg_widget_list_clear(cg_widget_t *w);
const char *cg_widget_list_item(const cg_widget_t *w, int index);

/* 树 */
int  cg_tree_add(cg_widget_t *w, int parent, const char *text);   /* parent = -1 表示根 */
int  cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...);
void cg_tree_clear(cg_widget_t *w);
int  cg_tree_count(const cg_widget_t *w);
const char *cg_tree_item_text(const cg_widget_t *w, int item);
int  cg_tree_selected(const cg_widget_t *w);
const char *cg_tree_selected_text(const cg_widget_t *w);
void cg_tree_expand(cg_widget_t *w, int item, int expand);

/* 表格 */
int  cg_table_add_column(cg_widget_t *w, const char *title, int width);  /* width 0 = 均分 */
int  cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells);
void cg_table_clear(cg_widget_t *w);
int  cg_table_row_count(const cg_widget_t *w);
const char *cg_table_cell(const cg_widget_t *w, int row, int col);
int  cg_table_selected(const cg_widget_t *w);

/* 状态栏 / 标签页 / 图片（图标按钮也用这两个 API 换图） */
void cg_status_set(cg_widget_t *w, int part, const char *text);
void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...);
int  cg_tabs_count(const cg_widget_t *w);
void cg_tabs_select(cg_widget_t *w, int index);
int  cg_tabs_selected(const cg_widget_t *w);
void cg_widget_set_image(cg_widget_t *w, const char *path);
const char *cg_widget_image(const cg_widget_t *w);

/* 表格行级状态（多选 / 勾选列 / 行数值 / 行图标；不是 .cgui 字段，运行时设） */
void cg_table_set_checked(cg_widget_t *w, int row, int on);
int  cg_table_checked(const cg_widget_t *w, int row);
int  cg_table_set_row_value(cg_widget_t *w, int row, int value);
int  cg_table_row_value(const cg_widget_t *w, int row);
void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path);
int  cg_table_selected_count(const cg_widget_t *w);
int  cg_table_selected_row(const cg_widget_t *w, int index);

/* 悬停提示 / 时钟周期 */
void cg_widget_set_hint(cg_widget_t *w, const char *text);
void cg_widget_set_period(cg_widget_t *w, int period_ms);   /* 0 = 停 */
int  cg_widget_period(const cg_widget_t *w);

/* 画板：在回调里直接画，重绘自动回放 */
void cg_canvas_clear(cg_widget_t *w);
void cg_canvas_set_color(cg_widget_t *w, unsigned rgb);
void cg_canvas_set_line_width(cg_widget_t *w, int px);
void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2);
void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill);
void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text);

/* 菜单：点中某一项后问是哪一项（menu / menubutton 通用） */
const char *cg_menu_last_id(const cg_widget_t *w);
const char *cg_menu_last_text(const cg_widget_t *w);
int         cg_menu_count(const cg_widget_t *w);   /* 可点项个数，不含分隔线与下拉项 */

/* 日期时间 / 月历 / 富文本 / 工具栏 都不需要专门 API：
 * 值就是 cg_widget_text()，工具栏选中项就是 cg_widget_value() */

/* 窗口 */
void cg_set_title(cg_app_t *app, const char *title);
void cg_cancel_close(cg_app_t *app);   /* 只在窗口的 close 回调里有意义：这次不关了 */
void cg_win_set_pos(cg_app_t *app, int x, int y);        /* top-left，屏幕坐标 */
void cg_win_set_size(cg_app_t *app, int width, int height);
void cg_win_center(cg_app_t *app);
void cg_win_show(cg_app_t *app, int show);
void cg_win_set_state(cg_app_t *app, const char *cmd);   /* "min" / "max" / "normal" */
void cg_win_set_topmost(cg_app_t *app, int on);
void cg_win_set_icon(cg_app_t *app, const char *ico_path);   /* NULL 还原默认 */
int  cg_app_has_window(const cg_app_t *app);   /* --selftest 不开窗：弹框前先问一句 */

/* 托盘图标（app.tray = true 会自动建；这里可手动控制 / 弹气泡）。
 * ico_path 为 NULL 用窗口图标，tip 为 NULL 用窗口标题。 */
void cg_tray_set(cg_app_t *app, const char *ico_path, const char *tip);
void cg_tray_remove(cg_app_t *app);
void cg_tray_balloon(cg_app_t *app, const char *tip_title, const char *text);

/* 窗口事件的参数（鼠标坐标 / 功能键 / 键码 / 滚轮量 / 托盘动作） */
int cg_event_x(const cg_widget_t *self);
int cg_event_y(const cg_widget_t *self);
int cg_event_keys(const cg_widget_t *self);   /* 1=Ctrl 2=Shift 4=Alt（位或，对齐）*/
int cg_event_key(const cg_widget_t *self);    /* 键码（keydown/keyup，VK_*）*/
int cg_event_delta(const cg_widget_t *self);  /* 滚轮量，正数向上 */
int cg_tray_action(const cg_widget_t *self);  /* 1=单击 2=双击 3=右键 */

/* 程序配置（app.info）：key 见 1.2 节，取不到返回 NULL */
const char *cg_app_info(const cg_app_t *app, const char *key);

/* 动态扩展与绑定 */
cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent, const char *type, const char *id,
                           int x, int y, int w, int h, const char *text);
int  cg_on(cg_widget_t *w, cg_event_t ev, cg_event_fn fn);              /* 直接给函数指针 */
int  cg_on_named(cg_widget_t *w, cg_event_t ev, const char *cb_name);  /* 按 .cgui 里的名字 */

/* 业务数据挂载 / 定时器 / 退出 */
void  *cg_widget_get_user(const cg_widget_t *w);
void   cg_widget_set_user(cg_widget_t *w, void *user);
cg_timer_t *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user);
void   cg_kill_timer(cg_timer_t *t);
void   cg_quit(cg_app_t *app);
void   cg_set_title(cg_app_t *app, const char *title);
```

---

## 7. 错误处理约定

| 情况 | 运行时行为 |
| --- | --- |
| 文件读不到 | 若 `auto_locate_cgui` 打开，依次找：exe 旁边 → `exe目录\..\apps\<exe名>\<exe名>.cgui` → 同目录下原文件名；都找不到就把试过的路径全打印出来，返回码 2 |
| JSON 语法错 / 顶层不是对象 / `widgets` 不是数组 | 报 `解析 .cgui 失败: JSON: ... (line N)`，返回码 3 |
| `cgui` 版本大于 1 | 报版本号，返回码 3 |
| `type` 不认识 | 报类型名，返回码 3 |
| 非容器带了 `children` | 报控件 id 与类型，返回码 3 |
| `events` 里的名字没有对应 `CG_BIND` | **只警告不退出**（该事件不触发）；`--selftest` 下算失败，返回码 5 |
| `events` 里有不认识的键 | 忽略 |
| 控件 id 重复 | 不报错，但 `cg_find` 命中的是**后建**的那个（运行时按创建顺序倒序查找）；`cg_widget_add` 造重名会直接失败并报错 |
| `image` 的 `src` 找不到图片 | 只警告，控件显示成虚线占位框（把路径画在中间便于排查） |
| `cg_tree_add` / `cg_table_add_row` 等用在错的控件类型上 | 打印一行 `需要 xxx 控件，实际给的是 yyy`，返回 -1，不做任何修改 |
