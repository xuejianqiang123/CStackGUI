# CStackGUI

**拖拽画界面 → 存成 `.cgui` → C 写业务 → 编译出一个独立 exe。**

> 只想用、不想看开发细节？直接看 [release\README.md](packaging/README.release.md) ——
> 那是发布包里的使用者说明，从双击 exe 到出 exe 全流程，不带开发内容。

---

## 一、简介

### 1.1 它是什么

CStackGUI 是一套「画布 + C 运行时」的桌面 GUI 开发方案，三句话讲完：

- 界面的长相在**画布**里拖出来，存成 `.cgui` 文件；**程序运行的时候才读它，不编译进去**——
  改界面不用重新编译 C。
- 业务逻辑是普通 C 文件，只写回调函数；**事件按控件特性分配**（标签没有交互、只有树有
  展开/折叠），画布只列出该控件真正支持的事件。
- 最终产物是**一个 exe，零依赖**：换台电脑复制过去就能跑。

```
designer\    画布（Electron + React） ──打包──>  release\CStackGUI.exe          ← 你用来画界面
runtime\     C 运行时库（Win32 后端） ──编译──>  build\lib\libcstackgui.a
apps\<名字>\  业务程序（.cgui + C 文件）──链接──>  apps\<名字>\<名字>.exe          ← 你交付给别人的
```

### 1.2 有哪些控件

**42 种**，全部是真能用的原生/自绘控件（点得动、能输入、能改状态、能绑事件）：

| 分类 | 控件 |
| --- | --- |
| 基础 | 按钮 / 图标按钮 / 标签 / 链接 / 开关 |
| 输入 | 输入框（单行 / 多行 / 数值框三种形态，同一个 `edit`）/ 富文本 / 日期时间 / 月历 / 复选框 / 单选钮 / 列表框 / 下拉框 |
| 数值 | 滑块 / 进度条 / 圆环进度 / 仪表盘 |
| 数据 | 标签页 / 树 / 表格（多选、勾选列、行数值、行图标）/ 状态栏 / 终端（日志视图）/ 图表（折线 / 柱状，实时滚动）|
| 菜单 | 菜单栏 / 菜单按钮 / 工具栏 |
| 容器 | 分组框 / 容器块 / 卡片 |
| 其它 | 分割线 / 分隔条（可拖动的分栏边界）/ 图片 / 外形框 / 时钟 / 画板 / 颜色选择 / 状态灯 / 网页浏览（实验性） |
| 装饰 | 徽章 / 标签片 / 评分 / 分页 |

事件**按控件特性分配**，不是每个控件一套：控件级有 `click / dblclick / change / submit /
select / release / expand / collapse / tick / check / spin / gotfocus / lostfocus`，外加
`button` / `edit` 的控件级鼠标键盘全套；窗口级那套（`ready / close / size / keydown / wheel`…）
写在 `app.events` 里。每种控件支持哪些事件见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md)
第 4 节（真相在 `runtime/src/core/cg_doc_kind.c` 的 `k_kinds` 掩码）。

界面之外还有一套 **基础能力 API**（`cg_kit`）：读写文件、INI 配置、路径处理、
文本与编码转换（UTF-8 ↔ GBK）、**十六进制 / Base64 / URL 编解码**、
**进制转换与补码**（2/8/16/36 互转、按位宽读有符号数 —— 看寄存器值用）、
**字节操作**（反转 / 取左右 / 找子集 / 按字节序读写 u16·u32 —— 拼帧拆帧的基元）、
**文件读写**（文本一条路、**二进制**一条路 —— 含 `0x00` 的固件 / 镜像走后者，不会静默截断）、
**CRC32 / CRC16-Modbus 校验**（工控帧）、**MD5 / SHA-1 / SHA-256 / SHA-512 摘要**
（含文件校验，分块读不占内存）、**系统信息**（环境变量、系统版本、磁盘剩余、
进程在不在、本机 IP）、剪贴板、
用默认程序打开文件/网址、启动外部程序、读写注册表（做开机自启）、写日志。
**所以业务代码从头到尾只 include `cstackgui.h`**，
不用为了读个配置去 include `windows.h`。这些函数的用法在画布「支持库」页签里一查就有 ——
而且它与画布是**双向连通**的：选中一个控件，「支持库」页签自动预置该类型相关的关键词
（表格 → 表格/行/列，定时器 → 定时器）；反过来点某条 API，能看到它被哪些控件的哪个事件绑着、
一键定位到画布上。"按钮按了没反应"这类差评，先从这两头查起。

**串口与网口也在这套能力里**：`cg_serial_*`（枚举端口 / 打开 / 收发）、`cg_net_*`（TCP 客户端 / 服务端 / 收发），
以及同一层的 UDP（`cg_net_udp_open` / `cg_net_sendto` / `cg_net_recvfrom`，数据报收发 —— 广播发现、板子上报都走它）。
业务侧照样不用 include `windows.h`：串口与 TCP 由运行时在**读线程**里把字节搬进环形缓冲，业务在时钟回调里取；
**UDP 不开读线程**（数据报的报文边界与来源地址必须原样交给业务，所以它是"时钟里 `recvfrom`"的拉取式）。
所以用法与坑单独写在 [docs/COMM.md](docs/COMM.md)。

界面里还有一件**不用写代码**的事：**控件绑定**。在画布「属性」里给一个控件选个"源"控件
（等价于 `.cgui` 里写 `"bind": { "src": "sld_level" }`），它的值/文字/勾选就一直跟着源走 ——
"滑块拖动时旁边的标签显示数字"这种活，一行 C 都不用写，连重置都不用管它。
源与目标的类型决定抄什么、双向怎么用，见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 2.8 节，
活例子见 `examples\lesson03_values`（拖动滑块，旁边的标签自己跟着变；**那几个回调都不用写**）。

和它同类的是**动作（actions）**：`.cgui` 顶层写一条命令（文字 + 快捷键 + 回调名），菜单项和按钮都用
`"action": "save"` 引用它 —— **一个回调管住菜单项和按钮，一处启用状态全跟着变**；快捷键走消息循环里的
加速键表，所以 **Ctrl+S 在输入框里也按得出来**（按键会被输入框吃掉，窗口级事件收不到）。
见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 2.8.1 节。

