# 入门 04 · 数据与日志：组织、刷新、看得见

> 目录 `examples\lessons\lesson04_data\`　编译 `build-app.bat lesson04_data`　运行 `examples\lessons\lesson04_data\lesson04_data.exe`　自检 `lesson04_data.exe --selftest examples\lessons\lesson04_data\lesson04_data.cgui` → 控件 23 个 / 回调 18 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
前几课都是"一个控件一个值"，这一课装的是**一组数据**，外加一个**自己会刷新**的日志区。四种"装东西"的控件各自一套「加 / 清 / 数 / 读 / 选中」的 API，记住这张表就不会找错函数。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson04_data`，双击 `examples\lessons\lesson04_data\lesson04_data.exe`。
2. 切左侧标签页看列表框 / 树 / 表格 / 图表；点「加节点」「加行」看控件怎么追加内容。
3. 拖中间的分隔条，看右侧终端与左侧内容的变化；等时钟控件往终端写带时间戳的行。

## 关键控件一览（本课 23 个控件 / 18 个回调）
| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `lst_items` | `listbox` | 加 / 清 / 数 / 读 / 选中一套 |
| `tre_dir` | `tree` | `cg_tree_add(树, parent, 文字)`，`parent = -1` 加在根上 |
| `tbl_data` | `table` | 加行 / 加列；行勾选是运行时 API |
| `cht_line` / `cht_bar` | `chart` | `cg_chart_push` 推点，`items` 是系列名 |
| `sp_main` | `splitter` | 天生"两栏"语义，拖它只改前后两块 |
| `term_log` | `terminal` | `cg_term_write` 写行，三色与时间戳 |
| `tmr_beat` | `timer` | `period` + `events.tick` |

## 重点看哪几处
- **表格的行勾选 / 行数值 / 行图标不是 `.cgui` 字段**，是运行时 API；勾选变化发的是 `check` 事件，不是 `select`。
- **`splitter` 只改"紧挨着的前一块" + "同容器最后一块"**，夹在中间的兄弟只被平移；运行时没有布局引擎，容器变窄不会让固定尺寸子控件跟着缩。
- **`timer` 是一次性的**，周期性任务要自己在回调末尾重挂。
- 终端写日志：一次调用 = 一段数据（里面带换行会自动切行），行数上限由 `value` 控制。

## 详细讲解
见 `docs\articles\04-数据与日志.md`，以及 `examples\教学使用说明.md` 的「入门 04」小节。
