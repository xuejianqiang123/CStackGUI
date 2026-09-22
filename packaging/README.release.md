# CStackGUI 使用说明

**拖拽画界面 → 存成 `.cgui` → C 写业务 → 编译出一个独立 exe。**

> 本目录是自包含发布包：画布 + 预编译好的运行时库 + 示例 + 编译脚本，不需要这个源码仓库。
> 整个目录拷给任何人都能用。

---

## 一、简介

CStackGUI 是一套「画布 + C 运行时」的桌面 GUI 开发方案，三句话讲完：

- 界面的长相在**画布**里拖出来，存成 `.cgui` 文件；**程序运行的时候才读它，不编译进去**——
  改界面不用重新编译 C。
- 业务逻辑是普通 C 文件，只写回调函数；**事件按控件特性分配**，画布只列出该控件真正支持的事件。
- 最终产物是**一个 exe，零依赖**：换台电脑复制过去就能跑。

**42 种控件、13 种事件**（按钮/输入/列表/树/表格/标签页/月历/时钟/画板/颜色选择/评分/分页……），
全部控件和事件的用法都能在自带示例 `examples\gallery\`（控件陈列室·三合一）里找到。

当前边界：按钮/开关/勾选/输入框/滑块/进度条走自绘外观层（吃圆角与配色），系统控件（列表框/树/表格/标签页）不吃；
窗口尺寸固定（绝对坐标，所见即所得）；网页浏览控件走系统 WebBrowser，标注为实验性。

---

## 二、开发环境搭建

### 2.1 需要装什么

| 你想干什么 | 需要装什么 |
| --- | --- |
| 只画界面、导出 `.cgui` | **什么都不用装**，双击 `CStackGUI.exe` |
| 写出能跑的 exe | MinGW-w64 的 gcc（见 2.3） |
| 写 C 时要补全 / 跳转 / 实时报错 | Node.js + clangd（见第三节 3.2） |

### 2.2 本目录里有什么

```
CStackGUI.exe          画布：拖拽设计界面、导出 C 骨架
build-app.bat          编译某个工程 -> exe（成功后自动刷新 clangd 配置）
compile_commands.json  clangd 语言服务配置（编译一次后自动生成，给编辑器用）
README.md              本文件
tools\
  gen-cc.mjs           生成 compile_commands.json（build-app.bat 会自动调，也可手动跑）
  check-format.bat     格式契约回归：画布写出的 .cgui 运行时一定能读
  format-roundtrip.mjs 上一条的实际测试脚本
  check-kit.bat        cg_kit 基础能力冒烟测试（文件/配置/编码/剪贴板/注册表）
  kit-test.c           上面那个测试的源码，可以照着当 cg_kit 用法示例看
  shot.ps1             跑一个界面并截图（调外观时肉眼验证用）
runtime\
  include\cstackgui.h  业务代码唯一需要 include 的头
  lib\libcstackgui.a   编译好的运行时库（不用自己编）
  lib\cstackgui_res.o  资源对象（v6 通用控件清单 + 版本信息 + 图标）
  res\                 manifest / 默认图标（工程的 <名字>.rc 会引用这里的清单）
examples\
  lesson01_basics\ ~ lesson05_commands\
                       教学示例：五课（基础与容器 → 输入与选择 → 数值与指示 → 数据与日志 → 命令与图形）
  教学使用说明.md       上面那五课怎么学（先读它）
  gallery\             示例：控件陈列室·三合一（67 个控件，全部控件+事件都用上了）
  notepad\             示例：真实小工具·记事本（菜单 / 文件 / 编码 / cg_kit 全套）
apps\                  你自己导出的工程会放在这里（现在还没有）
build\                 本包的中间产物（可随时删）
.codebuddy\
  skills\              CodeBuddy AI 的项目级技能（写 C 回调 / 手改 .cgui，见第三节）