再往上一层是**命名样式（`styles{}`）**：`.cgui` 顶层把一套字体 / 字号 / 配色写一次、给个名字，
控件按名字引用（`"style": "log"`，还能 `"use": "log"` 再局部覆盖几个字段）——
**改一次字号不用再改几十处**。它只有「名字引用 + 局部覆盖」这一层，**不是 CSS**
（没有选择器 / 继承 / 层叠 / 优先级计算），见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 2.2.1 节。

仓库自带几类示例，分工不同：

- `examples\lesson01_basics` ~ `examples\lesson06_window` —— **六课教学示例**：基础与容器 →
  输入与选择 → 数值与指示 → 数据与日志 → 命令与图形 → 主窗口与窗口事件。每课只讲一个主题、
  **只出现本课主题的控件**、各用一屏摆齐、都能单独编译运行。
  **入门从这里开始**，路线图与逐课详解见 [examples\教学使用说明.md](examples/教学使用说明.md)。
- `examples\cppdemo\` —— **C++ 例程**：同一个工程也能用 C++ 写（把源文件叫 `.cpp`，构建脚本自动换
  `g++ -std=c++17` 并用 g++ 链接，`libstdc++` 静态收进 exe）。演示"数据放 `std::vector`、状态放对象、
  回调 `extern "C"` 转发到单例"，`.cgui` / 绑定表 / `--selftest` 与 C 工程一字不差。
- `examples\gallery\` —— **控件陈列室（三合一）**：顶层标签页分「基础闭环 / 控件陈列 / 新特性」
  三页，67 个控件、47 个回调，**讲"控件怎么用"**（当"控件字典"翻）。
- `examples\notepad\` —— **真实小工具范例（记事本）**：菜单分发、脏标记、状态栏刷新、
  文件读写、GBK/UTF-8 识别、INI 记住上次文件与窗口尺寸、关闭前拦截。**讲"一个工具该怎么组织"**，
  也是 `cg_kit` 基础能力库的活例子（全程没有 include 任何 Windows 头）。
- `examples\serialterm\` / `examples\netterm\` / `examples\darkterm\` —— **接上真实链路与主题三个**：
  串口助手、网络调试助手，以及**深色主题示例**（`app.dark` + 命名样式 + 一个**运行时切换主题**的按钮）。
  前两个讲"链路上怎么收怎么发"，第三个讲"要为深色写哪套配色、哪些只能自己写死"。
  `serialterm` 功能上**对标 XCOM**（16 进制显示 / 时间戳 / 自动保存 / 定时发送 / 多条发送 / 文件收发），
  另外用 `cg_frame_*` 做了 XCOM 没有的**按帧显示**（四种定界可选：静默 / 换行符 / 定长 / 长度字段，
  每帧长度 + CRC16 结论），并用 `cg_proto_*` 做了**协议组帧面板**（帧头/地址/功能码/数据/校验/帧尾
  通用骨架 + 实时预览 —— 替掉 XCOM 那个写死厂商私有格式的「协议传输」页签）；
  不抄它的私有 IAP「协议传输」——通用件是 `cg_crc16` + `cg_modbus_*`。
  自测不需要硬件：`serialterm.exe --demo`（灌合成字节，结论打到 stdout）。
- `examples\shottool\` —— **QQ 风格截图工具**：托盘常驻 + 全局热键 `Ctrl+Alt+A` + 屏幕区域
  框选 + 矩形/椭圆/箭头/画笔/文字标注 + 完成即存 PNG 并复制剪贴板 + 贴图钉屏；
  另有一条 **GIF 录屏**（`Ctrl+Alt+G`：复用同一套框选，按帧率抓屏存成动画 GIF）。演示 runtime
  的五条"界面之外"能力 —— **屏幕抓取**（`cg_screen_capture`）、**全局热键**（`cg_hotkey_set`，
  后台也按得到）、**图片写剪贴板**（`cg_clip_set_image`）、**GIF 编码与录屏**（`cg_gif_*`，
  脏矩形优化：只编码变化的那一块）、以及**画板背景图**（截图当底、
  标注当命令，成品 = 抓画板自己）。
- `examples\modbus\` —— **工控向样板（Modbus/TCP 上位机）**：`cg_modbus_*` 拼请求帧 / 解响应帧，
  `cg_net_*` 收发，切帧交给 **`cg_frame_*`**（长度字段模式；串口上的 Modbus/RTU 换成
  `CG_FRAME_SILENCE` 即可），异常响应翻成人话，等回应超时报一句 —— 顺带带了 `mock-slave.mjs`
  （Node 模拟从站），**不需要任何硬件就能把整条链在本机跑通**：
  `node examples/modbus/mock-slave.mjs 1502`。

### 1.3 当前边界

- 运行时以 Win32 原生控件为底：稳、零依赖、输入法无障碍白送。按钮/开关/勾选/输入框/滑块/进度条/圆环
  走 **GDI+ 自绘外观层**（`runtime\src\skin\`），吃圆角与配色；列表框/树/表格/标签页等系统控件不吃。
- 布局仍是**绝对坐标**（所见即所得），没有布局引擎；给控件写了 `grow` 之后，**窗口拉伸时该吃的控件真会吃余量** ——
  多出来的空间按权重分给同一容器里的兄弟（**同一行 / 同列的并排控件各自把这一带填满**），带之后的兄弟整体平移，
  和分隔条（`splitter`）是同一套心智模型（见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 2.1.1）。
  写了 `grow` 有没有生效，**不用编译运行**：工具栏「**预演**」开关打开后，宽/高两条滑杆把窗口模拟拉大拉小，
  画布按同一套"按带分配"算法实时重排（**只读模拟，设计坐标一个字节都不动**）——原来只能 F5 跑起来看，现在 0 秒。
- 画布支持**多选**（Shift/Ctrl 点选、空白处拖框选、Ctrl+A 全选顶层）与**对齐/分布工具条**
  （左中右 / 上中下 / 横纵等距 / 等宽·等高·同尺寸；选中 ≥2 时浮在画布顶部）。
  还没有**组合**（把多选固化成文档里的一个组）和**批量属性编辑**（有网格吸附和邻近参考线）。
- **点表批量导入**：工具栏「导入点表」粘一段 `文字,类型[,值]`（每行一个信号，PLC 点表从 Excel 粘出来就是这形状），
  一次生成一网格控件（8 列网格；`#` 开头是注释；**坏行逐行报错但不停导入**，好行照进）。
  一次导入 = **一步撤销**。几百个点不用一个个拖 —— 生成的 `.cgui` 与手写逐字节同格式（git 可 diff、可评审）。
