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
| `styles` | 对象 | 否 | **命名样式表**：键 = 样式名，值 = 一份 `style` 对象。控件的 `style` 可以按名字引用它（也可用 `use` 再局部覆盖），见 2.2.1。**不写它 = 老 `.cgui` 行为完全不变** |

顶层就这四块：`app`（窗口属性）、`widgets`（控件树）、`styles`（命名样式，2.2.1）、
`actions`（动作，2.8.1）。后两块可选，不写就是老文件。

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
| `app.dark` | 布尔 / 字符串 | `false` | 深色模式：`true` 固定深色 / `"auto"` 跟随系统的「应用」主题 / 不写 = 浅色。只改**默认**配色（控件自己写了 `fg`/`bg` 的按它自己的来）；覆盖范围与边界见 1.1.1 |
| `app.enterTab` | 布尔 | `false` | 回车当 Tab 下移焦点（默认回车触发 `submit`） |
| `app.escClose` | 布尔 | `true` | Esc 关闭窗口；关掉后 Esc 会当成当前控件的 `submit` |
| `app.tray` | 布尔 | `false` | 建系统托盘图标（用窗口图标）；左键显示、右键弹菜单，并触发 `tray` 事件 |
| `app.icon` | 字符串 | 运行时自带 | 窗口图标 `.ico` 路径，解析顺序同 `src`（见 2.4） |
| `app.cursor` | 字符串 | `arrow` | 窗口鼠标指针：`arrow`/`hand`/`ibeam`/`wait`/`cross`/`sizeall`/`sizens`/`sizewe`/`sizenesw`/`sizenwse`/`no`/`help`/`uparrow` |
| `app.class` | 字符串 | `CStackGUIMain` | 窗口类名（注册 Win32 窗口类用的，一般不用改） |
| `app.note` | 字符串 | — | 备注，**只给画布看**，运行时忽略 |

### 1.1.1 `app.dark`：深色模式

```json
"app": { "dark": true }     /* 也可以写 "auto"：跟随系统的「应用」主题 */
```

| 写法 | 含义 |
| --- | --- |
| 不写 / `false` | **浅色**（缺省）—— 老 `.cgui` 的外观一个字都不变 |
| `true` | 固定深色 |
| `"auto"` | 跟随系统：读 HKCU `Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` 的 `AppsUseLightTheme`（`0` = 深色）；读不到那个键就按浅色算 |

> `AppsUseLightTheme` 就是「设置 → 个性化 → 颜色 → 选择模式」那一档（系统的**应用**模式）；
> 它旁边的 `SystemUsesLightTheme` 管的是任务栏 / 开始菜单，**不参与**这里的判断。

运行时开关（语义与 `app.dark` 完全一致）：

```c
int cg_theme_set_dark(cg_app_t *app, int on);   /* 运行时切换：1 = 深色，0 = 浅色；立即重画 */
int cg_theme_is_dark(const cg_app_t *app);      /* 现在是深色吗 */
```

**覆盖范围**（这是能力的边界，不是缺陷 —— 下表是 **2026-09-27 在 Win11 10.0.26100 上真机实测**的结果，
不是设计目标）：

✅ **跟随深色的 —— 本质是「默认值」跟着变**：

| ✅ 跟随 | 说明 |
| --- | --- |
| 窗口背景、`group` / `box` / `card` / `shape` 的底色 | 窗口与自绘面板的底色 |
| **没写 `fg` 的 `label`** 文字色 | 变浅、保持可读 |
| **没写 `fg` 的自绘文字**（复选框 / 单选钮 / 开关的标签）| 变浅、保持可读 |
| **没写 `fg` / `bg` 的 `edit`** | 深底浅字 |
| `terminal` | RichEdit 走 `EM_SETBKGNDCOLOR` |
| 分组框（`group`）里的 `label` 与 `edit` | 跟着分组底色，不是「糊一块灰」|
| **中性灰部件**：`progress` / `progressring` / `slider` 的**槽与轨道**、`led` 的灭态灯点、`chart` 的边框与网格、`chart` 的轴标签与图例 | 这些"浅灰细部件"跟着主题换成深色那套（收在 `cg_skin_neutral()` 一处）；品牌色填充（进度填充、滑块圆钮、圆环弧、灯的亮态）**不**跟随，见下面那条例外 |

> 这张表有一条例外，看图最直观：**画在自绘浅色填充上的文字不跟着变** —— 按钮的文字画在 M3 浅紫填充上，
> 深色下填充仍是浅紫，所以按钮文字**保持深色**（跟着变浅反而看不清）。「深色下按钮的字怎么没变白」
> 不是缺陷，就是这一条。

❌ **不跟随的 —— 实测保持系统浅色**：

| ❌ 不跟随 | 为什么 |
| --- | --- |
| `listbox` / `table` / `tree` | 本机 Win11 实测不跟随；代码里试过系统未公开的深色开关，**无效** ⇒ 保持系统浅色 |
| `tabs`、`combo` 的下拉列表、`monthcal`、`datepicker`、`statusbar`、`toolbar` | Windows 经典控件，主题由系统内部绘制，公开接口改不动 |
| `slider` 的**画布底**（不是槽）| 槽与轨道自绘、已跟随深色；但控件自己的**底**由原生 trackbar 画（主题灰），深色下仍是一圈浅框。试过在 `NM_CUSTOMDRAW` 的 PREPAINT 里填「背后色」、再补 `CDRF_SKIPDEFAULT`，**实测像素一个都没变** ⇒ 彻底解决要像 `progress` 那样换成自绘窗口类（连输入一起接管），未做 |

**这不是 bug，也不是「深色只做了一半」**：不跟随的控件分两类 —— 一类是 Windows 经典控件
（`tabs` / 下拉列表 / 月历 / 状态栏 / 工具栏 ……），长什么样由系统内部的主题绘制决定，公开接口里
没有一条能把它们切成深色；另一类是**品牌色填充**（按钮 / 勾选 / 开关的 M3 浅紫、进度条与圆环的
填充色、状态灯的亮色……）—— 那是皮肤配色本身的取值，不是主题没生效。
（中性灰部件曾经也算第二类，2026-09-27 已全部收口跟随深色，见上表 ✅。）
想把这两类都做成深色，等于**重画这批控件 + 另做一套深色皮肤**，是另一个量级的工程 ——
所以这一版的选择是：**改得动的都改，改不动的照实说**。

**混合观感（真实，但不算好看，先说清）**：`button` / `checkbox` / `switch` 这类**自绘填充**仍是 M3 浅紫，
深色下是「深底 + 浅紫控件」；其余自绘控件（`led` / `meter` / `badge` / `tag` / `rate` / `pagination` /
`colorpicker` / `iconbutton` / `menubutton` / `image` / `canvas` / `separator`）**底色跟着深、自己画的填充
不跟**（`canvas` 整块是深色、`separator` 的线可见、徽章与标签片仍是浅色系）—— 与按钮同一条口径；
控件**自己写了 `fg` / `bg`** 的一律**保持原样**（这正是上面「深色只改默认值」的直接结果）。
所以一份**为浅色写死配色**的 `.cgui` 在深色下会是混合观感 —— 想要整套一致的深色，
得自己把那批控件的 `fg` / `bg` 也换掉。

一句话总结：**深色覆盖窗口 / 面板底色与没写颜色的文字这类「默认值」；自绘控件的填充色与系统经典控件
仍是浅色 —— 它让界面暗下来，但不是一套完整的深色皮肤。**

行为细节：

- **深色只改默认值**：控件自己写了 `fg` / `bg`（无论内联 `style` 还是引用命名样式）就**以它为准**，
  深色不做覆盖 —— 所以「深色底 + 自己写的绿字终端」不会被搅坏；反过来，只写了 `bg` 没写 `fg` 的话，
  字色也不再替你补（要一致就两边都写全）。