```

### 2.3 只想画界面（不用装任何东西）

1. 双击 **`CStackGUI.exe`**。
2. 左侧「控件」面板里点一下（或按住拖到画布上）加控件；拖动手柄改大小。
3. 右侧「属性」里改 `id`、文字、位置尺寸、样式、初始状态。
4. 右侧「事件」里给控件填 **C 函数名**（点「自动」会按 `on_<id>_<事件>` 生成规范名字）。
   只有这个控件真正支持的事件才会列出来。
5. 想知道回调里能写什么，点右侧第 4 个页签「**支持库**」：全部 C 函数按用途分类列好，
   点一下看说明与示例，**双击就复制调用代码**，粘到 `_ui_events.c` 里即可。
6. 导出 C 骨架之后，事件行上的「**代码**」按钮能直接打开 `*_ui_events.c` 里那个函数
   （装了 VS Code 会用 `code -g` 直接定位到行），不用自己翻文件找。
7. `Ctrl+S` 存成 `.cgui`。

> 不想从空画布开始？点工具条「**新建**」会先让你挑模板：空白 / 表单 / 托盘小工具，
> 后两个已经把 id、中文文字、事件名都摆好了，照着改比从零画快得多。

界面随时可以拿回来改：`Ctrl+O` 打开 `.cgui`，改完再存即可。
**改界面不需要重新编译 exe**，因为 `.cgui` 是运行时读的。

### 2.4 要做出能跑的 exe

**装 gcc**：去 <https://winlibs.com> 或 MSYS2 装 MinGW-w64，命令行确认 `gcc --version` 能用；
不行就把它的 `bin` 加进 PATH，或 `set MINGW=D:\mingw64\bin`。

**设计界面并导出 C 骨架**：画好界面（记得在「事件」页签填好 C 函数名），点工具条
**「导出 C 骨架」**，推荐导到本包的 `apps\` 下（画布自动建 `apps\<界面标题>\`）。生成的文件：

| 文件 | 是谁的 | 要不要改 | 作用 |
| --- | --- | --- | --- |
| `<名字>.cgui` | 画布产物 | 别手改，回画布改 | 界面定义，运行时读它 |
| `<名字>_ui_events.h` | 生成，勿改 | 不要 | 回调原型 |
| `<名字>_ui_events.c` | **你的代码** | **主要就改它** | 每个事件一个空函数，等你填逻辑 |
| `main.c` | 生成 | 一般不动 | `main()` + 绑定表 |

**重新导出不会覆盖你已经写好的 `<名字>_ui_events.c`**，放心改界面再导出。

**填业务逻辑**：打开 `apps\<名字>\<名字>_ui_events.c`，把空函数填上：

```c
void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);                    /* 从控件拿到应用 */
    cg_widget_t *log = cg_find(app, "lst_log");         /* 用 id 找别的控件 */

    cg_widget_list_addf(log, "按钮被点了");              /* 往列表框写一条日志 */
    cg_widget_set_text(cg_find(app, "lab_status"), "已处理");
}
```

**编译与运行**（在本目录，有 `build-app.bat` 的那一层）：

```bat
build-app.bat <名字>              :: 产物是 apps\<名字>\<名字>.exe
apps\<名字>\<名字>.exe            :: 双击也行——程序自己找旁边的 .cgui
apps\<名字>\<名字>.exe --selftest :: 不开窗自检：真调一遍所有回调，名字错了非 0 退出
```

改完界面只要重新存 `.cgui` 直接重跑 exe；改了回调函数体才需要重新 `build-app.bat`。

> **工程放哪都行**：`CStackGUI.exe` 是单文件便携版。把它拷到任意目录，运行/导出时
> 会自动把 `runtime\` 和 `build-app.bat` 释放到 exe 旁边，工程目录也不再强制
> `apps\`——放在 exe 旁边的任意子文件夹（如 `main\`）都能直接编译。
> 点「运行」时没装 MinGW-w64 会弹窗引导你去下载页。

---

## 三、如何使用 CodeBuddy AI 开发当前工程？

本包自带 AI 项目技能（`.codebuddy\skills\`），用 **CodeBuddy 打开本目录**会自动加载——
你用日常的话提需求，AI 按本项目的规范和验证流程干活。

### 3.1 先把编辑器配好（clangd，一次性 5 分钟）

配好之后写 C 有**补全、跳转、实时报错**，AI 生成代码的质量也更高（它能读到真实诊断）。

1. 用 CodeBuddy（或 VSCode）**打开本目录**，扩展面板搜 **clangd** 安装插件；
2. 插件不自带 clangd 程序：按弹提示点 **Install**，或去
   <https://github.com/clangd/clangd/releases> 下载，验证 `clangd --version` 有输出；
3. 生成配置（二选一）：`build-app.bat gallery`（推荐，顺便验证环境）或 `node tools\gen-cc.mjs`；
4. 设置（`Ctrl+,`）搜 clangd，编辑 settings.json（路径按本机改）：

   ```json
   {
     "clangd.path": "C:\\clangd_19\\bin\\clangd.exe",
     "clangd.arguments": [
       "--query-driver=C:/mingw64/bin/gcc.exe",
       "--compile-commands-dir=."
     ]
   }
   ```

   `--query-driver` 让 clangd 去问 gcc 系统头文件在哪（否则 `windows.h` 一片标红）；
5. 重启窗口，打开 `examples\gallery\main.c` 验证：头文件不标红、输入 `cg_` 有补全、
   `Ctrl+点击` 能跳进 `cstackgui.h`。

Node.js（LTS）去 <https://nodejs.org> 装，第 3 步和回归测试需要 22+。

### 3.2 直接用大白话提需求

| 你说 | AI 会怎么做 |
| --- | --- |
| 「给确认按钮加个点击逻辑，点了往日志里写一条」 | 按 `cstackgui-dev` 技能里的 API 与验证流程改 `_ui_events.c`，改完自检 |
| 「界面加一个下拉框，选项是 ABC」 | 画布或 `.cgui` 里加控件（按 `cgui-edit` 的字段契约），告诉你重跑 exe 即可 |
| 「把窗口标题改成 XXX」 | 只动 `.cgui`，不碰 C 代码，提醒你不用重编译 |
| 「帮我看看为什么点了没反应」 | 先跑 `--selftest` 查事件名是否登记，再查控件 id 与绑定 |

**提需求的小技巧**：说清「哪个控件、什么行为、结果写到哪」，一次到位；
写完主动说「帮我审查一下」，AI 会按项目门禁自检。

### 3.3 AI 改完代码，必须过的门禁

无论 AI 还是人改了代码，这几条是硬门禁：

```bat
build-app.bat gallery                                            :: 编译通过
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui  :: 回 0 才算事件闭环
tools\check-format.bat                                           :: 手改/批量生成 .cgui 后必跑
tools\check-kit.bat                                              :: 用到 cg_kit（文件/配置/编码）时跑
```

---

## 四、工程详解

### 4.1 流程图

**一条业务程序的生命线**：

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
└─────────────────┘        └──────────────┘            └──────────────────┘
```