- **深色模式**（`app.dark`，或运行时 `cg_theme_set_dark()`）跟着变的是**默认值**：窗口与面板
  （`group` / `box` / `card` / `shape`）的底色、没写 `fg` 的标签与自绘文字（复选框 / 单选钮 / 开关的标签）、
  没写颜色的输入框、终端、**中性灰的那些细部件**（进度与圆环的槽／轨道、滑块的槽、状态灯的灭态、
  图表的边框·网格·轴标签），以及分组框里的标签与输入框。
  **不跟随的**：列表框 / 表格 / 树（Win11 实测无效）、标签页、下拉列表、月历、日期选择、状态栏、
  工具栏，以及 `slider` 的**画布底**（槽跟随了，但控件底由原生 trackbar 画，实测改不动）；
  按钮 / 勾选 / 开关这类自绘控件**底色跟着深、品牌色填充不跟**（仍是 M3 浅紫 —— 有意为之，
  画在它上面的文字也就不跟着变浅）；自己写了 `fg`/`bg` 的控件一律保持原样 ——
  **写死颜色的控件要深浅两套值**，用**色彩角色**解决：`roles{}` 定义深浅两份值、
  颜色字段写 `"@角色名"` 引用，深浅切换全套跟着走（见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 2.2.2）；
  菜单 / 列表 / 树是平台边界。
  见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 1.1.1。
- **截图**：`cg_win_screenshot()` / `cg_widget_screenshot()` 把整个窗口或某个控件存成 **PNG** ——
  走 `PrintWindow`，所以窗口被挡住、不在前台也拿得到正确内容；现场出问题时一键取证贴进工单
  （见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 6 节）。
- **文件拖放**：把固件 / 日志从资源管理器拖到窗口上就行 —— `.cgui` 里给 `app.events.drop`
  绑一个回调，`cg_drop_count()` / `cg_drop_file()` 取路径（**只在回调期间有效**）。
- **剪贴板图片**：`cg_clip_get_image()` 把剪贴板里的图存成 **PNG** —— 示波器 / 相机截的图
  直接落进调试记录（见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 6 节）。
- 菜单可以在运行时增删改（`cg_menu_add_item` / `remove` / `clear_items` / `set_item_*`），但有两个边界：
  新增的项**不支持引用动作**（要快捷键就在 `.cgui` 里写固定项）；改的是内存里的菜单，**不写回 `.cgui`**。
- 控件绑定只镜像**值 / 文字 / 勾选**三类语义，**不做格式换算**（不能把 0~100 映射成 0~5、或自动加
  `%` 后缀）；要格式化还是得写回调。颜色选择器的颜色值不参与绑定。
- 动作（`actions`）v1：工具栏（`toolbar`）的按钮**不支持**引用动作（它的条目模型是纯文字列表）；没有
  运行时增删菜单项的 API；动作的 `text` 不能在运行时改。
- 网页浏览控件走系统 WebBrowser（IE 内核），标注为实验性。

**文档地图**（每份只讲一块，别指望一份读全）：

| 想了解 | 看哪份 |
| --- | --- |
| 怎么上手、环境、示例学习路线 | 本文件 |
| 界面文件 `.cgui` 每个字段是什么意思 | [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) |
| 内部为什么这么设计、模块怎么分层 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| 自绘外观（M3）怎么改 | [docs/M3-SKIN.md](docs/M3-SKIN.md) |
| 怎么读写串口 / 网口 TCP 与 UDP / 通信层怎么用 | [docs/COMM.md](docs/COMM.md) |
| 想按入门顺序读一遍（10 篇文章 + 图文） | [docs/articles/README.md](docs/articles/README.md) |
| 把工程交付给别人、打包发布 | [packaging/](packaging/) |
| C 代码规范、改动前必读的约定 | `.codebuddy/skills/`（`firmware_code_style` / `designer-modify`）|

**对不上时信谁**（优先级从高到低）：`runtime/` 的代码与 `tests/` 的断言 → `docs/*.md` → 界面上的文字提示。
两条"真相所在"的位置要记住：`.cgui` 的**字段语义**在 `runtime/src/core/cg_doc.c`，
**控件类型与支持的事件**在 `runtime/src/core/cg_doc_kind.c` 的 `k_kinds` ——
画布端 `designer/src/model.ts` 是跟着它们走的，不是反过来。
文档与代码不一致时**按代码改文档**，并把该补的测试补上。

---

## 二、开发环境搭建

### 2.1 需要装什么

| 需要 | 版本 | 什么时候需要 |
| --- | --- | --- |
| MinGW-w64 gcc | 8+（实测 16.2） | **编译 exe 时**。默认找 `C:\mingw64\bin`，下载：https://pan.quark.cn/s/f7f5f4791017 |
| Node.js | 22.12+ / 24+ | 只在想自己改画布、重新打包画布时 |

先用 `gcc --version` 确认；提示"不是内部或外部命令"就把 MinGW 的 `bin` 加进 PATH，
或直接设 `MINGW` 环境变量。画布本身**不依赖** C 编译器：它只负责生成 `.cgui` 和 C 骨架。

### 2.2 画布从哪来