- 用户改系统主题时（`WM_THEMECHANGED`）**重新应用一次**：`"auto"` 的工程跟着系统切换走，不用重启。
- 缺省 = 浅色，**不改变现有工程的外观**；`false` 与不写完全一致。
- **画布不做深色预览**（在 Web 侧把整套自绘皮肤再实现一遍不值当）：画布只提供 `app.dark` 开关，
  要看真实效果就跑起来看。

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
| `grow` | 整数 | 否 | 余量分配权重 `0..99`。**缺省 0 / 不写 = 不吃余量**（序列化时不写出 0）。与 `rect` 同级的 widget 字段，语义见 2.1.1。容错：小数**四舍五入**、`true` 当 1、数字串按数字读；算出来**小于 0 或大于 99 都归 0**，不报错、不中断加载 |
| `text` | 字符串 | 否 | 显示文字。**缺省 = 空文字** —— 运行时以 `.cgui` 为唯一真相，**不回填**类型默认值；第 4 节那列"默认文本"是**画布新建控件**时的初值（`cg_widget_add()` 也用它）|
| `style` | 对象 / 字符串 | 否 | 控件样式。对象 = 内联字段（见 2.2）；字符串 = 引用 `styles{}` 里的命名样式名；对象里带 `use` = 「命名 + 局部覆盖」。三种形态与合并规则见 2.2.1 |
| `events` | 对象 | 否 | 见第 3 节 |
| `children` | 数组 | 否 | 只有容器类型（`group` / `box` / `card` / `tabs`）能带；**非容器带 children 会被运行时判为错误** |
| `page` | 整数 | 否 | 属于父 `tabs` 的第几页（0 起）。缺省 0 |
| `src` | 字符串 | 否 | 图片路径：`image` 的内容图、`iconbutton` 的图标、`button` 的背景图，见 2.4 |
| `format` | 字符串 | 否 | `datepicker` 的细分格式：`date`（缺省）/ `time` / `datetime`；`shape` 的外形：`rect`（缺省）/ `roundrect` / `ellipse`；`splitter` 的方向：`col`（缺省，左右分栏）/ `row`（上下分行）；`terminal` 的显示模式：留空（文本）/ `hex`；`chart` 的图形：`line`（缺省）/ `bar`；`led` 的形状：`round`（缺省，圆）/ `square`（方）；`meter` 的样式：`arc`（缺省，270° 圆弧）/ `half`（半圆 180°）|
| `hint` | 字符串 | 否 | 鼠标悬停提示（tooltip），**所有控件通用**；不写 / 空 = 无提示 |
| `bind` | 对象 | 否 | 控件绑定：值/文字/勾选跟着另一个控件走，见 2.8 |
| `action` | 字符串 | 否 | 动作引用：点这个控件 = 触发同名动作（文字 / 启用 / 勾选都跟随动作），见 2.8.1。**只有支持 `click` 的类型能用** |
| `period` | 整数 | 否 | `timer` 的触发周期（毫秒）。**不写 = 0 = 不触发**（画布新建时钟时给 1000）；`led` 的闪烁周期（毫秒，仅在状态=闪时有意义；缺省 500，小于 100 兜回缺省）|
| `port` / `baud` / `bits` / `stop` / `parity` / `auto` | 字符串 / 整数 / 整数 / 整数 / 整数 / 布尔 | 否 | **只有 `serial`（串口组件）用**：端口名（`COM4`，不写 = 没配，运行时 `cg_widget_serial_open(w, port)` 指定）/ 波特率（缺省 9600）/ 数据位 5..8（缺省 8）/ 停止位 1..2（缺省 1）/ 校验 0=无 1=奇 2=偶（缺省 0）/ 自动启动（不写 = 等 `cg_widget_serial_open()` 开）。越界值一律回退缺省 |
| `multi` / `check` | 布尔 | 否 | `table`：`multi` = 允许整行多选，`check` = 显示行首复选框列（配合 `check` 事件）|
| `multiline` | 布尔 | 否 | `edit`：允许多行（`ES_MULTILINE` + Enter 变换行）。**不写 = 单行**；多行一般还要配竖向 `scroll` |
| `scroll` | 整数 | 否 | `edit`：滚动条 `1` 横向 / `2` 纵向 / `3` 双向（位 1 = 横、位 2 = 纵）。**不写 = 0 = 无** |
| `maxlen` | 整数 | 否 | `edit`：最多允许输入多少字符（`EM_SETLIMITTEXT`）。**不写 = 0 = 不限制**（保持系统默认，不是"限 0 个字符"）|
| `conv` | 整数 | 否 | `edit`：大小写转换 `1` 大写转小写 / `2` 小写转大写。**不写 = 0 = 不转换**。注意 Windows 的 `ES_UPPERCASE`/`ES_LOWERCASE` 在**写入文本时也会转**，所以初始 `text` 也会被转换（`text: "abc"` + `conv: 2` 显示成 `ABC`）|
| `input` | 整数 | 否 | `edit`：输入方式。`1` 只读（能选中、改不了）/ `2` 密码（显示成圆点）/ `3` 整数文本 / `4` 小数文本 / `5` 字节 / `6` 短整数 / `7` 整数 / `8` 长整数 / `9` 小数 / `10` 双精度小数 / `11` 日期时间。**不写 = 0 = 通常方式**。`3` 靠 `ES_NUMBER`；`4`~`11` 由运行时把关：**按键**只放行该类型可能出现的字符（数字、负号、小数点、日期分隔符，非法字符连 `char` 事件都不发），**失焦**时能解析就夹到类型范围（字节 0~255、短整数 ±32767、整数 ±21 亿）并重写文本，解析不了就清空（"没填"是合法状态，运行时不会弹窗）|
| `border` | 整数 | 否 | `edit`：边框样式 `1` 无边框 / `2` 凹入式 / `3` 凸出式 / `4` 浅凹入式 / `5` 镜框式 / `6` 单线边框式。**不写 = 0 = 本工程原本的 M3 圆角描边**（和上面六种并列，方便老文件不动） |
| `spin` | 整数 | 否 | `edit`：右侧上下调节器 `1` 自动（点一下按 ±1 改值并夹到 `min`/`max`）/ `2` 手动（只派发 `spin` 事件，值由业务自己改）。**不写 = 0 = 无调节器**；**写了它就是数值框**：`value` 是真相、显示文本由它派生（`text` 对它不生效），`cg_widget_value()`/`set_value()`/绑定都按值走 —— 这就是原来独立类型 `spinner` 的能力。多行输入框没有调节器 |
| `items` | 字符串数组 | 否 | `listbox` / `combo` / `tabs`（页签文字）/ `statusbar`（分段文字）/ `toolbar`（按钮文字）的内容 |
| `nodes` | 对象数组 | 否 | `tree` 的节点树（2.5）或 `menu`/`menubutton` 的菜单项（2.7）|
| `columns` / `rows` | 数组 | 否 | `table` 的列与行，见 2.6 |

### 2.1.1 `grow`：窗口拉伸时按权重吃余量

默认界面是**绝对坐标**：把窗口拉大，控件一动不动，多出来的地方空着。给控件加一个 `grow`，
就能让它在容器变大时"吃掉"多出来的空间 —— 这是**唯一一个和窗口拉伸有关的字段**，
但它**不是一个布局引擎**（绝对坐标仍然是主体）。

```json
{ "id": "term_log", "type": "terminal",
  "rect": { "x": 8, "y": 8, "w": 464, "h": 180 }, "grow": 1 }
```

语义（**沿一个轴看，按"带"分配**）：把该轴上的兄弟按位置排序，**位置区间互相重叠的属于同一条带**
（直观说就是"同一行 / 同一列"）；**带与带之间**才按权重分余量，**带内每个 `grow > 0` 的成员各自把这一带填满**
（同一条带里的成员不是二选一，也不会被彼此推开）；**排在这一条带之后（位置更靠后）的兄弟整体平移，
与之重叠的并排兄弟不动**。横向与竖向各自独立算一遍（先横后竖）。

- **权重**：`1..99`；带与带之间按"带内成员权重之和"分，只有一个带写了 `grow` 就全归它。
- **同一条带不是二选一**：同一行 / 同一列里**每个** `grow > 0` 的成员都各自吃一份、都把这一带填满；
  这一带里 `grow` 为 0 的成员尺寸不变、原地不动。
- **同容器里竞争**：参与竞争的是**同一个父容器**里的兄弟；面板（`group` / `box` / `card`）里的
  子控件各自成组，互不影响。
- **谁会被平移**：吃余量会让这一带变宽 / 变高；**排在这一条带之后（位置更靠后）的兄弟整体平移**
  （尺寸不变），**与之重叠的并排兄弟不动**。—— 这两条只管**平移**：平移不会把不同带的兄弟压到一起，
  但**带内就地变大不保证不重叠**（见下面的「一条注意」）。
- **两个轴用同一个权重**：只有一个数，横向与竖向都按它吃 —— 没有"只在某个轴吃"的开关（见下）。
- **每次从设计坐标重算**（不做增量累加）：来回拉伸不会漂移、不会累积舍入误差。
- **画布不做实时预览**：属性面板给一个 `0..99` 的数值输入 + 一句说明；拖窗口时画布里的控件不会当场变。

**两个具体例子**（比抽象描述好懂）：

- `[日志区 | 分隔条 | 发送区]`（竖向三段、位置互不重叠 = **三条带**）：只给日志区写 `grow`，
  窗口变高时**只有日志区变高**，分隔条与发送区被整体推下去。
- `[终端 | 工具栏]`（左右并排、竖向上重叠 = **同一条带**）：两块都写 `grow` 时，窗口变高会让
  **两块都变高**；只给终端写 `grow` 则只有终端变高，工具栏保持原高、也不会被推走。

**一条注意**：带内成员是"各自就地"吃、不会互相让位 —— 所以**同一条带**里把 `grow` 给坐标更小
（更靠左 / 靠上）的那块，它会朝正方向长出去、**盖住**坐标更大的同带成员。注意**同带不等于这两块自己重叠**：
该轴上只要有个全宽兄弟（比如横贯整行的状态栏），整行就被并成一条带，原本不重叠的两块也成了同带。
所以**同一条带里**只给**边界最靠外**的那块写 `grow` 最安全 —— 它的右 / 下边界已经是这一带的最外沿，
再往外长就落到窗口边缘，带里没有谁的起点在它长出去的那段里（"坐标最大"只是这条的一个特例：
像 `examples\serialterm` 的日志区，坐标不是最大、但右边界就是带尾，同样安全）。这条只在**同带**内成立：
独占一条带（该轴上与谁都不重叠）的控件随便设，见下面的规则 1；不重叠又不同带的兄弟只会被整体平移。

**两条日常规则**（绝大多数需求靠这两条就够）：

1. **想让它变高（竖向吃）**：把 `grow` 写在你**希望变高的那一块**上（经验上就是日志 / 内容区，**不是底部那根细条**）；
   同一行里若还有并排的兄弟也该跟着变高，给它们**都**写 `grow`（同一条带里各自填满）。
   因为一个权重**两轴同吃**，这块横向也会长 —— 所以优先选**横向边界已经是这一带最外沿**的那块
   （通常就是横贯整宽的内容区，横向没有右邻居）：它横向多吃的部分落在窗口边缘外，影响不到别的兄弟。
   **它不必是容器里的最后一块**：只要它在竖向上与别的兄弟不重叠（各自成带），它长大、后面的带整体下移就行 ——
   `examples\serialterm` / `examples\netterm` 的日志区都属于这种（在容器**中段**也安全）。
2. **左右分栏**：把 `grow` 写在该吃宽度的那一栏上；另一栏不写 —— 它**宽度不变**：不与前者重叠
   （属于后面的带）就被整体平移，重叠（同一条带）就原地不动。
   （左右并排的两栏在竖向上同属一条带 —— 两块都写 `grow` 时，窗口变高会让它们一起变高。）

**和分隔条（`splitter`）是同一套心智模型**：都是在同一个容器里"局部撑开一块、其余的让位"，
都以设计坐标为基准。区别只在触发者与分配方式：
`splitter` 是**用户拖动**，只改"紧临的前一块 + 同容器最后一块"（中间兄弟只平移，见 2.9 的例子）；
`grow` 是**窗口尺寸变化**时按"带"分给 `grow > 0` 的兄弟。学过一个，另一个不用重新学。

**v1 明确不做**（有意留的边界）：

- 不加 `sizePolicy` / `anchor` / 两遍尺寸协商 —— **不做完整布局引擎**；
- 不做"只在某个轴吃"的独立开关：**一个权重、两轴同吃**（想只吃一个方向就按上面两条规则放）；
- 不做 `WM_GETMINMAXINFO` 的最小尺寸联动；
- **画布不做实时预览**；
- 窗口缩到比设计尺寸还小时，吃余量的控件**尺寸可以被压到 0**（不设下限、不出负数）；
- 对 `menu` / `statusbar` / `toolbar` 没有特殊例外（它们的矩形本来就特殊，建议别给它们写 `grow`）。

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