**事件绑定机制**一句话版：`.cgui` 里存的是**函数名字符串**，运行时按名字在绑定表里查函数指针
——画布和 C 代码彻底解耦，所以事件名写错编译不报错，靠 `--selftest` 和运行时 stderr 警告兜底。

### 4.2 有哪些控件、各支持哪些事件

| 控件 | 支持的事件 | 备注 |
| --- | --- | --- |
| 按钮 / 图标按钮 / 链接 / 标签片 | `click`、`dblclick` | 图标按钮用 `src` 指定图片 |
| 开关 / 复选框 / 单选钮 / 评分 | `change` | 开关是自绘的药丸形 |
| 单行输入 | `change`、`submit`、`gotfocus`、`lostfocus` | `submit` 是回车 |
| 多行输入 / 富文本 / 数字微调 / 日期时间 / 颜色选择 | `change` | |
| 列表框 | `select`、`dblclick`、`gotfocus`、`lostfocus` | |
| 下拉框 | `select` | |
| 滑块 | `change`、`release` | 拖动中给 `change`，松手给 `release` |
| 标签页 | `select` | 子控件用 `page` 指定属于第几页 |
| 树 | `select`、`dblclick`、`expand`、`collapse` | 展开/折叠时能拿到是哪个节点 |
| 表格 | `select`、`dblclick`、`check` | `.cgui` 里开 `check`/`multi` 可加勾选列与多选 |
| 月历 | `select`、`dblclick` | 选中的日期写回 `text` |
| 菜单栏 / 菜单按钮 | `click` | 用 `cg_menu_last_id()` 区分点的是哪一项 |
| 工具栏 | `click` | 用 `cg_widget_value()` 拿是第几个按钮 |
| 时钟（timer） | `tick` | `period` 毫秒一跳，回调里周期性干活 |
| 画板（canvas） | `click`、`dblclick` | 用 `cg_canvas_line/rect/ellipse/text` 画 |
| 徽章 / 进度条 / 圆环 / 状态栏 / 分割线 / 容器 / 外形框 | 无 | 只用来显示，或被程序写入 |

**窗口**本身有一个 `close` 事件（点关闭按钮前触发），在画布右侧「文档」页签里绑，
回调里 `cg_cancel_close()` 能拦下一次关闭。

### 4.3 常见问题