- **直接用**：双击 `designer\release\CStackGUI.exe`（仓库里已打包好）。
- **自己打包**：`cd designer && npm install && npm run dist` → 产出 `designer\release\CStackGUI.exe`。
- **开发态**：`cd designer && npm run dev`（vite + electron，改画布源码用）。
- **给没有这个仓库的人用**：项目根跑 `make-release.bat` → 产出自包含的 `release\` 目录
  （画布 + 预编译运行时库 + 示例 + 编译脚本 + 使用者说明，打包前会真编译真跑一遍示例自检）。

### 2.3 从画布到 exe（第一次大约 5 分钟）

1. **摆界面**：双击画布 → 工具条定窗口标题/宽高/底色 → 左侧控件面板点一下或拖进画布 →
   拖动手柄改尺寸；控件拖进分组框会变成子控件（坐标相对容器）。
2. **定属性**：右侧「属性」页签改 `id`（C 代码靠它找控件，起个好认的名字）、文字、
   位置尺寸、字体颜色、初始状态。
3. **绑事件**：右侧「事件」页签给控件填 **C 函数名**（「自动」按钮按 `on_<id>_<事件>` 生成）。
   一个函数名可被多个控件共用；绑了不支持的事件，运行时会警告、`--selftest` 会报错。
   不知道回调里能写什么？右侧第 4 个页签「**支持库**」按用途列出了全部 C 函数（附说明与示例，
   双击即复制调用代码）——新手从这里抄第一段代码最快。
   导出骨架之后，事件行上的「**代码**」按钮会直接打开 `*_ui_events.c` 里对应那个函数
   （装了 VS Code 用 `code -g` 跳到行），不用自己翻文件找。
4. **保存**：`Ctrl+S` 存成 `.cgui`（例如 `apps\myapp\myapp.cgui`）。
5. **导出骨架**：工具条「导出 C 骨架」→ 选 `apps\` 目录。生成 5 个文件：
   `<名字>.cgui`（画布产物）、`<名字>_ui_events.h/.c`（**.c 是你的代码，主要改它**）、
   `main.c`、`build.bat`。**重新导出不会覆盖你写过的 `_ui_events.c`**。
   **首次导出**（目录里还没有骨架）会先问一句用 **C 还是 C++**：
   C++ 给的是 `<名字>_ui_events.cpp` + `main.cpp`（MinGW-w64 自带 g++，编译脚本自动换
   `g++ -std=c++17` 并用 g++ 链接）；已经导过的工程不再问 —— 同一个工程混两套源文件会
   链接重复定义，要换语言请另建工程。
6. **填逻辑、编译**：在项目根目录跑：

   ```bat
   build-app.bat myapp
   ```

   产物落在工程自己的目录里，双击即跑：

   ```
   apps\myapp\
     myapp.exe           <- 双击就能跑（界面文件就在旁边）
     lib\libcstackgui.a  <- 运行时库副本，整个目录拷走也能重编译
   ```

   > 工程目录里**不再**生成 `build-app.bat`：编译在项目根那条（`build-app.bat <名字>`）
   > 或画布里打开 `.cgui` 按 F5。编译的实现在 `CStackGUI.exe` 内部，客户不需要装
   > Node.js，也不需要旁边放任何内核文件 —— 只要装了 MinGW-w64。

7. **运行 / 自检**：

   ```bat
   apps\myapp\myapp.exe                                  :: 双击也行（自动找旁边的 .cgui）
   apps\myapp\myapp.exe --selftest apps\myapp\myapp.cgui :: 不开窗，解析 + 真调一遍所有控件回调（窗口事件只核对名字）
   ```

   `--selftest` 是唯一能自动发现"事件名写错"的手段：有名字没登记就非 0 退出。

### 2.4 之后改东西要做什么

| 你改了什么 | 要做什么 |
| --- | --- |
| 界面（位置、文字、加控件、改事件名） | 画布里改 → `Ctrl+S` 覆盖 `.cgui` → **直接重跑 exe，不用重新编译** |
| 业务逻辑（回调函数体） | 改 `_ui_events.c` → `build-app.bat <名字>` → 重跑 |
| 动了 `runtime\` | 直接重新 `build-app.bat <名字>`（它会先刷新运行时库） |

第一行是这个项目最大的省事之处：调个按钮位置不用重编译 C。

### 2.5 常用命令

```bat
:: 编译（项目根目录执行）
build-app.bat myapp              :: 刷新运行时库 + 编译 apps\myapp\（带控制台）
build-app.bat myapp --release    :: 发布版：-mwindows，没有控制台窗口
build-all.bat                    :: 运行时 + examples\ 下所有示例（每个都跑一遍 --selftest）
build-runtime.bat                :: 只编译运行时库（build-app 会先跑它）

:: 验证（改了 .cgui 字段或控件类型后必跑）
build-app.bat gallery                                            :: 先有 gallery.exe
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui  :: 回调闭环自检
examples\notepad\notepad.exe --selftest examples\notepad\notepad.cgui  :: 真实工具范例自检
tools\run-tests.bat                                              :: C 层单测（JSON/文档/类型表/事件表）+ cg_kit 冒烟
tools\run-tests.bat --memcheck                                   :: 再叠一层堆泄漏记账（ld --wrap malloc/free）
tools\check-format.bat                                           :: 格式契约回归 + 支持库表一致性 + bat 编码
                                                                 :: （含 electron 语法 + 骨架语言判据 check-skeleton）
tools\check-kit.bat                                              :: cg_kit 基础能力冒烟测试（已并入 run-tests）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix
                                                                 :: 动过 .bat 之后跑：UTF-8 无 BOM + ASCII 前导，否则双击时注释被当命令跑