### 2.2.1 `styles{}`：命名样式，与 `style` 的三种形态

同一套字号 / 颜色在几十个控件上各写一遍，改一次字号就得改几十处（`examples\netterm` 里
`"font": "Microsoft YaHei UI", "size": 9` 重复了十几遍）。顶层写一次、给个名字，控件按名字引用：

```json
"styles": {
  "title": { "font": "Microsoft YaHei UI", "size": 12, "bold": true, "fg": "#1D1B20" },
  "log":   { "size": 9,  "fg": "#00FF00", "bg": "#101010" }
}
```

- 键 = **样式名**（非空字符串）；值 = 一份 2.2 的 `style` 对象（字段完全相同：`font` / `size` /
  `bold` / `fg` / `bg` / `radius` / `align`）。
- **没写的字段不参与**合并 —— 命名样式是「只覆盖写了的字段」，不是「缺省值也算一份」。
- 重名：**后写的覆盖前写的**（与 `actions` 一致）。
- 容错：名字为空 / 值不是对象 / 字段值非法 → **跳过该条或该字段**，不报错、不中断加载
  （与 `actions` 同一个口径）。

控件的 `style` 支持**三种形态**（都要支持，且向后兼容）：

| `.cgui` 写法 | 含义 |
| --- | --- |
| `"style": { "size": 11 }` | **内联**：原来就这样，行为不变 |
| `"style": "log"` | **纯命名引用**：整个样式来自 `styles.log` |
| `"style": { "use": "log", "size": 11 }` | **命名 + 局部覆盖**：先取 `styles.log`，再用本对象里**写了的**字段覆盖 |

**合并规则：先命名样式、后内联字段（内联优先）。** 也就是「`use` 先铺底，本对象写的字段再往上盖」；
对象里**没写**的字段不覆盖命名样式 —— 「没写」与「写成默认值」是两件事：写 `"size": 0` 才是真的
把字号盖回系统默认。

容错（同样不报错、不中断加载）：

- 空名条目直接跳过（也没有引用它的办法）；
- `use` 指向**不存在的名字** → **当没有 `use`** 处理，只用本对象里写的字段；
- `"style": "log"` 但 `styles` 里没有 `log` → 等价于没写 `style`（全走缺省）；
- **值取不到的字段算「没写」**：`"fg": "乱码"`、`"font": 123`、`"size": "大一点"` 这类写法**不会**覆盖
  命名样式里对应的值 —— 理由很实际：**写错一个色值不该让样式里那个好值悄悄失效**。
  `font` / `size` / `bold` / `fg` / `bg` / `align` / `radius` **七个字段都是这个口径**：
  键存在但值解析不出来 = 这个字段没写，命名样式里的值照常生效。
- **没有「强制无色」的写法**：`fg` / `bg` 传 `CG_COLOR_NONE`（或任何取不到的值）都算没写 —— 想用透明 /
  系统默认，**不写这个字段**就是那个意思（`label` / `link` / `separator` 的透明也一样，见 2.2）。
  —— 所以不需要一个「写了但没值」的写法，也就没有"值取不到要按写了算"的必要。

**这不是 CSS，也别当 CSS 用**：没有选择器、没有继承、没有层叠、没有优先级计算 —— 只有
「名字引用 + 局部覆盖」这一层。另外 `styles{}` 是**加载期读一次**：运行时要改单个控件，
走 `cg_widget_set_colors` / `cg_widget_set_font` / `cg_widget_set_rect` 那条路，不是改样式表。

### 2.3 状态字段

既可以直接写在节点上，也可以放进 `state` 对象里（两者同时存在时顶层优先，方便手写小文件）。

| 字段 | 类型 | 默认 | 生效控件 |
| --- | --- | --- | --- |
| `visible` | 布尔 | `true` | 全部 |
| `enabled` | 布尔 | `true` | 全部 |
| `checked` | 布尔 | `false` | `checkbox` / `radio` |
| `value` | 整数 | `0`（**运行时解析缺省**；画布新建"有范围"的控件时给初值 40，见 `designer/src/model.ts`）| `slider` / `progress` / `listbox` / `combo` / `tabs`（选中项序号，按**项数**夹到 `0..项数-1`，**不按 `min`/`max`**；`cg_widget_set_value()` 也是这套语义） / `meter`（当前值，按 `min`/`max` 夹） / `led`（`0` 灭 / `1` 亮 / `2` 闪，**不按区间夹**） / **数值输入框**（`edit` + `spin`：值是真相、文本由它派生）|
| `min` / `max` | 整数 | `0` / `100` | `slider` / `progress` / **数值输入框**（`edit` + `spin`，箭头与手输都夹）/ `meter`（量程）/ `rate`（星数 = `max-min`，上限 10）/ `pagination`（页数 = `max-min+1`）。`max <= min` 时自动修正为 `min+1` |

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
| `action` | 动作名（见 2.8.1）：该项显示 `action.text`、点击触发该动作（**不再走菜单自己的 `click`**），启用/勾选跟随动作 |
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

菜单项在 `.cgui` 里是**声明式**的；运行时也可以改（`cg_menu_*` 一组），定位一律用项 id，顶层用 `NULL`：

| 调用 | 作用 |
| --- | --- |
| `cg_menu_add_item(w, parent_id, pos, text, id)` | 往某个下拉里加一项（`parent_id` = 那个下拉**自己的 id**）；传 `NULL` = 加在顶层。`pos = -1` 或越界 = 追加。`text = "-"` = 分隔线。返回新项下标 / 失败 `-1` |
| `cg_menu_remove_item(w, id)` | 删掉那一项**连同它的子树**；返回删掉的行数 / 失败 `-1` |
| `cg_menu_clear_items(w, parent_id)` | 清空某个下拉里的项（**下拉自身保留**）；`parent_id` 传 `NULL` = 清空整个菜单 |
| `cg_menu_set_item_text` / `_enabled` / `_checked` `(w, id, …)` | 改那一项的文字 / 可用 / 勾选 |

三条要知道的边界：

1. **往某个下拉里加项，那个下拉必须先在 `.cgui` 里有 id** —— 菜单项 DSL 写成 `文件 | menu_file` 即可
   （顶层加项用 `NULL`，不需要 id）；
2. 运行时新增的项**不支持引用动作**：要快捷键就在 `.cgui` 里写固定项，动态项用 `cg_menu_last_id()` 分发；
3. 改的是**内存里的菜单，不写回 `.cgui`**（重开界面回到文件里那份）—— `.cgui` 是设计期产物，
   运行时状态（当前端口、最近打开的文件表）本来也不该写进去。

对**动作项**调 `set_item_text / _enabled / _checked` **不生效**（那三项归动作管，用 `cg_action_set_*`），
只记一条 WARN。`menubutton` 的弹出菜单每次点击现场重建，所以上面这些改完**天然就是最新的**，不需要额外动作。

### 2.8 `bind`：控件绑定

让两个控件一直保持一致，省掉「自己写两个回调互相 set」的活。写**想被同步的那个控件**上：

