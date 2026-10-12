# 数据浏览器：分页看大 CSV，几万行也不卡

> 目录 `examples\dataview\`　编译 `build-app.bat dataview`　运行 `examples\dataview\dataview.exe`　自检 `dataview.exe --selftest examples\dataview\dataview.cgui` → 控件 18 个 / 回调 10 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
只教一件事：**几万行数据不许一次塞进表格**。整份 CSV 只读一遍，把每行的「偏移 + 长度」记进一张固定数组（**不复制行内容**），再分页（每页 200 行）+ 按需渲染 —— 表格里永远只有当前页；排序也只排那张偏移数组。补的能力面是「大文件分页浏览 + 表格按需填充 + 单元格就地编辑」。
有意留的简化：CSV 只认逗号与换行，不处理引号转义、不处理字段内换行、不转码；文件超 16 MB 只解析前一段。对着采集表翻页找行、核对列值很实用。

## 二、怎么用
1. 点「打开 CSV...」选一个 CSV / 文本文件（超索引上限只读前一段）。
2. 表格 `tbl_data` 只显示当前页，底部 `pg_main` 翻页。
3. 排序：在「排序列」下拉选列 → 按需勾「降序」→ 点「排序」（排序是显式按钮，不是双击）。
4. 统计：在「统计列」下拉选列 → 点「统计」，看这一列「当前页」与「全部行」并排算出的差异。
5. 编辑：双击表格里任意一格，弹输入框（原值预填、全选），回车即写回 —— **只改内存，原文件不动**。
6. 导出：「导出当前页」或「导出全部行」，攒块写盘。
7. 命令行：`dataview.exe`（自动找同名 `.cgui`）、`dataview.exe xxx.cgui [要打开的.csv]`、`dataview.exe 某个.csv`（界面自动找，启动即打开它）。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `btn_open` | button | 打开 CSV（超上限只读前一段） |
| `btn_export_page` / `btn_export_all` | button | 导出当前页 / 全部行 |
| `btn_stats` | button | 对「统计列」做当前页与全部行并排统计 |
| `cbo_sort` / `cbo_stat` | combo | 选排序列 / 统计列 |
| `chk_desc` | checkbox | 勾上 = 降序（只影响点「排序」时的方向） |
| `btn_sort` | button | 按「排序列」重新排 |
| `tbl_data` | table | 当前页数据；双击任意格就地编辑 |
| `pg_main` | pagination | 分页翻页 |
| `edt_report` | edit | 只读报告区（载入限制 / 排序结论 / 统计） |
| `sb_main` | statusbar | 三段状态 |

## 四、关键接口
`cg_file_size` 先量大小判断是否超限；`cg_file_read_bytes` 整读一次拿全部字节（唯一堆内存，用完还）；`cg_table_begin_update` / `cg_table_end_update` 包住翻页的一批行；`cg_table_clear` / `cg_table_add_column` / `cg_table_add_row` 建表与填页；`cg_table_last_col` 双击时问列号；`cg_input_box` 弹单行输入框编辑单元格；`cg_file_write` / `cg_file_append` 攒块导出；`cg_status_set` / `cg_status_setf` 写状态栏。

## 五、容易踩的坑
- **当前页 ≠ 全部行**：「统计」把两者并排算，就是让「只看当前页就下结论」当场显形。
- CSV 只认逗号与换行：不处理引号转义、不处理字段内换行；别的程序另存的 ANSI/GBK 中文会乱码。
- 翻页是「先清空再逐行填」，必须用 `cg_table_begin_update` / `cg_table_end_update` 包住 —— 不包就是"填 200 行 = 整表重建 200 次"，界面会一直闪。
- 编辑只改内存、绝不回写原文件；新值含逗号 / 引号 / 换行会被拒绝 —— 不做引号转义就写会把行结构搞坏。
- 全部固定数组、没有任何 malloc/free：行数 / 列数 / 单元格长度超了会明说，不悄悄丢。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · dataview」。
- `docs\CGUI-FORMAT.md` → `table` / `pagination` / `combo` 的字段与事件（第 2 节、第 4 节）。