:: 画布（designer 目录里执行）
npm install / npm run dev / npm run build / npm run dist
```

---

## 三、如何使用 CodeBuddy AI 开发当前工程？

本仓库的 `.codebuddy\` 目录是一套完整的 AI 辅助开发体系（技能 + 工作流 + 门禁），
用 CodeBuddy 打开本工程会自动加载。体系总览见 [.codebuddy/README.md](.codebuddy/README.md)，
这里只讲和**本工程**直接相关的用法。

### 3.1 本项目沉淀的专属技能

| 技能 | 干什么 | 什么时候自动触发 |
| --- | --- | --- |
| `designer-modify` | 改画布（designer/）的完整说明书：文件地图、`.cgui` 契约、新增控件要动的 7~8 处、验证门禁 | 只要改 `designer\` 下的代码 |
| `firmware_code_style` | C 代码规范（含函数 Doxygen 注释格式 `@brief/@param/@return`） | 写/改 `.c`/`.h` 时 |
| `git-commit-style` | 提交信息格式：`[类型][模块] 一句话说清` | 要求提交时 |
| `plan-create-prd` / `plan-architecture` / `piv-slice-epic` | 需求 → PRD / 架构 → SDD / 大需求拆切片 | 提出新功能、说"先做计划" |
| `piv-plan-implementation` → `piv-implement` → `piv-validate` → `piv-review-changes` → `git-commit-style` | 计划 → 实现 → 验证 → 审查 → 提交 的内层循环 | 开发任务的标准流程 |
| `piv-run-full-loop` | 上面整条链一次跑完 | 提出明确的小功能需求时 |

此外还有一组审查技能（缓冲区 / 内存 / 通信协议 / 错误处理 / 结构体 / C 规范 / 界面观感）会在提交前审查时按改动领域自动调用；完整清单见 `.codebuddy/skills/INDEX.md`。

### 3.2 推荐的对话方式

**加一个功能（推荐走完整闭环）**：

```
你：「给表格控件加行勾选功能，要能读到每行勾选状态」
AI：plan-create-prd → 出 PRD 和验收标准 → 确认后 piv-run-full-loop
    → 实现 → build-all + selftest 验证 → 审查 → 分批提交
```

**改画布外观**：

```
你：「把控件面板按分组折叠」
AI：自动加载 designer-modify（改 designer\ 前必读）→ 定向修改 → npx tsc --noEmit + vite build
```

**手写一个新工程**：

```
你：「在 apps\ 里做一个串口调试助手」
AI：piv-run-full-loop → 画布建界面（.cgui）→ 生成回调骨架 → 填业务 → build-app.bat 编译自检
```

### 3.3 AI 改完代码，必须过的门禁

无论 AI 还是人改了代码，这几条是硬门禁（`piv-validate` 会自动跑）：

```bat
build-all.bat                                  :: 运行时 + 所有示例编译通过
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui   :: 回 0 才算事件闭环
tools\run-tests.bat                            :: C 层单测：JSON / .cgui 解析 / 类型表 / 事件表 + cg_kit 冒烟
tools\check-format.bat                         :: 画布写的 .cgui 必须被 C 侧读懂（改格式/控件时）
                                               :: 顺带查「支持库」表与 cstackgui.h 是否还一致
                                               :: 再顺带查仓库 bat 的编码与 ASCII 前导（见下面第 4 条）
tools\check-kit.bat                            :: cg_kit 基础能力（文件/配置/编码/注册表）冒烟测试
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix
                                               :: 动过任何 .bat 之后跑（-Mode check 已并入 check-format）