```json
{
  "id": "lab_value",
  "type": "label",
  "rect": { "x": 24, "y": 120, "w": 120, "h": 24 },
  "bind": { "src": "sld_level" }
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `src` | 字符串 | **是** | 源控件的 `id`（同一份 `.cgui` 里的另一个控件）。空串 = 不绑定（画布就是这么存的）|
| `two` | 布尔 | 否 | `true` = 双向：本控件被用户改动时也推回源。缺省 `false`（单向：只有源变化时同步）|

**抄什么由「本控件（目标）的类型」决定**，不用额外配置：

| 目标的类型 | 同步内容 |
| --- | --- |
| `slider` `progress` `progressring` **数值输入框**（`edit` + `spin`）`rate` `pagination` `tabs` `listbox` `combo` `toolbar` | 抄 `value`。**列表类（`tabs`/`listbox`/`combo`/`toolbar`）的下标不按 `min`/`max` 夹**，跟用户点选是同一套语义；其余按目标的 `min`/`max` 夹 |
| `checkbox` `radio` `switch` | 抄勾选状态：源也是勾选型就抄 `checked`，否则取「源的值是否为 0」 |
| 其它能显示文字的（`label` `edit`（无调节器的普通输入框）`richtext` `statusbar` `button` `link` `badge` `tag` …） | 源是文本型（普通输入框/标签/链接/徽章/标签片/日期）就抄**文字**，否则把源的**数值**写成十进制 |

两条刻意的规矩，知道了才不会误判：

- **绑定的同步不派发目标的 `change` 回调**。镜像是「跟着变」，不是用户操作 —— 否则一个控件动一下会连带触发一串回调，
  顺序谁也说不清。也因此不存在回调互推的死循环（值一样就不写）。想在同步发生时做事，把 `change` 绑在**源**控件上。
- 源控件的 `id` **改了要一起改**：`src` 是名字，运行时按 `cg_find()` 查；找不到源时启动会写一条警告日志，
  `--selftest` 会把它算进失败项（返回 5）。画布上改 id 时会自动把指向旧 id 的 `bind.src` 一起改掉。
- 链式绑定（`A -> B -> C`）有效，会一路传下去；双向绕成环（`A <-> B <-> C <-> A`）靠「值相等就停」和最大深度兜底，不会挂死。

运行时也能绑（覆盖 `.cgui` 里写的）：

```c
/* 滑块拖动时旁边的标签显示数字；第三个参数 1 = 双向 */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "lab_value"), 0);
```

### 2.8.1 `actions` / `action`：动作（命名命令 + 快捷键）

同一件事往往有好几个入口：菜单里一项、工具区一个按钮、再加一个 `Ctrl+S`。逐处绑回调的写法会让
"同一个命令"散成 N 份回调、启用状态也要改 N 处，迟早不同步。**动作（action）**把它收成一条命名命令
—— 一条命令 = 文字 + 快捷键 + 一个回调名；菜单项和控件都只**引用**它：

```json
"actions": {
  "save": { "text": "保存(&S)", "accel": "Ctrl+S", "handler": "on_save" },
  "grid": { "text": "显示网格", "handler": "on_grid_toggle", "checked": true }
}
```

`actions` 是可选的**顶层字段**（与 `app` / `widgets` 同级）。**不写它 = 没有动作，老 `.cgui` 行为完全不变。**

| 键 | 类型 | 缺省 | 说明 |
| --- | --- | --- | --- |
| （对象键）| 字符串 | — | 动作名。**必须非空**（空名条目被直接跳过，不报错 —— 也没有引用它的办法）；重名不报错，**后者覆盖** |
| `text` | 字符串 | `""` | 引用它的菜单项 / 控件显示的文字 |
| `accel` | 字符串 | `""` | 快捷键，语法见下。空 = 没有快捷键 |
| `handler` | 字符串 | `""` | C 侧 `CG_BIND` 的名字。**空 → 该动作被忽略**（启动警告，`--selftest` 报错）|
| `checked` | 布尔 | `false` | 初始勾选状态（同步给引用它的菜单项）|
| `enabled` | 布尔 | `true` | 初始启用状态 |

未知键忽略（与其它字段一致）。**两个引用点**：

- **菜单项** —— `menu` / `menubutton` 的 `nodes[]` 里任一项加 `"action": "save"`：该项文字 =
  `action.text` 非空时用 `action.text`、否则用该项自己的 `text`；点击触发该动作，**不再给菜单控件派发 `click`**
  （所以**这一项**上 `cg_menu_last_id()` 没意义）；启用/勾选跟随动作；动作项**不写 `id`**。
  同一根菜单里**动作项与普通项可以混用**：普通项照旧走菜单控件的 `events.click`（`examples\notepad` 就是
  "文件"菜单四项走动作、编辑/帮助仍走 `on_menu_click` 分发）。引用了**不存在的动作名** → 启动警告 + 自检报错。
- **控件** —— 节点上加 `"action": "save"`：**只有支持 `click` 的类型能用**（真相是 `cg_doc_kind.c` 里那份
  `k_kinds[].events`），不支持 click 的类型写了 `action` → 启动警告 + 自检报错。文字上 `action.text` 非空就
  **盖掉控件自己的 `text`**（`.cgui` 里写的仍在，运行时被盖住）；点击触发该动作，不再走自己的 `click` / `dblclick`
  （因此**同时绑了 `events.click` 也是警告 + 自检报错** —— 那份 click 永远不会被调用）；启用跟随动作。
  和绑定一样**引用是名字**：动作改名不会自动更新引用，引用处会变成"不存在的动作"，自检会报出来。

**快捷键语法（`accel`）**：

```
[Ctrl+][Shift+][Alt+]键
```

修饰键大小写不敏感、可任意顺序、重复算一次。键的取值：`A`–`Z`、`0`–`9`、`F1`–`F24`、`Esc`、`Enter`、
`Tab`、`Space`、`Back`（退格）、`Delete`、`Insert`、`Home`、`End`、`PageUp`、`PageDown`、`Left`、`Right`、`Up`、`Down`。

- **不接受无修饰键的单个字母或数字**（裸 `S` 会让用户没法往输入框里打 s）—— 解析失败；裸功能键（`F5`）可以。
- 解析不了的 `accel`（未知键、或与别的动作重复声明同一个快捷键）会被**忽略并写警告日志**，`--selftest` 报错。
- 规范化显示时修饰键固定按 `Ctrl+Shift+Alt` 顺序（如 `Ctrl+Shift+S`）；菜单项文字右侧会自动拼上
  `\tCtrl+Shift+S`（由系统右对齐显示）。

**快捷键为什么能"在输入框里也生效"**：它由**消息循环里的加速键表**处理（`HACCEL` + `TranslateAcceleratorW`），
在消息进到窗口之前就截住。窗口级 `keydown` 做不到这点 —— 焦点在输入框里时按键被 EDIT 吃掉、不冒泡，窗口根本
收不到；所以写 `app.events.keydown` 里判 `Ctrl+S` 是没用的，写成动作才有用。

**回调语义**：最终都走 `cg_action_invoke()`，签名与普通事件回调完全一样
（`void cb(cg_widget_t *self, cg_event_t ev, void *user)`），但有两个固定值：

- `self` 恒为**主窗口伪控件**（`cg_widget_id(self)` 是 `"__window"`）—— 一个动作可能有好几个入口，
  "从哪个入口点的"不是业务该依赖的信息；要知道是哪个动作被激活，用 `cg_action_name(self)`（一个函数挂多个动作时用）。
- `ev` 恒为 `CG_EVENT_CLICK`（与"菜单项点击 = click"同一套词汇，**不新增事件枚举**）。

**启用 / 勾选是动作的**：`cg_action_set_enabled()` 会把所有引用它的菜单项与控件一起变灰 / 恢复，
`cg_action_set_checked()` 会把引用它的菜单项打勾 / 去掉。反过来，你手动 `cg_widget_set_enabled(w, 0)`
禁用某个按钮**不会**影响动作，还会在下一次动作同步时被覆盖回去 —— 要禁就禁动作。

运行时的动作 API：

```c
int  cg_action_invoke(cg_app_t *app, const char *name);        /* 1 = 真的调了回调；0 = 无此动作 / 被禁用 / handler 没登记 */
void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled);   /* 同步到所有引用点 */
int  cg_action_is_enabled(const cg_app_t *app, const char *name);
void cg_action_set_checked(cg_app_t *app, const char *name, int checked);   /* 同步到引用它的菜单项 */
int  cg_action_is_checked(const cg_app_t *app, const char *name);
const char *cg_action_name(const cg_widget_t *self);           /* 在动作回调里问"是哪个动作"；没有则 NULL */
int  cg_action_count(const cg_app_t *app);                     /* 动作个数（遍历 / 自检用）*/
const char *cg_action_at(const cg_app_t *app, int i);          /* 第 i 个动作名，越界返回 NULL */
const char *cg_action_accel(const cg_app_t *app, const char *name);  /* 规范化后的快捷键（如 "Ctrl+S"）；没有返回 "" */
```

**v1 明确不做**（有意留的边界）：工具栏（`toolbar`）的按钮**不支持**引用动作（它的条目模型是纯文字 `items`，
与菜单的 `nodes` 不同，要支持得先换模型）；**没有运行时增删菜单项的 API**；动作的 `text` 不能在运行时改。

### 2.9 `terminal`：终端 / 日志视图

只读的按行视图，通信 / 调试工具最需要的那块。业务只管把字节丢进来：

```c
cg_term_write(cg_find(app, "term_log"), CG_TERM_RX, buf, n);             /* 收到 */
cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, "AT+GMR\r\n", -1);   /* 发出（-1 = 按字符串取长度）*/
```

几个字段在这个控件上有专门含义：

| 字段 | 含义 |
| --- | --- |
| `format` | 显示模式：留空 = 文本（ASCII / UTF-8）；`"hex"` = 十六进制。**只影响之后写入的行** |
| `checked` | 每行前面要不要 `[时:分:秒]`（画布上显示成「时间戳」）|
| `value` | 最多保留多少行，超了从最老的丢；`0` / 不写 = 默认 500 |
| `items` | **初始内容**（可选）；运行时写入的行接在后面 |

行为约定（这些是踩过的坑，写清楚省得猜）：

- **一次调用 = 一段数据**：里面带 `\r\n` 会**切成多行**（串口一次收 `"OK\r\nERR"` 是常态），`\r` 丢掉；
  一次调用只刷新一次界面，所以可以放心在收数据的循环里逐段调用。
- 文本模式只把控制字符（`< 0x20` 与 `0x7F`）换成 `.`，**0x80 以上原样保留** —— 所以中文不会被搅坏。
- 行按方向着色：`CG_TERM_RX`（绿）/ `CG_TERM_TX`（蓝）/ `CG_TERM_SYS`（灰），行首分别是 `RX ` / `TX ` / `-- `。
- 内容可以**选中复制**（日志最常被做的事就是复制出去），也能 `cg_term_save(w, path)` 存成
  UTF-8 + CRLF 的文本文件。
- 其余 API：`cg_term_clear` / `cg_term_set_hex` / `cg_term_hex` / `cg_term_set_timestamp` /
  `cg_term_set_max_lines` / `cg_term_count` / `cg_term_save`。

日志区 + 分隔条这种"上日志下操作"的经典布局：

```json
{ "widgets": [
  { "id": "term_log", "type": "terminal", "rect": { "x": 8, "y": 8, "w": 464, "h": 180 },
    "checked": true, "value": 500 },
  { "id": "sp_main", "type": "splitter", "format": "row",
    "rect": { "x": 8, "y": 192, "w": 464, "h": 6 }, "events": { "release": "on_split" } },
  { "id": "term_send", "type": "terminal", "rect": { "x": 8, "y": 202, "w": 464, "h": 110 } }
] }
```

**拖动时的真实行为**（`cg_splitter_apply()`）：只改**两块**的尺寸 —— 分隔条**紧挨着的前一块**、
以及**同一容器里的最后一块**（吃余量）；夹在中间的其他兄弟**只被平移**（`set_pos`）。于是：

- **一个 pane 只放一个控件**。一个 pane 里想放多个（图表 + 按钮 + 终端），先用
  `group` / `card` / `box` 包成一个容器，再拿容器当 pane；
- 两端之外不要再留兄弟 —— 否则一拖就被挤出窗口、被边缘裁掉（表现为"某个按钮只剩半个字"）；
- 正确样例见 `examples/serialterm/serialterm.cgui`（分隔条后面接的是**一个** `group` 容器）；
- 同一个窗口里 `statusbar` 也是参与布局的兄弟，**把它写在 `widgets` 数组前面**，
  别让它成为"最后一块"（否则拖分隔条会去改状态栏的宽度）。

### 2.10 `chart`：折线 / 柱状图

实时曲线：业务按采样节奏推点，窗口、范围、网格与绘制都由控件管。

```c
cg_chart_push(cht, 0, 42);            /* 给第 0 个系列推一个点 */
cg_chart_push_all(cht, vals, 2);      /* 一次给前 2 个系列各推一个点（多通道采样最常用）*/
```

| 字段 | 在本控件上的含义 |
| --- | --- |
| `items` | **系列名**，一行一个（每个条目 = 一条曲线）；不写就是单系列 |
| `value` | **窗口点数**：每系列保留多少个点（缺省 120，最多 4096）。它不是"当前值" |
| `min` / `max` | **Y 轴范围**（缺省 0~100）；超出范围的值按边界裁剪后画，不会画到框外 |
| `format` | `"line"`（缺省，折线）/ `"bar"`（柱状）|

行为约定：

- **窗口满了丢最老的**（不是环形缓冲）：内存里整体左移一格，所以
  `cg_chart_point(w, 系列, 0)` 永远是最老的点、最后一个下标永远是最新的。
- 点数超过绘图区像素宽度时**不抽点**，而是对每个像素列取该列覆盖点的 min/max 画一条竖线
  （LVGL 的做法）—— 既不慢，也不会刚好把那个尖峰抽掉。抽点最坑的就是丢毛刺。
- 颜色按固定调色板自动分配（8 色），按系列顺序取；写了 `items` 时名字会显示在右上角图例里。
- 改了 `value` / `items` 之后**点缓冲会重建、已有点会丢**（旧窗口配不上新窗口），这是有意的。
- 实时性靠"业务用 `cg_set_timer` 定周期推点"，**不要另开线程碰控件**（控件不是线程安全的）。
- 其余 API：`cg_chart_clear` / `cg_chart_add_series` / `cg_chart_series_count` /
  `cg_chart_filled` / `cg_chart_point` / `cg_chart_set_range`。

```json
{ "id": "cht_env", "type": "chart", "rect": { "x": 8, "y": 8, "w": 480, "h": 180 },
  "value": 200, "min": 0, "max": 100, "items": ["温度", "湿度"] }
