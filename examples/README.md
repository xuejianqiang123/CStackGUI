# examples\ —— 示例与教学工程

这里的工程分三类（本文件的索引由 `node tools\check-examples.mjs --ex-index` 生成、
由 `--docs` 门禁回校，**别手改索引块**）：

- **教学六课**：按顺序读，控件分类与用法的地基；
- **合并套件**：21 个实用工具并成 4 个多页签工程 —— 一个套件编出全部工具，
  每个工具的完整逻辑仍在自己的模块里（`modules\<缩写>.c`），照抄价值零损失；
- **独立工程**：gallery（控件陈列室）、toolbox（系统工具箱启动器）、cppdemo（C++ 示例）。

<!-- ex-index:begin（由 node tools\check-examples.mjs --ex-index 生成，别手改）-->

## 教学六课（`examples\lessons\`）

**顺序即内容**（前一课的概念是后一课的地基），每课都是能单独编译运行的小工程：

| 课 | 目录 | 主题 |
| --- | --- | --- |
| 入门 01 | `examples/lessons/lesson01_basics` | 基础与容器 |
| 入门 02 | `examples/lessons/lesson02_inputs` | 输入与选择 |
| 入门 03 | `examples/lessons/lesson03_values` | 数值与指示 |
| 入门 04 | `examples/lessons/lesson04_data` | 数据与日志 |
| 入门 05 | `examples/lessons/lesson05_commands` | 命令与图形 |
| 入门 06 | `examples/lessons/lesson06_window` | 主窗口与窗口事件 |

## 合并套件（`examples\suites\`）

21 个工具并成 4 个**多页签工程**：main 只做入口（拼绑定表 / 窗口事件分发器），
每个工具的完整逻辑在自己的模块里（照抄价值零损失）。编译一个套件 = 编出全部页签。

| 套件 | 页签（工具） | 编译 | 运行 / 自检 | 逐工具文档 |
| --- | --- | --- | --- | --- |
| `comm` | serialterm / netterm / darkterm / modbus / framedec | `build-app.bat suites\comm` | `examples\suites\comm\comm.exe` | `suites\comm\docs\*.md`（5 份） |
| `dev` | palette / drawpad / hextool / encconv / calculator | `build-app.bat suites\dev` | `examples\suites\dev\dev.exe` | `suites\dev\docs\*.md`（5 份） |
| `sys` | shottool / traytool / syspanel / envsnap / settings | `build-app.bat suites\sys` | `examples\suites\sys\sys.exe` | `suites\sys\docs\*.md`（5 份） |
| `text` | notepad / logfilter / filecheck / dataview / htmlreport / datarec | `build-app.bat suites\text` | `examples\suites\text\text.exe` | `suites\text\docs\*.md`（6 份） |

## 独立工程（一个工程就是它自己，不并套件）

| 工程 | 目录 | 演示 |
| --- | --- | --- |
| `cppdemo` | `examples\cppdemo` | C++ 例程 · 数据与界面分开 |
| `gallery` | `examples\gallery` | CStackGUI 控件陈列室 |
| `toolbox` | `examples\toolbox` | 工具箱启动器 1.1.0 |
<!-- ex-index:end -->