cd designer && npx tsc --noEmit && npx vite build   :: 改了画布时
```

门禁红了先看 4.3 节开头的**排查表**（现象 → 先查什么），别从头猜。
另外提醒一句：**`read_lints` 这类 IDE 提示不报 TS 语法错误** —— 曾经出现 `model.ts` 少一个 `}`
（lint 0 错、`tsc --noEmit` 只说"文件末尾少个 `}`"），最后是 `npx vite build` 直接指出行号。
所以改了画布必须真跑 tsc + vite build。

### 3.4 让 AI 不踩坑的几条铁律（也是代码评审重点）

1. **控件类型表有两份，必须同步**：`runtime\src\core\cg_doc_kind.c` 的 `k_kinds` 与
   `designer\src\model.ts` 的 `KINDS`。新增控件 = 两边各加一行 + `cg_win_ctl.c` 的
   class/style 分支 + 画布 `WidgetView.tsx` 外观；事件表 `KIND_EVENTS` 与 `CG_EV_*` 位掩码同理。
2. **`.cgui` 字段语义的真相在 C 侧**（`cg_doc.c` 的解析逻辑），画布模型跟着它走；
   改格式必须跑 `tools\check-format.bat`，防止"画布能存、程序起不来"。
3. **界面之外的能力由 `cg_kit` 提供**（文件/配置/编码/剪贴板/注册表）——不要为了这些去
   include `windows.h`：那样业务代码就绑死在 Win32 上了，而且画布「支持库」页签里也查不到。
4. **bat 存成 UTF-8（无 BOM）+ CRLF，开头放一段纯 ASCII 前导**。cmd 用"启动这个 cmd 进程时的
   控制台代码页"解码 bat，中文 Windows 双击时是 936 —— UTF-8 的中文会被按 2 字节/字解码、行定位
   错位，注释后半句被当命令执行（满屏 `"... is not recognized ..."`）。前导先把代码页切成 65001、
   再用 `cmd /c "%~f0"` 让同一个文件在**新进程**里重跑一遍，那一趟从头按 UTF-8 解码且中途不再切页，
   中文注释与中文输出都正常（双击和 UTF-8 终端都验证过）。另外：**带中文的 bat 里不能出现
   `goto` / `call :label`**（cmd 找标签按字节偏移扫描，UTF-8 中文会把它扫歪，表现为结尾多一行
   `"*"` 报错或莫名 `Press any key`）—— 子过程要拆成独立 .bat（如 `tools\gen-launcher.bat`）。
   改完跑 `tools\bat-utf8.ps1 -Mode fix` 归位，`-Mode check` 已并入 `tools\check-format.bat`。

---

## 四、工程详解

### 4.1 目录结构

**`examples\` 是仓库自带的示例，`apps\` 是你自己的工程**（画布「导出 C 骨架」的默认去处），
两者分开，别弄混。

```
CStackGUI\
  README.md                    本文件（开发者视角）
  docs\ARCHITECTURE.md         架构设计与取舍
  docs\CGUI-FORMAT.md          .cgui 字段规范 + C API 速查
  designer\                    画布源码（Electron + React + TS）
    src\model.ts               .cgui 数据模型 + KIND_EVENTS 事件表（与 cg_doc.c 对应）
    src\supportlib.ts          「支持库」页签的数据：C API 中文索引（与 cstackgui.h 对齐）
    src\codegen.ts             导出骨架（C / C++ 两套模板）
    electron\main.mjs          文件读写、导出、菜单、项目根记忆（便携版自动识别）
  runtime\                     C 运行时库
    include\cstackgui.h        业务代码唯一需要 include 的头
    src\core\                  后端无关层（JSON / 文档 / 控件模型 / 事件 / 生命周期）
      cg_json.c                只读 JSON 解析
      cg_doc.c                 .cgui -> 控件文档树（解析 / 释放 / 打印）
      cg_doc_kind.c            控件类型表 k_kinds（含每种控件支持的事件）
      cg_event.c               事件名表、回调绑定解析、事件派发
      cg_widget.c              运行时控件模型：构建、查找、属性 API
      cg_widget_data.c         路标：七个文件各管什么（原 1149 行按控件族拆开）
      cg_widget_list.c         列表 / 状态栏 / 菜单 / 提示 / 时钟 / 图片
      cg_widget_tree.c         树（含跨文件共用的 want_type）
      cg_widget_table.c        表格
      cg_widget_tabs.c         标签页
      cg_widget_split.c        分隔条
      cg_widget_term.c         终端（日志视图）
      cg_widget_gauge.c        图表 / 状态灯 / 仪表盘
      cg_bind.c                控件绑定：值/文字/勾选自动同步到另一个控件（.cgui 的 bind）
      cg_action.c              动作层：命名命令（文字/快捷键/回调名）+ 加速键表 + 启用与勾选同步
      cg_app.c                 应用生命周期（定位 .cgui / 打开 / 消息循环）与 --selftest
      cg_log.c                 日志：stderr 镜像 + 日志文件（GUI 版没有控制台时唯一的线索）
      cg_timer.c               一次性定时器
    src\win32\                 Win32 原生后端（窗口、控件、消息映射、对话框、托盘、诊断弹窗）
      cg_win_*.c               14 个子模块，共用 cg_win_internal.h
    src\skin\                  自绘外观层（GDI+，M3 风格）
      cg_skin.c                按钮/开关/勾选/滑块/进度/圆环
      cg_skin_edit.c           输入框圆角裁切与单行文字垂直居中
    src\kit\                   平台服务层：跟界面无关的那一半（文件/路径/INI/编码/剪贴板/注册表）
      cg_kit.c                 基础能力 API（cg_kit）：业务代码不必 include windows.h
    res\                       清单（v6 通用控件 + 高 DPI）与版本信息
  examples\                    仓库自带的示例
    lesson01_basics\           教学示例 01：基础与容器（按钮/图标钮/开关/徽章/容器）
    lesson02_inputs\           教学示例 02：输入与选择（输入框/复选/单选/下拉/日期/月历）
    lesson03_values\           教学示例 03：数值与指示（滑块/进度/圆环/仪表盘/状态灯/评分）
    lesson04_data\             教学示例 04：数据与日志（标签页/树/表格/终端/图表/分隔条）
    lesson05_commands\         教学示例 05：命令与图形（菜单/工具栏/画板/外形/图片/网页）
    lesson06_window\           教学示例 06：主窗口与窗口事件（app.* 配置 / 26 种窗口事件 / 程序信息）
    教学使用说明.md             六课路线图 + 每课详解 + 通用知识（学这套示例先读它）
    cppdemo\                   C++ 例程（main.cpp：STL 管数据 + 类管状态，构建自动走 g++）
    gallery\                   控件陈列室·三合一（gallery.cgui / main.c / assets\ / 产物）
    notepad\                   真实小工具范例：记事本（菜单/文件/配置/cg_kit 全套）
    serialterm\                串口助手（对标 XCOM：终端日志 + hex/时间戳/按帧 + 定时/多条发送 + 文件收发）
    netterm\                   网络调试助手（TCP/UDP + 终端日志 + 角色切换）
    darkterm\                  深色主题示例（app.dark + 命名样式 + 运行时切换主题）
    modbus\                    Modbus/TCP 上位机（cg_modbus_* 拼帧解帧 + cg_frame_* 切帧 + mock-slave.mjs 模拟从站）
    hextool\                   进制与校验工具箱（HEX/文本互转 + 大小端 + CRC16/CRC32/MD5/SHA + Base64/URL + 报文组装）
    filecheck\                 文件核对工具（两个文件的大小与五种摘要比对 + 逐字节差异定位 + 报告导出）
    traytool\                  托盘常驻小工具（托盘图标 + 关窗即最小化 + 5 条全局快捷键 + 剪贴板 + 截图 + 切深色）
    shottool\                  截图工具（托盘 + 全局热键 Ctrl+Alt+A + 屏幕框选 + 矩形/椭圆/箭头/画笔/文字标注 + 存图/复制/贴图 + Ctrl+Alt+G 的 GIF 录屏）
    syspanel\                  系统与配置面板（系统位数/版本/磁盘占用/环境变量/进程存在性/串口枚举 + INI 读写）
    datarec\                   采集记录器（模拟数据源 + 实时双曲线 + CSV 分块落盘 + 回放 + 内存上限保护）
    logfilter\                 日志过滤器（大日志关键字过滤 + 行号表 + 导出，说清"整读 + 32 MB 上限"的能力边界）
    dataview\                  数据浏览器（大 CSV 分页浏览：只索引不复制、界面只渲染当前页、排序 / 统计 / 导出）
    envsnap\                   环境快照（懒加载的树：展开才采集 + 环境变量清单放 INI 可增删改查 + 导出 / 复制）
    htmlreport\                HTML 报告器（CSV → 离线可看的 HTML：内嵌 CSS / 无 JavaScript / 单元格转义；网页控件预览 + 系统浏览器兜底）
    framedec\                  帧协议解析器（自定义帧：切帧 / 三种校验 / 解码，噪声与残帧都给明确结论）
    settings\                  设置持久化（INI / 注册表 HKCU / 用户数据目录 三个后端并排对比，含注册表往返自测）
    encconv\                   编码与换行转换器（GBK ↔ UTF-8 + 换行统一：先按严格 UTF-8 校验判定，超限只跳过不截断）
    drawpad\                   画板（拖拽作图 / 撤销重做 / 导出 PNG；讲清"图形是被录制的绘制命令"）
    palette\                   调色板（取色 + HEX/rgb/hsl/相对亮度/对比度换算 + 8 格调色板 + 导出 CSS）
    toolbox\                   工具箱启动器（8 页签按钮墙 + 分组条 + 成对装/撤开关 + 搜索 + 最近使用 + 我的工具 INI 槽位 + 右键菜单扫描/装撤 + 深浅主题，对标 mxx1-toolbox）
  apps\                        你自己的工程（画布导出的默认位置）
    README.md                  这个目录是干嘛的
  build-runtime.bat            编译运行时库
  build-app.bat                编译某个工程 -> 落在它自己的目录里
  build-all.bat                运行时 + 所有示例，一条命令
  make-release.bat             一键打发布包 -> release\（内含自检）
  packaging\                   发布包里的物料模板（编译脚本、使用者说明）
  tests\                       C 层单元测试（套件自动注册，不用维护清单；入口 tools\run-tests.bat）
    cg_test.h                  极简框架：断言 + CG_TEST_SUITE 自注册
    cg_test_main.c             运行器：收集套件、--list / --grep / 汇总退出码
    cg_test_memcheck.c         --memcheck 的 malloc/free 记账（ld --wrap）
    unit\test_json.c           JSON 解析：取值兜底 / 转义 / 颜色 / 错误路径
    unit\test_doc.c            .cgui 解析：窗口属性 / 控件树 / 事件名 / 结构化内容 / 越界修正
    unit\test_kind.c           类型表与事件掩码、事件名表的双向契约
  tools\                       开发期检查工具
    run-tests.bat              C 层单测入口（编译 tests\ 并运行；可 --memcheck / --grep）
    check-format.bat           格式契约回归（画布写的 .cgui 必须被 C 侧读懂）
    bat-utf8.ps1               检查/修复仓库 bat 的编码与 ASCII 前导（UTF-8 + CRLF，无 BOM）
    gen-launcher.bat           生成工程级启动脚本（不含 goto/标签，UTF-8 前导 + 版本标记幂等）
    check-support.mjs          画布「支持库」表 vs cstackgui.h 的一致性检查
    check-comments.mjs         C 函数注释规范门禁（runtime/ tests/ examples/ 的 @brief/@param/@return）
   apply-comments.mjs         按"补丁块"批量落盘注释改动（并行起草后一次写入，含"只改注释"复核）
    check-kit.bat              cg_kit 基础能力冒烟测试（编译并运行 kit-test.c）
    kit-test.c                 上面那个测试的源码（文件/配置/编码/剪贴板/注册表）
    format-roundtrip.mjs       .cgui 往返：画布改 -> C 读 -> 再写回
    gen-cc.mjs                 给业务代码生成 clangd 配置（编辑器补全）
    shot.ps1 / shot-canvas.ps1 截 C 程序窗口 / 截画布窗口（肉眼看外观用）
  .codebuddy\                  CodeBuddy AI 配置（技能、工作流，见第三章）
  build\                       纯构建目录：各 .o 和运行时库源头（可随时删）
  release\                     发布包（make-release.bat 的产物，gitignore）