```

### 2.11 `led`：状态灯

一个"看一眼就知道通没通"的小灯。业务只给它一个状态，闪烁不用自己管：

```c
cg_led_set(cg_find(app, "led_net"), 2);                /* 0 灭 / 1 亮 / 2 闪 */
cg_led_set_color(cg_find(app, "led_net"), 0x1B6E33);   /* 换亮起的颜色，缺省绿 */
int st = cg_led_state(cg_find(app, "led_net"));        /* 读回状态 */
```

几个字段在这个控件上有专门含义：

| 字段 | 在本控件上的含义 |
| --- | --- |
| `value` | 状态：`0` 灭 / `1` 亮 / `2` 闪。**越界会被夹到 0..2** |
| `format` | 形状：`"round"`（缺省，圆）/ `"square"`（方）|
| `period` | 闪烁周期（毫秒），只在状态 = 闪时有意义；**缺省 500，小于 100 按 500 算**（不是夹到 100：再快也看不出在闪，只会看着像暗的）|
| `text` | 灯右边的说明文字（可空；为空时灯占满整个矩形）|
| `style.fg` | 亮起的颜色，缺省绿 `#1B6E33` |

行为约定：

- **闪烁不用你开时钟**：设成 `2` 之后，控件自己按 `period` 的前后半个周期各亮 / 灭一次；
  业务只要 `cg_led_set(w, 2)` 这一件事。
- `period` 小于 100 会被**兜回缺省 500** —— 再快人眼已经不觉得在闪，只会像"暗的"，所以直接兜底。
- 它是**纯显示**控件：没有任何事件，点它没有反应。

```json
{ "id": "led_net", "type": "led", "rect": { "x": 24, "y": 24, "w": 96, "h": 20 },
  "value": 1, "format": "round", "text": "网络", "style": { "fg": "#1B6E33" } }
```

### 2.12 `meter`：仪表盘

一个带刻度与指针的表，业务给它一个值：

```c
cg_meter_set(cg_find(app, "mtr_cpu"), 63);             /* 给一个值（按 min/max 夹）*/
cg_meter_set_range(cg_find(app, "mtr_cpu"), 0, 100);   /* 改量程 */
```

| 字段 | 在本控件上的含义 |
| --- | --- |
| `value` | 当前值，被 `min`/`max` 夹（和滑块同一套语义）|
| `min` / `max` | 量程（缺省 0~100）|
| `format` | 样式：`"arc"`（缺省，270° 圆弧）/ `"half"`（半圆 180°）|
| `text` | 单位文字，画在中心大数值下面（可空）|
| `style.fg` | 圆弧 / 指针主色，缺省 `#6750A4` |

行为约定：

- 表由**底弧（灰）+ 值弧（彩色，随值增长）+ 刻度线 + 指针 + 中心大数值 + 下面的单位**一起画。
- 角度：`arc` 从 135° 顺时针扫 270° 到 45°；`half` 从 180° 扫 180°（左端到右端）。
- 值超量程**按边界夹取**后画，不会跑出弧外；`cg_meter_set_range` 遇到 `max <= min` 会纠正成
  `min+1`（和 2.3 的通用规矩一致）；万一绕过它造出非法量程，指针停在起点角，**不会除零**。
- 纯显示控件，没有事件；值只能被程序写（`cg_meter_set`）。

```json
{ "id": "mtr_cpu", "type": "meter", "rect": { "x": 24, "y": 24, "w": 140, "h": 140 },
  "value": 63, "min": 0, "max": 100, "format": "arc", "text": "%" }
```

---

## 3. `events`：事件绑定

```json
"events": { "click": "on_ok_click", "dblclick": "on_ok_dblclick" }
```

- 键是事件名，值是 **C 函数名（字符串）**。
- 允许的键（窗口级事件写在 `app.events` 里，见第 4 节）：

