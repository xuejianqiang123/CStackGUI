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

**39 种**，全部是真能用的原生/自绘控件（点得动、能输入、能改状态、能绑事件）：

| 分类 | 控件 |
| --- | --- |
| 基础 | 按钮 / 图标按钮 / 标签 / 链接 / 开关 |
| 输入 | 单行输入 / 多行输入 / 富文本 / 数字微调 / 日期时间 / 月历 / 复选框 / 单选钮 / 列表框 / 下拉框 |
| 数值 | 滑块 / 进度条 / 圆环进度 |
| 数据 | 标签页 / 树 / 表格（多选、勾选列、行数值、行图标）/ 状态栏 |
| 菜单 | 菜单栏 / 菜单按钮 / 工具栏 |
| 容器 | 分组框 / 容器块 / 卡片 |
| 其它 | 分割线 / 图片 / 外形框 / 时钟 / 画板 / 颜色选择 / 网页浏览（实验性） |
| 装饰 | 徽章 / 标签片 / 评分 / 分页 |

事件 13 种：`click / dblclick / change / submit / select / release / expand / collapse /
tick / check / gotfocus / lostfocus`（+ 窗口级 `close`）。
每种控件支持哪些事件见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md)。

界面之外还有一套 **基础能力 API**（`cg_kit`）：读写文件、INI 配置、路径处理、
文本与编码转换（UTF-8 ↔ GBK）、剪贴板、用默认程序打开文件/网址、启动外部程序、
读写注册表（做开机自启）、写日志。**所以业务代码从头到尾只 include `cstackgui.h`**，
不用为了读个配置去 include `windows.h`。这 30 个函数的用法在画布「支持库」页签里一查就有。

仓库自带三类示例，分工不同：

- `examples\lesson01_edit_button` ~ `examples\lesson05_todo` —— **五课教学示例**，从"编辑框 + 按钮"
  一路学到"菜单 + 文件 + 配置"的小工具，每课只讲一个主题、都能单独编译运行。
  **入门从这里开始**，路线图与逐课详解见 [examples\教学使用说明.md](examples/教学使用说明.md)。
- `examples\gallery\` —— **控件陈列室（三合一）**：顶层标签页分「基础闭环 / 控件陈列 / 新特性」
  三页，67 个控件、47 个回调，**讲"控件怎么用"**（当"控件字典"翻）。
- `examples\notepad\` —— **真实小工具范例（记事本）**：菜单分发、脏标记、状态栏刷新、
  文件读写、GBK/UTF-8 识别、INI 记住上次文件与窗口尺寸、关闭前拦截。**讲"一个工具该怎么组织"**，
  也是 `cg_kit` 基础能力库的活例子（全程没有 include 任何 Windows 头）。

### 1.3 当前边界

- 运行时以 Win32 原生控件为底：稳、零依赖、输入法无障碍白送。按钮/开关/勾选/输入框/滑块/进度条/圆环
  走 **GDI+ 自绘外观层**（`runtime\src\skin\`），吃圆角与配色；列表框/树/表格/标签页等系统控件不吃。
- 窗口尺寸固定，没有布局引擎（绝对坐标，所见即所得，拉伸不重排）。
- 画布目前是单选，没有多选、组合、对齐工具条（有网格吸附和邻近参考线）。
- 菜单是声明式的：能读结果（`cg_menu_last_id()`），但没有运行时增删菜单项的 API。
- 网页浏览控件走系统 WebBrowser（IE 内核），标注为实验性。

设计细节见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，界面文件字段见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md)。

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
5. **导出 C 骨架**：工具条「导出 C 骨架」→ 选 `apps\` 目录。生成 5 个文件：
   `<名字>.cgui`（画布产物）、`<名字>_ui_events.h/.c`（**.c 是你的代码，主要改它**）、
   `main.c`、`build.bat`。**重新导出不会覆盖你写过的 `_ui_events.c`**。
6. **填逻辑、编译**：在项目根目录跑：

   ```bat
   build-app.bat myapp
   ```

   产物落在工程自己的目录里，双击即跑：

   ```
   apps\myapp\
     myapp.exe           <- 双击就能跑（界面文件就在旁边）
     lib\libcstackgui.a  <- 运行时库副本，整个目录拷走也能重编译
     build-app.bat       <- 双击就能重编译这一个工程
   ```

7. **运行 / 自检**：

   ```bat
   apps\myapp\myapp.exe                                  :: 双击也行（自动找旁边的 .cgui）
   apps\myapp\myapp.exe --selftest apps\myapp\myapp.cgui :: 不开窗，解析 + 真调一遍所有回调
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

此外还有 14 个嵌入式审查技能（内存/中断/协议……）会在提交前审查时按改动领域自动调用。

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
    src\codegen.ts             导出 C 骨架
    electron\main.mjs          文件读写、导出、菜单、项目根记忆（便携版自动识别）
  runtime\                     C 运行时库
    include\cstackgui.h        业务代码唯一需要 include 的头
    src\core\                  后端无关层（JSON / 文档 / 控件模型 / 事件 / 生命周期）
      cg_json.c                只读 JSON 解析
      cg_doc.c                 .cgui -> 控件文档树（解析 / 释放 / 打印）
      cg_doc_kind.c            控件类型表 k_kinds（含每种控件支持的事件）
      cg_event.c               事件名表、回调绑定解析、事件派发
      cg_widget.c              运行时控件模型：构建、查找、属性 API
      cg_widget_data.c         内容 API：列表/树/表格/状态栏/标签页/图片/提示/时钟
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
    lesson01_edit_button\      教学示例 01：编辑框和按钮（最小闭环）
    lesson02_controls\         教学示例 02：常用输入控件（表单 / 容器）
    lesson03_events\           教学示例 03：窗口事件 / 时钟 / 键盘 / 鼠标 / 关闭拦截
    lesson04_data\             教学示例 04：列表框 / 树 / 表格
    lesson05_todo\             教学示例 05：综合练习·待办清单（菜单 + 文件 + cg_kit）
    教学使用说明.md             五课路线图 + 每课详解 + 通用知识（学这套示例先读它）
    gallery\                   控件陈列室·三合一（gallery.cgui / main.c / assets\ / 产物）
    notepad\                   真实小工具范例：记事本（菜单/文件/配置/cg_kit 全套）
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
   │ notify_from() / notify_common() / FocusProc   ← 原生通知翻译成 13 种事件
   ↓
cg_dispatch(w, ev) ──> 按 .cgui 里绑的函数名查表 ──> 你的回调（self, ev, user）
```

**事件绑定机制**一句话版：`.cgui` 里存的是**函数名字符串**，运行时按名字在
`CG_BIND("名字", 函数)` 表里查指针——画布和 C 代码彻底解耦，所以事件名写错编译不报错，
靠 `--selftest`（非 0 退出）和运行时 stderr 警告兜底。

### 4.3 常见问题

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