```

### 4.2 流程图

**一条业务程序的生命线**（你日常走的主流程）：

```
┌─────────────┐   Ctrl+S    ┌──────────────┐  运行时读取  ┌──────────────────┐
│  画布(设计)   │ ──────────> │  xxx.cgui    │ ──────────> │  C 运行时(Win32)   │
│ 拖控件/绑事件 │             │ 界面的唯一真相 │             │ 解析→原生控件→窗口  │
└─────────────┘             └──────────────┘             └────────┬─────────┘
       ↑                           │ 改界面不用重编译                │ 事件(click/change…)
       │ 导出 C 骨架(只生成一次)     ↓                              ↓
┌──────┴──────────┐        ┌──────────────┐             ┌──────────────────┐
│ xxx_ui_events.c │ ─────> │ libcstackgui.a│ ──链接──>  │ 回调: 按 .cgui 里  │
│ 你写的业务逻辑    │  编译   │  运行时库      │            │ 写的函数名查表调用  │
└─────────────────┘        └──────────────┘             └──────────────────┘
```

**运行时内部的数据流**（改运行时前先看这张）：

```
xxx.cgui (JSON)
   │ cg_doc_parse()                     ← 字段语义的真相在这（k_kinds 类型表）
   ↓
cg_node_t 树 ──build_node()──> cg_widget_t 树（状态 + 回调函数指针）
   │                                     │
   │                              create_native_tree()  ← 父先于子，照树序建 HWND
   ↓                                     ↓
Win32 消息 (WM_COMMAND/WM_NOTIFY/WM_DRAWITEM…)
   │ notify_from() / notify_common() / FocusProc   ← 原生通知翻译成事件（映射表见 cg_win_notify.c）
   ↓