| 事件 | 什么时候发 | 谁支持 |
| --- | --- | --- |
| `click` | 点了一下 | 按钮/图标按钮/链接/输入框（`edit`）/图片/画板/标签片/菜单·菜单按钮·工具栏 |
| `dblclick` | 系统双击间隔内点了第二下 | 上面那批能点的（按钮/图标按钮/链接/输入框/图片/画板）+ 列表/树/表格 |
| `change` | 文字/勾选/数值/颜色变了 | 输入类、开关复选、滑块（拖动中）、颜色选择、评分、分页 |
| `submit` | 单行输入框里按了回车 | 只有单行输入 |
| `select` | 选中项变了 | 列表/下拉/标签页/树/表格/月历 |
| `release` | 滑块/分隔条松手（拖完才发一次） | `slider`、`splitter` |
| `expand` / `collapse` | 树节点展开/折叠 | 只有树 |
| `tick` | 时钟控件的周期到了（`period` 毫秒一次） | 只有时钟 |
| `arrive` | 串口组件收到数据（缓冲有数据就在主线程派发；回调里 `cg_widget_serial()` 取句柄再 `cg_serial_read()`，数据落到哪个控件由这行代码决定） | 只有串口组件 |
| `check` | 表格勾选列的行勾选状态变了 | 只有表格（`.cgui` 里开 `check`） |
| `gotfocus` / `lostfocus` | 控件获得 / 失去键盘焦点 | 有焦点概念的原生控件（输入类、按钮、开关勾选、滑块、列表/下拉/树/表格/标签页）|
| `mousedown` / `mouseup` | 按下 / 松开左键 | 窗口（客户区）+ `button` / `edit`（控件内）|
| `rdown` / `rup` | 按下 / 松开右键 | 同上 |
| `mousemove` | 鼠标移动（触发频繁，别做重活） | 同上 |
| `keydown` / `keyup` / `char` / `wheel` | 按键 / 放键 / 字符输入 / 滚轮 | 窗口（全窗口）+ `button` / `edit`（控件持有焦点时）|
| `spin` | 输入框右侧的上下调节器被点/被拖 | 只有 `edit`（`.cgui` 里开了 `spin`）。`cg_event_delta` 给 +1 / -1；`1` 自动方式下文本已经被改好并夹到区间 |
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
| `button` | 按钮 | 否 | 120×40 | `按钮` | `click`、`dblclick`、`gotfocus`、`lostfocus`、鼠标全套（`mousedown`/`mouseup`/`rdown`/`rup`/`mousemove`）、键盘全套（`keydown`/`keyup`/`char`/`wheel`）| `BUTTON`+`BS_OWNERDRAW`（M3 自绘；`src` 指定背景图）|
| `iconbutton` | 图标按钮 | 否 | 48×48 | — | `click`、`dblclick` | `BUTTON` 自绘（`src` 指定图片）|
| `label` | 标签 | 否 | 160×24 | `标签` | **无**（纯显示） | `STATIC` |
| `link` | 链接 | 否 | 160×24 | `链接` | `click`、`dblclick` | `STATIC`+`SS_NOTIFY`（手型光标、蓝色下划线）|
| `switch` | 开关 | 否 | 56×32 | — | `change`、`gotfocus`、`lostfocus` | `BUTTON` 自绘（M3 胶囊）|
| `edit` | 输入框 | 否 | 200×32 | — | `click`（EDIT 自己不发点击通知，由运行时在左键抬起时补派发）、`change`、`submit`、`spin`（调节钮被按下）、`gotfocus`、`lostfocus`、鼠标全套（`mousedown`/`mouseup`/`dblclick`/`rdown`/`rup`/`mousemove`）、键盘全套（`keydown`/`keyup`/`char`/`wheel`）| `EDIT`（圆角+垂直居中自绘；多行 / 滚动条 / 最大长度 / 大小写转换 / 边框 / 输入方式 / 调节器由 `multiline`/`scroll`/`maxlen`/`conv`/`border`/`input`/`spin` 控制，「对齐方式」复用 `style.align` → `ES_LEFT`/`ES_CENTER`/`ES_RIGHT`）。**多行 = 原来的 `multiedit`，带调节器 = 原来的 `spinner`**（两个类型 2026-09-22 合并进来，老 `.cgui` 自动升级）|
| `richtext` | 富文本 | 否 | 320×160 | — | `change`、`gotfocus`、`lostfocus` | `RICHEDIT50W`（缺库时降级 EDIT）|
| `datepicker` | 日期时间 | 否 | 140×28 | — | `change`、`gotfocus`、`lostfocus` | `SysDateTimePick32`（datetime 用两个 HWND）|
| `monthcal` | 月历 | 否 | 230×180 | — | `select`（**没有双击事件**：月历**从不发 `NM_DBLCLK`**，`MCN_SELECT` 已经把"点了哪一天"报全 —— 2026-09-23 真机实测后撤掉） | `SysMonthCal32`（选中日期写回 `text`）|
| `checkbox` | 复选框 | 否 | 160×28 | `复选` | `change`、`gotfocus`、`lostfocus` | `BUTTON`+`BS_OWNERDRAW`（自绘）|
| `radio` | 单选钮 | 否 | 160×28 | `单选` | `change`、`gotfocus`、`lostfocus` | `BUTTON`+`BS_OWNERDRAW`（自绘，互斥靠兄弟遍历）|
| `listbox` | 列表框 | 否 | 220×140 | — | `select`、`dblclick`、`gotfocus`、`lostfocus` | `LISTBOX` |
| `combo` | 下拉框 | 否 | 200×200 | — | `select`、`gotfocus`、`lostfocus` | `COMBOBOX` |
| `slider` | 滑块 | 否 | 200×32 | — | `change`、`release`、`gotfocus`、`lostfocus` | `msctls_trackbar32`+`NM_CUSTOMDRAW`（自绘轨道/滑块）|
| `progress` | 进度条 | 否 | 200×20 | — | **无**（只被写） | 自绘窗口 `CG_PROGRESS_CLASS`（原生不支持 custom draw）|
| `progressring` | 圆环进度 | 否 | 120×120 | — | **无**（只被写） | 自绘窗口（GDI+ 轨道 + 圆头弧）|
| `tabs` | 标签页 | **是** | 320×220 | — | `select`、`gotfocus`、`lostfocus` | `SysTabControl32`；子控件按 `page` 显隐 |
| `tree` | 树 | 否 | 220×180 | — | `select`、`dblclick`、`expand`、`collapse`、`gotfocus`、`lostfocus` | `SysTreeView32` |
| `table` | 表格 | 否 | 360×180 | — | `select`、`dblclick`、`check`、`gotfocus`、`lostfocus` | `SysListView32` 报表模式（`check`/`multi` 开勾选列与多选）|
| `statusbar` | 状态栏 | 否 | 400×24 | — | **无** | `msctls_statusbar32` |
| `terminal` | 终端 | 否 | 360×200 | — | **无** | 只读 RichEdit：按行着色（RX 绿 / TX 蓝 / 自己记的灰）、可选中复制，见 2.9 |
| `chart` | 图表 | 否 | 320×160 | — | **无** | 自绘折线 / 柱状图：滑动窗口 + 超像素密度时按列画 min/max，见 2.10 |
| `led` | 状态灯 | 否 | 20×20 | — | **无**（纯显示） | 自绘窗口类 `CStackGUILed`：圆 / 方指示点，灭 / 亮 / 闪，见 2.11 |
| `meter` | 仪表盘 | 否 | 140×140 | — | **无**（纯显示） | 自绘窗口类 `CStackGUIMeter`：底弧 + 值弧 + 刻度 + 指针 + 中心值，见 2.12 |
| `menu` | 菜单栏 | 否 | 无矩形 | — | `click` | 窗口 `HMENU`（一根窗口一根）|
| `menubutton` | 菜单按钮 | 否 | 130×32 | `菜单` | `click` | `BUTTON` + `TrackPopupMenu` |
| `toolbar` | 工具栏 | 否 | 400×32 | — | `click` | `ToolbarWindow32`（纯文字按钮）|
| `group` | 分组框 | **是** | 260×180 | `分组` | **无** | 自注册面板窗口（边框+标题完全自绘）|
| `box` | 容器块 | **是** | 200×120 | — | **无** | 自注册面板窗口 |
| `card` | 卡片 | **是** | 280×180 | `卡片` | **无** | 自注册面板窗口 |
| `splitter` | 分隔条 | 否 | 6×200 | — | `release` | 自绘窗口：能拖动的分区边界（**只改两块**：紧邻的前一块 + 同容器最后一块吃余量，中间兄弟只平移 → 一个 pane 只放一个控件），见下面 `terminal` 那节的例子 |
| `separator` | 分割线 | 否 | 200×2 | — | **无** | `STATIC` + `SS_ETCHEDHORZ` |
| `image` | 图片 | 否 | 200×120 | — | `click`、`dblclick` | 自绘窗口（GDI+ 读图）|
| `shape` | 外形框 | 否 | 120×120 | — | **无** | 自注册面板窗口（自绘外形）|
| `timer` | 时钟 | 否 | 无矩形 | — | `tick` | **无窗口**，后端 50ms 心跳驱动（`period` 毫秒）|
| `serial` | 串口 | 否 | 无矩形 | — | `arrive` | **无窗口**（像易语言的串口组件）：只带参数（`port`/`baud`/`bits`/`stop`/`parity`/`auto`），收到数据在主线程派发 `arrive`，**数据落到哪个控件由用户代码决定**（terminal / chart / listbox 都行） |
| `canvas` | 画板 | 否 | 240×160 | — | `click`、`dblclick` | 自绘窗口（`cg_canvas_*` 画线/矩形/椭圆/文字）|
| `colorpicker` | 颜色选择 | 否 | 64×28 | — | `change` | `BUTTON` 自绘色块（点击弹系统取色器，值存 `value`）|
| `browser` | 网页浏览 | 否 | 320×240 | — | **无** | `AtlAxWin` 宿主系统 WebBrowser（起不来降级 `STATIC` 占位；实验性）|
| `badge` | 徽章 | 否 | 48×24 | `99+` | **无** | `BUTTON` 自绘小圆片 |
| `tag` | 标签片 | 否 | 72×28 | `标签` | `click` | `BUTTON` 自绘 |
| `rate` | 评分 | 否 | 140×32 | — | `change` | `BUTTON` 自绘五角星（星数 = `max-min`，上限 10）|
| `pagination` | 分页 | 否 | 180×32 | — | `change` | `BUTTON` 自绘（页数 = `max-min+1`）|

> 上表的事件列与 `k_kinds[].events` 位掩码一一对应（`node tools\check-examples.mjs --docs` 会逐行回校）。
> 鼠标 / 键盘那种"一整类"的写在行内括号里；某类控件不该有的事件，运行时与自检都会明确报出来。

**窗口本身也是一个"控件"**（伪控件 id 固定为 `__window`，头文件里是 `CG_WINDOW_ID`）。
它的事件写在 `app.events` 里，和控件事件共用同一套 `CG_BIND` 机制：

```json
"app": { "title": "...", "events": { "ready": "on_ready", "close": "on_close", "tray": "on_tray" } }
```

> **注意 `ready` 的时机**（2026-09-27 实测）：`app.events.ready` 比「窗口第一次收到 `WM_SIZE`」**更早** ——
> 在它里面调 `cg_win_set_size()` / `cg_win_set_pos()` **不生效**（随后创建流程会把尺寸定回去）。
> 要在启动时恢复上次的窗口尺寸，用 `cg_app_desc_t.on_ready` **钩子**（`desc.on_ready = 函数`）：
> 它在第一次 `WM_SIZE` **之后**才触发，在那里改窗口尺寸是生效的（`examples\serialterm` 用的就是这个钩子）。
> 两种写法都跑过：同一份探针里先打出 `ready`、再打出第一次 `size`，钩子的输出夹在它们之后 —— 顺序就是证据。

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
| `mousedown`/`mouseup`/`rdown`/`rup`/`mousemove` | 在窗口客户区按/放左键、按/放右键、移动鼠标 |
| `dblclick` | 在窗口客户区双击 |
| `drop` | 文件被拖进窗口（从资源管理器拖过来）。回调里用 `cg_drop_count(app)` / `cg_drop_file(app, i)` 取路径 —— **路径只在本次回调期间有效**，要留着就在回调里拷走 |
| `gotfocus` / `lostfocus` | 窗口获得 / 失去键盘焦点 |
| `keydown` / `keyup` | 按下 / 放开按键（`cg_event_key()` 键码、`cg_event_keys()` 功能键）|
| `char` | 输入了可打印字符（中文输入法也走这里）|
| `wheel` | 滚轮滚动（`cg_event_delta()` 滚动量，正数向上）|

鼠标/键盘/滚轮事件的参数因为回调签名固定（只有 `self`/`ev`/`user`），存在**派发目标**上
（窗口事件存在窗口对象上；`button` / `edit` 的控件级鼠标/键盘事件存在该控件上），
用 `cg_event_x/y/keys/key/delta` 和 `cg_tray_action` 读（`self` 传回调收到的那个控件即可）。

`close` 回调里可以调 `cg_cancel_close(app)` 取消这次关闭（比如还有没保存的内容）。

**事件是按控件特性分配的**，不是每个控件都有全套：

- 能点的才有 `click` / `dblclick`；能改内容的才有 `change`
- 有"选择"概念的才有 `select`；只有树有 `expand` / `collapse`
- 只有 `slider` 与 `splitter` 有 `release`（滑块拖动中发 `change`、松手才发 `release`；分隔条拖完发一次）
- 控件级的鼠标/键盘/滚轮事件只有 `button` 与 `edit`（坐标相对控件客户区；窗口级那套坐标相对窗口客户区）
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
- `button` 的 `src` 是**背景图**：拉伸铺满整个按钮（圆角裁切），文字画在图上；
  悬停/按下/禁用在图上叠一层半透明蒙版做反馈。没图就照常画纯色按钮。
- `timer` 不写 `period` 就等于关着（C 侧缺省 0）；画布新建时钟会给 1000，运行时用 `cg_widget_set_period()` 改。
- `serial`（串口组件）是**无界面控件**：运行时不建窗口、不占矩形；`auto: true` 时窗口建好就自动开链路
  （失败只记日志不拦启动）。链路开着时运行时每 50ms 看一眼缓冲，有数据就在主线程派发 `arrive`；
  回调里 `cg_widget_serial(w)` 取句柄（**未打开/自检合成触发时是 NULL，必须先判**），`cg_serial_read()` 非阻塞取数据，
  想喂几个控件喂几个控件。运行时动态换端口/波特率：`cg_widget_serial_open(w, "COM4")`（会先关旧的）
  和 `cg_widget_serial_set_baud(w, 115200)`（开着就按新参数重开）。app 退出时链路自动关。
- `colorpicker` 的当前颜色存在 `value`（`0xRRGGBB`），点色块弹系统取色器。程序改颜色**直接用
  `cg_widget_set_value()`** 即可（2026-09-28 核实：运行时对 `colorpicker` 的值是特判的，
  **不按 `min`/`max` 夹取**）—— 早先那句"会被区间截断、别用它"已经过时。
