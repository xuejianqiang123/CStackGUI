# examples\ —— 示例与教学工程

这里的工程分三类（本文件的索引由 `node tools\check-examples.mjs --ex-index` 生成、
由 `--docs` 门禁回校，**别手改索引块**）：

- **教学六课**：按顺序读，控件分类与用法的地基；
- **合并套件**：实用工具并成几个多页签工程（数量见下表）—— 一个套件编出全部工具，
  每个工具的完整逻辑仍在自己的模块里（`modules\<缩写>.c`），照抄价值零损失；
- **独立工程**：gallery（控件陈列室）、toolbox（系统工具箱启动器）、cppdemo（C++ 示例）、
  calc（深色科技感计算器：上输公式下出结果 + 文本长度统计）。

<!-- ex-index:begin（由 node tools\check-examples.mjs --ex-index 生成，别手改）-->

## 教学六课（`examples\lessons\lessons\` · 一个多页签套件）

**顺序即内容**（前一课的概念是后一课的地基），六课并进一个多页签套件，每课一页签、一个模块：

| 页 | 模块 | 主题 |
| --- | --- | --- |
| 01 | `examples\lessons\lessons\modules\l1.c` | 基础与容器 |
| 02 | `examples\lessons\lessons\modules\l2.c` | 输入与选择 |
| 03 | `examples\lessons\lessons\modules\l3.c` | 数值与指示 |
| 04 | `examples\lessons\lessons\modules\l4.c` | 数据与日志 |
| 05 | `examples\lessons\lessons\modules\l5.c` | 命令与图形 |
| 06 | `examples\lessons\lessons\modules\l6.c` | 主窗口与窗口事件 |

编译 `build-app.bat lessons\lessons`，运行 `examples\lessons\lessons\lessons.exe` —— 六课在六个页签里。

## 合并套件（`examples\suites\`）

21 个工具并成 2 个**多页签工程**：main 只做入口（拼绑定表 / 窗口事件分发器），
每个工具的完整逻辑在自己的模块里（照抄价值零损失）。编译一个套件 = 编出全部页签。

| 套件 | 页签（工具） | 编译 | 运行 / 自检 | 逐工具文档 |
| --- | --- | --- | --- | --- |
| `studio` | serialterm / netterm / darkterm / modbus / framedec / palette / drawpad / hextool / encconv / calculator / notepad / logfilter / filecheck / dataview / htmlreport / datarec | `build-app.bat suites\studio` | `examples\suites\studio\studio.exe` | `suites\studio\docs\*.md`（16 份） |
| `sys` | shottool / traytool / syspanel / envsnap / settings | `build-app.bat suites\sys` | `examples\suites\sys\sys.exe` | `suites\sys\docs\*.md`（5 份） |

## 独立工程（一个工程就是它自己，不并套件）

| 工程 | 目录 | 演示 |
| --- | --- | --- |
| `calc` | `examples\calc` | 计算器 |
| `calculator` | `examples\calculator` | 计算器 · CStackGUI |
| `cppdemo` | `examples\cppdemo` | C++ 例程 · 数据与界面分开 |
| `gallery` | `examples\gallery` | CStackGUI 控件陈列室 |
| `toolbox` | `examples\toolbox` | 工具箱启动器 1.1.0 |
<!-- ex-index:end -->