cg_dispatch(w, ev) ──> 按 .cgui 里绑的函数名查表 ──> 你的回调（self, ev, user）
```

**事件绑定机制**一句话版：`.cgui` 里存的是**函数名字符串**，运行时按名字在
`CG_BIND("名字", 函数)` 表里查指针——画布和 C 代码彻底解耦，所以事件名写错编译不报错，
靠 `--selftest`（非 0 退出）和运行时 stderr 警告兜底。

### 4.3 常见问题

先看这张**排查表**（现象 → 先查什么，按顺序查）。它的价值在"这类问题别从头猜"：
下面每一条都是这个工程真实踩过的坑，指的位置就是真相所在的地方。

| 现象 | 先查什么 |
| --- | --- |
| 改了界面 / 代码，跑起来没变化 | ① stderr 里 `[cstackgui] 界面文件: ...` 是哪个文件（`.cgui` 运行时才读，不改它就别指望界面变）② exe 是不是刚编的（`build-app.bat <工程>` 是增量编译，按时间戳）③ 改的是 `runtime\` 就看是不是链了旧 `libcstackgui.a`（正常 `build-app.bat` 会自动刷库，`CSTACKGUI_SKIP_RUNTIME=1` 会跳过） |
| 事件不触发 / 点了没反应 | ① `.cgui` 里的函数名与 C 侧 `CG_BIND("名字", 函数)` 是否**逐字**相同（不相等不报错，只在启动时汇总弹一次 + 写日志）② 该控件**是否支持**这个事件（画布只列支持的；绑错了 `--selftest` 返回 5）③ 控件是否被容器挡住、或不在当前标签页（`page`）④ 是否在 `--selftest` 下跑（自检不开窗，弹对话框之前先问 `cg_app_has_window()`） |
| 值 / 文字写进去没生效 | ① `cg_widget_set_value` 会按 `min`/`max` **夹取**（列表类控件的下标不走夹取）② 输入驱动型控件（`edit`，含多行与数值框）的顺序是"先落模型再 sync"③ 树节点要**全部插完**再 `cg_tree_expand`（插的过程中展开是空操作） |
| 绑定不同步 | ① 源控件 `id` 是否拼对（启动写警告日志、`--selftest` 返回 5）② 抄什么由**目标类型**决定（值型抄值 / 勾选型抄勾选 / 其余抄源的文字或数值）③ 同步**不触发**目标的 `change` 回调 —— 想在同步时做事，把回调绑在**源**上 |
| 中文显示成乱码 | ① 控制台/终端的代码页（仓库里的 `.bat` 一律 UTF-8 无 BOM + 头部重入 65001，见 `tools\bat-utf8.ps1`）② 宽字符格式化必须用 `%ls`（`%s` 是多字节）③ 业务文件按 UTF-8 存，转 GBK 用 `cg_utf8_to_gbk()`（方向写反就是乱码，但不报错） |
| 底色 / 圆角设了没用 | ① 系统控件（列表框 / 树 / 表格 / 标签页 / 月历 / 下拉框）不吃自绘外观，这是 Win32 固有限制 ② 标签不设 `bg` = **透明**，透出的是"背后的颜色"（`cg_win_behind_rgb`）③ 改完底色用像素采样复核，别只看代码（`tools\shot.ps1`） |
| 控件位置 / 尺寸不对 | ① 容器里的坐标是**相对容器**的 ② 菜单栏挂在窗口上没有矩形，它会占客户区顶部，运行时按 `WindowRect-ClientRect` 差值把窗口调高 ③ 高分屏有 DPI 缩放（`app->scale = dpi*100/96`），单位是逻辑像素 |
| `.cgui` 打开就报错 | ① `type` 拼错 → 整个文件加载失败并报出类型名 ② 非容器带 `children` 会被判错（能放子控件的只有 `group`/`box`/`card`/`tabs`）③ 颜色 6 位是 `#RRGGBB`、**8 位是 `#AARRGGBB`**（顺序写反不报错，只是颜色错位）④ 字段语义的真相在 `runtime\src\core\cg_doc.c`，不在画布 |
| 关了窗口崩溃 / 卡住 | ① 别在事件回调里立刻销毁控件，用 `cg_set_timer()` 延后 ② 长活别做在 `idle` 事件里 ③ 链接报 `Permission denied` = exe 还在跑，先结束进程再编 |
| 看不到任何输出 / 想确认程序状态 | GUI 版（`--release`）**没有控制台，stderr 不可靠**：看日志 `%LOCALAPPDATA%\<exe名>\app.log`（256KB 轮转成 `.old`），或自己 `cg_log()` 记一条 |

下面这些是更常见的问答：

**Q：事件名写错了，为什么编译不报错？**
绑定是"运行期按名字查表"，画布和 C 代码才能互不依赖。兜底：运行时 stderr 打印
`[cstackgui] 警告: .cgui 引用了回调 "on_xxx"，但 C 代码里没有 CG_BIND 它`；
或跑 `--selftest`，非 0 退出码明确报出来。

**Q：程序启动说"找不到界面文件"？**
按顺序找：命令行路径 → exe 旁边 → `exe目录\..\apps\<exe名>\<exe名>.cgui`，试过的路径都会打印。
也可以显式传：`apps\myapp\myapp.exe apps\myapp\myapp.cgui`。

**Q：改了界面，程序里没变？**
`.cgui` 是运行时读的，看 stderr 里 `[cstackgui] 界面文件: ...` 确认读的是哪个文件。

**Q：双击 exe 什么都没发生 / 程序闪退？**
发布版（`--release`）没有控制台，提示不会印在屏幕上，而是**写进日志 + 弹中文提示框**。
日志固定在 `%LOCALAPPDATA%\<exe名>\app.log`（例如 `C:\Users\你\AppData\Local\myapp\app.log`），
例如把地址栏粘进去就能打开。常见三类：界面文件没和 exe 放一起、`.cgui` 格式有问题、
某个事件绑的函数名在 C 代码里没 `CG_BIND`（这种情况程序能起来，但会弹一次提示列出名字）。

**Q：按钮圆角/背景色设了没用？**
按钮/开关/勾选/输入框/滑块/进度条/圆环已走 GDI+ 自绘外观层（`runtime\src\skin\`），圆角与配色生效；
系统控件（列表框/树/表格/标签页/月历/下拉框）仍不吃自定义外观，这是 Win32 原生控件的固有限制
（见 `docs/ARCHITECTURE.md` §8）。

**Q：打包出来的 CStackGUI.exe 说找不到 C 项目根？**
便携版会自动认（`PORTABLE_EXECUTABLE_DIR`）；兜底点工具条「定位项目根」选一次（会记住），
或 `set CSTACKGUI_HOME=D:\你的\CStackGUI`。