- `colorpicker` 只有 `change` 一个事件，而且是**在系统取色器里点了确定之后**才发；
  **没有单独的"被点击"事件**，所以"点色块选中它"这种事得靠 `change` 来做。
- `rate` 星数 = `max-min`（上限 10）；`pagination` 页数 = `max-min+1`；当前值都用 `cg_widget_value()` 读。
- `canvas` 用 `cg_canvas_clear/set_color/set_line_width/line/rect/ellipse/text` 在回调里画，
  绘制命令被录下来，重绘时按序回放。
- **`canvas` 目前不能拖拽作图（2026-09-28 实测，已知缺口）**：画板自身只声明 `click` / `dblclick`，
  **没有** `mousedown` / `mousemove` / `mouseup`；而把这三个挂在**窗口级**（`app.events`）也不管用 ——
  程序化键鼠的对照实验里，合成**点击按钮**能正常驱动程序（日志有记录），但同样的合成"按下 → 拖动 → 抬起"
  在**画板上**与**窗口空白客户区**各做一次，程序**毫无反应**，即窗口级鼠标事件在真机运行中没有被派发。
  另外画板的 `click` 也**不回写坐标**（`ev_x` / `ev_y` 拿不到）。所以"在画板上拖拽作图"这条路目前走不通，
  要支持它需要给 `canvas` 补上自己的鼠标事件（或让窗口级鼠标事件真正派发并对子控件区域做命中测试）。
  参考实现见 `examples\drawpad`（设计、命令表与回放逻辑都是对的，能力补齐即可直接用）。
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

/* 控件绑定：dst 一直跟着 src 走（见 2.8）。two_way 非 0 = 双向 */
int  cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way);

/* 动作：命名命令，菜单项 / 控件用 .cgui 的 "action" 引用（见 2.8.1）。
 * 启用 / 勾选会同步到所有引用点；self 恒为主窗口伪控件、ev 恒为 click。 */
int  cg_action_invoke(cg_app_t *app, const char *name);        /* 1 = 真调了回调；0 = 无此动作 / 被禁用 */
void cg_action_set_enabled(cg_app_t *app, const char *name, int enabled);
int  cg_action_is_enabled(const cg_app_t *app, const char *name);
void cg_action_set_checked(cg_app_t *app, const char *name, int checked);
int  cg_action_is_checked(const cg_app_t *app, const char *name);
const char *cg_action_name(const cg_widget_t *self);           /* 在动作回调里问"是哪个动作" */
const char *cg_action_accel(const cg_app_t *app, const char *name);

/* 列表类控件（listbox / combo / tabs 标签 / statusbar 分段） */
int  cg_widget_list_add(cg_widget_t *w, const char *item);
int  cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...);
void cg_widget_list_clear(cg_widget_t *w);
int  cg_widget_list_count(const cg_widget_t *w);
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
/* 批量更新：两个之间只记脏、不重建，end 时重建一次。循环填表必须用它包住 ——
 * 表格每加一行都整表重建，逐行填 N 行 = N 次重建（连续闪烁 + 越填越慢），见文末"常见坑" */
void cg_table_begin_update(cg_widget_t *w);
void cg_table_end_update(cg_widget_t *w);
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

/* 纯显示：状态灯 / 仪表盘（都没有事件，值只能被程序写；滑块 / 进度 / 圆环用上面的 cg_widget_set_value） */
void cg_led_set(cg_widget_t *w, int state);          /* 0 灭 / 1 亮 / 2 闪（闪烁由控件自己定时重绘） */
int  cg_led_state(const cg_widget_t *w);
void cg_led_set_color(cg_widget_t *w, unsigned rgb); /* 亮起的颜色，缺省绿（等价 cg_widget_set_colors(w, rgb, CG_COLOR_NONE)） */
void cg_meter_set(cg_widget_t *w, int value);        /* 被 min/max 夹，和滑块同一套语义 */
void cg_meter_set_range(cg_widget_t *w, int min, int max);   /* max<=min 会纠正成 min+1 */

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

/* 菜单：运行时增删改（见 2.7）。定位一律用项 id，顶层用 NULL。
 * 新增的项不支持引用动作；改的是内存里的菜单，不写回 .cgui。 */
int  cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos,
                      const char *text, const char *id);   /* 返回新项下标；失败 -1 */
int  cg_menu_remove_item(cg_widget_t *w, const char *id);  /* 返回删掉的行数（含子树） */
int  cg_menu_clear_items(cg_widget_t *w, const char *parent_id);
void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text);
void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled);
void cg_menu_set_item_checked(cg_widget_t *w, const char *id, int checked);

/* 日期时间 / 月历 / 富文本 / 工具栏 都不需要专门 API：
 * 值就是 cg_widget_text()，工具栏选中项就是 cg_widget_value() */

/* 窗口 */
void cg_set_title(cg_app_t *app, const char *title);
void cg_cancel_close(cg_app_t *app);   /* 只在窗口的 close 回调里有意义：这次不关了 */

/* 深色模式（见 1.1.1）：只改默认配色，控件自己写了 fg/bg 的按它自己的来 */
int  cg_theme_set_dark(cg_app_t *app, int on);   /* 1 = 深色 / 0 = 浅色；立即重画 */
int  cg_theme_is_dark(const cg_app_t *app);      /* 现在是深色吗 */
void cg_win_set_pos(cg_app_t *app, int x, int y);        /* top-left，屏幕坐标 */
void cg_win_set_size(cg_app_t *app, int width, int height);
void cg_win_center(cg_app_t *app);
void cg_win_show(cg_app_t *app, int show);
void cg_win_set_state(cg_app_t *app, const char *cmd);   /* "min" / "max" / "normal" */
void cg_win_set_topmost(cg_app_t *app, int on);
void cg_win_set_icon(cg_app_t *app, const char *ico_path);   /* NULL 还原默认 */

/* 截图（工单取证 / 文档配图）：一律存 PNG。
 * 走 PrintWindow，所以窗口**被挡住或不在前台**也能拿到正确内容；少数控件不响应时退回抓屏，
 * 并在日志里说明"这张是抓屏来的、可能被遮挡"。GDI+ 起不来的机器返回 0（那种机器 image 控件本来也是占位框）。 */
int  cg_win_screenshot(cg_app_t *app, const char *path);        /* 整个窗口（含标题栏） */
int  cg_widget_screenshot(cg_widget_t *w, const char *path);    /* 单个控件（日期时间存主窗口那个） */

/* 文件拖放（在 app.events.drop 的回调里用；路径只在回调期间有效） */
int  cg_drop_count(cg_app_t *app);                 /* 本次拖进来几个文件 */
char *cg_drop_file(cg_app_t *app, int index);      /* 第 index 个的路径（堆串，用完 cg_free） */
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

/* 日志：GUI 版（-mwindows）没有控制台，printf/stderr 用户看不到。
 * 运行时启动时就会开一份日志：%LOCALAPPDATA%\<exe名>\app.log
 * 致命错误（界面文件找不到 / 格式错 / 创建窗口失败 / 崩溃）会弹中文提示框并写这份日志，
 * 业务代码也用 cg_log 往同一个文件里补记录。 */
void cg_log(int level, const char *fmt, ...);   /* CG_LOG_INFO / WARN / ERROR */
void cg_log_set_level(int level);

/* 基础能力（cg_kit）：界面之外但做真工具一定会用的。
 * 全是"返回堆串，用完 cg_free"；有了它们业务代码真的只 include cstackgui.h。 */
char *cg_file_read(const char *path);                       /* 整文件，UTF-8 BOM 自动去掉。
                                                             * ⚠ 含 0x00 的二进制**别用它**：它返回字符串，
                                                             *   strlen 会在第一个 0 处截断（固件/镜像用下面的）*/
unsigned char *cg_file_read_bytes(const char *path, int *out_len);  /* 二进制：带长度出口；0 字节文件也非 NULL */
int   cg_file_write_bytes(const char *path, const void *data, int len);  /* 二进制覆盖写；len = 0 建空文件 */
long long cg_file_size(const char *path);                   /* 不存在 / 目录 = -1 */
int   cg_file_write(const char *path, const char *text);    /* 覆盖写 UTF-8 */
int   cg_file_append(const char *path, const char *text);
int   cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size);
char *cg_app_dir(void);                                     /* exe 所在目录 */
char *cg_app_data_dir(void);                                /* %LOCALAPPDATA%\<exe名>，自动建 */
char *cg_path_join(const char *a, const char *b) / cg_path_dir / cg_path_name / cg_path_ext;
char *cg_ini_read(const char *path, const char *section, const char *key, const char *def);
int   cg_ini_write(const char *path, const char *section, const char *key, const char *value);
char *cg_str_format(const char *fmt, ...) / cg_str_trim / cg_str_replace;
int   cg_str_equal(const char *a, const char *b);           /* 忽略大小写 */
char *cg_utf8_to_gbk(const char *utf8) / cg_gbk_to_utf8;    /* 老 ANSI/GBK 文件互转 */

/* 编码：十六进制 / Base64 / URL —— 都是"调用方给缓冲"，返回写出的长度；
 * 参数或内容非法返回 -1，解码类缓冲放不下返回 -2（编码类需要的长度能事先算出来）。 */
int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size);
int cg_hex_decode(const char *text, void *out, int out_size);
int cg_base64_encode(const void *data, int len, char *out, int out_size);
int cg_base64_decode(const char *text, void *out, int out_size);
int cg_url_encode(const char *text, int space_plus, char *out, int out_size);
int cg_url_decode(const char *text, char *out, int out_size);

/* 字节操作：拼帧 / 拆帧的基元（风格同上：调用方给缓冲、返回写出的长度）。
 * 字节序参数统一：big_endian 非 0 = 大端（网络序 / Modbus 寄存器序），0 = 小端。
 * 越界一律"什么都不写 + 返回失败码" —— 半个字段写进帧里比报错更难查。 */
void cg_bytes_reverse(void *data, int len);                          /* 原地反转 */
int  cg_bytes_slice(const void *data, int len, int from, int count,  /* count = -1 取到底 */
                    void *out, int out_size);
int  cg_bytes_find(const void *data, int len, const void *needle, int nlen, int from);
int  cg_bytes_get_u16(const void *data, int len, int offset, int big_endian);   /* 越界 -1 */
long long cg_bytes_get_u32(const void *data, int len, int offset, int big_endian);  /* 见头文件：避免与 -1 撞车 */
int  cg_bytes_put_u16(void *out, int out_size, int offset, unsigned v, int big_endian);
int  cg_bytes_put_u32(void *out, int out_size, int offset, unsigned v, int big_endian);