**Q：改完界面，exe 里没变化？**
`.cgui` 是运行时读的，确认程序读的是你改的那个文件（启动时 stderr 会打印 `[cstackgui] 界面文件: ...`）。

**Q：启动说"找不到界面文件"？**
它会依次找：命令行给的路径 → exe 旁边 → `exe目录\..\<apps|examples>\<exe名>\<exe名>.cgui`，
最后把试过的路径都打印出来。按提示放文件，或者显式传路径。

**Q：事件名写错了，为什么编译不报错？**
绑定是"运行期按名字查表"，画布和 C 代码才能互不依赖。兜底有两个：运行时 stderr 警告，
或 `--selftest` 非 0 退出。

**Q：按钮圆角、自定义背景色设了没用？**
按钮/开关/勾选/输入框/滑块/进度条是自绘的（走 GDI+ 外观层），圆角与配色生效；
系统控件（列表框、树、表格、标签页、月历、下拉框）由 Windows 主题画，不给自定义外观，这是原生控件的固有限制。

**Q：双击 exe 什么都没发生，或者程序一闪就没了？**
编译出的发布版**没有控制台窗口**，所以出错信息不会印在屏幕上，而是做两件事：
弹一个中文提示框说明原因，并把详细过程写进日志文件：

```
%LOCALAPPDATA%\<exe名>\app.log      例如 C:\Users\你\AppData\Local\myapp\app.log
```

把地址栏粘到资源管理器里就能打开。最常见的三种：
① 界面文件和 exe 不在同一个目录（`.cgui` 是运行时读的，必须放在旁边）；
② `.cgui` 文件格式坏了（画布打开一次再保存即可修好）；
③ 事件绑的函数名在 C 代码里没 `CG_BIND` —— 这种程序还能起来，但启动时会弹一次提示，
   把没登记的名字列出来（这些事件点了不会有反应）。

自己写的代码也可以往同一个日志里记东西：`cg_log(CG_LOG_INFO, "打开了 %s", path);`

**Q：`build-app.bat` 说找不到 gcc？**
见 2.4，装 MinGW-w64 或设 `MINGW` 环境变量。

**Q：`windows.h` 还是标红？** clangd 的 `--query-driver` 路径不对，或没重启窗口。
**Q：提示 compile_commands.json not found？** 先跑一次 `build-app.bat gallery`。

### 4.4 画布里的「支持库」

右侧「支持库」页签把运行时的全部 C 函数按用途列成三棵树（运行时核心 / 界面控件 / 基础能力 →
分类 → 函数），共 150 条。顶部搜索框既能搜函数名（`cg_find`）也能搜中文（`消息框`、`表格`、
`定时器`、`读配置`），**搜分类名会把那一类整棵展开**。点函数看帮助卡片（原型 / 说明 / 参数 /
返回 / 示例），双击行或点「复制」把调用代码放进剪贴板。

「基础能力」那一棵是 `cg_kit`：文件读写、路径、INI 配置、UTF-8↔GBK、剪贴板、用默认程序打开、
启动外部程序、注册表、写日志 —— **这些都不用 include `windows.h`**，只认 `cstackgui.h`。

### 4.5 示例

发布包里带两个示例：

- `examples\lesson01_basics` ~ `lesson05_commands` —— **五课教学示例**：基础与容器 → 输入与选择 →
  数值与指示 → 数据与日志 → 命令与图形，每课只讲一个主题、只出现本课主题的控件。
  **第一次用就从这里开始**，
  路线图与逐课详解见 `examples\教学使用说明.md`。
- `examples\gallery\` —— **控件陈列室**：67 个控件、47 个回调，三页标签页，当"控件用法字典"翻。
- `examples\notepad\` —— **真实小工具范例（记事本）**：菜单分发、状态栏刷新、文件读写、
  GBK/UTF-8 识别、INI 记住上次文件与窗口尺寸、关闭前拦截。想写"真东西"时照它抄结构最省事。

都能直接编译运行：`build-app.bat lesson01_basics` / `build-app.bat gallery` / `build-app.bat notepad`。

```bat
build-app.bat gallery
examples\gallery\gallery.exe
```

`examples\gallery\` 是控件陈列室·三合一：顶层标签页分「基础闭环 / 控件陈列 / 新特性」
三页，全部控件和事件都在里面，想找"某个控件怎么用"看它最快。
`main.c` 里按页分节，注释说明每个回调在演示什么。