/* 进制与补码：输出一律大写；cg_base_to_int 认 0x/0b/0o 前缀，整串必须合法 */
int cg_int_to_base(long long v, int base, char *out, int out_size);
int cg_base_to_int(const char *s, int base, long long *out);   /* 1 = 成功，0 = 失败 */
int cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size);
long long         cg_to_signed(unsigned long long v, int bits);    /* 0xFFF6 按 12 位 = -10 */
unsigned long long cg_from_signed(long long v, int bits);

/* 校验（CRC）：逐位算不建表，天然线程安全；_file 版本分块读，不整读进内存 */
unsigned cg_crc32(const void *data, int len);   /* IEEE 802.3 / zlib；"123456789" = 0xCBF43926 */
unsigned cg_crc16(const void *data, int len);   /* CRC16/Modbus；"123456789" = 0x4B37，挂帧尾低字节在前 */
int      cg_crc32_file(const char *path, unsigned *out);   /* 1 = 成功 */

/* 无线状态（IoT 上位机先看这一眼；零新增链接依赖 —— 符号运行期从系统 DLL 取） */
int cg_wifi_count(void);                       /* 有几块无线网卡（0 = 没有 / WLAN 服务没起） */
int cg_wifi_info(int idx, cg_wifi_info_t *out);      /* 描述/状态/当前 SSID/信号(-1 = 没连) */
int cg_wifi_networks(cg_wifi_net_t *out, int cap, int *count);  /* 周边网络（系统扫描缓存） */
int cg_ble_radio_count(void);                  /* 有几个蓝牙无线电（0 = 没有 / 关着） */
int cg_ble_radio_info(int idx, cg_ble_radio_t *out);
int cg_ble_device_count(void);                 /* 已配对/记得过的设备数（不做主动搜索） */
int cg_ble_device_info(int idx, cg_ble_device_t *out);
int cg_ble_find_device(const char *addr_or_name, cg_ble_device_t *out);  /* 按地址或名字找，0 = 找到 */

/* BLE 基元（不需要外设就能用；写 BLE 工具最容易踩的三处坑都在这里） */
void cg_ble_addr_str(unsigned long long addr, char *out, int out_size);  /* "AA:BB:CC:DD:EE:FF" */
int  cg_ble_addr_from_str(const char *text, unsigned long long *out);    /* 必须正好 6 字节 */
int  cg_ble_uuid16_str(unsigned short uuid16, char *out, int out_size);  /* -> 小写标准基写法 */
int  cg_ble_uuid16_parse(const char *text, unsigned short *out16);       /* "fff1" 与长写法都认 */
int  cg_ble_uuid_equal(const char *a, const char *b);                    /* "fff1" == 它的长写法 */
int  cg_ble_attr_flags(unsigned properties);                             /* -> CG_BLE_ATTR_* 组合 */

/* Modbus 帧编解码（纯函数：不持状态、不碰 I/O；RTU 自动挂 CRC 并校验，TCP 带 MBAP 头） */
int cg_modbus_build_read (int tcp, int tid, int slave, int func, int start, int count,
                          void *out, int out_size);          /* 01/02/03/04；返回帧长，-1 = 参数非法 */
int cg_modbus_build_write(int tcp, int tid, int slave, int func, int start,
                          const unsigned short *vals, int nvalues, void *out, int out_size);
int cg_modbus_parse(int tcp, const void *frame, int len, cg_modbus_rsp_t *out);
                          /* 0 = 成功；-1 帧太短；-2 CRC 不对（线上坏了）；-3 格式不对。异常响应已替你剥 0x80 */

/* 哈希（摘要）：手写实现；返回小写十六进制堆串，用完 cg_free */
char *cg_hash(const void *data, int len, const char *algo);      /* md5 / sha1 / sha256 / sha512 */
char *cg_hash_file(const char *path, const char *algo);          /* 分块读，核固件用 */

/* 系统信息：环境变量一个函数就够（精易那 34 条命令在 C 侧就是 cg_env） */
char *cg_env(const char *name);                    /* 没有返回 NULL；值一律 UTF-8 */
int   cg_env_int(const char *name, int fallback);  /* 不合法返回 fallback */
int   cg_sys_is64(void);                           /* 不随进程位数骗人 */
char *cg_sys_version(void);                        /* "Windows 10 (10.0.19045)" */
int   cg_disk_space(const char *path, unsigned long long *free_bytes, unsigned long long *total);
int   cg_process_running(const char *exe_name);    /* 大小写不敏感 */
char *cg_local_ip(void);                           /* 本机 IPv4；不发包、不查 DNS */
char *cg_clip_get_text(void);
int   cg_clip_set_text(const char *text);
int   cg_clip_get_image(const char *path);   /* 剪贴板里的图片存成 PNG（1 = 成功；需 exe 在跑）*/
int   cg_shell_open(const char *path_or_url);               /* 默认程序打开文件/网址 */
int   cg_run_process(const char *cmd_line, int wait);
char *cg_time_now(const char *fmt);
char *cg_reg_read(const char *subkey, const char *name, const char *def);
int   cg_reg_write(const char *subkey, const char *name, const char *value);
void  cg_free(void *p);                                     /* 上面返回的都要它释放 */

/* 业务数据挂载 / 定时器 / 退出 */
void  *cg_widget_get_user(const cg_widget_t *w);
void   cg_widget_set_user(cg_widget_t *w, void *user);
cg_timer_t *cg_set_timer(cg_app_t *app, int delay_ms, void (*fn)(void *), void *user);
void   cg_kill_timer(cg_timer_t *t);
void   cg_quit(cg_app_t *app);
void   cg_set_title(cg_app_t *app, const char *title);
```

---

### 6.1 终端 / 图表 / 串口 / 网口 / 对话框 / 窗口（这里不逐条抄签名）

下面这些组**框架都有**，本文不逐条抄签名（抄了容易和头文件跑偏）—— 用之前看
`runtime/include/cstackgui.h`，或看右侧那列的专文。**别因为速查里没列，就以为框架没有。**

| 组 | 函数（前缀 / 名字） | 深入看 |
| --- | --- | --- |
| 终端（日志视图） | `cg_term_write` / `cg_term_clear` / `cg_term_save` / `cg_term_set_hex` / `cg_term_hex` / `cg_term_set_timestamp` / `cg_term_set_max_lines` / `cg_term_count` | 本文 2.9 |
| 图表 | `cg_chart_` 系列（推点、清空、系列与坐标轴设置） | 本文 2.10 |
| 串口 | `cg_serial_` 系列（枚举 / 打开 / 收发 / 关闭） | `COMM.md` |
| 网口 | `cg_net_` 系列（TCP 客户端与服务端、UDP 数据报） | `COMM.md` |
| 通用对话框 | `cg_open_file_dialog` / `cg_save_file_dialog` / `cg_choose_color` / `cg_message_box` | 本文 6 节上文 |
| 窗口 | `cg_win_center` / `cg_win_set_pos` / `cg_win_set_size` / `cg_win_show` / `cg_win_set_topmost` / `cg_win_set_state` / `cg_win_set_icon` / `cg_cancel_close` / **`cg_win_screenshot` / `cg_widget_screenshot`**（存 PNG） | 本文第 1 节 |
| 散项 | `cg_splitter_pos` / `cg_splitter_set`、`cg_browser_navigate`、`cg_widget_hint`、`cg_widget_count`、`cg_widget_set_value_range`、`cg_widget_list_scroll_bottom`、`cg_file_exists` / `cg_file_delete` / `cg_file_copy`、`cg_dir_exists` / `cg_dir_create`、`cg_event_name` / `cg_event_from_name`、`cg_dump_doc` | 头文件 |

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
| `actions` / `action` 有问题（某条动作没写 `handler` 或 handler 没 `CG_BIND`、`action` 引用了不存在的动作名、该类型不支持 `click` 却写了 `action`、既写 `action` 又绑了 `events.click`、`accel` 看不懂或与别的动作重复）| **只警告不退出**：出问题的那个动作（或那条快捷键）被忽略，界面照常打开；`--selftest` 下全部算失败，返回码 5 |
| `styles{}` 里某条有问题（名字为空、值不是对象、字段值非法） | **跳过该条 / 该字段**，不报错、不中断加载：`use` 指向不存在的名字 = 当没有 `use`，`"style": "不存在的名字"` = 没写 `style` |
| `style` 引用了不存在的样式名 | **不崩、不中断加载**（按"没写 `style`"处理），但**不静默**：启动时记一条 WARN（写明有几个控件、第一个失效的名字），`--selftest` 把它计入失败（返回码 5）—— 与 `events` 里名字没登记、`actions` 出问题同一个口径。**画布不认得这个失效是设计期标红的**，两端合起来才不漏 |
| 控件 id 重复 | 不报错，但 `cg_find` 命中的是**后建**的那个（运行时按创建顺序倒序查找）；`cg_widget_add` 造重名会直接失败并报错 |
| `image` 的 `src` 找不到图片 | 只警告，控件显示成虚线占位框（把路径画在中间便于排查） |
| `button` / `iconbutton` 的 `src` 找不到图片 | 只警告；`button` 照常画纯色按钮、`iconbutton` 画占位框 |
| 循环里逐行 `cg_table_add_row` 填很多行 | **每加一行都会整表重建**（删列、删全部行再重插）⇒ 界面连续闪烁、耗时随行数涨得很快。用 `cg_table_begin_update` / `cg_table_end_update` 把"清空 + 填行"包起来：中间只记脏、结束时重建一次（`dataview` 翻页就靠这个） |
| `table` 的双击事件只告诉业务"哪一行"，不告诉"哪一列" | 双击回调里用 `cg_table_selected(w)` 取行号（双击前的选中变化已经把它更新好了）；**列号框架不给**，要按列做事就自己放一个"操作列"下拉 —— `dataview` 的"双击进入编辑"就是这么做的 |
| `cg_tree_add` / `cg_table_add_row` 等用在错的控件类型上 | 打印一行 `需要 xxx 控件，实际给的是 yyy`，返回 -1，不做任何修改 |
